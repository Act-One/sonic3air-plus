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

#include "oxygen/rendering/vulkan/VulkanDevice.h"
#include "oxygen/rendering/vulkan/IPresentBackend.h"

#include "oxygen/helper/Logging.h"

#include <algorithm>
#include <cstring>
#include <sstream>


namespace
{
	template<typename T>
	bool loadInstanceFunction(T& outFunction, PFN_vkGetInstanceProcAddr getInstanceProcAddr, VkInstance instance, const char* name)
	{
		outFunction = reinterpret_cast<T>(getInstanceProcAddr(instance, name));
		return (nullptr != outFunction);
	}

	template<typename T>
	bool loadDeviceFunction(T& outFunction, PFN_vkGetDeviceProcAddr getDeviceProcAddr, VkDevice device, const char* name)
	{
		outFunction = reinterpret_cast<T>(getDeviceProcAddr(device, name));
		return (nullptr != outFunction);
	}

	template<typename T>
	void addUnique(std::vector<T>& target, const T& value)
	{
		if (std::find(target.begin(), target.end(), value) == target.end())
			target.push_back(value);
	}

	template<>
	void addUnique<const char*>(std::vector<const char*>& target, const char* const& value)
	{
		for (const char* existing : target)
		{
			if (nullptr != existing && nullptr != value && std::strcmp(existing, value) == 0)
				return;
		}
		target.push_back(value);
	}
}


