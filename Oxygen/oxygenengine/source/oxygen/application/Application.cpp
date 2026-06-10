/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/application/Application.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/application/GameLoader.h"
#include "oxygen/application/audio/AudioOutBase.h"
#include "oxygen/application/audio/AudioPlayer.h"
#include "oxygen/application/gameview/GameView.h"
#include "oxygen/application/input/ControlsIn.h"
#include "oxygen/application/input/InputManager.h"
#include "oxygen/application/menu/GameSetupScreen.h"
#include "oxygen/application/menu/OxygenMenu.h"
#include "oxygen/application/overlays/BackdropView.h"
#include "oxygen/application/overlays/CheatSheetOverlay.h"
#include "oxygen/application/overlays/DebugLogView.h"
#include "oxygen/application/overlays/DebugSidePanel.h"
#include "oxygen/application/overlays/MemoryHexView.h"
#include "oxygen/application/overlays/ProfilingView.h"
#include "oxygen/application/overlays/SaveStateMenu.h"
#include "oxygen/application/overlays/TouchControlsOverlay.h"
#include "oxygen/application/video/VideoOut.h"
#include "oxygen/menu/imgui/ImGuiIntegration.h"
#include "oxygen/helper/Logging.h"
#include "oxygen/helper/Profiling.h"
#include "oxygen/network/EngineServerClient.h"
#include "oxygen/platform/PlatformFunctions.h"
#include "oxygen/simulation/GameRecorder.h"
#include "oxygen/simulation/LogDisplay.h"
#include "oxygen/simulation/PersistentData.h"
#include "oxygen/simulation/Simulation.h"
#if defined(SUPPORT_IMGUI)
	#include "oxygen/menu/devmode/DevModeMainWindow.h"
#endif


static const float MOUSE_HIDE_TIME = 1.0f;	// Seconds until mouse cursor gets hidden after last movement

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
}


Application::Application() :
	mGameLoader(new GameLoader()),
	mSimulation(new Simulation()),
	mSaveStateMenu(new SaveStateMenu())
{
	if (hasVirtualGamepad())
	{
		mTouchControlsOverlay = new TouchControlsOverlay();
		InputManager::instance().enableTouchInput(true);
	}

	// Register profiling region IDs
	Profiling::startup();
	Profiling::registerRegion(ProfilingRegion::SIMULATION,			 "Simulation",	Color(1.0f, 1.0f, 0.0f));
	Profiling::registerRegion(ProfilingRegion::SIMULATION_USER_CALL, "User Calls",	Color(0.7f, 0.7f, 0.0f));
	Profiling::registerRegion(ProfilingRegion::AUDIO,				 "Audio",		Color::RED);
	Profiling::registerRegion(ProfilingRegion::RENDERING,			 "Rendering",	Color::BLUE);
	Profiling::registerRegion(ProfilingRegion::FRAMESYNC,			 "Frame Sync",	Color(0.3f, 0.3f, 0.3f));

	mApplicationTimer.start();
}

Application::~Application()
{
	SaveStateMenu* unparentedSaveStateMenu = (nullptr != mSaveStateMenu && nullptr == mSaveStateMenu->getParent()) ? mSaveStateMenu : nullptr;
	TouchControlsOverlay* unparentedTouchControlsOverlay = (nullptr != mTouchControlsOverlay && nullptr == mTouchControlsOverlay->getParent()) ? mTouchControlsOverlay : nullptr;

	RMX_LOG_INFO("Application destructor: deleting GUI children");
	deleteAllChildren();
	mGameView = nullptr;
	mGameSetupScreen = nullptr;
	mBackdropView = nullptr;
	mTouchControlsOverlay = nullptr;
	mCheatSheetOverlay = nullptr;
	mOxygenMenu = nullptr;
	mSaveStateMenu = nullptr;
	mDebugSidePanel = nullptr;
	mProfilingView = nullptr;
	RMX_LOG_INFO("Application destructor: GUI children deleted");

#if defined(SUPPORT_IMGUI)
	// This will also shutdown ImGui itself
	ImGuiManager::instance().clearProviders();
#endif

	RMX_LOG_INFO("Application destructor: shutting down engine server client");
	EngineServerClient::instance().shutdownClient();
	RMX_LOG_INFO("Application destructor: engine server client shut down");

	RMX_LOG_INFO("Application destructor: deleting owned systems");
	delete mGameLoader;
	delete unparentedSaveStateMenu;
	delete unparentedTouchControlsOverlay;
	delete mSimulation;
	mGameLoader = nullptr;
	mSaveStateMenu = nullptr;
	mTouchControlsOverlay = nullptr;
	mSimulation = nullptr;
	RMX_LOG_INFO("Application destructor: owned systems deleted");

	RMX_LOG_INFO("Application destructor: shutting down sockets");
	Sockets::shutdownSockets();
	RMX_LOG_INFO("Application destructor complete");
}

