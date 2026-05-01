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

#include "oxygen/rendering/vulkan/VulkanRenderer.h"

#include "oxygen/application/Configuration.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/drawing/vulkan/VulkanDrawer.h"
#include "oxygen/drawing/vulkan/VulkanDrawerResources.h"
#include "oxygen/drawing/vulkan/VulkanDrawerTexture.h"
#include "oxygen/helper/Logging.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/rendering/vulkan/VulkanDevice.h"


namespace
{
	VulkanDrawerResources& getDrawerResources()
	{
		DrawerInterface* drawer = EngineMain::instance().getDrawer().getActiveDrawer();
		RMX_ASSERT(nullptr != drawer, "Expected an active drawer");
		RMX_ASSERT(drawer->getType() == Drawer::Type::VULKAN, "Expected the Vulkan drawer");
		return static_cast<VulkanDrawer*>(drawer)->getResources();
	}

	Vec4f calculateBlurKernel(float x)
	{
		const float y = 1.0f - 2.0f * x;
		return Vec4f(y * y, y * x, x * y, x * x);
	}

	const Vec4f& getBlurKernel(int blurValue)
	{
		static const Vec4f BLUR_KERNELS[] =
		{
			Vec4f(1.0f, 0.0f, 0.0f, 0.0f),
			calculateBlurKernel(16.0f / 256.0f),
			calculateBlurKernel(32.0f / 256.0f),
			calculateBlurKernel(48.0f / 256.0f),
			calculateBlurKernel(64.0f / 256.0f)
		};
		return BLUR_KERNELS[blurValue % 5];
	}

	vulkan::PipelineID getVdpSpritePipelineId(BlendMode blendMode)
	{
		switch (blendMode)
		{
			case BlendMode::OPAQUE:			return vulkan::PipelineID::VDP_SPRITE_OPAQUE;
			case BlendMode::ADDITIVE:		return vulkan::PipelineID::VDP_SPRITE_ADDITIVE;
			case BlendMode::SUBTRACTIVE:	return vulkan::PipelineID::VDP_SPRITE_SUBTRACTIVE;
			case BlendMode::MULTIPLICATIVE: return vulkan::PipelineID::VDP_SPRITE_MULTIPLICATIVE;
			case BlendMode::MINIMUM:		return vulkan::PipelineID::VDP_SPRITE_MINIMUM;
			case BlendMode::MAXIMUM:		return vulkan::PipelineID::VDP_SPRITE_MAXIMUM;
			case BlendMode::ONE_BIT:
			case BlendMode::ALPHA:
			default:						return vulkan::PipelineID::VDP_SPRITE;
		}
	}

	vulkan::PipelineID getPaletteSpritePipelineId(BlendMode blendMode)
	{
		switch (blendMode)
		{
			case BlendMode::OPAQUE:			return vulkan::PipelineID::PALETTE_SPRITE_OPAQUE;
			case BlendMode::ADDITIVE:		return vulkan::PipelineID::PALETTE_SPRITE_ADDITIVE;
			case BlendMode::SUBTRACTIVE:	return vulkan::PipelineID::PALETTE_SPRITE_SUBTRACTIVE;
			case BlendMode::MULTIPLICATIVE: return vulkan::PipelineID::PALETTE_SPRITE_MULTIPLICATIVE;
			case BlendMode::MINIMUM:		return vulkan::PipelineID::PALETTE_SPRITE_MINIMUM;
			case BlendMode::MAXIMUM:		return vulkan::PipelineID::PALETTE_SPRITE_MAXIMUM;
			case BlendMode::ONE_BIT:
			case BlendMode::ALPHA:
			default:						return vulkan::PipelineID::PALETTE_SPRITE;
		}
	}

	vulkan::PipelineID getComponentSpritePipelineId(BlendMode blendMode)
	{
		switch (blendMode)
		{
			case BlendMode::OPAQUE:			return vulkan::PipelineID::COMPONENT_SPRITE_OPAQUE;
			case BlendMode::ADDITIVE:		return vulkan::PipelineID::COMPONENT_SPRITE_ADDITIVE;
			case BlendMode::SUBTRACTIVE:	return vulkan::PipelineID::COMPONENT_SPRITE_SUBTRACTIVE;
			case BlendMode::MULTIPLICATIVE: return vulkan::PipelineID::COMPONENT_SPRITE_MULTIPLICATIVE;
			case BlendMode::MINIMUM:		return vulkan::PipelineID::COMPONENT_SPRITE_MINIMUM;
			case BlendMode::MAXIMUM:		return vulkan::PipelineID::COMPONENT_SPRITE_MAXIMUM;
			case BlendMode::ONE_BIT:
			case BlendMode::ALPHA:
			default:						return vulkan::PipelineID::COMPONENT_SPRITE;
		}
	}

	VulkanDrawerTexture* ensureVulkanDrawerTextureReady(DrawerTexture& texture, VulkanDrawerResources& drawerResources);
}


VulkanRenderer::VulkanRenderer(RenderParts& renderParts, DrawerTexture& outputTexture) :
	Renderer(RENDERER_TYPE_ID, renderParts, outputTexture),
	mDrawerResources(getDrawerResources()),
	mRenderResources(renderParts, mDrawerResources),
	mSoftwareRenderer(renderParts, outputTexture)
{
}

VulkanRenderer::~VulkanRenderer()
{
	mRenderResources.clearAllCaches();
	destroyHardwareResources();
}

