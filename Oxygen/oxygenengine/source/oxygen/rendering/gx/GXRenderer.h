/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen/rendering/Renderer.h"
#include "oxygen/drawing/DrawerTexture.h"

class Drawer;
class PlaneGeometry;
class SpriteGeometry;
namespace renderitems
{
	struct VdpSpriteInfo;
	struct PaletteSpriteInfo;
	struct ComponentSpriteInfo;
	struct SpriteMaskInfo;
}


class GXRenderer final : public Renderer
{
public:
	static constexpr int8 RENDERER_TYPE_ID = 0x50;

public:
	GXRenderer(RenderParts& renderParts, DrawerTexture& outputTexture);

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
	struct CachedTexture
	{
		DrawerTexture mTexture;
		uint32 mLastUsedFrame = 0;
	};

private:
	void beginFrameCaches();
	void clearFrameCaches();
	void pruneFrameCaches();
	void drawGeometry(const Geometry& geometry, bool& scissorActive);
	void drawPlane(const PlaneGeometry& geometry);
	void drawSprite(const SpriteGeometry& geometry);
	void drawVdpSprite(const renderitems::VdpSpriteInfo& spriteInfo);
	void drawPaletteSprite(const renderitems::PaletteSpriteInfo& spriteInfo);
	void drawComponentSprite(const renderitems::ComponentSpriteInfo& spriteInfo);
	void drawSpriteMask(const renderitems::SpriteMaskInfo& spriteInfo);

	DrawerTexture& getPatternTexture(uint16 patternIndex, int paletteIndex);
	DrawerTexture& getPaletteSpriteTexture(const renderitems::PaletteSpriteInfo& spriteInfo);
	DrawerTexture& getComponentSpriteTexture(const renderitems::ComponentSpriteInfo& spriteInfo);

private:
	Vec2i mGameResolution;
	Recti mCurrentViewport;
	bool mCurrentTargetAlreadyHasNativeFrame = false;
	uint32 mFrameCacheGeneration = 0;
	std::unordered_map<uint64, std::unique_ptr<CachedTexture>> mPatternTextures;
	std::unordered_map<uint64, std::unique_ptr<CachedTexture>> mPaletteSpriteTextures;
	std::unordered_map<uint64, std::unique_ptr<CachedTexture>> mComponentSpriteTextures;
};
