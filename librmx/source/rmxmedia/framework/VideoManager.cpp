/*
*	rmx Library
*	Copyright (C) 2008-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "rmxmedia.h"

#if defined(PLATFORM_WINDOWS)
	#pragma warning(disable: 4005)	// Macro redefinition of APIENTRY

	#if defined(__GNUC__)
		#include <SDL2/SDL_syswm.h>
	#else
		#include <SDL/SDL_syswm.h>
	#endif

	#define WIN32_LEAN_AND_MEAN
	#include "CleanWindowsInclude.h"

#elif defined(PLATFORM_WEB)
	#include <emscripten.h>
	#include <emscripten/html5.h>

#elif defined(PLATFORM_WIIU)
	#include <whb/gfx.h>

#endif


namespace rmx
{
	namespace
	{
		void getWindowSizeForRendering(SDL_Window* window, int& outWidth, int& outHeight)
		{
			outWidth = 0;
			outHeight = 0;
			if (nullptr == window)
				return;

			SDL_GetWindowSizeInPixels(window, &outWidth, &outHeight);
			if (outWidth <= 0 || outHeight <= 0)
			{
				SDL_GetWindowSize(window, &outWidth, &outHeight);
			}
		}

	#if defined(PLATFORM_WIIU)
		void setVideoConfigToTVDrawableSize(VideoConfig& videoConfig)
		{
			GX2ColorBuffer* tvColorBuffer = WHBGfxGetTVColourBuffer();
			if (nullptr != tvColorBuffer && tvColorBuffer->surface.width > 0 && tvColorBuffer->surface.height > 0)
			{
				videoConfig.mWindowRect.set(0, 0, (int)tvColorBuffer->surface.width, (int)tvColorBuffer->surface.height);
			}
			else
			{
				videoConfig.mWindowRect.set(0, 0, 1280, 720);
			}
		}
	#endif
	}

	VideoManager::VideoManager()
	{
	}

	VideoManager::~VideoManager()
	{
		// TODO: Do this right here?
		if (nullptr != mMainWindow)
		{
			SDL_DestroyWindow(mMainWindow);
			mMainWindow = nullptr;
		}
	}

	bool VideoManager::setVideoMode(const VideoConfig& videoconfig)
	{
		VideoConfig effectiveVideoConfig = videoconfig;
	#if defined(PLATFORM_WIIU)
		// The Wii U path renders directly to the TV scan buffer, not to the
		// desktop-sized SDL window requested by config.json.
		setVideoConfigToTVDrawableSize(effectiveVideoConfig);
		effectiveVideoConfig.mBorderless = true;
		effectiveVideoConfig.mResizeable = false;
	#endif

		// Change video mode
		uint32 flags = 0;
	#ifdef RMX_WITH_OPENGL_SUPPORT
		if (effectiveVideoConfig.mRenderer == VideoConfig::Renderer::OPENGL)
		{
		#if !defined(PLATFORM_WIIU)
			flags |= SDL_WINDOW_OPENGL;
		#endif
		}
	#endif
		if (effectiveVideoConfig.mFullscreen)
		{
			flags |= SDL_WINDOW_FULLSCREEN;
		}
		else
		{
			if (effectiveVideoConfig.mBorderless)
				flags |= SDL_WINDOW_BORDERLESS;
			if (effectiveVideoConfig.mResizeable)
				flags |= SDL_WINDOW_RESIZABLE;
		}

		int startX = SDL_WINDOWPOS_CENTERED_DISPLAY(effectiveVideoConfig.mDisplayIndex);
		int startY = SDL_WINDOWPOS_CENTERED_DISPLAY(effectiveVideoConfig.mDisplayIndex);
		if (effectiveVideoConfig.mPositioning)
		{
			startX = effectiveVideoConfig.mStartPos.x;
			startY = effectiveVideoConfig.mStartPos.y;
		}

		mMainWindow = SDL_CreateWindow(*effectiveVideoConfig.mCaption, startX, startY, effectiveVideoConfig.mWindowRect.width, effectiveVideoConfig.mWindowRect.height, flags);

		// Success so far?
		if (nullptr == mMainWindow)
			return false;

	#ifdef RMX_WITH_OPENGL_SUPPORT
		if (effectiveVideoConfig.mRenderer == VideoConfig::Renderer::OPENGL)
		{
		#if defined(PLATFORM_WIIU)
			GX2GL_CreateContext();
			GX2GL_SetSwapInterval(effectiveVideoConfig.mVSync ? 1 : 0);
			setVideoConfigToTVDrawableSize(effectiveVideoConfig);
		#else
			SDL_GL_CreateContext(mMainWindow);
			SDL_GL_SetSwapInterval(effectiveVideoConfig.mVSync ? 1 : 0);
		#endif
		}
	#endif

		// Copy video config
		mVideoConfig = effectiveVideoConfig;

		SDL_GetWindowSize(mMainWindow, &mVideoConfig.mWindowRect.width, &mVideoConfig.mWindowRect.height);
	#if defined(PLATFORM_WIIU)
		setVideoConfigToTVDrawableSize(mVideoConfig);
	#endif
		SDL_ShowCursor(!effectiveVideoConfig.mHideCursor);

	#ifdef RMX_WITH_OPENGL_SUPPORT
		if (effectiveVideoConfig.mRenderer == VideoConfig::Renderer::OPENGL)
		{
			// Defaults for OpenGL
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		#ifdef ALLOW_LEGACY_OPENGL
			glEnable(GL_ALPHA_TEST);
			glAlphaFunc(GL_GREATER, 0.001f);
		#endif
		}
	#endif
		return true;
	}

	bool VideoManager::initialize(const VideoConfig& videoconfig)
	{
		// Initialize video mode
		if (!FTX::System->initialize())
			return false;

		// Only change the mode?
		if (mInitialized)
			return setVideoMode(videoconfig);

	#ifdef RMX_WITH_OPENGL_SUPPORT
		if (videoconfig.mRenderer == VideoConfig::Renderer::OPENGL)
		{
		#if !defined(PLATFORM_WIIU)
			// Setup video mode for OpenGL
			SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
			SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
			SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
			SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
			SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
			SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, videoconfig.mMultisampling);
		#endif
		}
	#endif

		if (!setVideoMode(videoconfig))
			return false;

	#ifdef RMX_WITH_OPENGL_SUPPORT
		if (videoconfig.mRenderer == VideoConfig::Renderer::OPENGL)
		{
		#ifdef RMX_USE_GLEW
			const GLenum result = glewInit();
			if (result != GLEW_OK)
			{
				RMX_ERROR("Error in OpenGL initialization (glewInit):\n" << glewGetErrorString(result), );
				return false;
			}
		#endif
		#ifdef RMX_USE_GLAD
			gladLoadGL();
		#endif
		}
	#endif

		mInitialized = true;

	#if defined(PLATFORM_WINDOWS) && !defined(PLATFORM_UWP)
		// Set icon (Windows)
		if (mVideoConfig.mIconResource != 0)
		{
			HICON hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(mVideoConfig.mIconResource));
			SendMessage((HWND)FTX::Video->getNativeWindowHandle(), WM_SETICON, ICON_BIG, (LPARAM)hIcon);
		}
	#endif

		// Set icon (general)
		if (nullptr != mVideoConfig.mIconBitmap.getData() || mVideoConfig.mIconSource.nonEmpty())
		{
			Bitmap tmp;
			Bitmap* bitmap = &mVideoConfig.mIconBitmap;
			if (bitmap->empty())
			{
				bitmap = nullptr;
				if (tmp.load(mVideoConfig.mIconSource.toWString()))
				{
					bitmap = &tmp;
				}
			}

			if (nullptr != bitmap)
			{
				bitmap->rescale(32, 32);
				SDL_Surface* icon = SDL_CreateRGBSurfaceFrom(bitmap->getData(), 32, 32, 32, bitmap->getWidth() * sizeof(uint32), 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
				SDL_SetWindowIcon(mMainWindow, icon);
				SDL_FreeSurface(icon);
			}
		}

		return true;
	}

	void VideoManager::setInitialized(const VideoConfig& videoconfig, SDL_Window* window)
	{
		mVideoConfig = videoconfig;
		mMainWindow = window;
		if (nullptr != mMainWindow)
		{
			getWindowSizeForRendering(mMainWindow, mVideoConfig.mWindowRect.width, mVideoConfig.mWindowRect.height);
		}
		mInitialized = true;
	}

	void VideoManager::clearInitialized()
	{
		mInitialized = false;
		mReshaped = false;
		mMainWindow = nullptr;
	}

	void VideoManager::reshape(int width, int height)
	{
		// Called e.g. when window size changed
		if (mVideoConfig.mWindowRect.width == width && mVideoConfig.mWindowRect.height == height)
			return;

		mVideoConfig.mWindowRect.width = width;
		mVideoConfig.mWindowRect.height = height;
		mReshaped = true;
	/*
		// Reset video mode
		setVideoMode(mVideoConfig);
	*/
	}

	void VideoManager::beginRendering()
	{
		if (mVideoConfig.mAutoClearScreen)
		{
		#ifdef RMX_WITH_OPENGL_SUPPORT
			if (mVideoConfig.mRenderer == VideoConfig::Renderer::OPENGL)
			{
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			}
		#endif
		}
	}

	void VideoManager::endRendering()
	{
		if (mVideoConfig.mAutoSwapBuffers)
		{
		#ifdef RMX_WITH_OPENGL_SUPPORT
			if (mVideoConfig.mRenderer == VideoConfig::Renderer::OPENGL)
			{
			#if defined(PLATFORM_WIIU)
				GX2GL_SwapWindow();
			#else
				SDL_GL_SwapWindow(mMainWindow);
			#endif
			}
		#endif
		}
		mReshaped = false;
	}

	void VideoManager::setPixelView()
	{
	#ifdef RMX_WITH_OPENGL_SUPPORT
		if (mVideoConfig.mRenderer == VideoConfig::Renderer::OPENGL)
		{
		#ifdef ALLOW_LEGACY_OPENGL
			// Set 2D view
			glViewport(0, 0, mVideoConfig.mWindowRect.width, mVideoConfig.mWindowRect.height);
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glOrtho(0.0, mVideoConfig.mWindowRect.width, mVideoConfig.mWindowRect.height, 0.0, +1.0, -1.0);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
		#else
			RMX_ASSERT(false, "Unsupported without legacy OpenGL support");
		#endif
		}
	#endif
	}

	void VideoManager::setPerspective2D(double fov, double dnear, double dfar)
	{
	#ifdef RMX_WITH_OPENGL_SUPPORT
		if (mVideoConfig.mRenderer == VideoConfig::Renderer::OPENGL)
		{
		#ifdef ALLOW_LEGACY_OPENGL
			// Set perspective 2D view
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			double dx = mVideoConfig.mWindowRect.width * dnear * 0.5;
			double dy = mVideoConfig.mWindowRect.height * dnear * 0.5;
			glFrustum(-dx, dx, dy, -dy, dnear, dfar);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
			glTranslatef(-mVideoConfig.mWindowRect.width * 0.5f, -mVideoConfig.mWindowRect.height * 0.5f, -1.0f);
		#else
			RMX_ASSERT(false, "Unsupported without legacy OpenGL support");
		#endif
		}
	#endif
	}

	void VideoManager::getScreenBitmap(Bitmap& bitmap)
	{
	#ifdef RMX_WITH_OPENGL_SUPPORT
		if (mVideoConfig.mRenderer == VideoConfig::Renderer::OPENGL)
		{
			const Recti& screen = getScreenRect();
			bitmap.create(screen.width, screen.height);
			glReadPixels(screen.left, screen.top, screen.width, screen.height, GL_RGBA, GL_UNSIGNED_BYTE, bitmap.getData());
			bitmap.mirrorVertical();
		}
	#endif
	}

	uint64 VideoManager::getNativeWindowHandle() const
	{
	#if defined(PLATFORM_WINDOWS) && !defined(PLATFORM_UWP)
		SDL_SysWMinfo info;
		SDL_VERSION(&info.version);
		if (!SDL_GetWindowWMInfo(mMainWindow, &info))
			return 0;
		return (uint64)info.info.win.window;
	#else
		// TODO: Implement this
		return 0;
	#endif
	}

}