void VulkanRenderer::initialize()
{
	mGameResolution = Configuration::instance().mGameScreen;
	mGameScreenTexture.setupAsRenderTarget(mGameResolution);
	mRenderResources.initialize();

	if (isHardwareMode())
	{
		ensureHardwareResources();
	}
	else
	{
		ensureSoftwareRendererInitialized();
		static bool sLogged = false;
		if (!sLogged)
		{
			sLogged = true;
			RMX_LOG_WARNING("Vulkan Software currently uses the software game renderer with Vulkan presentation/composition");
		}
	}
}

void VulkanRenderer::reset()
{
	mRenderResources.clearAllCaches();
	if (mSoftwareRendererInitialized)
	{
		mSoftwareRenderer.reset();
	}
	destroyHardwareResources();

	if (isHardwareMode())
	{
		ensureHardwareResources();
	}
}

void VulkanRenderer::setGameResolution(const Vec2i& gameResolution)
{
	mGameResolution = gameResolution;
	if (mSoftwareRendererInitialized)
	{
		mSoftwareRenderer.setGameResolution(gameResolution);
	}
	destroyHardwareResources();

	if (isHardwareMode())
	{
		mGameScreenTexture.setupAsRenderTarget(gameResolution);
		ensureHardwareResources();
	}
}

void VulkanRenderer::ensureSoftwareRendererInitialized()
{
	if (mSoftwareRendererInitialized)
		return;

	mSoftwareRenderer.initialize();
	mSoftwareRenderer.setGameResolution(mGameResolution);
	mSoftwareRendererInitialized = true;
}

void VulkanRenderer::clearGameScreen()
{
	if (!isHardwareMode())
	{
		ensureSoftwareRendererInitialized();
		mSoftwareRenderer.clearGameScreen();
		return;
	}

	if (!ensureHardwareResources())
		return;

	VulkanDrawerTexture* outputTexture = ensureVulkanDrawerTextureReady(mGameScreenTexture, mDrawerResources);
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr == outputTexture || nullptr == backend || !outputTexture->mTexture.isValid())
		return;

	if (!backend->beginOffscreenFrame(EngineMain::instance().getSDLWindow()))
		return;

	backend->beginColorPass(outputTexture->mTexture, VK_ATTACHMENT_LOAD_OP_CLEAR, &mDepthTexture);
	backend->endRenderingPass();
	backend->submitOffscreenFrame(true);
}

void VulkanRenderer::renderGameScreen(const std::vector<Geometry*>& geometries)
{
	mRenderResources.refresh();
	RenderParts& renderParts = RenderParts::instance();

	if (!isHardwareMode())
	{
		ensureSoftwareRendererInitialized();
		mSoftwareRenderer.renderGameScreen(geometries);
		return;
	}

	if (!ensureHardwareResources() || !supportsGeometryHardwareRendering(geometries))
	{
		ensureSoftwareRendererInitialized();
		mSoftwareRenderer.renderGameScreen(geometries);
		uploadGameScreenBitmap();
		return;
	}

	if (!beginHardwareFrame())
	{
		ensureSoftwareRendererInitialized();
		mSoftwareRenderer.renderGameScreen(geometries);
		uploadGameScreenBitmap();
		return;
	}

	const bool usingSpriteMask = isUsingSpriteMask(geometries);
	setScissorRect(Recti(0, 0, mGameResolution.x, mGameResolution.y));
	drawRectGeometryInternal(Recti(0, 0, mGameResolution.x, mGameResolution.y), renderParts.getPaletteManager().getBackdropColor(), vulkan::PipelineID::SIMPLE_COLORED_OPAQUE);

	uint16 lastRenderQueue = 0xffff;
	for (const Geometry* geometry : geometries)
	{
		if (nullptr == geometry)
			continue;

		const uint16 renderQueue = geometry->mRenderQueue;
		if (usingSpriteMask && lastRenderQueue < 0x8000 && renderQueue >= 0x8000)
		{
			if (!copyGameScreenToProcessingBuffer())
			{
				finishHardwareFrame();
				ensureSoftwareRendererInitialized();
				mSoftwareRenderer.renderGameScreen(geometries);
				uploadGameScreenBitmap();
				return;
			}
		}

		switch (geometry->getType())
		{
			case Geometry::Type::PLANE:
				drawPlaneGeometry(static_cast<const PlaneGeometry&>(*geometry));
				break;

			case Geometry::Type::SPRITE:
				drawSpriteGeometry(static_cast<const SpriteGeometry&>(*geometry));
				break;

			case Geometry::Type::RECT:
				drawRectGeometry(static_cast<const RectGeometry&>(*geometry));
				break;

			case Geometry::Type::TEXTURED_RECT:
				drawTexturedRectGeometry(static_cast<const TexturedRectGeometry&>(*geometry));
				break;

			case Geometry::Type::EFFECT_BLUR:
				drawBlur(static_cast<const EffectBlurGeometry&>(*geometry));
				break;

			case Geometry::Type::VIEWPORT:
				setScissorRect(static_cast<const ViewportGeometry&>(*geometry).mRect);
				break;

			default:
				break;
		}

		lastRenderQueue = renderQueue;
	}

	finishHardwareFrame();
}

void VulkanRenderer::renderDebugDraw(int debugDrawMode, const Recti& rect)
{
	ensureSoftwareRendererInitialized();
	mSoftwareRenderer.renderDebugDraw(debugDrawMode, rect);

	if (isHardwareMode())
	{
		uploadGameScreenBitmap();
	}
}

