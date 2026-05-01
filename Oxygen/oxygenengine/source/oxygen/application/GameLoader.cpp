/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/application/GameLoader.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/application/GameProfile.h"
#include "oxygen/application/audio/AudioOutBase.h"
#include "oxygen/application/input/InputManager.h"
#include "oxygen/application/modding/ModManager.h"
#include "oxygen/application/video/VideoOut.h"
#include "oxygen/helper/Logging.h"
#include "oxygen/platform/PlatformFunctions.h"
#include "oxygen/rendering/RenderResources.h"
#include "oxygen/resources/FontCollection.h"
#include "oxygen/resources/ResourcesCache.h"
#include "oxygen/simulation/PersistentData.h"
#include <filesystem>
#if defined(PLATFORM_ANDROID)
	#include "oxygen/platform/android/AndroidJavaInterface.h"
#endif


namespace
{
#if defined(PLATFORM_UWP)
	const wchar_t* UWP_DEPLOYMENT_STAGING_DIR = L"D:/DevelopmentFiles/Scarlet3AIR/";

	bool tryImportRomFromUwpDeploymentStaging()
	{
		Configuration& config = Configuration::instance();
		const GameProfile& gameProfile = GameProfile::instance();
		if (gameProfile.mRomInfos.empty() || config.mGameAppDataPath.empty())
			return false;

		bool importedAnyRom = false;

		try
		{
			std::filesystem::create_directories(std::filesystem::path(config.mGameAppDataPath));
		}
		catch (const std::exception& e)
		{
			RMX_LOG_WARNING("Failed to prepare LocalFolder for ROM import: " << e.what());
			return false;
		}

		for (const GameProfile::RomInfo& romInfo : gameProfile.mRomInfos)
		{
			const std::filesystem::path localRomPath(config.mGameAppDataPath + romInfo.mSteamRomName);
			if (std::filesystem::exists(localRomPath))
				continue;

			const std::filesystem::path stagedRomPath(std::wstring(UWP_DEPLOYMENT_STAGING_DIR) + romInfo.mSteamRomName);
			if (!std::filesystem::exists(stagedRomPath))
				continue;

			try
			{
				std::filesystem::copy_file(stagedRomPath, localRomPath, std::filesystem::copy_options::overwrite_existing);
				RMX_LOG_INFO("Imported staged ROM into LocalFolder: " << stagedRomPath.u8string());
				importedAnyRom = true;
			}
			catch (const std::exception& e)
			{
				RMX_LOG_WARNING("Failed to import staged ROM from DevelopmentFiles: " << e.what());
			}
		}

		return importedAnyRom;
	}
#endif
}


void GameLoader::setSetupScreenMessage(const std::string& title, const std::string& text)
{
	mSetupScreenTitle = title;
	mSetupScreenText = text;
}

std::string GameLoader::buildMissingRomText(const GameProfile& gameProfile) const
{
	if (gameProfile.mRomInfos.empty())
	{
		return "Copy a game ROM to:\nLocalFolder/";
	}

	const std::string romFilename = WString(gameProfile.mRomInfos[0].mSteamRomName).toStdString();
	return "Copy an original " + gameProfile.mRomInfos[0].mSteamGameName +
		   " ROM to:\nLocalFolder/" + romFilename +
#if defined(PLATFORM_UWP)
		   "\n\nRemote deploy can also stage it at:\nDevelopmentFiles/Scarlet3AIR/" + romFilename;
#else
		   "";
#endif
}