void Application::initialize()
{
	GuiBase::initialize();

	if (nullptr == mGameView)
	{
		RMX_LOG_INFO("Adding game view");
		mGameView = new GameView(*mSimulation);
		addChild(*mGameView);
		mBackdropView = &createChild<BackdropView>();
	}

	mWindowMode = (WindowMode)Configuration::instance().mWindowMode;

	if (EngineMain::getDelegate().useDeveloperFeatures())
	{
		RMX_LOG_INFO("Adding debug views");
		mDebugSidePanel = &createChild<DebugSidePanel>();
		createChild<MemoryHexView>();
		createChild<DebugLogView>();

	#if defined(SUPPORT_IMGUI)
		ImGuiManager::instance().getOrAddImGuiContentProvider<DevModeMainWindow>(0);
	#endif
	}

	mOxygenMenu = &createChild<OxygenMenu>();
	//mOxygenMenu->setVisible(true);
#if !(defined(PLATFORM_WIIU) && !defined(DEBUG))
	mProfilingView = &createChild<ProfilingView>();
	mCheatSheetOverlay = &createChild<CheatSheetOverlay>();
#endif

	if (nullptr != mTouchControlsOverlay && nullptr == mTouchControlsOverlay->getParent())
	{
		mTouchControlsOverlay->buildTouchControls();
		addChild(*mTouchControlsOverlay);
	}

	// Font
	mLogDisplayFont.setSize(15.0f);
	mLogDisplayFont.addFontProcessor(std::make_shared<ShadowFontProcessor>(Vec2i(1, 1), 1.0f));

	RMX_LOG_INFO("Application initialization complete");
}

void Application::deinitialize()
{
	RMX_LOG_INFO("");
	RMX_LOG_INFO("--- SHUTDOWN ---");

#if defined(PLATFORM_WIIU)
	RMX_LOG_INFO("Application shutdown: Wii U renderer teardown will run during engine shutdown");
#endif

	// Destroy game app here already, instead of using the auto-deletion of children
	if (nullptr != mGameApp)
	{
		RMX_LOG_INFO("Application shutdown: deleting game app");
		deleteChild(*mGameApp);
		mGameApp = nullptr;
		RMX_LOG_INFO("Application shutdown: game app deleted");
	}

	RMX_LOG_INFO("Application shutdown: delegate shutdownGame");
	EngineMain::getDelegate().shutdownGame();
	RMX_LOG_INFO("Application shutdown: delegate shutdownGame complete");

	// Stop all sounds and especially streaming of emulated sounds before simulation shutdown
	RMX_LOG_INFO("Application shutdown: clearing audio playback");
	EngineMain::instance().getAudioOut().getAudioPlayer().clearPlayback();
	RMX_LOG_INFO("Application shutdown: audio playback cleared");
	RMX_LOG_INFO("Application shutdown: simulation shutdown");
	mSimulation->shutdown();
	RMX_LOG_INFO("Application shutdown: simulation shutdown complete");

	// Update display index, in case the window was moved meanwhile
#if defined(PLATFORM_WIIU)
	RMX_LOG_INFO("Application shutdown complete");
#else
	if (nullptr != FTX::Video->getMainWindow())
	{
		RMX_LOG_INFO("Application shutdown: updating display index");
		updateWindowDisplayIndex();
		RMX_LOG_INFO("Application shutdown: display index updated");
	}
	RMX_LOG_INFO("Application shutdown complete");
#endif
}

void Application::beginFrame()
{
	// Handle text input
	{
		// Start or stop text input from SDL
		//  -> The start call is required to even get any "textinput" callbacks
		//  -> On devices that support it (like Android), active text input will also bring up the virtual keyboard
		if (mRequestActiveTextInput != (bool)SDL_IsTextInputActive())
		{
			if (mRequestActiveTextInput)
			{
				SDL_StartTextInput();
			}
			else
			{
				SDL_StopTextInput();
			}
		}

		// Reset for next frame, so text input gets deactivated if nobody requests it again
		mRequestActiveTextInput = false;
	}

	GuiBase::beginFrame();
}

void Application::endFrame()
{
	GuiBase::endFrame();
}

void Application::sdlEvent(const SDL_Event& ev)
{
	GuiBase::sdlEvent(ev);

	//RMX_LOG_INFO("SDL event: type = " << ev.type);

	mImGuiIntegration.processSdlEvent(ev);

	// Inform input manager as well
	if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP)
	{
		InputManager::instance().injectSDLInputEvent(ev);
	}

	// Handle events that FTX doesn't
	switch (ev.type)
	{
		case SDL_WINDOWEVENT:
		{
#if defined(PLATFORM_WIIU)
			break;
#else
			if (ev.window.windowID == SDL_GetWindowID(&EngineMain::instance().getSDLWindow()))
			{
				switch (ev.window.event)
				{
					case SDL_WINDOWEVENT_FOCUS_LOST:
					{
						EngineMain::getDelegate().onApplicationLostFocus();
						break;
					}
				}
			}
			break;
#endif
		}

		case SDL_APP_WILLENTERBACKGROUND:
		{
			EngineMain::getDelegate().onApplicationLostFocus();
			break;
		}

		case SDL_JOYDEVICEADDED:
		case SDL_CONTROLLERDEVICEADDED:
		{
			if (SDL_GetTicks() > 5000)
			{
				LogDisplay::instance().setLogDisplay("New game controller found");
				InputManager::instance().rescanRealDevices();
			}
			break;
		}

		case SDL_JOYDEVICEREMOVED:
		case SDL_CONTROLLERDEVICEREMOVED:
		case SDL_CONTROLLERDEVICEREMAPPED:
		{
			if (SDL_GetTicks() > 5000)
			{
				LogDisplay::instance().setLogDisplay((ev.type == SDL_CONTROLLERDEVICEREMAPPED) ? "Game controller mapping changed" : "Game controller was disconnected");
				InputManager::instance().rescanRealDevices();
			}
			break;
		}
	}
}

