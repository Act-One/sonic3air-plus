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

#include "oxygen/drawing/vulkan/VulkanDrawer.h"

#include "oxygen/application/Configuration.h"
#include "oxygen/drawing/DrawCollection.h"
#include "oxygen/drawing/DrawCommand.h"
#include "oxygen/drawing/software/SoftwareDrawer.h"
#include "oxygen/drawing/software/SoftwareDrawerTexture.h"
#include "oxygen/drawing/vulkan/VulkanDrawerResources.h"
#include "oxygen/drawing/vulkan/VulkanDrawerTexture.h"
#include "oxygen/helper/Logging.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/rendering/vulkan/Frame.h"
#include "oxygen/rendering/vulkan/Texture.h"
#include "oxygen/rendering/vulkan/VulkanDevice.h"
#include "oxygen/rendering/vulkan/VulkanRenderResources.h"
#include "oxygen/resources/PaletteCollection.h"
#include "oxygen/resources/SpriteCollection.h"

#include <deque>


namespace
{
	enum class SamplerIndex : uint8
	{
		POINT_CLAMP = 0,
		POINT_WRAP,
		LINEAR_CLAMP,
		LINEAR_WRAP,
		COUNT
	};

	int nextPowerOfTwo(int value)
	{
		int result = 1;
		while (result < value)
			result <<= 1;
		return result;
	}

	struct SimpleRectConstants
	{
		Vec4f mTransform = Vec4f(0.0f, 0.0f, 0.0f, 0.0f);
		Vec4f mTintColor = Color::WHITE;
		Vec4f mAddedColor = Color::TRANSPARENT;
		int32 mTextureSize[2] = { 0, 0 };
		int32 mAlphaTest = 0;
		int32 mShadowHighlightMode = 0;
	};

	inline bool isHardwareDrawerEnabled()
	{
		return (Configuration::instance().mRenderMethod == Configuration::RenderMethod::VULKAN_FULL);
	}

	inline bool useAlphaTest(BlendMode blendMode)
	{
		return (blendMode == BlendMode::ALPHA);
	}

	inline VkPipelineStageFlags2 getSourceStageMaskForLayout(VkImageLayout layout)
	{
		switch (layout)
		{
			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:		return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:			return VK_PIPELINE_STAGE_2_COPY_BIT;
			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:			return VK_PIPELINE_STAGE_2_COPY_BIT;
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:		return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			case VK_IMAGE_LAYOUT_UNDEFINED:						return VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
			default:											return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		}
	}

	inline VkAccessFlags2 getSourceAccessMaskForLayout(VkImageLayout layout)
	{
		switch (layout)
		{
			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:		return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:			return VK_ACCESS_2_TRANSFER_WRITE_BIT;
			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:			return VK_ACCESS_2_TRANSFER_READ_BIT;
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:		return VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
			default:											return 0;
		}
	}

