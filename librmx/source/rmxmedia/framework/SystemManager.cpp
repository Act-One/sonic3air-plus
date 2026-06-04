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
	#include <whb/proc.h>

#endif


namespace rmx
{

	SystemManager::SystemManager()
	{
	}

	SystemManager::~SystemManager()
	{
	}

	bool SystemManager::initialize()
	{
		if (mInitialized)
			return true;

		// On Wii U the GX2 path owns video output directly.
		// SDL is initialized without its video backend.
#if defined(PLATFORM_WIIU)
		if (SDL_Init(0) < 0)
#else
		// Initialize SDL video
		if (SDL_Init(SDL_INIT_VIDEO) < 0)
#endif
		{
#if defined(PLATFORM_WIIU)
			std::cout << "SDL_Init(0) failed with error: " << SDL_GetError() << "\n";
#else
			std::cout << "SDL_Init(SDL_INIT_VIDEO) failed with error: " << SDL_GetError() << "\n";
#endif
			return false;
		}

#if defined(PLATFORM_WIIU)
		if (!WHBProcIsRunning())
		{
			WHBProcInit();
			mProcUIInitialized = true;
		}
#endif

		mInitialized = true;
		return true;
	}

	void SystemManager::exit()
	{
		// Quit SDL
		RMX_LOG_INFO("SystemManager: SDL_Quit begin");
		SDL_Quit();
		RMX_LOG_INFO("SystemManager: SDL_Quit complete");

#if defined(PLATFORM_WIIU)
		if (mProcUIInitialized)
		{
			RMX_LOG_INFO("SystemManager: WHBProcShutdown begin");
			WHBProcShutdown();
			RMX_LOG_INFO("SystemManager: WHBProcShutdown complete");
			mProcUIInitialized = false;
		}
#endif
	}

