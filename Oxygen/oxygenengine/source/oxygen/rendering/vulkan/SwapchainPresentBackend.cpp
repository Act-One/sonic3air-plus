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

#include "oxygen/rendering/vulkan/SwapchainPresentBackend.h"
#include "oxygen/rendering/vulkan/Texture.h"
#include "oxygen/rendering/vulkan/VulkanDevice.h"

#include "oxygen/helper/Logging.h"


namespace
{
	void recordImageBarrier(vulkan::VulkanDevice& device, VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags2 srcStageMask, VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask)
	{
		VkImageMemoryBarrier2 imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		imageBarrier.srcStageMask = srcStageMask;
		imageBarrier.srcAccessMask = srcAccessMask;
		imageBarrier.dstStageMask = dstStageMask;
		imageBarrier.dstAccessMask = dstAccessMask;
		imageBarrier.oldLayout = oldLayout;
		imageBarrier.newLayout = newLayout;
		imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.image = image;
		imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBarrier.subresourceRange.baseMipLevel = 0;
		imageBarrier.subresourceRange.levelCount = 1;
		imageBarrier.subresourceRange.baseArrayLayer = 0;
		imageBarrier.subresourceRange.layerCount = 1;

		VkDependencyInfo dependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependencyInfo.imageMemoryBarrierCount = 1;
		dependencyInfo.pImageMemoryBarriers = &imageBarrier;
		device.vk().CmdPipelineBarrier2(commandBuffer, &dependencyInfo);
	}

	void clampSwapExtent(const VkSurfaceCapabilitiesKHR& surfaceCapabilities, Vec2i& extent)
	{
		if (surfaceCapabilities.currentExtent.width != 0xffffffffu)
		{
			extent.x = (int)surfaceCapabilities.currentExtent.width;
			extent.y = (int)surfaceCapabilities.currentExtent.height;
			return;
		}

		extent.x = clamp(extent.x, (int)surfaceCapabilities.minImageExtent.width, (int)surfaceCapabilities.maxImageExtent.width);
		extent.y = clamp(extent.y, (int)surfaceCapabilities.minImageExtent.height, (int)surfaceCapabilities.maxImageExtent.height);
	}

	void getWindowSizeForRendering(SDL_Window* window, int& outWidth, int& outHeight)
	{
		outWidth = 0;
		outHeight = 0;
		if (nullptr == window)
			return;

		SDL_GetWindowSizeInPixels(window, &outWidth, &outHeight);
		if (outWidth <= 0 || outHeight <= 0)
		{
			SDL_GetWindowSize(window, &outWidth, &outHeight);
		}
	}
}


void vulkan::SwapchainPresentBackend::collectInstanceExtensions(SDL_Window& window, std::vector<const char*>& outExtensions) const
{
	unsigned extensionCount = 0;
	if (!SDL_Vulkan_GetInstanceExtensions(&window, &extensionCount, nullptr))
	{
		RMX_LOG_WARNING("SDL_Vulkan_GetInstanceExtensions count query failed: " << SDL_GetError());
		return;
	}

	const size_t existingSize = outExtensions.size();
	outExtensions.resize(existingSize + extensionCount);
	if (!SDL_Vulkan_GetInstanceExtensions(&window, &extensionCount, &outExtensions[existingSize]))
	{
		RMX_LOG_WARNING("SDL_Vulkan_GetInstanceExtensions query failed: " << SDL_GetError());
		outExtensions.resize(existingSize);
	}
}

void vulkan::SwapchainPresentBackend::collectDeviceExtensions(std::vector<const char*>& outExtensions) const
{
	outExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
}

bool vulkan::SwapchainPresentBackend::init(VulkanDevice& device, SDL_Window& window, Texture& sourceTexture, bool useVSync)
{
	mWindow = &window;
	mSourceTexture = &sourceTexture;

	if (!createSurface(device, window))
		return false;
	return createSwapchain(device, sourceTexture, useVSync);
}

bool vulkan::SwapchainPresentBackend::supportsPhysicalDevice(VulkanDevice& device, VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, SDL_Window& window)
{
	if (!createSurface(device, window))
		return false;

	VkBool32 supported = VK_FALSE;
	const VkResult supportResult = device.vk().GetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, mSurface, &supported);
	if (supportResult != VK_SUCCESS || !supported)
		return false;

	uint32_t formatCount = 0;
	device.vk().GetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, mSurface, &formatCount, nullptr);
	if (formatCount == 0)
		return false;

	uint32_t presentModeCount = 0;
	device.vk().GetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, mSurface, &presentModeCount, nullptr);
	return (presentModeCount > 0);
}

void vulkan::SwapchainPresentBackend::resize(VulkanDevice& device, SDL_Window& window, Texture& sourceTexture, bool useVSync)
{
	mWindow = &window;
	mSourceTexture = &sourceTexture;
	device.vk().DeviceWaitIdle(device.getDevice());
	destroySwapchain(device);
	createSwapchain(device, sourceTexture, useVSync);
}

