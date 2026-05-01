/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_WINDOWS)

#include "oxygen/drawing/DrawerInterface.h"

class D3D11DrawerResources;
namespace d3d11drawer
{
	struct Internal;
}


class D3D11Drawer final : public DrawerInterface
{
public:
	D3D11Drawer();
	~D3D11Drawer();

	inline Drawer::Type getType() override  { return Drawer::Type::DIRECT3D11; }
	bool wasSetupSuccessful() override;

	void updateDrawer(float deltaSeconds) override;

	void createTexture(DrawerTexture& outTexture) override;
	void refreshTexture(DrawerTexture& texture) override;
	void setupRenderWindow(SDL_Window* window) override;
	void performRendering(const DrawCollection& drawCollection) override;
	void presentScreen() override;

	D3D11DrawerResources& getResources();

private:
	d3d11drawer::Internal& mInternal;
};

#endif
