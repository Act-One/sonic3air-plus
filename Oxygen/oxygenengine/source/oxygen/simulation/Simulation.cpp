/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/simulation/Simulation.h"
#include "oxygen/simulation/CodeExec.h"
#include "oxygen/simulation/EmulatorInterface.h"
#include "oxygen/simulation/GameRecorder.h"
#include "oxygen/simulation/LogDisplay.h"
#include "oxygen/simulation/SaveStateSerializer.h"
#include "oxygen/simulation/SimulationState.h"
#include "oxygen/simulation/analyse/ROMDataAnalyser.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/application/audio/AudioOutBase.h"
#include "oxygen/application/input/InputRecorder.h"
#include "oxygen/application/modding/ModManager.h"
#include "oxygen/application/video/VideoOut.h"
#include "oxygen/helper/Logging.h"
#include "oxygen/network/netplay/NetplayManager.h"
#include "oxygen/platform/PlatformFunctions.h"
#include "oxygen/rendering/parts/RenderParts.h"

#if defined(PLATFORM_WIIU)
	#include <coreinit/thread.h>
#endif


#if defined(PLATFORM_WIIU)
namespace detail
{
	class CodeExecFrameUpdateThread final : public rmx::ThreadBase
	{
	public:
		CodeExecFrameUpdateThread() :
			ThreadBase("CodeExec Frame Worker")
		{
			setWiiUThreadAffinity(OS_THREAD_ATTRIB_AFFINITY_CPU2);
			mConditionVariable = SDL_CreateCond();
			mConditionLock = SDL_CreateMutex();
			startThread();
		}

		~CodeExecFrameUpdateThread()
		{
			signalStopThread(false);
			SDL_LockMutex(mConditionLock);
			SDL_CondBroadcast(mConditionVariable);
			SDL_UnlockMutex(mConditionLock);
			joinThread();
			SDL_DestroyCond(mConditionVariable);
			SDL_DestroyMutex(mConditionLock);
		}

		bool performFrameUpdate(CodeExec& codeExec)
		{
			SDL_LockMutex(mConditionLock);
			RMX_CHECK(nullptr == mActiveCodeExec, "CodeExec frame worker already has an active task", SDL_UnlockMutex(mConditionLock); return codeExec.performFrameUpdate());
			mActiveCodeExec = &codeExec;
			mComplete = false;
			mResult = false;
			SDL_CondBroadcast(mConditionVariable);
			while (!mComplete)
			{
				SDL_CondWait(mConditionVariable, mConditionLock);
			}
			const bool result = mResult;
			SDL_UnlockMutex(mConditionLock);
			return result;
		}

	protected:
		void threadFunc() override
		{
			while (mShouldBeRunning)
			{
				SDL_LockMutex(mConditionLock);
				while (nullptr == mActiveCodeExec && mShouldBeRunning)
				{
					SDL_CondWait(mConditionVariable, mConditionLock);
				}
				CodeExec* codeExec = mActiveCodeExec;
				SDL_UnlockMutex(mConditionLock);

				if (nullptr != codeExec)
				{
					const bool result = codeExec->performFrameUpdate();
					SDL_LockMutex(mConditionLock);
					if (mActiveCodeExec == codeExec)
					{
						mResult = result;
						mComplete = true;
						mActiveCodeExec = nullptr;
					}
					SDL_CondBroadcast(mConditionVariable);
					SDL_UnlockMutex(mConditionLock);
				}
			}
		}

	private:
		SDL_cond* mConditionVariable = nullptr;
		SDL_mutex* mConditionLock = nullptr;
		CodeExec* mActiveCodeExec = nullptr;
		bool mComplete = false;
		bool mResult = false;
	};
}
#endif


namespace
{
#if defined(PLATFORM_WIIU)
	static constexpr bool ENABLE_WIIU_SIM_FRAME_TRACE = false;
	static constexpr bool ENABLE_WIIU_SIM_TIMING_LOGS = false;
	static constexpr bool ENABLE_WIIU_CODE_EXEC_WORKER = false;

