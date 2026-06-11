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
	#include <coreinit/foreground.h>
	#include <coreinit/thread.h>
	#include <coreinit/time.h>
	#include <gx2/event.h>
	#include <proc_ui/procui.h>
	#include <sysapp/launch.h>
	#include <whb/proc.h>

#endif


namespace rmx
{

#if defined(PLATFORM_WIIU)
	namespace
	{
		enum class ProcUIFrameState
		{
			FOREGROUND,
			BACKGROUND,
			EXITING
		};

		void releaseProcUIDrawDone(const char* reason)
		{
			RMX_LOG_INFO("SystemManager: ProcUI draw done release (" << reason << ")");
			GX2DrawDone();
			ProcUIDrawDoneRelease();
		}

		bool procUIShutdownReady()
		{
			return ProcUIIsRunning() && ProcUIInShutdown();
		}

		void stopWHBProcForExit(const char* reason)
		{
			static bool sLaunchMenuRequested = false;
			if (!sLaunchMenuRequested)
			{
				RMX_LOG_INFO("SystemManager: requesting HOME menu after ProcUI exit");
				SYSLaunchMenu();
				sLaunchMenuRequested = true;
			}
			RMX_LOG_INFO("SystemManager: stopping WHBProc run flag for " << reason);
			WHBProcStopRunning();
			RMX_LOG_INFO("SystemManager: WHBProc run flag stopped for " << reason);
		}

		uint32 procUIReleaseCallback(void* param)
		{
			SystemManager* system = reinterpret_cast<SystemManager*>(param);
			if (nullptr != system)
				system->notifyWiiUProcUIReleaseFromCallback();
			return 0;
		}

		uint32 procUIExitCallback(void* param)
		{
			SystemManager* system = reinterpret_cast<SystemManager*>(param);
			if (nullptr != system)
				system->notifyWiiUProcUIExitFromCallback();
			return 0;
		}

		ProcUIFrameState processProcUIState(bool& outRenderAllowed, bool& outReleaseAcknowledged, SystemManager::WiiUProcUIForegroundReleaseHandler foregroundReleaseHandler)
		{
			outRenderAllowed = true;
			if (!ProcUIIsRunning())
				return ProcUIFrameState::FOREGROUND;

			const ProcUIStatus status = ProcUIProcessMessages(TRUE);
			switch (status)
			{
				case PROCUI_STATUS_IN_FOREGROUND:
					outReleaseAcknowledged = false;
					return ProcUIFrameState::FOREGROUND;

				case PROCUI_STATUS_RELEASE_FOREGROUND:
					RMX_LOG_INFO("SystemManager: ProcUI requested foreground release");
					outRenderAllowed = false;
					if (nullptr != foregroundReleaseHandler)
					{
						RMX_LOG_INFO("SystemManager: releasing app foreground resources");
						foregroundReleaseHandler();
						RMX_LOG_INFO("SystemManager: app foreground resources released");
					}
					else
					{
						RMX_LOG_WARNING("SystemManager: ProcUI foreground release has no app release handler");
					}
					releaseProcUIDrawDone("release foreground");
					outReleaseAcknowledged = true;
					return ProcUIFrameState::BACKGROUND;

				case PROCUI_STATUS_IN_BACKGROUND:
					outRenderAllowed = false;
					OSSleepTicks(OSMillisecondsToTicks(16));
					return ProcUIFrameState::BACKGROUND;

				case PROCUI_STATUS_EXITING:
					RMX_LOG_INFO("SystemManager: ProcUI requested exit");
					outRenderAllowed = false;
					outReleaseAcknowledged = true;
					return ProcUIFrameState::EXITING;
			}

			return ProcUIFrameState::FOREGROUND;
		}
	}
#endif

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
		if (!ProcUIIsRunning())
		{
			WHBProcInit();
			mProcUIInitialized = true;
			mProcUIRenderAllowed = true;
			mProcUIExitRequested = false;
			mProcUIReleaseRequested = false;
			mProcUIReleaseAcknowledged = false;
			ProcUIRegisterCallback(PROCUI_CALLBACK_RELEASE, procUIReleaseCallback, this, 100);
			ProcUIRegisterCallback(PROCUI_CALLBACK_EXIT, procUIExitCallback, this, 100);
		}
		OSEnableForegroundExit();
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
		mProcUIRenderAllowed = false;
		if (mProcUIInitialized)
		{
			ProcUIClearCallbacks();
			RMX_LOG_INFO("SystemManager: stopping WHBProc run flag");
			WHBProcStopRunning();
			RMX_LOG_INFO("SystemManager: WHBProcShutdown begin");
			WHBProcShutdown();
			mProcUIInitialized = false;
			mProcUIExitRequested = false;
			mProcUIReleaseRequested = false;
			mProcUIReleaseAcknowledged = false;
		}
#endif
	}

