/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)

#include "oxygen/rendering/Renderer.h"
#include "oxygen/rendering/Geometry.h"
#include "oxygen/rendering/software/SoftwareRenderer.h"
#include "oxygen/rendering/vulkan/Pipeline.h"
#include "oxygen/rendering/vulkan/VulkanRenderResources.h"

class VulkanDrawerResources;
class RenderParts;

namespace renderitems
{
	struct VdpSpriteInfo;
	struct PaletteSpriteInfo;
	struct ComponentSpriteInfo;
	struct SpriteMaskInfo;
}


class VulkanRenderer : public Renderer
{
public:
	static constexpr int8 RENDERER_TYPE_ID = 0x40;

public:
	VulkanRenderer(RenderParts& renderParts, DrawerTexture& outputTexture);
	~VulkanRenderer() override;

	void initialize() override;
	void reset() override;
	void setGameResolution(const Vec2i& gameResolution) override;
	void clearGameScreen() override;
	void renderGameScreen(const std::vector<Geometry*>& geometries) override;
	void renderDebugDraw(int debugDrawMode, const Recti& rect) override;

private:
	struct FullscreenConstants
	{
		Vec2f mTexelOffset = Vec2f(0.0f, 0.0f);
		Vec2f mPadding0 = Vec2f(0.0f, 0.0f);
		Vec4f mKernel = Vec4f(1.0f, 0.0f, 0.0f, 0.0f);
	};

	struct SimpleRectConstants
	{
		Vec4f mTransform = Vec4f(0.0f, 0.0f, 0.0f, 0.0f);
		Vec4f mTintColor = Color::WHITE;
		Vec4f mAddedColor = Color::TRANSPARENT;
		int32 mTextureSize[2] = { 0, 0 };
		int32 mAlphaTest = 0;
		int32 mShadowHighlightMode = 0;
	};

	struct PlaneConstants
	{
		int32 mActiveRect[4] = { 0, 0, 0, 0 };
		int32 mGameResolution[2] = { 0, 0 };
		float mPaletteOffset = 0.0f;
		int32 mPriorityFlag = 0;
		int32 mPlayfieldSize[4] = { 0, 0, 0, 0 };
		int32 mVScrollOffsetBias = 0;
		int32 mScrollOffsetX = 0;
		int32 mScrollOffsetY = 0;
		int32 mUseHorizontalScrolling = 0;
		int32 mUseVerticalScrolling = 0;
		int32 mNoRepeat = 0;
		int32 mPadding0 = 0;
		int32 mPadding1 = 0;
		int32 mPadding2 = 0;
	};

	struct VdpSpriteConstants
	{
		int32 mSize[2] = { 0, 0 };
		int32 mFirstPattern = 0;
		int32 mPadding0 = 0;
		int32 mPosition[3] = { 0, 0, 0 };
		int32 mWaterLevel = 0;
		int32 mGameResolution[2] = { 0, 0 };
		int32 mPadding1[2] = { 0, 0 };
		Vec4f mTintColor = Color::WHITE;
		Vec4f mAddedColor = Color::TRANSPARENT;
		int32 mShadowHighlightMode = 0;
		int32 mPadding2[3] = { 0, 0, 0 };
	};

	struct CustomSpriteConstants
	{
		int32 mSize[2] = { 0, 0 };
		int32 mAlphaTest = 0;
		int32 mPadding0 = 0;
		int32 mPosition[3] = { 0, 0, 0 };
		int32 mWaterLevel = 0;
		int32 mPivotOffset[2] = { 0, 0 };
		int32 mGameResolution[2] = { 0, 0 };
		Vec4f mTransformation = Vec4f(1.0f, 0.0f, 0.0f, 1.0f);
		Vec4f mTintColor = Color::WHITE;
		Vec4f mAddedColor = Color::TRANSPARENT;
		int32 mAtex = 0;
		int32 mShadowHighlightMode = 0;
		int32 mPadding1[2] = { 0, 0 };
	};

private:
	bool isHardwareMode() const;
	void ensureSoftwareRendererInitialized();
	bool supportsGeometryHardwareRendering(const std::vector<Geometry*>& geometries) const;
	bool isBlendModeSupported(BlendMode blendMode) const;
	bool uploadGameScreenBitmap();
	bool readBackGameScreenTexture();
	bool copyGameScreenToProcessingBuffer();
	void destroyHardwareResources();
	bool ensureHardwareResources();
	bool createSampler(VkFilter filter, VkSamplerAddressMode addressMode, VkSampler& outSampler);
	bool createQuadBuffers();
	bool ensureDynamicTexturedBuffer(size_t requiredSize);
	void syncGameScreenBitmap();
	bool beginHardwareFrame();
	void finishHardwareFrame();
	void setScissorRect(const Recti& rect);
	void drawRectGeometryInternal(const Recti& rect, const Color& color, vulkan::PipelineID pipelineId);
	void drawRectGeometry(const RectGeometry& geometry);
	void drawTexturedRectGeometry(const TexturedRectGeometry& geometry);
	void drawPlaneGeometry(const PlaneGeometry& geometry);
	void drawSpriteGeometry(const SpriteGeometry& geometry);
	void drawVdpSprite(const renderitems::VdpSpriteInfo& spriteInfo);
	void drawPaletteSprite(const renderitems::PaletteSpriteInfo& spriteInfo);
	void drawComponentSprite(const renderitems::ComponentSpriteInfo& spriteInfo);
	void drawSpriteMask(const renderitems::SpriteMaskInfo& spriteInfo);
	void drawBlur(const EffectBlurGeometry& geometry);
	bool drawPipeline(vulkan::PipelineID pipelineId, const void* uniformData, size_t uniformSize, size_t uniformAlignment, VkBuffer vertexBuffer, const vulkan::Descriptors::MaterialKey& materialKey, uint32 vertexCount, VkDeviceSize vertexOffset = 0);

private:
	VulkanDrawerResources& mDrawerResources;
	VulkanRenderResources mRenderResources;
	SoftwareRenderer mSoftwareRenderer;
	vulkan::BufferAllocation mQuadVertexBuffer;
	vulkan::BufferAllocation mTexturedQuadVertexBuffer;
	vulkan::BufferAllocation mDynamicTexturedVertexBuffer;
	VkDeviceSize mDynamicTexturedVertexCapacity = 0;
	VkDeviceSize mDynamicTexturedWriteOffset = 0;
	vulkan::Texture mDepthTexture;
	vulkan::Texture mProcessingTexture;
	vulkan::Texture mTransientTexturedRectTexture;
	VkSampler mPointClampSampler = VK_NULL_HANDLE;
	VkSampler mLinearClampSampler = VK_NULL_HANDLE;
	Vec2i mGameResolution;
	Recti mCurrentScissorRect;
	bool mSoftwareRendererInitialized = false;
	bool mHardwareResourcesInitialized = false;
	bool mIsRenderingToProcessingBuffer = false;
};

#endif