	double getWiiUSimElapsedMilliseconds(uint64 start, uint64 end)
	{
		static const double frequency = (double)SDL_GetPerformanceFrequency();
		return ((double)(end - start) * 1000.0) / frequency;
	}

	bool performFrameUpdateOnWiiU(CodeExec& codeExec, detail::CodeExecFrameUpdateThread* worker)
	{
		if constexpr (!ENABLE_WIIU_CODE_EXEC_WORKER)
		{
			static bool sLoggedMainThreadCodeExec = false;
			if (!sLoggedMainThreadCodeExec)
			{
				sLoggedMainThreadCodeExec = true;
				RMX_LOG_INFO("Simulation: Wii U CodeExec worker disabled; running frame updates on main thread");
			}
			return codeExec.performFrameUpdate();
		}

		if (nullptr == worker)
			return codeExec.performFrameUpdate();

		static bool sLoggedCpu2CodeExec = false;
		if (!sLoggedCpu2CodeExec)
		{
			sLoggedCpu2CodeExec = true;
			RMX_LOG_INFO("Simulation: LemonScript frame updates scheduled on CPU2");
		}

		return worker->performFrameUpdate(codeExec);
	}
#endif

	void recordKeyFrame(uint32 frameNumber, Simulation& simulation, GameRecorder& gameRecorder, const GameRecorder::InputData& inputData)
	{
		static std::vector<uint8> data;
		data.reserve(0x128000);
		data.clear();

		SaveStateSerializer serializer(simulation, RenderParts::instance());
		serializer.saveState(data);

		gameRecorder.addKeyFrame(frameNumber, inputData, data);
	}
}


Simulation::Simulation() :
	mCodeExec(*new CodeExec()),
	mSimulationState(*new SimulationState()),
	mGameRecorder(*new GameRecorder()),
	mInputRecorder(*new InputRecorder())
{
	if (EngineMain::getDelegate().useDeveloperFeatures())
	{
		mROMDataAnalyser = new ROMDataAnalyser();
	}
}

Simulation::~Simulation()
{
#if defined(PLATFORM_WIIU)
	mWiiUCodeExecThread.reset();
#endif
	delete &mCodeExec;
	delete &mSimulationState;
	delete &mGameRecorder;
	delete &mInputRecorder;
	delete mROMDataAnalyser;
}

bool Simulation::startup()
{
	Configuration& config = Configuration::instance();

#if defined(PLATFORM_WIIU)
	if constexpr (ENABLE_WIIU_CODE_EXEC_WORKER)
	{
		if (!mWiiUCodeExecThread)
		{
			mWiiUCodeExecThread.reset(new detail::CodeExecFrameUpdateThread());
		}
	}
#endif

	RMX_LOG_INFO("Setup of EmulatorInterface");
	mCodeExec.startup();

	// Load scripts
	RMX_LOG_INFO("Loading scripts");
	bool success = mCodeExec.reloadScripts(true, false);	// Note: First parameter could just as well be set to false
	if (success)
	{
		mCodeExec.reinitRuntime(nullptr, CodeExec::CallStackInitPolicy::RESET);
	}

	// Optionally load save state
	mStateLoaded.clear();
	if (success && EngineMain::getDelegate().useDeveloperFeatures() && !config.mLoadSaveState.empty() && config.mStartPhase == 3)
	{
		success = loadState(config.mSaveStatesDirLocal + config.mLoadSaveState + L".state", false);
		if (!success)
			loadState(config.mSaveStatesDir + config.mLoadSaveState + L".state", false);
	}
	RMX_LOG_INFO("Runtime environment ready");

	if (EngineMain::getDelegate().useDeveloperFeatures())
	{
		// Startup input recorder
		mInputRecorder.initFromConfig();
	}

	if (mGameRecorder.isPlaying())
	{
		// Try the long and short name
		if (mGameRecorder.loadRecording(L"gamerecording.bin"))
		{
			RMX_LOG_INFO("Playback of 'gamerecording.bin'");
		}
		else if (mGameRecorder.loadRecording(L"gamerec.bin"))
		{
			RMX_LOG_INFO("Playback of 'gamerec.bin'");
		}

		if (mGameRecorder.hasFrameNumber(mFrameNumber))
		{
			mGameRecorder.setIgnoreKeys(config.mGameRecorder.mPlaybackIgnoreKeys);
			config.setSettingsReadOnly(true);	// Do not overwrite settings
			jumpToFrame(config.mGameRecorder.mPlaybackStartFrame, false);
		}
	}

	return true;
}

