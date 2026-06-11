/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/rendering/gx/GXRenderer.h"

#include "oxygen/application/EngineMain.h"
#include "oxygen/drawing/Drawer.h"
#include "oxygen/helper/Logging.h"
#include "oxygen/rendering/Geometry.h"
#include "oxygen/rendering/parts/PatternManager.h"
#include "oxygen/rendering/parts/PlaneManager.h"
#include "oxygen/rendering/parts/RenderItem.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/rendering/parts/ScrollOffsetsManager.h"
#include "oxygen/rendering/parts/palette/PaletteManager.h"
#include "oxygen/rendering/sprite/ComponentSprite.h"
#include "oxygen/rendering/sprite/PaletteSprite.h"


namespace
{
	static constexpr uint32 GX_PATTERN_CACHE_MAX_AGE = 300;
	static constexpr uint32 GX_CUSTOM_SPRITE_CACHE_MAX_AGE = 120;

	bool isBlendModeSupported(BlendMode blendMode)
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

	uint64 hashBytes(uint64 seed, const void* data, size_t size)
	{
		const uint8* bytes = static_cast<const uint8*>(data);
		for (size_t i = 0; i < size; ++i)
		{
			seed ^= (uint64)bytes[i];
			seed *= 1099511628211ull;
		}
		return seed;
	}

	uint64 hashPointer(uint64 seed, const void* ptr)
	{
		const uint64 value = (uint64)(uintptr_t)ptr;
		return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
	}

	uint64 hashPaletteState(uint64 seed, const PaletteBase& palette)
	{
		seed ^= palette.getKey() + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
		const uint16 changeCounter = palette.getChangeCounter();
		return hashBytes(seed, &changeCounter, sizeof(changeCounter));
	}

	uint64 hashPatternState(uint64 seed, const PatternManager::CacheItem::Pattern& pattern)
	{
		return hashBytes(seed, pattern.mPixels, sizeof(pattern.mPixels));
	}

	void applySpriteTint(const renderitems::SpriteInfo& spriteInfo, Color& tintColor, Color& addedColor)
	{
		tintColor = spriteInfo.mTintColor;
		addedColor = spriteInfo.mAddedColor;
		if (spriteInfo.mUseGlobalComponentTint)
		{
			RenderParts::instance().getPaletteManager().applyGlobalComponentTint(tintColor, addedColor);
		}
	}

	void appendSpriteQuad(std::vector<DrawerMeshVertex>& vertices, const renderitems::CustomSpriteInfoBase& spriteInfo)
	{
		const float width = (float)spriteInfo.mSize.x;
		const float height = (float)spriteInfo.mSize.y;
		const Vec2f pivot((float)spriteInfo.mPivotOffset.x, (float)spriteInfo.mPivotOffset.y);
		const Vec2f position((float)spriteInfo.mInterpolatedPosition.x, (float)spriteInfo.mInterpolatedPosition.y);

		const Vec2f localPositions[6] =
		{
			Vec2f(0.0f, 0.0f), Vec2f(0.0f, height), Vec2f(width, height),
			Vec2f(width, height), Vec2f(width, 0.0f), Vec2f(0.0f, 0.0f)
		};
		const Vec2f uvs[6] =
		{
			Vec2f(0.0f, 0.0f), Vec2f(0.0f, 1.0f), Vec2f(1.0f, 1.0f),
			Vec2f(1.0f, 1.0f), Vec2f(1.0f, 0.0f), Vec2f(0.0f, 0.0f)
		};

		vertices.resize(6);
		for (int i = 0; i < 6; ++i)
		{
			const Vec2f transformed = spriteInfo.mTransformation.transformVector(localPositions[i] + pivot) + position;
			vertices[i].mPosition = transformed;
			vertices[i].mTexcoords = uvs[i];
		}
	}

	void buildPatternBitmap(Bitmap& outBitmap, const PatternManager::CacheItem::Pattern& pattern, const uint32* palette, uint8 atex)
	{
		outBitmap.create(8, 8);
		uint32* dst = outBitmap.getData();
		for (int i = 0; i < 64; ++i)
		{
			const uint8 colorIndex = pattern.mPixels[i] + atex;
			dst[i] = (colorIndex & 0x0f) ? palette[colorIndex] : 0;
		}
	}