void Application::keyboard(const rmx::KeyboardEvent& ev)
{
	// Debug only
	//RMX_LOG_INFO(*String(0, "Keyboard event: key=0x%08x, scancode=0x%04x", ev.key, ev.scancode));

	if (mPausedByFocusLoss)
	{
		if (ev.state)
		{
			setPausedByFocusLoss(false);
		}
		return;
	}

	if (mImGuiIntegration.isCapturingKeyboard())
	{
		FTX::System->consumeCurrentEvent();
	}

	GuiBase::keyboard(ev);

	if (FTX::System->wasEventConsumed())
		return;

	if (ev.state)
	{
		if (FTX::keyState(SDLK_LALT) || FTX::keyState(SDLK_RALT))
		{
			// Alt pressed
			if (!ev.repeat)
			{
				// No key repeat for these
				switch (ev.key)
				{
					case SDLK_RETURN:
					{
						if (FTX::keyState(SDLK_LSHIFT))
						{
							setUnscaledWindow();
						}
						else
						{
							toggleFullscreen();
						}
						break;
					}

					case 'p':
					{
						Configuration::instance().mPerformanceDisplay = (Configuration::instance().mPerformanceDisplay + 1) % 3;
						break;
					}

					case 'r':
					{
						// Not available for normal users, as this would crash the application if OpenGL is not supported
						if (EngineMain::getDelegate().useDeveloperFeatures())
						{
							updateWindowDisplayIndex();
							std::vector<Configuration::RenderMethod> supportedMethods;
							for (const Configuration::RenderMethod renderMethod : { Configuration::RenderMethod::SOFTWARE, Configuration::RenderMethod::D3D11_SOFT, Configuration::RenderMethod::D3D11_FULL, Configuration::RenderMethod::VULKAN_SOFT, Configuration::RenderMethod::OPENGL_SOFT, Configuration::RenderMethod::OPENGL_FULL })
							{
								if (Configuration::isSupportedRenderMethod(renderMethod))
									supportedMethods.push_back(renderMethod);
							}

							Configuration::RenderMethod newRenderMethod = Configuration::instance().mRenderMethod;
							for (size_t index = 0; index < supportedMethods.size(); ++index)
							{
								if (supportedMethods[index] == Configuration::instance().mRenderMethod)
								{
									newRenderMethod = supportedMethods[(index + 1) % supportedMethods.size()];
									break;
								}
							}
							EngineMain::instance().switchToRenderMethod(newRenderMethod);
							LogDisplay::instance().setLogDisplay(std::string("Switched to ") + Configuration::getRenderMethodConfigString(Configuration::instance().mRenderMethod) + " renderer");
						}
						break;
					}

					case SDLK_END:
					{
						if (FTX::keyState(SDLK_RSHIFT))
						{
							// Intentional crash by null pointer exception when pressing Alt + RShift + End
							int* ptr = reinterpret_cast<int*>(mRemoveChild);	// This is usually a null pointer at this point
							*ptr = 0;
						}
					}
				}
			}
		}
		else
		{
			// Alt not pressed
			if (!ev.repeat)
			{
				// No key repeat for these
				switch (ev.key)
				{
					case SDLK_F1:
					{
						if (FTX::keyState(SDLK_LSHIFT) && EngineMain::getDelegate().useDeveloperFeatures())
						{
							PlatformFunctions::openFileExternal(L"config.json");
						}
					#ifdef SUPPORT_IMGUI
						else if (EngineMain::getDelegate().useDeveloperFeatures())
						{
							DevModeMainWindow* devModeMainWindow = ImGuiManager::instance().getImGuiContentProvider<DevModeMainWindow>();
							if (nullptr != devModeMainWindow)
							{
								devModeMainWindow->setIsWindowOpen(!devModeMainWindow->getIsWindowOpen());
							}
						}
					#endif
						else if (nullptr != mCheatSheetOverlay)
						{
							mCheatSheetOverlay->toggle();
						}
						break;
					}

					case SDLK_F2:
					{
						triggerGameRecordingSave();
						break;
					}

					case SDLK_F3:
					{
						const InputManager::RescanResult result = InputManager::instance().rescanRealDevices();
						LogDisplay::instance().setLogDisplay(String(0, "Re-scanned connected game controllers: %d found", result.mGamepadsFound));
						break;
					}

					case SDLK_F4:
					{
						const bool switched = ControlsIn::instance().switchGamepads();
						LogDisplay::instance().setLogDisplay(switched ? "Switched gamepads (switched)" : "Switched gamepads (original)");
						break;
					}

					case SDLK_F5:
					{
						// Save state menu
						if (EngineMain::getDelegate().useDeveloperFeatures())
						{
							if (!mSaveStateMenu->isActive() && mSimulation->isRunning())
							{
								addChild(*mSaveStateMenu);
								mSaveStateMenu->init(false);
								mSimulation->setSpeed(0.0f);
							}
						}
						break;
					}

					case SDLK_F8:
					{
						// This feature is hidden in non-developer environment -- you have to press right (!) shift as well
						if (EngineMain::getDelegate().useDeveloperFeatures() || FTX::keyState(SDLK_RSHIFT))
						{
							// Load state menu
							if (!mSaveStateMenu->isActive() && mSimulation->isRunning())
							{
								addChild(*mSaveStateMenu);
								mSaveStateMenu->init(true);
								mSimulation->setSpeed(0.0f);
							}
						}
						break;
					}

					case SDLK_PRINTSCREEN:
					{
						// Saving a screenshot to disk is meant to be developer-only, as the "getScreenshot" call can crash the application for some users
						//  (Yes, I had this active for everyone in the early days of S3AIR)
						if (EngineMain::getDelegate().useDeveloperFeatures() && nullptr != mGameView)
						{
							const std::string filename = "screenshot_" + rmx::getTimestampStringForFilename() + ".bmp";
							Bitmap bitmap;
							mGameView->getScreenshot(bitmap);
							bitmap.save(String(filename).toStdWString());
							LogDisplay::instance().setLogDisplay("Screenshot saved as \"" + filename + "\"");
						}
						break;
					}

				#ifdef DEBUG
					case 'r':
					{
						// Only for debugging visual differences between hardware and software renderers
						if (Configuration::isOpenGLRenderMethod(Configuration::instance().mRenderMethod))
						{
							const Configuration::RenderMethod newRenderMethod = (Configuration::instance().mRenderMethod == Configuration::RenderMethod::OPENGL_SOFT) ? Configuration::RenderMethod::OPENGL_FULL : Configuration::RenderMethod::OPENGL_SOFT;
							EngineMain::instance().switchToRenderMethod(newRenderMethod);
							LogDisplay::instance().setLogDisplay(std::string("Switched to ") + Configuration::getRenderMethodConfigString(Configuration::instance().mRenderMethod) + " renderer");
						}
						break;
					}
				#endif
				}
			}

			// Key repeat is fine for these
			switch (ev.key)
			{
				case SDLK_KP_PLUS:
				case SDLK_KP_MINUS:
				{
					int volume = roundToInt(Configuration::instance().mAudio.mMasterVolume * 100.0f);
					volume = clamp((ev.key == SDLK_KP_PLUS) ? volume + 5 : volume - 5, 0, 100);
					Configuration::instance().mAudio.mMasterVolume = (float)volume / 100.0f;
					LogDisplay::instance().setLogDisplay(String(0, "Audio volume: %d%%", volume));
					break;
				}

				case SDLK_KP_DIVIDE:
				case SDLK_KP_MULTIPLY:
				{
					// Resolution changes are potentially game breaking, hence developer-only
					if (EngineMain::getDelegate().useDeveloperFeatures())
					{
						VideoOut& videoOut = VideoOut::instance();
						uint32 width = videoOut.getScreenWidth();
						uint32 height = videoOut.getScreenHeight();

						width += (ev.key == SDLK_KP_MULTIPLY) ? 16 : -16;
						width = clamp(width, 320, 496);
						height = 224;

						videoOut.setScreenSize(width, height);
						LogDisplay::instance().setLogDisplay("Changed render resolution to " + std::to_string(width) + " x " + std::to_string(height) + " pixels");
					}
					break;
				}
			}
		}
	}
}