bool VulkanRenderer::isHardwareMode() const
{
	return (Configuration::instance().mRenderMethod == Configuration::RenderMethod::VULKAN_FULL);
}

bool VulkanRenderer::supportsGeometryHardwareRendering(const std::vector<Geometry*>& geometries) const
{
	for (const Geometry* geometry : geometries)
	{
		if (nullptr == geometry)
			continue;

		switch (geometry->getType())
		{
			case Geometry::Type::UNDEFINED:
				continue;

			case Geometry::Type::PLANE:
			case Geometry::Type::SPRITE:
			case Geometry::Type::RECT:
			case Geometry::Type::TEXTURED_RECT:
			case Geometry::Type::EFFECT_BLUR:
			case Geometry::Type::VIEWPORT:
				break;

			default:
				return false;
		}

		if (geometry->getType() == Geometry::Type::SPRITE)
		{
			const SpriteGeometry& spriteGeometry = static_cast<const SpriteGeometry&>(*geometry);
			if (!isBlendModeSupported(spriteGeometry.mSpriteInfo.mBlendMode))
				return false;
			if (spriteGeometry.mSpriteInfo.getType() == RenderItem::Type::SPRITE_MASK && !mProcessingTexture.isValid())
				return false;
		}
	}
	return true;
}

bool VulkanRenderer::isBlendModeSupported(BlendMode blendMode) const
{
	switch (blendMode)
	{
		case BlendMode::OPAQUE:
		case BlendMode::ALPHA:
		case BlendMode::ONE_BIT:
		case BlendMode::ADDITIVE:
		case BlendMode::SUBTRACTIVE:
		case BlendMode::MULTIPLICATIVE:
		case BlendMode::MINIMUM:
		case BlendMode::MAXIMUM:
			return true;

		default:
			return false;
	}
}

namespace
{
	VulkanDrawerTexture* ensureVulkanDrawerTextureReady(DrawerTexture& texture, VulkanDrawerResources& drawerResources)
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

	void assignMaterialTexture(vulkan::Descriptors::MaterialKey& materialKey, uint32 index, const vulkan::Texture& texture, VkSampler sampler)
	{
		materialKey.mImageViews[index] = texture.getImageView();
		materialKey.mSamplers[index] = sampler;
		materialKey.mVersions[index] = texture.getDescriptorVersion();
	}
}

bool VulkanRenderer::uploadGameScreenBitmap()
{
	VulkanDrawerTexture* outputTexture = ensureVulkanDrawerTextureReady(mGameScreenTexture, mDrawerResources);
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr == outputTexture || nullptr == backend)
		return false;

	const Bitmap& bitmap = mGameScreenTexture.getBitmap();
	if (bitmap.empty())
		return false;

	return outputTexture->mTexture.updateFromBitmap(backend->getDevice(), backend->getAllocator(), nullptr, bitmap, vulkan::OFFSCREEN_FORMAT);
}

bool VulkanRenderer::readBackGameScreenTexture()
{
	VulkanDrawerTexture* outputTexture = ensureVulkanDrawerTextureReady(mGameScreenTexture, mDrawerResources);
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr == outputTexture || nullptr == backend || !outputTexture->mTexture.isValid())
		return false;

	Bitmap bitmap;
	if (!outputTexture->mTexture.readToBitmap(backend->getDevice(), backend->getAllocator(), bitmap))
		return false;

	mGameScreenTexture.accessBitmap().swap(bitmap);
	return true;
}

bool VulkanRenderer::copyGameScreenToProcessingBuffer()
{
	VulkanDrawerTexture* outputTexture = ensureVulkanDrawerTextureReady(mGameScreenTexture, mDrawerResources);
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr == outputTexture || nullptr == backend || !outputTexture->mTexture.isValid() || !mProcessingTexture.isValid())
		return false;

	backend->endRenderingPass();
	backend->beginColorPass(mProcessingTexture, VK_ATTACHMENT_LOAD_OP_CLEAR);

	SimpleRectConstants copyConstants;
	copyConstants.mTransform = Vec4f(-1.0f, 1.0f, 2.0f, -2.0f);
	copyConstants.mTintColor = Color::WHITE;

	vulkan::Descriptors::MaterialKey materialKey;
	assignMaterialTexture(materialKey, 0, outputTexture->mTexture, mPointClampSampler);
	const Recti previousScissorRect = mCurrentScissorRect;
	setScissorRect(Recti(0, 0, mGameResolution.x, mGameResolution.y));
	const bool copyResult = drawPipeline(vulkan::PipelineID::COPY, &copyConstants, sizeof(copyConstants), 16, mQuadVertexBuffer.mBuffer, materialKey, 6);
	setScissorRect(previousScissorRect);
	if (!copyResult)
		return false;

	backend->endRenderingPass();
	backend->beginColorPass(outputTexture->mTexture, VK_ATTACHMENT_LOAD_OP_LOAD, &mDepthTexture);
	return true;
}