	void buildPaletteSpriteBitmap(Bitmap& outBitmap, const PaletteBitmap& source, const PaletteBase& palette, uint16 atex)
	{
		outBitmap.create(source.getWidth(), source.getHeight());
		const uint32* colors = palette.getRawColors();
		const size_t colorCount = palette.getSize();
		const uint8* src = source.getData();
		uint32* dst = outBitmap.getData();
		for (int i = 0; i < source.getPixelCount(); ++i)
		{
			const uint8 sourceIndex = src[i];
			if ((sourceIndex & 0x0f) == 0)
			{
				dst[i] = 0;
				continue;
			}
			const uint32 index = (uint32)sourceIndex + atex;
			dst[i] = (index < colorCount) ? colors[index] : 0;
		}
	}
}


GXRenderer::GXRenderer(RenderParts& renderParts, DrawerTexture& outputTexture) :
	Renderer(RENDERER_TYPE_ID, renderParts, outputTexture)
{
}

void GXRenderer::initialize()
{
	mGameResolution = Configuration::instance().mGameScreen;
	mCurrentViewport.set(0, 0, mGameResolution.x, mGameResolution.y);
}

void GXRenderer::reset()
{
	clearFrameCaches();
	mCurrentTargetAlreadyHasNativeFrame = false;
	mCurrentViewport.set(0, 0, mGameResolution.x, mGameResolution.y);
}

void GXRenderer::setGameResolution(const Vec2i& gameResolution)
{
	mGameResolution = gameResolution;
	mCurrentViewport.set(0, 0, mGameResolution.x, mGameResolution.y);
	clearFrameCaches();
}

void GXRenderer::clearGameScreen()
{
	Drawer& drawer = EngineMain::instance().getDrawer();
	drawer.setWindowRenderTarget(Recti(0, 0, mGameResolution.x, mGameResolution.y));
	drawer.setBlendMode(BlendMode::OPAQUE);
	drawer.drawRect(Recti(0, 0, mGameResolution.x, mGameResolution.y), Color::BLACK);
	drawer.performRendering();
}

void GXRenderer::renderGameScreen(const std::vector<Geometry*>& geometries)
{
	renderGameScreenToCurrentTarget(geometries, Recti(0, 0, mGameResolution.x, mGameResolution.y));
}

void GXRenderer::renderDebugDraw(int debugDrawMode, const Recti& rect)
{
	(void)debugDrawMode;
	(void)rect;
}

void GXRenderer::renderGameScreenToCurrentTarget(const std::vector<Geometry*>& geometries, const Recti& targetRect)
{
	beginFrameCaches();
	Drawer& drawer = EngineMain::instance().getDrawer();
	const Recti viewport = targetRect.empty() ? Recti(0, 0, mGameResolution.x, mGameResolution.y) : targetRect;
	mCurrentViewport.set(0, 0, mGameResolution.x, mGameResolution.y);

	drawer.setWindowRenderTarget(viewport);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
	drawer.setBlendMode(BlendMode::OPAQUE);

	Color backdropColor = mRenderParts.getPaletteManager().getBackdropColor();
	backdropColor.a = 1.0f;
	drawer.drawRect(Recti(0, 0, mGameResolution.x, mGameResolution.y), backdropColor);

	bool scissorActive = false;
	for (const Geometry* geometry : geometries)
	{
		if (nullptr != geometry)
			drawGeometry(*geometry, scissorActive);
	}
	if (scissorActive)
		drawer.popScissor();

	drawer.performRendering();
	mCurrentTargetAlreadyHasNativeFrame = true;
	pruneFrameCaches();
}

bool GXRenderer::canDrawPresentedGameScreenToCurrentTarget() const
{
	return mCurrentTargetAlreadyHasNativeFrame;
}

bool GXRenderer::drawPresentedGameScreenToCurrentTarget(const Recti& targetRect)
{
	(void)targetRect;
	const bool hadFrame = mCurrentTargetAlreadyHasNativeFrame;
	mCurrentTargetAlreadyHasNativeFrame = false;
	return hadFrame;
}

void GXRenderer::beginFrameCaches()
{
	++mFrameCacheGeneration;
}

void GXRenderer::clearFrameCaches()
{
	++mFrameCacheGeneration;
	mPatternTextures.clear();
	mPaletteSpriteTextures.clear();
	mComponentSpriteTextures.clear();
}

