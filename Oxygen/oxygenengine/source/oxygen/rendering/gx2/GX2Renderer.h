/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen/rendering/Renderer.h"
#include "oxygen/rendering/Geometry.h"
#include "oxygen/rendering/gx2/GX2RenderResources.h"
#include "oxygen/rendering/software/SoftwareRenderer.h"
#include "oxygen/drawing/DrawerTexture.h"


class Drawer;

class GX2Renderer final : public Renderer
{
public:
	static constexpr int8 RENDERER_TYPE_ID = 0x50;

public:
	GX2Renderer(RenderParts& renderParts, DrawerTexture& outputTexture);

	void initialize() override;
	void reset() override;
	void setGameResolution(const Vec2i& gameResolution) override;
	void clearGameScreen() override;
	void renderGameScreen(const std::vector<Geometry*>& geometries) override;
	void renderDebugDraw(int debugDrawMode, const Recti& rect) override;
	void renderGameScreenToCurrentTarget(const std::vector<Geometry*>& geometries, const Recti& targetRect);
	bool canDrawPresentedGameScreenToCurrentTarget() const;
	bool drawPresentedGameScreenToCurrentTarget(const Recti& targetRect);

private:
	void ensureSoftwareRendererInitialized();
	bool shouldUseSoftwareGameRendering(const std::vector<Geometry*>& geometries) const;
	bool supportsNativeRendering(const std::vector<Geometry*>& geometries) const;
	bool supportsNativeGeometry(const Geometry& geometry) const;
	bool supportsNativeSprite(const SpriteGeometry& geometry) const;
	bool requiresOffscreenNativeRendering(const std::vector<Geometry*>& geometries) const;
	void renderNativeGameScreen(const std::vector<Geometry*>& geometries);
	void renderHybridGameScreen(const std::vector<Geometry*>& geometries);
	DrawerTexture& updateGameScreenPresentTexture();
	void ensureProcessingTexture();
	void copyNativeGameScreenToProcessingTexture(const Recti& viewport);
	void resetNativeBlurProcessingState();
	void beginNativeBlurProcessingTarget(Drawer& drawer, bool outputIsWindow, const Recti& outputViewport);
	void restoreNativeBlurOutputTarget(Drawer& drawer);
	void resetNativeQueuedState();
	void setNativeBlendMode(Drawer& drawer, BlendMode blendMode);
	void prepareHybridOverlayGeometries(const std::vector<Geometry*>& geometries, std::vector<Geometry*>& outSoftwareGeometries);
	bool canPinNativeOverlayGeometry(const Geometry& geometry, bool allowDepthSensitiveSprites) const;
	bool canDrawNativeOverlayGeometry(const Geometry& geometry, bool allowDepthSensitiveSprites) const;
	bool isDepthWritingGeometry(const Geometry& geometry) const;
	void drawNativeGeometry(const Geometry& geometry, bool& scissorActive);
	void drawNativePlane(const PlaneGeometry& geometry);
	void drawNativeSprite(const SpriteGeometry& geometry);
	void drawNativeVdpSprite(const renderitems::VdpSpriteInfo& spriteInfo);
	void drawNativeComponentSprite(const renderitems::ComponentSpriteInfo& spriteInfo);
	void drawNativePaletteSprite(const renderitems::PaletteSpriteInfo& spriteInfo);
	void drawNativeSpriteMask(const renderitems::SpriteMaskInfo& spriteInfo);
	void drawNativeBlur(const EffectBlurGeometry& geometry);
	void invalidateNativeRenderTarget();

private:
	SoftwareRenderer mSoftwareRenderer;
	GX2RenderResources mRenderResources;
	Vec2i mGameResolution;
	bool mSoftwareRendererInitialized = false;
	bool mNativeRenderTargetReady = false;
	bool mCurrentTargetAlreadyHasNativeFrame = false;
	DrawerTexture mProcessingTexture;
	DrawerTexture mGameScreenPresentTextures[2];
	uint32 mGameScreenPresentTextureIndex = 0;
	std::vector<Geometry*> mNativeOverlayGeometries;
	Recti mNativeOverlayInitialViewport;
	bool mNativeOverlayUsesInitialViewport = false;
	bool mHasNativeQueuedBlendMode = false;
	BlendMode mNativeQueuedBlendMode = BlendMode::OPAQUE;
	bool mNativeBlurProcessingActive = false;
	bool mNativeBlurOutputIsWindow = false;
	Recti mNativeBlurOutputViewport;
};