void Simulation::shutdown()
{
	RMX_LOG_INFO("Simulation shutdown");
	mIsRunning = false;
#if defined(PLATFORM_WIIU)
	if (mWiiUCodeExecThread)
	{
		RMX_LOG_INFO("Simulation shutdown: stopping Wii U code exec worker");
		mWiiUCodeExecThread.reset();
		RMX_LOG_INFO("Simulation shutdown: Wii U code exec worker stopped");
	}
#endif
	mInputRecorder.shutdown();
	RMX_LOG_INFO("Simulation input recorder shutdown complete");

	RMX_LOG_INFO("Simulation shutdown complete");
}

void Simulation::reloadScriptsAfterModsChange()
{
	// Immediate reload of the scripts (while the loading text box is shown)
	if (mCodeExec.reloadScripts(false, false))
	{
		mCodeExec.reinitRuntime(nullptr, CodeExec::CallStackInitPolicy::RESET);
	}
}

EmulatorInterface& Simulation::getEmulatorInterface()
{
	return mCodeExec.getEmulatorInterface();
}

void Simulation::resetState()
{
	EngineMain::instance().getAudioOut().reset();
	resetIntoGame(nullptr);
}

void Simulation::resetIntoGame(const std::vector<std::pair<std::string, std::string>>* enforcedCallStack)
{
	// Reset randomization
	randomize();

	// Reset simulation
	mSimulationState.reset();

	// Reset video & audio
	VideoOut::instance().reset();
	EngineMain::instance().getAudioOut().resetGame();

	// Reset code execution
	mCodeExec.reset();
	mStateLoaded.clear();

	// Reload and initialize scripts as needed
	if (mCodeExec.reloadScripts(false, false))
	{
		mCodeExec.reinitRuntime(enforcedCallStack, CodeExec::CallStackInitPolicy::RESET);
	}

	// Apply mod settings
	applyModSettingsToGlobals();

	mFrameNumber = 0;
	mCurrentTargetFrame = 0.0;
	mLastCorrectionFrame = 0;
	mStepsLimit = -1;
	mBreakConditions.clearAll();
	mGameRecorder.clear();
}

void Simulation::resetIntoGame(const std::string& entryFunctionName)
{
	const std::vector<std::pair<std::string, std::string>> enforcedCallStack = { { entryFunctionName, "" } };
	resetIntoGame(&enforcedCallStack);
}

void Simulation::reloadLastState()
{
	if (!mStateLoaded.empty())
	{
		loadState(mStateLoaded);
	}
}

bool Simulation::loadState(const std::wstring& filename, bool showError)
{
	VideoOut::instance().reset();
	EngineMain::instance().getAudioOut().reset();

	SaveStateSerializer::StateType stateType;
	SaveStateSerializer serializer(*this, RenderParts::instance());

	const bool success = serializer.loadState(filename, &stateType);
	if (!success)
	{
		if (showError)
			RMX_ERROR("Failed to load save state '" << WString(filename).toStdString() << "'", );
		return false;
	}

	mStateLoaded = filename;
	mCodeExec.reinitRuntime(nullptr, (stateType == SaveStateSerializer::StateType::GENSX) ? CodeExec::CallStackInitPolicy::READ_FROM_ASM : CodeExec::CallStackInitPolicy::USE_EXISTING);

	if (Configuration::instance().mDevMode.mApplyModSettingsAfterLoadState)
	{
		applyModSettingsToGlobals();
	}

	mFrameNumber = 0;
	mCurrentTargetFrame = 0.0;
	mLastCorrectionFrame = 0;
	mStepsLimit = -1;
	mBreakConditions.clearAll();

	mGameRecorder.clear();
	return true;
}

