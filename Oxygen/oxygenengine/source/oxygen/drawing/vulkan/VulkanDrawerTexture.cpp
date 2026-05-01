/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)

#include "oxygen/drawing/vulkan/VulkanDrawerTexture.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/drawing/vulkan/VulkanDrawer.h"
#include "oxygen/drawing/vulkan/VulkanDrawerResources.h"
#include "oxygen/rendering/vulkan/Renderer.h"
#include "oxygen/helper/Logging.h"


namespace
{
	VulkanDrawerResources& getDrawerResources()
	{
		DrawerInterface* drawer = EngineMain::instance().getDrawer().getActiveDrawer();
		RMX_ASSERT(nullptr != drawer, "Expected an active drawer");
		RMX_ASSERT(drawer->getType() == Drawer::Type::VULKAN, "Expected the Vulkan drawer");
		return static_cast<VulkanDrawer*>(drawer)->getResources();
	}
}


VulkanDrawerTexture::~VulkanDrawerTexture()
{
	if (!mTexture.isValid())
		return;

	DrawerInterface* drawer = EngineMain::instance().getDrawer().getActiveDrawer();
	if (nullptr == drawer || drawer->getType() != Drawer::Type::VULKAN)
		return;

	VulkanDrawerResources& drawerResources = getDrawerResources();
	vulkan::RendererBackend* backend = drawerResources.getBackend();
	if (nullptr != backend)
	{
		mTexture.destroy(backend->getDevice(), backend->getAllocator());
	}
}


void VulkanDrawerTexture::updateFromBitmap(const Bitmap& bitmap)
{
	VulkanDrawerResources& drawerResources = getDrawerResources();
	vulkan::RendererBackend* backend = drawerResources.getBackend();
	if (nullptr == backend)
		return;

	mTexture.updateFromBitmap(backend->getDevice(), backend->getAllocator(), nullptr, bitmap, VK_FORMAT_R8G8B8A8_UNORM);
}

void VulkanDrawerTexture::setupAsRenderTarget(const Vec2i& size)
{
	mOwner.accessBitmap().create(size.x, size.y);

	VulkanDrawerResources& drawerResources = getDrawerResources();
	vulkan::RendererBackend* backend = drawerResources.getBackend();
	if (nullptr == backend)
		return;

	if (!mTexture.isValid() || mTexture.getSize() != size)
	{
		mTexture.create(backend->getDevice(), backend->getAllocator(), vulkan::OFFSCREEN_FORMAT, size, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true, true);
	}
}

void VulkanDrawerTexture::writeContentToBitmap(Bitmap& outBitmap)
{
	VulkanDrawerResources& drawerResources = getDrawerResources();
	vulkan::RendererBackend* backend = drawerResources.getBackend();
	if (nullptr == backend)
		return;

	mTexture.readToBitmap(backend->getDevice(), backend->getAllocator(), outBitmap);
}

void VulkanDrawerTexture::refreshImplementation(bool setupRenderTarget, const Vec2i& size)
{
	if (setupRenderTarget)
	{
		setupAsRenderTarget(size);
	}
	else if (!mOwner.accessBitmap().empty())
	{
		updateFromBitmap(mOwner.accessBitmap());
	}
}

#endif
