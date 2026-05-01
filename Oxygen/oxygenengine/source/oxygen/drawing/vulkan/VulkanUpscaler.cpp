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

#include "oxygen/drawing/vulkan/VulkanUpscaler.h"

#include "oxygen/application/Configuration.h"
#include "oxygen/drawing/vulkan/VulkanDrawerResources.h"
#include "oxygen/helper/FileHelper.h"
#include "oxygen/helper/Logging.h"
#include "oxygen/rendering/vulkan/Descriptors.h"
#include "oxygen/rendering/vulkan/Frame.h"
#include "oxygen/rendering/vulkan/Pipeline.h"
#include "oxygen/rendering/vulkan/VulkanDevice.h"


namespace
{
	inline std::wstring getShaderAssetPath(const wchar_t* filename)
	{
		std::wstring path = Configuration::instance().mEngineDataPath;
		rmx::FileIO::normalizePath(path, true);
		path += filename;
		return path;
	}

	void recordImageBarrier(vulkan::VulkanDevice& device, VkCommandBuffer commandBuffer, vulkan::Texture& texture, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags2 srcStageMask, VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask)
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
		imageBarrier.image = texture.getImage();
		imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBarrier.subresourceRange.baseMipLevel = 0;
		imageBarrier.subresourceRange.levelCount = 1;
		imageBarrier.subresourceRange.baseArrayLayer = 0;
		imageBarrier.subresourceRange.layerCount = 1;

		VkDependencyInfo dependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependencyInfo.imageMemoryBarrierCount = 1;
		dependencyInfo.pImageMemoryBarriers = &imageBarrier;
		device.vk().CmdPipelineBarrier2(commandBuffer, &dependencyInfo);
		texture.setLayout(newLayout);
	}

	void assignMaterialTexture(vulkan::Descriptors::MaterialKey& materialKey, uint32 index, const vulkan::Texture& texture, VkSampler sampler)
	{
		materialKey.mImageViews[index] = texture.getImageView();
		materialKey.mSamplers[index] = sampler;
		materialKey.mVersions[index] = texture.getDescriptorVersion();
	}
}


VulkanUpscaler::VulkanUpscaler(Type type, VulkanDrawerResources& resources) :
	mType(type),
	mResources(resources)
{
}

VulkanUpscaler::~VulkanUpscaler()
{
	shutdown();
}

bool VulkanUpscaler::startup()
{
	shutdown();

	vulkan::RendererBackend* backend = mResources.getBackend();
	if (nullptr == backend)
		return false;

	if (!createSampler(VK_FILTER_NEAREST, mPointClampSampler))
		return false;
	if (!createSampler(VK_FILTER_LINEAR, mLinearClampSampler))
		return false;

	const float vertexData[] =
	{
		0.0f, 0.0f,
		0.0f, 1.0f,
		1.0f, 1.0f,
		1.0f, 1.0f,
		1.0f, 0.0f,
		0.0f, 0.0f
	};
	if (!backend->getAllocator().allocateBuffer(sizeof(vertexData), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mFullscreenQuadVertexBuffer))
		return false;
	if (nullptr == mFullscreenQuadVertexBuffer.mMappedData)
		mFullscreenQuadVertexBuffer.mMappedData = backend->getAllocator().map(mFullscreenQuadVertexBuffer);
	if (nullptr == mFullscreenQuadVertexBuffer.mMappedData)
		return false;
	std::memcpy(mFullscreenQuadVertexBuffer.mMappedData, vertexData, sizeof(vertexData));

	if (mType == Type::HQX)
	{
		mLookupTextures.resize(3);
		mLookupTextures[0].mImagePath = getShaderAssetPath(L"data/shader/hq2x.png");
		mLookupTextures[1].mImagePath = getShaderAssetPath(L"data/shader/hq3x.png");
		mLookupTextures[2].mImagePath = getShaderAssetPath(L"data/shader/hq4x.png");
	}

	return true;
}

