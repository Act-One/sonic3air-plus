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

#include "oxygen/rendering/vulkan/Descriptors.h"
#include "oxygen/rendering/vulkan/VulkanDevice.h"

#include "oxygen/helper/Logging.h"


namespace
{
	size_t hashValue(size_t value, size_t seed)
	{
		return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
	}
}


size_t vulkan::Descriptors::MaterialKeyHasher::operator()(const MaterialKey& key) const
{
	size_t seed = 0;
	for (size_t index = 0; index < key.mImageViews.size(); ++index)
	{
		seed = hashValue(reinterpret_cast<size_t>(key.mImageViews[index]), seed);
		seed = hashValue(reinterpret_cast<size_t>(key.mSamplers[index]), seed);
		seed = hashValue((size_t)key.mVersions[index], seed);
	}
	return seed;
}

bool vulkan::Descriptors::startup(VulkanDevice& device, const std::array<Frame, VULKAN_FRAMES_IN_FLIGHT>& frames)
{
	shutdown(device);

	mFrames = &frames;

	std::array<VkDescriptorSetLayoutBinding, 2> frameBindings = {};
	frameBindings[0].binding = 0;
	frameBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	frameBindings[0].descriptorCount = 1;
	frameBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	frameBindings[1].binding = 1;
	frameBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	frameBindings[1].descriptorCount = 1;
	frameBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo frameLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	frameLayoutCreateInfo.bindingCount = (uint32_t)frameBindings.size();
	frameLayoutCreateInfo.pBindings = frameBindings.data();
	if (device.vk().CreateDescriptorSetLayout(device.getDevice(), &frameLayoutCreateInfo, nullptr, &mFrameSetLayout) != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkCreateDescriptorSetLayout failed for Vulkan frame descriptors");
		return false;
	}

	std::array<VkDescriptorSetLayoutBinding, 5> materialBindings = {};
	for (uint32_t index = 0; index < (uint32_t)materialBindings.size(); ++index)
	{
		materialBindings[index].binding = index;
		materialBindings[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		materialBindings[index].descriptorCount = 1;
		materialBindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	}

	VkDescriptorSetLayoutCreateInfo materialLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	materialLayoutCreateInfo.bindingCount = (uint32_t)materialBindings.size();
	materialLayoutCreateInfo.pBindings = materialBindings.data();
	if (device.vk().CreateDescriptorSetLayout(device.getDevice(), &materialLayoutCreateInfo, nullptr, &mMaterialSetLayout) != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkCreateDescriptorSetLayout failed for Vulkan material descriptors");
		return false;
	}

	if (!createFrameDescriptorPool(device))
		return false;

	VkDescriptorPool materialDescriptorPool = VK_NULL_HANDLE;
	if (!createMaterialDescriptorPool(device, materialDescriptorPool))
		return false;
	mMaterialDescriptorPools.push_back(materialDescriptorPool);

	for (uint32_t frameIndex = 0; frameIndex < VULKAN_FRAMES_IN_FLIGHT; ++frameIndex)
	{
		mFrameSets[frameIndex] = allocateSet(device, mFrameDescriptorPool, mFrameSetLayout);
		if (mFrameSets[frameIndex] == VK_NULL_HANDLE)
			return false;
		updateFrameSet(device, frameIndex);
	}

	return true;
}

void vulkan::Descriptors::shutdown(VulkanDevice& device)
{
	mMaterialSetCache.clear();
	mFrames = nullptr;

	for (VkDescriptorPool descriptorPool : mMaterialDescriptorPools)
	{
		if (descriptorPool != VK_NULL_HANDLE)
		{
			device.vk().DestroyDescriptorPool(device.getDevice(), descriptorPool, nullptr);
		}
	}
	mMaterialDescriptorPools.clear();

	if (mFrameDescriptorPool != VK_NULL_HANDLE)
	{
		device.vk().DestroyDescriptorPool(device.getDevice(), mFrameDescriptorPool, nullptr);
		mFrameDescriptorPool = VK_NULL_HANDLE;
	}
	if (mMaterialSetLayout != VK_NULL_HANDLE)
	{
		device.vk().DestroyDescriptorSetLayout(device.getDevice(), mMaterialSetLayout, nullptr);
		mMaterialSetLayout = VK_NULL_HANDLE;
	}
	if (mFrameSetLayout != VK_NULL_HANDLE)
	{
		device.vk().DestroyDescriptorSetLayout(device.getDevice(), mFrameSetLayout, nullptr);
		mFrameSetLayout = VK_NULL_HANDLE;
	}

	for (VkDescriptorSet& frameSet : mFrameSets)
	{
		frameSet = VK_NULL_HANDLE;
	}
}

void vulkan::Descriptors::updateFrameSet(VulkanDevice& device, uint32_t frameIndex)
{
	if (nullptr == mFrames || frameIndex >= VULKAN_FRAMES_IN_FLIGHT || mFrameSets[frameIndex] == VK_NULL_HANDLE)
		return;

	const Frame& frame = (*mFrames)[frameIndex];

	VkDescriptorBufferInfo globalsBufferInfo = {};
	globalsBufferInfo.buffer = frame.getGlobalsBuffer().mBuffer;
	globalsBufferInfo.offset = 0;
	globalsBufferInfo.range = frame.getGlobalsBuffer().mSize;

	VkDescriptorBufferInfo perDrawBufferInfo = {};
	perDrawBufferInfo.buffer = frame.getPerDrawBuffer().mBuffer;
	perDrawBufferInfo.offset = 0;
	perDrawBufferInfo.range = frame.getPerDrawBuffer().mSize;

	std::array<VkWriteDescriptorSet, 2> writes = {};
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet = mFrameSets[frameIndex];
	writes[0].dstBinding = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[0].pBufferInfo = &globalsBufferInfo;

	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet = mFrameSets[frameIndex];
	writes[1].dstBinding = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	writes[1].pBufferInfo = &perDrawBufferInfo;

	device.vk().UpdateDescriptorSets(device.getDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
}

VkDescriptorSet vulkan::Descriptors::getFrameSet(uint32_t frameIndex) const
{
	return (frameIndex < VULKAN_FRAMES_IN_FLIGHT) ? mFrameSets[frameIndex] : VK_NULL_HANDLE;
}

VkDescriptorSet vulkan::Descriptors::getMaterialSet(VulkanDevice& device, const MaterialKey& key)
{
	const auto it = mMaterialSetCache.find(key);
	if (it != mMaterialSetCache.end())
		return it->second;

	if (mMaterialDescriptorPools.empty())
	{
		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
		if (!createMaterialDescriptorPool(device, descriptorPool))
			return VK_NULL_HANDLE;
		mMaterialDescriptorPools.push_back(descriptorPool);
	}

	VkResult allocResult = VK_SUCCESS;
	VkDescriptorSet descriptorSet = allocateSet(device, mMaterialDescriptorPools.back(), mMaterialSetLayout, &allocResult);
	if (descriptorSet == VK_NULL_HANDLE)
	{
		if (allocResult != VK_ERROR_OUT_OF_POOL_MEMORY && allocResult != VK_ERROR_FRAGMENTED_POOL)
			return VK_NULL_HANDLE;

		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
		if (!createMaterialDescriptorPool(device, descriptorPool))
			return VK_NULL_HANDLE;
		mMaterialDescriptorPools.push_back(descriptorPool);
		descriptorSet = allocateSet(device, descriptorPool, mMaterialSetLayout, &allocResult);
	}
	if (descriptorSet == VK_NULL_HANDLE)
		return VK_NULL_HANDLE;

	std::array<VkDescriptorImageInfo, 5> imageInfos = {};
	std::array<VkWriteDescriptorSet, 5> writes = {};
	uint32_t writeCount = 0;

	for (uint32_t index = 0; index < (uint32_t)imageInfos.size(); ++index)
	{
		if (key.mImageViews[index] == VK_NULL_HANDLE || key.mSamplers[index] == VK_NULL_HANDLE)
			continue;

		imageInfos[index].sampler = key.mSamplers[index];
		imageInfos[index].imageView = key.mImageViews[index];
		imageInfos[index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet& write = writes[writeCount++];
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = descriptorSet;
		write.dstBinding = index;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfos[index];
	}

	if (writeCount > 0)
	{
		device.vk().UpdateDescriptorSets(device.getDevice(), writeCount, writes.data(), 0, nullptr);
	}

	mMaterialSetCache[key] = descriptorSet;
	return descriptorSet;
}

bool vulkan::Descriptors::createFrameDescriptorPool(VulkanDevice& device)
{
	std::array<VkDescriptorPoolSize, 2> poolSizes = {};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[0].descriptorCount = VULKAN_FRAMES_IN_FLIGHT;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	poolSizes[1].descriptorCount = VULKAN_FRAMES_IN_FLIGHT;

	VkDescriptorPoolCreateInfo poolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	poolCreateInfo.flags = 0;
	poolCreateInfo.maxSets = VULKAN_FRAMES_IN_FLIGHT;
	poolCreateInfo.poolSizeCount = (uint32_t)poolSizes.size();
	poolCreateInfo.pPoolSizes = poolSizes.data();
	if (device.vk().CreateDescriptorPool(device.getDevice(), &poolCreateInfo, nullptr, &mFrameDescriptorPool) != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkCreateDescriptorPool failed for Vulkan frame descriptors");
		return false;
	}
	return true;
}

bool vulkan::Descriptors::createMaterialDescriptorPool(VulkanDevice& device, VkDescriptorPool& outDescriptorPool)
{
	outDescriptorPool = VK_NULL_HANDLE;

	std::array<VkDescriptorPoolSize, 1> poolSizes = {};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[0].descriptorCount = 5 * 8192;

	VkDescriptorPoolCreateInfo poolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	poolCreateInfo.flags = 0;
	poolCreateInfo.maxSets = 8192;
	poolCreateInfo.poolSizeCount = (uint32_t)poolSizes.size();
	poolCreateInfo.pPoolSizes = poolSizes.data();
	if (device.vk().CreateDescriptorPool(device.getDevice(), &poolCreateInfo, nullptr, &outDescriptorPool) != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkCreateDescriptorPool failed for Vulkan material descriptors");
		return false;
	}
	return true;
}

VkDescriptorSet vulkan::Descriptors::allocateSet(VulkanDevice& device, VkDescriptorPool descriptorPool, VkDescriptorSetLayout layout, VkResult* outResult)
{
	if (nullptr != outResult)
		*outResult = VK_SUCCESS;

	if (descriptorPool == VK_NULL_HANDLE || layout == VK_NULL_HANDLE)
		return VK_NULL_HANDLE;

	VkDescriptorSetAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	allocateInfo.descriptorPool = descriptorPool;
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = &layout;

	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	const VkResult result = device.vk().AllocateDescriptorSets(device.getDevice(), &allocateInfo, &descriptorSet);
	if (nullptr != outResult)
		*outResult = result;
	if (result != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkAllocateDescriptorSets failed with result " << (int)result);
		return VK_NULL_HANDLE;
	}
	return descriptorSet;
}

#endif
