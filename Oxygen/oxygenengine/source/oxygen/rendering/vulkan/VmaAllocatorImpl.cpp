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

#include "oxygen/rendering/vulkan/VmaAllocatorImpl.h"
#include "oxygen/rendering/vulkan/VulkanDevice.h"

#include "oxygen/helper/Logging.h"

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_VULKAN_VERSION 1003000
#if defined(PLATFORM_WIIU)
namespace wiiu_vma
{
	template<typename T>
	class SingleThreadAtomic
	{
	public:
		SingleThreadAtomic() = default;
		SingleThreadAtomic(T value) : mValue(value) {}

		T load() const { return mValue; }
		void store(T value) { mValue = value; }
		T fetch_add(T value) { const T oldValue = mValue; mValue += value; return oldValue; }
		T fetch_sub(T value) { const T oldValue = mValue; mValue -= value; return oldValue; }
		bool compare_exchange_strong(T& expected, T desired)
		{
			if (mValue == expected)
			{
				mValue = desired;
				return true;
			}
			expected = mValue;
			return false;
		}

		operator T() const { return mValue; }
		T operator++() { return mValue += 1; }
		T operator--() { return mValue -= 1; }
		T operator+=(T value) { return mValue += value; }
		T operator-=(T value) { return mValue -= value; }

	private:
		T mValue = 0;
	};
}

// devkitPPC does not ship libatomic; VMA only uses these counters for allocator bookkeeping here.
#define VMA_ATOMIC_UINT32 wiiu_vma::SingleThreadAtomic<uint32_t>
#define VMA_ATOMIC_UINT64 wiiu_vma::SingleThreadAtomic<uint64_t>
#define VMA_MEMORY_BUDGET 0
#endif
#include "oxygen/rendering/vulkan/external/vk_mem_alloc.h"


namespace
{
	inline VmaAllocator castAllocator(void* allocator)
	{
		return reinterpret_cast<VmaAllocator>(allocator);
	}

	inline VmaAllocation castAllocation(void* allocation)
	{
		return reinterpret_cast<VmaAllocation>(allocation);
	}
}


bool vulkan::VmaAllocatorImpl::startup(VulkanDevice& device)
{
	shutdown(device);
	RMX_LOG_INFO("Vulkan: VMA setup begin");

	VmaVulkanFunctions vulkanFunctions = {};
	vulkanFunctions.vkGetInstanceProcAddr = device.vk().GetInstanceProcAddr;
	vulkanFunctions.vkGetDeviceProcAddr = device.vk().GetDeviceProcAddr;
	vulkanFunctions.vkGetPhysicalDeviceProperties = device.vk().GetPhysicalDeviceProperties;
	vulkanFunctions.vkGetPhysicalDeviceMemoryProperties = device.vk().GetPhysicalDeviceMemoryProperties;
	vulkanFunctions.vkAllocateMemory = device.vk().AllocateMemory;
	vulkanFunctions.vkFreeMemory = device.vk().FreeMemory;
	vulkanFunctions.vkMapMemory = device.vk().MapMemory;
	vulkanFunctions.vkUnmapMemory = device.vk().UnmapMemory;
	vulkanFunctions.vkFlushMappedMemoryRanges = device.vk().FlushMappedMemoryRanges;
	vulkanFunctions.vkInvalidateMappedMemoryRanges = device.vk().InvalidateMappedMemoryRanges;
	vulkanFunctions.vkBindBufferMemory = device.vk().BindBufferMemory;
	vulkanFunctions.vkBindImageMemory = device.vk().BindImageMemory;
	vulkanFunctions.vkGetBufferMemoryRequirements = device.vk().GetBufferMemoryRequirements;
	vulkanFunctions.vkGetImageMemoryRequirements = device.vk().GetImageMemoryRequirements;
	vulkanFunctions.vkCreateBuffer = device.vk().CreateBuffer;
	vulkanFunctions.vkDestroyBuffer = device.vk().DestroyBuffer;
	vulkanFunctions.vkCreateImage = device.vk().CreateImage;
	vulkanFunctions.vkDestroyImage = device.vk().DestroyImage;
	vulkanFunctions.vkCmdCopyBuffer = device.vk().CmdCopyBuffer;
	vulkanFunctions.vkGetBufferMemoryRequirements2KHR = device.vk().GetBufferMemoryRequirements2;
	vulkanFunctions.vkGetImageMemoryRequirements2KHR = device.vk().GetImageMemoryRequirements2;
	vulkanFunctions.vkBindBufferMemory2KHR = device.vk().BindBufferMemory2;
	vulkanFunctions.vkBindImageMemory2KHR = device.vk().BindImageMemory2;
	vulkanFunctions.vkGetPhysicalDeviceMemoryProperties2KHR = device.vk().GetPhysicalDeviceMemoryProperties2;
	vulkanFunctions.vkGetDeviceBufferMemoryRequirements = device.vk().GetDeviceBufferMemoryRequirements;
	vulkanFunctions.vkGetDeviceImageMemoryRequirements = device.vk().GetDeviceImageMemoryRequirements;

	VmaAllocatorCreateInfo createInfo = {};
	createInfo.flags = VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT |
					   VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT;
	createInfo.physicalDevice = device.getPhysicalDevice();
	createInfo.device = device.getDevice();
	createInfo.instance = device.getInstance();
	createInfo.vulkanApiVersion = VK_API_VERSION_1_3;
	createInfo.pVulkanFunctions = &vulkanFunctions;

	VmaAllocator allocator = VK_NULL_HANDLE;
	RMX_LOG_INFO("Vulkan: calling vmaCreateAllocator");
	const VkResult result = vmaCreateAllocator(&createInfo, &allocator);
	if (result != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vmaCreateAllocator failed with result " << (int)result);
		return false;
	}

	mAllocator = allocator;
	mDevice = &device;
	RMX_LOG_INFO("Vulkan: VMA setup complete");
	return true;
}