bool vulkan::SwapchainPresentBackend::acquireImage(VulkanDevice& device, uint32_t frameIndex, VkSemaphore imageAvailableSemaphore, uint32_t& outImageIndex, VkImage& outImage)
{
	(void)frameIndex;

	if (mSwapchain == VK_NULL_HANDLE)
		return false;

	const VkResult result = device.vk().AcquireNextImageKHR(device.getDevice(), mSwapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &outImageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
		return false;
	if (!isSuccess(result))
	{
		RMX_LOG_ERROR("vkAcquireNextImageKHR failed with result " << (int)result);
		return false;
	}

	outImage = (outImageIndex < mSwapchainImages.size()) ? mSwapchainImages[outImageIndex] : VK_NULL_HANDLE;
	return (outImage != VK_NULL_HANDLE);
}

bool vulkan::SwapchainPresentBackend::recordPresent(VulkanDevice& device, VkCommandBuffer commandBuffer, VkImage sourceImage, VkImageLayout sourceImageLayout, const Vec2i& sourceSize, uint32_t imageIndex)
{
	if (commandBuffer == VK_NULL_HANDLE || sourceImage == VK_NULL_HANDLE || imageIndex >= mSwapchainImages.size())
		return false;

	const VkPipelineStageFlags2 sourceStageMask = (sourceImageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT :
		(sourceImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT :
		(sourceImageLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) ? VK_PIPELINE_STAGE_2_COPY_BIT :
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	const VkAccessFlags2 sourceAccessMask = (sourceImageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT :
		(sourceImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) ? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT :
		(sourceImageLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) ? VK_ACCESS_2_TRANSFER_WRITE_BIT :
		0;

	recordImageBarrier(device, commandBuffer, sourceImage, sourceImageLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		sourceStageMask,
		VK_PIPELINE_STAGE_2_BLIT_BIT,
		sourceAccessMask,
		VK_ACCESS_2_TRANSFER_READ_BIT);

	const VkImageLayout oldSwapchainLayout = (imageIndex < mSwapchainImageLayouts.size()) ? mSwapchainImageLayouts[imageIndex] : VK_IMAGE_LAYOUT_UNDEFINED;
	recordImageBarrier(device, commandBuffer, mSwapchainImages[imageIndex], oldSwapchainLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		(oldSwapchainLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_2_NONE,
		VK_PIPELINE_STAGE_2_BLIT_BIT,
		0,
		VK_ACCESS_2_TRANSFER_WRITE_BIT);

	VkImageBlit blit = {};
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.srcSubresource.mipLevel = 0;
	blit.srcSubresource.baseArrayLayer = 0;
	blit.srcSubresource.layerCount = 1;
	blit.srcOffsets[0] = { 0, 0, 0 };
	blit.srcOffsets[1] = { sourceSize.x, sourceSize.y, 1 };
	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.dstSubresource.mipLevel = 0;
	blit.dstSubresource.baseArrayLayer = 0;
	blit.dstSubresource.layerCount = 1;
	blit.dstOffsets[0] = { 0, 0, 0 };
	blit.dstOffsets[1] = { mExtent.x, mExtent.y, 1 };
	device.vk().CmdBlitImage(commandBuffer, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mSwapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

	recordImageBarrier(device, commandBuffer, mSwapchainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_PIPELINE_STAGE_2_BLIT_BIT,
		VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		0);

	mSwapchainImageLayouts[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	return true;
}

bool vulkan::SwapchainPresentBackend::queuePresent(VulkanDevice& device, uint32_t frameIndex, uint32_t imageIndex, VkSemaphore renderCompleteSemaphore)
{
	(void)frameIndex;

	if (mSwapchain == VK_NULL_HANDLE)
		return false;

	VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &renderCompleteSemaphore;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &mSwapchain;
	presentInfo.pImageIndices = &imageIndex;

	const VkResult result = device.vk().QueuePresentKHR(device.getGraphicsQueue(), &presentInfo);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
		return false;
	if (!isSuccess(result))
	{
		RMX_LOG_ERROR("vkQueuePresentKHR failed with result " << (int)result);
		return false;
	}
	return true;
}

void vulkan::SwapchainPresentBackend::shutdown(VulkanDevice& device)
{
	destroySwapchain(device);
	destroySurface(device);
	mSourceTexture = nullptr;
	mWindow = nullptr;
	mExtent = Vec2i();
}

VkFormat vulkan::SwapchainPresentBackend::getSwapchainFormat() const
{
	return mSurfaceFormat.format;
}

Vec2i vulkan::SwapchainPresentBackend::getExtent() const
{
	return mExtent;
}

bool vulkan::SwapchainPresentBackend::isVSyncEnabled() const
{
	return mUseVSync;
}

bool vulkan::SwapchainPresentBackend::createSurface(VulkanDevice& device, SDL_Window& window)
{
	if (mSurface != VK_NULL_HANDLE)
		return true;

	if (!SDL_Vulkan_CreateSurface(&window, device.getInstance(), &mSurface))
	{
		RMX_LOG_ERROR("SDL_Vulkan_CreateSurface failed: " << SDL_GetError());
		return false;
	}

	return true;
}

void vulkan::SwapchainPresentBackend::destroySurface(VulkanDevice& device)
{
	if (mSurface != VK_NULL_HANDLE)
	{
		device.vk().DestroySurfaceKHR(device.getInstance(), mSurface, nullptr);
		mSurface = VK_NULL_HANDLE;
	}
}

bool vulkan::SwapchainPresentBackend::createSwapchain(VulkanDevice& device, Texture& sourceTexture, bool useVSync)
{
	if (mSurface == VK_NULL_HANDLE)
		return false;

	VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
	if (device.vk().GetPhysicalDeviceSurfaceCapabilitiesKHR(device.getPhysicalDevice(), mSurface, &surfaceCapabilities) != VK_SUCCESS)
		return false;

	mSourceTexture = &sourceTexture;
	mUseVSync = useVSync;
	mSurfaceFormat = chooseSurfaceFormat(device);
	mPresentMode = choosePresentMode(device, useVSync);

	int windowWidth = 0;
	int windowHeight = 0;
	if (nullptr != mWindow)
		getWindowSizeForRendering(mWindow, windowWidth, windowHeight);
	mExtent.set(std::max(windowWidth, 1), std::max(windowHeight, 1));
	clampSwapExtent(surfaceCapabilities, mExtent);

	uint32_t imageCount = 2;
	if (mPresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
		imageCount = 3;
	imageCount = std::max(imageCount, surfaceCapabilities.minImageCount);
	if (surfaceCapabilities.maxImageCount > 0)
		imageCount = std::min(imageCount, surfaceCapabilities.maxImageCount);

	VkSwapchainCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	createInfo.surface = mSurface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = mSurfaceFormat.format;
	createInfo.imageColorSpace = mSurfaceFormat.colorSpace;
	createInfo.imageExtent = { (uint32_t)mExtent.x, (uint32_t)mExtent.y };
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	createInfo.preTransform = surfaceCapabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = mPresentMode;
	createInfo.clipped = VK_TRUE;
	createInfo.oldSwapchain = VK_NULL_HANDLE;

	if (device.vk().CreateSwapchainKHR(device.getDevice(), &createInfo, nullptr, &mSwapchain) != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkCreateSwapchainKHR failed");
		return false;
	}

	uint32_t swapchainImageCount = 0;
	if (device.vk().GetSwapchainImagesKHR(device.getDevice(), mSwapchain, &swapchainImageCount, nullptr) != VK_SUCCESS || swapchainImageCount == 0)
		return false;

	mSwapchainImages.resize(swapchainImageCount);
	mSwapchainImageLayouts.assign(swapchainImageCount, VK_IMAGE_LAYOUT_UNDEFINED);
	if (device.vk().GetSwapchainImagesKHR(device.getDevice(), mSwapchain, &swapchainImageCount, mSwapchainImages.data()) != VK_SUCCESS)
		return false;

	return true;
}

void vulkan::SwapchainPresentBackend::destroySwapchain(VulkanDevice& device)
{
	mSwapchainImages.clear();
	mSwapchainImageLayouts.clear();
	if (mSwapchain != VK_NULL_HANDLE)
	{
		device.vk().DestroySwapchainKHR(device.getDevice(), mSwapchain, nullptr);
		mSwapchain = VK_NULL_HANDLE;
	}
}

VkSurfaceFormatKHR vulkan::SwapchainPresentBackend::chooseSurfaceFormat(VulkanDevice& device) const
{
	VkSurfaceFormatKHR fallback = { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };

	uint32_t formatCount = 0;
	if (device.vk().GetPhysicalDeviceSurfaceFormatsKHR(device.getPhysicalDevice(), mSurface, &formatCount, nullptr) != VK_SUCCESS || formatCount == 0)
		return fallback;

	std::vector<VkSurfaceFormatKHR> formats(formatCount);
	device.vk().GetPhysicalDeviceSurfaceFormatsKHR(device.getPhysicalDevice(), mSurface, &formatCount, formats.data());
	fallback = formats[0];

	for (const VkSurfaceFormatKHR& format : formats)
	{
		if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			return format;
	}
	for (const VkSurfaceFormatKHR& format : formats)
	{
		if (format.format == VK_FORMAT_R8G8B8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			return format;
	}
	return fallback;
}

VkPresentModeKHR vulkan::SwapchainPresentBackend::choosePresentMode(VulkanDevice& device, bool useVSync) const
{
	if (useVSync)
		return VK_PRESENT_MODE_FIFO_KHR;

	uint32_t presentModeCount = 0;
	if (device.vk().GetPhysicalDeviceSurfacePresentModesKHR(device.getPhysicalDevice(), mSurface, &presentModeCount, nullptr) != VK_SUCCESS || presentModeCount == 0)
		return VK_PRESENT_MODE_FIFO_KHR;

	std::vector<VkPresentModeKHR> presentModes(presentModeCount);
	device.vk().GetPhysicalDeviceSurfacePresentModesKHR(device.getPhysicalDevice(), mSurface, &presentModeCount, presentModes.data());

	for (const VkPresentModeKHR presentMode : presentModes)
	{
		if (presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
			return presentMode;
	}
	return VK_PRESENT_MODE_FIFO_KHR;
}

#endif