void Simulation::saveState(const std::wstring& filename)
{
	SaveStateSerializer serializer(*this, RenderParts::instance());
	const bool success = serializer.saveState(filename);
	RMX_CHECK(success, "Failed to save save state '" << WString(filename).toStdString() << "'", return);

	// Also save a screenshot
	Bitmap bmp;
	VideoOut::instance().getScreenshot(bmp);
	bmp.save(filename + L".bmp");

	// Set as default for "reloadLastState"
	mStateLoaded = filename;
}

bool Simulation::triggerFullScriptsReload()
{
	if (mCodeExec.reloadScripts(true, true))
	{
		mCodeExec.restoreRuntimeState(!mStateLoaded.empty());
		return true;
	}
	else
	{
		return false;
	}
}

void Simulation::update(float timeElapsed)
{
	if (!isRunning() || !mCodeExec.isCodeExecutionPossible())
		return;

	if (mRewindSteps >= 0)
	{
		setSpeed(0.0f);
		while (mRewindSteps >= 1)
		{
			if (jumpToFrame(mFrameNumber - mRewindSteps))
				mRewindSteps = 0;
			else
				--mRewindSteps;		// Try again with one step less
		}
		mRewindSteps = -1;
	}

	// Limit length of one frame to 100ms
	timeElapsed = clamp(timeElapsed, 0.0f, 0.1f);

	// Do nothing as long as not enough time has passed
	const double oldTargetFrame = mCurrentTargetFrame;
	if (mStepsLimit < 0)
	{
		if (mSimulationSpeed > 0.0f)
		{
			const float step = timeElapsed * mSimulationSpeed;
			mCurrentTargetFrame += (double)step * (double)getSimulationFrequency();
		}
		else
		{
			mCurrentTargetFrame = roundToDouble(mCurrentTargetFrame);
		}
	}
	else if (mStepsLimit > 0)
	{
		mCurrentTargetFrame = roundToDouble(mCurrentTargetFrame + 1.0);
	}

	const bool useFrameInterpolation = Configuration::useFrameInterpolation(Configuration::instance().mFrameSync);
	const uint32 requiredFrameNumber = useFrameInterpolation ? (uint32)std::ceil(mCurrentTargetFrame) : (uint32)roundToInt(mCurrentTargetFrame);

	if (mFrameNumber < requiredFrameNumber)
	{
		const uint32 startTime = SDL_GetTicks();
		const uint32 limitTime = startTime + 200;

		while (mFrameNumber < requiredFrameNumber)
		{
			// Update emulation
			const bool result = generateFrame();
			if (!result)
				break;

			// Time limit to prevent non-responding application
			if (SDL_GetTicks() >= limitTime)
			{
				// Reset target frame to an earlier frame, but still make sure we had any progress at all
				mCurrentTargetFrame = std::max((double)mFrameNumber, oldTargetFrame);
				break;
			}
		}

		// Each second, a small correction to the accumulated time gets applied
		if ((int)(mFrameNumber - mLastCorrectionFrame) >= (int)getSimulationFrequency() || mFrameNumber < mLastCorrectionFrame)
		{
			// The idea here is to bring the accumulated time towards the midpoint, where it's most stable against unintentional double frames or frame skips (which might happen otherwise)
			//  -> This is most useful for 60 Hz displays with V-sync on, but should have a similar effect on e.g. 75 Hz, 90 Hz, 120 Hz
			//  -> It should not introduce any noticeable issues or game speed changes in other cases
			const double stableOffset = useFrameInterpolation ? 0.25 : 0.0;
			const double diff = mCurrentTargetFrame - (roundToDouble(mCurrentTargetFrame - stableOffset) + stableOffset);
			const constexpr double maxChange = 0.1;
			mCurrentTargetFrame += clamp(-diff, -maxChange, maxChange);
			mLastCorrectionFrame = mFrameNumber;
		}
	}

	if (mFrameNumber == requiredFrameNumber)
	{
		const float position = saturate((float)(mCurrentTargetFrame - (double)(mFrameNumber - 1)));
		VideoOut::instance().setInterFramePosition(position);
	}
	else
	{
		VideoOut::instance().setInterFramePosition(0.0f);
	}

#if 0
	// Meant for debugging of accumulated time stability
	if ((mFrameNumber % 6) == 0)
		LogDisplay::instance().setModeDisplay(String(0, "diff = %+0.3f", (float)(mCurrentTargetFrame - roundToDouble(mCurrentTargetFrame))));
#endif
}

