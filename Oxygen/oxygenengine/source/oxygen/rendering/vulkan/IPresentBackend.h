/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen/rendering/vulkan/VulkanCommon.h"

#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)

namespace vulkan
{
	class VulkanDevice;
	class Texture;

	class IPresentBackend
	{
	public:
		virtual ~IPresentBackend() = default;

		virtual void collectInstanceExtensions(SDL_Window& window, std::vector<const char*>& outExtensions) const = 0;
		virtual void collectDeviceExtensions(std::vector<const char*>& outExtensions) const = 0;
		virtual bool init(VulkanDevice& device, SDL_Window& window, Texture& sourceTexture, bool useVSync) = 0;
		virtual bool supportsPhysicalDevice(VulkanDevice& device, VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, SDL_Window& window) = 0;
		virtual void resize(VulkanDevice& device, SDL_Window& window, Texture& sourceTexture, bool useVSync) = 0;
		virtual bool acquireImage(VulkanDevice& device, uint32_t frameIndex, VkSemaphore imageAvailableSemaphore, uint32_t& outImageIndex, VkImage& outImage) = 0;
		virtual bool recordPresent(VulkanDevice& device, VkCommandBuffer commandBuffer, VkImage sourceImage, VkImageLayout sourceImageLayout, const Vec2i& sourceSize, uint32_t imageIndex) = 0;
		virtual bool queuePresent(VulkanDevice& device, uint32_t frameIndex, uint32_t imageIndex, VkSemaphore renderCompleteSemaphore) = 0;
		virtual void shutdown(VulkanDevice& device) = 0;
		virtual VkFormat getSwapchainFormat() const = 0;
		virtual Vec2i getExtent() const = 0;
		virtual bool isVSyncEnabled() const = 0;
	};
}

#endif