void GXRenderer::pruneFrameCaches()
{
	if ((mFrameCacheGeneration % 30) != 0)
		return;

	auto pruneCache = [this](auto& cache, uint32 maxAge)
	{
		for (auto it = cache.begin(); it != cache.end(); )
		{
			const uint32 age = mFrameCacheGeneration - it->second->mLastUsedFrame;
			if (age > maxAge)
				it = cache.erase(it);
			else
				++it;
		}
	};

	pruneCache(mPatternTextures, GX_PATTERN_CACHE_MAX_AGE);
	pruneCache(mPaletteSpriteTextures, GX_CUSTOM_SPRITE_CACHE_MAX_AGE);
	pruneCache(mComponentSpriteTextures, GX_CUSTOM_SPRITE_CACHE_MAX_AGE);
}

void GXRenderer::drawGeometry(const Geometry& geometry, bool& scissorActive)
{
	Drawer& drawer = EngineMain::instance().getDrawer();
	switch (geometry.getType())
	{
		case Geometry::Type::RECT:
		{
			const RectGeometry& rect = geometry.as<RectGeometry>();
			drawer.setBlendMode(BlendMode::ALPHA);
			drawer.drawRect(rect.mRect, rect.mColor);
			break;
		}

		case Geometry::Type::TEXTURED_RECT:
		{
			const TexturedRectGeometry& rect = geometry.as<TexturedRectGeometry>();
			drawer.setBlendMode(BlendMode::ALPHA);
			drawer.setSamplingMode(SamplingMode::POINT);
			drawer.drawRect(rect.mRect, rect.mDrawerTexture, rect.mTintColor, rect.mAddedColor);
			break;
		}

		case Geometry::Type::PLANE:
			drawPlane(geometry.as<PlaneGeometry>());
			break;

		case Geometry::Type::SPRITE:
			drawSprite(geometry.as<SpriteGeometry>());
			break;

		case Geometry::Type::VIEWPORT:
			if (scissorActive)
				drawer.popScissor();
			drawer.pushScissor(geometry.as<ViewportGeometry>().mRect);
			scissorActive = true;
			break;

		case Geometry::Type::EFFECT_BLUR:
			break;

		default:
			break;
	}
}

