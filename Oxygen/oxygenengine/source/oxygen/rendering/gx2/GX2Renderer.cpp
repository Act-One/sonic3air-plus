/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/rendering/gx2/GX2Renderer.h"

#include "oxygen/application/Configuration.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/drawing/Drawer.h"
#include "oxygen/drawing/DrawerTexture.h"
#include "oxygen/helper/Logging.h"
#include "oxygen/rendering/parts/RenderItem.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/rendering/parts/PlaneManager.h"
#include "oxygen/rendering/parts/PatternManager.h"
#include "oxygen/rendering/sprite/ComponentSprite.h"
#include "oxygen/rendering/sprite/PaletteSprite.h"


namespace
{
	constexpr bool ENABLE_GX2_RENDERER_DIAGNOSTIC_DUMPS = false;
	constexpr bool ENABLE_GX2_RENDERER_FRAME_LOGS = false;
	constexpr bool ENABLE_GX2_RENDERER_SPRITE_TRACE = false;
	constexpr bool FORCE_SOFTWARE_GAME_RENDERER = false;

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

	bool isColorTransparent(const Color& color)
	{
		return color.r == 0.0f && color.g == 0.0f && color.b == 0.0f && color.a == 0.0f;
	}

	uint32 tintedABGR(uint32 abgr, const Color& tint)
	{
		const uint32 r = (uint32)roundToInt((float)((abgr) & 0xff) * tint.r);
		const uint32 g = (uint32)roundToInt((float)((abgr >> 8) & 0xff) * tint.g);
		const uint32 b = (uint32)roundToInt((float)((abgr >> 16) & 0xff) * tint.b);
		const uint32 a = (uint32)roundToInt((float)((abgr >> 24) & 0xff) * tint.a);
		return std::min<uint32>(r, 255)
			 | (std::min<uint32>(g, 255) << 8)
			 | (std::min<uint32>(b, 255) << 16)
			 | (std::min<uint32>(a, 255) << 24);
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

	void buildTintedBitmap(Bitmap& outBitmap, const Bitmap& source, const Color& tint)
	{
		outBitmap.create(source.getWidth(), source.getHeight());
		const uint32* src = source.getData();
		uint32* dst = outBitmap.getData();
		if (tint == Color::WHITE)
		{
			memcpy(dst, src, (size_t)source.getPixelCount() * sizeof(uint32));
			return;
		}
		for (int i = 0; i < source.getPixelCount(); ++i)
		{
			dst[i] = tintedABGR(src[i], tint);
		}
	}

	void buildVdpSpriteBitmap(Bitmap& outBitmap, const renderitems::VdpSpriteInfo& spriteInfo, RenderParts& renderParts)
	{
		const Vec2i size(spriteInfo.mSize.x * 8, spriteInfo.mSize.y * 8);
		outBitmap.create(size);
		outBitmap.clear(0);

		const PaletteManager& paletteManager = renderParts.getPaletteManager();
		const uint32* palettes[2] =
		{
			paletteManager.getMainPalette(0).getRawColors(),
			paletteManager.getMainPalette(1).getRawColors()
		};
		const PatternManager::CacheItem* patternCache = renderParts.getPatternManager().getPatternCache();

		for (int y = 0; y < size.y; ++y)
		{
			const uint32* palette = ((spriteInfo.mInterpolatedPosition.y + y) < paletteManager.mSplitPositionY) ? palettes[0] : palettes[1];
			uint32* dst = outBitmap.getPixelPointer(0, y);
			for (int x = 0; x < size.x; ++x)
			{
				int patternX = x / 8;
				int patternY = y / 8;
				if (spriteInfo.mFirstPattern & 0x0800)
					patternX = spriteInfo.mSize.x - patternX - 1;
				if (spriteInfo.mFirstPattern & 0x1000)
					patternY = spriteInfo.mSize.y - patternY - 1;

				const uint16 patternIndex = spriteInfo.mFirstPattern + patternY + patternX * spriteInfo.mSize.y;
				const PatternManager::CacheItem::Pattern& pattern = patternCache[patternIndex & 0x07ff].mFlipVariation[(patternIndex >> 11) & 3];
				const uint8 colorIndex = pattern.mPixels[(x & 7) + (y & 7) * 8] + ((patternIndex >> 9) & 0x30);
				dst[x] = (colorIndex & 0x0f) ? palette[colorIndex] : 0;
			}
		}
	}

	void dumpBitmapPpmOnce(const char* path, const Bitmap& bitmap)
	{
		static std::unordered_set<std::string> dumpedPaths;
		if (bitmap.empty() || !dumpedPaths.insert(path).second)
			return;

		FILE* file = fopen(path, "wb");
		if (nullptr == file)
			return;

		fprintf(file, "P6\n%d %d\n255\n", bitmap.getWidth(), bitmap.getHeight());
		for (int y = 0; y < bitmap.getHeight(); ++y)
		{
			const uint8* src = reinterpret_cast<const uint8*>(bitmap.getPixelPointer(0, y));
			for (int x = 0; x < bitmap.getWidth(); ++x)
			{
				const uint8 rgb[3] =
				{
					src[x * 4 + ABGR32_BYTE_R],
					src[x * 4 + ABGR32_BYTE_G],
					src[x * 4 + ABGR32_BYTE_B]
				};
				fwrite(rgb, 1, sizeof(rgb), file);
			}
		}
		fclose(file);
		RMX_LOG_INFO("GX2Renderer: dumped diagnostic bitmap " << path << " size=" << bitmap.getWidth() << "x" << bitmap.getHeight());
	}

	struct BitmapSample
	{
		uint32 nonBlackSamples = 0;
		uint32 alphaSamples = 0;
		uint32 sampleXor = 0;
		uint32 center = 0;
	};

	BitmapSample sampleBitmap(const Bitmap& bitmap)
	{
		BitmapSample sample;
		if (bitmap.empty())
			return sample;

		const int width = bitmap.getWidth();
		const int height = bitmap.getHeight();
		sample.center = *bitmap.getPixelPointer(width / 2, height / 2);
		for (int yIndex = 0; yIndex < 8; ++yIndex)
		{
			const int y = (height <= 1) ? 0 : (int)(((uint64)yIndex * (uint64)(height - 1)) / 7u);
			for (int xIndex = 0; xIndex < 8; ++xIndex)
			{
				const int x = (width <= 1) ? 0 : (int)(((uint64)xIndex * (uint64)(width - 1)) / 7u);
				const uint32 pixel = *bitmap.getPixelPointer(x, y);
				if ((pixel & 0x00ffffffu) != 0)
					++sample.nonBlackSamples;
				if ((pixel & 0xff000000u) != 0)
					++sample.alphaSamples;
				sample.sampleXor ^= pixel;
			}
		}
		return sample;
	}

	uint64 hashPointer(uint64 seed, const void* ptr)
	{
		const uint64 value = (uint64)(uintptr_t)ptr;
		return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
	}

	void appendSpriteQuad(std::vector<DrawerMeshVertex>& vertices, const renderitems::CustomSpriteInfoBase& spriteInfo)
	{
		const float width = (float)spriteInfo.mSize.x;
		const float height = (float)spriteInfo.mSize.y;
		const Vec2f pivot((float)spriteInfo.mPivotOffset.x, (float)spriteInfo.mPivotOffset.y);
		const Vec2f position((float)spriteInfo.mInterpolatedPosition.x, (float)spriteInfo.mInterpolatedPosition.y);

		const Vec2f localPositions[6] =
		{
			Vec2f(0.0f, 0.0f),
			Vec2f(0.0f, height),
			Vec2f(width, height),
			Vec2f(width, height),
			Vec2f(width, 0.0f),
			Vec2f(0.0f, 0.0f),
		};
		const Vec2f uvs[6] =
		{
			Vec2f(0.0f, 0.0f),
			Vec2f(0.0f, 1.0f),
			Vec2f(1.0f, 1.0f),
			Vec2f(1.0f, 1.0f),
			Vec2f(1.0f, 0.0f),
			Vec2f(0.0f, 0.0f),
		};

		vertices.resize(6);
		for (int i = 0; i < 6; ++i)
		{
			const Vec2f local = localPositions[i] + pivot;
			const Vec2f transformed = spriteInfo.mTransformation.transformVector(local) + position;
			vertices[i].mPosition = transformed;
			vertices[i].mTexcoords = uvs[i];
		}
	}

	void appendPaletteSpriteQuad(std::vector<DrawerMeshVertex>& vertices, const renderitems::CustomSpriteInfoBase& spriteInfo)
	{
		const float width = (float)spriteInfo.mSize.x;
		const float height = (float)spriteInfo.mSize.y;
		const Vec2f pivot((float)spriteInfo.mPivotOffset.x, (float)spriteInfo.mPivotOffset.y);
		const Vec2f position((float)spriteInfo.mInterpolatedPosition.x, (float)spriteInfo.mInterpolatedPosition.y);

		const Vec2f localPositions[6] =
		{
			Vec2f(0.0f, 0.0f),
			Vec2f(0.0f, height),
			Vec2f(width, height),
			Vec2f(width, height),
			Vec2f(width, 0.0f),
			Vec2f(0.0f, 0.0f),
		};

		vertices.resize(6);
		for (int i = 0; i < 6; ++i)
		{
			const Vec2f transformed = spriteInfo.mTransformation.transformVector(localPositions[i] + pivot) + position;
			vertices[i].mPosition = transformed;
			vertices[i].mTexcoords = localPositions[i];
		}
	}

	void applySpriteTint(renderitems::SpriteInfo const& spriteInfo, Color& tintColor, Color& addedColor)
	{
		tintColor = spriteInfo.mTintColor;
		addedColor = spriteInfo.mAddedColor;
		if (spriteInfo.mUseGlobalComponentTint)
		{
			RenderParts::instance().getPaletteManager().applyGlobalComponentTint(tintColor, addedColor);
		}
	}

	void traceNativeSprite(const char* label, const renderitems::CustomSpriteInfoBase& spriteInfo)
	{
		if constexpr (ENABLE_GX2_RENDERER_SPRITE_TRACE)
		{
			static int sTraceCount = 0;
			if (sTraceCount >= 80)
				return;
			if (spriteInfo.mSize.x < 32 && spriteInfo.mSize.y < 32)
				return;
			RMX_LOG_INFO("GX2Renderer sprite trace: " << label
				<< " key=0x" << rmx::hexString(spriteInfo.mKey, 16)
				<< " pos=" << spriteInfo.mInterpolatedPosition.x << "," << spriteInfo.mInterpolatedPosition.y
				<< " pivot=" << spriteInfo.mPivotOffset.x << "," << spriteInfo.mPivotOffset.y
				<< " size=" << spriteInfo.mSize.x << "x" << spriteInfo.mSize.y
				<< " blend=" << (int)spriteInfo.mBlendMode
				<< " tintA=" << spriteInfo.mTintColor.a
				<< " transformIdentity=" << spriteInfo.mTransformation.isIdentity());
			++sTraceCount;
		}
	}

	const char* geometryTypeName(Geometry::Type type)
	{
		switch (type)
		{
			case Geometry::Type::UNDEFINED:		return "undefined";
			case Geometry::Type::PLANE:			return "plane";
			case Geometry::Type::SPRITE:		return "sprite";
			case Geometry::Type::RECT:			return "rect";
			case Geometry::Type::TEXTURED_RECT:	return "textured_rect";
			case Geometry::Type::VIEWPORT:		return "viewport";
			case Geometry::Type::EFFECT_BLUR:	return "effect_blur";
			default:							return "unknown";
		}
	}

	const char* renderItemTypeName(RenderItem::Type type)
	{
		switch (type)
		{
			case RenderItem::Type::VDP_SPRITE:		return "vdp_sprite";
			case RenderItem::Type::PALETTE_SPRITE:	return "palette_sprite";
			case RenderItem::Type::COMPONENT_SPRITE:return "component_sprite";
			case RenderItem::Type::SPRITE_MASK:		return "sprite_mask";
			case RenderItem::Type::RECTANGLE:		return "rectangle";
			case RenderItem::Type::TEXT:			return "text";
			case RenderItem::Type::VIEWPORT:		return "viewport";
			case RenderItem::Type::INVALID:			return "invalid";
			default:								return "unknown";
		}
	}
}


GX2Renderer::GX2Renderer(RenderParts& renderParts, DrawerTexture& outputTexture) :
	Renderer(RENDERER_TYPE_ID, renderParts, outputTexture),
	mSoftwareRenderer(renderParts, outputTexture),
	mRenderResources(renderParts)
{
}

void GX2Renderer::initialize()
{
	mGameResolution = Configuration::instance().mGameScreen;
	mRenderResources.initialize();
	invalidateNativeRenderTarget();
	if constexpr (FORCE_SOFTWARE_GAME_RENDERER)
	{
		RMX_LOG_WARNING("GX2Renderer: forced software game renderer output");
	}
	else
	{
		RMX_LOG_INFO("GX2Renderer: native GX2 game renderer enabled, software fallback retained for unported geometry");
	}
}

void GX2Renderer::reset()
{
	if (mSoftwareRendererInitialized)
	{
		mSoftwareRenderer.reset();
	}
	invalidateNativeRenderTarget();
	mRenderResources.clearAllCaches();
}

void GX2Renderer::setGameResolution(const Vec2i& gameResolution)
{
	if (mGameResolution == gameResolution)
		return;

	mGameResolution = gameResolution;
	if (mSoftwareRendererInitialized)
	{
		mSoftwareRenderer.setGameResolution(gameResolution);
	}
	invalidateNativeRenderTarget();
}

void GX2Renderer::clearGameScreen()
{
	if constexpr (FORCE_SOFTWARE_GAME_RENDERER)
	{
		renderHybridGameScreen({});
	}
	else
	{
		renderNativeGameScreen({});
	}
}

void GX2Renderer::renderGameScreen(const std::vector<Geometry*>& geometries)
{
	static uint32 sRenderFrame = 0;
	if constexpr (ENABLE_GX2_RENDERER_FRAME_LOGS)
	{
		if (sRenderFrame < 8 || (sRenderFrame % 300) == 0)
		{
			uint32 planeCount = 0;
			uint32 spriteCount = 0;
			uint32 nativeSpriteCount = 0;
			uint32 texturedRectCount = 0;
			uint32 nativeCount = 0;
			for (const Geometry* geometry : geometries)
			{
				if (nullptr == geometry)
					continue;
				if (geometry->getType() == Geometry::Type::PLANE)
					++planeCount;
				else if (geometry->getType() == Geometry::Type::SPRITE)
				{
					++spriteCount;
					if (supportsNativeSprite(geometry->as<SpriteGeometry>()))
						++nativeSpriteCount;
				}
				else if (geometry->getType() == Geometry::Type::TEXTURED_RECT)
					++texturedRectCount;
				if (supportsNativeGeometry(*geometry))
					++nativeCount;
			}
			RMX_LOG_INFO("GX2Renderer: frame=" << sRenderFrame
				<< " geometries=" << geometries.size()
				<< " native=" << nativeCount
				<< " planes=" << planeCount
				<< " sprites=" << spriteCount
				<< " nativeSprites=" << nativeSpriteCount
				<< " texturedRects=" << texturedRectCount);
		}
	}
	++sRenderFrame;

	if constexpr (FORCE_SOFTWARE_GAME_RENDERER)
	{
		renderHybridGameScreen(geometries);
		return;
	}

	mRenderResources.refresh();
	if (!supportsNativeRendering(geometries))
	{
		static uint32 sFallbackLogCount = 0;
		if (sFallbackLogCount < 8)
		{
			for (const Geometry* geometry : geometries)
			{
				if (nullptr != geometry && !supportsNativeGeometry(*geometry))
				{
					RMX_LOG_INFO("GX2Renderer: software fallback in offscreen path due to geometry=" << geometryTypeName(geometry->getType())
						<< ((geometry->getType() == Geometry::Type::SPRITE) ? " sprite=" : "")
						<< ((geometry->getType() == Geometry::Type::SPRITE) ? renderItemTypeName(geometry->as<SpriteGeometry>().mSpriteInfo.getType()) : ""));
					++sFallbackLogCount;
					break;
				}
			}
		}
		renderHybridGameScreen(geometries);
		return;
	}

	renderNativeGameScreen(geometries);
}

void GX2Renderer::renderGameScreenToCurrentTarget(const std::vector<Geometry*>& geometries, const Recti& targetRect)
{
	static bool sLoggedDirectWindow = false;
	if (!sLoggedDirectWindow)
	{
		sLoggedDirectWindow = true;
		RMX_LOG_INFO("GX2Renderer: preparing game screen for current GX2 target");
	}

	Drawer& drawer = EngineMain::instance().getDrawer();
	const Recti viewport = targetRect.empty() ? Recti(0, 0, mGameResolution.x, mGameResolution.y) : targetRect;
	mCurrentTargetAlreadyHasNativeFrame = false;

	if constexpr (FORCE_SOFTWARE_GAME_RENDERER)
	{
		std::vector<Geometry*> softwareGeometries;
		prepareHybridOverlayGeometries(geometries, softwareGeometries);
		renderHybridGameScreen(softwareGeometries);
		if (!mNativeOverlayGeometries.empty())
		{
			mRenderResources.refresh();
		}
		updateGameScreenPresentTexture();
		return;
	}

	mRenderResources.refresh();

	if (!supportsNativeRendering(geometries))
	{
		static uint32 sDirectFallbackLogCount = 0;
		if (sDirectFallbackLogCount < 8)
		{
			for (const Geometry* geometry : geometries)
			{
				if (nullptr != geometry && !supportsNativeGeometry(*geometry))
				{
					RMX_LOG_INFO("GX2Renderer: software fallback in direct path due to geometry=" << geometryTypeName(geometry->getType())
						<< ((geometry->getType() == Geometry::Type::SPRITE) ? " sprite=" : "")
						<< ((geometry->getType() == Geometry::Type::SPRITE) ? renderItemTypeName(geometry->as<SpriteGeometry>().mSpriteInfo.getType()) : ""));
					++sDirectFallbackLogCount;
					break;
				}
			}
		}
		std::vector<Geometry*> softwareGeometries;
		prepareHybridOverlayGeometries(geometries, softwareGeometries);
		renderHybridGameScreen(softwareGeometries);
		updateGameScreenPresentTexture();
		return;
	}

	if (requiresOffscreenNativeRendering(geometries))
	{
		renderNativeGameScreen(geometries);
		drawer.setWindowRenderTarget(viewport);
		drawer.setBlendMode(BlendMode::OPAQUE);
		drawer.setSamplingMode(SamplingMode::POINT);
		drawer.setWrapMode(TextureWrapMode::CLAMP);
		drawer.drawRect(viewport, mGameScreenTexture);
		mCurrentTargetAlreadyHasNativeFrame = true;
		return;
	}

	mNativeOverlayGeometries.clear();
	mNativeOverlayUsesInitialViewport = false;
	invalidateNativeRenderTarget();
	drawer.setWindowRenderTarget(viewport);
	drawer.setBlendMode(BlendMode::OPAQUE);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
	drawer.drawRect(viewport, mRenderParts.getPaletteManager().getBackdropColor());

	const bool usingSpriteMask = isUsingSpriteMask(geometries);
	bool scissorActive = false;
	uint16 lastRenderQueue = 0xffff;
	for (const Geometry* geometry : geometries)
	{
		if (nullptr != geometry)
		{
			const uint16 renderQueue = geometry->mRenderQueue;
			if (usingSpriteMask && lastRenderQueue < 0x8000 && renderQueue >= 0x8000)
			{
				if (scissorActive)
				{
					drawer.popScissor();
					scissorActive = false;
				}
				drawer.performRendering();
				copyNativeGameScreenToProcessingTexture(viewport);
			}
			drawNativeGeometry(*geometry, scissorActive);
			lastRenderQueue = renderQueue;
		}
	}
	if (scissorActive)
	{
		drawer.popScissor();
	}
	mCurrentTargetAlreadyHasNativeFrame = true;
}

void GX2Renderer::drawPresentedGameScreenToCurrentTarget(const Recti& targetRect)
{
	if (mCurrentTargetAlreadyHasNativeFrame)
	{
		mCurrentTargetAlreadyHasNativeFrame = false;
		return;
	}

	DrawerTexture& presentTexture = mGameScreenPresentTextures[mGameScreenPresentTextureIndex];
	DrawerTexture* texture = presentTexture.getBitmap().empty() ? nullptr : &presentTexture;
	if (nullptr == texture && mNativeRenderTargetReady)
		texture = &mGameScreenTexture;
	if (nullptr == texture)
		return;

	static bool sLoggedLateGameDraw = false;
	if (!sLoggedLateGameDraw)
	{
		sLoggedLateGameDraw = true;
		RMX_LOG_INFO("GX2Renderer: drawing presented game screen after logical target bind");
	}

	Drawer& drawer = EngineMain::instance().getDrawer();
	const Recti viewport = targetRect.empty() ? Recti(0, 0, mGameResolution.x, mGameResolution.y) : targetRect;
	drawer.setBlendMode(BlendMode::OPAQUE);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
	drawer.drawRect(viewport, *texture);

	if (!mNativeOverlayGeometries.empty())
	{
		bool scissorActive = false;
		if (mNativeOverlayUsesInitialViewport)
		{
			drawer.pushScissor(mNativeOverlayInitialViewport);
			scissorActive = true;
		}

		for (const Geometry* geometry : mNativeOverlayGeometries)
		{
			if (nullptr != geometry)
			{
				drawNativeGeometry(*geometry, scissorActive);
			}
		}
		if (scissorActive)
		{
			drawer.popScissor();
		}
	}
}

void GX2Renderer::renderDebugDraw(int debugDrawMode, const Recti& rect)
{
	ensureSoftwareRendererInitialized();
	mSoftwareRenderer.renderDebugDraw(debugDrawMode, rect);
	invalidateNativeRenderTarget();
}

void GX2Renderer::ensureSoftwareRendererInitialized()
{
	if (mSoftwareRendererInitialized)
		return;

	mSoftwareRenderer.initialize();
	if (mGameResolution.x > 0 && mGameResolution.y > 0)
	{
		mSoftwareRenderer.setGameResolution(mGameResolution);
	}
	mSoftwareRendererInitialized = true;
}

bool GX2Renderer::supportsNativeRendering(const std::vector<Geometry*>& geometries) const
{
	for (const Geometry* geometry : geometries)
	{
		if (nullptr != geometry && !supportsNativeGeometry(*geometry))
			return false;
	}
	return true;
}

bool GX2Renderer::supportsNativeGeometry(const Geometry& geometry) const
{
	switch (geometry.getType())
	{
		case Geometry::Type::UNDEFINED:
		case Geometry::Type::RECT:
		case Geometry::Type::VIEWPORT:
			return true;

		case Geometry::Type::TEXTURED_RECT:
			return true;

		case Geometry::Type::SPRITE:
			return supportsNativeSprite(geometry.as<SpriteGeometry>());

		case Geometry::Type::PLANE:
			return PlaneManager::isRenderablePlaneIndex(geometry.as<PlaneGeometry>().mPlaneIndex);

		case Geometry::Type::EFFECT_BLUR:
		default:
			return false;
	}
}

bool GX2Renderer::supportsNativeSprite(const SpriteGeometry& geometry) const
{
	if (!isBlendModeSupported(geometry.mSpriteInfo.mBlendMode))
		return false;

	switch (geometry.mSpriteInfo.getType())
	{
		case RenderItem::Type::COMPONENT_SPRITE:
		{
			const renderitems::ComponentSpriteInfo& spriteInfo = static_cast<const renderitems::ComponentSpriteInfo&>(geometry.mSpriteInfo);
			if (nullptr == spriteInfo.mCacheItem)
				return false;
			return true;
		}

		case RenderItem::Type::PALETTE_SPRITE:
		{
			const renderitems::PaletteSpriteInfo& spriteInfo = static_cast<const renderitems::PaletteSpriteInfo&>(geometry.mSpriteInfo);
			if (nullptr == spriteInfo.mCacheItem)
				return false;
			return true;
		}

		case RenderItem::Type::VDP_SPRITE:
		{
			const renderitems::VdpSpriteInfo& spriteInfo = static_cast<const renderitems::VdpSpriteInfo&>(geometry.mSpriteInfo);
			return spriteInfo.mSize.x > 0 && spriteInfo.mSize.y > 0;
		}

		case RenderItem::Type::SPRITE_MASK:
		{
			const renderitems::SpriteMaskInfo& spriteInfo = static_cast<const renderitems::SpriteMaskInfo&>(geometry.mSpriteInfo);
			return spriteInfo.mSize.x > 0 && spriteInfo.mSize.y > 0;
		}

		default:
			return false;
	}
}

bool GX2Renderer::requiresOffscreenNativeRendering(const std::vector<Geometry*>& geometries) const
{
	return isUsingSpriteMask(geometries);
}

void GX2Renderer::renderNativeGameScreen(const std::vector<Geometry*>& geometries)
{
	Drawer& drawer = EngineMain::instance().getDrawer();
	if (!mNativeRenderTargetReady || mGameScreenTexture.getSize() != mGameResolution)
	{
		// Software fallback updates the same DrawerTexture as a bitmap-backed texture. Recreate it as a GX2 render target before native drawing.
		mGameScreenTexture.invalidate();
		mGameScreenTexture.ensureValidity();
		mGameScreenTexture.setupAsRenderTarget(mGameResolution);
		mNativeRenderTargetReady = true;
	}

	const Recti viewport(0, 0, mGameResolution.x, mGameResolution.y);
	drawer.setRenderTarget(mGameScreenTexture, viewport);
	drawer.setBlendMode(BlendMode::OPAQUE);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
	drawer.drawRect(viewport, mRenderParts.getPaletteManager().getBackdropColor());

	bool scissorActive = false;
	for (const Geometry* geometry : geometries)
	{
		if (nullptr != geometry)
		{
			drawNativeGeometry(*geometry, scissorActive);
		}
	}
	if (scissorActive)
	{
		drawer.popScissor();
	}
	drawer.performRendering();
}

void GX2Renderer::renderHybridGameScreen(const std::vector<Geometry*>& geometries)
{
	ensureSoftwareRendererInitialized();
	mSoftwareRenderer.renderGameScreen(geometries);
#if defined(PLATFORM_WIIU)
	static uint32 sSoftwareBitmapLogCount = 0;
	static uint32 sSoftwareBitmapBlackLogCount = 0;
	if (!geometries.empty())
	{
		const BitmapSample sample = sampleBitmap(mGameScreenTexture.getBitmap());
		const bool shouldLog = (sample.nonBlackSamples > 0 && sSoftwareBitmapLogCount < 12)
			|| (sample.nonBlackSamples == 0 && sSoftwareBitmapBlackLogCount < 3);
		if (shouldLog)
		{
			RMX_LOG_INFO("GX2Renderer: software game bitmap sample"
				<< " index=" << sSoftwareBitmapLogCount
				<< " blackIndex=" << sSoftwareBitmapBlackLogCount
				<< " geometries=" << geometries.size()
				<< " size=" << mGameScreenTexture.getWidth() << "x" << mGameScreenTexture.getHeight()
				<< " nonBlack=" << sample.nonBlackSamples
				<< " alpha=" << sample.alphaSamples
				<< " xor=" << rmx::hexString(sample.sampleXor, 8)
				<< " center=" << rmx::hexString(sample.center, 8));
		}
		if (sample.nonBlackSamples > 0)
		{
			++sSoftwareBitmapLogCount;
		}
		else if (sSoftwareBitmapBlackLogCount < 3)
		{
			++sSoftwareBitmapBlackLogCount;
		}
	}
	if constexpr (ENABLE_GX2_RENDERER_DIAGNOSTIC_DUMPS)
	{
		static uint32 sHybridFrame = 0;
		++sHybridFrame;
		if (sHybridFrame == 300 || sHybridFrame == 900 || sHybridFrame == 1500)
		{
			char path[160];
			snprintf(path, sizeof(path), "/vol/external01/wiiu/apps/sonic3air/savedata/gx2_software_frame_%u.ppm", sHybridFrame);
			dumpBitmapPpmOnce(path, mGameScreenTexture.getBitmap());
		}
	}
#endif
	invalidateNativeRenderTarget();
}

DrawerTexture& GX2Renderer::updateGameScreenPresentTexture()
{
	mGameScreenPresentTextureIndex = 1 - mGameScreenPresentTextureIndex;
	DrawerTexture& texture = mGameScreenPresentTextures[mGameScreenPresentTextureIndex];
	Bitmap& renderedBitmap = mGameScreenTexture.accessBitmap();
	Bitmap& presentBitmap = texture.accessBitmap();
	if (presentBitmap.nonEmpty() && presentBitmap.getSize() == renderedBitmap.getSize())
	{
		presentBitmap.swap(renderedBitmap);
	}
	else
	{
		presentBitmap = renderedBitmap;
	}
	texture.bitmapUpdated();

#if defined(PLATFORM_WIIU)
	static uint32 sPresentTextureLogCount = 0;
	static uint32 sPresentTextureBlackLogCount = 0;
	const BitmapSample sample = sampleBitmap(texture.getBitmap());
	const bool shouldLog = (sample.nonBlackSamples > 0 && sPresentTextureLogCount < 10)
		|| (sample.nonBlackSamples == 0 && sPresentTextureBlackLogCount < 3);
	if (shouldLog)
	{
		RMX_LOG_INFO("GX2Renderer: uploaded game present texture index=" << mGameScreenPresentTextureIndex
			<< " size=" << texture.getWidth() << "x" << texture.getHeight()
			<< " nonBlack=" << sample.nonBlackSamples
			<< " alpha=" << sample.alphaSamples
			<< " xor=" << rmx::hexString(sample.sampleXor, 8)
			<< " center=" << rmx::hexString(sample.center, 8));
		if (sample.nonBlackSamples > 0)
			++sPresentTextureLogCount;
		else
			++sPresentTextureBlackLogCount;
	}
#endif
	return texture;
}

void GX2Renderer::ensureProcessingTexture()
{
	if (mProcessingTexture.getSize() == mGameResolution && mProcessingTexture.isRenderTarget())
		return;

	mProcessingTexture.invalidate();
	mProcessingTexture.ensureValidity();
	mProcessingTexture.setupAsRenderTarget(mGameResolution);
}

void GX2Renderer::copyNativeGameScreenToProcessingTexture(const Recti& viewport)
{
	ensureProcessingTexture();

	Drawer& drawer = EngineMain::instance().getDrawer();
	drawer.setRenderTarget(mProcessingTexture, viewport);
	drawer.setBlendMode(BlendMode::OPAQUE);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
	drawer.drawRect(viewport, mGameScreenTexture);
	drawer.performRendering();
	drawer.setRenderTarget(mGameScreenTexture, viewport);
}

void GX2Renderer::prepareHybridOverlayGeometries(const std::vector<Geometry*>& geometries, std::vector<Geometry*>& outSoftwareGeometries)
{
	mNativeOverlayGeometries.clear();
	mNativeOverlayUsesInitialViewport = false;
	outSoftwareGeometries.clear();
	outSoftwareGeometries.reserve(geometries.size());
	mNativeOverlayGeometries.reserve(geometries.size());

	if (geometries.empty())
		return;

	const bool usingSpriteMask = isUsingSpriteMask(geometries);
	bool hasPauseMenuViewport = false;
	bool allowDepthSensitiveSprites = true;
	for (const Geometry* geometry : geometries)
	{
		if (nullptr != geometry && geometry->getType() == Geometry::Type::VIEWPORT && geometry->mRenderQueue >= 0xfe00)
		{
			hasPauseMenuViewport = true;
		}
		if (nullptr != geometry && isDepthWritingGeometry(*geometry))
		{
			allowDepthSensitiveSprites = false;
		}
	}

	size_t overlayStart = geometries.size();
	if (hasPauseMenuViewport)
	{
		static uint32 sPauseSoftwareLogCount = 0;
		if (sPauseSoftwareLogCount < 8)
		{
			RMX_LOG_INFO("GX2Renderer: pause viewport detected; keeping pause geometry in software target");
		}
		++sPauseSoftwareLogCount;
	}
	(void)usingSpriteMask;
	(void)allowDepthSensitiveSprites;

	for (size_t i = overlayStart; i < geometries.size(); ++i)
	{
		const Geometry* geometry = geometries[i];
		if (nullptr != geometry && geometry->getType() == Geometry::Type::VIEWPORT && i != overlayStart)
		{
			overlayStart = geometries.size();
			break;
		}
	}

	Recti lastViewport;
	bool hasLastViewport = false;
	uint32 pinnedTextCount = 0;
	uint32 pinnedSpriteCount = 0;
	uint32 pinnedRectCount = 0;
	for (size_t i = 0; i < geometries.size(); ++i)
	{
		Geometry* geometry = geometries[i];
		if (nullptr != geometry && geometry->getType() == Geometry::Type::VIEWPORT && i < overlayStart)
		{
			lastViewport = geometry->as<ViewportGeometry>().mRect;
			hasLastViewport = true;
		}

		const bool suffixOverlay = (i >= overlayStart);
		const bool pinnedOverlay = !hasPauseMenuViewport
			&& !suffixOverlay
			&& nullptr != geometry
			&& canPinNativeOverlayGeometry(*geometry);

		if (!suffixOverlay && !pinnedOverlay)
		{
			outSoftwareGeometries.push_back(geometry);
		}
		else
		{
			mNativeOverlayGeometries.push_back(geometry);
			if (pinnedOverlay)
			{
				switch (geometry->getType())
				{
					case Geometry::Type::TEXTURED_RECT:	++pinnedTextCount; break;
					case Geometry::Type::SPRITE:		++pinnedSpriteCount; break;
					case Geometry::Type::RECT:			++pinnedRectCount; break;
					default:							break;
				}
			}
		}
	}

	if (!mNativeOverlayGeometries.empty())
	{
		mNativeOverlayInitialViewport = lastViewport;
		mNativeOverlayUsesInitialViewport = hasLastViewport;

		static uint32 sOverlayLogCount = 0;
		if (sOverlayLogCount < 12)
		{
			RMX_LOG_INFO("GX2Renderer: native overlay split software=" << outSoftwareGeometries.size()
				<< " nativeOverlay=" << mNativeOverlayGeometries.size()
				<< " pinnedText=" << pinnedTextCount
				<< " pinnedSprites=" << pinnedSpriteCount
				<< " pinnedRects=" << pinnedRectCount
				<< " pauseViewport=" << (hasPauseMenuViewport ? 1 : 0)
				<< " depthSensitiveSprites=" << (allowDepthSensitiveSprites ? 1 : 0)
				<< " initialViewport=" << (mNativeOverlayUsesInitialViewport ? 1 : 0));
		}
		++sOverlayLogCount;
	}
}

bool GX2Renderer::canPinNativeOverlayGeometry(const Geometry& geometry) const
{
	if (!canDrawNativeOverlayGeometry(geometry, true))
		return false;

	switch (geometry.getType())
	{
		case Geometry::Type::TEXTURED_RECT:
			return true;

		case Geometry::Type::RECT:
			return geometry.mRenderQueue >= 0x8000;

		case Geometry::Type::SPRITE:
			return false;

		default:
			return false;
	}
}

bool GX2Renderer::canDrawNativeOverlayGeometry(const Geometry& geometry, bool allowDepthSensitiveSprites) const
{
	switch (geometry.getType())
	{
		case Geometry::Type::RECT:
		case Geometry::Type::TEXTURED_RECT:
		case Geometry::Type::VIEWPORT:
			return supportsNativeGeometry(geometry);

		case Geometry::Type::SPRITE:
		{
			const SpriteGeometry& spriteGeometry = geometry.as<SpriteGeometry>();
			if (!supportsNativeSprite(spriteGeometry))
				return false;

			const renderitems::SpriteInfo& spriteInfo = spriteGeometry.mSpriteInfo;
			if (!allowDepthSensitiveSprites && !spriteInfo.mPriorityFlag)
				return false;

			switch (spriteInfo.getType())
			{
				case RenderItem::Type::COMPONENT_SPRITE:
				case RenderItem::Type::PALETTE_SPRITE:
				case RenderItem::Type::VDP_SPRITE:
					return true;

				default:
					return false;
			}
		}

		default:
			return false;
	}
}

bool GX2Renderer::isDepthWritingGeometry(const Geometry& geometry) const
{
	switch (geometry.getType())
	{
		case Geometry::Type::PLANE:
			return geometry.as<PlaneGeometry>().mPriorityFlag;

		case Geometry::Type::SPRITE:
		{
			const renderitems::SpriteInfo& spriteInfo = geometry.as<SpriteGeometry>().mSpriteInfo;
			return spriteInfo.mPriorityFlag || spriteInfo.getType() == RenderItem::Type::SPRITE_MASK;
		}

		default:
			return false;
	}
}

void GX2Renderer::drawNativeGeometry(const Geometry& geometry, bool& scissorActive)
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
			drawer.setWrapMode(TextureWrapMode::CLAMP);
			drawer.drawRect(rect.mRect, rect.mDrawerTexture, rect.mTintColor, rect.mAddedColor);
			break;
		}

		case Geometry::Type::SPRITE:
			drawNativeSprite(geometry.as<SpriteGeometry>());
			break;

		case Geometry::Type::PLANE:
			drawNativePlane(geometry.as<PlaneGeometry>());
			break;

		case Geometry::Type::VIEWPORT:
		{
			if (scissorActive)
			{
				drawer.popScissor();
			}
			drawer.pushScissor(geometry.as<ViewportGeometry>().mRect);
			scissorActive = true;
			break;
		}

		default:
			break;
	}
}