void VulkanUpscaler::shutdown()
{
	vulkan::RendererBackend* backend = mResources.getBackend();
	if (nullptr != backend)
	{
		for (LookupTexture& lookupTexture : mLookupTextures)
		{
			if (lookupTexture.mTexture.isValid())
				lookupTexture.mTexture.destroy(backend->getDevice(), backend->getAllocator());
		}
		if (mFullscreenQuadVertexBuffer.mBuffer != VK_NULL_HANDLE)
			backend->getAllocator().destroyBuffer(mFullscreenQuadVertexBuffer);
		if (mPointClampSampler != VK_NULL_HANDLE)
		{
			backend->getDevice().vk().DestroySampler(backend->getDevice().getDevice(), mPointClampSampler, nullptr);
			mPointClampSampler = VK_NULL_HANDLE;
		}
		if (mLinearClampSampler != VK_NULL_HANDLE)
		{
			backend->getDevice().vk().DestroySampler(backend->getDevice().getDevice(), mLinearClampSampler, nullptr);
			mLinearClampSampler = VK_NULL_HANDLE;
		}
	}
	mLookupTextures.clear();
	mFullscreenQuadVertexBuffer = {};
}

bool VulkanUpscaler::loadLookupTexture(LookupTexture& lookupTexture)
{
	if (lookupTexture.mInitialized)
		return lookupTexture.mTexture.isValid();

	vulkan::RendererBackend* backend = mResources.getBackend();
	if (nullptr == backend)
		return false;

	Bitmap bitmap;
	if (!FileHelper::loadBitmap(bitmap, lookupTexture.mImagePath))
	{
		RMX_ERROR("Failed to load upscaler texture " << WString(lookupTexture.mImagePath).toStdString(), return false);
	}

	lookupTexture.mInitialized = true;
	return lookupTexture.mTexture.updateFromBitmap(backend->getDevice(), backend->getAllocator(), nullptr, bitmap, VK_FORMAT_R8G8B8A8_UNORM);
}

bool VulkanUpscaler::createSampler(VkFilter filter, VkSampler& outSampler)
{
	vulkan::RendererBackend* backend = mResources.getBackend();
	if (nullptr == backend)
		return false;

	VkSamplerCreateInfo createInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	createInfo.magFilter = filter;
	createInfo.minFilter = filter;
	createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	createInfo.maxLod = 0.0f;
	return (backend->getDevice().vk().CreateSampler(backend->getDevice().getDevice(), &createInfo, nullptr, &outSampler) == VK_SUCCESS);
}

bool VulkanUpscaler::drawFullscreenPass(vulkan::PipelineID pipelineId, const UpscalerUniforms& uniforms, vulkan::Texture& mainTexture, VkSampler mainSampler, vulkan::Texture* auxTexture, VkSampler auxSampler)
{
	vulkan::RendererBackend* backend = mResources.getBackend();
	if (nullptr == backend)
		return false;

	return drawPassIntoTarget(pipelineId, Recti(0, 0, backend->getWindowTexture().getSize().x, backend->getWindowTexture().getSize().y), uniforms, mainTexture, mainSampler, auxTexture, auxSampler);
}

bool VulkanUpscaler::drawPassIntoTarget(vulkan::PipelineID pipelineId, const Recti& viewportRect, const UpscalerUniforms& uniforms, vulkan::Texture& mainTexture, VkSampler mainSampler, vulkan::Texture* auxTexture, VkSampler auxSampler)
{
	vulkan::RendererBackend* backend = mResources.getBackend();
	if (nullptr == backend || viewportRect.empty())
		return false;

	vulkan::Frame& frame = backend->getCurrentFrame();
	const auto uniformSlice = frame.allocateUniform(sizeof(UpscalerUniforms), backend->getDevice().getLimits().minUniformBufferOffsetAlignment);
	if (nullptr == uniformSlice.mMappedData)
		return false;
	std::memcpy(uniformSlice.mMappedData, &uniforms, sizeof(uniforms));

	vulkan::Descriptors::MaterialKey materialKey;
	assignMaterialTexture(materialKey, 0, mainTexture, mainSampler);
	if (nullptr != auxTexture)
	{
		assignMaterialTexture(materialKey, 1, *auxTexture, auxSampler);
	}

	VkDescriptorSet descriptorSets[2] =
	{
		backend->getDescriptors().getFrameSet(backend->getCurrentFrameIndex()),
		backend->getDescriptors().getMaterialSet(backend->getDevice(), materialKey)
	};
	if (descriptorSets[0] == VK_NULL_HANDLE || descriptorSets[1] == VK_NULL_HANDLE)
		return false;

	VkCommandBuffer commandBuffer = frame.getCommandBuffer();
	VkPipeline pipeline = backend->getPipelines().getPipeline(pipelineId);
	if (pipeline == VK_NULL_HANDLE)
		return false;

	VkViewport viewport = {};
	viewport.x = (float)viewportRect.x;
	viewport.y = (float)viewportRect.y;
	viewport.width = (float)viewportRect.width;
	viewport.height = (float)viewportRect.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	backend->getDevice().vk().CmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset = { viewportRect.x, viewportRect.y };
	scissor.extent = { (uint32_t)viewportRect.width, (uint32_t)viewportRect.height };
	backend->getDevice().vk().CmdSetScissor(commandBuffer, 0, 1, &scissor);

	backend->getDevice().vk().CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	backend->getDevice().vk().CmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, backend->getPipelines().getPipelineLayout(), 0, 2, descriptorSets, 1, &uniformSlice.mDynamicOffset);

	VkBuffer vertexBuffer = mFullscreenQuadVertexBuffer.mBuffer;
	const VkDeviceSize vertexOffset = 0;
	backend->getDevice().vk().CmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
	backend->getDevice().vk().CmdDraw(commandBuffer, 6, 1, 0, 0);
	return true;
}

