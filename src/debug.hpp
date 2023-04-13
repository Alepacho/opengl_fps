#pragma once

#define GL_SILENCE_DEPRECATION
#include "GL/glew.h"

#include <algorithm> // For std::min, std::max
#include <cmath>     // For std::pow, std::sin, std::cos
#include <iostream>
#include <list>   // Blobs are stored in a list.
#include <vector> // For std::vector, in which we store texture & lightmap

static const char* GetGLErrorStr(GLenum err) {
    switch (err) {
        case GL_NO_ERROR:          return "No error";
        case GL_INVALID_ENUM:      return "Invalid enum";
        case GL_INVALID_VALUE:     return "Invalid value";
        case GL_INVALID_OPERATION: return "Invalid operation";
        case GL_STACK_OVERFLOW:    return "Stack overflow";
        case GL_STACK_UNDERFLOW:   return "Stack underflow";
        case GL_OUT_OF_MEMORY:     return "Out of memory";
        default:                   return "Unknown error";
    }
}

inline bool CheckGLError(std::string where = "Unknown") {
    while (true) {
        const GLenum err = glGetError();
        if (GL_NO_ERROR == err)
            break;

        std::cout << where << ": " "GL Error: " << GetGLErrorStr(err) << std::endl;
        // exit(1);
        return true;
    }
    return false;
}

inline void debug(std::string msg) {
	std::cout << msg << std::endl;
}