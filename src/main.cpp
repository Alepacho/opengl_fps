#define GL_SILENCE_DEPRECATION
#include "GL/glew.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_opengl.h"

// Standard C++ includes:
#include <algorithm> // For std::min, std::max
#include <cmath>     // For std::pow, std::sin, std::cos
#include <iostream>
#include <list>   // Blobs are stored in a list.
#include <vector> // For std::vector, in which we store texture & lightmap

#include "map.hpp"
#include "math.hpp"
#include "actor.hpp"
#include "debug.hpp"

SDL_Window *window = NULL;
SDL_GLContext ctx;

const unsigned nwalls = sizeof(map) / sizeof(*map);
static bool TexturesInstalled = false;
static GLuint WallTextureID;
static bool UseAddmap[nwalls] = {false};
static bool UseDecals[nwalls] = {false};
static unsigned LightmapIDs[nwalls] = {0};
static unsigned AddmapIDs[nwalls]   = {0};
static unsigned DecalIDs[nwalls]    = {0};
static std::vector<float> DecalMaps[nwalls];
static float mouseSens 		= 0.35f;
static double fov 			= 90.0;
static bool useFrameBuffer 	= false;
static bool useDithering 	= false;
static bool toggleMouse 	= true;

void InstallTexture(
	const void *data, 
	int w, int h, 
	int txno, 
	int type1, int type2, 
	int filter, int wrap
) {
	glBindTexture(GL_TEXTURE_2D, txno);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	// Control how the texture repeats or not
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	// Control how the texture is rendered at different distances
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // GL_LINEAR
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter); // 
	// Decide upon the manner in which to import the texture
	if (filter == GL_LINEAR || filter == GL_NEAREST)
		glTexImage2D(GL_TEXTURE_2D, 0, type1, w, h, 0, type1, type2, data);
	else
		gluBuild2DMipmaps(GL_TEXTURE_2D, type1, w, h, type1, type2, data);
}

void ActivateTexture(int layer, int txno, int mode = GL_MODULATE) {
	glActiveTextureARB(layer);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, txno);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode);
	glColor3f(1, 1, 1);
}
void DisableTexture(int layer) {
	glActiveTextureARB(layer);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);
	glColor3f(1, 1, 1);
}

namespace PC {
	int W = 1024, H = W * 9 / 16;
	const unsigned DitheringBits = 6;
	const unsigned R = 7, G = 9, B = 4; // 7*9*4 regular palette (252 colors)
	const double PaletteGamma = 1.5;    // Apply this gamma to palette
	const double DitherGamma = 2.0 / PaletteGamma; // Apply this gamma to dithering
	const bool TemporalDithering = true;
	unsigned char ColorConvert[3][256][256], Dither8x8[8][8];
	unsigned Pal[R * G * B];

	unsigned int* ImageBuffer = NULL;//[W * H];
	int selector;

	// End graphics
	void Close(int code = 0) {
		if (code != 0) std::cout << "Error!" << std::endl;
		// if (ImageBuffer != NULL) delete ImageBuffer;
		// ImageBuffer = NULL;

		SDL_GL_DeleteContext(ctx);
		if (window != NULL) SDL_DestroyWindow(window);
		SDL_Quit();
		exit(code);
	}

