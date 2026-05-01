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

#include "oxygen/rendering/vulkan/Texture.h"
#include "oxygen/rendering/vulkan/Frame.h"
#include "oxygen/rendering/vulkan/VulkanDevice.h"

#include "oxygen/helper/Logging.h"


namespace
{
	uint32_t getBytesPerPixel(VkFormat format)
	{
		switch (format)
		{
			case VK_FORMAT_R8_UNORM:
			case VK_FORMAT_R8_UINT:
				return 1;

			case VK_FORMAT_R16_UINT:
			case VK_FORMAT_R16_SINT:
				return 2;

			case VK_FORMAT_R8G8B8A8_UNORM:
			case VK_FORMAT_R8G8B8A8_SRGB:
			case VK_FORMAT_B8G8R8A8_UNORM:
				return 4;

			default:
				return 0;
		}
	}

	void recordImageBarrier(vulkan::VulkanDevice& device, VkCommandBuffer commandBuffer, VkImage image, VkImageAspectFlags aspectFlags, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags2 srcStageMask, VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask)
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
		imageBarrier.subresourceRange.aspectMask = aspectFlags;
		imageBarrier.subresourceRange.baseMipLevel = 0;
		imageBarrier.subresourceRange.levelCount = 1;
		imageBarrier.subresourceRange.baseArrayLayer = 0;
		imageBarrier.subresourceRange.layerCount = 1;