void Application::mouse(const rmx::MouseEvent& ev)
{
	if (mImGuiIntegration.isCapturingMouse())
	{
		FTX::System->consumeCurrentEvent();
	}

	GuiBase::mouse(ev);
}

void Application::update(float timeElapsed)
{
	if (mIsVeryFirstFrameForLogging)
	{
		RMX_LOG_INFO("Start of first application update call");
	}

	EngineMain::instance().applyPendingRenderMethodSwitch();

#if defined(PLATFORM_UWP)
	static bool sEnsuredFullscreen = false;
	static float sStartupGamepadRescanTimeout = 0.0f;
	static int sStartupGamepadRescanAttempts = 0;
	if (!sEnsuredFullscreen && mWindowMode != WindowMode::WINDOWED)
	{
		sEnsuredFullscreen = true;

		SDL_Window* window = FTX::Video->getMainWindow();
		if (nullptr != window)
		{
			SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);

			int width = 0;
			int height = 0;
			getWindowSizeForRendering(window, width, height);
			FTX::Video->reshape(width, height);
		}
	}

	if (sStartupGamepadRescanAttempts < 20 && InputManager::instance().getGamepads().empty())
	{
		sStartupGamepadRescanTimeout -= timeElapsed;
		if (sStartupGamepadRescanTimeout <= 0.0f)
		{
			sStartupGamepadRescanTimeout = 0.25f;
			++sStartupGamepadRescanAttempts;

			SDL_PumpEvents();
			const InputManager::RescanResult result = InputManager::instance().rescanRealDevices(true);
			if (result.mGamepadsFound > 0)
			{
				RMX_LOG_INFO("Startup controller rescan succeeded with " << result.mGamepadsFound << " detected gamepad(s)");
			}
		}
	}
