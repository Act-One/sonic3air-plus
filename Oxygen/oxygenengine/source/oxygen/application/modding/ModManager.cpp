/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/application/modding/ModManager.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/file/ZipFileProvider.h"
#include "oxygen/helper/JsonHelper.h"
#include "oxygen/helper/Logging.h"
#include "oxygen/helper/Utils.h"

namespace
{
	std::wstring normalizeModListEntry(const std::string& activeEntry)
	{
		std::wstring path = String(activeEntry).toStdWString();
		std::replace(path.begin(), path.end(), L'\\', L'/');

		std::wstring lowerPath = path;
		std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
		const size_t modsPos = lowerPath.rfind(L"/mods/");
		if (modsPos != std::wstring::npos)
		{
			path.erase(0, modsPos + 6);
			lowerPath.erase(0, modsPos + 6);
		}
		while (rmx::startsWith(path, L"./"))
		{
			path.erase(0, 2);
		}
		while (!path.empty() && path.front() == L'/')
		{
			path.erase(path.begin());
		}
		if (rmx::startsWithCaseInsensitive(path, L"mods/"))
		{
			path.erase(0, 5);
		}
		while (!path.empty() && path.back() == L'/')
		{
			path.pop_back();
		}
		if (rmx::endsWithCaseInsensitive(path, L"/mod.json"))
		{
			path.erase(path.length() - 9);
		}
		return path;
	}

	void addModLookup(std::unordered_map<uint64, Mod*>& lookup, const std::string& value, Mod& mod)
	{
		if (!value.empty())
		{
			lookup[rmx::getMurmur2_64(value)] = &mod;
		}
	}

	void addModLookup(std::unordered_map<uint64, Mod*>& lookup, const std::wstring& value, Mod& mod)
	{
		if (!value.empty())
		{
			lookup[rmx::getMurmur2_64(WString(value).toStdString())] = &mod;
		}
	}
}


ModManager::~ModManager()
{
	clear();
}

void ModManager::startup()
{
	// Update base path (actually only needs to be done once, it shouldn't change afterwards anyways)
	mBasePath = Configuration::instance().mGameAppDataPath + L"mods/";
	FTX::FileSystem->createDirectory(mBasePath);
	RMX_LOG_INFO("ModManager: scanning mods at " << WString(mBasePath).toStdString());
#if defined(PLATFORM_WIIU)
	{
		std::vector<std::wstring> appRootModPaths;
		std::wstring appRootPath = Configuration::instance().mGameAppDataPath;
		std::replace(appRootPath.begin(), appRootPath.end(), L'\\', L'/');
		if (rmx::endsWithCaseInsensitive(appRootPath, L"/savedata/"))
		{
			appRootPath.erase(appRootPath.length() - 9);
			appRootModPaths.emplace_back(appRootPath + L"mods/");
		}
		appRootModPaths.emplace_back(L"fs:/vol/external01/wiiu/apps/sonic3air/mods/");
		appRootModPaths.emplace_back(L"/vol/external01/wiiu/apps/sonic3air/mods/");

		for (const std::wstring& appRootModsPath : appRootModPaths)
		{
			if (appRootModsPath != mBasePath && FTX::FileSystem->exists(appRootModsPath))
			{
				rmx::RealFileProvider* provider = new rmx::RealFileProvider();
				FTX::FileSystem->addManagedFileProvider(*provider);
				FTX::FileSystem->addMountPoint(*provider, mBasePath + L"root/", appRootModsPath, 0x40);
				RMX_LOG_INFO("ModManager: also scanning Wii U app-root mods at " << WString(appRootModsPath).toStdString());
				break;
			}
		}
	}
#endif

	// First go through all mod directories recursively to gather all installed mods
	scanMods();

	// Now get the list of active mods
	//  -> Check if there's an "active-mods.json" file and read it
	std::wstring activeModsPath = mBasePath + L"active-mods.json";
#if defined(PLATFORM_WIIU)
	if (!FTX::FileSystem->exists(activeModsPath) && FTX::FileSystem->exists(mBasePath + L"root/active-mods.json"))
	{
		activeModsPath = mBasePath + L"root/active-mods.json";
		RMX_LOG_INFO("ModManager: using Wii U app-root active mod list");
	}
#endif
	if (FTX::FileSystem->exists(activeModsPath))
	{
		Json::Value json = JsonHelper::loadFile(activeModsPath);

		Json::Value activeMods = json["ActiveMods"];
		const int numMods = activeMods.isArray() ? (int)activeMods.size() : 0;
		for (int i = 0; i < numMods; ++i)
		{
			if (!activeMods[i].isString())
				continue;

			Mod* mod = findModForActiveEntry(activeMods[i].asString());
			if (nullptr == mod)
			{
				RMX_LOG_INFO("ModManager: active mod entry not found: " << activeMods[i].asString());
			}
			else
			{
				// Make this mod active
				mod->mState = Mod::State::ACTIVE;
				mActiveMods.emplace_back(mod);
			}
		}
	}

	// Load the mod-specific settings
	copyModSettingsFromConfig();

	onActiveModsChanged(true);
	RMX_LOG_INFO("ModManager: startup found " << (uint32)mAllMods.size() << " mods, active " << (uint32)mActiveMods.size());
}