void GXRenderer::drawPlane(const PlaneGeometry& geometry)
{
	if (!PlaneManager::isRenderablePlaneIndex(geometry.mPlaneIndex))
		return;

	Drawer& drawer = EngineMain::instance().getDrawer();
	drawer.setBlendMode(BlendMode::ALPHA);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);

	Recti rect(0, 0, mGameResolution.x, mGameResolution.y);
	rect.intersect(geometry.mActiveRect);
	rect.intersect(mCurrentViewport);
	if (rect.empty())
		return;

	const PlaneManager& planeManager = mRenderParts.getPlaneManager();
	const ScrollOffsetsManager& scrollOffsetsManager = mRenderParts.getScrollOffsetsManager();
	const uint16* planeData = planeManager.getPlanePatternsBuffer((uint8)geometry.mPlaneIndex);
	const uint16 numPatternsPerLine = (geometry.mPlaneIndex <= PlaneManager::PLANE_A) ? planeManager.getPlayfieldSizeInPatterns().x : 64;
	const uint16 positionMaskH = planeManager.getPlayfieldSizeInPixels().x - 1;
	const uint16 positionMaskV = planeManager.getPlayfieldSizeInPixels().y - 1;
	const int16 verticalScrollOffsetBias = scrollOffsetsManager.getVerticalScrollOffsetBias();

	const uint16* scrollOffsetsH = nullptr;
	const uint16* scrollOffsetsV = nullptr;
	uint16 scrollMaskH = 0xff;
	uint16 scrollMaskV = 0;
	bool scrollNoRepeat = false;
	uint16 wScrollOffsetX = 0;

	if (geometry.mPlaneIndex == PlaneManager::PLANE_W)
	{
		wScrollOffsetX = (uint16)scrollOffsetsManager.getPlaneWScrollOffset().x;
		scrollOffsetsH = &wScrollOffsetX;
		scrollMaskH = 0;
	}
	else
	{
		scrollOffsetsH = scrollOffsetsManager.getScrollOffsetsH(geometry.mScrollOffsets);
		scrollOffsetsV = scrollOffsetsManager.getScrollOffsetsV(geometry.mScrollOffsets);
		scrollMaskV = scrollOffsetsManager.getVerticalScrolling() ? 0x1f : 0;
		scrollNoRepeat = scrollOffsetsManager.getHorizontalScrollNoRepeat(geometry.mScrollOffsets);
	}

	bool constantHorizontalScroll = true;
	int16 horizontalScrollOffset = 0;
	if (nullptr != scrollOffsetsH)
	{
		horizontalScrollOffset = (int16)scrollOffsetsH[rect.y & scrollMaskH];
		for (int y = rect.y + 1; y < rect.y + rect.height; ++y)
		{
			if ((int16)scrollOffsetsH[y & scrollMaskH] != horizontalScrollOffset)
			{
				constantHorizontalScroll = false;
				break;
			}
		}
	}

	if (!scrollNoRepeat && constantHorizontalScroll && (nullptr == scrollOffsetsV || scrollMaskV == 0))
	{
		const int verticalScrollOffset = (nullptr == scrollOffsetsV) ? 0 : (int)scrollOffsetsV[0];
		const int bottom = rect.y + rect.height;
		const int right = rect.x + rect.width;
		for (int y = rect.y; y < bottom; )
		{
			const int vy = (y + verticalScrollOffset) & positionMaskV;
			const int patternRow = vy & 7;
			int rows = std::min(8 - patternRow, bottom - y);
			const int splitY = mRenderParts.getPaletteManager().mSplitPositionY;
			if (y < splitY && y + rows > splitY)
				rows = splitY - y;
			const int paletteIndex = (y < splitY) ? 0 : 1;

			int vx = rect.x + horizontalScrollOffset;
			for (int x = rect.x; x < right; )
			{
				vx &= positionMaskH;
				const int patternColumn = vx & 7;
				const int columns = std::min(8 - patternColumn, right - x);
				const uint16 patternIndex = planeData[(vx / 8) + (vy / 8) * numPatternsPerLine];
				const bool priority = (patternIndex & 0x8000) != 0;
				if (priority == geometry.mPriorityFlag)
				{
					DrawerTexture& texture = getPatternTexture(patternIndex, paletteIndex);
					const Vec2f uv0((float)patternColumn / 8.0f, (float)patternRow / 8.0f);
					const Vec2f uv1((float)(patternColumn + columns) / 8.0f, (float)(patternRow + rows) / 8.0f);
					drawer.drawRect(Recti(x, y, columns, rows), texture, uv0, uv1, Color::WHITE);
				}

				x += columns;
				vx += columns;
			}

			y += rows;
		}
		return;
	}

	for (int y = rect.y; y < rect.y + rect.height; ++y)
	{
		const int paletteIndex = (y < mRenderParts.getPaletteManager().mSplitPositionY) ? 0 : 1;
		int vx = rect.x;
		if (nullptr != scrollOffsetsH)
			vx += (int16)scrollOffsetsH[y & scrollMaskH];

		int startX = rect.x;
		int endX = rect.x + rect.width;
		if (scrollNoRepeat)
		{
			if (vx > 0x0800)
			{
				startX += 0x1000 - vx;
				vx = 0;
			}
			else if (endX > startX + (positionMaskH - vx))
			{
				endX = startX + (positionMaskH - vx) + 1;
			}
			if (startX >= endX)
				continue;
		}

		for (int x = startX; x < endX; )
		{
			vx &= positionMaskH;
			int vy;
			if (nullptr == scrollOffsetsV)
			{
				vy = y;
			}
			else if (scrollMaskV == 0)
			{
				vy = (y + scrollOffsetsV[0]) & positionMaskV;
			}
			else
			{
				const int verticalScrollOffset = scrollOffsetsV[((x - verticalScrollOffsetBias) >> 4) & scrollMaskV];
				vy = (y + verticalScrollOffset) & positionMaskV;
			}

			const uint16 patternIndex = planeData[(vx / 8) + (vy / 8) * numPatternsPerLine];
			const bool priority = (patternIndex & 0x8000) != 0;
			const int patternColumn = vx & 7;
			const int patternRow = vy & 7;
			const int pixels = std::min(8 - patternColumn, endX - x);

			if (priority == geometry.mPriorityFlag)
			{
				DrawerTexture& texture = getPatternTexture(patternIndex, paletteIndex);
				const Vec2f uv0((float)patternColumn / 8.0f, (float)patternRow / 8.0f);
				const Vec2f uv1((float)(patternColumn + pixels) / 8.0f, (float)(patternRow + 1) / 8.0f);
				drawer.drawRect(Recti(x, y, pixels, 1), texture, uv0, uv1, Color::WHITE);
			}

			x += pixels;
			vx += pixels;
		}
	}
}

