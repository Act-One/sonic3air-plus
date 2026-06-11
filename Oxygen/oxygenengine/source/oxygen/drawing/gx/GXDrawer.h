/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen/drawing/DrawerInterface.h"


class GXDrawer final : public DrawerInterface
{
public:
	GXDrawer();
	~GXDrawer();

	Drawer::Type getType() override;
	bool wasSetupSuccessful() override;

	void createTexture(DrawerTexture& outTexture) override;
	void refreshTexture(DrawerTexture& texture) override;
	void setupRenderWindow(SDL_Window* window) override;
	void performRendering(const DrawCollection& drawCollection) override;
	void presentScreen() override;

private:
	struct Internal;
	Internal& mInternal;
};