void ModManager::clear()
{
	for (auto& pair : mZipFileProviders)
	{
		delete pair.second;
	}
	mZipFileProviders.clear();
	for (Mod* mod : mAllMods)
		delete mod;
	mAllMods.clear();
	mActiveMods.clear();
	mModsByLocalDirectoryHash.clear();
	mModsByIDHash.clear();
}

bool ModManager::rescanMods()
{
	return scanMods();
}

void ModManager::saveActiveMods()
{
	if (mBasePath.empty())
		return;

	Json::Value root;
	{
		Json::Value modNames(Json::arrayValue);
		for (Mod* mod : mActiveMods)
		{
			modNames.append(WString(mod->mLocalDirectory).toStdString());
		}
		root["ActiveMods"] = modNames;
		root["UseLegacyLoading"] = false;
	}

	JsonHelper::saveFile(mBasePath + L"active-mods.json", root);
}

void ModManager::setActiveMods(const std::vector<Mod*>& newActiveModsList)
{
	// Reset flags
	for (Mod* mod : mAllMods)
	{
		mod->mWillGetActive = false;
	}

	// Mark the now active mods as such
	for (Mod* mod : newActiveModsList)
	{
		RMX_CHECK(mod->mState != Mod::State::FAILED, "Mod \"" << mod->mDisplayName << "\" is failed, can't be made active", continue);
		if (mod->mState == Mod::State::INACTIVE)
		{
			mod->mState = Mod::State::ACTIVE;
		}
		mod->mWillGetActive = true;
	}

	// Remove previously active mods that are not part of the new list
	for (Mod* mod : mActiveMods)
	{
		if (!mod->mWillGetActive)
		{
			mod->mState = Mod::State::INACTIVE;
		}
	}

	// Update list of active mods
	mActiveMods = newActiveModsList;

	onActiveModsChanged();
}

bool ModManager::anyActiveModUsesFeature(uint64 featureNameHash) const
{
	for (const Mod* mod : mActiveMods)
	{
		if (nullptr != mod->getUsedFeature(featureNameHash))
			return true;
	}
	return false;
}

void ModManager::copyModSettingsFromConfig()
{
	Configuration& config = Configuration::instance();
	for (Mod* mod : mAllMods)
	{
		if (mod->mSettingCategories.empty())
			continue;

		// First try the directory name (which has been used to save the mod settings since they existed)
		uint64 hash = rmx::getMurmur2_64(mod->mDirectoryName);
		const auto it = config.mModSettings.find(hash);
		if (it == config.mModSettings.end())
		{
			// Try the unique ID instead (forward compatibility with future versions, which may actually use that one)
			hash = rmx::getMurmur2_64(mod->mUniqueID);
			const auto it = config.mModSettings.find(hash);
			if (it == config.mModSettings.end())
				continue;
		}

		const Configuration::Mod& configMod = it->second;
		if (configMod.mSettings.empty())
			continue;

		for (Mod::SettingCategory& modSettingCategory : mod->mSettingCategories)
		{
			for (Mod::Setting& modSetting : modSettingCategory.mSettings)
			{
				const uint64 hash2 = rmx::getMurmur2_64(modSetting.mIdentifier);
				const auto it2 = configMod.mSettings.find(hash2);
				if (it2 != configMod.mSettings.end())
				{
					modSetting.mCurrentValue = it2->second.mValue;
				}
			}
		}
	}
}