#endif

	// ImGui frame start must be done here (instead of at the start of "render"), to ensure that the mouse capturing flag is set correctly
	//  -> This is particularly relevant for touch input, where we would miss the first touch into an ImGui window and falsely pass it to the touch overlay
	mImGuiIntegration.startFrame();
	if (mImGuiIntegration.isCapturingMouse() || mImGuiIntegration.isCapturingKeyboard() || mImGuiIntegration.hasBlockingImGuiWindow())
	{
		FTX::System->consumeCurrentEvent();
	}

	// Global slow motion for debugging menu transitions etc.
	const bool isDeveloperMode = EngineMain::getDelegate().useDeveloperFeatures();
	if (isDeveloperMode && FTX::keyState(SDLK_RSHIFT))
	{
		timeElapsed /= 10.0f;
	}

	// Update loading
	if (mGameLoader->isLoading())
	{
		if (nullptr == mGameSetupScreen)
		{
			mGameSetupScreen = &mGameView->createChild<GameSetupScreen>();
		}

		updateLoading();
	}
	else
	{
		if (nullptr != mGameSetupScreen)
		{
			mGameView->deleteChild(*mGameSetupScreen);
			mGameSetupScreen = nullptr;
		}
	}

	// Update engine server client and netplay
#if !(defined(PLATFORM_WIIU) && !defined(DEBUG))
	EngineServerClient::instance().updateClient(timeElapsed);
#endif

	// Update drawer
	EngineMain::instance().getDrawer().updateDrawer(timeElapsed);

	// Update input
	InputManager::instance().updateInput(timeElapsed);

	if (mPausedByFocusLoss)
	{
		if (InputManager::instance().anythingPressed())
		{
			setPausedByFocusLoss(false);
		}

		// Skip the rest
		return;
	}

	// Update simulation
	Profiling::pushRegion(ProfilingRegion::SIMULATION);
	mSimulation->update(timeElapsed);
	Profiling::popRegion(ProfilingRegion::SIMULATION);

	// Update game
	EngineMain::getDelegate().updateGame(timeElapsed);

	// Update audio
	Profiling::pushRegion(ProfilingRegion::AUDIO);
	EngineMain::instance().getAudioOut().realtimeUpdate(timeElapsed);
	Profiling::popRegion(ProfilingRegion::AUDIO);

	if (isDeveloperMode)
	{
		// Update debugging stuff
		Profiling::pushRegion(ProfilingRegion::SIMULATION);
		mSimulation->refreshDebugging();
		Profiling::popRegion(ProfilingRegion::SIMULATION);
	}

	// GUI
	LogDisplay& logDisplay = LogDisplay::instance();
	logDisplay.mLogDisplayTimeout = std::max(logDisplay.mLogDisplayTimeout - std::min(timeElapsed, 0.1f), 0.0f);

	mGameView->earlyUpdate(timeElapsed);
	GuiBase::update(timeElapsed);

	if (nullptr != mRemoveChild)
	{
		removeChild(*mRemoveChild);
		mRemoveChild = nullptr;
	}

	if (FTX::mouseRel() != Vec2i() || FTX::mouseState(rmx::MouseButton::Left) || FTX::mouseState(rmx::MouseButton::Right) || mImGuiIntegration.isCapturingMouse())
	{
		mMouseHideTimer = 0.0f;
		SDL_ShowCursor(1);
	}
	else if (mMouseHideTimer < MOUSE_HIDE_TIME)
	{
		mMouseHideTimer += timeElapsed;
		if (mMouseHideTimer >= MOUSE_HIDE_TIME)
			SDL_ShowCursor(0);
	}

	// Update persistent data
	PersistentData::instance().updatePersistentData();

	if (mIsVeryFirstFrameForLogging)
	{
		RMX_LOG_INFO("End of first application render call");
	}
}

