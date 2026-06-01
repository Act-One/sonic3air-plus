/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen/drawing/DrawerInterface.h"
#include "oxygen/drawing/software/SoftwareDrawer.h"


class GX2Drawer final : public DrawerInterface
{
public:
	GX2Drawer();
	~GX2Drawer();

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
	SoftwareDrawer mSoftwareDrawer;
};