GameLoader::UpdateResult GameLoader::updateLoading()
{
	switch (mState)
	{
		case State::UNLOADED:
		{
			setSetupScreenMessage("Loading Game", "Checking for ROM and startup data...");
			RMX_LOG_INFO("Loading ROM...");
#if defined(PLATFORM_UWP)
			tryImportRomFromUwpDeploymentStaging();
#endif
			if (!ResourcesCache::instance().loadRom())
			{
			#if defined(PLATFORM_ANDROID)
				AndroidJavaInterface& javaInterface = AndroidJavaInterface::instance();
				if (javaInterface.hasRomFileAlready())
				{
					const bool success = ResourcesCache::instance().loadRomFromMemory(javaInterface.getRomFileInjection().mRomContent);
					if (success)
					{
						mState = State::ROM_LOADED;
						return UpdateResult::CONTINUE_IMMEDIATE;
					}
				}
			#endif

				RMX_LOG_INFO("ROM loading failed");

				const GameProfile& gameProfile = GameProfile::instance();
				if (!gameProfile.mRomInfos.empty())
				{
				#if defined(PLATFORM_WINDOWS) && !defined(PLATFORM_UWP)
					const std::string text = "This game requires an original " + gameProfile.mRomInfos[0].mSteamGameName + " ROM to work.\nIf you have one, click OK and select it in the following dialog.\n\nSee the Manual for details.";
					const PlatformFunctions::DialogResult result = PlatformFunctions::showDialogBox(rmx::ErrorSeverity::INFO, PlatformFunctions::DialogButtons::OK_CANCEL, gameProfile.mFullName, text);
					if (result == PlatformFunctions::DialogResult::CANCEL)
					{
						return UpdateResult::FAILURE;
					}

					const std::wstring title = L"Select " + String(gameProfile.mRomInfos[0].mSteamGameName).toStdWString() + L" ROM";
					const std::wstring romPath = PlatformFunctions::openFileSelectionDialog(title, gameProfile.mRomInfos[0].mSteamRomName, L"Genesis ROM files (*.bin)\0*.bin\0All Files\0*.*\0\0");
					const bool success = ResourcesCache::instance().loadRomFromFile(romPath);
					if (success)
					{
						mState = State::ROM_LOADED;
						return UpdateResult::CONTINUE_IMMEDIATE;
					}
					else
					{
						// How about another try?
						mState = State::UNLOADED;
						return UpdateResult::CONTINUE_IMMEDIATE;
					}

				#elif defined(PLATFORM_UWP)
					const std::string message = buildMissingRomText(gameProfile);
					setSetupScreenMessage("ROM Required", message);
					RMX_LOG_WARNING("ROM could not be loaded yet on UWP. Waiting for ROM in LocalFolder.");
					mNextRomRetryTicks = SDL_GetTicks() + 1000;
					mState = State::WAITING_FOR_ROM;
					return UpdateResult::CONTINUE;

				#elif defined(PLATFORM_ANDROID)
					javaInterface.openRomFileSelectionDialog(gameProfile.mRomInfos[0].mSteamGameName);
					mState = State::WAITING_FOR_ROM;
					return UpdateResult::CONTINUE;

				#else
					RMX_ERROR("ROM could not be loaded!\nAn original " + gameProfile.mRomInfos[0].mSteamGameName + " ROM must be added manually. See the Manual for details.\n\nThe application will now close.", );
					return UpdateResult::FAILURE;

				#endif
				}
				else
				{
				#if defined(PLATFORM_UWP)
					setSetupScreenMessage("ROM Required", buildMissingRomText(gameProfile));
					RMX_LOG_WARNING("ROM could not be loaded yet on UWP. Waiting for ROM in LocalFolder.");
					mNextRomRetryTicks = SDL_GetTicks() + 1000;
					mState = State::WAITING_FOR_ROM;
					return UpdateResult::CONTINUE;
				#else
					RMX_ERROR("ROM could not be loaded!\nAn original game ROM must be added manually. See the Manual for details.\n\nThe application will now close.", );
				#endif
				}
			}
			RMX_LOG_INFO("ROM found at: " << WString(Configuration::instance().mLastRomPath).toStdString());
			setSetupScreenMessage("Loading Game", "Preparing mods, scripts, and resources...");

			mState = State::ROM_LOADED;
			return UpdateResult::CONTINUE_IMMEDIATE;
		}

		case State::WAITING_FOR_ROM:
		{
		#if defined(PLATFORM_ANDROID)
			AndroidJavaInterface& javaInterface = AndroidJavaInterface::instance();
			const AndroidJavaInterface::BinaryDialogResult result = javaInterface.getRomFileInjection().mDialogResult;
			switch (result)
			{
				case AndroidJavaInterface::BinaryDialogResult::SUCCESS:
				{
					const bool success = ResourcesCache::instance().loadRomFromMemory(javaInterface.getRomFileInjection().mRomContent);
					if (success)
					{
						mState = State::ROM_LOADED;
						return UpdateResult::CONTINUE_IMMEDIATE;
					}
					// Fallthrough by design
				}

				case AndroidJavaInterface::BinaryDialogResult::FAILED:
				{
					// How about another try?
					mState = State::UNLOADED;
					return UpdateResult::CONTINUE_IMMEDIATE;
				}

				default:
					break;
			}
		#elif defined(PLATFORM_UWP)
			const uint32 currentTicks = SDL_GetTicks();
			if (SDL_TICKS_PASSED(currentTicks, mNextRomRetryTicks))
			{
				mNextRomRetryTicks = currentTicks + 1000;
				tryImportRomFromUwpDeploymentStaging();
				if (ResourcesCache::instance().loadRom())
				{
					RMX_LOG_INFO("ROM found at: " << WString(Configuration::instance().mLastRomPath).toStdString());
					setSetupScreenMessage("Loading Game", "Preparing mods, scripts, and resources...");
					mState = State::ROM_LOADED;
					return UpdateResult::CONTINUE_IMMEDIATE;
				}
			}
		#endif
			return UpdateResult::CONTINUE;
		}

		case State::ROM_LOADED:
		{
			// Initialize mods
			RMX_LOG_INFO("Mod manager initialization...");
			ModManager::instance().startup();

			// Update input after mods are loaded
			InputManager::instance().handleActiveModsChanged();

			// Load sprites
			RMX_LOG_INFO("Loading sprites");
			VideoOut::instance().getRenderResources().loadSprites();

			// Load resources
			RMX_LOG_INFO("Resource cache loading...");
			ResourcesCache::instance().loadAllResources();

			// Load fonts
			RMX_LOG_INFO("Font loading...");
			FontCollection::instance().reloadAll();

			// Load persistent data
			RMX_LOG_INFO("Persistent data loading...");
			PersistentData::instance().loadFromBasePath(Configuration::instance().mPersistentDataBasePath);

			// Load audio definitions
			EngineMain::instance().getAudioOut().handleGameLoaded();

			// Game loaded
			mState = State::READY;
			return UpdateResult::SUCCESS;
		}

		case State::READY:
		{
			// Nothing to do
			return UpdateResult::SUCCESS;
		}

		case State::FAILED:
		{
			return UpdateResult::FAILURE;
		}
	}

	// Unhandled state
	return UpdateResult::FAILURE;
}
