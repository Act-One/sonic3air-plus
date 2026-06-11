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
	constexpr bool ENABLE_GX2_RENDERER_FRAME_LOGS = false;
	constexpr bool ENABLE_GX2_RENDERER_SPRITE_TRACE = false;
	constexpr bool FORCE_NATIVE_WINDOW_GAME_OFFSCREEN = true;
	constexpr bool USE_INDEXED_PALETTE_SPRITE_SHADER = true;
	constexpr bool USE_ATLAS_VDP_SPRITE_SHADER = true;
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

}


GX2Renderer::GX2Renderer(RenderParts& renderParts, DrawerTexture& outputTexture) :
	Renderer(RENDERER_TYPE_ID, renderParts, outputTexture),
	mRenderResources(renderParts)
{
}

void GX2Renderer::initialize()
{
	mGameResolution = Configuration::instance().mGameScreen;
	mRenderResources.initialize();
	invalidateNativeRenderTarget();
	RMX_LOG_INFO("GX2Renderer: native GX2 game renderer enabled");
}

void GX2Renderer::reset()
{
	invalidateNativeRenderTarget();
	mRenderResources.clearAllCaches();
}

void GX2Renderer::setGameResolution(const Vec2i& gameResolution)
{
	if (mGameResolution == gameResolution)
		return;

	mGameResolution = gameResolution;
	invalidateNativeRenderTarget();
	mRenderResources.clearAllCaches();
}

void GX2Renderer::clearGameScreen()
{
	renderNativeGameScreen({});
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

	mRenderResources.refresh();
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

	mRenderResources.refresh();

#if defined(PLATFORM_WIIU)
	if constexpr (ENABLE_GX2_RENDERER_FRAME_LOGS)
	{
		static uint32 sDirectSummaryLogCount = 0;
		static uint64 sLastDirectSignature = UINT64_MAX;
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
		const uint64 signature =
			((uint64)geometries.size() & 0xffu)
			| ((uint64)planeCount << 8)
			| ((uint64)spriteCount << 16)
			| ((uint64)paletteSpriteCount << 24)
			| ((uint64)componentSpriteCount << 32)
			| ((uint64)maskSpriteCount << 40)
			| ((uint64)rectCount << 48)
			| ((uint64)texturedRectCount << 56);
		if (sDirectSummaryLogCount < 12 || (signature != sLastDirectSignature && sDirectSummaryLogCount < 64))
		{
			sLastDirectSignature = signature;
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
	return mCurrentTargetAlreadyHasNativeFrame || mNativeRenderTargetReady;
}

bool GX2Renderer::drawPresentedGameScreenToCurrentTarget(const Recti& targetRect)
{
	if (mCurrentTargetAlreadyHasNativeFrame)
	{
		mCurrentTargetAlreadyHasNativeFrame = false;
		return true;
	}

	DrawerTexture* texture = &mGameScreenTexture;
	if (nullptr == texture || !mNativeRenderTargetReady)
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

	return true;
}

void GX2Renderer::renderDebugDraw(int debugDrawMode, const Recti& rect)
{
	(void)debugDrawMode;
	(void)rect;
	static bool sLoggedDebugDrawDisabled = false;
	if (!sLoggedDebugDrawDisabled)
	{
		sLoggedDebugDrawDisabled = true;
		RMX_LOG_INFO("GX2Renderer: debug draw skipped on pure native GX2");
	}
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
	mCurrentTargetAlreadyHasNativeFrame = false;
	Drawer& drawer = EngineMain::instance().getDrawer();
	if (!mNativeRenderTargetReady || mGameScreenTexture.getSize() != mGameResolution)
	{
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
}
