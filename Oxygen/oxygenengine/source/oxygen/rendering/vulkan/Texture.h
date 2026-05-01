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
	enum class SamplerKind : uint8_t
	{
		POINT_CLAMP = 0,
		POINT_WRAP,
		LINEAR_CLAMP,
		LINEAR_WRAP,
		COUNT
	};

	class Texture
	{
	public:
		Texture() = default;
		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;
		Texture(Texture&& other) noexcept;
		Texture& operator=(Texture&& other) noexcept;

		bool create(class VulkanDevice& device, IAllocator& allocator, VkFormat format, const Vec2i& size, VkImageUsageFlags usage, VkImageAspectFlags aspectFlags, bool sampled, bool renderTarget);
		void destroy(class VulkanDevice& device, IAllocator& allocator);
		bool updateFromBitmap(class VulkanDevice& device, IAllocator& allocator, class Frame* frame, const Bitmap& bitmap, VkFormat format);
		bool updateRaw(class VulkanDevice& device, IAllocator& allocator, class Frame* frame, const Vec2i& size, VkFormat format, const void* pixelData, size_t rowPitch, VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT);
		bool updateSubRectFromBitmap(class VulkanDevice& device, IAllocator& allocator, class Frame* frame, const Bitmap& bitmap, const Vec2i& targetPosition);
		bool updateSubRectRaw(class VulkanDevice& device, IAllocator& allocator, class Frame* frame, const Vec2i& targetPosition, const Vec2i& size, const void* pixelData, size_t rowPitch);
		bool readToBitmap(class VulkanDevice& device, IAllocator& allocator, Bitmap& outBitmap);

		inline bool isValid() const						{ return mImage.mImage != VK_NULL_HANDLE && mView != VK_NULL_HANDLE; }
		inline const Vec2i& getSize() const				{ return mSize; }
		inline VkFormat getFormat() const				{ return mFormat; }
		inline VkImage getImage() const					{ return mImage.mImage; }
		inline VkImageView getImageView() const			{ return mView; }
		inline VkImageLayout getLayout() const			{ return mCurrentLayout; }
		inline uint64_t getDescriptorVersion() const	{ return mDescriptorVersion; }
		inline bool isRenderTarget() const				{ return mRenderTarget; }
		inline bool isSampled() const					{ return mSampled; }
		inline void setLayout(VkImageLayout layout)		{ mCurrentLayout = layout; }

	private:
		ImageAllocation mImage;
		VkImageView mView = VK_NULL_HANDLE;
		Vec2i mSize;
		VkFormat mFormat = VK_FORMAT_UNDEFINED;
		VkImageAspectFlags mAspectFlags = 0;
		VkImageLayout mCurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		uint64_t mDescriptorVersion = 1;
		bool mSampled = false;
		bool mRenderTarget = false;
	};
}

#endif