bool vulkan::VulkanDevice::startup(const std::vector<const char*>& instanceExtensions, const std::vector<const char*>& deviceExtensions, SDL_Window& window, IPresentBackend& presentBackend)
{
	if (!loadGlobalFunctions())
		return false;

	std::vector<const char*> finalInstanceExtensions = instanceExtensions;
	addUnique<const char*>(finalInstanceExtensions, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

	if (!createInstance(finalInstanceExtensions))
		return false;
	if (!loadInstanceFunctions())
		return false;

	mEnabledDeviceExtensions = deviceExtensions;
	addUnique<const char*>(mEnabledDeviceExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	if (!pickPhysicalDevice(mEnabledDeviceExtensions, window, presentBackend))
		return false;

	RMX_LOG_INFO("Vulkan: creating logical device");
	if (!createLogicalDevice(mEnabledDeviceExtensions))
		return false;
	RMX_LOG_INFO("Vulkan: loading device functions");
	if (!loadDeviceFunctions())
		return false;
	RMX_LOG_INFO("Vulkan: device startup complete");

	return true;
}

void vulkan::VulkanDevice::shutdown()
{
	if (mDevice != VK_NULL_HANDLE)
	{
		mFunctions.DeviceWaitIdle(mDevice);
		mFunctions.DestroyDevice(mDevice, nullptr);
		mDevice = VK_NULL_HANDLE;
	}
	if (mInstance != VK_NULL_HANDLE)
	{
		mFunctions.DestroyInstance(mInstance, nullptr);
		mInstance = VK_NULL_HANDLE;
	}
	SDL_Vulkan_UnloadLibrary();
}

bool vulkan::VulkanDevice::loadGlobalFunctions()
{
	if (SDL_Vulkan_LoadLibrary(nullptr) != 0)
	{
		RMX_LOG_ERROR("SDL_Vulkan_LoadLibrary failed: " << SDL_GetError());
		return false;
	}

	mFunctions.GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
	if (nullptr == mFunctions.GetInstanceProcAddr)
	{
		RMX_LOG_ERROR("SDL_Vulkan_GetVkGetInstanceProcAddr returned null");
		return false;
	}

	loadInstanceFunction(mFunctions.EnumerateInstanceVersion, mFunctions.GetInstanceProcAddr, VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
	loadInstanceFunction(mFunctions.CreateInstance, mFunctions.GetInstanceProcAddr, VK_NULL_HANDLE, "vkCreateInstance");
	if (nullptr == mFunctions.CreateInstance)
	{
		RMX_LOG_ERROR("Failed to load vkCreateInstance");
		return false;
	}
	return true;
}

bool vulkan::VulkanDevice::createInstance(const std::vector<const char*>& instanceExtensions)
{
	const uint32_t apiVersion = (nullptr != mFunctions.EnumerateInstanceVersion) ? [] (PFN_vkEnumerateInstanceVersion enumerateInstanceVersion)
	{
		uint32_t version = VK_API_VERSION_1_0;
		enumerateInstanceVersion(&version);
		return version;
	}(mFunctions.EnumerateInstanceVersion) : VK_API_VERSION_1_0;

	if (VK_API_VERSION_MAJOR(apiVersion) < 1 || (VK_API_VERSION_MAJOR(apiVersion) == 1 && VK_API_VERSION_MINOR(apiVersion) < 3))
	{
		RMX_LOG_ERROR("Vulkan 1.3 is required, loader only reports " << VK_API_VERSION_MAJOR(apiVersion) << "." << VK_API_VERSION_MINOR(apiVersion));
		return false;
	}

	VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	appInfo.pApplicationName = "Sonic 3 A.I.R.";
	appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 26, 3, 28);
	appInfo.pEngineName = "Oxygen Engine";
	appInfo.engineVersion = VK_MAKE_API_VERSION(0, 26, 4, 26);
	appInfo.apiVersion = VK_API_VERSION_1_3;

	VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
	createInfo.ppEnabledExtensionNames = instanceExtensions.empty() ? nullptr : instanceExtensions.data();

	const VkResult result = mFunctions.CreateInstance(&createInfo, nullptr, &mInstance);
	if (result != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkCreateInstance failed with result " << (int)result);
		return false;
	}
	return true;
}

bool vulkan::VulkanDevice::loadInstanceFunctions()
{
	#define LOAD_INSTANCE_FN(member, name) \
		if (!loadInstanceFunction(mFunctions.member, mFunctions.GetInstanceProcAddr, mInstance, name)) \
		{ \
			RMX_LOG_ERROR("Failed to load Vulkan instance function " << name); \
			return false; \
		}

	LOAD_INSTANCE_FN(GetDeviceProcAddr, "vkGetDeviceProcAddr");
	LOAD_INSTANCE_FN(EnumeratePhysicalDevices, "vkEnumeratePhysicalDevices");
	LOAD_INSTANCE_FN(DestroyInstance, "vkDestroyInstance");
	LOAD_INSTANCE_FN(EnumerateDeviceExtensionProperties, "vkEnumerateDeviceExtensionProperties");
	LOAD_INSTANCE_FN(GetPhysicalDeviceProperties, "vkGetPhysicalDeviceProperties");
	LOAD_INSTANCE_FN(GetPhysicalDeviceProperties2, "vkGetPhysicalDeviceProperties2");
	LOAD_INSTANCE_FN(GetPhysicalDeviceFeatures2, "vkGetPhysicalDeviceFeatures2");
	LOAD_INSTANCE_FN(GetPhysicalDeviceMemoryProperties, "vkGetPhysicalDeviceMemoryProperties");
	LOAD_INSTANCE_FN(GetPhysicalDeviceMemoryProperties2, "vkGetPhysicalDeviceMemoryProperties2");
	LOAD_INSTANCE_FN(GetPhysicalDeviceQueueFamilyProperties, "vkGetPhysicalDeviceQueueFamilyProperties");
	LOAD_INSTANCE_FN(GetPhysicalDeviceFormatProperties, "vkGetPhysicalDeviceFormatProperties");
	LOAD_INSTANCE_FN(GetPhysicalDeviceSurfaceSupportKHR, "vkGetPhysicalDeviceSurfaceSupportKHR");
	LOAD_INSTANCE_FN(GetPhysicalDeviceSurfaceCapabilitiesKHR, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
	LOAD_INSTANCE_FN(GetPhysicalDeviceSurfaceFormatsKHR, "vkGetPhysicalDeviceSurfaceFormatsKHR");
	LOAD_INSTANCE_FN(GetPhysicalDeviceSurfacePresentModesKHR, "vkGetPhysicalDeviceSurfacePresentModesKHR");
	LOAD_INSTANCE_FN(CreateDevice, "vkCreateDevice");
	LOAD_INSTANCE_FN(DestroySurfaceKHR, "vkDestroySurfaceKHR");
	#undef LOAD_INSTANCE_FN
	return true;
}

bool vulkan::VulkanDevice::pickPhysicalDevice(const std::vector<const char*>& deviceExtensions, SDL_Window& window, IPresentBackend& presentBackend)
{
	uint32_t deviceCount = 0;
	VkResult result = mFunctions.EnumeratePhysicalDevices(mInstance, &deviceCount, nullptr);
	if (result != VK_SUCCESS || deviceCount == 0)
	{
		RMX_LOG_ERROR("No Vulkan physical devices available");
		return false;
	}

	std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
	result = mFunctions.EnumeratePhysicalDevices(mInstance, &deviceCount, physicalDevices.data());
	if (result != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkEnumeratePhysicalDevices failed with result " << (int)result);
		return false;
	}

	struct Candidate
	{
		VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
		VkPhysicalDeviceProperties mProperties = {};
		uint32_t mQueueFamilyIndex = 0;
		int mScore = -1;
	};

	Candidate bestCandidate;

	for (VkPhysicalDevice physicalDevice : physicalDevices)
	{
		if (!supportsDeviceExtensions(physicalDevice, deviceExtensions))
			continue;

		VkPhysicalDeviceProperties properties = {};
		mFunctions.GetPhysicalDeviceProperties(physicalDevice, &properties);

		uint32_t queueFamilyCount = 0;
		mFunctions.GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
		mFunctions.GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

		for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueFamilyCount; ++queueFamilyIndex)
		{
			const VkQueueFamilyProperties& queueFamily = queueFamilyProperties[queueFamilyIndex];
			if ((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 || queueFamily.queueCount == 0)
				continue;
			if (!presentBackend.supportsPhysicalDevice(*this, physicalDevice, queueFamilyIndex, window))
				continue;

			VkPhysicalDeviceVulkan13Features vulkan13Features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
			VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			features2.pNext = &vulkan13Features;
			mFunctions.GetPhysicalDeviceFeatures2(physicalDevice, &features2);
			if (!vulkan13Features.dynamicRendering || !vulkan13Features.synchronization2)
				continue;

			int score = 0;
			switch (properties.deviceType)
			{
				case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:	score = 300; break;
				case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:	score = 200; break;
				case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:	score = 100; break;
				default:									score = 0;   break;
			}

			if (score > bestCandidate.mScore)
			{
				bestCandidate.mPhysicalDevice = physicalDevice;
				bestCandidate.mProperties = properties;
				bestCandidate.mQueueFamilyIndex = queueFamilyIndex;
				bestCandidate.mScore = score;
			}
		}
	}

	if (bestCandidate.mPhysicalDevice == VK_NULL_HANDLE)
	{
		RMX_LOG_ERROR("No suitable Vulkan 1.3 physical device found");
		return false;
	}

	mPhysicalDevice = bestCandidate.mPhysicalDevice;
	mGraphicsQueueFamilyIndex = bestCandidate.mQueueFamilyIndex;
	mProperties = bestCandidate.mProperties;
	mFunctions.GetPhysicalDeviceMemoryProperties(mPhysicalDevice, &mMemoryProperties);
	RMX_LOG_INFO("Selected Vulkan device: " << mProperties.deviceName);
	return true;
}

bool vulkan::VulkanDevice::createLogicalDevice(const std::vector<const char*>& deviceExtensions)
{
	VkPhysicalDeviceVulkan13Features vulkan13Features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	vulkan13Features.dynamicRendering = VK_TRUE;
	vulkan13Features.synchronization2 = VK_TRUE;
	vulkan13Features.maintenance4 = VK_TRUE;

	const float queuePriority = 1.0f;
	VkDeviceQueueCreateInfo queueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queueCreateInfo.queueFamilyIndex = mGraphicsQueueFamilyIndex;
	queueCreateInfo.queueCount = 1;
	queueCreateInfo.pQueuePriorities = &queuePriority;

	VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceCreateInfo.queueCreateInfoCount = 1;
	deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
	deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
	deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.empty() ? nullptr : deviceExtensions.data();
	deviceCreateInfo.pNext = &vulkan13Features;

	{
		std::ostringstream stream;
		stream << "Vulkan: vkCreateDevice queueFamily=" << mGraphicsQueueFamilyIndex << ", extensions=[";
		for (size_t i = 0; i < deviceExtensions.size(); ++i)
		{
			if (i > 0)
				stream << ", ";
			stream << deviceExtensions[i];
		}
		stream << "], features={dynamicRendering=1, synchronization2=1, maintenance4=1}";
		RMX_LOG_INFO(stream.str());
	}

	const VkResult result = mFunctions.CreateDevice(mPhysicalDevice, &deviceCreateInfo, nullptr, &mDevice);
	RMX_LOG_INFO("Vulkan: vkCreateDevice result=" << (int)result << ", device=" << (void*)mDevice);
	if (result != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkCreateDevice failed with result " << (int)result);
		return false;
	}

	if (nullptr == mFunctions.GetDeviceProcAddr)
	{
		RMX_LOG_ERROR("Vulkan: vkGetDeviceProcAddr instance function is null before vkGetDeviceQueue lookup");
		return false;
	}

	RMX_LOG_INFO("Vulkan: loading vkGetDeviceQueue");
	if (!loadDeviceFunction(mFunctions.GetDeviceQueue, mFunctions.GetDeviceProcAddr, mDevice, "vkGetDeviceQueue"))
	{
		mFunctions.GetDeviceQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(mFunctions.GetInstanceProcAddr(mInstance, "vkGetDeviceQueue"));
		if (nullptr == mFunctions.GetDeviceQueue)
		{
			RMX_LOG_ERROR("Failed to load Vulkan device function vkGetDeviceQueue");
			return false;
		}
		RMX_LOG_INFO("Vulkan: vkGetDeviceQueue loaded through vkGetInstanceProcAddr fallback");
	}

	RMX_LOG_INFO("Vulkan: requesting graphics queue from family " << mGraphicsQueueFamilyIndex);
	mFunctions.GetDeviceQueue(mDevice, mGraphicsQueueFamilyIndex, 0, &mGraphicsQueue);
	RMX_LOG_INFO("Vulkan: graphics queue handle=" << (void*)mGraphicsQueue);
	return (mGraphicsQueue != VK_NULL_HANDLE);
}

bool vulkan::VulkanDevice::loadDeviceFunctions()
{
	#define LOAD_DEVICE_FN(member, name) \
		if (!loadDeviceFunction(mFunctions.member, mFunctions.GetDeviceProcAddr, mDevice, name)) \
		{ \
			RMX_LOG_ERROR("Failed to load Vulkan device function " << name); \
			return false; \
		}

	LOAD_DEVICE_FN(DestroyDevice, "vkDestroyDevice");
	LOAD_DEVICE_FN(GetDeviceQueue, "vkGetDeviceQueue");
	LOAD_DEVICE_FN(DeviceWaitIdle, "vkDeviceWaitIdle");
	LOAD_DEVICE_FN(CreateCommandPool, "vkCreateCommandPool");
	LOAD_DEVICE_FN(DestroyCommandPool, "vkDestroyCommandPool");
	LOAD_DEVICE_FN(ResetCommandPool, "vkResetCommandPool");
	LOAD_DEVICE_FN(AllocateCommandBuffers, "vkAllocateCommandBuffers");
	LOAD_DEVICE_FN(BeginCommandBuffer, "vkBeginCommandBuffer");
	LOAD_DEVICE_FN(EndCommandBuffer, "vkEndCommandBuffer");
	LOAD_DEVICE_FN(CreateFence, "vkCreateFence");
	LOAD_DEVICE_FN(DestroyFence, "vkDestroyFence");
	LOAD_DEVICE_FN(WaitForFences, "vkWaitForFences");
	LOAD_DEVICE_FN(ResetFences, "vkResetFences");
	LOAD_DEVICE_FN(CreateSemaphore, "vkCreateSemaphore");
	LOAD_DEVICE_FN(DestroySemaphore, "vkDestroySemaphore");
	LOAD_DEVICE_FN(QueueSubmit2, "vkQueueSubmit2");
	LOAD_DEVICE_FN(QueueWaitIdle, "vkQueueWaitIdle");
	LOAD_DEVICE_FN(CreateSwapchainKHR, "vkCreateSwapchainKHR");
	LOAD_DEVICE_FN(DestroySwapchainKHR, "vkDestroySwapchainKHR");
	LOAD_DEVICE_FN(GetSwapchainImagesKHR, "vkGetSwapchainImagesKHR");
	LOAD_DEVICE_FN(AcquireNextImageKHR, "vkAcquireNextImageKHR");
	LOAD_DEVICE_FN(QueuePresentKHR, "vkQueuePresentKHR");
	LOAD_DEVICE_FN(CreateBuffer, "vkCreateBuffer");
	LOAD_DEVICE_FN(DestroyBuffer, "vkDestroyBuffer");
	LOAD_DEVICE_FN(GetBufferMemoryRequirements, "vkGetBufferMemoryRequirements");
	LOAD_DEVICE_FN(GetBufferMemoryRequirements2, "vkGetBufferMemoryRequirements2");
	LOAD_DEVICE_FN(GetDeviceBufferMemoryRequirements, "vkGetDeviceBufferMemoryRequirements");
	LOAD_DEVICE_FN(BindBufferMemory, "vkBindBufferMemory");
	LOAD_DEVICE_FN(BindBufferMemory2, "vkBindBufferMemory2");
	LOAD_DEVICE_FN(CreateImage, "vkCreateImage");
	LOAD_DEVICE_FN(DestroyImage, "vkDestroyImage");
	LOAD_DEVICE_FN(GetImageMemoryRequirements, "vkGetImageMemoryRequirements");
	LOAD_DEVICE_FN(GetImageMemoryRequirements2, "vkGetImageMemoryRequirements2");
	LOAD_DEVICE_FN(GetDeviceImageMemoryRequirements, "vkGetDeviceImageMemoryRequirements");
	LOAD_DEVICE_FN(BindImageMemory, "vkBindImageMemory");
	LOAD_DEVICE_FN(BindImageMemory2, "vkBindImageMemory2");
	LOAD_DEVICE_FN(AllocateMemory, "vkAllocateMemory");
	LOAD_DEVICE_FN(FreeMemory, "vkFreeMemory");
	LOAD_DEVICE_FN(MapMemory, "vkMapMemory");
	LOAD_DEVICE_FN(UnmapMemory, "vkUnmapMemory");
	LOAD_DEVICE_FN(FlushMappedMemoryRanges, "vkFlushMappedMemoryRanges");
	LOAD_DEVICE_FN(InvalidateMappedMemoryRanges, "vkInvalidateMappedMemoryRanges");
	LOAD_DEVICE_FN(CreateImageView, "vkCreateImageView");
	LOAD_DEVICE_FN(DestroyImageView, "vkDestroyImageView");
	LOAD_DEVICE_FN(CreateSampler, "vkCreateSampler");
	LOAD_DEVICE_FN(DestroySampler, "vkDestroySampler");
	LOAD_DEVICE_FN(CreateDescriptorSetLayout, "vkCreateDescriptorSetLayout");
	LOAD_DEVICE_FN(DestroyDescriptorSetLayout, "vkDestroyDescriptorSetLayout");
	LOAD_DEVICE_FN(CreateDescriptorPool, "vkCreateDescriptorPool");
	LOAD_DEVICE_FN(DestroyDescriptorPool, "vkDestroyDescriptorPool");
	LOAD_DEVICE_FN(AllocateDescriptorSets, "vkAllocateDescriptorSets");
	LOAD_DEVICE_FN(UpdateDescriptorSets, "vkUpdateDescriptorSets");
	LOAD_DEVICE_FN(CreatePipelineLayout, "vkCreatePipelineLayout");
	LOAD_DEVICE_FN(DestroyPipelineLayout, "vkDestroyPipelineLayout");
	LOAD_DEVICE_FN(CreatePipelineCache, "vkCreatePipelineCache");
	LOAD_DEVICE_FN(DestroyPipelineCache, "vkDestroyPipelineCache");
	LOAD_DEVICE_FN(GetPipelineCacheData, "vkGetPipelineCacheData");
	LOAD_DEVICE_FN(CreateShaderModule, "vkCreateShaderModule");
	LOAD_DEVICE_FN(DestroyShaderModule, "vkDestroyShaderModule");
	LOAD_DEVICE_FN(CreateGraphicsPipelines, "vkCreateGraphicsPipelines");
	LOAD_DEVICE_FN(DestroyPipeline, "vkDestroyPipeline");
	LOAD_DEVICE_FN(CmdPipelineBarrier2, "vkCmdPipelineBarrier2");
	LOAD_DEVICE_FN(CmdBeginRendering, "vkCmdBeginRendering");
	LOAD_DEVICE_FN(CmdEndRendering, "vkCmdEndRendering");
	LOAD_DEVICE_FN(CmdSetViewport, "vkCmdSetViewport");
	LOAD_DEVICE_FN(CmdSetScissor, "vkCmdSetScissor");
	LOAD_DEVICE_FN(CmdBindPipeline, "vkCmdBindPipeline");
	LOAD_DEVICE_FN(CmdBindDescriptorSets, "vkCmdBindDescriptorSets");
	LOAD_DEVICE_FN(CmdBindVertexBuffers, "vkCmdBindVertexBuffers");
	LOAD_DEVICE_FN(CmdBindIndexBuffer, "vkCmdBindIndexBuffer");
	LOAD_DEVICE_FN(CmdDraw, "vkCmdDraw");
	LOAD_DEVICE_FN(CmdDrawIndexed, "vkCmdDrawIndexed");
	LOAD_DEVICE_FN(CmdCopyBuffer, "vkCmdCopyBuffer");
	LOAD_DEVICE_FN(CmdCopyBufferToImage, "vkCmdCopyBufferToImage");
	LOAD_DEVICE_FN(CmdCopyImageToBuffer, "vkCmdCopyImageToBuffer");
	LOAD_DEVICE_FN(CmdBlitImage, "vkCmdBlitImage");
	#undef LOAD_DEVICE_FN
	return true;
}

bool vulkan::VulkanDevice::supportsDeviceExtensions(VkPhysicalDevice physicalDevice, const std::vector<const char*>& requiredExtensions) const
{
	uint32_t extensionCount = 0;
	mFunctions.EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
	std::vector<VkExtensionProperties> extensionProperties(extensionCount);
	mFunctions.EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensionProperties.data());

	for (const char* requiredExtension : requiredExtensions)
	{
		bool found = false;
		for (const VkExtensionProperties& property : extensionProperties)
		{
			if (std::strcmp(property.extensionName, requiredExtension) == 0)
			{
				found = true;
				break;
			}
		}
		if (!found)
			return false;
	}
	return true;
}

#endif