	// Initialize graphics
	void Init() {
		if (SDL_Init(SDL_INIT_EVERYTHING) < 0) Close(1);
		SDL_WindowFlags window_flags = (SDL_WindowFlags)(
			SDL_WINDOW_SHOWN |
			SDL_WINDOW_RESIZABLE
		);
		window = SDL_CreateWindow("OpenGL 256 FPS Demo", SDL_WINDOWPOS_CENTERED,
									SDL_WINDOWPOS_CENTERED, PC::W, PC::H, window_flags);
		if (window == NULL) Close(1);

		//
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
		// SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY); // ! wont work without this mode
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
							SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		ctx = SDL_GL_CreateContext(window);
		if (ctx == NULL) Close(1);

		SDL_GL_MakeCurrent(window, ctx);
		SDL_GL_SetSwapInterval(1);

		GLenum glew_check;
		glewExperimental = GL_TRUE;
		glew_check = glewInit();
		if (glew_check != GLEW_OK) Close(1);

		if (CheckGLError("PC::Init")) PC::Close(1);

		// Create bayer 8x8 dithering matrix.
		for (unsigned y = 0; y < 8; ++y)
			for (unsigned x = 0; x < 8; ++x)
			Dither8x8[y][x] = ((x)&4) / 4u + ((x)&2) * 2u + ((x)&1) * 16u +
								((x ^ y) & 4) / 2u + ((x ^ y) & 2) * 4u +
								((x ^ y) & 1) * 32u;

		// Create gamma-corrected look-up tables for dithering.
		double dtab[256], ptab[256];
		for (unsigned n = 0; n < 256; ++n) {
			dtab[n] = (255.0 / 256.0) - std::pow(n / 256.0, 1 / DitherGamma);
			ptab[n] = std::pow(n / 255.0, 1.0 / PaletteGamma);
		}
		for (unsigned n = 0; n < 256; ++n) {
			for (unsigned d = 0; d < 256; ++d) {
				ColorConvert[0][n][d] = std::min(B - 1, (unsigned)(ptab[n] * (B - 1) + dtab[d]));
				ColorConvert[1][n][d] = B * std::min(G - 1, (unsigned)(ptab[n] * (G - 1) + dtab[d]));
				ColorConvert[2][n][d] = G * B * std::min(R - 1, (unsigned)(ptab[n] * (R - 1) + dtab[d]));
			}
		}
		for (unsigned color = 0; color < R * G * B; ++color) {
			Pal[color] =
				0x40000 * (int)(std::pow(((color / (B * G)) % R) * 1. / (R - 1), PaletteGamma) * 63) +
				0x00400 * (int)(std::pow(((color / B) % G) * 1. / (G - 1), PaletteGamma) * 63) +
				0x00004 * (int)(std::pow(((color) % B) * 1. / (B - 1), PaletteGamma) * 63);
		}

		if (toggleMouse) {
			SDL_WarpMouseInWindow(window, W / 2, H / 2);
		}
	}

	void Resize() {
		// glDeleteRenderbuffers(1, &targetBuffer);
	}