void VulkanRenderer::destroyHardwareResources()
{
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr == backend)
	{
		mHardwareResourcesInitialized = false;
		return;
	}

	vulkan::IAllocator& allocator = backend->getAllocator();
	vulkan::VulkanDevice& device = backend->getDevice();

	if (mPointClampSampler != VK_NULL_HANDLE)
	{
		device.vk().DestroySampler(device.getDevice(), mPointClampSampler, nullptr);
		mPointClampSampler = VK_NULL_HANDLE;
	}
	if (mLinearClampSampler != VK_NULL_HANDLE)
	{
		device.vk().DestroySampler(device.getDevice(), mLinearClampSampler, nullptr);
		mLinearClampSampler = VK_NULL_HANDLE;
	}

	if (mQuadVertexBuffer.mBuffer != VK_NULL_HANDLE)
		allocator.destroyBuffer(mQuadVertexBuffer);
	if (mTexturedQuadVertexBuffer.mBuffer != VK_NULL_HANDLE)
		allocator.destroyBuffer(mTexturedQuadVertexBuffer);
	if (mDynamicTexturedVertexBuffer.mBuffer != VK_NULL_HANDLE)
		allocator.destroyBuffer(mDynamicTexturedVertexBuffer);

	if (mDepthTexture.isValid())
		mDepthTexture.destroy(device, allocator);
	if (mProcessingTexture.isValid())
		mProcessingTexture.destroy(device, allocator);
	if (mTransientTexturedRectTexture.isValid())
		mTransientTexturedRectTexture.destroy(device, allocator);

	mHardwareResourcesInitialized = false;
	mIsRenderingToProcessingBuffer = false;
	mDynamicTexturedVertexCapacity = 0;
	mDynamicTexturedWriteOffset = 0;
}

bool VulkanRenderer::ensureHardwareResources()
{
	if (!isHardwareMode())
		return false;
	if (mHardwareResourcesInitialized)
		return true;

	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr == backend)
		return false;

	vulkan::VulkanDevice& device = backend->getDevice();
	vulkan::IAllocator& allocator = backend->getAllocator();

	if (!createSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, mPointClampSampler))
		return false;
	if (!createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, mLinearClampSampler))
		return false;
	if (!createQuadBuffers())
		return false;

	if (!mDepthTexture.isValid() || mDepthTexture.getSize() != mGameResolution)
	{
		if (mDepthTexture.isValid())
			mDepthTexture.destroy(device, allocator);
		if (!mDepthTexture.create(device, allocator, VK_FORMAT_D32_SFLOAT, mGameResolution,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, false, true))
		{
			return false;
		}
	}

	if (!mProcessingTexture.isValid() || mProcessingTexture.getSize() != mGameResolution)
	{
		if (mProcessingTexture.isValid())
			mProcessingTexture.destroy(device, allocator);
		if (!mProcessingTexture.create(device, allocator, vulkan::OFFSCREEN_FORMAT, mGameResolution,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT, true, true))
		{
			return false;
		}
	}

	mHardwareResourcesInitialized = true;
	return true;
}

bool VulkanRenderer::createSampler(VkFilter filter, VkSamplerAddressMode addressMode, VkSampler& outSampler)
{
	if (outSampler != VK_NULL_HANDLE)
		return true;

	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr == backend)
		return false;

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

	return (backend->getDevice().vk().CreateSampler(backend->getDevice().getDevice(), &samplerCreateInfo, nullptr, &outSampler) == VK_SUCCESS);
}

bool VulkanRenderer::createQuadBuffers()
{
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
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

bool VulkanRenderer::ensureDynamicTexturedBuffer(size_t requiredSize)
{
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr == backend)
		return false;

	if (mDynamicTexturedVertexBuffer.mBuffer != VK_NULL_HANDLE && mDynamicTexturedVertexCapacity >= requiredSize)
		return true;

	if (mDynamicTexturedVertexBuffer.mBuffer != VK_NULL_HANDLE)
	{
		if (backend->getCurrentFrame().isRecording())
		{
			// Recorded draws can still reference the old buffer until the frame fence signals.
			backend->getCurrentFrame().addPendingUpload(std::move(mDynamicTexturedVertexBuffer));
		}
		else
		{
			backend->getAllocator().destroyBuffer(mDynamicTexturedVertexBuffer);
		}
		mDynamicTexturedVertexCapacity = 0;
	}

	const VkDeviceSize allocationSize = std::max<VkDeviceSize>((VkDeviceSize)requiredSize, (VkDeviceSize)(256 * 1024));
	if (!backend->getAllocator().allocateBuffer(allocationSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mDynamicTexturedVertexBuffer))
		return false;

	mDynamicTexturedVertexCapacity = allocationSize;
	return true;
}

bool VulkanRenderer::beginHardwareFrame()
{
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	VulkanDrawerTexture* outputTexture = ensureVulkanDrawerTextureReady(mGameScreenTexture, mDrawerResources);
	if (nullptr == backend || nullptr == outputTexture || !outputTexture->mTexture.isValid())
		return false;

	if (!backend->beginOffscreenFrame(EngineMain::instance().getSDLWindow()))
		return false;

	backend->beginColorPass(outputTexture->mTexture, VK_ATTACHMENT_LOAD_OP_CLEAR, &mDepthTexture);
	mCurrentScissorRect = Recti(0, 0, mGameResolution.x, mGameResolution.y);
	mDynamicTexturedWriteOffset = 0;
	return true;
}

void VulkanRenderer::finishHardwareFrame()
{
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr == backend)
		return;

	backend->endRenderingPass();
	backend->submitOffscreenFrame(true);
}

void VulkanRenderer::setScissorRect(const Recti& rect)
{
	mCurrentScissorRect = rect;
}

