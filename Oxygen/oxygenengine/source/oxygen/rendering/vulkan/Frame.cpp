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

#include "oxygen/rendering/vulkan/Frame.h"
#include "oxygen/rendering/vulkan/VulkanDevice.h"

#include "oxygen/helper/Logging.h"


namespace
{
	static constexpr VkDeviceSize GLOBALS_BUFFER_SIZE = 4096;
}


bool vulkan::Frame::startup(VulkanDevice& device, IAllocator& allocator, uint32_t uniformBufferSize)
{
	shutdown(device, allocator);

	VkCommandPoolCreateInfo commandPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	commandPoolCreateInfo.queueFamilyIndex = device.getGraphicsQueueFamilyIndex();
	if (device.vk().CreateCommandPool(device.getDevice(), &commandPoolCreateInfo, nullptr, &mCommandPool) != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkCreateCommandPool failed");
		return false;
	}

	VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	commandBufferAllocateInfo.commandPool = mCommandPool;
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocateInfo.commandBufferCount = 1;
	if (device.vk().AllocateCommandBuffers(device.getDevice(), &commandBufferAllocateInfo, &mCommandBuffer) != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkAllocateCommandBuffers failed");
		return false;
	}

	VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	if (device.vk().CreateFence(device.getDevice(), &fenceCreateInfo, nullptr, &mFence) != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkCreateFence failed");
		return false;
	}

	VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	if (device.vk().CreateSemaphore(device.getDevice(), &semaphoreCreateInfo, nullptr, &mImageAvailableSemaphore) != VK_SUCCESS ||
		device.vk().CreateSemaphore(device.getDevice(), &semaphoreCreateInfo, nullptr, &mRenderCompleteSemaphore) != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkCreateSemaphore failed");
		return false;
	}

	if (!allocator.allocateBuffer(GLOBALS_BUFFER_SIZE, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, mGlobalsBuffer))
		return false;
	if (!allocator.allocateBuffer(std::max<uint32_t>(uniformBufferSize, 1024u * 1024u), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, mPerDrawBuffer))
		return false;

	mPerDrawWriteOffset = 0;
	mRecording = false;
	return true;
}

void vulkan::Frame::shutdown(VulkanDevice& device, IAllocator& allocator)
{
	if (mRecording && mCommandBuffer != VK_NULL_HANDLE)
	{
		device.vk().EndCommandBuffer(mCommandBuffer);
		mRecording = false;
	}

	resetUploads(allocator);
	allocator.destroyBuffer(mPerDrawBuffer);
	allocator.destroyBuffer(mGlobalsBuffer);

	if (mRenderCompleteSemaphore != VK_NULL_HANDLE)
	{
		device.vk().DestroySemaphore(device.getDevice(), mRenderCompleteSemaphore, nullptr);
		mRenderCompleteSemaphore = VK_NULL_HANDLE;
	}
	if (mImageAvailableSemaphore != VK_NULL_HANDLE)
	{
		device.vk().DestroySemaphore(device.getDevice(), mImageAvailableSemaphore, nullptr);
		mImageAvailableSemaphore = VK_NULL_HANDLE;
	}
	if (mFence != VK_NULL_HANDLE)
	{
		device.vk().DestroyFence(device.getDevice(), mFence, nullptr);
		mFence = VK_NULL_HANDLE;
	}
	if (mCommandPool != VK_NULL_HANDLE)
	{
		device.vk().DestroyCommandPool(device.getDevice(), mCommandPool, nullptr);
		mCommandPool = VK_NULL_HANDLE;
		mCommandBuffer = VK_NULL_HANDLE;
	}

	mPerDrawWriteOffset = 0;
}

bool vulkan::Frame::begin(VulkanDevice& device, IAllocator& allocator)
{
	if (mFence == VK_NULL_HANDLE || mCommandPool == VK_NULL_HANDLE || mCommandBuffer == VK_NULL_HANDLE)
		return false;

	const VkResult waitResult = device.vk().WaitForFences(device.getDevice(), 1, &mFence, VK_TRUE, UINT64_MAX);
	if (waitResult != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkWaitForFences failed with result " << (int)waitResult);
		return false;
	}

	resetUploads(allocator);
	device.vk().ResetFences(device.getDevice(), 1, &mFence);
	device.vk().ResetCommandPool(device.getDevice(), mCommandPool, 0);
	mPerDrawWriteOffset = 0;

	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	const VkResult beginResult = device.vk().BeginCommandBuffer(mCommandBuffer, &beginInfo);
	if (beginResult != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkBeginCommandBuffer failed with result " << (int)beginResult);
		return false;
	}

	mRecording = true;
	return true;
}

void vulkan::Frame::resetUploads(IAllocator& allocator)
{
	for (PendingBufferRelease& pendingRelease : mPendingUploads)
	{
		allocator.destroyBuffer(pendingRelease.mBuffer);
	}
	mPendingUploads.clear();
}

void vulkan::Frame::end(VulkanDevice& device)
{
	if (!mRecording || mCommandBuffer == VK_NULL_HANDLE)
		return;

	const VkResult result = device.vk().EndCommandBuffer(mCommandBuffer);
	if (result != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkEndCommandBuffer failed with result " << (int)result);
	}
	mRecording = false;
}

vulkan::Frame::UniformSlice vulkan::Frame::allocateUniform(size_t size, size_t alignment)
{
	UniformSlice uniformSlice;
	if (mPerDrawBuffer.mBuffer == VK_NULL_HANDLE || nullptr == mPerDrawBuffer.mMappedData)
		return uniformSlice;

	const size_t alignedOffset = vulkan::alignUp(mPerDrawWriteOffset, alignment);
	if (alignedOffset + size > (size_t)mPerDrawBuffer.mSize)
	{
		RMX_LOG_WARNING("Vulkan per-draw uniform buffer ran out of space");
		return uniformSlice;
	}

	uniformSlice.mMappedData = static_cast<uint8*>(mPerDrawBuffer.mMappedData) + alignedOffset;
	uniformSlice.mDynamicOffset = (uint32_t)alignedOffset;
	mPerDrawWriteOffset = alignedOffset + size;
	return uniformSlice;
}

void vulkan::Frame::addPendingUpload(BufferAllocation&& allocation)
{
	if (allocation.mBuffer != VK_NULL_HANDLE)
	{
		PendingBufferRelease& pendingRelease = vectorAdd(mPendingUploads);
		pendingRelease.mBuffer = allocation;
		allocation = {};
	}
}

#endif