	void Update(BlobActor& player, Actor portals[], std::list<BlobActor> blobs) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			switch (e.type) {
				case SDL_QUIT: {
					PC::Close();
				} break;
				case SDL_KEYDOWN: {
					const auto sc = e.key.keysym.scancode;
					if (sc == SDL_SCANCODE_Q) PC::Close();
					if (sc == SDL_SCANCODE_T) {
						toggleMouse = !toggleMouse;
						SDL_SetRelativeMouseMode(toggleMouse ? SDL_TRUE : SDL_FALSE);
						SDL_WarpMouseInWindow(window, W / 2, H / 2);
					}
					if (sc == SDL_SCANCODE_1) fov = std::max(fov - 1, 65.0);
					if (sc == SDL_SCANCODE_2) fov = std::min(fov + 1, 110.0);
				} break;
				case SDL_WINDOWEVENT: {
					const auto we = e.window.event;
					if (we == SDL_WINDOWEVENT_FOCUS_LOST) {
						if (toggleMouse) SDL_SetRelativeMouseMode(SDL_FALSE);
					} 
					if (we == SDL_WINDOWEVENT_FOCUS_GAINED) {
						if (toggleMouse) {
							SDL_SetRelativeMouseMode(SDL_TRUE);
							SDL_WarpMouseInWindow(window, W / 2, H / 2);
						}
					} 
					if (we == SDL_WINDOWEVENT_RESIZED) {
						SDL_GetWindowSize(window, &PC::W, &PC::H);
						// if (ImageBuffer != NULL) delete ImageBuffer;
						// ImageBuffer = new GLuint(W * H);
					}
				} break;
				case SDL_MOUSEMOTION: {
					// Get mouse relative position (since last poll) and update view
					
				} break;
				case SDL_MOUSEBUTTONDOWN: {
					// Fire a portal
					Actor &portal = portals[e.button.button == SDL_BUTTON_LEFT ? 1 : 0];
					HitRec r = IntersectRay(player.camera, player.dir, map);
					portal.dir = map[r.wallno].normal;
					portal.camera = r.hit + portal.dir * 1e-4;
					// Figure out where the "up" vector for the portal should go.
					portal.up = portal.dir.Cross(player.dir.Cross(player.up)).Normalized();
					portal.up *= -1.0;
				} break;
			}
		}

		// Update Player Movement
		// const bool keys[SDL_NUM_SCANCODES] = { false };
		const unsigned char* keys = SDL_GetKeyboardState(NULL);
		// auto sc = e.key.keysym.scancode;
		if (keys[SDL_SCANCODE_ESCAPE]) PC::Close();
		if (keys[SDL_SCANCODE_W]) player.MovementSignal(BlobActor::sig_push,   0);
		if (keys[SDL_SCANCODE_S]) player.MovementSignal(BlobActor::sig_push, 180);
		if (keys[SDL_SCANCODE_A]) player.MovementSignal(BlobActor::sig_push, -90);
		if (keys[SDL_SCANCODE_D]) player.MovementSignal(BlobActor::sig_push,  90);
		if (keys[SDL_SCANCODE_SPACE]) player.MovementSignal(BlobActor::sig_jump);
		if (keys[SDL_SCANCODE_B]) {
			BlobActor blob;
			blob.fatness = {{0.45, 0.45, 0.45}};
			blob.camera = player.camera + player.dir * 0.2;
			blob.dir = player.dir;
			blob.vel = player.dir * 0.2 + player.vel;
			blobs.push_back(blob);
		}
		// Update Player Camera Rotation 
		if (toggleMouse) {
			int mx, my;
			SDL_GetRelativeMouseState(&mx, &my);
			player.MovementSignal(BlobActor::sig_aim, (short)-(mx * mouseSens), (short)-(my * mouseSens));
		}

		player.Update();
		if (CheckGLError("Update")) PC::Close(1);
	}

	template <class Func>
	void Render(
		const unsigned PW, const unsigned PH, 
		Actor portals[], BlobActor& player,
		GLuint frame_buffers[], Func &RenderWorld,
		GLuint portal_textures[]
	) {
		glViewport(0, 0, PW, PH);
		for (int recursion = 0; recursion < 1; ++recursion) {
			// Render both portal's point of view
			for (int p = 0; p < 2; ++p) {
				int seen = p, vista = 1 - p;
				Actor &seen_portal = portals[seen];   // Which portal presents the view
				Actor &vista_portal = portals[vista]; // Which portal's view is seen

				XYZ<double> eye_to_seen_portal = seen_portal.camera - player.camera;
				double eye_to_seen_distance = eye_to_seen_portal.Len();
				eye_to_seen_portal /= eye_to_seen_distance; // Normalize

				// // Figure out the angle between the seen_portal's normal&up and
				// // eye_to_seen_portal,
				// double dir_portaldir_cosine =
				// eye_to_seen_portal.Dot(seen_portal.dir); double dir_portalup_cosine =
				// eye_to_seen_portal.Dot(seen_portal.up);
				// // Create a new eye-beam by translating that angle to vista_portal's
				// // normal&up.

				// // FIXME: FIGURE OUT HOW TO TRANSLATE THE VIEW!

				// TODO: Add such projection that all camera rays are cast parallel to
				// a viewing plane that is larger as audience comes nearer.
				double distance = (portals[seen].camera - player.camera).Len();
				double portalfov = 180.0 / (1 + distance);

				// fprintf(stderr, 
				// 	"Portal %d at <%.5f,%.5f,%.5f>, dir=<%.5f,%.5f,%.5f>, up=<%.5f,%.5f,%.5f>, cos=%.5f\n",
				//     p,
				//     seen_portal.camera.d[0],
				//     seen_portal.camera.d[1],
				//     seen_portal.camera.d[2],
				//     seen_portal.dir.d[0],
				//     seen_portal.dir.d[1],
				//     seen_portal.dir.d[2],
				//     seen_portal.up.d[0],
				//     seen_portal.up.d[1],
				//     seen_portal.up.d[2],
				//     seen_portal.dir.Dot(seen_portal.up)
				// );

				if (useFrameBuffer) {
					glBindFramebuffer(GL_FRAMEBUFFER, frame_buffers[seen]);
				} else {
					glPushAttrib(GL_COLOR_BUFFER_BIT | GL_PIXEL_MODE_BIT); //  // 
					glDrawBuffer(GL_BACK);
					glReadBuffer(GL_BACK);
				}
				vista_portal.Render(RenderWorld, portalfov, 1.0 / 1.0);

				if (useFrameBuffer) {
					ActivateTexture(GL_TEXTURE0_ARB, portal_textures[seen]);
                	glGenerateMipmapEXT(GL_TEXTURE_2D);
				} else {
					ActivateTexture(GL_TEXTURE0_ARB, portal_textures[seen]);
					glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, PW, PH);
					glGenerateMipmapEXT(GL_TEXTURE_2D);
					glPopAttrib();
				}
			}
		}

		if (useFrameBuffer) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		} 
		glViewport(0, 0, PC::W, PC::H);

		// glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		// glBindRenderbuffer(GL_RENDERBUFFER, targetBuffer);
		// Render player's point of view
		if (player.Render(RenderWorld, fov, (double)PC::W / (double)PC::H)) {
			if (CheckGLError("Player::Render")) PC::Close(1);
		}
		// glBindFramebuffer(GL_FRAMEBUFFER, 0);
		// glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		// glBindRenderbuffer(GL_RENDERBUFFER, 0);

		if (useDithering) {
			// static unsigned f = 0; ++f; // Frame number
			// if (ImageBuffer != NULL) {
			// 	for (unsigned p = 0, y = 0; y < (unsigned)H; ++y) {
			// 		for (unsigned x = 0; x < (unsigned)W; ++x, ++p) {
			// 			// Convert the RGB color into a palette index with dithering
			// 			unsigned rgb = ImageBuffer[p], d = Dither8x8[y&7][x&7]; // 0..63
			// 			d &= (0x3F - (0x3F >> DitheringBits));
			// 			if(!TemporalDithering)
			// 				d *= 4; // No temporal dithering
			// 			else // Do temporal dithering
			// 				d += ((f^y^(x&1)*2u ^ (x&2)/2u) & 3) << 6;
			// 			// Put the pixel in video memory
			// 			ImageBuffer[p] = Pal[
			// 				ColorConvert[0][(rgb >> 0) & 0xFF][d]
			// 			+ ColorConvert[1][(rgb >> 8) & 0xFF][d]
			// 			+ ColorConvert[2][(rgb >>16) & 0xFF][d] ];
			// 		}
			// 	}
			// }
		}

		SDL_GL_SwapWindow(window);

		if (CheckGLError("Render")) PC::Close(1);
	}
} // namespace PC