void VulkanRenderer::drawRectGeometryInternal(const Recti& rect, const Color& color, vulkan::PipelineID pipelineId)
{
	SimpleRectConstants constants;
	constants.mTransform = Vec4f(
		-1.0f + 2.0f * (float)rect.x / (float)mGameResolution.x,
		 1.0f - 2.0f * (float)rect.y / (float)mGameResolution.y,
		 2.0f * (float)rect.width / (float)mGameResolution.x,
		-2.0f * (float)rect.height / (float)mGameResolution.y);
	constants.mTintColor = color;

	drawPipeline(pipelineId, &constants, sizeof(constants), 16, mQuadVertexBuffer.mBuffer, {}, 6);
}

void VulkanRenderer::drawRectGeometry(const RectGeometry& geometry)
{
	drawRectGeometryInternal(geometry.mRect, geometry.mColor, vulkan::PipelineID::SIMPLE_COLORED);
}

void VulkanRenderer::drawTexturedRectGeometry(const TexturedRectGeometry& geometry)
{
	VulkanDrawerTexture* texture = ensureVulkanDrawerTextureReady(geometry.mDrawerTexture, mDrawerResources);
	if (nullptr == texture || !texture->mTexture.isValid())
		return;

	SimpleRectConstants constants;
	constants.mTransform = Vec4f(
		-1.0f + 2.0f * (float)geometry.mRect.x / (float)mGameResolution.x,
		 1.0f - 2.0f * (float)geometry.mRect.y / (float)mGameResolution.y,
		 2.0f * (float)geometry.mRect.width / (float)mGameResolution.x,
		-2.0f * (float)geometry.mRect.height / (float)mGameResolution.y);
	constants.mTintColor = geometry.mTintColor;
	constants.mAddedColor = geometry.mAddedColor;
	constants.mTextureSize[0] = texture->mTexture.getSize().x;
	constants.mTextureSize[1] = texture->mTexture.getSize().y;
	constants.mAlphaTest = 1;

	vulkan::Descriptors::MaterialKey materialKey;
	assignMaterialTexture(materialKey, 0, texture->mTexture, mPointClampSampler);

	drawPipeline(vulkan::PipelineID::SIMPLE_TEXTURED_ALPHA, &constants, sizeof(constants), 16, mQuadVertexBuffer.mBuffer, materialKey, 6);
}

void VulkanRenderer::drawPlaneGeometry(const PlaneGeometry& geometry)
{
	if (!PlaneManager::isRenderablePlaneIndex(geometry.mPlaneIndex))
	{
		static int sLoggedInvalidPlaneGeometryCount = 0;
		if (sLoggedInvalidPlaneGeometryCount < 8)
		{
			++sLoggedInvalidPlaneGeometryCount;
			RMX_LOG_INFO("VulkanRenderer: skipping invalid plane geometry with plane index " << geometry.mPlaneIndex
				<< ", rect=(" << geometry.mActiveRect.x << "," << geometry.mActiveRect.y << "," << geometry.mActiveRect.width << "," << geometry.mActiveRect.height
				<< "), scrollOffsets=" << (int)geometry.mScrollOffsets << ", renderQueue=0x" << rmx::hexString(geometry.mRenderQueue, 4));
		}
		return;
	}

	RenderParts& renderParts = RenderParts::instance();
	const ScrollOffsetsManager& scrollOffsetsManager = renderParts.getScrollOffsetsManager();
	const PaletteManager& paletteManager = renderParts.getPaletteManager();

	PlaneConstants constants;
	constants.mActiveRect[0] = geometry.mActiveRect.x;
	constants.mActiveRect[1] = geometry.mActiveRect.y;
	constants.mActiveRect[2] = geometry.mActiveRect.width;
	constants.mActiveRect[3] = geometry.mActiveRect.height;
	constants.mGameResolution[0] = mGameResolution.x;
	constants.mGameResolution[1] = mGameResolution.y;
	const Vec4i playfieldSize = (geometry.mPlaneIndex <= PlaneManager::PLANE_A) ? renderParts.getPlaneManager().getPlayfieldSizeForShaders() : Vec4i(512, 256, 64, 32);
	constants.mPlayfieldSize[0] = playfieldSize.x;
	constants.mPlayfieldSize[1] = playfieldSize.y;
	constants.mPlayfieldSize[2] = playfieldSize.z;
	constants.mPlayfieldSize[3] = playfieldSize.w;
	constants.mPriorityFlag = geometry.mPriorityFlag ? 1 : 0;
	constants.mUseHorizontalScrolling = (geometry.mPlaneIndex != PlaneManager::PLANE_W) ? 1 : 0;
	constants.mUseVerticalScrolling = (geometry.mPlaneIndex != PlaneManager::PLANE_W && scrollOffsetsManager.getVerticalScrolling()) ? 1 : 0;
	constants.mNoRepeat = scrollOffsetsManager.getHorizontalScrollNoRepeat(geometry.mScrollOffsets) ? 1 : 0;
	constants.mVScrollOffsetBias = scrollOffsetsManager.getVerticalScrollOffsetBias();
	if (geometry.mPlaneIndex == PlaneManager::PLANE_W)
	{
		const Vec2i& scrollOffset = scrollOffsetsManager.getPlaneWScrollOffset();
		constants.mUseHorizontalScrolling = 0;
		constants.mUseVerticalScrolling = 0;
		constants.mScrollOffsetX = scrollOffset.x;
		constants.mScrollOffsetY = scrollOffset.y;
	}
	else
	{
		constants.mScrollOffsetX = scrollOffsetsManager.getScrollOffsetsH(geometry.mScrollOffsets)[0];
		constants.mScrollOffsetY = scrollOffsetsManager.getScrollOffsetsV(geometry.mScrollOffsets)[0];
	}

	vulkan::Descriptors::MaterialKey materialKey;
	assignMaterialTexture(materialKey, 0, mRenderResources.getPatternCacheTexture(), mPointClampSampler);
	assignMaterialTexture(materialKey, 1, mRenderResources.getMainPaletteTexture(), mPointClampSampler);
	assignMaterialTexture(materialKey, 2, mRenderResources.getPlanePatternsTexture(geometry.mPlaneIndex), mPointClampSampler);
	assignMaterialTexture(materialKey, 3, mRenderResources.getHScrollOffsetsTexture(geometry.mScrollOffsets), mPointClampSampler);
	assignMaterialTexture(materialKey, 4, mRenderResources.getVScrollOffsetsTexture(geometry.mScrollOffsets), mPointClampSampler);

	const int splitY = paletteManager.mSplitPositionY;
	if (splitY > geometry.mActiveRect.y && splitY < geometry.mActiveRect.y + geometry.mActiveRect.height)
	{
		PlaneConstants upperConstants = constants;
		upperConstants.mActiveRect[3] = splitY - geometry.mActiveRect.y;
		upperConstants.mPaletteOffset = 0.0f;
		drawPipeline(vulkan::PipelineID::PLANE, &upperConstants, sizeof(upperConstants), 16, mQuadVertexBuffer.mBuffer, materialKey, 6);

		PlaneConstants lowerConstants = constants;
		lowerConstants.mActiveRect[1] = splitY;
		lowerConstants.mActiveRect[3] = geometry.mActiveRect.y + geometry.mActiveRect.height - splitY;
		lowerConstants.mPaletteOffset = 0.5f;
		drawPipeline(vulkan::PipelineID::PLANE, &lowerConstants, sizeof(lowerConstants), 16, mQuadVertexBuffer.mBuffer, materialKey, 6);
	}
	else
	{
		constants.mPaletteOffset = (geometry.mActiveRect.y >= splitY) ? 0.5f : 0.0f;
		drawPipeline(vulkan::PipelineID::PLANE, &constants, sizeof(constants), 16, mQuadVertexBuffer.mBuffer, materialKey, 6);
	}
}