void ModManager::copyModSettingsToConfig()
{
	Configuration& config = Configuration::instance();
	for (Mod* mod : mAllMods)
	{
		const uint64 hash = rmx::getMurmur2_64(mod->mDirectoryName);
		Configuration::Mod& configMod = config.mModSettings[hash];
		configMod.mModName = mod->mDirectoryName;
		configMod.mSettings.clear();

		for (Mod::SettingCategory& modSettingCategory : mod->mSettingCategories)
		{
			for (Mod::Setting& modSetting : modSettingCategory.mSettings)
			{
				const uint64 hash2 = rmx::getMurmur2_64(modSetting.mIdentifier);
				Configuration::Mod::Setting& configModSetting = configMod.mSettings[hash2];
				configModSetting.mIdentifier = modSetting.mIdentifier;
				configModSetting.mValue = modSetting.mCurrentValue;
			}
		}
	}
}

bool ModManager::addZipFileProvider(const std::wstring& zipLocalPath)
{
	const auto it = mZipFileProviders.find(zipLocalPath);
	if (it != mZipFileProviders.end())
	{
		// Already added, nothing else to do
		return true;
	}

	// Create a new zip file provider
	ZipFileProvider* provider = new ZipFileProvider(mBasePath + zipLocalPath);
	if (provider->isLoaded())
	{
		// Mount using the zip file name as a virtual folder name
		FTX::FileSystem->addMountPoint(*provider, mBasePath + zipLocalPath + L"/", L"", 0x100);
		mZipFileProviders[zipLocalPath] = provider;

		// Done
		RMX_LOG_INFO("Loaded mod zip file: " << WString(zipLocalPath).toStdString());
		return true;
	}
	else
	{
		// Failure
		RMX_LOG_INFO("Failed to load mod zip file: " << WString(zipLocalPath).toStdString());
		delete provider;
		return false;
	}
}

bool ModManager::tryRemoveZipFileProvider(const std::wstring& zipLocalPath)
{
	const auto it = mZipFileProviders.find(zipLocalPath);
	if (it == mZipFileProviders.end())
		return false;

	// Note that destroying the file provider will automatically remove its mount points
	delete it->second;
	mZipFileProviders.erase(it);
	return true;
}
// the bruh function (this does mostly everything)
Mod* ModManager::findModForActiveEntry(const std::string& activeEntry) const
{
	const std::wstring localPath = normalizeModListEntry(activeEntry);
	const auto it = mModsByLocalDirectoryHash.find(rmx::getMurmur2_64(localPath));
	if (it != mModsByLocalDirectoryHash.end())
	{
		return it->second;
	}

	const auto it2 = mModsByIDHash.find(rmx::getMurmur2_64(activeEntry));
	if (it2 != mModsByIDHash.end())
	{
		return it2->second;
	}

	const std::string normalizedEntry = WString(localPath).toStdString();
	const auto it3 = mModsByIDHash.find(rmx::getMurmur2_64(normalizedEntry));
	return (it3 != mModsByIDHash.end()) ? it3->second : nullptr;
}

