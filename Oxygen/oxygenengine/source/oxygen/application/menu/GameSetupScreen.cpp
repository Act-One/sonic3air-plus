/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/application/GameLoader.h"
#include "oxygen/application/menu/GameSetupScreen.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/helper/Utils.h"


GameSetupScreen::GameSetupScreen()
{
}

GameSetupScreen::~GameSetupScreen()
{
}

void GameSetupScreen::initialize()
{
	mTitleFont.setSize(10.0f);
	mTitleFont.addFontProcessor(std::make_shared<ShadowFontProcessor>(Vec2i(1, 1), 1.0f));

	mBodyFont.setSize(7.0f);
	mBodyFont.addFontProcessor(std::make_shared<ShadowFontProcessor>(Vec2i(1, 1), 1.0f));

	refreshText();
}

void GameSetupScreen::deinitialize()
{
}

void GameSetupScreen::update(float timeElapsed)
{
	mAnimationTimer += timeElapsed;
	refreshText();
	GuiBase::update(timeElapsed);
}

void GameSetupScreen::refreshText()
{
	const GameLoader& gameLoader = GameLoader::instance();
	const int wrapWidth = std::max(180, getRect().width - 48);
	if (mCachedTitle == gameLoader.getSetupScreenTitle() && mCachedText == gameLoader.getSetupScreenText() && mCachedWrapWidth == wrapWidth)
		return;

	mCachedTitle = gameLoader.getSetupScreenTitle();
	mCachedText = gameLoader.getSetupScreenText();
	mCachedWrapWidth = wrapWidth;

	utils::splitTextIntoLines(mTextLines, mCachedText, mBodyFont, wrapWidth);
}

void GameSetupScreen::render()
{
	GuiBase::render();

	Drawer& drawer = EngineMain::instance().getDrawer();

	const Recti screenRect = getRect();
	const int marginX = std::max(10, screenRect.width / 18);
	const int marginY = std::max(10, screenRect.height / 14);
	const Recti panel(screenRect.x + marginX, screenRect.y + marginY, screenRect.width - marginX * 2, screenRect.height - marginY * 2);
	const int titleHeight = mTitleFont.getLineHeight();
	const int bodyHeight = mBodyFont.getLineHeight();
	const int innerPadding = std::max(12, panel.width / 24);

	drawer.drawRect(screenRect, Color(0.05f, 0.08f, 0.12f));
	drawer.drawRect(panel, Color(0.0f, 0.0f, 0.0f, 0.70f));
	drawer.drawRect(Recti(panel.x, panel.y, panel.width, 2), Color(0.9f, 0.55f, 0.25f));

	drawer.printText(mTitleFont, Recti(panel.x + innerPadding, panel.y + innerPadding, panel.width - innerPadding * 2, titleHeight), mCachedTitle, 5, Color::WHITE);

	int py = panel.y + innerPadding + titleHeight + 12;
	for (const std::string& line : mTextLines)
	{
		if (py + bodyHeight > panel.y + panel.height - innerPadding)
			break;

		drawer.printText(mBodyFont, Recti(panel.x + innerPadding, py, panel.width - innerPadding * 2, bodyHeight), line, 1, Color(0.9f, 0.95f, 1.0f));
		py += bodyHeight + 4;
	}

	drawer.performRendering();
}