bool VulkanUpscaler::renderImage(vulkan::Texture& sourceTexture, SDL_Window& window, bool useVSync)
{
	vulkan::RendererBackend* backend = mResources.getBackend();
	if (nullptr == backend || !sourceTexture.isValid())
		return false;

	const int filtering = Configuration::instance().mFiltering;
	const int scanlines = Configuration::instance().mScanlines;

	if (mType == Type::DEFAULT)
	{
		return backend->presentTexture(sourceTexture, window, useVSync);
	}

	vulkan::PipelineID pipelineId = vulkan::PipelineID::UPSCALE_SOFT;
	vulkan::PipelineID pass0PipelineId = vulkan::PipelineID::UPSCALE_XBRZ_PASS0;
	bool isMultiPass = false;
	vulkan::Texture* auxTexture = nullptr;
	VkSampler mainSampler = mPointClampSampler;
	VkSampler auxSampler = mPointClampSampler;

	UpscalerUniforms uniforms;
	uniforms.mGameResolution[0] = sourceTexture.getSize().x;
	uniforms.mGameResolution[1] = sourceTexture.getSize().y;

	if (!backend->beginFrame(window, useVSync))
		return false;

	uniforms.mOutputSize[0] = backend->getWindowTexture().getSize().x;
	uniforms.mOutputSize[1] = backend->getWindowTexture().getSize().y;

	switch (mType)
	{
		case Type::SOFT:
		{
			float pixelFactor = uniforms.mOutputSize[1] / (float)std::max(uniforms.mGameResolution[1], 1);
			pixelFactor *= (filtering == 1) ? 2.0f : 1.0f;
			uniforms.mPixelFactor = clamp(pixelFactor, 1.0f, 1000.0f);
			uniforms.mScanlinesIntensity = (scanlines > 0) ? ((float)scanlines * 0.25f) : 0.0f;
			mainSampler = mLinearClampSampler;
			break;
		}

		case Type::XBRZ:
			pipelineId = vulkan::PipelineID::UPSCALE_XBRZ_PASS1;
			isMultiPass = true;
			break;

		case Type::HQX:
		{
			const int lookupTextureIndex = clamp(filtering - 4, 0, 2);
			if (!loadLookupTexture(mLookupTextures[lookupTextureIndex]))
			{
				backend->endRenderingPass();
				backend->endFrame();
				return false;
			}
			auxTexture = &mLookupTextures[lookupTextureIndex].mTexture;
			switch (lookupTextureIndex)
			{
				default:
				case 0: pipelineId = vulkan::PipelineID::UPSCALE_HQ2X; break;
				case 1: pipelineId = vulkan::PipelineID::UPSCALE_HQ3X; break;
				case 2: pipelineId = vulkan::PipelineID::UPSCALE_HQ4X; break;
			}
			break;
		}

		default:
			break;
	}

	backend->endRenderingPass();

	vulkan::Texture* mainTexture = &sourceTexture;
	if (isMultiPass)
	{
		vulkan::Texture& pass0Texture = backend->getProcessingTexture();
		backend->beginColorPass(pass0Texture, VK_ATTACHMENT_LOAD_OP_CLEAR);
		if (!drawFullscreenPass(pass0PipelineId, uniforms, sourceTexture, mPointClampSampler, nullptr, VK_NULL_HANDLE))
			return false;
		backend->endRenderingPass();

		recordImageBarrier(backend->getDevice(), backend->getCurrentFrame().getCommandBuffer(), pass0Texture, pass0Texture.getLayout(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
		mainTexture = &pass0Texture;
		auxTexture = &sourceTexture;
	}

	backend->beginColorPass(backend->getWindowTexture(), VK_ATTACHMENT_LOAD_OP_CLEAR);
	if (!drawFullscreenPass(pipelineId, uniforms, *mainTexture, mainSampler, auxTexture, auxSampler))
		return false;

	backend->endFrame();
	backend->present();
	return true;
}

bool VulkanUpscaler::renderImage(const Recti& rect, const Recti& fullViewport, vulkan::Texture& sourceTexture, vulkan::Texture& targetTexture)
{
	vulkan::RendererBackend* backend = mResources.getBackend();
	if (nullptr == backend || !sourceTexture.isValid() || !targetTexture.isValid() || rect.empty())
		return false;

	const Recti viewportRect(fullViewport.x + rect.x, fullViewport.y + rect.y, rect.width, rect.height);
	if (viewportRect.empty())
		return false;

	const int filtering = Configuration::instance().mFiltering;
	const int scanlines = Configuration::instance().mScanlines;

	vulkan::PipelineID pipelineId = vulkan::PipelineID::UPSCALE_SOFT;
	vulkan::PipelineID pass0PipelineId = vulkan::PipelineID::UPSCALE_XBRZ_PASS0;
	bool isMultiPass = false;
	vulkan::Texture* auxTexture = nullptr;
	VkSampler mainSampler = mPointClampSampler;
	VkSampler auxSampler = mPointClampSampler;

	UpscalerUniforms uniforms;
	uniforms.mGameResolution[0] = sourceTexture.getSize().x;
	uniforms.mGameResolution[1] = sourceTexture.getSize().y;
	uniforms.mOutputSize[0] = rect.width;
	uniforms.mOutputSize[1] = rect.height;

	switch (mType)
	{
		case Type::DEFAULT:
			return false;

		case Type::SOFT:
		{
			float pixelFactor = rect.height / (float)std::max(sourceTexture.getSize().y, 1);
			pixelFactor *= (filtering == 1) ? 2.0f : 1.0f;
			uniforms.mPixelFactor = clamp(pixelFactor, 1.0f, 1000.0f);
			uniforms.mScanlinesIntensity = (scanlines > 0) ? ((float)scanlines * 0.25f) : 0.0f;
			mainSampler = mLinearClampSampler;
			break;
		}

		case Type::XBRZ:
			pipelineId = vulkan::PipelineID::UPSCALE_XBRZ_PASS1;
			isMultiPass = true;
			break;

		case Type::HQX:
		{
			const int lookupTextureIndex = clamp(filtering - 4, 0, 2);
			if (!loadLookupTexture(mLookupTextures[lookupTextureIndex]))
				return false;
			auxTexture = &mLookupTextures[lookupTextureIndex].mTexture;
			switch (lookupTextureIndex)
			{
				default:
				case 0: pipelineId = vulkan::PipelineID::UPSCALE_HQ2X; break;
				case 1: pipelineId = vulkan::PipelineID::UPSCALE_HQ3X; break;
				case 2: pipelineId = vulkan::PipelineID::UPSCALE_HQ4X; break;
			}
			break;
		}
	}

	backend->endRenderingPass();

	vulkan::Texture* mainTexture = &sourceTexture;
	if (isMultiPass)
	{
		vulkan::Texture& pass0Texture = backend->getProcessingTexture();
		backend->beginColorPass(pass0Texture, VK_ATTACHMENT_LOAD_OP_CLEAR);
		if (!drawPassIntoTarget(pass0PipelineId, Recti(0, 0, sourceTexture.getSize().x, sourceTexture.getSize().y), uniforms, sourceTexture, mPointClampSampler, nullptr, VK_NULL_HANDLE))
			return false;
		backend->endRenderingPass();

		recordImageBarrier(backend->getDevice(), backend->getCurrentFrame().getCommandBuffer(), pass0Texture, pass0Texture.getLayout(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
		mainTexture = &pass0Texture;
		auxTexture = &sourceTexture;
	}

	backend->beginColorPass(targetTexture, VK_ATTACHMENT_LOAD_OP_LOAD);
	const bool result = drawPassIntoTarget(pipelineId, viewportRect, uniforms, *mainTexture, mainSampler, auxTexture, auxSampler);
	backend->endRenderingPass();
	return result;
}

#endif
