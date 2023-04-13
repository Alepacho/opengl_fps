#pragma once
#define GL_SILENCE_DEPRECATION

#include "map.hpp"
#include "math.hpp"
#include "debug.hpp"

// These constants control vertical movement:
const double gravity = -0.011, terminalvelocity = -2.0, jump = 0.18;

class Actor {
    public:
    XYZ<double> camera; // Where the actor is situated
    XYZ<double> dir;    // Where actor is looking (updated from look_angle, y=always zero)
    XYZ<double> up;     // What is the "up" direction for this actor
    Actor() : dir{{0, 0, 0}}, up{{0, 1, 0}} {}
    virtual ~Actor() {}

    template <typename Func>
    bool Render(Func &DrawWorld, double FoV, double aspect, double near = 1e-3) {
        // Decide upon how the viewport is to be projected.
        if (CheckGLError("Actor::Render Start")) return true;
        glMatrixMode(GL_PROJECTION); // Target matrix: Projection
        if (CheckGLError("Actor::Render 1")) return true;
        glLoadIdentity();            // Reset any transformations
        if (CheckGLError("Actor::Render 2")) return true;
        gluPerspective(FoV, aspect, near, 30.0);
        if (CheckGLError("Actor::Render 3")) return true;

        // Decide upon the manner in which the world is transformed from the
        // perspective of the viewport. In OpenGL, the camera never moves.
        // The world is simply rotated/scaled/shorn around the camera.
        glMatrixMode(GL_MODELVIEW); // Target matrix: World
        if (CheckGLError("Actor::Render 4")) return true;
        glLoadIdentity();           // Reset any transformations
        if (CheckGLError("Actor::Render 5")) return true;
        gluLookAt(camera.d[0], camera.d[1], camera.d[2], camera.d[0] + dir.d[0],
                camera.d[1] + dir.d[1], camera.d[2] + dir.d[2], up.d[0], up.d[1],
                up.d[2]);
        if (CheckGLError("Actor::Render 6")) return true;

        // Enable depth calculations to work on the new frame.
        glClear(GL_DEPTH_BUFFER_BIT);
        if (CheckGLError("Actor::Render 7")) return true;

        // Draw everything that should be rendered.
        DrawWorld(*this);
        if (CheckGLError("Actor::Render 8")) return true;

        // Tell OpenGL to render and display stuff.
        glFlush();

        if (CheckGLError("Actor::Render End")) return true;
        return false;
    }
};

class BlobActor : public Actor {
    public:
    enum SignalType { sig_push, sig_jump, sig_aim };

    double look_angle; // Angle of looking, around the Y axis
    double yaw;        // Angle of aiming, in Y direction, visual effect only

    XYZ<double> fatness; // Fatness of player avatar
    XYZ<double> center;  // Center of the player avatar
    XYZ<double> fluctuation;

    bool moving;       // Has nonzero velocity?
    bool ground;       // Can jump?
    double move_angle; // Relative to looking-direction, the angle
                        // in which the actor is trying to walk to
    XYZ<double> vel;   // Actor's current velocity
    int pushing;       // -1 = decelerating, +1 = accelerating, 0 = idle

    BlobActor()
        : fatness{{1, 1, 1}}, center{{0, 0, 0}}, moving(true), ground(false),
            move_angle(0), vel{{0, 0, 0}}, pushing(0) {}
    virtual ~BlobActor() {}
    virtual void Update() {
        ground = true;
        if (pushing) {
            // Try to push into the looking-towards direction
            const double maxvel = 0.1 * (pushing > 0),
                        acceleration = (pushing > 0 ? 0.2 : 0.1);
            // Which direction we are actively trying to go
            XYZ<double> move_vec = {{1, 0, 0}};
            Matrix<double> a;
            a.InitRotate(
                XYZ<double>{{0, (look_angle + move_angle) * M_PI / 180.0, 0}});
            a.Transform(move_vec);

            // Update the current velocity so it slowly approaches
            // either the desired direction, or halt. Only update
            // the horizontal axis though; the vertical is handled
            // entirely by gravity.
            vel.d[0] = vel.d[0] * (1 - acceleration) +
                        move_vec.d[0] * (acceleration * maxvel);
            vel.d[2] = vel.d[2] * (1 - acceleration) +
                        move_vec.d[2] * (acceleration * maxvel);
            moving = true;
        }
        if (moving) {
            // For the purposes of collision testing,
            // the player is an axis-aligned ellipsoid.
            // We do collision tests in two phases. First horizontal, then
            // vertical. Attempting to do both at same time would make it
            // too difficult to decide reliably when the player can jump.
            ground = false;
            double yvel = std::max(vel.d[1] + gravity, terminalvelocity);
            vel.d[1] = 0.0;
            camera -= center;
            CollideAndSlide(camera, vel, fatness, map);
            if (CollideAndSlide(camera, {{0, yvel, 0}}, fatness, map)) {
                if (yvel < 0) ground = true;
                yvel = 0.0;
            }
            vel.d[1] = yvel;
            camera += center;
            if (vel.Squared() < 1e-9) {
                vel *= 0.;
                pushing = 0;
                if (ground) moving = false;
            }
        }
        if (pushing) pushing = -1;

        double yaw_angle = yaw + vel.d[1] * 35.;
        dir = {{1, 0, 0}};
        up  = {{0, 1, 0}};
        Matrix<double> a;
        a.InitRotate(XYZ<double>{{0, look_angle * M_PI / 180.0, yaw_angle * M_PI / 180.0}});
        a.Transform(dir);
        a.Transform(up);
    }

    void MovementSignal(SignalType type, int param1 = 0, int param2 = 0) {
        switch (type) {
            case sig_push:
                pushing = 1;
                move_angle = param1;
            break;
            case sig_jump:
                if (ground) {
                    moving = true;
                    vel.d[1] += jump;
                }
            break;
            case sig_aim:
                look_angle -= param1;
                yaw -= param2;
                if (yaw < -90) yaw = -90;
                if (yaw > 90) yaw = 90; // defer too aggressive tilts
            break;
        }
    }
    void Fluctuate() {
        double fatsize = fatness.Len();
        fatness += fluctuation;
        for (unsigned c = 0; c < 3; ++c) {
            fluctuation.d[c] += ((rand() / double(RAND_MAX)) - 0.5) * 2.0 * 0.3;
            if (fluctuation.d[c] < -0.5) fluctuation.d[c] = -0.3;
            if (fluctuation.d[c] > 0.5) fluctuation.d[c] = 0.3;
            if (fatness.d[c] < 0.4) fatness.d[c] = 0.4;
            if (fatness.d[c] > 1.0) fatness.d[c] = 1.0;
        }
        fatness *= fatsize / fatness.Len();
    }
};