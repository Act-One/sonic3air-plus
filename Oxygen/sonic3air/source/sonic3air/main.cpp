/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"
#include "sonic3air/EngineDelegate.h"
#include "sonic3air/GameArgumentsReader.h"
#include "sonic3air/helper/PackageBuilder.h"
#include "sonic3air/platform/PlatformSpecifics.h"

#include "oxygen/platform/PlatformFunctions.h"

#if defined(PLATFORM_WII)
#include <gccore.h>
#include <ogc/system.h>
#include <cstdio>

namespace
{
	void initWiiBootConsole()
	{
		static bool sInitialized = false;
		if (sInitialized)
			return;

		VIDEO_Init();
		GXRModeObj* renderMode = VIDEO_GetPreferredMode(nullptr);
		if (nullptr == renderMode)
			return;

		void* framebuffer = MEM_K0_TO_K1(SYS_AllocateFramebuffer(renderMode));
		if (nullptr == framebuffer)
			return;

		CON_Init(framebuffer, 20, 20, renderMode->fbWidth - 40, renderMode->xfbHeight - 40, renderMode->fbWidth * VI_DISPLAY_PIX_SZ);
		VIDEO_Configure(renderMode);
		VIDEO_SetNextFramebuffer(framebuffer);
		VIDEO_SetBlack(false);
		VIDEO_Flush();
		VIDEO_WaitVSync();
		if (renderMode->viTVMode & VI_NON_INTERLACE)
			VIDEO_WaitVSync();
		sInitialized = true;
	}

	__attribute__((constructor(101))) void earlyWiiBootConsoleConstructor()
	{
		initWiiBootConsole();
		SYS_Report("[S3AIR Wii] early constructor\n");
		std::printf("[S3AIR Wii] early constructor\n");
		std::fflush(stdout);
	}
}

#define S3AIR_WII_BOOT_REPORT(message) do { SYS_Report("[S3AIR Wii] %s\n", message); std::printf("[S3AIR Wii] %s\n", message); std::fflush(stdout); } while (false)
#else
#define S3AIR_WII_BOOT_REPORT(message) do {} while (false)
#endif


// [Added for Switch platform] HJW: I know it's sloppy to put this here... it'll get moved afterwards
// Building with my env (msys2,gcc) requires this stub for some reason
#ifndef pathconf
long pathconf(const char* path, int name)
{
	errno = ENOSYS;
	return -1;
}
#endif

#if defined(PLATFORM_WINDOWS) && !defined(PLATFORM_UWP) && !defined(__GNUC__)
extern "C"
{
	// Tell graphics drivers to prefer the dedicated GPU, if there's integrated graphics as well
	_declspec(dllexport) uint32 NvOptimusEnablement = 1;
	_declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#if defined(PLATFORM_VITA)
extern "C"
{
	// Any value higher than 324 MB will make the game either boot without sound or just crash the PSVITA due to lack of physical RAM
	int _newlib_heap_size_user = 324 * 1024 * 1024;
	unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;
}
#endif


int main(int argc, char** argv)
{
#if defined(PLATFORM_WII)
	initWiiBootConsole();
#endif
	S3AIR_WII_BOOT_REPORT("main enter");
#if defined(PLATFORM_WII)
	S3AIR_WII_BOOT_REPORT("platform fs startup begin");
	PlatformFunctions::onEngineStartup();
	S3AIR_WII_BOOT_REPORT("platform fs startup complete");
#endif
	EngineMain::earlySetup();
	S3AIR_WII_BOOT_REPORT("early setup complete");
	PlatformSpecifics::platformStartup();
	S3AIR_WII_BOOT_REPORT("platform startup complete");

	GameArgumentsReader arguments;

#if defined(PLATFORM_VITA)
	argc = 0;
#else
	// Read command line arguments
	arguments.read(argc, argv);
	S3AIR_WII_BOOT_REPORT("arguments read");

	// For certain arguments, just try to forward them to an already running instance of S3AIR
	if (!arguments.mUrl.empty())
	{
		if (CommandForwarder::trySendCommand("ForwardedCommand:Url:" + arguments.mUrl))
			return 0;
	}

	// Make sure we're in the correct working directory
	PlatformFunctions::changeWorkingDirectory(arguments.mExecutableCallPath);
	S3AIR_WII_BOOT_REPORT("working directory selected");
#endif

#if defined(PLATFORM_WINDOWS) && !defined(PLATFORM_UWP)
	// Check if the user has an old version of "audioremaster.bin", and remove it if that's the case
	//  -> As the newer installations don't include that file, it is most likely an out-dated one, and could cause problems (at least an assert) down the line
	if (FTX::FileSystem->exists(L"data/audioremaster.bin"))
		FTX::FileSystem->removeFile(L"data/audioremaster.bin");
#endif

#if !defined(PLATFORM_ANDROID) && !defined(PLATFORM_VITA)
	if (arguments.mPack)
	{
		PackageBuilder::performPacking();
		if (!arguments.mNativize && !arguments.mDumpCppDefinitions)		// In case multiple arguments got combined, the others would get ignored without this check
			return 0;
	}
#endif

	// Randomization is quite important for server communication
	randomize();

	try
	{
		S3AIR_WII_BOOT_REPORT("construct engine");
		// Create engine delegate and engine main instance
#if defined(PLATFORM_WIIU)
		EngineDelegate* myDelegate = new EngineDelegate();
		EngineMain* myMain = new EngineMain(*myDelegate, arguments);
#else
		EngineDelegate myDelegate;
		EngineMain myMain(myDelegate, arguments);
#endif

		// Evaluate some more arguments
		Configuration& config = Configuration::instance();
		if (arguments.mNativize)
		{
			config.mRunScriptNativization = 1;
			config.mScriptNativizationOutput = L"source/sonic3air/_nativized/NativizedCode.inc";
			config.mExitAfterScriptLoading = true;
		}
		if (arguments.mDumpCppDefinitions)
		{
			config.mDumpCppDefinitionsOutput = L"scripts/_reference/cpp_core_functions.lemon";
			config.mExitAfterScriptLoading = true;
		}

		// Now run the game
		S3AIR_WII_BOOT_REPORT("execute engine");
#if defined(PLATFORM_WIIU)
		myMain->execute();
		RMX_LOG_INFO("main: EngineMain execute returned");
		if (nullptr != FTX::System && FTX::System->isWiiUProcUIExitRequested())
		{
			RMX_LOG_INFO("main: returning 0");
			_Exit(0);
		}
		delete myMain;
		delete myDelegate;
#else
		myMain.execute();
#endif
		S3AIR_WII_BOOT_REPORT("engine returned");
	}
	catch (const std::exception& e)
	{
		RMX_ERROR("Caught unhandled exception in main loop: " << e.what(), );
	}

#if defined(PLATFORM_WIIU)
	RMX_LOG_INFO("main: returning 0");
#endif
	return 0;
}