		VkDependencyInfo dependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependencyInfo.imageMemoryBarrierCount = 1;
		dependencyInfo.pImageMemoryBarriers = &imageBarrier;
		device.vk().CmdPipelineBarrier2(commandBuffer, &dependencyInfo);
	}

	template<typename TRecorder>
	bool executeImmediate(vulkan::VulkanDevice& device, TRecorder&& recorder)
	{
		VkCommandPool commandPool = VK_NULL_HANDLE;
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		VkFence fence = VK_NULL_HANDLE;

		VkCommandPoolCreateInfo commandPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		commandPoolCreateInfo.queueFamilyIndex = device.getGraphicsQueueFamilyIndex();
		if (device.vk().CreateCommandPool(device.getDevice(), &commandPoolCreateInfo, nullptr, &commandPool) != VK_SUCCESS)
			return false;

		VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocateInfo.commandPool = commandPool;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 1;
		if (device.vk().AllocateCommandBuffers(device.getDevice(), &allocateInfo, &commandBuffer) != VK_SUCCESS)
		{
			device.vk().DestroyCommandPool(device.getDevice(), commandPool, nullptr);
			return false;
		}

		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (device.vk().BeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
		{
			device.vk().DestroyCommandPool(device.getDevice(), commandPool, nullptr);
			return false;
		}

		recorder(commandBuffer);

		if (device.vk().EndCommandBuffer(commandBuffer) != VK_SUCCESS)
		{
			device.vk().DestroyCommandPool(device.getDevice(), commandPool, nullptr);
			return false;
		}

		VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		if (device.vk().CreateFence(device.getDevice(), &fenceCreateInfo, nullptr, &fence) != VK_SUCCESS)
		{
			device.vk().DestroyCommandPool(device.getDevice(), commandPool, nullptr);
			return false;
		}

		VkCommandBufferSubmitInfo commandBufferSubmitInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
		commandBufferSubmitInfo.commandBuffer = commandBuffer;

		VkSubmitInfo2 submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
		submitInfo.commandBufferInfoCount = 1;
		submitInfo.pCommandBufferInfos = &commandBufferSubmitInfo;

		const VkResult submitResult = device.vk().QueueSubmit2(device.getGraphicsQueue(), 1, &submitInfo, fence);
		if (submitResult == VK_SUCCESS)
		{
			device.vk().WaitForFences(device.getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
		}

		device.vk().DestroyFence(device.getDevice(), fence, nullptr);
		device.vk().DestroyCommandPool(device.getDevice(), commandPool, nullptr);
		return (submitResult == VK_SUCCESS);
	}

	bool uploadPixelDataRegion(vulkan::Texture& texture, vulkan::VulkanDevice& device, vulkan::IAllocator& allocator, vulkan::Frame* frame, const void* pixelData, size_t rowPitch, const Vec2i& targetPosition, const Vec2i& uploadSizeInPixels)
	{
		if (!texture.isValid() || nullptr == pixelData)
			return false;
		if (uploadSizeInPixels.x <= 0 || uploadSizeInPixels.y <= 0)
			return false;
		if (targetPosition.x < 0 || targetPosition.y < 0 || targetPosition.x + uploadSizeInPixels.x > texture.getSize().x || targetPosition.y + uploadSizeInPixels.y > texture.getSize().y)
			return false;

		const uint32_t bytesPerPixel = getBytesPerPixel(texture.getFormat());
		if (bytesPerPixel == 0)
		{
			RMX_LOG_ERROR("Unsupported Vulkan texture upload format " << (int)texture.getFormat());
			return false;
		}

		const size_t tightPitch = (size_t)uploadSizeInPixels.x * bytesPerPixel;
		const VkDeviceSize uploadSize = (VkDeviceSize)std::max<size_t>(rowPitch, tightPitch) * (VkDeviceSize)uploadSizeInPixels.y;

		vulkan::BufferAllocation stagingBuffer;
		if (!allocator.allocateBuffer(uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, stagingBuffer))
			return false;

		if (nullptr == stagingBuffer.mMappedData)
			stagingBuffer.mMappedData = allocator.map(stagingBuffer);
		if (nullptr == stagingBuffer.mMappedData)
		{
			allocator.destroyBuffer(stagingBuffer);
			return false;
		}

		uint8* dst = static_cast<uint8*>(stagingBuffer.mMappedData);
		const uint8* src = static_cast<const uint8*>(pixelData);
		for (int y = 0; y < uploadSizeInPixels.y; ++y)
		{
			std::memcpy(dst + y * tightPitch, src + y * rowPitch, tightPitch);
		}

		const auto recordCopy = [&](VkCommandBuffer commandBuffer)
		{
			recordImageBarrier(device, commandBuffer, texture.getImage(), VK_IMAGE_ASPECT_COLOR_BIT, texture.getLayout(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				(texture.getLayout() == VK_IMAGE_LAYOUT_UNDEFINED) ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_2_COPY_BIT,
				0,
				VK_ACCESS_2_TRANSFER_WRITE_BIT);

			VkBufferImageCopy copyRegion = {};
			copyRegion.bufferOffset = 0;
			copyRegion.bufferRowLength = 0;
			copyRegion.bufferImageHeight = 0;
			copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copyRegion.imageSubresource.mipLevel = 0;
			copyRegion.imageSubresource.baseArrayLayer = 0;
			copyRegion.imageSubresource.layerCount = 1;
			copyRegion.imageOffset = { targetPosition.x, targetPosition.y, 0 };
			copyRegion.imageExtent = { (uint32_t)uploadSizeInPixels.x, (uint32_t)uploadSizeInPixels.y, 1 };
			device.vk().CmdCopyBufferToImage(commandBuffer, stagingBuffer.mBuffer, texture.getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

			recordImageBarrier(device, commandBuffer, texture.getImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_2_COPY_BIT,
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
		};

		if (nullptr != frame && frame->isRecording())
		{
			recordCopy(frame->getCommandBuffer());
			frame->addPendingUpload(std::move(stagingBuffer));
			texture.setLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			return true;
		}

		const bool result = executeImmediate(device, recordCopy);
		allocator.destroyBuffer(stagingBuffer);
		if (result)
			texture.setLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		return result;
	}

	bool uploadPixelData(vulkan::Texture& texture, vulkan::VulkanDevice& device, vulkan::IAllocator& allocator, vulkan::Frame* frame, const void* pixelData, size_t rowPitch)
	{
		return uploadPixelDataRegion(texture, device, allocator, frame, pixelData, rowPitch, Vec2i(), texture.getSize());
	}
}


vulkan::Texture::Texture(Texture&& other) noexcept
{
	*this = std::move(other);
}

vulkan::Texture& vulkan::Texture::operator=(Texture&& other) noexcept
{
	if (this == &other)
		return *this;

	mImage = other.mImage;
	mView = other.mView;
	mSize = other.mSize;
	mFormat = other.mFormat;
	mAspectFlags = other.mAspectFlags;
	mCurrentLayout = other.mCurrentLayout;
	mDescriptorVersion = other.mDescriptorVersion;
	mSampled = other.mSampled;
	mRenderTarget = other.mRenderTarget;

	other.mImage = {};
	other.mView = VK_NULL_HANDLE;
	other.mSize = Vec2i();
	other.mFormat = VK_FORMAT_UNDEFINED;
	other.mAspectFlags = 0;
	other.mCurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	other.mDescriptorVersion = 1;
	other.mSampled = false;
	other.mRenderTarget = false;
	return *this;
}


bool vulkan::Texture::create(VulkanDevice& device, IAllocator& allocator, VkFormat format, const Vec2i& size, VkImageUsageFlags usage, VkImageAspectFlags aspectFlags, bool sampled, bool renderTarget)
{
	destroy(device, allocator);

	mSize = size;
	mFormat = format;
	mAspectFlags = aspectFlags;
	mSampled = sampled;
	mRenderTarget = renderTarget;

	VkImageCreateInfo createInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	createInfo.imageType = VK_IMAGE_TYPE_2D;
	createInfo.format = format;
	createInfo.extent.width = (uint32_t)std::max(size.x, 1);
	createInfo.extent.height = (uint32_t)std::max(size.y, 1);
	createInfo.extent.depth = 1;
	createInfo.mipLevels = 1;
	createInfo.arrayLayers = 1;
	createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	createInfo.usage = usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	if (!allocator.allocateImage(createInfo, mImage))
		return false;

	VkImageViewCreateInfo viewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewCreateInfo.image = mImage.mImage;
	viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewCreateInfo.format = format;
	viewCreateInfo.subresourceRange.aspectMask = aspectFlags;
	viewCreateInfo.subresourceRange.baseMipLevel = 0;
	viewCreateInfo.subresourceRange.levelCount = 1;
	viewCreateInfo.subresourceRange.baseArrayLayer = 0;
	viewCreateInfo.subresourceRange.layerCount = 1;
	if (device.vk().CreateImageView(device.getDevice(), &viewCreateInfo, nullptr, &mView) != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkCreateImageView failed");
		destroy(device, allocator);
		return false;
	}

	mCurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	++mDescriptorVersion;
	return true;
}

void vulkan::Texture::destroy(VulkanDevice& device, IAllocator& allocator)
{
	const bool hadResources = (mView != VK_NULL_HANDLE || mImage.mImage != VK_NULL_HANDLE);
	if (mView != VK_NULL_HANDLE)
	{
		device.vk().DestroyImageView(device.getDevice(), mView, nullptr);
		mView = VK_NULL_HANDLE;
	}
	allocator.destroyImage(mImage);
	mSize = Vec2i();
	mFormat = VK_FORMAT_UNDEFINED;
	mAspectFlags = 0;
	mCurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	mSampled = false;
	mRenderTarget = false;
	if (hadResources)
		++mDescriptorVersion;
}

bool vulkan::Texture::updateFromBitmap(VulkanDevice& device, IAllocator& allocator, Frame* frame, const Bitmap& bitmap, VkFormat format)
{
	if (bitmap.empty())
		return false;

	return updateRaw(device, allocator, frame, bitmap.getSize(), format, bitmap.getData(), bitmap.getWidth() * sizeof(uint32), VK_IMAGE_USAGE_SAMPLED_BIT);
}

bool vulkan::Texture::updateRaw(VulkanDevice& device, IAllocator& allocator, Frame* frame, const Vec2i& size, VkFormat format, const void* pixelData, size_t rowPitch, VkImageUsageFlags usage)
{
	if (nullptr == pixelData || size.x <= 0 || size.y <= 0)
		return false;

	if (!isValid() || mSize != size || mFormat != format)
	{
		if (!create(device, allocator, format, size, usage, VK_IMAGE_ASPECT_COLOR_BIT, true, false))
			return false;
	}

	return uploadPixelData(*this, device, allocator, frame, pixelData, rowPitch);
}

bool vulkan::Texture::updateSubRectFromBitmap(VulkanDevice& device, IAllocator& allocator, Frame* frame, const Bitmap& bitmap, const Vec2i& targetPosition)
{
	if (bitmap.empty())
		return false;

	return updateSubRectRaw(device, allocator, frame, targetPosition, bitmap.getSize(), bitmap.getData(), bitmap.getWidth() * sizeof(uint32));
}

bool vulkan::Texture::updateSubRectRaw(VulkanDevice& device, IAllocator& allocator, Frame* frame, const Vec2i& targetPosition, const Vec2i& size, const void* pixelData, size_t rowPitch)
{
	if (!isValid())
		return false;

	return uploadPixelDataRegion(*this, device, allocator, frame, pixelData, rowPitch, targetPosition, size);
}

bool vulkan::Texture::readToBitmap(VulkanDevice& device, IAllocator& allocator, Bitmap& outBitmap)
{
	if (!isValid())
		return false;

	const uint32_t bytesPerPixel = getBytesPerPixel(mFormat);
	if (bytesPerPixel == 0)
		return false;

	const VkDeviceSize bufferSize = (VkDeviceSize)std::max(mSize.x, 1) * (VkDeviceSize)std::max(mSize.y, 1) * bytesPerPixel;

	BufferAllocation stagingBuffer;
	if (!allocator.allocateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, stagingBuffer))
		return false;

	const VkImageLayout previousLayout = mCurrentLayout;
	const bool success = executeImmediate(device,
		[&](VkCommandBuffer commandBuffer)
		{
			recordImageBarrier(device, commandBuffer, mImage.mImage, mAspectFlags, previousLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				(previousLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_2_COPY_BIT,
				0,
				VK_ACCESS_2_TRANSFER_READ_BIT);

			VkBufferImageCopy copyRegion = {};
			copyRegion.imageSubresource.aspectMask = mAspectFlags;
			copyRegion.imageSubresource.mipLevel = 0;
			copyRegion.imageSubresource.baseArrayLayer = 0;
			copyRegion.imageSubresource.layerCount = 1;
			copyRegion.imageExtent = { (uint32_t)mSize.x, (uint32_t)mSize.y, 1 };
			device.vk().CmdCopyImageToBuffer(commandBuffer, mImage.mImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer.mBuffer, 1, &copyRegion);

			recordImageBarrier(device, commandBuffer, mImage.mImage, mAspectFlags, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, previousLayout,
				VK_PIPELINE_STAGE_2_COPY_BIT,
				VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
				VK_ACCESS_2_TRANSFER_READ_BIT,
				0);
		});

	if (!success)
	{
		allocator.destroyBuffer(stagingBuffer);
		return false;
	}

	outBitmap.create(mSize.x, mSize.y);
	if (nullptr == stagingBuffer.mMappedData)
		stagingBuffer.mMappedData = allocator.map(stagingBuffer);
	if (nullptr == stagingBuffer.mMappedData)
	{
		allocator.destroyBuffer(stagingBuffer);
		return false;
	}

	const uint8* src = static_cast<const uint8*>(stagingBuffer.mMappedData);
	if (mFormat == VK_FORMAT_R8G8B8A8_UNORM || mFormat == VK_FORMAT_R8G8B8A8_SRGB)
	{
		std::memcpy(outBitmap.getData(), src, (size_t)mSize.x * (size_t)mSize.y * sizeof(uint32));
	}
	else if (mFormat == VK_FORMAT_B8G8R8A8_UNORM)
	{
		for (int y = 0; y < mSize.y; ++y)
		{
			const uint32* row = reinterpret_cast<const uint32*>(src + (size_t)y * (size_t)mSize.x * sizeof(uint32));
			uint32* dst = outBitmap.getPixelPointer(0, y);
			for (int x = 0; x < mSize.x; ++x)
			{
				const uint32 color = row[x];
				dst[x] = (color & 0xff00ff00u) | ((color & 0x00ff0000u) >> 16) | ((color & 0x000000ffu) << 16);
			}
		}
	}
	else
	{
		allocator.destroyBuffer(stagingBuffer);
		return false;
	}

	allocator.destroyBuffer(stagingBuffer);
	return true;
}

#endif