#if defined(PLATFORM_WIIU)
	void SystemManager::releaseWiiUProcUIForeground(const char* reason)
	{
		if (mProcUIReleaseAcknowledged)
			return;

		mProcUIRenderAllowed = false;
		RMX_LOG_INFO("SystemManager: releasing ProcUI foreground (" << reason << ")");
		if (nullptr != mProcUIForegroundReleaseHandler)
		{
			RMX_LOG_INFO("SystemManager: releasing app foreground resources");
			mProcUIForegroundReleaseHandler();
			RMX_LOG_INFO("SystemManager: app foreground resources released");
		}
		else
		{
			RMX_LOG_WARNING("SystemManager: ProcUI foreground release has no app release handler");
		}
		releaseProcUIDrawDone(reason);
		mProcUIReleaseAcknowledged = true;
		mProcUIReleaseRequested = false;
	}

	void SystemManager::notifyWiiUProcUIReleaseFromCallback()
	{
		if (mProcUIReleaseAcknowledged)
		{
			static uint32 sIgnoredReleaseCallbackLogCount = 0;
			if (sIgnoredReleaseCallbackLogCount < 8)
			{
				RMX_LOG_INFO("SystemManager: ignoring stale ProcUI release callback after draw-done acknowledgement");
				++sIgnoredReleaseCallbackLogCount;
			}
			return;
		}
		if (mProcUIRenderAllowed)
		{
			RMX_LOG_INFO("SystemManager: ProcUI release callback blocked rendering");
		}
		mProcUIRenderAllowed = false;
		mProcUIReleaseRequested = true;
	}

	void SystemManager::notifyWiiUProcUIExitFromCallback()
	{
		if (!mProcUIExitRequested)
		{
			RMX_LOG_INFO("SystemManager: ProcUI exit callback requested shutdown");
		}
		mProcUIRenderAllowed = false;
		mProcUIExitRequested = true;
		mProcUIReleaseRequested = false;
	}
#endif

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
#if defined(PLATFORM_WIIU)
		if (!mProcUIRenderAllowed || mProcUIExitRequested || !ProcUIInForeground())
			return;
#endif

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
		const bool wasProcUIRenderAllowed = mProcUIRenderAllowed;
		const ProcUIFrameState procUIFrameState = processProcUIState(mProcUIRenderAllowed, mProcUIReleaseAcknowledged, mProcUIForegroundReleaseHandler);
		if (procUIFrameState == ProcUIFrameState::EXITING)
		{
			mProcUIReleaseRequested = false;
			mProcUIExitRequested = true;
		}
		if (mProcUIExitRequested)
		{
			mProcUIReleaseRequested = false;
			mProcUIRenderAllowed = false;
			if (procUIShutdownReady())
			{
				mProcUIReleaseAcknowledged = true;
				stopWHBProcForExit("deferred exit");
				quit();
			}
			else
			{
				static uint32 sDeferredExitWaitLogCount = 0;
				if (sDeferredExitWaitLogCount < 8)
				{
					RMX_LOG_INFO("SystemManager: exit callback pending real ProcUI shutdown");
					++sDeferredExitWaitLogCount;
				}
				OSSleepTicks(OSMillisecondsToTicks(16));
			}
			return;
		}
		if (procUIFrameState == ProcUIFrameState::BACKGROUND)
		{
			if (mProcUIReleaseRequested && !mProcUIReleaseAcknowledged)
				releaseWiiUProcUIForeground("background callback");
			return;
		}
		if (!wasProcUIRenderAllowed && mProcUIRenderAllowed)
		{
			mProcUIReleaseRequested = false;
			mProcUIReleaseAcknowledged = false;
			++mProcUIForegroundGeneration;
			RMX_LOG_INFO("SystemManager: ProcUI foreground acquired generation=" << mProcUIForegroundGeneration);
		}
#endif

		mRoot.beginFrame();

		checkSDLEvents();

#if defined(PLATFORM_WIIU)
		if (mProcUIReleaseRequested || !ProcUIInForeground())
		{
			releaseWiiUProcUIForeground(mProcUIReleaseRequested ? "mid-frame callback" : "mid-frame foreground lost");
			mRoot.endFrame();
			return;
		}
#endif

		update();

#if defined(PLATFORM_WIIU)
		if (mProcUIExitRequested)
		{
			mProcUIReleaseRequested = false;
			mProcUIRenderAllowed = false;
			if (procUIShutdownReady())
			{
				mProcUIReleaseAcknowledged = true;
				stopWHBProcForExit("post-update exit");
				quit();
			}
			mRoot.endFrame();
			return;
		}
		if (mProcUIReleaseRequested || !ProcUIInForeground())
		{
			releaseWiiUProcUIForeground(mProcUIReleaseRequested ? "post-update callback" : "post-update foreground lost");
			mRoot.endFrame();
			return;
		}
		if (!mProcUIRenderAllowed)
		{
			mRoot.endFrame();
			return;
		}
#endif

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