void Application::render()
{
	Profiling::pushRegion(ProfilingRegion::RENDERING);

	if (mIsVeryFirstFrameForLogging)
	{
		RMX_LOG_INFO("Start of first application render call");
	}

	if (mImGuiIntegration.isCapturingMouse())
	{
		FTX::System->consumeCurrentEvent();
	}

	Drawer& drawer = EngineMain::instance().getDrawer();
#if defined(PLATFORM_WIIU)
	drawer.setupRenderWindow(nullptr);
#else
	drawer.setupRenderWindow(&EngineMain::instance().getSDLWindow());
#endif

	if (mImGuiIntegration.hasBlockingImGuiWindow())
	{
		mPausedByFocusLoss = false;
	}
	else
	{
		GuiBase::render();

		// TODO: This gets called too late
		mBackdropView->setGameViewRect(mGameView->getGameViewportRect());

		// Show log display output
		{
			LogDisplay& logDisplay = LogDisplay::instance();

			if (!logDisplay.mModeDisplayString.empty())
			{
				const Recti rect(0, 0, FTX::screenWidth(), 26);
				drawer.drawRect(rect, Color(0.4f, 0.4f, 0.4f, 0.4f));
				drawer.printText(mLogDisplayFont, Vec2i(5, 5), logDisplay.mModeDisplayString);
			}

			if (logDisplay.mLogDisplayTimeout > 0.0f)
			{
				drawer.printText(mLogDisplayFont, Vec2i(5, FTX::screenHeight() - 25), logDisplay.mLogDisplayString, 1, Color(1.0f, 1.0f, 1.0f, saturate(logDisplay.mLogDisplayTimeout / 0.25f)));
			}

			if (!logDisplay.mLogErrorStrings.empty())
			{
				Vec2i pos(5, FTX::screenHeight() - 30 - (int)logDisplay.mLogErrorStrings.size() * 20);
				for (const String& error : logDisplay.mLogErrorStrings)
				{
					drawer.printText(mLogDisplayFont, pos, error, 1, Color(1.0f, 0.2f, 0.2f));
					pos.y += 20;
				}
			}
		}

		if (mPausedByFocusLoss)
		{
			drawer.drawRect(FTX::screenRect(), Color(0.0f, 0.0f, 0.0f, 0.8f));

		#if defined(PLATFORM_ANDROID) || defined(PLATFORM_WEB) || defined(PLATFORM_IOS)
			constexpr uint64 key = rmx::constMurmur2_64("auto_pause_text_tap");
		#else
			constexpr uint64 key = rmx::constMurmur2_64("auto_pause_text_key");
		#endif
			const float scale = (float)(FTX::screenHeight() / 160);		// A bit larger than the usual upscaled pixel size
			drawer.drawSprite(FTX::screenSize() / 2, key, Color(0.3f, 1.0f, 1.0f), Vec2f(scale));
		}
	}

	drawer.performRendering();

	mImGuiIntegration.buildContents();
	mImGuiIntegration.endFrame();

	// Needed only for precise profiling
	//glFinish();

	Profiling::popRegion(ProfilingRegion::RENDERING);

	// Update profiling data & explicit buffer swap
	{
		Profiling::pushRegion(ProfilingRegion::FRAMESYNC);

		const Configuration& config = Configuration::instance();
		const double currentTime = mApplicationTimer.getSecondsSinceStart() * 1000.0;
		const double tickLengthMilliseconds = 1000.0 / (double)mSimulation->getSimulationFrequency();
		const bool useVSync = Configuration::useVSync(config.mFrameSync);
		const bool useFrameCap = Configuration::useFrameCap(config.mFrameSync) || (useVSync && !Configuration::renderMethodSupportsNativeVSync(config.mRenderMethod));
#if defined(PLATFORM_WIIU)
		static bool sLoggedWiiUFrameSync = false;
		if (!sLoggedWiiUFrameSync)
		{
			RMX_LOG_INFO("Application: Wii U frame sync simFreq=" << mSimulation->getSimulationFrequency()
				<< " frameSync=" << (int)config.mFrameSync
				<< " renderMethod=" << (int)config.mRenderMethod
				<< " useVSync=" << (useVSync ? 1 : 0)
				<< " useFrameCap=" << (useFrameCap ? 1 : 0)
				<< " tickMs=" << tickLengthMilliseconds);
			sLoggedWiiUFrameSync = true;
		}
#endif
		if (useFrameCap)
		{
			double delay = mNextRefreshTime - currentTime;
			if (delay < 0.0 || delay > tickLengthMilliseconds)
			{
				// No delay in these cases
				mNextRefreshTime = currentTime + tickLengthMilliseconds;
			}
			else
			{
				mNextRefreshTime += tickLengthMilliseconds;
				PlatformFunctions::preciseDelay(delay);
			}
		}
		else if (useVSync)
		{
			// Rely on V-Sync, but still use a minimum delay in case it's off
			double delay = tickLengthMilliseconds - Profiling::getRootRegion().mTimer.getAccumulatedSeconds() * 1000.0;
			if (delay >= 1.0)
			{
				SDL_Delay(1);	// No precise timing should be needed here
			}
		}

		if (mIsVeryFirstFrameForLogging)
		{
			RMX_LOG_INFO("First present screen call");
		}

		drawer.presentScreen();

#if defined(PLATFORM_WIIU)
		static constexpr bool ENABLE_WIIU_RUNTIME_CADENCE_LOGS = false;
		if constexpr (ENABLE_WIIU_RUNTIME_CADENCE_LOGS)
		{
			static double sLastCadenceLogTime = 0.0;
			static int sPresentedFrames = 0;
			static int sCadenceLogCount = 0;

			++sPresentedFrames;
			const double cadenceTime = mApplicationTimer.getSecondsSinceStart() * 1000.0;
			if (sLastCadenceLogTime <= 0.0)
			{
				sLastCadenceLogTime = cadenceTime;
				sPresentedFrames = 0;
			}
			else if (sCadenceLogCount < 24 && cadenceTime - sLastCadenceLogTime >= 2000.0)
			{
				const double elapsedMs = cadenceTime - sLastCadenceLogTime;
				const double presentRate = (double)sPresentedFrames * 1000.0 / elapsedMs;
				const float systemRate = (nullptr != FTX::System) ? FTX::System->getFramerate() : 0.0f;
				RMX_LOG_INFO("Application: Wii U cadence presents=" << sPresentedFrames
					<< " elapsedMs=" << elapsedMs
					<< " presentHz=" << presentRate
					<< " systemFps=" << systemRate
					<< " frameSync=" << (int)config.mFrameSync
					<< " useVSync=" << (useVSync ? 1 : 0)
					<< " useFrameCap=" << (useFrameCap ? 1 : 0));
				static std::vector<std::pair<Profiling::Region*, int>> sProfilingRegions;
				Profiling::listRegionsRecursive(sProfilingRegions);
				RMX_LOG_INFO("Application: Wii U profiling rootMs=" << (Profiling::getRootRegion().mAverageTime * 1000.0)
					<< " simPerSec=" << Profiling::getAdditionalData().mAverageSimulationsPerSecond
					<< " smoothSimPerSec=" << Profiling::getAdditionalData().mSmoothedSimulationsPerSecond);
				for (const std::pair<Profiling::Region*, int>& pair : sProfilingRegions)
				{
					const Profiling::Region& region = *pair.first;
					RMX_LOG_INFO("Application: Wii U profiling region " << region.mName
						<< " ms=" << (region.mAverageTime * 1000.0));
				}
				sLastCadenceLogTime = cadenceTime;
				sPresentedFrames = 0;
				++sCadenceLogCount;
			}
		}
#endif

	#if 0
		// Use a glFinish or glFlush here...?
		//  -> PRO
		//     - glFinish (but not glFlush) helps to give a precise measurement of frame sync time on Windows
		//     - It also prevents stutters (at least one when the first sound effect gets played, for whatever reason that happens)
		//  -> CONTRA
		//     - I previously found that glFinish seems to produce double output of the same frame here and there, but only when combined with the glFinish call above
		//     - glFinish introduces performance issues on Android and potentially on weak machines in general (the more lightweight glFlush does not)
		//  -> Conclusion:
		//     - Better leave both out; glFinish is too expensive, and glFlush doesn't really have much of an effect
		glFinish();
	#endif

		Profiling::popRegion(ProfilingRegion::FRAMESYNC);
		Profiling::nextFrame(mSimulation->getFrameNumber());
	}

	if (mIsVeryFirstFrameForLogging)
	{
		RMX_LOG_INFO("End of first application render call");
		RMX_LOG_INFO("Ready to go");
		mIsVeryFirstFrameForLogging = false;
	}
}

