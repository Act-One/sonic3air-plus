/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen/rendering/vulkan/IPresentBackend.h"

#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)

namespace vulkan
{
	class SwapchainPresentBackend final : public IPresentBackend
	{
	public:
		void collectInstanceExtensions(SDL_Window& window, std::vector<const char*>& outExtensions) const override;
		void collectDeviceExtensions(std::vector<const char*>& outExtensions) const override;
		bool init(VulkanDevice& device, SDL_Window& window, Texture& sourceTexture, bool useVSync) override;
		bool supportsPhysicalDevice(VulkanDevice& device, VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, SDL_Window& window) override;
		void resize(VulkanDevice& device, SDL_Window& window, Texture& sourceTexture, bool useVSync) override;
		bool acquireImage(VulkanDevice& device, uint32_t frameIndex, VkSemaphore imageAvailableSemaphore, uint32_t& outImageIndex, VkImage& outImage) override;
		bool recordPresent(VulkanDevice& device, VkCommandBuffer commandBuffer, VkImage sourceImage, VkImageLayout sourceImageLayout, const Vec2i& sourceSize, uint32_t imageIndex) override;
		bool queuePresent(VulkanDevice& device, uint32_t frameIndex, uint32_t imageIndex, VkSemaphore renderCompleteSemaphore) override;
		void shutdown(VulkanDevice& device) override;
		VkFormat getSwapchainFormat() const override;
		Vec2i getExtent() const override;
		bool isVSyncEnabled() const override;

	private:
		bool createSurface(VulkanDevice& device, SDL_Window& window);
		void destroySurface(VulkanDevice& device);
		bool createSwapchain(VulkanDevice& device, Texture& sourceTexture, bool useVSync);
		void destroySwapchain(VulkanDevice& device);
		VkSurfaceFormatKHR chooseSurfaceFormat(VulkanDevice& device) const;
		VkPresentModeKHR choosePresentMode(VulkanDevice& device, bool useVSync) const;

	private:
		SDL_Window* mWindow = nullptr;
		Texture* mSourceTexture = nullptr;
		VkSurfaceKHR mSurface = VK_NULL_HANDLE;
		VkSwapchainKHR mSwapchain = VK_NULL_HANDLE;
		std::vector<VkImage> mSwapchainImages;
		std::vector<VkImageLayout> mSwapchainImageLayouts;
		VkSurfaceFormatKHR mSurfaceFormat = {};
		VkPresentModeKHR mPresentMode = VK_PRESENT_MODE_FIFO_KHR;
		Vec2i mExtent;
		bool mUseVSync = true;
	};
}

#endif