void VulkanRenderer::drawSpriteGeometry(const SpriteGeometry& geometry)
{
	switch (geometry.mSpriteInfo.getType())
	{
		case RenderItem::Type::VDP_SPRITE:
			drawVdpSprite(static_cast<const renderitems::VdpSpriteInfo&>(geometry.mSpriteInfo));
			break;

		case RenderItem::Type::PALETTE_SPRITE:
			drawPaletteSprite(static_cast<const renderitems::PaletteSpriteInfo&>(geometry.mSpriteInfo));
			break;

		case RenderItem::Type::COMPONENT_SPRITE:
			drawComponentSprite(static_cast<const renderitems::ComponentSpriteInfo&>(geometry.mSpriteInfo));
			break;

		case RenderItem::Type::SPRITE_MASK:
			drawSpriteMask(static_cast<const renderitems::SpriteMaskInfo&>(geometry.mSpriteInfo));
			break;

		default:
			break;
	}
}

void VulkanRenderer::drawVdpSprite(const renderitems::VdpSpriteInfo& spriteInfo)
{
	if (!isBlendModeSupported(spriteInfo.mBlendMode))
		return;

	RenderParts& renderParts = RenderParts::instance();
	VdpSpriteConstants constants;
	constants.mSize[0] = spriteInfo.mSize.x;
	constants.mSize[1] = spriteInfo.mSize.y;
	constants.mFirstPattern = spriteInfo.mFirstPattern;
	constants.mPosition[0] = spriteInfo.mInterpolatedPosition.x;
	constants.mPosition[1] = spriteInfo.mInterpolatedPosition.y;
	constants.mPosition[2] = spriteInfo.mPriorityFlag ? 1 : 0;
	constants.mWaterLevel = renderParts.getPaletteManager().mSplitPositionY;
	constants.mGameResolution[0] = mGameResolution.x;
	constants.mGameResolution[1] = mGameResolution.y;
	const PaletteManager& paletteManager = renderParts.getPaletteManager();
	constants.mTintColor = spriteInfo.mTintColor;
	constants.mAddedColor = spriteInfo.mAddedColor;
	constants.mShadowHighlightMode = paletteManager.useShadowHighlightMode() ? 1 : 0;
	if (spriteInfo.mUseGlobalComponentTint)
		paletteManager.applyGlobalComponentTint(constants.mTintColor, constants.mAddedColor);

	vulkan::Descriptors::MaterialKey materialKey;
	assignMaterialTexture(materialKey, 0, mRenderResources.getPatternCacheTexture(), mPointClampSampler);
	assignMaterialTexture(materialKey, 1, mRenderResources.getMainPaletteTexture(), mPointClampSampler);

	drawPipeline(getVdpSpritePipelineId(spriteInfo.mBlendMode), &constants, sizeof(constants), 16, mQuadVertexBuffer.mBuffer, materialKey, 6);
}