void vulkan::VmaAllocatorImpl::shutdown(VulkanDevice& device)
{
	(void)device;

	if (nullptr != mAllocator)
	{
		vmaDestroyAllocator(castAllocator(mAllocator));
		mAllocator = nullptr;
	}
	mDevice = nullptr;
}

bool vulkan::VmaAllocatorImpl::allocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, BufferAllocation& outAllocation)
{
	if (nullptr == mAllocator)
		return false;

	VkBufferCreateInfo createInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	createInfo.size = std::max<VkDeviceSize>(size, 1);
	createInfo.usage = usage;
	createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocationCreateInfo = {};
	allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
	allocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocation allocation = VK_NULL_HANDLE;
	VmaAllocationInfo allocationInfo = {};
	const VkResult result = vmaCreateBuffer(castAllocator(mAllocator), &createInfo, &allocationCreateInfo, &outAllocation.mBuffer, &allocation, &allocationInfo);
	if (result != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vmaCreateBuffer failed with result " << (int)result);
		outAllocation = {};
		return false;
	}

	outAllocation.mMappedData = allocationInfo.pMappedData;
	outAllocation.mSize = allocationInfo.size;
	outAllocation.mAllocation = allocation;
	return true;
}

bool vulkan::VmaAllocatorImpl::allocateImage(const VkImageCreateInfo& createInfo, ImageAllocation& outAllocation)
{
	if (nullptr == mAllocator)
		return false;

	VmaAllocationCreateInfo allocationCreateInfo = {};
	allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	VmaAllocation allocation = VK_NULL_HANDLE;
	const VkResult result = vmaCreateImage(castAllocator(mAllocator), &createInfo, &allocationCreateInfo, &outAllocation.mImage, &allocation, nullptr);
	if (result != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vmaCreateImage failed with result " << (int)result);
		outAllocation = {};
		return false;
	}

	outAllocation.mAllocation = allocation;
	return true;
}

void vulkan::VmaAllocatorImpl::destroyBuffer(BufferAllocation& allocation)
{
	if (nullptr == mAllocator || allocation.mBuffer == VK_NULL_HANDLE || nullptr == allocation.mAllocation)
	{
		allocation = {};
		return;
	}

	vmaDestroyBuffer(castAllocator(mAllocator), allocation.mBuffer, castAllocation(allocation.mAllocation));
	allocation = {};
}

void vulkan::VmaAllocatorImpl::destroyImage(ImageAllocation& allocation)
{
	if (nullptr == mAllocator || allocation.mImage == VK_NULL_HANDLE || nullptr == allocation.mAllocation)
	{
		allocation = {};
		return;
	}

	vmaDestroyImage(castAllocator(mAllocator), allocation.mImage, castAllocation(allocation.mAllocation));
	allocation = {};
}

void* vulkan::VmaAllocatorImpl::map(BufferAllocation& allocation)
{
	if (nullptr == mAllocator || allocation.mBuffer == VK_NULL_HANDLE || nullptr == allocation.mAllocation)
		return nullptr;

	if (nullptr != allocation.mMappedData)
		return allocation.mMappedData;

	void* mappedData = nullptr;
	if (vmaMapMemory(castAllocator(mAllocator), castAllocation(allocation.mAllocation), &mappedData) == VK_SUCCESS)
	{
		allocation.mMappedData = mappedData;
	}
	return allocation.mMappedData;
}

void vulkan::VmaAllocatorImpl::unmap(BufferAllocation& allocation)
{
	// Buffers are allocated persistently mapped by design, so there is nothing to do here.
	(void)allocation;
}

VkDeviceSize vulkan::VmaAllocatorImpl::getAllocationSize(const BufferAllocation& allocation) const
{
	if (nullptr == mAllocator || allocation.mBuffer == VK_NULL_HANDLE || nullptr == allocation.mAllocation)
		return 0;

	VmaAllocationInfo allocationInfo = {};
	vmaGetAllocationInfo(castAllocator(mAllocator), castAllocation(allocation.mAllocation), &allocationInfo);
	return allocationInfo.size;
}

#endif
