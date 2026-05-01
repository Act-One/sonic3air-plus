/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen/rendering/vulkan/Frame.h"
#include "oxygen/rendering/vulkan/Texture.h"

#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)

namespace vulkan
{
	class Descriptors
	{
	public:
		struct MaterialKey
		{
			std::array<VkImageView, 5> mImageViews = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
			std::array<VkSampler, 5> mSamplers = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
			std::array<uint64_t, 5> mVersions = { 0, 0, 0, 0, 0 };

			inline bool operator==(const MaterialKey& other) const
			{
				return (mImageViews == other.mImageViews && mSamplers == other.mSamplers && mVersions == other.mVersions);
			}
		};

		struct MaterialKeyHasher
		{
			size_t operator()(const MaterialKey& key) const;
		};

	public:
		bool startup(class VulkanDevice& device, const std::array<Frame, VULKAN_FRAMES_IN_FLIGHT>& frames);
		void shutdown(class VulkanDevice& device);

		void updateFrameSet(class VulkanDevice& device, uint32_t frameIndex);
		VkDescriptorSet getFrameSet(uint32_t frameIndex) const;
		VkDescriptorSet getMaterialSet(class VulkanDevice& device, const MaterialKey& key);

		inline VkDescriptorSetLayout getFrameSetLayout() const		{ return mFrameSetLayout; }
		inline VkDescriptorSetLayout getMaterialSetLayout() const	{ return mMaterialSetLayout; }

	private:
		bool createFrameDescriptorPool(class VulkanDevice& device);
		bool createMaterialDescriptorPool(class VulkanDevice& device, VkDescriptorPool& outDescriptorPool);
		VkDescriptorSet allocateSet(class VulkanDevice& device, VkDescriptorPool descriptorPool, VkDescriptorSetLayout layout, VkResult* outResult = nullptr);

	private:
		VkDescriptorSetLayout mFrameSetLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout mMaterialSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool mFrameDescriptorPool = VK_NULL_HANDLE;
		std::array<VkDescriptorSet, VULKAN_FRAMES_IN_FLIGHT> mFrameSets = { VK_NULL_HANDLE, VK_NULL_HANDLE };
		std::unordered_map<MaterialKey, VkDescriptorSet, MaterialKeyHasher> mMaterialSetCache;
		std::vector<VkDescriptorPool> mMaterialDescriptorPools;
		const std::array<Frame, VULKAN_FRAMES_IN_FLIGHT>* mFrames = nullptr;
	};
}

#endif