void VulkanRenderer::drawPaletteSprite(const renderitems::PaletteSpriteInfo& spriteInfo)
{
	if (nullptr == spriteInfo.mCacheItem || spriteInfo.mSize.x == 0 || spriteInfo.mSize.y == 0 || !isBlendModeSupported(spriteInfo.mBlendMode))
		return;

	RenderParts& renderParts = RenderParts::instance();
	const vulkan::Texture* texture = mRenderResources.getPaletteSpriteTexture(*spriteInfo.mCacheItem, spriteInfo.mUseUpscaledSprite);
	if (nullptr == texture || !texture->isValid())
		return;

	CustomSpriteConstants constants;
	constants.mSize[0] = spriteInfo.mSize.x;
	constants.mSize[1] = spriteInfo.mSize.y;
	constants.mAlphaTest = (spriteInfo.mBlendMode != BlendMode::OPAQUE) ? 1 : 0;
	constants.mPosition[0] = spriteInfo.mInterpolatedPosition.x;
	constants.mPosition[1] = spriteInfo.mInterpolatedPosition.y;
	constants.mPosition[2] = spriteInfo.mPriorityFlag ? 1 : 0;
	constants.mWaterLevel = renderParts.getPaletteManager().mSplitPositionY;
	constants.mPivotOffset[0] = spriteInfo.mPivotOffset.x;
	constants.mPivotOffset[1] = spriteInfo.mPivotOffset.y;
	constants.mGameResolution[0] = mGameResolution.x;
	constants.mGameResolution[1] = mGameResolution.y;
	constants.mTransformation = spriteInfo.mTransformation.mMatrix;
	const PaletteManager& paletteManager = renderParts.getPaletteManager();
	constants.mTintColor = spriteInfo.mTintColor;
	constants.mAddedColor = spriteInfo.mAddedColor;
	constants.mShadowHighlightMode = paletteManager.useShadowHighlightMode() ? 1 : 0;
	if (spriteInfo.mUseGlobalComponentTint)
		paletteManager.applyGlobalComponentTint(constants.mTintColor, constants.mAddedColor);
	constants.mAtex = spriteInfo.mAtex;

	const vulkan::Texture& paletteTexture = mRenderResources.getPaletteTexture(spriteInfo.mPrimaryPalette, spriteInfo.mSecondaryPalette);
	vulkan::Descriptors::MaterialKey materialKey;
	assignMaterialTexture(materialKey, 0, *texture, mPointClampSampler);
	assignMaterialTexture(materialKey, 1, paletteTexture, mPointClampSampler);

	drawPipeline(getPaletteSpritePipelineId(spriteInfo.mBlendMode), &constants, sizeof(constants), 16, mQuadVertexBuffer.mBuffer, materialKey, 6);
}

void VulkanRenderer::drawComponentSprite(const renderitems::ComponentSpriteInfo& spriteInfo)
{
	if (nullptr == spriteInfo.mCacheItem || !isBlendModeSupported(spriteInfo.mBlendMode))
		return;

	RenderParts& renderParts = RenderParts::instance();
	const vulkan::Texture* texture = mRenderResources.getComponentSpriteTexture(*spriteInfo.mCacheItem);
	if (nullptr == texture || !texture->isValid())
		return;

	CustomSpriteConstants constants;
	constants.mSize[0] = spriteInfo.mSize.x;
	constants.mSize[1] = spriteInfo.mSize.y;
	constants.mAlphaTest = (spriteInfo.mBlendMode != BlendMode::OPAQUE) ? 1 : 0;
	constants.mPosition[0] = spriteInfo.mInterpolatedPosition.x;
	constants.mPosition[1] = spriteInfo.mInterpolatedPosition.y;
	constants.mPosition[2] = spriteInfo.mPriorityFlag ? 1 : 0;
	constants.mPivotOffset[0] = spriteInfo.mPivotOffset.x;
	constants.mPivotOffset[1] = spriteInfo.mPivotOffset.y;
	constants.mGameResolution[0] = mGameResolution.x;
	constants.mGameResolution[1] = mGameResolution.y;
	constants.mTransformation = spriteInfo.mTransformation.mMatrix;
	const PaletteManager& paletteManager = renderParts.getPaletteManager();
	constants.mTintColor = spriteInfo.mTintColor;
	constants.mAddedColor = spriteInfo.mAddedColor;
	if (spriteInfo.mUseGlobalComponentTint)
		paletteManager.applyGlobalComponentTint(constants.mTintColor, constants.mAddedColor);

	vulkan::Descriptors::MaterialKey materialKey;
	assignMaterialTexture(materialKey, 0, *texture, mPointClampSampler);

	drawPipeline(getComponentSpritePipelineId(spriteInfo.mBlendMode), &constants, sizeof(constants), 16, mQuadVertexBuffer.mBuffer, materialKey, 6);
}

