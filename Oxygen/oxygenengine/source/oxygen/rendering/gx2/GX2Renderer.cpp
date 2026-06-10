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
	constexpr bool ENABLE_GX2_RENDERER_COMPATIBILITY_LOGS = false;
	constexpr bool FORCE_SOFTWARE_GAME_RENDERER = false;
	constexpr bool FORCE_NATIVE_WINDOW_GAME_OFFSCREEN = true;
	constexpr bool USE_INDEXED_PALETTE_SPRITE_SHADER = true;
	constexpr bool USE_ATLAS_VDP_SPRITE_SHADER = true;
	constexpr bool ENABLE_GX2_FALLBACK_BITMAP_LOGS = false;
	constexpr bool ENABLE_NATIVE_PRIORITY_DEPTH = true;
#if defined(PLATFORM_WIIU)
	constexpr int WIIU_SAFE_GAME_WIDTH = 400;
	constexpr int WIIU_SAFE_GAME_HEIGHT = 224;
#endif

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

	bool usesBackgroundBlur(const std::vector<Geometry*>& geometries)
	{
		if (Configuration::instance().mBackgroundBlur <= 0)
			return false;

		for (const Geometry* geometry : geometries)
		{
			if (nullptr != geometry && geometry->getType() == Geometry::Type::EFFECT_BLUR)
				return true;
		}
		return false;
	}
