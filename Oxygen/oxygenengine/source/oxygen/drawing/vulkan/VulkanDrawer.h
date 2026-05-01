/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)

#include "oxygen/drawing/DrawerInterface.h"

class VulkanDrawerResources;
class SoftwareDrawer;
namespace vulkandrawer
{
	struct Internal;
}


class VulkanDrawer final : public DrawerInterface
{
public:
	VulkanDrawer();
	~VulkanDrawer();

	inline Drawer::Type getType() override  { return Drawer::Type::VULKAN; }
	bool wasSetupSuccessful() override;

	void updateDrawer(float deltaSeconds) override;

	void createTexture(DrawerTexture& outTexture) override;
	void refreshTexture(DrawerTexture& texture) override;
	void setupRenderWindow(SDL_Window* window) override;
	void performRendering(const DrawCollection& drawCollection) override;
	void presentScreen() override;

	VulkanDrawerResources& getResources();

private:
	vulkandrawer::Internal& mInternal;
};

#endif