	void transitionTextureForSampling(vulkan::VulkanDevice& device, VkCommandBuffer commandBuffer, vulkan::Texture& texture)
	{
		if (!texture.isValid() || texture.getLayout() == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			return;

		VkImageMemoryBarrier2 imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		imageBarrier.srcStageMask = getSourceStageMaskForLayout(texture.getLayout());
		imageBarrier.srcAccessMask = getSourceAccessMaskForLayout(texture.getLayout());
		imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		imageBarrier.oldLayout = texture.getLayout();
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
		texture.setLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	vulkan::PipelineID getTexturedPipelineId(bool useCustomUVs, BlendMode blendMode)
	{
		switch (blendMode)
		{
			case BlendMode::OPAQUE:
				return useCustomUVs ? vulkan::PipelineID::SIMPLE_TEXTURED_UV_OPAQUE : vulkan::PipelineID::SIMPLE_TEXTURED_OPAQUE;

			case BlendMode::ADDITIVE:
				return useCustomUVs ? vulkan::PipelineID::SIMPLE_TEXTURED_UV_ADDITIVE : vulkan::PipelineID::SIMPLE_TEXTURED_ADDITIVE;

			case BlendMode::SUBTRACTIVE:
				return useCustomUVs ? vulkan::PipelineID::SIMPLE_TEXTURED_UV_SUBTRACTIVE : vulkan::PipelineID::SIMPLE_TEXTURED_SUBTRACTIVE;

			case BlendMode::MULTIPLICATIVE:
				return useCustomUVs ? vulkan::PipelineID::SIMPLE_TEXTURED_UV_MULTIPLICATIVE : vulkan::PipelineID::SIMPLE_TEXTURED_MULTIPLICATIVE;

			case BlendMode::MINIMUM:
				return useCustomUVs ? vulkan::PipelineID::SIMPLE_TEXTURED_UV_MINIMUM : vulkan::PipelineID::SIMPLE_TEXTURED_MINIMUM;

			case BlendMode::MAXIMUM:
				return useCustomUVs ? vulkan::PipelineID::SIMPLE_TEXTURED_UV_MAXIMUM : vulkan::PipelineID::SIMPLE_TEXTURED_MAXIMUM;

			case BlendMode::ONE_BIT:
			case BlendMode::ALPHA:
			default:
				return useCustomUVs ? vulkan::PipelineID::SIMPLE_TEXTURED_UV_ALPHA : vulkan::PipelineID::SIMPLE_TEXTURED_ALPHA;
		}
	}

	vulkan::PipelineID getColoredPipelineId(BlendMode blendMode)
	{
		switch (blendMode)
		{
			case BlendMode::OPAQUE:			return vulkan::PipelineID::SIMPLE_COLORED_OPAQUE;
			case BlendMode::ADDITIVE:		return vulkan::PipelineID::SIMPLE_COLORED_ADDITIVE;
			case BlendMode::SUBTRACTIVE:	return vulkan::PipelineID::SIMPLE_COLORED_SUBTRACTIVE;
			case BlendMode::MULTIPLICATIVE: return vulkan::PipelineID::SIMPLE_COLORED_MULTIPLICATIVE;
			case BlendMode::MINIMUM:		return vulkan::PipelineID::SIMPLE_COLORED_MINIMUM;
			case BlendMode::MAXIMUM:		return vulkan::PipelineID::SIMPLE_COLORED_MAXIMUM;
			case BlendMode::ONE_BIT:
			case BlendMode::ALPHA:
			default:						return vulkan::PipelineID::SIMPLE_COLORED;
		}
	}

	vulkan::PipelineID getIndexedPipelineId(BlendMode blendMode)
	{
		switch (blendMode)
		{
			case BlendMode::OPAQUE:			return vulkan::PipelineID::SIMPLE_INDEXED_OPAQUE;
			case BlendMode::ADDITIVE:		return vulkan::PipelineID::SIMPLE_INDEXED_ADDITIVE;
			case BlendMode::SUBTRACTIVE:	return vulkan::PipelineID::SIMPLE_INDEXED_SUBTRACTIVE;
			case BlendMode::MULTIPLICATIVE: return vulkan::PipelineID::SIMPLE_INDEXED_MULTIPLICATIVE;
			case BlendMode::MINIMUM:		return vulkan::PipelineID::SIMPLE_INDEXED_MINIMUM;
			case BlendMode::MAXIMUM:		return vulkan::PipelineID::SIMPLE_INDEXED_MAXIMUM;
			case BlendMode::ONE_BIT:
			case BlendMode::ALPHA:
			default:						return vulkan::PipelineID::SIMPLE_INDEXED_ALPHA;
		}
	}

	vulkan::PipelineID getVertexColorPipelineId(BlendMode blendMode)
	{
		switch (blendMode)
		{
			case BlendMode::OPAQUE:			return vulkan::PipelineID::SIMPLE_VERTEX_COLOR_OPAQUE;
			case BlendMode::ADDITIVE:		return vulkan::PipelineID::SIMPLE_VERTEX_COLOR_ADDITIVE;
			case BlendMode::SUBTRACTIVE:	return vulkan::PipelineID::SIMPLE_VERTEX_COLOR_SUBTRACTIVE;
			case BlendMode::MULTIPLICATIVE: return vulkan::PipelineID::SIMPLE_VERTEX_COLOR_MULTIPLICATIVE;
			case BlendMode::MINIMUM:		return vulkan::PipelineID::SIMPLE_VERTEX_COLOR_MINIMUM;
			case BlendMode::MAXIMUM:		return vulkan::PipelineID::SIMPLE_VERTEX_COLOR_MAXIMUM;
			case BlendMode::ONE_BIT:
			case BlendMode::ALPHA:
			default:						return vulkan::PipelineID::SIMPLE_VERTEX_COLOR_ALPHA;
		}
	}

	void assignMaterialTexture(vulkan::Descriptors::MaterialKey& materialKey, uint32 index, const vulkan::Texture& texture, VkSampler sampler)
	{
		materialKey.mImageViews[index] = texture.getImageView();
		materialKey.mSamplers[index] = sampler;
		materialKey.mVersions[index] = texture.getDescriptorVersion();
	}
}


namespace vulkandrawer
{
	struct Internal
	{
		Internal() :
			mRenderResources(RenderParts::instance(), mResources)
		{
			mSetupSuccessful = mResources.startup();
		}

		~Internal()
		{
			cleanupGpuResources();
		}

		void cleanupGpuResources()
		{
			vulkan::RendererBackend* backend = mResources.getBackend();
			if (nullptr != backend)
			{
				mRenderResources.clearAllCaches();

				for (auto& frameAtlases : mTempTextAtlases)
				{
					for (TempTextAtlas& atlas : frameAtlases)
					{
						if (atlas.mTexture.isValid())
						{
							atlas.mTexture.destroy(backend->getDevice(), backend->getAllocator());
						}
					}
					frameAtlases.clear();
				}
				if (mQuadVertexBuffer.mBuffer != VK_NULL_HANDLE)
					backend->getAllocator().destroyBuffer(mQuadVertexBuffer);
				if (mTexturedQuadVertexBuffer.mBuffer != VK_NULL_HANDLE)
					backend->getAllocator().destroyBuffer(mTexturedQuadVertexBuffer);
				if (mDynamicTexturedVertexBuffer.mBuffer != VK_NULL_HANDLE)
					backend->getAllocator().destroyBuffer(mDynamicTexturedVertexBuffer);
				if (mDynamicVertexColorBuffer.mBuffer != VK_NULL_HANDLE)
					backend->getAllocator().destroyBuffer(mDynamicVertexColorBuffer);

				for (VkSampler& sampler : mSamplers)
				{
					if (sampler != VK_NULL_HANDLE)
					{
						backend->getDevice().vk().DestroySampler(backend->getDevice().getDevice(), sampler, nullptr);
						sampler = VK_NULL_HANDLE;
					}
				}
			}

			mQuadVertexBuffer = {};
			mTexturedQuadVertexBuffer = {};
			mDynamicTexturedVertexBuffer = {};
			mDynamicVertexColorBuffer = {};
			mDynamicTexturedVertexCapacity = 0;
			mDynamicVertexColorCapacity = 0;
			mDynamicTexturedWriteOffset = 0;
			mDynamicVertexColorWriteOffset = 0;
			for (size_t index = 0; index < mTempTextAtlasPageIndices.size(); ++index)
			{
				mTempTextAtlasPageIndices[index] = 0;
			}
			mFrameActive = false;
			mRenderPassActive = false;
			mCurrentTargetTexture = nullptr;
		}

		inline bool mayRenderAnything() const
		{
			return !mInvalidScissorRegion;
		}

		inline Vec4f getTransformOfRectInViewport(const Recti& inputRect) const
		{
			Vec4f transform;
			transform.x = mPixelToViewSpaceTransform.x + (float)inputRect.x * mPixelToViewSpaceTransform.z;
			transform.y = mPixelToViewSpaceTransform.y + (float)inputRect.y * mPixelToViewSpaceTransform.w;
			transform.z = (float)inputRect.width * mPixelToViewSpaceTransform.z;
			transform.w = (float)inputRect.height * mPixelToViewSpaceTransform.w;
			return transform;
		}

		void setupViewport(const Recti& logicalViewport, const Recti& physicalViewport)
		{
			mCurrentViewport = logicalViewport;
			mPhysicalViewport = physicalViewport;
			mPixelToViewSpaceTransform.x = -1.0f;
			mPixelToViewSpaceTransform.y = 1.0f;
			mPixelToViewSpaceTransform.z = 2.0f / (float)logicalViewport.width;
			mPixelToViewSpaceTransform.w = -2.0f / (float)logicalViewport.height;
		}

		void applyCurrentScissor()
		{
			if (mScissorStack.empty())
			{
				mInvalidScissorRegion = false;
			}
			else
			{
				mInvalidScissorRegion = mScissorStack.back().empty();
			}
		}

		bool ensureWindowResources()
		{
			if (nullptr == mOutputWindow || !mResources.ensureWindowResources(mOutputWindow))
				return false;

			if (!mRenderResourcesInitialized)
			{
				mRenderResources.initialize();
				mRenderResourcesInitialized = true;
			}

			return ensureStaticQuadBuffers() && ensureAllSamplers();
		}

		bool ensureAllSamplers()
		{
			return ensureSampler(SamplerIndex::POINT_CLAMP, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE) &&
				   ensureSampler(SamplerIndex::POINT_WRAP, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT) &&
				   ensureSampler(SamplerIndex::LINEAR_CLAMP, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE) &&
				   ensureSampler(SamplerIndex::LINEAR_WRAP, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
		}

		bool ensureSampler(SamplerIndex index, VkFilter filter, VkSamplerAddressMode addressMode)
		{
			vulkan::RendererBackend* backend = mResources.getBackend();
			if (nullptr == backend)
				return false;

			VkSampler& sampler = mSamplers[(size_t)index];
			if (sampler != VK_NULL_HANDLE)
				return true;

			VkSamplerCreateInfo samplerCreateInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
			samplerCreateInfo.magFilter = filter;
			samplerCreateInfo.minFilter = filter;
			samplerCreateInfo.mipmapMode = (filter == VK_FILTER_LINEAR) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
			samplerCreateInfo.addressModeU = addressMode;
			samplerCreateInfo.addressModeV = addressMode;
			samplerCreateInfo.addressModeW = addressMode;
			samplerCreateInfo.minLod = 0.0f;
			samplerCreateInfo.maxLod = 0.0f;
			samplerCreateInfo.maxAnisotropy = 1.0f;
			return (backend->getDevice().vk().CreateSampler(backend->getDevice().getDevice(), &samplerCreateInfo, nullptr, &sampler) == VK_SUCCESS);
		}

		VkSampler getSampler(SamplingMode samplingMode, TextureWrapMode wrapMode) const
		{
			if (samplingMode == SamplingMode::BILINEAR)
			{
				return mSamplers[(size_t)((wrapMode == TextureWrapMode::REPEAT) ? SamplerIndex::LINEAR_WRAP : SamplerIndex::LINEAR_CLAMP)];
			}
			return mSamplers[(size_t)((wrapMode == TextureWrapMode::REPEAT) ? SamplerIndex::POINT_WRAP : SamplerIndex::POINT_CLAMP)];
		}

		bool ensureStaticQuadBuffers()
		{
			vulkan::RendererBackend* backend = mResources.getBackend();
			if (nullptr == backend)
				return false;

			vulkan::IAllocator& allocator = backend->getAllocator();
			if (mQuadVertexBuffer.mBuffer == VK_NULL_HANDLE)
			{
				static const float QUAD_VERTEX_DATA[] =
				{
					0.0f, 0.0f,
					0.0f, 1.0f,
					1.0f, 1.0f,
					1.0f, 1.0f,
					1.0f, 0.0f,
					0.0f, 0.0f
				};

				if (!allocator.allocateBuffer(sizeof(QUAD_VERTEX_DATA), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mQuadVertexBuffer))
					return false;
				std::memcpy(mQuadVertexBuffer.mMappedData, QUAD_VERTEX_DATA, sizeof(QUAD_VERTEX_DATA));
			}

			if (mTexturedQuadVertexBuffer.mBuffer == VK_NULL_HANDLE)
			{
				static const float TEXTURED_QUAD_VERTEX_DATA[] =
				{
					0.0f, 0.0f, 0.0f, 0.0f,
					0.0f, 1.0f, 0.0f, 1.0f,
					1.0f, 1.0f, 1.0f, 1.0f,
					1.0f, 1.0f, 1.0f, 1.0f,
					1.0f, 0.0f, 1.0f, 0.0f,
					0.0f, 0.0f, 0.0f, 0.0f
				};

				if (!allocator.allocateBuffer(sizeof(TEXTURED_QUAD_VERTEX_DATA), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mTexturedQuadVertexBuffer))
					return false;
				std::memcpy(mTexturedQuadVertexBuffer.mMappedData, TEXTURED_QUAD_VERTEX_DATA, sizeof(TEXTURED_QUAD_VERTEX_DATA));
			}

			return true;
		}

		bool ensureDynamicBuffer(vulkan::BufferAllocation& buffer, VkDeviceSize& capacity, size_t requiredSize)
		{
			vulkan::RendererBackend* backend = mResources.getBackend();
			if (nullptr == backend)
				return false;

			if (buffer.mBuffer != VK_NULL_HANDLE && capacity >= requiredSize)
				return true;

			if (buffer.mBuffer != VK_NULL_HANDLE)
			{
				if (backend->getCurrentFrame().isRecording())
				{
					// Earlier commands in the current frame may still reference this buffer.
					backend->getCurrentFrame().addPendingUpload(std::move(buffer));
				}
				else
				{
					backend->getAllocator().destroyBuffer(buffer);
				}
				capacity = 0;
			}

			const VkDeviceSize allocationSize = std::max<VkDeviceSize>((VkDeviceSize)requiredSize, (VkDeviceSize)(4 * 1024 * 1024));
			if (!backend->getAllocator().allocateBuffer(allocationSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, buffer))
				return false;

			capacity = allocationSize;
			return true;
		}

		bool beginFrame()
		{
			if (mFrameActive)
				return true;

			if (!ensureWindowResources())
				return false;

			vulkan::RendererBackend* backend = mResources.getBackend();
			if (nullptr == backend || nullptr == mOutputWindow)
				return false;

			if (!backend->beginOffscreenFrame(*mOutputWindow))
				return false;

			mFrameActive = true;
			mRenderPassActive = false;
			mScissorStack.clear();
			mInvalidScissorRegion = false;
			mTouchedRenderTargets.clear();
			mCurrentBlendMode = BlendMode::OPAQUE;
			mCurrentSamplingMode = SamplingMode::POINT;
			mCurrentWrapMode = TextureWrapMode::CLAMP;
			mDynamicTexturedWriteOffset = 0;
			mDynamicVertexColorWriteOffset = 0;
			const uint32 frameIndex = backend->getCurrentFrameIndex();
			mTempTextAtlasPageIndices[frameIndex] = 0;
			for (TempTextAtlas& atlas : mTempTextAtlases[frameIndex])
			{
				atlas.mCursor.set(0, 0);
				atlas.mRowHeight = 0;
			}
			return true;
		}

		void finishFrame()
		{
			if (!mFrameActive)
				return;

			vulkan::RendererBackend* backend = mResources.getBackend();
			if (nullptr == backend)
			{
				mFrameActive = false;
				mRenderPassActive = false;
				return;
			}

			if (mRenderPassActive)
			{
				backend->endRenderingPass();
				mRenderPassActive = false;
			}
			backend->submitOffscreenFrame(true);
			mFrameActive = false;
		}

		bool bindTarget(vulkan::Texture& targetTexture, const Recti& viewport, uint32 targetId)
		{
			vulkan::RendererBackend* backend = mResources.getBackend();
			if (nullptr == backend || !mFrameActive || !targetTexture.isValid())
				return false;

			const bool firstUseThisFrame = (mTouchedRenderTargets.insert(targetId).second);
			if (mRenderPassActive)
			{
				backend->endRenderingPass();
			}
			backend->beginColorPass(targetTexture, firstUseThisFrame ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD);
			mCurrentTargetTexture = &targetTexture;
			mCurrentTargetUniqueId = targetId;
			Recti physicalViewport = viewport;
			if (targetId == 0xffffffffu)
			{
				physicalViewport = Recti(0, 0, targetTexture.getSize().x, targetTexture.getSize().y);
			}
			setupViewport(viewport, physicalViewport);
			mScissorStack.clear();
			applyCurrentScissor();
			mRenderPassActive = true;
			return true;
		}

		bool ensureRenderPass()
		{
			if (!mFrameActive)
				return false;
			if (mRenderPassActive)
				return true;
			if (nullptr == mCurrentTargetTexture)
				return false;

			vulkan::RendererBackend* backend = mResources.getBackend();
			if (nullptr == backend)
				return false;

			const bool firstUseThisFrame = (mTouchedRenderTargets.insert(mCurrentTargetUniqueId).second);
			backend->beginColorPass(*mCurrentTargetTexture, firstUseThisFrame ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD);
			mRenderPassActive = true;
			return true;
		}

		bool drawPipeline(vulkan::PipelineID pipelineId, const void* uniformData, size_t uniformSize, size_t uniformAlignment, VkBuffer vertexBuffer, VkDeviceSize vertexOffset, const vulkan::Descriptors::MaterialKey& materialKey, uint32 vertexCount)
		{
			vulkan::RendererBackend* backend = mResources.getBackend();
			if (nullptr == backend || nullptr == uniformData || vertexBuffer == VK_NULL_HANDLE || vertexCount == 0)
				return false;
			if (!ensureRenderPass())
				return false;

			vulkan::Frame& frame = backend->getCurrentFrame();
			const vulkan::Frame::UniformSlice uniformSlice = frame.allocateUniform(uniformSize, std::max<size_t>(uniformAlignment, backend->getDevice().getLimits().minUniformBufferOffsetAlignment));
			if (nullptr == uniformSlice.mMappedData)
				return false;
			std::memcpy(uniformSlice.mMappedData, uniformData, uniformSize);

			VkDescriptorSet descriptorSets[2] =
			{
				backend->getDescriptors().getFrameSet(backend->getCurrentFrameIndex()),
				backend->getDescriptors().getMaterialSet(backend->getDevice(), materialKey)
			};
			if (descriptorSets[0] == VK_NULL_HANDLE || descriptorSets[1] == VK_NULL_HANDLE)
				return false;

			const VkPipeline pipeline = backend->getPipelines().getPipeline(pipelineId);
			if (pipeline == VK_NULL_HANDLE)
				return false;

			VkCommandBuffer commandBuffer = frame.getCommandBuffer();
			VkViewport viewport = {};
			viewport.x = (float)mPhysicalViewport.x;
			viewport.y = (float)(mPhysicalViewport.y + mPhysicalViewport.height);
			viewport.width = (float)mPhysicalViewport.width;
			viewport.height = -(float)mPhysicalViewport.height;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			backend->getDevice().vk().CmdSetViewport(commandBuffer, 0, 1, &viewport);

			Recti scissorRect = mCurrentViewport;
			if (!mScissorStack.empty())
			{
				scissorRect.intersect(mScissorStack.back());
			}
			scissorRect.x = clamp(scissorRect.x, mCurrentViewport.x, mCurrentViewport.x + mCurrentViewport.width);
			scissorRect.y = clamp(scissorRect.y, mCurrentViewport.y, mCurrentViewport.y + mCurrentViewport.height);
			scissorRect.width = clamp(scissorRect.width, 0, mCurrentViewport.x + mCurrentViewport.width - scissorRect.x);
			scissorRect.height = clamp(scissorRect.height, 0, mCurrentViewport.y + mCurrentViewport.height - scissorRect.y);

			const float scaleX = (float)mPhysicalViewport.width / (float)std::max(mCurrentViewport.width, 1);
			const float scaleY = (float)mPhysicalViewport.height / (float)std::max(mCurrentViewport.height, 1);

			VkRect2D scissor = {};
			scissor.offset.x = mPhysicalViewport.x + roundToInt((float)(scissorRect.x - mCurrentViewport.x) * scaleX);
			scissor.offset.y = mPhysicalViewport.y + roundToInt((float)(scissorRect.y - mCurrentViewport.y) * scaleY);
			scissor.extent.width = (uint32)std::max(roundToInt((float)std::max(scissorRect.width, 0) * scaleX), 0);
			scissor.extent.height = (uint32)std::max(roundToInt((float)std::max(scissorRect.height, 0) * scaleY), 0);
			backend->getDevice().vk().CmdSetScissor(commandBuffer, 0, 1, &scissor);

			backend->getDevice().vk().CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			backend->getDevice().vk().CmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, backend->getPipelines().getPipelineLayout(), 0, 2, descriptorSets, 1, &uniformSlice.mDynamicOffset);

			backend->getDevice().vk().CmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
			backend->getDevice().vk().CmdDraw(commandBuffer, vertexCount, 1, 0, 0);
			return true;
		}

		bool drawColoredRect(const Recti& rect, const Color& color)
		{
			SimpleRectConstants constants;
			constants.mTransform = getTransformOfRectInViewport(rect);
			constants.mTintColor = color;
			return drawPipeline(getColoredPipelineId(mCurrentBlendMode), &constants, sizeof(constants), 16, mQuadVertexBuffer.mBuffer, 0, {}, 6);
		}

		bool drawTexturedRect(const Recti& rect, vulkan::Texture& texture, const Color& tintColor, const Color& addedColor, bool alphaTest, const Vec2f* uv0 = nullptr, const Vec2f* uv1 = nullptr, SamplingMode samplingMode = SamplingMode::POINT, TextureWrapMode wrapMode = TextureWrapMode::CLAMP)
		{
			vulkan::RendererBackend* backend = mResources.getBackend();
			if (nullptr == backend || !texture.isValid())
				return false;

			transitionTextureForSampling(backend->getDevice(), backend->getCurrentFrame().getCommandBuffer(), texture);

			SimpleRectConstants constants;
			constants.mTransform = getTransformOfRectInViewport(rect);
			constants.mTintColor = tintColor;
			constants.mAddedColor = addedColor;
			constants.mTextureSize[0] = texture.getSize().x;
			constants.mTextureSize[1] = texture.getSize().y;
			constants.mAlphaTest = alphaTest ? 1 : 0;

			VkBuffer vertexBuffer = mQuadVertexBuffer.mBuffer;
			VkDeviceSize vertexOffset = 0;
			const bool useCustomUVs = (nullptr != uv0 && nullptr != uv1 && (*uv0 != Vec2f(0.0f, 0.0f) || *uv1 != Vec2f(1.0f, 1.0f)));
			if (useCustomUVs)
			{
				const float vertexData[] =
				{
					0.0f, 0.0f, uv0->x, uv0->y,
					0.0f, 1.0f, uv0->x, uv1->y,
					1.0f, 1.0f, uv1->x, uv1->y,
					1.0f, 1.0f, uv1->x, uv1->y,
					1.0f, 0.0f, uv1->x, uv0->y,
					0.0f, 0.0f, uv0->x, uv0->y
				};
				const size_t vertexDataSize = sizeof(vertexData);
				if (!ensureDynamicBuffer(mDynamicTexturedVertexBuffer, mDynamicTexturedVertexCapacity, mDynamicTexturedWriteOffset + vertexDataSize))
					return false;
				vertexOffset = mDynamicTexturedWriteOffset;
				std::memcpy((uint8*)mDynamicTexturedVertexBuffer.mMappedData + vertexOffset, vertexData, vertexDataSize);
				mDynamicTexturedWriteOffset += vertexDataSize;
				vertexBuffer = mDynamicTexturedVertexBuffer.mBuffer;
			}

			vulkan::Descriptors::MaterialKey materialKey;
			assignMaterialTexture(materialKey, 0, texture, getSampler(samplingMode, wrapMode));
			return drawPipeline(getTexturedPipelineId(useCustomUVs, mCurrentBlendMode), &constants, sizeof(constants), 16, vertexBuffer, vertexOffset, materialKey, 6);
		}

		bool drawIndexedRect(const Recti& rect, vulkan::Texture& texture, vulkan::Texture& paletteTexture, const Color& tintColor)
		{
			vulkan::RendererBackend* backend = mResources.getBackend();
			if (nullptr == backend || !texture.isValid() || !paletteTexture.isValid())
				return false;

			transitionTextureForSampling(backend->getDevice(), backend->getCurrentFrame().getCommandBuffer(), texture);
			transitionTextureForSampling(backend->getDevice(), backend->getCurrentFrame().getCommandBuffer(), paletteTexture);

			SimpleRectConstants constants;
			constants.mTransform = getTransformOfRectInViewport(rect);
			constants.mTintColor = tintColor;
			constants.mTextureSize[0] = texture.getSize().x;
			constants.mTextureSize[1] = texture.getSize().y;
			constants.mAlphaTest = useAlphaTest(mCurrentBlendMode) ? 1 : 0;
			constants.mShadowHighlightMode = RenderParts::instance().getPaletteManager().useShadowHighlightMode() ? 1 : 0;

			vulkan::Descriptors::MaterialKey materialKey;
			assignMaterialTexture(materialKey, 0, texture, getSampler(SamplingMode::POINT, TextureWrapMode::CLAMP));
			assignMaterialTexture(materialKey, 1, paletteTexture, getSampler(SamplingMode::POINT, TextureWrapMode::CLAMP));
			return drawPipeline(getIndexedPipelineId(mCurrentBlendMode), &constants, sizeof(constants), 16, mQuadVertexBuffer.mBuffer, 0, materialKey, 6);
		}

		bool drawMesh(const std::vector<DrawerMeshVertex>& triangles, vulkan::Texture& texture)
		{
			if (triangles.empty() || !texture.isValid())
				return false;

			std::vector<float> vertexData;
			vertexData.resize(triangles.size() * 4);
			for (size_t i = 0; i < triangles.size(); ++i)
			{
				const DrawerMeshVertex& src = triangles[i];
				float* dst = &vertexData[i * 4];
				dst[0] = src.mPosition.x;
				dst[1] = src.mPosition.y;
				dst[2] = src.mTexcoords.x;
				dst[3] = src.mTexcoords.y;
			}

			const size_t vertexDataSize = vertexData.size() * sizeof(float);
			if (!ensureDynamicBuffer(mDynamicTexturedVertexBuffer, mDynamicTexturedVertexCapacity, mDynamicTexturedWriteOffset + vertexDataSize))
				return false;
			const VkDeviceSize vertexOffset = mDynamicTexturedWriteOffset;
			std::memcpy((uint8*)mDynamicTexturedVertexBuffer.mMappedData + vertexOffset, vertexData.data(), vertexDataSize);
			mDynamicTexturedWriteOffset += vertexDataSize;

			vulkan::RendererBackend* backend = mResources.getBackend();
			if (nullptr == backend)
				return false;
			transitionTextureForSampling(backend->getDevice(), backend->getCurrentFrame().getCommandBuffer(), texture);

			SimpleRectConstants constants;
			constants.mTransform = mPixelToViewSpaceTransform;
			constants.mTintColor = Color::WHITE;
			constants.mTextureSize[0] = texture.getSize().x;
			constants.mTextureSize[1] = texture.getSize().y;
			constants.mAlphaTest = 1;

			vulkan::Descriptors::MaterialKey materialKey;
			assignMaterialTexture(materialKey, 0, texture, getSampler(mCurrentSamplingMode, mCurrentWrapMode));
			return drawPipeline(vulkan::PipelineID::SIMPLE_TEXTURED_UV_ALPHA, &constants, sizeof(constants), 16, mDynamicTexturedVertexBuffer.mBuffer, vertexOffset, materialKey, (uint32)triangles.size());
		}

		bool drawMeshVertexColor(const std::vector<DrawerMeshVertex_P2_C4>& triangles)
		{
			if (triangles.empty())
				return false;

			std::vector<float> vertexData;
			vertexData.resize(triangles.size() * 6);
			for (size_t i = 0; i < triangles.size(); ++i)
			{
				const DrawerMeshVertex_P2_C4& src = triangles[i];
				float* dst = &vertexData[i * 6];
				dst[0] = src.mPosition.x;
				dst[1] = src.mPosition.y;
				dst[2] = src.mColor.r;
				dst[3] = src.mColor.g;
				dst[4] = src.mColor.b;
				dst[5] = src.mColor.a;
			}

			const size_t vertexDataSize = vertexData.size() * sizeof(float);
			if (!ensureDynamicBuffer(mDynamicVertexColorBuffer, mDynamicVertexColorCapacity, mDynamicVertexColorWriteOffset + vertexDataSize))
				return false;
			const VkDeviceSize vertexOffset = mDynamicVertexColorWriteOffset;
			std::memcpy((uint8*)mDynamicVertexColorBuffer.mMappedData + vertexOffset, vertexData.data(), vertexDataSize);
			mDynamicVertexColorWriteOffset += vertexDataSize;

			SimpleRectConstants constants;
			constants.mTransform = mPixelToViewSpaceTransform;
			return drawPipeline(getVertexColorPipelineId(mCurrentBlendMode), &constants, sizeof(constants), 16, mDynamicVertexColorBuffer.mBuffer, vertexOffset, {}, (uint32)triangles.size());
		}

		template<typename T>
		void printText(Font& font, const T& text, const Recti& rect, const DrawerPrintOptions& printOptions)
		{
			vulkan::RendererBackend* backend = mResources.getBackend();
			if (nullptr == backend)
				return;

			Vec2i drawPosition;
			font.printBitmap(mTempTextBitmap, drawPosition, rect, text, printOptions.mAlignment, printOptions.mSpacing, &mTempTextReservedSize);
			if (mTempTextBitmap.empty())
				return;

			if (mRenderPassActive)
			{
				// Keep text texture uploads outside of an active color pass so the render/sampling
				// state gets resumed from a clean point afterwards.
				backend->endRenderingPass();
				mRenderPassActive = false;
			}
			vulkan::Texture* textTexture = nullptr;
			Vec2i atlasPosition;
			if (!acquireTempTextAtlasRegion(mTempTextBitmap.getSize(), textTexture, atlasPosition) || nullptr == textTexture)
				return;
			if (!textTexture->updateSubRectFromBitmap(backend->getDevice(), backend->getAllocator(), &backend->getCurrentFrame(), mTempTextBitmap, atlasPosition))
				return;

			const BlendMode previousBlendMode = mCurrentBlendMode;
			mCurrentBlendMode = BlendMode::ALPHA;
			const Vec2f uv0((float)atlasPosition.x / (float)textTexture->getSize().x, (float)atlasPosition.y / (float)textTexture->getSize().y);
			const Vec2f uv1((float)(atlasPosition.x + mTempTextBitmap.getSize().x) / (float)textTexture->getSize().x,
				(float)(atlasPosition.y + mTempTextBitmap.getSize().y) / (float)textTexture->getSize().y);
			drawTexturedRect(Recti(drawPosition, mTempTextBitmap.getSize()), *textTexture, printOptions.mTintColor, Color::TRANSPARENT, true, &uv0, &uv1);
			mCurrentBlendMode = previousBlendMode;
		}

		struct TempTextAtlas
		{
			vulkan::Texture mTexture;
			Vec2i mSize;
			Vec2i mCursor;
			int mRowHeight = 0;
		};

		Vec2i getTempTextAtlasPageSize(const Vec2i& requiredSize) const
		{
			const int baseWidth = std::max(std::max(mCurrentViewport.width, requiredSize.x), 1024);
			const int baseHeight = std::max(std::max(mCurrentViewport.height, requiredSize.y), 1024);
			return Vec2i(clamp(nextPowerOfTwo(baseWidth), 1024, 4096), clamp(nextPowerOfTwo(baseHeight), 1024, 4096));
		}

		bool ensureTempTextAtlas(TempTextAtlas& atlas, const Vec2i& requiredSize)
		{
			vulkan::RendererBackend* backend = mResources.getBackend();
			if (nullptr == backend)
				return false;

			const Vec2i desiredSize = getTempTextAtlasPageSize(requiredSize);
			if (atlas.mTexture.isValid() && atlas.mSize.x >= requiredSize.x && atlas.mSize.y >= requiredSize.y)
				return true;

			if (atlas.mTexture.isValid())
			{
				atlas.mTexture.destroy(backend->getDevice(), backend->getAllocator());
			}

			atlas.mSize = desiredSize;
			atlas.mCursor.set(0, 0);
			atlas.mRowHeight = 0;
			return atlas.mTexture.create(backend->getDevice(), backend->getAllocator(), VK_FORMAT_R8G8B8A8_UNORM, atlas.mSize,
				VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true, false);
		}

		bool acquireTempTextAtlasRegion(const Vec2i& bitmapSize, vulkan::Texture*& outTexture, Vec2i& outAtlasPosition)
		{
			outTexture = nullptr;
			outAtlasPosition.set(0, 0);

			vulkan::RendererBackend* backend = mResources.getBackend();
			if (nullptr == backend || bitmapSize.x <= 0 || bitmapSize.y <= 0)
				return false;

			const uint32 frameIndex = backend->getCurrentFrameIndex();
			auto& frameAtlases = mTempTextAtlases[frameIndex];
			size_t& pageIndex = mTempTextAtlasPageIndices[frameIndex];
			const Vec2i paddedSize(bitmapSize.x + 1, bitmapSize.y + 1);

			for (;;)
			{
				if (pageIndex >= frameAtlases.size())
				{
					frameAtlases.emplace_back();
				}

				TempTextAtlas& atlas = frameAtlases[pageIndex];
				if (!ensureTempTextAtlas(atlas, paddedSize))
					return false;

				if (atlas.mCursor.x + paddedSize.x > atlas.mSize.x)
				{
					atlas.mCursor.x = 0;
					atlas.mCursor.y += atlas.mRowHeight;
					atlas.mRowHeight = 0;
				}
				if (atlas.mCursor.y + paddedSize.y > atlas.mSize.y)
				{
					++pageIndex;
					continue;
				}

				outAtlasPosition = atlas.mCursor;
				atlas.mCursor.x += paddedSize.x;
				atlas.mRowHeight = std::max(atlas.mRowHeight, paddedSize.y);
				outTexture = &atlas.mTexture;
				return true;
			}
		}

		bool drawSprite(const SpriteDrawCommand& sc)
		{
			const SpriteCollection::Item* item = SpriteCollection::instance().getSprite(sc.mSpriteKey);
			if (nullptr == item)
				return false;

			SpriteBase& sprite = *item->mSprite;
			Vec2i offset = sprite.mOffset;
			Vec2i size = sprite.getSize();
			if (sc.mScale.x != 1.0f || sc.mScale.y != 1.0f)
			{
				offset.x = roundToInt((float)offset.x * sc.mScale.x);
				offset.y = roundToInt((float)offset.y * sc.mScale.y);
				size.x = roundToInt((float)size.x * sc.mScale.x);
				size.y = roundToInt((float)size.y * sc.mScale.y);
			}
			const Recti targetRect(sc.mPosition + offset, size);

			if (item->mUsesComponentSprite)
			{
				const vulkan::Texture* texture = mRenderResources.getComponentSpriteTexture(*item);
				return (nullptr != texture) ? drawTexturedRect(targetRect, const_cast<vulkan::Texture&>(*texture), sc.mTintColor, Color::TRANSPARENT, useAlphaTest(mCurrentBlendMode), nullptr, nullptr, mCurrentSamplingMode, mCurrentWrapMode) : false;
			}

			const vulkan::Texture* texture = mRenderResources.getPaletteSpriteTexture(*item, false);
			const PaletteBase* palette = PaletteCollection::instance().getPalette(sc.mPaletteKey, 0);
			if (nullptr == texture || nullptr == palette)
				return false;

			const vulkan::Texture& paletteTexture = mRenderResources.getPaletteTexture(palette, palette);
			return drawIndexedRect(targetRect, const_cast<vulkan::Texture&>(*texture), const_cast<vulkan::Texture&>(paletteTexture), sc.mTintColor);
		}

		bool drawSpriteRect(const SpriteRectDrawCommand& sc)
		{
			const SpriteCollection::Item* item = SpriteCollection::instance().getSprite(sc.mSpriteKey);
			if (nullptr == item || !item->mUsesComponentSprite)
				return false;

			const vulkan::Texture* texture = mRenderResources.getComponentSpriteTexture(*item);
			return (nullptr != texture) ? drawTexturedRect(sc.mRect, const_cast<vulkan::Texture&>(*texture), sc.mTintColor, Color::TRANSPARENT, useAlphaTest(mCurrentBlendMode), nullptr, nullptr, mCurrentSamplingMode, mCurrentWrapMode) : false;
		}

		VulkanDrawerTexture* ensureTextureReady(DrawerTexture& texture)
		{
			VulkanDrawerTexture* drawerTexture = texture.getImplementation<VulkanDrawerTexture>();
			if (nullptr == drawerTexture)
				return nullptr;

			if (!drawerTexture->mTexture.isValid())
			{
				if (texture.isRenderTarget())
				{
					drawerTexture->setupAsRenderTarget(texture.getSize());
				}
				else if (!texture.getBitmap().empty())
				{
					drawerTexture->updateFromBitmap(texture.getBitmap());
				}
			}

			return drawerTexture->mTexture.isValid() ? drawerTexture : nullptr;
		}

		bool drawUpscaledRect(const Recti& rect, DrawerTexture& texture)
		{
			VulkanDrawerTexture* drawerTexture = ensureTextureReady(texture);
			if (nullptr == drawerTexture)
				return false;

			if (nullptr == mCurrentTargetTexture)
				return false;

			const int filtering = Configuration::instance().mFiltering;
			const int scanlines = Configuration::instance().mScanlines;
			if (filtering != 0 || scanlines != 0)
			{
				vulkan::RendererBackend* backend = mResources.getBackend();
				if (nullptr == backend)
					return false;

				if (mRenderPassActive)
				{
					backend->endRenderingPass();
					mRenderPassActive = false;
				}

				return mResources.getUpscaler().renderImage(rect, mCurrentViewport, drawerTexture->mTexture, *mCurrentTargetTexture);
			}

			const BlendMode previousBlendMode = mCurrentBlendMode;
			mCurrentBlendMode = BlendMode::OPAQUE;
			const bool result = drawTexturedRect(rect, drawerTexture->mTexture, Color::WHITE, Color::TRANSPARENT, false, nullptr, nullptr, SamplingMode::POINT, TextureWrapMode::CLAMP);
			mCurrentBlendMode = previousBlendMode;
			return result;
		}

		bool mSetupSuccessful = false;
		VulkanDrawerResources mResources;
		VulkanRenderResources mRenderResources;
		SoftwareDrawer mSoftwareDrawer;
		SDL_Window* mOutputWindow = nullptr;
		Recti mCurrentViewport;
		Recti mPhysicalViewport;
		Vec4f mPixelToViewSpaceTransform;
		std::vector<Recti> mScissorStack;
		bool mInvalidScissorRegion = false;
		BlendMode mCurrentBlendMode = BlendMode::OPAQUE;
		SamplingMode mCurrentSamplingMode = SamplingMode::POINT;
		TextureWrapMode mCurrentWrapMode = TextureWrapMode::CLAMP;
		bool mFrameActive = false;
		bool mRenderPassActive = false;
		bool mRenderResourcesInitialized = false;
		vulkan::Texture* mCurrentTargetTexture = nullptr;
		Bitmap mOutputBitmap;
		Bitmap mTempTextBitmap;
		int mTempTextReservedSize = 0;
		std::array<std::deque<TempTextAtlas>, vulkan::VULKAN_FRAMES_IN_FLIGHT> mTempTextAtlases;
		std::array<size_t, vulkan::VULKAN_FRAMES_IN_FLIGHT> mTempTextAtlasPageIndices = { 0, 0 };
		vulkan::BufferAllocation mQuadVertexBuffer;
		vulkan::BufferAllocation mTexturedQuadVertexBuffer;
		vulkan::BufferAllocation mDynamicTexturedVertexBuffer;
		VkDeviceSize mDynamicTexturedVertexCapacity = 0;
		VkDeviceSize mDynamicTexturedWriteOffset = 0;
		vulkan::BufferAllocation mDynamicVertexColorBuffer;
		VkDeviceSize mDynamicVertexColorCapacity = 0;
		VkDeviceSize mDynamicVertexColorWriteOffset = 0;
		std::array<VkSampler, (size_t)SamplerIndex::COUNT> mSamplers = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
		std::unordered_set<uint32> mTouchedRenderTargets;
		uint32 mCurrentTargetUniqueId = 0xffffffffu;
	};
}


VulkanDrawer::VulkanDrawer() :
	mInternal(*new vulkandrawer::Internal())
{
}

VulkanDrawer::~VulkanDrawer()
{
	delete &mInternal;
}

bool VulkanDrawer::wasSetupSuccessful()
{
	return mInternal.mSetupSuccessful;
}

void VulkanDrawer::updateDrawer(float deltaSeconds)
{
	mInternal.mResources.refresh(deltaSeconds);
}

void VulkanDrawer::createTexture(DrawerTexture& outTexture)
{
	if (isHardwareDrawerEnabled())
	{
		outTexture.setImplementation(new VulkanDrawerTexture(outTexture));
	}
	else
	{
		outTexture.setImplementation(new SoftwareDrawerTexture(outTexture));
	}
}

void VulkanDrawer::refreshTexture(DrawerTexture& texture)
{
	createTexture(texture);
}

void VulkanDrawer::setupRenderWindow(SDL_Window* window)
{
	mInternal.mOutputWindow = window;
	mInternal.mResources.ensureWindowResources(window);

	if (!isHardwareDrawerEnabled() && nullptr != window)
	{
		int width = 0;
		int height = 0;
		SDL_GetWindowSizeInPixels(window, &width, &height);
		if (width <= 0 || height <= 0)
		{
			SDL_GetWindowSize(window, &width, &height);
		}
		width = std::max(width, 1);
		height = std::max(height, 1);
		if (mInternal.mOutputBitmap.getWidth() != width || mInternal.mOutputBitmap.getHeight() != height)
		{
			mInternal.mOutputBitmap.create(width, height);
		}
	}
}

void VulkanDrawer::performRendering(const DrawCollection& drawCollection)
{
	if (!isHardwareDrawerEnabled())
	{
		mInternal.mSoftwareDrawer.setExternalOutputBitmap(&mInternal.mOutputBitmap);
		mInternal.mSoftwareDrawer.performRendering(drawCollection);
		return;
	}

	if (!mInternal.beginFrame())
		return;

	for (DrawCommand* drawCommand : drawCollection.getDrawCommands())
	{
		switch (drawCommand->getType())
		{
			case DrawCommand::Type::UNDEFINED:
				RMX_ERROR("Got invalid draw command", );
				break;

			case DrawCommand::Type::SET_WINDOW_RENDER_TARGET:
			{
				vulkan::RendererBackend* backend = mInternal.mResources.getBackend();
				if (nullptr == backend)
					break;

				SetWindowRenderTargetDrawCommand& dc = drawCommand->as<SetWindowRenderTargetDrawCommand>();
				mInternal.bindTarget(backend->getWindowTexture(), dc.mViewport, 0xffffffffu);
				break;
			}

			case DrawCommand::Type::SET_RENDER_TARGET:
			{
				SetRenderTargetDrawCommand& dc = drawCommand->as<SetRenderTargetDrawCommand>();
				VulkanDrawerTexture* drawerTexture = mInternal.ensureTextureReady(*dc.mTexture);
				if (nullptr == drawerTexture)
					break;

				mInternal.bindTarget(drawerTexture->mTexture, dc.mViewport, dc.mTexture->getUniqueID());
				break;
			}

			case DrawCommand::Type::RECT:
			{
				if (!mInternal.mayRenderAnything())
					break;

				RectDrawCommand& dc = drawCommand->as<RectDrawCommand>();
				if (nullptr == dc.mTexture)
				{
					mInternal.drawColoredRect(dc.mRect, dc.mColor);
					break;
				}

				VulkanDrawerTexture* texture = mInternal.ensureTextureReady(*dc.mTexture);
				if (nullptr == texture)
					break;

				mInternal.drawTexturedRect(dc.mRect, texture->mTexture, dc.mColor, Color::TRANSPARENT, useAlphaTest(mInternal.mCurrentBlendMode), &dc.mUV0, &dc.mUV1, mInternal.mCurrentSamplingMode, mInternal.mCurrentWrapMode);
				break;
			}

			case DrawCommand::Type::UPSCALED_RECT:
			{
				if (!mInternal.mayRenderAnything())
					break;

				UpscaledRectDrawCommand& dc = drawCommand->as<UpscaledRectDrawCommand>();
				if (nullptr != dc.mTexture)
				{
					mInternal.drawUpscaledRect(dc.mRect, *dc.mTexture);
				}
				break;
			}

			case DrawCommand::Type::SPRITE:
			{
				if (!mInternal.mayRenderAnything())
					break;
				mInternal.drawSprite(drawCommand->as<SpriteDrawCommand>());
				break;
			}

			case DrawCommand::Type::SPRITE_RECT:
			{
				if (!mInternal.mayRenderAnything())
					break;
				mInternal.drawSpriteRect(drawCommand->as<SpriteRectDrawCommand>());
				break;
			}

			case DrawCommand::Type::MESH:
			{
				if (!mInternal.mayRenderAnything())
					break;

				MeshDrawCommand& dc = drawCommand->as<MeshDrawCommand>();
				if (dc.mTriangles.empty() || nullptr == dc.mTexture)
					break;

				VulkanDrawerTexture* texture = mInternal.ensureTextureReady(*dc.mTexture);
				if (nullptr == texture)
					break;

				mInternal.drawMesh(dc.mTriangles, texture->mTexture);
				break;
			}

			case DrawCommand::Type::MESH_VERTEX_COLOR:
			{
				if (!mInternal.mayRenderAnything())
					break;

				MeshVertexColorDrawCommand& dc = drawCommand->as<MeshVertexColorDrawCommand>();
				mInternal.drawMeshVertexColor(dc.mTriangles);
				break;
			}

			case DrawCommand::Type::SET_BLEND_MODE:
				mInternal.mCurrentBlendMode = drawCommand->as<SetBlendModeDrawCommand>().mBlendMode;
				break;

			case DrawCommand::Type::SET_SAMPLING_MODE:
				mInternal.mCurrentSamplingMode = drawCommand->as<SetSamplingModeDrawCommand>().mSamplingMode;
				break;

			case DrawCommand::Type::SET_WRAP_MODE:
				mInternal.mCurrentWrapMode = drawCommand->as<SetWrapModeDrawCommand>().mWrapMode;
				break;

			case DrawCommand::Type::PRINT_TEXT:
			{
				if (!mInternal.mayRenderAnything())
					break;
				PrintTextDrawCommand& dc = drawCommand->as<PrintTextDrawCommand>();
				mInternal.printText(*dc.mFont, dc.mText, dc.mRect, dc.mPrintOptions);
				break;
			}

			case DrawCommand::Type::PRINT_TEXT_W:
			{
				if (!mInternal.mayRenderAnything())
					break;
				PrintTextWDrawCommand& dc = drawCommand->as<PrintTextWDrawCommand>();
				mInternal.printText(*dc.mFont, dc.mText, dc.mRect, dc.mPrintOptions);
				break;
			}

			case DrawCommand::Type::PUSH_SCISSOR:
			{
				Recti scissorRect = drawCommand->as<PushScissorDrawCommand>().mRect;
				if (!mInternal.mScissorStack.empty())
					scissorRect.intersect(mInternal.mScissorStack.back());
				mInternal.mScissorStack.emplace_back(scissorRect);
				mInternal.applyCurrentScissor();
				break;
			}

			case DrawCommand::Type::POP_SCISSOR:
			{
				if (!mInternal.mScissorStack.empty())
					mInternal.mScissorStack.pop_back();
				mInternal.applyCurrentScissor();
				break;
			}
		}
	}

}

void VulkanDrawer::presentScreen()
{
	if (!isHardwareDrawerEnabled())
	{
		mInternal.mResources.presentBitmap(mInternal.mOutputBitmap, Configuration::useVSync(Configuration::instance().mFrameSync));
		return;
	}

	vulkan::RendererBackend* backend = mInternal.mResources.getBackend();
	if (!mInternal.mFrameActive || nullptr == backend)
		return;

	mInternal.finishFrame();
	mInternal.mResources.presentRawTexture(backend->getWindowTexture(), Configuration::useVSync(Configuration::instance().mFrameSync));
}

VulkanDrawerResources& VulkanDrawer::getResources()
{
	return mInternal.mResources;
}

#endif
