/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"
#include "sonic3air/menu/GameMenuManager.h"
#include "sonic3air/menu/GameMenuBase.h"


void GameMenuManager::initWithRoot(GuiBase& root)
{
	mRoot = &root;
}

void GameMenuManager::updateMenus()
{
	// Remove all menus that are faded out now, or for some other reason queued to be removed
	for (GameMenuBase* menu : mActiveMenus)
	{
		if (menu->canBeRemoved())
		{
			mMenusToBeRemoved.push_back(menu);
		}
	}

	while (!mMenusToBeRemoved.empty())
	{
		GameMenuBase* menu = mMenusToBeRemoved.back();
		if (nullptr != menu)
		{
			menu->removeFromParent();
		}
		mActiveMenus.erase(menu);
		mMenusToBeRemoved.pop_back();
	}
}

void GameMenuManager::addMenu(GameMenuBase& menu)
{
	const bool wasActive = (mActiveMenus.find(&menu) != mActiveMenus.end());
#if defined(PLATFORM_WIIU)
	static constexpr bool ENABLE_WIIU_MENU_MANAGER_TRACE = false;
	if constexpr (ENABLE_WIIU_MENU_MANAGER_TRACE)
	{
		static int sWiiUAddMenuLogCount = 0;
		if (sWiiUAddMenuLogCount < 32)
		{
			RMX_LOG_INFO("[WiiU Menu] addMenu menu=" << (uintptr_t)&menu << " wasActive=" << wasActive << " hadParent=" << (nullptr != menu.getParent()) << " baseState=" << (int)menu.getBaseState());
			++sWiiUAddMenuLogCount;
		}
	}
#endif
	if (nullptr == menu.getParent())
	{
		mRoot->addChild(menu);
	}
	mActiveMenus.insert(&menu);

	if (!wasActive || menu.getBaseState() == GameMenuBase::BaseState::INACTIVE)
	{
		menu.onFadeIn();
	}
}

void GameMenuManager::forceRemoveAll()
{
	for (GameMenuBase* menu : mActiveMenus)
	{
		mMenusToBeRemoved.push_back(menu);
	}

	while (!mMenusToBeRemoved.empty())
	{
		GameMenuBase* menu = mMenusToBeRemoved.back();
		if (nullptr != menu)
		{
			menu->removeFromParent();
		}
		mActiveMenus.erase(menu);
		mMenusToBeRemoved.pop_back();
	}
}