void GX2Renderer::drawNativePlane(const PlaneGeometry& geometry)
{
	EngineMain::instance().getDrawer().drawGX2Plane(geometry, mGameResolution, mRenderResources);
}

void GX2Renderer::drawNativeSprite(const SpriteGeometry& geometry)
{
	switch (geometry.mSpriteInfo.getType())
	{
		case RenderItem::Type::VDP_SPRITE:
			drawNativeVdpSprite(static_cast<const renderitems::VdpSpriteInfo&>(geometry.mSpriteInfo));
			break;

		case RenderItem::Type::COMPONENT_SPRITE:
			drawNativeComponentSprite(static_cast<const renderitems::ComponentSpriteInfo&>(geometry.mSpriteInfo));
			break;

		case RenderItem::Type::PALETTE_SPRITE:
			drawNativePaletteSprite(static_cast<const renderitems::PaletteSpriteInfo&>(geometry.mSpriteInfo));
			break;

		case RenderItem::Type::SPRITE_MASK:
			drawNativeSpriteMask(static_cast<const renderitems::SpriteMaskInfo&>(geometry.mSpriteInfo));
			break;

		default:
			break;
	}
}

void GX2Renderer::drawNativeVdpSprite(const renderitems::VdpSpriteInfo& spriteInfo)
{
	Drawer& drawer = EngineMain::instance().getDrawer();
	Color tintColor;
	Color addedColor;
	applySpriteTint(spriteInfo, tintColor, addedColor);

	drawer.setBlendMode(spriteInfo.mBlendMode);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
	const Recti rect(spriteInfo.mInterpolatedPosition, Vec2i(spriteInfo.mSize.x * 8, spriteInfo.mSize.y * 8));
	const int splitY = RenderParts::instance().getPaletteManager().mSplitPositionY;
	drawer.drawGX2VdpSprite(rect, spriteInfo.mSize, spriteInfo.mFirstPattern, splitY, tintColor, addedColor, mRenderResources);
}