void Application::childClosed(GuiBase& child)
{
	if (mSimulation->isRunning())
	{
		mSimulation->setSpeed(mSimulation->getDefaultSpeed());
	}
	mRemoveChild = &child;
}

void Application::setWindowMode(WindowMode windowMode, bool force)
{
	if (mWindowMode == windowMode && !force)
		return;

	SDL_Window* window = FTX::Video->getMainWindow();
	const int displayIndex = updateWindowDisplayIndex();

	switch (windowMode)
	{
		default:
		case WindowMode::WINDOWED:
		{
			if (mWindowMode >= WindowMode::FULLSCREEN_DESKTOP)
			{
				// Exit fullscreen first
				SDL_SetWindowFullscreen(window, 0);
			}

			SDL_SetWindowSize(window, Configuration::instance().mWindowSize.x, Configuration::instance().mWindowSize.y);
			SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex), SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex));
			SDL_SetWindowResizable(window, SDL_TRUE);
			SDL_SetWindowBordered(window, SDL_TRUE);
			break;
		}

		case WindowMode::FULLSCREEN_BORDERLESS:
		{
			if (mWindowMode >= WindowMode::FULLSCREEN_DESKTOP)
			{
				// Exit fullscreen first
				SDL_SetWindowFullscreen(window, 0);
			}

			SDL_Rect rect;
			if (SDL_GetDisplayBounds(displayIndex, &rect) == 0)
			{
				SDL_SetWindowSize(window, rect.w, rect.h);
				SDL_SetWindowPosition(window, rect.x, rect.y);
				SDL_SetWindowResizable(window, SDL_FALSE);
				SDL_SetWindowBordered(window, SDL_FALSE);
			}
			else
			{
				SDL_DisplayMode dm;
				if (SDL_GetDesktopDisplayMode(displayIndex, &dm) == 0)
				{
					SDL_SetWindowSize(window, dm.w, dm.h);
					SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
					SDL_SetWindowResizable(window, SDL_FALSE);
					SDL_SetWindowBordered(window, SDL_FALSE);
				}
			}
			break;
		}

		case WindowMode::FULLSCREEN_DESKTOP:
		{
			SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
			break;
		}

		case WindowMode::FULLSCREEN_EXCLUSIVE:
		{
			SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
			break;
		}
	}

	int width, height;
	getWindowSizeForRendering(window, width, height);
	FTX::Video->reshape(width, height);

	SDL_ShowCursor(windowMode == WindowMode::WINDOWED);

	mWindowMode = windowMode;
}