void VulkanRenderer::drawSpriteMask(const renderitems::SpriteMaskInfo& spriteInfo)
{
	if (!mProcessingTexture.isValid())
		return;

	const Vec2f uv0((float)spriteInfo.mPosition.x / (float)mGameResolution.x, (float)spriteInfo.mPosition.y / (float)mGameResolution.y);
	const Vec2f uv1((float)(spriteInfo.mPosition.x + spriteInfo.mSize.x) / (float)mGameResolution.x, (float)(spriteInfo.mPosition.y + spriteInfo.mSize.y) / (float)mGameResolution.y);
	const float vertexData[] =
	{
		0.0f, 0.0f, uv0.x, uv0.y,
		0.0f, 1.0f, uv0.x, uv1.y,
		1.0f, 1.0f, uv1.x, uv1.y,
		1.0f, 1.0f, uv1.x, uv1.y,
		1.0f, 0.0f, uv1.x, uv0.y,
		0.0f, 0.0f, uv0.x, uv0.y
	};
	const size_t vertexDataSize = sizeof(vertexData);
	if (!ensureDynamicTexturedBuffer((size_t)(mDynamicTexturedWriteOffset + vertexDataSize)))
		return;

	const VkDeviceSize vertexOffset = mDynamicTexturedWriteOffset;
	std::memcpy((uint8*)mDynamicTexturedVertexBuffer.mMappedData + vertexOffset, vertexData, vertexDataSize);
	mDynamicTexturedWriteOffset += vertexDataSize;

	SimpleRectConstants constants;
	constants.mTransform = Vec4f(
		-1.0f + 2.0f * (float)spriteInfo.mPosition.x / (float)mGameResolution.x,
		 1.0f - 2.0f * (float)spriteInfo.mPosition.y / (float)mGameResolution.y,
		 2.0f * (float)spriteInfo.mSize.x / (float)mGameResolution.x,
		-2.0f * (float)spriteInfo.mSize.y / (float)mGameResolution.y);
	constants.mTintColor = Color::WHITE;

	vulkan::Descriptors::MaterialKey materialKey;
	assignMaterialTexture(materialKey, 0, mProcessingTexture, mPointClampSampler);
	drawPipeline(vulkan::PipelineID::SIMPLE_TEXTURED_UV_OPAQUE, &constants, sizeof(constants), 16, mDynamicTexturedVertexBuffer.mBuffer, materialKey, 6, vertexOffset);
}

void VulkanRenderer::drawBlur(const EffectBlurGeometry& geometry)
{
	VulkanDrawerTexture* outputTexture = ensureVulkanDrawerTextureReady(mGameScreenTexture, mDrawerResources);
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr == outputTexture || nullptr == backend || !outputTexture->mTexture.isValid() || !mProcessingTexture.isValid())
		return;

	backend->endRenderingPass();
	backend->beginColorPass(mProcessingTexture, VK_ATTACHMENT_LOAD_OP_CLEAR);

	FullscreenConstants constants;
	constants.mTexelOffset = Vec2f(1.0f / (float)mGameResolution.x, 1.0f / (float)mGameResolution.y);
	constants.mKernel = getBlurKernel(geometry.mBlurValue);

	vulkan::Descriptors::MaterialKey materialKey;
	assignMaterialTexture(materialKey, 0, outputTexture->mTexture, mPointClampSampler);
	const Recti previousScissorRect = mCurrentScissorRect;
	setScissorRect(Recti(0, 0, mGameResolution.x, mGameResolution.y));
	drawPipeline(vulkan::PipelineID::BLUR, &constants, sizeof(constants), 16, mQuadVertexBuffer.mBuffer, materialKey, 6);

	backend->endRenderingPass();
	backend->beginColorPass(outputTexture->mTexture, VK_ATTACHMENT_LOAD_OP_CLEAR, &mDepthTexture);

	SimpleRectConstants copyConstants;
	copyConstants.mTransform = Vec4f(-1.0f, 1.0f, 2.0f, -2.0f);
	copyConstants.mTintColor = Color::WHITE;

	materialKey = {};
	assignMaterialTexture(materialKey, 0, mProcessingTexture, mPointClampSampler);
	drawPipeline(vulkan::PipelineID::COPY, &copyConstants, sizeof(copyConstants), 16, mQuadVertexBuffer.mBuffer, materialKey, 6);
	setScissorRect(previousScissorRect);
}

bool VulkanRenderer::drawPipeline(vulkan::PipelineID pipelineId, const void* uniformData, size_t uniformSize, size_t uniformAlignment, VkBuffer vertexBuffer, const vulkan::Descriptors::MaterialKey& materialKey, uint32 vertexCount, VkDeviceSize vertexOffset)
{
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr == backend || nullptr == uniformData || vertexBuffer == VK_NULL_HANDLE || vertexCount == 0)
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

	VkPipeline pipeline = backend->getPipelines().getPipeline(pipelineId);
	if (pipeline == VK_NULL_HANDLE)
		return false;

	VkCommandBuffer commandBuffer = frame.getCommandBuffer();
	const VkViewport viewport = { 0.0f, 0.0f, (float)mGameResolution.x, (float)mGameResolution.y, 0.0f, 1.0f };
	VkViewport flippedViewport = viewport;
	flippedViewport.y = (float)mGameResolution.y;
	flippedViewport.height = -(float)mGameResolution.y;
	backend->getDevice().vk().CmdSetViewport(commandBuffer, 0, 1, &flippedViewport);

	Recti scissorRect = mCurrentScissorRect;
	scissorRect.x = clamp(scissorRect.x, 0, mGameResolution.x);
	scissorRect.y = clamp(scissorRect.y, 0, mGameResolution.y);
	scissorRect.width = clamp(scissorRect.width, 0, mGameResolution.x - scissorRect.x);
	scissorRect.height = clamp(scissorRect.height, 0, mGameResolution.y - scissorRect.y);

	VkRect2D scissor = {};
	scissor.offset.x = scissorRect.x;
	scissor.offset.y = scissorRect.y;
	scissor.extent.width = (uint32)std::max(scissorRect.width, 0);
	scissor.extent.height = (uint32)std::max(scissorRect.height, 0);
	backend->getDevice().vk().CmdSetScissor(commandBuffer, 0, 1, &scissor);

	backend->getDevice().vk().CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	backend->getDevice().vk().CmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, backend->getPipelines().getPipelineLayout(), 0, 2, descriptorSets, 1, &uniformSlice.mDynamicOffset);

	backend->getDevice().vk().CmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
	backend->getDevice().vk().CmdDraw(commandBuffer, vertexCount, 1, 0, 0);
	return true;
}

#endif