// This function converts the level map into OpenGL quad primitives.
// Not particularly optimized (in particular, everything is always rendered).
static void ExtractLevelMap() {
	glShadeModel(GL_SMOOTH);

	// Walls are all created using this one texture.
	if (!TexturesInstalled) {
		// Generate a very simple rectangle of a texture.
		glGenTextures(1, &WallTextureID);
		glGenTextures(nwalls, LightmapIDs);
		glGenTextures(nwalls, AddmapIDs);

		const unsigned txW = 256, txH = 256;
		GLfloat texture[txH * txW];
		for (unsigned y = 0; y < txH; ++y)
			for (unsigned x = 0; x < txW; ++x)
				texture[y * txW + x] =
				0.7 - ((1.0 - std::sqrt(int(x - txW / 2) * int(x - txW / 2) /
				double(txW / 2.0) / (txW / 2.0) + int(y - txH / 2) * int(y - txH / 2) /
				double(txH / 2.0) / (txH / 2.0))) * 0.6 -
				!(x < 8 || y < 8 || (x + 8) >= txW || (y + 8) >= txH)) *
				(0.1 + 0.3 * std::pow((std::rand() % 100) / 100.0, 2.0));
		;
		InstallTexture(texture, txW, txH, WallTextureID, GL_LUMINANCE, GL_FLOAT,
					GL_LINEAR_MIPMAP_LINEAR, GL_REPEAT);
	}
	ActivateTexture(GL_TEXTURE0_ARB, WallTextureID);

  	for (unsigned wallno = 0; wallno < nwalls; ++wallno) {
		const maptype &m = map[wallno];
		auto v10 = m.p[1] - m.p[0];
		auto v30 = m.p[3] - m.p[0];
		int width = v30.Len();  // Number of times the texture
		int height = v10.Len(); // is repeated across the surface.

		if (!TexturesInstalled) {
			// Load lightmap.
			unsigned lmW = width * 32, lmH = height * 32;
			std::vector<float> map(lmW * lmH * 3);

			char Buf[64];
			std::snprintf(Buf, 64, "light/lmap/lmap%u.raw", wallno);
			FILE *fp = std::fopen(Buf, "rb");
			std::fread(&map[0], map.size(), sizeof(float), fp);
			std::fclose(fp);
			InstallTexture(&map[0], lmW, lmH, LightmapIDs[wallno], GL_RGB, GL_FLOAT,
							GL_LINEAR, GL_CLAMP_TO_EDGE);

			// Because OSMesa clamps all texture values into [0,1] range, meaning
			// that a lightsource can only darken the texture, never brighten it,
			// we must have a separate multiply-map and an add-map, where the
			// former can darken the texture and the latter can only brighten it.
			// (Unfortunately, due to how mathematics works, the add-map
			//  is specific to the underlying texture is was designed for.)

			std::snprintf(Buf, 64, "light/smap/smap%u.raw", wallno);
			fp = std::fopen(Buf, "rb");
			if (fp) {
				UseAddmap[wallno] = true;
				std::fread(&map[0], map.size(), sizeof(float), fp);
				std::fclose(fp);
				InstallTexture(&map[0], lmW, lmH, AddmapIDs[wallno], GL_RGB, GL_FLOAT,
							GL_LINEAR, GL_CLAMP_TO_EDGE);
			}
		} // TexturesInstalled

		if (UseAddmap[wallno])
			ActivateTexture(GL_TEXTURE2_ARB, AddmapIDs[wallno], GL_ADD);
		else
			DisableTexture(GL_TEXTURE2_ARB);

		if (UseDecals[wallno])
			ActivateTexture(GL_TEXTURE3_ARB, DecalIDs[wallno], GL_DECAL);
		else
			DisableTexture(GL_TEXTURE3_ARB);

		ActivateTexture(GL_TEXTURE1_ARB, LightmapIDs[wallno], GL_MODULATE);

		glNormal3fv(m.normal.d);

		glBegin(GL_QUADS);
		for (unsigned e = 0; e < 4; ++e) {
			glMultiTexCoord2fARB(GL_TEXTURE0_ARB, width * !((e + 2) & 2),
								height * !((e + 3) & 2));
			glMultiTexCoord2fARB(GL_TEXTURE1_ARB, 1 * !((e + 2) & 2),
								1 * !((e + 3) & 2));
			if (UseAddmap[wallno])
				glMultiTexCoord2fARB(GL_TEXTURE2_ARB, 1 * !((e + 2) & 2),
									1 * !((e + 3) & 2));
			if (UseDecals[wallno])
				glMultiTexCoord2fARB(GL_TEXTURE3_ARB, 1 * !((e + 2) & 2),
									1 * !((e + 3) & 2));
			glVertex3fv(m.p[e].d);
		}
		glEnd();
	}
	DisableTexture(GL_TEXTURE2_ARB);
	DisableTexture(GL_TEXTURE1_ARB);
	DisableTexture(GL_TEXTURE0_ARB);
	TexturesInstalled = true;
}


