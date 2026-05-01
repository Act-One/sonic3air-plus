/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen/rendering/vulkan/IAllocator.h"

#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)

namespace vulkan
{
	struct PendingBufferRelease
	{
		BufferAllocation mBuffer;
	};

	class Frame
	{
	public:
		struct UniformSlice
		{
			void* mMappedData = nullptr;
			uint32_t mDynamicOffset = 0;
		};

	public:
		bool startup(class VulkanDevice& device, IAllocator& allocator, uint32_t uniformBufferSize);
		void shutdown(class VulkanDevice& device, IAllocator& allocator);

		bool begin(class VulkanDevice& device, IAllocator& allocator);
		void resetUploads(IAllocator& allocator);
		void end(class VulkanDevice& device);

		UniformSlice allocateUniform(size_t size, size_t alignment);
		void addPendingUpload(BufferAllocation&& allocation);

		inline VkCommandPool getCommandPool() const				{ return mCommandPool; }
		inline VkCommandBuffer getCommandBuffer() const			{ return mCommandBuffer; }
		inline VkFence getFence() const							{ return mFence; }
		inline VkSemaphore getImageAvailableSemaphore() const	{ return mImageAvailableSemaphore; }
		inline VkSemaphore getRenderCompleteSemaphore() const	{ return mRenderCompleteSemaphore; }
		inline const BufferAllocation& getGlobalsBuffer() const	{ return mGlobalsBuffer; }
		inline const BufferAllocation& getPerDrawBuffer() const	{ return mPerDrawBuffer; }
		inline void* getGlobalsMappedData() const				{ return mGlobalsBuffer.mMappedData; }
		inline bool isRecording() const							{ return mRecording; }

	private:
		VkCommandPool mCommandPool = VK_NULL_HANDLE;
		VkCommandBuffer mCommandBuffer = VK_NULL_HANDLE;
		VkFence mFence = VK_NULL_HANDLE;
		VkSemaphore mImageAvailableSemaphore = VK_NULL_HANDLE;
		VkSemaphore mRenderCompleteSemaphore = VK_NULL_HANDLE;
		BufferAllocation mGlobalsBuffer;
		BufferAllocation mPerDrawBuffer;
		size_t mPerDrawWriteOffset = 0;
		std::vector<PendingBufferRelease> mPendingUploads;
		bool mRecording = false;
	};
}

#endif