void GXRenderer::drawSprite(const SpriteGeometry& geometry)
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

void GXRenderer::drawVdpSprite(const renderitems::VdpSpriteInfo& spriteInfo)
{
	if (spriteInfo.mSize.x <= 0 || spriteInfo.mSize.y <= 0 || !isBlendModeSupported(spriteInfo.mBlendMode))
		return;

	Color tintColor;
	Color addedColor;
	applySpriteTint(spriteInfo, tintColor, addedColor);

	Drawer& drawer = EngineMain::instance().getDrawer();
	drawer.setBlendMode(spriteInfo.mBlendMode);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);

	const PaletteManager& paletteManager = mRenderParts.getPaletteManager();
	const bool flipH = (spriteInfo.mFirstPattern & 0x0800) != 0;
	const bool flipV = (spriteInfo.mFirstPattern & 0x1000) != 0;
	for (int tileY = 0; tileY < spriteInfo.mSize.y; ++tileY)
	{
		for (int tileX = 0; tileX < spriteInfo.mSize.x; ++tileX)
		{
			const int patternX = flipH ? (spriteInfo.mSize.x - tileX - 1) : tileX;
			const int patternY = flipV ? (spriteInfo.mSize.y - tileY - 1) : tileY;
			const uint16 patternIndex = spriteInfo.mFirstPattern + patternY + patternX * spriteInfo.mSize.y;
			const int px = spriteInfo.mInterpolatedPosition.x + tileX * 8;
			const int py = spriteInfo.mInterpolatedPosition.y + tileY * 8;

			int row = 0;
			while (row < 8)
			{
				const int screenY = py + row;
				const int paletteIndex = (screenY < paletteManager.mSplitPositionY) ? 0 : 1;
				const int nextSplit = (paletteIndex == 0) ? paletteManager.mSplitPositionY : py + 8;
				const int endRow = clamp(nextSplit - py, row + 1, 8);
				DrawerTexture& texture = getPatternTexture(patternIndex, paletteIndex);
				const Vec2f uv0(0.0f, (float)row / 8.0f);
				const Vec2f uv1(1.0f, (float)endRow / 8.0f);
				drawer.drawRect(Recti(px, screenY, 8, endRow - row), texture, uv0, uv1, tintColor, addedColor);
				row = endRow;
			}
		}
	}
}

void GXRenderer::drawPaletteSprite(const renderitems::PaletteSpriteInfo& spriteInfo)
{
	if (spriteInfo.mSize.x <= 0 || spriteInfo.mSize.y <= 0 || nullptr == spriteInfo.mCacheItem || nullptr == spriteInfo.mCacheItem->mSprite || !isBlendModeSupported(spriteInfo.mBlendMode))
		return;

	Color tintColor;
	Color addedColor;
	applySpriteTint(spriteInfo, tintColor, addedColor);

	Drawer& drawer = EngineMain::instance().getDrawer();
	drawer.setBlendMode(spriteInfo.mBlendMode);
	DrawerTexture& texture = getPaletteSpriteTexture(spriteInfo);
	if (spriteInfo.mTransformation.isIdentity())
	{
		drawer.drawRect(Recti(spriteInfo.mInterpolatedPosition + spriteInfo.mPivotOffset, spriteInfo.mSize), texture, tintColor, addedColor);
	}
	else
	{
		std::vector<DrawerMeshVertex> vertices;
		appendSpriteQuad(vertices, spriteInfo);
		drawer.drawMesh(vertices, texture, tintColor, addedColor);
	}
}