int main() {
	const unsigned PW = std::min(PC::W, 128), PH = std::min(PC::H, 128);
	PC::Init();

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
	glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);

	{
		GLfloat v[4] = {0, 0, 0, 0};
		glLightModelfv(GL_LIGHT_MODEL_AMBIENT, v);
	}

	BlobActor player;
	player.fatness = {{0.2, 0.6, 0.2}}; // Shape of the ellipsoid
	player.center = {{0, 0.3, 0}};      // representing the actor
	player.camera = {{4, 3, 7.25}};     // Location thereof
	player.look_angle = 170;
	player.yaw = 10; // Where it is facing

	std::list<BlobActor> blobs;

	Actor portals[2];
	portals[0].camera = {{2, 2, 6}};
	portals[1].camera = {{2, 4, 6}};
	GLuint portal_textures[2];
	glGenTextures(2, portal_textures);

	GLuint frame_buffers[2], portal_buffers[2];
	if (useFrameBuffer) {
    	glGenFramebuffers(2, frame_buffers);
    	glGenRenderbuffers(2, portal_buffers);
	}

	for (int p = 0; p < 2; ++p) {
		InstallTexture(0, PW, PH, portal_textures[p], GL_RGB, GL_FLOAT, GL_LINEAR,
					GL_CLAMP_TO_EDGE);
		
		if (!useFrameBuffer) continue;
		glBindFramebuffer(GL_FRAMEBUFFER, frame_buffers[p]);
        glBindRenderbuffer(GL_RENDERBUFFER, portal_buffers[p]);
        glRenderbufferStorageEXT(GL_RENDERBUFFER, GL_RGB, PW, PH);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_TEXTURE_2D, portal_textures[p], 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                     GL_RENDERBUFFER, portal_buffers[p]);
        glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	auto RenderWorld = [&](Actor &exclude_actor) {
		// Create white spheres representing all lightsources.
		DisableTexture(GL_TEXTURE0_ARB);
		DisableTexture(GL_TEXTURE1_ARB);
		DisableTexture(GL_TEXTURE2_ARB);
		for (const auto &l : lights) {
			glTranslated(l.pos.d[0], l.pos.d[1], l.pos.d[2]);
			GLUquadric *qu = gluNewQuadric();
			gluSphere(qu, 0.1f, 16, 16);
			gluDeleteQuadric(qu);
			glTranslated(-l.pos.d[0], -l.pos.d[1], -l.pos.d[2]);
		}
		if (&exclude_actor != &player) {
			// For now, this blue sphere represents the player as well.
			glColor3f(.4, .4, .1);
			glPushMatrix();
			glTranslated(player.camera.d[0], player.camera.d[1], player.camera.d[2]);
			glTranslated(-player.center.d[0], -player.center.d[1],
						-player.center.d[2]);
			glScaled(player.fatness.d[0], player.fatness.d[1], player.fatness.d[2]);
			GLUquadric *qu = gluNewQuadric();
			gluSphere(qu, 1.0, 16, 16);
			gluDeleteQuadric(qu);
			glPopMatrix();
			glColor3f(1, 1, 1);
		}
		for (auto &blob : blobs) {
			// Blobs are also blue.
			glColor3f(1, .2, .1);
			glPushMatrix();
			glTranslated(blob.camera.d[0], blob.camera.d[1], blob.camera.d[2]);
			glTranslated(-blob.center.d[0], -blob.center.d[1], -blob.center.d[2]);
			glScaled(blob.fatness.d[0], blob.fatness.d[1], blob.fatness.d[2]);
			GLUquadric *qu = gluNewQuadric();
			gluSphere(qu, 1.0, 16, 16);
			gluDeleteQuadric(qu);
			glPopMatrix();
			glColor3f(1, 1, 1);
		}
		for (int p = 0; p < 2; ++p) {
			if (&exclude_actor != &portals[p]) {
				// Render this portal
				ActivateTexture(GL_TEXTURE0_ARB, portal_textures[p]);
				XYZ<GLfloat> v = portals[p].dir;
				glNormal3fv(v.d); // Direction where the portal is facing

				// The four corner points are made by rotating
				// the portal's "up" vector around its "dir"
				// at 90 degree steps.
				Matrix<double> a;
				glBegin(GL_QUADS);
				for (unsigned e = 0; e < 4; ++e) {
					glMultiTexCoord2fARB(GL_TEXTURE0_ARB, 1 * !!((e + 0) & 2),
										1 * !!((e + 3) & 2));
					a.InitAxisRotate(portals[p].dir, (e * 90 + 45) * -M_PI / 180.0);
					v = portals[p].up;
					a.Transform(v);
					v *= 0.75;
					v += portals[p].camera;
					glVertex3fv(v.d);
				}
				glEnd();
			}
		}

		ExtractLevelMap();
	};

	// Main loop
	while (true) {
		PC::Update(player, portals, blobs);
		PC::Render(PW, PH, portals, player, frame_buffers, RenderWorld, portal_textures);
	}
}