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
	class VmaAllocatorImpl final : public IAllocator
	{
	public:
		bool startup(VulkanDevice& device) override;
		void shutdown(VulkanDevice& device) override;

		bool allocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, BufferAllocation& outAllocation) override;
		bool allocateImage(const VkImageCreateInfo& createInfo, ImageAllocation& outAllocation) override;
		void destroyBuffer(BufferAllocation& allocation) override;
		void destroyImage(ImageAllocation& allocation) override;
		void* map(BufferAllocation& allocation) override;
		void unmap(BufferAllocation& allocation) override;
		VkDeviceSize getAllocationSize(const BufferAllocation& allocation) const override;

	private:
		void* mAllocator = nullptr;
		VulkanDevice* mDevice = nullptr;
	};
}

#endif
