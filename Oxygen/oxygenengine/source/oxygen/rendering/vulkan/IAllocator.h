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
	struct BufferAllocation
	{
		VkBuffer mBuffer = VK_NULL_HANDLE;
		void* mMappedData = nullptr;
		VkDeviceSize mSize = 0;
		void* mAllocation = nullptr;
	};

	struct ImageAllocation
	{
		VkImage mImage = VK_NULL_HANDLE;
		void* mAllocation = nullptr;
	};

	class IAllocator
	{
	public:
		virtual ~IAllocator() = default;

		virtual bool startup(class VulkanDevice& device) = 0;
		virtual void shutdown(class VulkanDevice& device) = 0;

		virtual bool allocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, BufferAllocation& outAllocation) = 0;
		virtual bool allocateImage(const VkImageCreateInfo& createInfo, ImageAllocation& outAllocation) = 0;
		virtual void destroyBuffer(BufferAllocation& allocation) = 0;
		virtual void destroyImage(ImageAllocation& allocation) = 0;
		virtual void* map(BufferAllocation& allocation) = 0;
		virtual void unmap(BufferAllocation& allocation) = 0;
		virtual VkDeviceSize getAllocationSize(const BufferAllocation& allocation) const = 0;
	};
}

#endif