void Application::toggleFullscreen()
{
	if (getWindowMode() == WindowMode::WINDOWED)
	{
	#if defined(PLATFORM_LINUX)
		// Under Linux, the fullscreen with desktop resolution works better, so that's the default
		setWindowMode(WindowMode::FULLSCREEN_DESKTOP);
	#else
		setWindowMode(WindowMode::FULLSCREEN_BORDERLESS);
	#endif
	}
	else
	{
		setWindowMode(WindowMode::WINDOWED);
	}
}

void Application::enablePauseOnFocusLoss()
{
	setPausedByFocusLoss(true);
}

void Application::triggerGameRecordingSave()
{
	if (mSimulation->getGameRecorder().isRecording())
	{
		WString filename;
		const uint32 numFrames = mSimulation->saveGameRecording(&filename);
		LogDisplay::instance().setLogDisplay(String(0, "Saved recording of last %d seconds in '%s'", numFrames / 60, *filename.toString()));
	}
}

bool Application::hasKeyboard() const
{
#if defined(PLATFORM_HAS_HARDWARE_KEYBOARD)
	// It should be safe to assume that desktop platforms always have a keyboard
	return true;
#else
	// For other platforms, ask the input manager, as it tracks whether any key was ever pressed
	return InputManager::instance().hasKeyboard();
#endif
}

bool Application::hasVirtualGamepad() const
{
	return (EngineMain::instance().getPlatformFlags() & 0x0002) != 0;
}

void Application::requestActiveTextInput()
{
	mRequestActiveTextInput = true;
}

int Application::updateWindowDisplayIndex()
{
	const int displayIndex = SDL_GetWindowDisplayIndex(FTX::Video->getMainWindow());
	if (displayIndex != -1)
	{
		Configuration::instance().mDisplayIndex = displayIndex;
		return displayIndex;
	}
	return std::max(0, Configuration::instance().mDisplayIndex);
}

void Application::setUnscaledWindow()
{
	// Cycle through the different scaling factors
	Vec2i desktopSize;
	{
		const int displayIndex = updateWindowDisplayIndex();
		SDL_Rect rect;
		if (SDL_GetDisplayBounds(displayIndex, &rect) == 0)
		{
			desktopSize.set(rect.w, rect.h);
		}
		else
		{
			SDL_DisplayMode dm;
			if (SDL_GetDesktopDisplayMode(displayIndex, &dm) == 0)
			{
				desktopSize.set(dm.w, dm.h);
			}
		}
	}

	const Vec2i gameScreenSize = VideoOut::instance().getScreenSize();
	int currentScale = 0;
	{
		const int maxScale = std::min(desktopSize.x / gameScreenSize.x, desktopSize.y / gameScreenSize.y);
		if (getWindowMode() == WindowMode::WINDOWED)
		{
			for (int scale = 1; scale < maxScale; ++scale)
			{
				if (Configuration::instance().mWindowSize == gameScreenSize * scale)
				{
					currentScale = scale;
					break;
				}
			}
		}
	}

	const int newScale = currentScale + 1;
	Configuration::instance().mWindowSize = gameScreenSize * newScale;
	setWindowMode(WindowMode::WINDOWED, true);
}

bool Application::updateLoading()
{
	while (true)
	{
		const GameLoader::UpdateResult updateResult = mGameLoader->updateLoading();
		switch (updateResult)
		{
			case GameLoader::UpdateResult::SUCCESS:
			{
				// The simulation startup may fail, and this should lead to the application not starting at all
				RMX_LOG_INFO("Simulation startup");
				if (!mSimulation->startup())
				{
					RMX_LOG_INFO("Simulation startup failed");

					// TODO: Handle this better
					FTX::System->quit();
					return false;
				}

				// If the application was only started to e.g. perform nativization, then exit now
				if (Configuration::instance().mExitAfterScriptLoading)
				{
					FTX::System->quit();
					return false;
				}

				// Startup game
				EngineMain::getDelegate().startupGame(mSimulation->getEmulatorInterface());

				RMX_LOG_INFO("Adding game app instance");
				mGameApp = &EngineMain::getDelegate().createGameApp();
				addChild(*mGameApp);
				break;
			}

			case GameLoader::UpdateResult::FAILURE:
			{
				// TODO: Handle this better
				FTX::System->quit();
				return false;
			}

			default:
				break;
		}

		// Return if no immediate update is requested
		if (updateResult != GameLoader::UpdateResult::CONTINUE_IMMEDIATE)
			break;
	}
	return true;
}

void Application::setPausedByFocusLoss(bool enable)
{
	if (mPausedByFocusLoss != enable)
	{
		mPausedByFocusLoss = enable;
		mSimulation->setRunning(!enable);
	}
}
