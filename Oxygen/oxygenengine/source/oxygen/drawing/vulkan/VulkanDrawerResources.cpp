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

#include "oxygen/drawing/vulkan/VulkanDrawerResources.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/drawing/vulkan/VulkanUpscaler.h"


VulkanDrawerResources::~VulkanDrawerResources()
{
	shutdown();
}


bool VulkanDrawerResources::startup()
{
	shutdown();
	mSetupSuccessful = true;
	return true;
}

void VulkanDrawerResources::shutdown()
{
	for (std::unique_ptr<VulkanUpscaler>& upscaler : mUpscalers)
	{
		if (nullptr != upscaler)
			upscaler->shutdown();
		upscaler.reset();
	}
	if (nullptr != mBackend && mPresentedBitmapTexture.isValid())
	{
		mPresentedBitmapTexture.destroy(mBackend->getDevice(), mBackend->getAllocator());
	}
	mBackend.reset();
	mOutputWindow = nullptr;
	mSetupSuccessful = false;
}

void VulkanDrawerResources::clearAllCaches()
{
}

void VulkanDrawerResources::refresh(float deltaSeconds)
{
	(void)deltaSeconds;
}

bool VulkanDrawerResources::ensureWindowResources(SDL_Window* window)
{
	if (!mSetupSuccessful || nullptr == window)
		return false;

	mOutputWindow = window;
	if (nullptr == mBackend)
	{
		mBackend = std::make_unique<vulkan::RendererBackend>();
		mSetupSuccessful = mBackend->startup(*window);
		if (!mSetupSuccessful)
		{
			mBackend.reset();
		}
		else
		{
			for (size_t index = 0; index < mUpscalers.size(); ++index)
			{
				mUpscalers[index] = std::make_unique<VulkanUpscaler>((VulkanUpscaler::Type)index, *this);
				if (!mUpscalers[index]->startup())
				{
					mSetupSuccessful = false;
					break;
				}
			}
			if (!mSetupSuccessful)
			{
				shutdown();
				return false;
			}
		}
	}
	return mSetupSuccessful;
}

bool VulkanDrawerResources::presentBitmap(const Bitmap& bitmap, bool useVSync)
{
	if (bitmap.empty() || nullptr == mOutputWindow)
		return false;
	if (!ensureWindowResources(mOutputWindow) || nullptr == mBackend)
		return false;

	if (!mPresentedBitmapTexture.updateFromBitmap(mBackend->getDevice(), mBackend->getAllocator(), nullptr, bitmap, VK_FORMAT_R8G8B8A8_UNORM))
		return false;

	return getUpscaler().renderImage(mPresentedBitmapTexture, *mOutputWindow, useVSync);
}

bool VulkanDrawerResources::presentTexture(vulkan::Texture& texture, bool useVSync)
{
	if (nullptr == mOutputWindow || !texture.isValid())
		return false;
	if (!ensureWindowResources(mOutputWindow) || nullptr == mBackend)
		return false;

	return getUpscaler().renderImage(texture, *mOutputWindow, useVSync);
}

bool VulkanDrawerResources::presentRawTexture(vulkan::Texture& texture, bool useVSync)
{
	if (nullptr == mOutputWindow || !texture.isValid())
		return false;
	if (!ensureWindowResources(mOutputWindow) || nullptr == mBackend)
		return false;

	return mBackend->presentTexture(texture, *mOutputWindow, useVSync);
}

VulkanUpscaler& VulkanDrawerResources::getUpscaler()
{
	VulkanUpscaler::Type upscalerType = VulkanUpscaler::Type::DEFAULT;

	const int filtering = Configuration::instance().mFiltering;
	const int scanlines = Configuration::instance().mScanlines;
	if (scanlines > 0 && filtering < 3)
	{
		upscalerType = VulkanUpscaler::Type::SOFT;
	}
	else
	{
		switch (filtering)
		{
			default:
			case 0:
				upscalerType = VulkanUpscaler::Type::DEFAULT;
				break;

			case 1:
			case 2:
				upscalerType = VulkanUpscaler::Type::SOFT;
				break;

			case 3:
				upscalerType = VulkanUpscaler::Type::XBRZ;
				break;

			case 4:
			case 5:
			case 6:
				upscalerType = VulkanUpscaler::Type::HQX;
				break;
		}
	}

	return *mUpscalers[(size_t)upscalerType];
}

#endif
