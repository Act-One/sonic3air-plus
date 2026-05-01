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
	struct VulkanFunctions
	{
		PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
		PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;
		PFN_vkEnumerateInstanceVersion EnumerateInstanceVersion = nullptr;
		PFN_vkCreateInstance CreateInstance = nullptr;
		PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
		PFN_vkDestroyInstance DestroyInstance = nullptr;
		PFN_vkEnumerateDeviceExtensionProperties EnumerateDeviceExtensionProperties = nullptr;
		PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
		PFN_vkGetPhysicalDeviceProperties2 GetPhysicalDeviceProperties2 = nullptr;
		PFN_vkGetPhysicalDeviceFeatures2 GetPhysicalDeviceFeatures2 = nullptr;
		PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties = nullptr;
		PFN_vkGetPhysicalDeviceMemoryProperties2 GetPhysicalDeviceMemoryProperties2 = nullptr;
		PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties = nullptr;
		PFN_vkGetPhysicalDeviceFormatProperties GetPhysicalDeviceFormatProperties = nullptr;
		PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR = nullptr;
		PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
		PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR = nullptr;
		PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDeviceSurfacePresentModesKHR = nullptr;
		PFN_vkCreateDevice CreateDevice = nullptr;
		PFN_vkDestroyDevice DestroyDevice = nullptr;
		PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
		PFN_vkDeviceWaitIdle DeviceWaitIdle = nullptr;
		PFN_vkCreateCommandPool CreateCommandPool = nullptr;
		PFN_vkDestroyCommandPool DestroyCommandPool = nullptr;
		PFN_vkResetCommandPool ResetCommandPool = nullptr;
		PFN_vkAllocateCommandBuffers AllocateCommandBuffers = nullptr;
		PFN_vkBeginCommandBuffer BeginCommandBuffer = nullptr;
		PFN_vkEndCommandBuffer EndCommandBuffer = nullptr;
		PFN_vkCreateFence CreateFence = nullptr;
		PFN_vkDestroyFence DestroyFence = nullptr;
		PFN_vkWaitForFences WaitForFences = nullptr;
		PFN_vkResetFences ResetFences = nullptr;
		PFN_vkCreateSemaphore CreateSemaphore = nullptr;
		PFN_vkDestroySemaphore DestroySemaphore = nullptr;
		PFN_vkQueueSubmit2 QueueSubmit2 = nullptr;
		PFN_vkQueueWaitIdle QueueWaitIdle = nullptr;
		PFN_vkCreateSwapchainKHR CreateSwapchainKHR = nullptr;
		PFN_vkDestroySwapchainKHR DestroySwapchainKHR = nullptr;
		PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR = nullptr;
		PFN_vkAcquireNextImageKHR AcquireNextImageKHR = nullptr;
		PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;
		PFN_vkDestroySurfaceKHR DestroySurfaceKHR = nullptr;
		PFN_vkCreateBuffer CreateBuffer = nullptr;
		PFN_vkDestroyBuffer DestroyBuffer = nullptr;
		PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements = nullptr;
		PFN_vkGetBufferMemoryRequirements2 GetBufferMemoryRequirements2 = nullptr;
		PFN_vkGetDeviceBufferMemoryRequirements GetDeviceBufferMemoryRequirements = nullptr;
		PFN_vkBindBufferMemory BindBufferMemory = nullptr;
		PFN_vkBindBufferMemory2 BindBufferMemory2 = nullptr;
		PFN_vkCreateImage CreateImage = nullptr;
		PFN_vkDestroyImage DestroyImage = nullptr;
		PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements = nullptr;
		PFN_vkGetImageMemoryRequirements2 GetImageMemoryRequirements2 = nullptr;
		PFN_vkGetDeviceImageMemoryRequirements GetDeviceImageMemoryRequirements = nullptr;
		PFN_vkBindImageMemory BindImageMemory = nullptr;
		PFN_vkBindImageMemory2 BindImageMemory2 = nullptr;
		PFN_vkAllocateMemory AllocateMemory = nullptr;
		PFN_vkFreeMemory FreeMemory = nullptr;
		PFN_vkMapMemory MapMemory = nullptr;
		PFN_vkUnmapMemory UnmapMemory = nullptr;
		PFN_vkFlushMappedMemoryRanges FlushMappedMemoryRanges = nullptr;
		PFN_vkInvalidateMappedMemoryRanges InvalidateMappedMemoryRanges = nullptr;
		PFN_vkCreateImageView CreateImageView = nullptr;
		PFN_vkDestroyImageView DestroyImageView = nullptr;
		PFN_vkCreateSampler CreateSampler = nullptr;
		PFN_vkDestroySampler DestroySampler = nullptr;
		PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout = nullptr;
		PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout = nullptr;
		PFN_vkCreateDescriptorPool CreateDescriptorPool = nullptr;
		PFN_vkDestroyDescriptorPool DestroyDescriptorPool = nullptr;
		PFN_vkAllocateDescriptorSets AllocateDescriptorSets = nullptr;
		PFN_vkUpdateDescriptorSets UpdateDescriptorSets = nullptr;
		PFN_vkCreatePipelineLayout CreatePipelineLayout = nullptr;
		PFN_vkDestroyPipelineLayout DestroyPipelineLayout = nullptr;
		PFN_vkCreatePipelineCache CreatePipelineCache = nullptr;
		PFN_vkDestroyPipelineCache DestroyPipelineCache = nullptr;
		PFN_vkGetPipelineCacheData GetPipelineCacheData = nullptr;
		PFN_vkCreateShaderModule CreateShaderModule = nullptr;
		PFN_vkDestroyShaderModule DestroyShaderModule = nullptr;
		PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines = nullptr;
		PFN_vkDestroyPipeline DestroyPipeline = nullptr;
		PFN_vkCmdPipelineBarrier2 CmdPipelineBarrier2 = nullptr;
		PFN_vkCmdBeginRendering CmdBeginRendering = nullptr;
		PFN_vkCmdEndRendering CmdEndRendering = nullptr;
		PFN_vkCmdSetViewport CmdSetViewport = nullptr;
		PFN_vkCmdSetScissor CmdSetScissor = nullptr;
		PFN_vkCmdBindPipeline CmdBindPipeline = nullptr;
		PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets = nullptr;
		PFN_vkCmdBindVertexBuffers CmdBindVertexBuffers = nullptr;
		PFN_vkCmdBindIndexBuffer CmdBindIndexBuffer = nullptr;
		PFN_vkCmdDraw CmdDraw = nullptr;
		PFN_vkCmdDrawIndexed CmdDrawIndexed = nullptr;
		PFN_vkCmdCopyBuffer CmdCopyBuffer = nullptr;
		PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage = nullptr;
		PFN_vkCmdCopyImageToBuffer CmdCopyImageToBuffer = nullptr;
		PFN_vkCmdBlitImage CmdBlitImage = nullptr;
	};

	class VulkanDevice
	{
	public:
		bool startup(const std::vector<const char*>& instanceExtensions, const std::vector<const char*>& deviceExtensions, SDL_Window& window, class IPresentBackend& presentBackend);
		void shutdown();

		inline const VulkanFunctions& vk() const				{ return mFunctions; }
		inline VkInstance getInstance() const					{ return mInstance; }
		inline VkPhysicalDevice getPhysicalDevice() const		{ return mPhysicalDevice; }
		inline VkDevice getDevice() const						{ return mDevice; }
		inline VkQueue getGraphicsQueue() const				{ return mGraphicsQueue; }
		inline uint32_t getGraphicsQueueFamilyIndex() const	{ return mGraphicsQueueFamilyIndex; }
		inline VkPhysicalDeviceProperties getProperties() const{ return mProperties; }
		inline VkPhysicalDeviceMemoryProperties getMemoryProperties() const { return mMemoryProperties; }
		inline VkPhysicalDeviceLimits getLimits() const		{ return mProperties.limits; }
		inline const std::vector<const char*>& getEnabledDeviceExtensions() const { return mEnabledDeviceExtensions; }

	private:
		bool loadGlobalFunctions();
		bool createInstance(const std::vector<const char*>& instanceExtensions);
		bool pickPhysicalDevice(const std::vector<const char*>& deviceExtensions, SDL_Window& window, IPresentBackend& presentBackend);
		bool createLogicalDevice(const std::vector<const char*>& deviceExtensions);
		bool loadInstanceFunctions();
		bool loadDeviceFunctions();
		bool supportsDeviceExtensions(VkPhysicalDevice physicalDevice, const std::vector<const char*>& requiredExtensions) const;

	private:
		VulkanFunctions mFunctions;
		VkInstance mInstance = VK_NULL_HANDLE;
		VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
		VkDevice mDevice = VK_NULL_HANDLE;
		VkQueue mGraphicsQueue = VK_NULL_HANDLE;
		uint32_t mGraphicsQueueFamilyIndex = 0;
		VkPhysicalDeviceProperties mProperties = {};
		VkPhysicalDeviceMemoryProperties mMemoryProperties = {};
		std::vector<const char*> mEnabledDeviceExtensions;
	};
}

#endif