void GXRenderer::drawComponentSprite(const renderitems::ComponentSpriteInfo& spriteInfo)
{
	if (spriteInfo.mSize.x <= 0 || spriteInfo.mSize.y <= 0 || nullptr == spriteInfo.mCacheItem || nullptr == spriteInfo.mCacheItem->mSprite || !isBlendModeSupported(spriteInfo.mBlendMode))
		return;

	Color tintColor;
	Color addedColor;
	applySpriteTint(spriteInfo, tintColor, addedColor);

	Drawer& drawer = EngineMain::instance().getDrawer();
	drawer.setBlendMode(spriteInfo.mBlendMode);
	DrawerTexture& texture = getComponentSpriteTexture(spriteInfo);
	if (spriteInfo.mTransformation.isIdentity())
	{
		drawer.drawRect(Recti(spriteInfo.mInterpolatedPosition + spriteInfo.mPivotOffset, spriteInfo.mSize), texture, tintColor, addedColor);
	}
	else
	{
		std::vector<DrawerMeshVertex> vertices;
		appendSpriteQuad(vertices, spriteInfo);
		drawer.drawMesh(vertices, texture, tintColor, addedColor);
	}
}

void GXRenderer::drawSpriteMask(const renderitems::SpriteMaskInfo& spriteInfo)
{
	(void)spriteInfo;
}

DrawerTexture& GXRenderer::getPatternTexture(uint16 patternIndex, int paletteIndex)
{
	const uint8 flip = (patternIndex >> 11) & 3;
	const uint8 atex = (patternIndex >> 9) & 0x30;
	const PatternManager::CacheItem::Pattern& pattern = mRenderParts.getPatternManager().getPatternCache()[patternIndex & 0x07ff].mFlipVariation[flip];
	const PaletteBase& palette = mRenderParts.getPaletteManager().getMainPalette(paletteIndex);
	const uint64 key = ((uint64)(patternIndex & 0x07ff) << 32) | ((uint64)flip << 24) | ((uint64)paletteIndex << 16) | atex;
	uint64 contentKey = hashPatternState(key, pattern);
	contentKey = hashPaletteState(contentKey, palette);
	std::unique_ptr<CachedTexture>& cached = mPatternTextures[contentKey];
	if (!cached)
	{
		cached = std::make_unique<CachedTexture>();
		Bitmap bitmap;
		buildPatternBitmap(bitmap, pattern, palette.getRawColors(), atex);
		cached->mTexture.accessBitmap() = bitmap;
		cached->mTexture.bitmapUpdated();
	}
	cached->mLastUsedFrame = mFrameCacheGeneration;
	return cached->mTexture;
}

DrawerTexture& GXRenderer::getPaletteSpriteTexture(const renderitems::PaletteSpriteInfo& spriteInfo)
{
	uint64 key = spriteInfo.mKey ^ ((uint64)spriteInfo.mAtex << 48);
	key = hashPointer(key, spriteInfo.mPrimaryPalette);
	const PaletteManager& paletteManager = mRenderParts.getPaletteManager();
	const PaletteBase& palette = (nullptr == spriteInfo.mPrimaryPalette) ? paletteManager.getMainPalette(0) : *spriteInfo.mPrimaryPalette;
	key = hashPaletteState(key, palette);
	std::unique_ptr<CachedTexture>& cached = mPaletteSpriteTextures[key];
	if (!cached)
	{
		cached = std::make_unique<CachedTexture>();
		PaletteSprite& sprite = *static_cast<PaletteSprite*>(spriteInfo.mCacheItem->mSprite);
		Bitmap bitmap;
		buildPaletteSpriteBitmap(bitmap, sprite.accessBitmap(), palette, spriteInfo.mAtex);
		cached->mTexture.accessBitmap() = bitmap;
		cached->mTexture.bitmapUpdated();
	}
	cached->mLastUsedFrame = mFrameCacheGeneration;
	return cached->mTexture;
}

DrawerTexture& GXRenderer::getComponentSpriteTexture(const renderitems::ComponentSpriteInfo& spriteInfo)
{
	uint64 key = spriteInfo.mKey;
	key = hashPointer(key, spriteInfo.mCacheItem);
	std::unique_ptr<CachedTexture>& cached = mComponentSpriteTextures[key];
	if (!cached)
	{
		cached = std::make_unique<CachedTexture>();
		ComponentSprite& sprite = *static_cast<ComponentSprite*>(spriteInfo.mCacheItem->mSprite);
		cached->mTexture.accessBitmap() = sprite.accessBitmap();
		cached->mTexture.bitmapUpdated();
	}
	cached->mLastUsedFrame = mFrameCacheGeneration;
	return cached->mTexture;
}