bool Simulation::generateFrame()
{
	ControlsIn& controlsIn = ControlsIn::instance();

#if defined(PLATFORM_WIIU)
	const uint64 simT0 = ENABLE_WIIU_SIM_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
#endif
	const bool beginningNewFrame = mCodeExec.willBeginNewFrame();
	const float tickLength = 1.0f / getSimulationFrequency();

	bool completedCurrentFrame = false;

#if defined(PLATFORM_WIIU)
	static int sWiiUFrameLogCount = 0;
	const bool logWiiUFrame = ENABLE_WIIU_SIM_FRAME_TRACE && (sWiiUFrameLogCount < 180);
	if (logWiiUFrame)
	{
		RMX_LOG_INFO("[WiiU Sim] generateFrame begin frame=" << mFrameNumber << " beginNew=" << beginningNewFrame << " codeState=" << (int)mCodeExec.getExecutionState());
		++sWiiUFrameLogCount;
	}
#endif

	// Steps to do when beginning a new frame
	if (beginningNewFrame)
	{
		// Check if we can even begin a new frame
		if (!NetplayManager::instance().canBeginNextFrame(mFrameNumber))
			return false;

		// Tell game instance
		EngineMain::getDelegate().onPreFrameUpdate();

		// Tell video that we begin a new frame
		VideoOut::instance().preFrameUpdate();

		// Game recorder: Save initial frame
		if (mGameRecorder.isRecording() && mGameRecorder.getRangeEnd() == 0)
		{
			recordKeyFrame(0, *this, mGameRecorder, GameRecorder::InputData());
		}

		controlsIn.beginInputUpdate();

		// Update netplay
		NetplayManager::instance().onFrameUpdate(controlsIn, mFrameNumber);

		// If game recorder has input data for the frame transition, then use that
		//  -> This is particularly relevant for rewinds, namely for the small fast forwards from the previous keyframe
		GameRecorder::PlaybackResult result;
		if (mGameRecorder.getFrameData(mFrameNumber + 1, result))
		{
			if (mGameRecorder.isPlaying())
				LogDisplay::instance().setModeDisplay("Game recorder playback at frame: " + std::to_string(mFrameNumber + 1));

			if (nullptr != result.mData && !Configuration::instance().mGameRecorder.mPlaybackIgnoreKeys)
			{
				// Load save state
				SaveStateSerializer::StateType stateType;
				SaveStateSerializer serializer(*this, RenderParts::instance());

				const bool success = serializer.loadState(*result.mData, &stateType);
				if (success)
				{
					mCodeExec.reinitRuntime(nullptr, (stateType == SaveStateSerializer::StateType::GENSX) ? CodeExec::CallStackInitPolicy::READ_FROM_ASM : CodeExec::CallStackInitPolicy::USE_EXISTING);
					completedCurrentFrame = true;
				}
				else
				{
					RMX_ERROR("Failed to load save state", );
				}
			}

			controlsIn.injectInputs(result.mInput->mInputs);
		}

		// Input recorder playback
		if (EngineMain::getDelegate().useDeveloperFeatures())
		{
			if (mInputRecorder.isPlaying())
			{
				const InputRecorder::InputState& inputState = mInputRecorder.updatePlayback(mFrameNumber);
				controlsIn.injectInputs(inputState.mInputFlags);
			}
		}

		// Update input state
		{
			controlsIn.endInputUpdate();

			EngineMain::getDelegate().onControlsUpdate();

			// Input state can be queried by scripts via "Input.getController" and "Input.getControllerPrevious"
		}
	}

#if defined(PLATFORM_WIIU)
	const uint64 simT1 = ENABLE_WIIU_SIM_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
	uint64 simT2 = simT1;
	uint64 simT3 = simT1;
#endif
	if (!completedCurrentFrame)		// Can be true already if game recorder playback just loaded a state
	{
		// Perform game simulation
#if defined(PLATFORM_WIIU)
		if (logWiiUFrame)
		{
			RMX_LOG_INFO("[WiiU Sim] generateFrame performFrameUpdate begin frame=" << mFrameNumber);
		}
#endif
#if defined(PLATFORM_WIIU)
		simT2 = ENABLE_WIIU_SIM_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
		completedCurrentFrame = performFrameUpdateOnWiiU(mCodeExec, mWiiUCodeExecThread.get());
		simT3 = ENABLE_WIIU_SIM_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
#else
		completedCurrentFrame = mCodeExec.performFrameUpdate();
#endif
#if defined(PLATFORM_WIIU)
		if (logWiiUFrame)
		{
			RMX_LOG_INFO("[WiiU Sim] generateFrame performFrameUpdate end frame=" << mFrameNumber << " completed=" << completedCurrentFrame << " codeState=" << (int)mCodeExec.getExecutionState());
		}
#endif
	}

#if defined(PLATFORM_WIIU)
	const uint64 simT4 = ENABLE_WIIU_SIM_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
#endif

	// Steps to do when a frame got completed
	if (completedCurrentFrame)
	{
		// Tell game instance
		EngineMain::getDelegate().onPostFrameUpdate();

		// Tell video that we begin a new frame
		VideoOut::instance().postFrameUpdate();

		if (EngineMain::getDelegate().useDeveloperFeatures())
		{
			// Update input recording
			if (mInputRecorder.isRecording())
			{
				InputRecorder::InputState inputState;
				controlsIn.writeCurrentState(inputState.mInputFlags);

				mInputRecorder.updateRecording(inputState);
			}
		}

		// Update game recording
		if (mGameRecorder.isRecording())
		{
			if (!mGameRecorder.hasFrameNumber(mFrameNumber + 1))
			{
				GameRecorder::InputData inputData;
				controlsIn.writeCurrentState(inputData.mInputs);

				// Keyframe every 3 seconds - except when dev mode is active, because rewinding requires more frequent keyframes
				const int keyframeFrequency = EngineMain::getDelegate().useDeveloperFeatures() ? 10 : 180;
				const int framesToKeep = EngineMain::getDelegate().useDeveloperFeatures() ? 3600 : 1800;
				if (((mFrameNumber + 1) % keyframeFrequency) == 0)
				{
					recordKeyFrame(mFrameNumber + 1, *this, mGameRecorder, inputData);
					mGameRecorder.discardOldFrames(framesToKeep);
				}
				else
				{
					mGameRecorder.addFrame(mFrameNumber + 1, inputData);
				}
			}
		}
		else if (mGameRecorder.isPlaying() && EngineMain::getDelegate().useDeveloperFeatures())
		{
			// Generate a keyframe every 10 frames, to allow for quick rewinds during game recording playback as well
			const int keyframeFrequency = 10;
			if (((mFrameNumber + 1) % keyframeFrequency) == 0 && !mGameRecorder.isKeyframe(mFrameNumber + 1) && mGameRecorder.canAddFrame(mFrameNumber + 1))
			{
				GameRecorder::InputData inputData;
				controlsIn.writeCurrentState(inputData.mInputs);

				recordKeyFrame(mFrameNumber + 1, *this, mGameRecorder, inputData);
			}
		}

		++mFrameNumber;

		if (mStepsLimit > 0)
			--mStepsLimit;
	}

#if defined(PLATFORM_WIIU)
	const uint64 simT5 = ENABLE_WIIU_SIM_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
	if constexpr (ENABLE_WIIU_SIM_TIMING_LOGS)
	{
		static uint32 sTimingSamples = 0;
		static double sPreFrameMilliseconds = 0.0;
		static double sCodeExecMilliseconds = 0.0;
		static double sPostFrameMilliseconds = 0.0;
		static double sTotalMilliseconds = 0.0;
		static double sMaxCodeExecMilliseconds = 0.0;
		static double sMaxTotalMilliseconds = 0.0;

		if (completedCurrentFrame)
		{
			const double preFrameMilliseconds = getWiiUSimElapsedMilliseconds(simT0, simT1);
			const double codeExecMilliseconds = getWiiUSimElapsedMilliseconds(simT2, simT3);
			const double postFrameMilliseconds = getWiiUSimElapsedMilliseconds(simT4, simT5);
			const double totalMilliseconds = getWiiUSimElapsedMilliseconds(simT0, simT5);
			sPreFrameMilliseconds += preFrameMilliseconds;
			sCodeExecMilliseconds += codeExecMilliseconds;
			sPostFrameMilliseconds += postFrameMilliseconds;
			sTotalMilliseconds += totalMilliseconds;
			sMaxCodeExecMilliseconds = std::max(sMaxCodeExecMilliseconds, codeExecMilliseconds);
			sMaxTotalMilliseconds = std::max(sMaxTotalMilliseconds, totalMilliseconds);
			++sTimingSamples;

			if (sTimingSamples >= 180)
			{
				const double inv = 1.0 / (double)sTimingSamples;
				RMX_LOG_INFO("Simulation timing avg pre=" << (sPreFrameMilliseconds * inv) << "ms code=" << (sCodeExecMilliseconds * inv) << "ms post=" << (sPostFrameMilliseconds * inv) << "ms total=" << (sTotalMilliseconds * inv) << "ms maxCode=" << sMaxCodeExecMilliseconds << "ms maxTotal=" << sMaxTotalMilliseconds << "ms");
				sTimingSamples = 0;
				sPreFrameMilliseconds = 0.0;
				sCodeExecMilliseconds = 0.0;
				sPostFrameMilliseconds = 0.0;
				sTotalMilliseconds = 0.0;
				sMaxCodeExecMilliseconds = 0.0;
				sMaxTotalMilliseconds = 0.0;
			}
		}
	}
#endif

	// Return false if frame got interrupted
	//  -> In this case, the outer loop should break
	//  -> Same if code execution is not possible any more
#if defined(PLATFORM_WIIU)
	if (logWiiUFrame)
	{
		RMX_LOG_INFO("[WiiU Sim] generateFrame end frame=" << mFrameNumber << " completed=" << completedCurrentFrame << " canContinue=" << mCodeExec.isCodeExecutionPossible());
	}
#endif
	return (completedCurrentFrame && mCodeExec.isCodeExecutionPossible());
}