bool ModManager::scanMods()
{
	// Mark all existing mods as dirty first
	for (Mod* existingMod : mAllMods)
	{
		existingMod->mDirty = true;
	}

	// Check for zip files in the mods directory
	{
		std::vector<std::wstring> zipPaths;
		findZipsRecursively(zipPaths, L"", 3);
		std::set<std::wstring> zipPathSet(zipPaths.begin(), zipPaths.end());
		for (auto it = mZipFileProviders.begin(); it != mZipFileProviders.end(); )
		{
			if (zipPathSet.find(it->first) == zipPathSet.end())
			{
				RMX_LOG_INFO("Unloaded removed mod zip file: " << WString(it->first).toStdString());
				delete it->second;
				it = mZipFileProviders.erase(it);
			}
			else
			{
				++it;
			}
		}
		for (const std::wstring& zipPath : zipPaths)
		{
			addZipFileProvider(zipPath);
		}
	}

	// Scan mod directory
	std::vector<FoundMod> foundMods;
	foundMods.reserve(0x100);		// We can be generous here to avoid reallocations
	scanDirectoryRecursive(foundMods, L"");

	// Have a closer look at the mods found
	bool anyChange = false;
	for (const FoundMod& foundMod : foundMods)
	{
		const std::string directoryName = WString(foundMod.mDirectoryName).toStdString();
		const Json::Value& root = foundMod.mModJson;

		std::string errorMessage;
		{
			Json::Value metadataJson = root["Metadata"];
			if (metadataJson.isObject())
			{
				Json::Value value = metadataJson["GameVersion"];
				if (value.isString())
				{
					const uint32 versionNumber = utils::getVersionNumberFromString(value.asString());
					if (versionNumber != 0 && versionNumber > EngineMain::getDelegate().getAppMetaData().mBuildVersionNumber)
					{
						errorMessage = "Mod '" + directoryName + "' requires newer game version v" + utils::getVersionStringFromNumber(versionNumber) + ".";
					}
				}
			}
		}

		const std::wstring localDirectory = foundMod.mLocalPath + foundMod.mDirectoryName;
		const uint64 localDirectoryHash = rmx::getMurmur2_64(localDirectory);
		const auto it = mModsByLocalDirectoryHash.find(localDirectoryHash);
		if (it != mModsByLocalDirectoryHash.end())
		{
			// It's an already known mod, mark as still present
			Mod* mod = it->second;
			mod->mDirty = false;

			// TODO: Check if mod got changed since last scan
			//  -> Having a different modification date of "mod.json" should be a good enough lightweight test
			//  -> If changed, reload mod; and if it's active, reload content as well

		}
		else
		{
			// Add as a new mod
			Mod* mod = new Mod();
			mod->mUniqueID = directoryName;		// Just a fallback in case it's not overwritten in "Mod::loadFromJson" below
			mod->mDirectoryName = directoryName;
			mod->mLocalDirectory = localDirectory;
			mod->mFullPath = mBasePath + localDirectory + L'/';
			mod->mLocalDirectoryHash = localDirectoryHash;

			mAllMods.emplace_back(mod);
			mModsByLocalDirectoryHash[localDirectoryHash] = mod;
			anyChange = true;

			if (errorMessage.empty())
			{
				RMX_LOG_INFO("Found mod: '" << directoryName << "'");
				mod->mState = Mod::State::INACTIVE;
			}
			else
			{
				RMX_LOG_INFO("Could not load mod: '" << directoryName << "'");
				mod->mState = Mod::State::FAILED;
				mod->mFailedMessage = errorMessage;
			}

			// Load mod meta data from JSON
			mod->loadFromJson(root);

			const uint64 idHash = rmx::getMurmur2_64(mod->mUniqueID);
			mModsByIDHash[idHash] = mod;
		}
	}

	// Check for mods still marked dirty, those got deleted
	{
		bool anyActiveModsRemoved = false;
		for (auto it = mAllMods.begin(); it != mAllMods.end(); )
		{
			Mod* mod = *it;
			if (mod->mDirty)
			{
				if (mod->mState == Mod::State::ACTIVE)
				{
					const auto it2 = std::find(mActiveMods.begin(), mActiveMods.end(), mod);
					if (it2 != mActiveMods.end())
					{
						mActiveMods.erase(it2);
						anyActiveModsRemoved = true;
					}
				}
				mModsByLocalDirectoryHash.erase(mod->mLocalDirectoryHash);
				it = mAllMods.erase(it);
				delete mod;
				anyChange = true;
			}
			else
			{
				++it;
			}
		}

		if (anyActiveModsRemoved)
		{
			onActiveModsChanged();
		}
	}

	// Sort mod list by directory name
	std::sort(mAllMods.begin(), mAllMods.end(), [](const Mod* a, const Mod* b)
	{
		const int cmp = a->mLocalDirectory.compare(b->mLocalDirectory);
		return (cmp != 0) ? (cmp < 0) : (a->mDirectoryName < b->mDirectoryName);
	});

	rebuildModIdLookup();

	if (anyChange)
	{
		RMX_LOG_INFO("ModManager: scan complete, found " << (uint32)mAllMods.size() << " mods");
	}
	return anyChange;
}
// should probably check if its a lemon mod because those can be inconsistent on wii u but for now just leave it
void ModManager::rebuildModIdLookup()
{
	mModsByIDHash.clear();
	for (Mod* mod : mAllMods)
	{
		addModLookup(mModsByIDHash, mod->mUniqueID, *mod);
		addModLookup(mModsByIDHash, mod->mDirectoryName, *mod);
		addModLookup(mModsByIDHash, mod->mDisplayName, *mod);
		addModLookup(mModsByIDHash, mod->mLocalDirectory, *mod);
		addModLookup(mModsByIDHash, normalizeModListEntry(WString(mod->mLocalDirectory).toStdString()), *mod);
	}
}