	void SystemManager::checkSDLEvents()
	{
		// Handle events
		mInputContext.backupState();

		// Process SDL event queue
		SDL_Event evnt;
		while (SDL_PollEvent(&evnt))
		{
			switch (evnt.type)
			{
				case SDL_QUIT:
					quit();
					break;

				case SDL_WINDOWEVENT:
					if (evnt.window.event == SDL_WINDOWEVENT_RESIZED || evnt.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
						reshape(evnt.window.data1, evnt.window.data2);
					break;

				case SDL_KEYDOWN:
				case SDL_KEYUP:
					keyboard(evnt.key);
					break;

				case SDL_TEXTINPUT:
					textinput(evnt.text);
					break;

				case SDL_MOUSEBUTTONDOWN:
				case SDL_MOUSEBUTTONUP:
					mouse(evnt.button);
					break;

				case SDL_MOUSEWHEEL:
					mousewheel(evnt.wheel);
					break;

				case SDL_MOUSEMOTION:
					mInputContext.applyMousePos(evnt.motion.x, evnt.motion.y);
					break;
			}

			mRoot.sdlEvent(evnt);
		}

		// Track changes since previous update
		mInputContext.refreshChangeFlags();
	}

	void SystemManager::reshape(int width, int height)
	{
		if (FTX::Video.valid())
			FTX::Video->reshape(width, height);
	}

	void SystemManager::keyboard(const SDL_KeyboardEvent& evnt)
	{
		KeyboardEvent ev;
		ev.key = evnt.keysym.sym;
		ev.scancode = evnt.keysym.scancode;
		ev.modifiers = evnt.keysym.mod;
		ev.state = (evnt.type == SDL_KEYDOWN);
		ev.repeat = (evnt.repeat != 0);
		mInputContext.applyEvent(ev);

		mCurrentEventConsumed = false;
		mRoot.keyboard(ev);
	}

	void SystemManager::textinput(const SDL_TextInputEvent& evnt)
	{
		TextInputEvent ev;
		ev.text = rmx::convertFromUTF8(evnt.text);

		mCurrentEventConsumed = false;
		mRoot.textinput(ev);
	}

	void SystemManager::mouse(const SDL_MouseButtonEvent& evnt)
	{
		// Mouse click
		if (evnt.button < 1 || evnt.button > 7)
			return;

		static const MouseButton buttonMap[3] = { MouseButton::Left, MouseButton::Middle, MouseButton::Right };
		MouseEvent ev;
		ev.button = (evnt.button <= 3) ? buttonMap[evnt.button-1] : (MouseButton)(evnt.button-3);
		ev.state = (evnt.type == SDL_MOUSEBUTTONDOWN);
		ev.position.set(evnt.x, evnt.y);
		mInputContext.applyEvent(ev);

		mCurrentEventConsumed = false;
		mRoot.mouse(ev);
	}

	void SystemManager::mousewheel(const SDL_MouseWheelEvent& evnt)
	{
		// Mouse wheel
		mInputContext.applyMouseWheel(evnt.y);
	}

	void SystemManager::update()
	{
		// Update timing
		unsigned int oldTicks = mTicks;
		mTicks = SDL_GetTicks();
		mTimeDifference = (float)(mTicks - oldTicks) * 0.001f;
		mTotalTime += mTimeDifference;

		const float dt = clamp(mTimeDifference, 0.0001f, 1.0f);
		const float adaption = expf(-dt * 10.0f);
		mFrameRate = (1.0f / dt) * (1.0f - adaption) + mFrameRate * adaption;

		// Update root GuiBase instance
		mCurrentEventConsumed = false;
		mRoot.update(dt);
		++mFrameCounter;
	}

	void SystemManager::render()
	{
		// Perform rendering
		if (!FTX::Video.valid())
			return;
		if (!FTX::Video->isActive())
			return;

		mCurrentEventConsumed = false;
		FTX::Video->beginRendering();
		mRoot.render();
		FTX::Video->endRendering();
	}

	void SystemManager::run(GuiBase& app)
	{
		RMX_LOG_INFO("SystemManager: adding root app");
		mRoot.addChild(app);
		RMX_LOG_INFO("SystemManager: root app added");
		RMX_LOG_INFO("SystemManager: entering main loop");
		run();
		RMX_LOG_INFO("SystemManager: main loop returned");
		RMX_LOG_INFO("SystemManager: removing root app");
		mRoot.removeChild(app);
		RMX_LOG_INFO("SystemManager: root app removed");
	}

	void SystemManager::mainLoop()
	{
#if defined(PLATFORM_WIIU)
		if (!WHBProcIsRunning())
		{
			quit();
			return;
		}
#endif

		mRoot.beginFrame();

		checkSDLEvents();

#if defined(PLATFORM_WIIU)
		if (!WHBProcIsRunning())
		{
			mRoot.endFrame();
			quit();
			return;
		}
#endif

		update();
		render();

		mRoot.endFrame();

#ifdef PLATFORM_WEB
		if (!mRunning)
		{
			emscripten_cancel_main_loop();
			EM_ASM(
				if(Module["onExit"])Module["onExit"]();
			);
		}
#endif
	}

	void loop_func(void* arg)
	{
		SystemManager* host = (SystemManager*)arg;
		host->mainLoop();
	}

	void SystemManager::run()
	{
		// Start
		mTicks = SDL_GetTicks();
		mRunning = true;

#ifdef PLATFORM_WEB
		emscripten_set_main_loop_arg(loop_func, (void*)this, 0, 1);
#else
		// Main loop
		while (mRunning)
		{
#if defined(PLATFORM_WIIU)
			if (!WHBProcIsRunning())
			{
				mRunning = false;
				break;
			}
#endif
			mainLoop();
		}
#endif
	}

	void SystemManager::quit()
	{
		mRunning = false;
	}

	void SystemManager::warpMouse(int x, int y)
	{
		SDL_WarpMouseInWindow(FTX::Video->mMainWindow, x, y);
		mInputContext.applyMousePos(x, y);
	}

}