// use software rendering here, GX2 rendering doesn't yet support this and it's used in some non-gameplay scenes
// add GX2 support for it eventually. for now this is a pretty nasty hack
	bool isDefaultPlaneRenderQueue(uint16 renderQueue)
	{
		switch (renderQueue)
		{
			case 0x1000:
			case 0x2000:
			case 0x2200:
			case 0x3000:
			case 0x4000:
			case 0x4200:
				return true;

			default:
				return false;
		}
	}

	bool spriteNeedsSoftwareParityFallback(const SpriteGeometry& geometry)
	{
		const renderitems::SpriteInfo& spriteInfo = geometry.mSpriteInfo;
		switch (spriteInfo.getType())
		{
			case RenderItem::Type::COMPONENT_SPRITE:
			{
				const renderitems::ComponentSpriteInfo& componentSprite = static_cast<const renderitems::ComponentSpriteInfo&>(spriteInfo);
				return !componentSprite.mTransformation.isIdentity();
			}

			case RenderItem::Type::PALETTE_SPRITE:
			{
				const renderitems::PaletteSpriteInfo& paletteSprite = static_cast<const renderitems::PaletteSpriteInfo&>(spriteInfo);
				return !paletteSprite.mTransformation.isIdentity();
			}

			default:
				return false;
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

	BitmapSample sampleBitmapRegion(const Bitmap& bitmap, const Recti& requestedRect)
	{
		BitmapSample sample;
		if (bitmap.empty())
			return sample;

		const int width = bitmap.getWidth();
		const int height = bitmap.getHeight();
		const int x0 = clamp(requestedRect.x, 0, width);
		const int y0 = clamp(requestedRect.y, 0, height);
		const int x1 = clamp(requestedRect.x + requestedRect.width, 0, width);
		const int y1 = clamp(requestedRect.y + requestedRect.height, 0, height);
		const int regionWidth = x1 - x0;
		const int regionHeight = y1 - y0;
		if (regionWidth <= 0 || regionHeight <= 0)
			return sample;

		sample.center = *bitmap.getPixelPointer(x0 + regionWidth / 2, y0 + regionHeight / 2);
		for (int yIndex = 0; yIndex < 8; ++yIndex)
		{
			const int y = y0 + ((regionHeight <= 1) ? 0 : (int)(((uint64)yIndex * (uint64)(regionHeight - 1)) / 7u));
			for (int xIndex = 0; xIndex < 8; ++xIndex)
			{
				const int x = x0 + ((regionWidth <= 1) ? 0 : (int)(((uint64)xIndex * (uint64)(regionWidth - 1)) / 7u));
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

	BitmapSample sampleBitmap(const Bitmap& bitmap)
	{
		if (bitmap.empty())
			return BitmapSample();

		return sampleBitmapRegion(bitmap, Recti(0, 0, bitmap.getWidth(), bitmap.getHeight()));
	}

#if defined(PLATFORM_WIIU)
	Recti getWiiUSafeSourceRect(const DrawerTexture& texture)
	{
		return Recti(0, 0, std::min(WIIU_SAFE_GAME_WIDTH, texture.getWidth()), std::min(WIIU_SAFE_GAME_HEIGHT, texture.getHeight()));
	}

	Recti getWiiUPresentRect(const Recti& viewport, const Recti& sourceRect)
	{
		return Recti(viewport.x + (viewport.width - sourceRect.width) / 2,
			viewport.y + (viewport.height - sourceRect.height) / 2,
			sourceRect.width,
			sourceRect.height);
	}

	void drawWiiUPresentedGameTexture(Drawer& drawer, const Recti& viewport, DrawerTexture& texture)
	{
		const Recti sourceRect = getWiiUSafeSourceRect(texture);
		if (!sourceRect.empty())
		{
			drawer.drawRect(viewport, texture, sourceRect, Color::WHITE);
		}
	}
#endif

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
				<< " priority=" << (spriteInfo.mPriorityFlag ? 1 : 0)
				<< " globalTint=" << (spriteInfo.mUseGlobalComponentTint ? 1 : 0)
				<< " tint=" << spriteInfo.mTintColor.r << "," << spriteInfo.mTintColor.g << "," << spriteInfo.mTintColor.b << "," << spriteInfo.mTintColor.a
				<< " added=" << spriteInfo.mAddedColor.r << "," << spriteInfo.mAddedColor.g << "," << spriteInfo.mAddedColor.b << "," << spriteInfo.mAddedColor.a
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
		RMX_LOG_INFO("GX2Renderer: native GX2 game renderer enabled; software game bouncebacks disabled");
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
	mRenderResources.clearAllCaches();
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
	resetNativeBlurProcessingState();
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

	if constexpr (ENABLE_GX2_RENDERER_COMPATIBILITY_LOGS)
	{
		if (shouldUseSoftwareGameRendering(geometries))
		{
			static uint32 sParityFallbackLogCount = 0;
			if (sParityFallbackLogCount < 8)
			{
				RMX_LOG_INFO("GX2Renderer: formerly-software parity frame is using native GX2");
				++sParityFallbackLogCount;
			}
		}
	}

	mRenderResources.refresh();
	if constexpr (ENABLE_GX2_RENDERER_COMPATIBILITY_LOGS)
	{
		if (!supportsNativeRendering(geometries))
		{
			static uint32 sFallbackLogCount = 0;
			if (sFallbackLogCount < 8)
			{
				for (const Geometry* geometry : geometries)
				{
					if (nullptr != geometry && !supportsNativeGeometry(*geometry))
					{
						RMX_LOG_INFO("GX2Renderer: native offscreen path will skip unsupported geometry=" << geometryTypeName(geometry->getType())
							<< ((geometry->getType() == Geometry::Type::SPRITE) ? " sprite=" : "")
							<< ((geometry->getType() == Geometry::Type::SPRITE) ? renderItemTypeName(geometry->as<SpriteGeometry>().mSpriteInfo.getType()) : ""));
						++sFallbackLogCount;
						break;
					}
				}
			}
		}
	}

	renderNativeGameScreen(geometries);
}

void GX2Renderer::renderGameScreenToCurrentTarget(const std::vector<Geometry*>& geometries, const Recti& targetRect)
{
	resetNativeBlurProcessingState();
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
		DrawerTexture& presentTexture = updateGameScreenPresentTexture();
		if (!mNativeOverlayGeometries.empty())
		{
			mRenderResources.refresh();
		}
		drawer.setWindowRenderTarget(viewport);
		resetNativeQueuedState();
		setNativeBlendMode(drawer, BlendMode::OPAQUE);
		drawer.setSamplingMode(SamplingMode::POINT);
		drawer.setWrapMode(TextureWrapMode::CLAMP);
		drawer.drawRect(viewport, presentTexture);
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
		mCurrentTargetAlreadyHasNativeFrame = true;
		return;
	}

	if constexpr (ENABLE_GX2_RENDERER_COMPATIBILITY_LOGS)
	{
		if (shouldUseSoftwareGameRendering(geometries))
		{
			static uint32 sDirectParityFallbackLogCount = 0;
			if (sDirectParityFallbackLogCount < 8)
			{
				RMX_LOG_INFO("GX2Renderer: formerly-software direct frame is using native GX2");
				++sDirectParityFallbackLogCount;
			}
		}
	}

	mRenderResources.refresh();

#if defined(PLATFORM_WIIU)
	if constexpr (ENABLE_GX2_RENDERER_FRAME_LOGS)
	{
		static uint32 sDirectSummaryLogCount = 0;
		if (sDirectSummaryLogCount < 12)
		{
			uint32 planeCount = 0;
			uint32 spriteCount = 0;
			uint32 vdpSpriteCount = 0;
			uint32 paletteSpriteCount = 0;
			uint32 componentSpriteCount = 0;
			uint32 maskSpriteCount = 0;
			uint32 rectCount = 0;
			uint32 texturedRectCount = 0;
			uint32 blurCount = 0;
			uint32 viewportCount = 0;
			uint32 nativeCount = 0;
			for (const Geometry* geometry : geometries)
			{
				if (nullptr == geometry)
					continue;
				if (supportsNativeGeometry(*geometry))
					++nativeCount;
				switch (geometry->getType())
				{
					case Geometry::Type::PLANE:
						++planeCount;
						break;
					case Geometry::Type::SPRITE:
					{
						++spriteCount;
						switch (geometry->as<SpriteGeometry>().mSpriteInfo.getType())
						{
							case RenderItem::Type::VDP_SPRITE:		++vdpSpriteCount; break;
							case RenderItem::Type::PALETTE_SPRITE:	++paletteSpriteCount; break;
							case RenderItem::Type::COMPONENT_SPRITE:	++componentSpriteCount; break;
							case RenderItem::Type::SPRITE_MASK:		++maskSpriteCount; break;
							default: break;
						}
						break;
					}
					case Geometry::Type::RECT:
						++rectCount;
						break;
					case Geometry::Type::TEXTURED_RECT:
						++texturedRectCount;
						break;
					case Geometry::Type::EFFECT_BLUR:
						++blurCount;
						break;
					case Geometry::Type::VIEWPORT:
						++viewportCount;
						break;
					default:
						break;
				}
			}
			RMX_LOG_INFO("GX2Renderer: direct frame geometries=" << geometries.size()
				<< " native=" << nativeCount
				<< " planes=" << planeCount
				<< " sprites=" << spriteCount
				<< " vdp=" << vdpSpriteCount
				<< " palette=" << paletteSpriteCount
				<< " component=" << componentSpriteCount
				<< " mask=" << maskSpriteCount
				<< " rects=" << rectCount
				<< " texturedRects=" << texturedRectCount
				<< " blur=" << blurCount
				<< " viewports=" << viewportCount
				<< " target=" << viewport.x << "," << viewport.y << " " << viewport.width << "x" << viewport.height);
			++sDirectSummaryLogCount;
		}
	}
#endif

	if constexpr (ENABLE_GX2_RENDERER_COMPATIBILITY_LOGS)
	{
		if (!supportsNativeRendering(geometries))
		{
			static uint32 sDirectFallbackLogCount = 0;
			if (sDirectFallbackLogCount < 8)
			{
				for (const Geometry* geometry : geometries)
				{
					if (nullptr != geometry && !supportsNativeGeometry(*geometry))
					{
						RMX_LOG_INFO("GX2Renderer: native direct path will skip unsupported geometry=" << geometryTypeName(geometry->getType())
							<< ((geometry->getType() == Geometry::Type::SPRITE) ? " sprite=" : "")
							<< ((geometry->getType() == Geometry::Type::SPRITE) ? renderItemTypeName(geometry->as<SpriteGeometry>().mSpriteInfo.getType()) : ""));
						++sDirectFallbackLogCount;
						break;
					}
				}
			}
		}
	}

	if (requiresOffscreenNativeRendering(geometries))
	{
		static bool sLoggedOffscreenWindowPath = false;
		if (!sLoggedOffscreenWindowPath)
		{
			sLoggedOffscreenWindowPath = true;
			RMX_LOG_INFO("GX2Renderer: native window path uses logical render target before pixel-perfect TV presentation");
		}
		renderNativeGameScreen(geometries);
		drawer.setWindowRenderTarget(viewport);
		resetNativeQueuedState();
		setNativeBlendMode(drawer, BlendMode::OPAQUE);
		drawer.setSamplingMode(SamplingMode::POINT);
		drawer.setWrapMode(TextureWrapMode::CLAMP);
#if defined(PLATFORM_WIIU)
		drawWiiUPresentedGameTexture(drawer, viewport, mGameScreenTexture);
#else
		drawer.drawRect(viewport, mGameScreenTexture);
#endif
		mCurrentTargetAlreadyHasNativeFrame = true;
		return;
	}

	mNativeOverlayGeometries.clear();
	mNativeOverlayUsesInitialViewport = false;
	invalidateNativeRenderTarget();
	drawer.setWindowRenderTarget(viewport);
	resetNativeQueuedState();
	setNativeBlendMode(drawer, BlendMode::OPAQUE);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
	Color backdropColor = mRenderParts.getPaletteManager().getBackdropColor();
	backdropColor.a = 1.0f;
	drawer.drawRect(viewport, backdropColor);

	const bool useBlurProcessing = usesBackgroundBlur(geometries);
	if (useBlurProcessing)
	{
		beginNativeBlurProcessingTarget(drawer, true, viewport);
	}

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
				resetNativeQueuedState();
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
	drawer.performRendering();
	resetNativeQueuedState();
	mCurrentTargetAlreadyHasNativeFrame = true;
}

bool GX2Renderer::canDrawPresentedGameScreenToCurrentTarget() const
{
	const DrawerTexture& presentTexture = mGameScreenPresentTextures[mGameScreenPresentTextureIndex];
	return mCurrentTargetAlreadyHasNativeFrame || mNativeRenderTargetReady || !presentTexture.getBitmap().empty();
}

bool GX2Renderer::drawPresentedGameScreenToCurrentTarget(const Recti& targetRect)
{
	if (mCurrentTargetAlreadyHasNativeFrame)
	{
		mCurrentTargetAlreadyHasNativeFrame = false;
		return true;
	}

	DrawerTexture& presentTexture = mGameScreenPresentTextures[mGameScreenPresentTextureIndex];
	DrawerTexture* texture = mNativeRenderTargetReady ? &mGameScreenTexture : (presentTexture.getBitmap().empty() ? nullptr : &presentTexture);
	if (nullptr == texture)
		return false;

	static bool sLoggedLateGameDraw = false;
	if (!sLoggedLateGameDraw)
	{
		sLoggedLateGameDraw = true;
		RMX_LOG_INFO("GX2Renderer: drawing presented game screen after logical target bind");
	}

	Drawer& drawer = EngineMain::instance().getDrawer();
	const Recti viewport = targetRect.empty() ? Recti(0, 0, mGameResolution.x, mGameResolution.y) : targetRect;
	resetNativeQueuedState();
	setNativeBlendMode(drawer, BlendMode::OPAQUE);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
#if defined(PLATFORM_WIIU)
	drawWiiUPresentedGameTexture(drawer, viewport, *texture);
#else
	drawer.drawRect(viewport, *texture);
#endif

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
	return true;
}

void GX2Renderer::renderDebugDraw(int debugDrawMode, const Recti& rect)
{
#if defined(PLATFORM_WIIU)
	(void)debugDrawMode;
	(void)rect;
	static bool sLoggedDebugDrawDisabled = false;
	if (!sLoggedDebugDrawDisabled)
	{
		sLoggedDebugDrawDisabled = true;
		RMX_LOG_INFO("GX2Renderer: debug draw skipped on pure native GX2");
	}
#else
	ensureSoftwareRendererInitialized();
	mSoftwareRenderer.renderDebugDraw(debugDrawMode, rect);
	invalidateNativeRenderTarget();
#endif
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

bool GX2Renderer::shouldUseSoftwareGameRendering(const std::vector<Geometry*>& geometries) const
{
	bool wouldHaveUsedSoftware = false;
	for (const Geometry* geometry : geometries)
	{
		if (nullptr == geometry)
			continue;

		switch (geometry->getType())
		{
			case Geometry::Type::PLANE:
			{
				const PlaneGeometry& plane = geometry->as<PlaneGeometry>();
				if (!isDefaultPlaneRenderQueue(plane.mRenderQueue))
					wouldHaveUsedSoftware = true;
				break;
			}

			case Geometry::Type::SPRITE:
			{
				if (spriteNeedsSoftwareParityFallback(geometry->as<SpriteGeometry>()))
					wouldHaveUsedSoftware = true;
				break;
			}

			case Geometry::Type::VIEWPORT:
			case Geometry::Type::EFFECT_BLUR:
				wouldHaveUsedSoftware = true;
				break;

			default:
				break;
		}
	}
#if defined(PLATFORM_WIIU)
	if (wouldHaveUsedSoftware)
	{
		static uint32 sNativeBouncebackLogCount = 0;
		if (sNativeBouncebackLogCount < 8)
		{
			RMX_LOG_INFO("GX2Renderer: native GX2 is handling a former software bounceback frame");
			++sNativeBouncebackLogCount;
		}
	}
#endif
	return false;
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
			return true;

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
#if defined(PLATFORM_WIIU)
	if constexpr (FORCE_NATIVE_WINDOW_GAME_OFFSCREEN)
		return true;
#endif
	return isUsingSpriteMask(geometries);
}

void GX2Renderer::renderNativeGameScreen(const std::vector<Geometry*>& geometries)
{
	resetNativeBlurProcessingState();
	mNativeOverlayGeometries.clear();
	mNativeOverlayUsesInitialViewport = false;
	mCurrentTargetAlreadyHasNativeFrame = false;
	Drawer& drawer = EngineMain::instance().getDrawer();
	if (!mNativeRenderTargetReady || mGameScreenTexture.getSize() != mGameResolution)
	{
		// Software fallback updates the same DrawerTexture as a bitmap-backed texture. Recreate it as a GX2 render target before native drawing.
		mGameScreenTexture.invalidate();
		mGameScreenTexture.ensureValidity();
		mGameScreenTexture.setContentKnownOpaque(true);
		mGameScreenTexture.setupAsRenderTarget(mGameResolution);
		mNativeRenderTargetReady = true;
	}

	const Recti viewport(0, 0, mGameResolution.x, mGameResolution.y);
	drawer.setRenderTarget(mGameScreenTexture, viewport);
	resetNativeQueuedState();
	setNativeBlendMode(drawer, BlendMode::OPAQUE);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
	if constexpr (ENABLE_NATIVE_PRIORITY_DEPTH)
	{
		drawer.clearGX2Depth();
	}
	Color backdropColor = mRenderParts.getPaletteManager().getBackdropColor();
	backdropColor.a = 1.0f;
	drawer.drawRect(viewport, backdropColor);

	const bool useBlurProcessing = usesBackgroundBlur(geometries);
	if (useBlurProcessing)
	{
		beginNativeBlurProcessingTarget(drawer, false, viewport);
	}

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
	resetNativeQueuedState();
}

void GX2Renderer::renderHybridGameScreen(const std::vector<Geometry*>& geometries)
{
#if defined(PLATFORM_WIIU)
	static uint32 sNativeHybridRedirectLogCount = 0;
	if (sNativeHybridRedirectLogCount < 4)
	{
		RMX_LOG_INFO("GX2Renderer: hybrid software path redirected to native GX2");
		++sNativeHybridRedirectLogCount;
	}
	renderNativeGameScreen(geometries);
#else
	ensureSoftwareRendererInitialized();
	mSoftwareRenderer.renderGameScreen(geometries);
	if constexpr (ENABLE_GX2_FALLBACK_BITMAP_LOGS)
	{
		static uint32 sSoftwareBitmapLogCount = 0;
		static uint32 sSoftwareBitmapBlackLogCount = 0;
		static uint32 sSoftwareBitmapFrame = 0;
		++sSoftwareBitmapFrame;
		if (!geometries.empty())
		{
			const Bitmap& gameBitmap = mGameScreenTexture.getBitmap();
			const int bitmapWidth = gameBitmap.getWidth();
			const int bitmapHeight = gameBitmap.getHeight();
			const BitmapSample sample = sampleBitmap(gameBitmap);
			const BitmapSample leftSample = sampleBitmapRegion(gameBitmap, Recti(0, 0, std::min(bitmapWidth, 160), bitmapHeight));
			const BitmapSample middleSample = sampleBitmapRegion(gameBitmap, Recti(std::min(bitmapWidth, 160), 0, std::max(0, std::min(bitmapWidth, 240) - std::min(bitmapWidth, 160)), bitmapHeight));
			const BitmapSample rightSample = sampleBitmapRegion(gameBitmap, Recti(std::min(bitmapWidth, 240), 0, std::max(0, bitmapWidth - std::min(bitmapWidth, 240)), bitmapHeight));
			const bool shouldLog = (sample.nonBlackSamples > 0 && sSoftwareBitmapLogCount < 12)
				|| (sample.nonBlackSamples == 0 && sSoftwareBitmapBlackLogCount < 3);
			if (shouldLog)
			{
				RMX_LOG_INFO("GX2Renderer: software game bitmap sample"
					<< " frame=" << sSoftwareBitmapFrame
					<< " index=" << sSoftwareBitmapLogCount
					<< " blackIndex=" << sSoftwareBitmapBlackLogCount
					<< " geometries=" << geometries.size()
					<< " size=" << mGameScreenTexture.getWidth() << "x" << mGameScreenTexture.getHeight()
					<< " nonBlack=" << sample.nonBlackSamples
					<< " alpha=" << sample.alphaSamples
					<< " xor=" << rmx::hexString(sample.sampleXor, 8)
					<< " center=" << rmx::hexString(sample.center, 8)
					<< " left=" << leftSample.nonBlackSamples << "/" << leftSample.alphaSamples << "/" << rmx::hexString(leftSample.center, 8)
					<< " middle=" << middleSample.nonBlackSamples << "/" << middleSample.alphaSamples << "/" << rmx::hexString(middleSample.center, 8)
					<< " right=" << rightSample.nonBlackSamples << "/" << rightSample.alphaSamples << "/" << rmx::hexString(rightSample.center, 8));
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
	invalidateNativeRenderTarget();
#endif
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
	if constexpr (ENABLE_GX2_FALLBACK_BITMAP_LOGS)
	{
		static uint32 sPresentTextureLogCount = 0;
		static uint32 sPresentTextureBlackLogCount = 0;
		static uint32 sPresentTextureFrame = 0;
		++sPresentTextureFrame;
		const Bitmap& uploadedBitmap = texture.getBitmap();
		const int bitmapWidth = uploadedBitmap.getWidth();
		const int bitmapHeight = uploadedBitmap.getHeight();
		const BitmapSample sample = sampleBitmap(uploadedBitmap);
		const BitmapSample leftSample = sampleBitmapRegion(uploadedBitmap, Recti(0, 0, std::min(bitmapWidth, 160), bitmapHeight));
		const BitmapSample middleSample = sampleBitmapRegion(uploadedBitmap, Recti(std::min(bitmapWidth, 160), 0, std::max(0, std::min(bitmapWidth, 240) - std::min(bitmapWidth, 160)), bitmapHeight));
		const BitmapSample rightSample = sampleBitmapRegion(uploadedBitmap, Recti(std::min(bitmapWidth, 240), 0, std::max(0, bitmapWidth - std::min(bitmapWidth, 240)), bitmapHeight));
		const bool shouldLog = (sample.nonBlackSamples > 0 && sPresentTextureLogCount < 10)
			|| (sample.nonBlackSamples == 0 && sPresentTextureBlackLogCount < 3);
		if (shouldLog)
		{
			RMX_LOG_INFO("GX2Renderer: uploaded game present texture frame=" << sPresentTextureFrame
				<< " index=" << mGameScreenPresentTextureIndex
				<< " size=" << texture.getWidth() << "x" << texture.getHeight()
				<< " nonBlack=" << sample.nonBlackSamples
				<< " alpha=" << sample.alphaSamples
				<< " xor=" << rmx::hexString(sample.sampleXor, 8)
				<< " center=" << rmx::hexString(sample.center, 8)
				<< " left=" << leftSample.nonBlackSamples << "/" << leftSample.alphaSamples << "/" << rmx::hexString(leftSample.center, 8)
				<< " middle=" << middleSample.nonBlackSamples << "/" << middleSample.alphaSamples << "/" << rmx::hexString(middleSample.center, 8)
				<< " right=" << rightSample.nonBlackSamples << "/" << rightSample.alphaSamples << "/" << rmx::hexString(rightSample.center, 8));
			if (sample.nonBlackSamples > 0)
				++sPresentTextureLogCount;
			else
				++sPresentTextureBlackLogCount;
		}
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
	resetNativeQueuedState();
	setNativeBlendMode(drawer, BlendMode::OPAQUE);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
	drawer.drawRect(viewport, mGameScreenTexture);
	drawer.performRendering();
	resetNativeQueuedState();
	drawer.setRenderTarget(mGameScreenTexture, viewport);
	resetNativeQueuedState();
}

void GX2Renderer::resetNativeBlurProcessingState()
{
	mNativeBlurProcessingActive = false;
	mNativeBlurOutputIsWindow = false;
	mNativeBlurOutputViewport = Recti();
}

void GX2Renderer::beginNativeBlurProcessingTarget(Drawer& drawer, bool outputIsWindow, const Recti& outputViewport)
{
	ensureProcessingTexture();

	mNativeBlurProcessingActive = true;
	mNativeBlurOutputIsWindow = outputIsWindow;
	mNativeBlurOutputViewport = outputViewport;

	const Recti processingViewport(0, 0, mGameResolution.x, mGameResolution.y);
	drawer.setRenderTarget(mProcessingTexture, processingViewport);
	resetNativeQueuedState();
	setNativeBlendMode(drawer, BlendMode::OPAQUE);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
	Color backdropColor = mRenderParts.getPaletteManager().getBackdropColor();
	backdropColor.a = 1.0f;
	drawer.drawRect(processingViewport, backdropColor);
}

void GX2Renderer::restoreNativeBlurOutputTarget(Drawer& drawer)
{
	if (mNativeBlurOutputIsWindow)
	{
		drawer.setWindowRenderTarget(mNativeBlurOutputViewport);
	}
	else
	{
		drawer.setRenderTarget(mGameScreenTexture, mNativeBlurOutputViewport);
	}
	resetNativeQueuedState();
}

void GX2Renderer::resetNativeQueuedState()
{
	mHasNativeQueuedBlendMode = false;
}

void GX2Renderer::setNativeBlendMode(Drawer& drawer, BlendMode blendMode)
{
	if (!mHasNativeQueuedBlendMode || mNativeQueuedBlendMode != blendMode)
	{
		drawer.setBlendMode(blendMode);
		mNativeQueuedBlendMode = blendMode;
		mHasNativeQueuedBlendMode = true;
	}
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
			&& canPinNativeOverlayGeometry(*geometry, allowDepthSensitiveSprites);

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

bool GX2Renderer::canPinNativeOverlayGeometry(const Geometry& geometry, bool allowDepthSensitiveSprites) const
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
		{
			const SpriteGeometry& spriteGeometry = geometry.as<SpriteGeometry>();
			if (geometry.mRenderQueue < 0x8000)
				return false;
			if (!allowDepthSensitiveSprites && !spriteGeometry.mSpriteInfo.mPriorityFlag && geometry.mRenderQueue < 0xc000)
				return false;
			return true;
		}

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
			setNativeBlendMode(drawer, BlendMode::ALPHA);
			drawer.drawRect(rect.mRect, rect.mColor);
			break;
		}

		case Geometry::Type::TEXTURED_RECT:
		{
			const TexturedRectGeometry& rect = geometry.as<TexturedRectGeometry>();
			setNativeBlendMode(drawer, BlendMode::ALPHA);
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

		case Geometry::Type::EFFECT_BLUR:
			if (scissorActive)
			{
				drawer.popScissor();
				scissorActive = false;
			}
			drawNativeBlur(geometry.as<EffectBlurGeometry>());
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
	if (!isBlendModeSupported(spriteInfo.mBlendMode))
		return;

	Drawer& drawer = EngineMain::instance().getDrawer();
	Color tintColor;
	Color addedColor;
	applySpriteTint(spriteInfo, tintColor, addedColor);

	setNativeBlendMode(drawer, spriteInfo.mBlendMode);
	const Recti rect(spriteInfo.mInterpolatedPosition, Vec2i(spriteInfo.mSize.x * 8, spriteInfo.mSize.y * 8));
	if constexpr (USE_ATLAS_VDP_SPRITE_SHADER)
	{
		const PaletteManager& paletteManager = RenderParts::instance().getPaletteManager();
		const bool shadowHighlightMode = paletteManager.useShadowHighlightMode();
		drawer.drawGX2VdpSprite(rect, spriteInfo.mSize, spriteInfo.mFirstPattern, paletteManager.mSplitPositionY, tintColor, addedColor, spriteInfo.mPriorityFlag, shadowHighlightMode, mRenderResources);
	}
	else
	{
		DrawerTexture& texture = mRenderResources.getVdpSpriteTexture(spriteInfo);
		drawer.drawGX2TextureSprite(rect, texture, tintColor, addedColor, spriteInfo.mPriorityFlag);
	}
}

void GX2Renderer::drawNativeComponentSprite(const renderitems::ComponentSpriteInfo& spriteInfo)
{
	if (spriteInfo.mSize.x <= 0 || spriteInfo.mSize.y <= 0 || nullptr == spriteInfo.mCacheItem || nullptr == spriteInfo.mCacheItem->mSprite || !isBlendModeSupported(spriteInfo.mBlendMode))
		return;

	Drawer& drawer = EngineMain::instance().getDrawer();
	Color tintColor;
	Color addedColor;
	applySpriteTint(spriteInfo, tintColor, addedColor);
	traceNativeSprite("component", spriteInfo);

	DrawerTexture& texture = mRenderResources.getComponentSpriteTexture(spriteInfo);
	setNativeBlendMode(drawer, spriteInfo.mBlendMode);
	if (spriteInfo.mTransformation.isIdentity())
	{
		drawer.drawGX2TextureSprite(Recti(spriteInfo.mInterpolatedPosition + spriteInfo.mPivotOffset, spriteInfo.mSize), texture, tintColor, addedColor, spriteInfo.mPriorityFlag);
	}
	else
	{
		std::vector<DrawerMeshVertex> vertices;
		appendSpriteQuad(vertices, spriteInfo);
		drawer.drawGX2TextureSprite(vertices, texture, tintColor, addedColor, spriteInfo.mPriorityFlag);
	}
}

void GX2Renderer::drawNativePaletteSprite(const renderitems::PaletteSpriteInfo& spriteInfo)
{
	if (spriteInfo.mSize.x <= 0 || spriteInfo.mSize.y <= 0 || nullptr == spriteInfo.mCacheItem || nullptr == spriteInfo.mCacheItem->mSprite || !isBlendModeSupported(spriteInfo.mBlendMode))
		return;

	Drawer& drawer = EngineMain::instance().getDrawer();
	Color tintColor;
	Color addedColor;
	applySpriteTint(spriteInfo, tintColor, addedColor);
	traceNativeSprite("palette", spriteInfo);

	const PaletteManager& paletteManager = RenderParts::instance().getPaletteManager();
	const PaletteBase& primaryPalette = (nullptr == spriteInfo.mPrimaryPalette) ? paletteManager.getMainPalette(0) : *spriteInfo.mPrimaryPalette;
	const PaletteBase& secondaryPalette = (nullptr == spriteInfo.mSecondaryPalette) ? paletteManager.getMainPalette(1) : *spriteInfo.mSecondaryPalette;
	setNativeBlendMode(drawer, spriteInfo.mBlendMode);
	if constexpr (USE_INDEXED_PALETTE_SPRITE_SHADER)
	{
		DrawerTexture& dataTexture = mRenderResources.getPaletteSpriteDataTexture(spriteInfo, primaryPalette, secondaryPalette);
		if (spriteInfo.mTransformation.isIdentity())
		{
			const Recti rect(spriteInfo.mInterpolatedPosition + spriteInfo.mPivotOffset, spriteInfo.mSize);
			const bool shadowHighlightMode = paletteManager.useShadowHighlightMode();
			drawer.drawGX2PaletteSprite(rect, dataTexture, paletteManager.mSplitPositionY, spriteInfo.mAtex, tintColor, addedColor, spriteInfo.mPriorityFlag, shadowHighlightMode);
		}
		else
		{
			std::vector<DrawerMeshVertex> vertices;
			appendPaletteSpriteQuad(vertices, spriteInfo);
			const bool shadowHighlightMode = paletteManager.useShadowHighlightMode();
			drawer.drawGX2PaletteSprite(vertices, spriteInfo.mSize, dataTexture, paletteManager.mSplitPositionY, spriteInfo.mAtex, tintColor, addedColor, spriteInfo.mPriorityFlag, shadowHighlightMode);
		}
	}
	else
	{
		(void)secondaryPalette;
		DrawerTexture& texture = mRenderResources.getPaletteSpriteTexture(spriteInfo, primaryPalette);
		if (spriteInfo.mTransformation.isIdentity())
		{
			const Recti rect(spriteInfo.mInterpolatedPosition + spriteInfo.mPivotOffset, spriteInfo.mSize);
			drawer.drawGX2TextureSprite(rect, texture, tintColor, addedColor, spriteInfo.mPriorityFlag);
		}
		else
		{
			std::vector<DrawerMeshVertex> vertices;
			appendPaletteSpriteQuad(vertices, spriteInfo);
			drawer.drawGX2TextureSprite(vertices, texture, tintColor, addedColor, spriteInfo.mPriorityFlag);
		}
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
	setNativeBlendMode(drawer, BlendMode::OPAQUE);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
	drawer.drawRect(rect, mProcessingTexture, rect, Color::WHITE);
}

void GX2Renderer::drawNativeBlur(const EffectBlurGeometry& geometry)
{
	ensureProcessingTexture();
	Drawer& drawer = EngineMain::instance().getDrawer();
	if (mNativeBlurProcessingActive)
	{
		drawer.performRendering();
		resetNativeQueuedState();
		restoreNativeBlurOutputTarget(drawer);
		mNativeBlurProcessingActive = false;
	}
	setNativeBlendMode(drawer, BlendMode::OPAQUE);
	drawer.setSamplingMode(SamplingMode::POINT);
	drawer.setWrapMode(TextureWrapMode::CLAMP);
	drawer.drawGX2Blur(mProcessingTexture, mGameResolution, getBlurKernel(geometry.mBlurValue));
}

void GX2Renderer::invalidateNativeRenderTarget()
{
	mNativeRenderTargetReady = false;
	mCurrentTargetAlreadyHasNativeFrame = false;
	mNativeOverlayGeometries.clear();
	mNativeOverlayUsesInitialViewport = false;
	mNativeOverlayInitialViewport = Recti();
}