void ModManager::scanDirectoryRecursive(std::vector<FoundMod>& outFoundMods, const std::wstring& localPath)
{
	std::vector<std::wstring> subDirectories;
	FTX::FileSystem->listDirectories(mBasePath + localPath, subDirectories);

	for (const std::wstring& directoryName : subDirectories)
	{
		// Completely ignore directory names starting with #
		if (directoryName[0] != L'#')
		{
			// Check if this directory is itself a mod
			Json::Value root = JsonHelper::loadFile(mBasePath + localPath + directoryName + L"/mod.json");
			if (root.isObject())
			{
				// Looks like this directory is meant to be a mod
				FoundMod& foundMod = vectorAdd(outFoundMods);
				foundMod.mLocalPath = localPath;
				foundMod.mDirectoryName = directoryName;
				foundMod.mModJson = root;
			}
			else
			{
				// No "mod.json" found, scan subdirectories
				scanDirectoryRecursive(outFoundMods, localPath + directoryName + L'/');
			}
		}
	}
}

void ModManager::findZipsRecursively(std::vector<std::wstring>& outZipPaths, const std::wstring& localPath, int maxDepth)
{
	std::vector<rmx::FileIO::FileEntry> zipFileEntries;
	FTX::FileSystem->listFilesByMask(mBasePath + localPath + L"*.zip", false, zipFileEntries);
	for (const rmx::FileIO::FileEntry& zipFileEntry : zipFileEntries)
	{
		outZipPaths.push_back(localPath + zipFileEntry.mFilename);
	}

	if (maxDepth > 0)
	{
		std::vector<std::wstring> subDirectories;
		FTX::FileSystem->listDirectories(mBasePath + localPath, subDirectories);
		for (const std::wstring& subDirectory : subDirectories)
		{
			// Completely ignore directory names starting with #
			if (subDirectory[0] != '#')
			{
				// Also ignore directories that are mods themselves already
				if (!FTX::FileSystem->exists(mBasePath + localPath + subDirectory + L"/mod.json"))
				{
					findZipsRecursively(outZipPaths, localPath + subDirectory + L'/', maxDepth - 1);
				}
			}
		}
	}
}

void ModManager::onActiveModsChanged(bool duringStartup)
{
	// Update priorities values in mods
	for (size_t index = 0; index < mActiveMods.size(); ++index)
	{
		mActiveMods[index]->mActivePriority = (uint32)index;
	}

	// Rebuild lookup map
	mActiveModsByNameHash.clear();
	for (Mod* mod : mActiveMods)
	{
		// Add under all different names that can refer to the mod
		mActiveModsByNameHash.emplace(rmx::getMurmur2_64(mod->mUniqueID), mod);
		mActiveModsByNameHash.emplace(rmx::getMurmur2_64(mod->mDirectoryName), mod);
		mActiveModsByNameHash.emplace(rmx::getMurmur2_64(mod->mDisplayName), mod);
	}

	if (!duringStartup)		// Not needed during startup, as the engine performs the necessary loading steps anyways afterwards
	{
		// Tell the engine so it can make the necessary updates in all systems
		EngineMain::instance().onActiveModsChanged();
	}
}