bool Simulation::jumpToFrame(uint32 frameNumber, bool clearRecordingAfterwards)
{
	if (mGameRecorder.isRecording() || mGameRecorder.isPlaying())
	{
		if (mGameRecorder.isPlaying())
			clearRecordingAfterwards = false;

		// Go back until the most recent keyframe, in case the selected frame is not a keyframe itself
		GameRecorder::PlaybackResult result;
		uint32 keyframeNumber = frameNumber;
		if (!mGameRecorder.getFrameData(keyframeNumber, result))
			return false;

		while (nullptr == result.mData)
		{
			if (keyframeNumber == 0)
				return false;
			--keyframeNumber;
			if (!mGameRecorder.getFrameData(keyframeNumber, result))
				return false;
		}

		SaveStateSerializer::StateType stateType;
		SaveStateSerializer serializer(*this, RenderParts::instance());

		const bool success = serializer.loadState(*result.mData, &stateType);
		if (success)
		{
			// Inject inputs for this frame, so that previous input will be set correctly in the next frame
			ControlsIn::instance().injectInputs(result.mInput->mInputs);

			mCodeExec.reinitRuntime(nullptr, (stateType == SaveStateSerializer::StateType::GENSX) ? CodeExec::CallStackInitPolicy::READ_FROM_ASM : CodeExec::CallStackInitPolicy::USE_EXISTING);
			mFrameNumber = keyframeNumber;
			mCurrentTargetFrame = (float)frameNumber;

			if (clearRecordingAfterwards)
			{
				// Discard later frames to disable the logic that uses their recorded inputs instead of player input
				mGameRecorder.discardFramesAfter(frameNumber);
			}
			return true;
		}
	}

	return false;
}

