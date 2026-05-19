/*
*	rmx Library
*	Copyright (C) 2008-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once


// Library linking via pragma
#if defined(PLATFORM_WINDOWS) && !defined(PLATFORM_UWP) && defined(RMX_LIB)
	#pragma comment(lib, "sdl2main.lib")
	#pragma comment(lib, "sdl2.lib")
	#pragma comment(lib, "winmm.lib")
	#pragma comment(lib, "imm32.lib")
	#pragma comment(lib, "version.lib")
	#pragma comment(lib, "setupapi.lib")
	#pragma comment(lib, "opengl32.lib")
#endif

// This is for some reason needed under Linux
#if defined(__GNUC__) && __GNUC__ >= 4
	#define DECLSPEC __attribute__ ((visibility("default")))
#endif


// SDL
#ifdef PLATFORM_WINDOWS
	// Needed for MSYS2
	#if defined(__GNUC__)
		#include <SDL2/SDL.h>
	#else
		#include <SDL/SDL.h>
	#endif

#elif defined(PLATFORM_LINUX)
	#include <SDL2/SDL.h>

#else
	#include <SDL.h>
#endif


// OpenGL
#if defined(PLATFORM_UWP)
	// UWP/Xbox builds currently use the software renderer path only.

#elif defined(PLATFORM_WINDOWS)
	#define ALLOW_LEGACY_OPENGL
	#define RMX_USE_GLEW

#elif defined(PLATFORM_LINUX)
	#if defined(RMX_LINUX_ENFORCE_GLES2)	// Build option: Use OpenGL ES 2
		#define RMX_USE_GLES2
		#define GL_GLEXT_PROTOTYPES
		#include <GLES2/gl2.h>
		#include <GLES2/gl2ext.h>
	#else
		#define RMX_USE_GLEW
	#endif

#elif defined(PLATFORM_MAC)
	#define ALLOW_LEGACY_OPENGL		// Should be removed for macOS I guess?
	#include <OpenGL/gl3.h>
	#include <OpenGL/glu.h>

#elif defined(PLATFORM_WEB)
	#include <GL/glew.h>

#elif defined(PLATFORM_ANDROID)
	#define RMX_USE_GLES2
	#define GL_GLEXT_PROTOTYPES
	#include <GLES2/gl2.h>
	#include <GLES2/gl2ext.h>

#elif defined(PLATFORM_IOS)
	#define RMX_USE_GLES2
	#define GL_GLEXT_PROTOTYPES
	#include <OpenGLES/ES2/gl.h>
	#include <OpenGLES/ES2/glext.h>

#elif defined(PLATFORM_WIIU)
	#include "gl/gl.h"
	#include "gx2gl/sdl_bridge.h"
	#ifndef GL_LUMINANCE
		#define GL_LUMINANCE GL_RED
	#endif
	#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
		#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
		#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X 0x8516
		#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y 0x8517
		#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y 0x8518
		#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z 0x8519
		#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z 0x851A
	#endif
	#ifndef GL_FRAMEBUFFER_BINDING
		#define GL_FRAMEBUFFER_BINDING 0x8CA6
	#endif
	#ifndef GL_R8UI
		#define GL_R8UI   0x8232
		#define GL_R16I   0x8233
		#define GL_R16UI  0x8234
	#endif
	#ifndef GL_RED_INTEGER
		#define GL_RED_INTEGER 0x8D94
	#endif
	#ifndef GL_TEXTURE1
		#define GL_TEXTURE1  (GL_TEXTURE0 + 1)
		#define GL_TEXTURE2  (GL_TEXTURE0 + 2)
		#define GL_TEXTURE3  (GL_TEXTURE0 + 3)
		#define GL_TEXTURE4  (GL_TEXTURE0 + 4)
		#define GL_TEXTURE5  (GL_TEXTURE0 + 5)
		#define GL_TEXTURE6  (GL_TEXTURE0 + 6)
		#define GL_TEXTURE7  (GL_TEXTURE0 + 7)
		#define GL_TEXTURE8  (GL_TEXTURE0 + 8)
		#define GL_TEXTURE9  (GL_TEXTURE0 + 9)
		#define GL_TEXTURE10 (GL_TEXTURE0 + 10)
		#define GL_TEXTURE11 (GL_TEXTURE0 + 11)
		#define GL_TEXTURE12 (GL_TEXTURE0 + 12)
		#define GL_TEXTURE13 (GL_TEXTURE0 + 13)
		#define GL_TEXTURE14 (GL_TEXTURE0 + 14)
		#define GL_TEXTURE15 (GL_TEXTURE0 + 15)
		#define GL_TEXTURE16 (GL_TEXTURE0 + 16)
		#define GL_TEXTURE17 (GL_TEXTURE0 + 17)
		#define GL_TEXTURE18 (GL_TEXTURE0 + 18)
		#define GL_TEXTURE19 (GL_TEXTURE0 + 19)
		#define GL_TEXTURE20 (GL_TEXTURE0 + 20)
		#define GL_TEXTURE21 (GL_TEXTURE0 + 21)
		#define GL_TEXTURE22 (GL_TEXTURE0 + 22)
		#define GL_TEXTURE23 (GL_TEXTURE0 + 23)
		#define GL_TEXTURE24 (GL_TEXTURE0 + 24)
		#define GL_TEXTURE25 (GL_TEXTURE0 + 25)
		#define GL_TEXTURE26 (GL_TEXTURE0 + 26)
		#define GL_TEXTURE27 (GL_TEXTURE0 + 27)
		#define GL_TEXTURE28 (GL_TEXTURE0 + 28)
		#define GL_TEXTURE29 (GL_TEXTURE0 + 29)
		#define GL_TEXTURE30 (GL_TEXTURE0 + 30)
		#define GL_TEXTURE31 (GL_TEXTURE0 + 31)
	#endif

#elif defined(PLATFORM_SWITCH)
	#include <EGL/egl.h>    // EGL library
	#include <EGL/eglext.h> // EGL extensions
	#include <glad/glad.h>  // glad library (OpenGL loader)
	#define RMX_USE_GLAD
	#define GL_LUMINANCE GL_RED

#elif defined(PLATFORM_VITA)
	#include <vitaGL.h>
	#define RMX_USE_GLES2

#else
	#error Unsupported platform
#endif


#if defined(RMX_USE_GLES2) && !defined(__EMSCRIPTEN__)
	#if !defined(PLATFORM_LINUX) && !defined(__vita__)
		#define GL_RGB8				 GL_RGB
		#define GL_RGBA8			 GL_RGBA
		#define glGenVertexArrays	 glGenVertexArraysOES
		#define glDeleteVertexArrays glDeleteVertexArraysOES
		#define glBindVertexArray	 glBindVertexArrayOES
	#endif
	#define glClearDepth glClearDepthf
	#define glDepthRange glDepthRangef
#endif


#ifdef RMX_USE_GLEW
	#ifndef GLEW_STATIC
		#define GLEW_STATIC
	#endif
	#define GLEW_NO_GLU
	#include "rmxmedia/_glew/GL/glew.h"
#endif