void GX2Renderer::drawNativeComponentSprite(const renderitems::ComponentSpriteInfo& spriteInfo)
{
	Drawer& drawer = EngineMain::instance().getDrawer();
	Color tintColor;
	Color addedColor;
	applySpriteTint(spriteInfo, tintColor, addedColor);
	traceNativeSprite("component", spriteInfo);

	DrawerTexture& texture = mRenderResources.getComponentSpriteTexture(spriteInfo);
	drawer.setBlendMode(spriteInfo.mBlendMode);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
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

void GX2Renderer::drawNativePaletteSprite(const renderitems::PaletteSpriteInfo& spriteInfo)
{
	Drawer& drawer = EngineMain::instance().getDrawer();
	Color tintColor;
	Color addedColor;
	applySpriteTint(spriteInfo, tintColor, addedColor);
	traceNativeSprite("palette", spriteInfo);

	const PaletteManager& paletteManager = RenderParts::instance().getPaletteManager();
	const PaletteBase& primaryPalette = (nullptr == spriteInfo.mPrimaryPalette) ? paletteManager.getMainPalette(0) : *spriteInfo.mPrimaryPalette;
	const PaletteBase& secondaryPalette = (nullptr == spriteInfo.mSecondaryPalette) ? paletteManager.getMainPalette(1) : *spriteInfo.mSecondaryPalette;
	drawer.setBlendMode(spriteInfo.mBlendMode);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
	DrawerTexture& dataTexture = mRenderResources.getPaletteSpriteDataTexture(spriteInfo, primaryPalette, secondaryPalette);
	if (spriteInfo.mTransformation.isIdentity())
	{
		const Recti rect(spriteInfo.mInterpolatedPosition + spriteInfo.mPivotOffset, spriteInfo.mSize);
		drawer.drawGX2PaletteSprite(rect, dataTexture, paletteManager.mSplitPositionY, spriteInfo.mAtex, tintColor, addedColor);
	}
	else
	{
		std::vector<DrawerMeshVertex> vertices;
		appendPaletteSpriteQuad(vertices, spriteInfo);
		drawer.drawGX2PaletteSprite(vertices, spriteInfo.mSize, dataTexture, paletteManager.mSplitPositionY, spriteInfo.mAtex, tintColor, addedColor);
	}
}

void GX2Renderer::drawNativeSpriteMask(const renderitems::SpriteMaskInfo& spriteInfo)
{
	if (!mProcessingTexture.isValid() || !mProcessingTexture.isRenderTarget())
		return;

	Recti rect(spriteInfo.mInterpolatedPosition, spriteInfo.mSize);
	rect.intersect(Recti(0, 0, mGameResolution.x, mGameResolution.y));
	if (rect.empty())
		return;

	Drawer& drawer = EngineMain::instance().getDrawer();
	drawer.setBlendMode(BlendMode::OPAQUE);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
	drawer.drawRect(rect, mProcessingTexture, rect, Color::WHITE);
}

void GX2Renderer::invalidateNativeRenderTarget()
{
	mNativeRenderTargetReady = false;
}