int Simulation::setRewind(int rewindSteps)
{
	mRewindSteps = rewindSteps;
	if (mGameRecorder.isRecording())
		mRewindSteps = std::min<int>(mFrameNumber - mGameRecorder.getRangeStart(), mRewindSteps);
	return mRewindSteps;
}

float Simulation::getSimulationFrequency() const
{
	return (mSimulationFrequencyOverride > 0.0f) ? mSimulationFrequencyOverride : (float)Configuration::instance().mSimulationFrequency;
}

void Simulation::setSpeed(float emulatorSpeed)
{
	mSimulationSpeed = emulatorSpeed;
	mStepsLimit = -1;
	mBreakConditions.clearAll();
}

void Simulation::setNextSingleStep()
{
	mStepsLimit = 1;
}

void Simulation::removeStepsLimit()
{
	mStepsLimit = -1;
}

void Simulation::setBreakCondition(BreakCondition breakCondition, bool enable)
{
	mBreakConditions.set(breakCondition, enable);
}

void Simulation::sendBreakSignal(BreakCondition breakCondition)
{
	if (mBreakConditions.isSet(breakCondition))
	{
		mStepsLimit = 0;
	}
}

void Simulation::refreshDebugging()
{
	VideoOut::instance().preRefreshDebugging();
	mCodeExec.executeScriptFunction("OxygenCallback.setupCustomSidePanelEntries", false);
	VideoOut::instance().postRefreshDebugging();
}

uint32 Simulation::saveGameRecording(WString* outFilename)
{
	std::wstring filename = L"gamerecording.bin";
	const std::string timeString = PlatformFunctions::getCompactSystemTimeString();
	if (!timeString.empty())
	{
		filename = L"gamerecording_" + String(timeString).toStdWString() + L".bin";
	}
	filename = Configuration::instance().mGameAppDataPath + L"gamerecordings/" + filename;

	if (!mGameRecorder.saveRecording(filename, 180))
		return 0;

	if (nullptr != outFilename)
		*outFilename = filename;

	return mGameRecorder.getCurrentNumberOfFrames();
}

void Simulation::applyModSettingsToGlobals()
{
	// Apply mod settings
	for (Mod* mod : ModManager::instance().getActiveMods())
	{
		for (Mod::SettingCategory& modSettingCategory : mod->mSettingCategories)
		{
			for (Mod::Setting& modSetting : modSettingCategory.mSettings)
			{
				mCodeExec.getLemonScriptRuntime().setGlobalVariableValue<int64>(modSetting.mBinding, modSetting.mCurrentValue);
			}
		}
	}
}
