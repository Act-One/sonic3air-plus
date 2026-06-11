/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/application/video/VideoOut.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/drawing/opengl/OpenGLDrawer.h"
#include "oxygen/helper/Logging.h"
#include "oxygen/rendering/Geometry.h"
#if defined(PLATFORM_WINDOWS)
#include "oxygen/rendering/d3d11/D3D11Renderer.h"
#endif
#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)
#include "oxygen/rendering/vulkan/VulkanRenderer.h"
#endif
#include "oxygen/rendering/RenderResources.h"
#include "oxygen/rendering/opengl/OpenGLRenderer.h"
#if defined(PLATFORM_WII)
#include "oxygen/rendering/gx/GXRenderer.h"
#endif
#if defined(PLATFORM_WIIU)
#include "oxygen/rendering/gx2/GX2Renderer.h"
#endif
#include "oxygen/rendering/software/SoftwareRenderer.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/resources/FontCollection.h"
#include "oxygen/simulation/EmulatorInterface.h"
#include "oxygen/simulation/LogDisplay.h"
#include "oxygen/simulation/Simulation.h"


VideoOut::VideoOut() :
	mRenderResources(*new RenderResources())
{
#if defined(PLATFORM_WIIU)
	mGeometries.reserve(0x300);
#else
	mGeometries.reserve(0x100);
#endif
}

VideoOut::~VideoOut()
{
	delete mRenderParts;
	delete &mRenderResources;
	delete mSoftwareRenderer;
#if defined(PLATFORM_WINDOWS)
	delete mD3D11Renderer;
#endif
#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)
	delete mVulkanRenderer;
#endif
#ifdef RMX_WITH_OPENGL_SUPPORT
	delete mOpenGLRenderer;
#endif
#if defined(PLATFORM_WII)
	delete mGXRenderer;
#endif
}

namespace
{
#if defined(PLATFORM_WII) || defined(PLATFORM_WIIU)
	constexpr bool ENABLE_WIIU_VIDEOOUT_TIMING_LOGS = false;
#if defined(PLATFORM_WII)
	constexpr int WII_SAFE_GAME_WIDTH = 400;
	constexpr int WII_SAFE_GAME_HEIGHT = 224;
	constexpr int WII_SCANOUT_WIDTH = 400;
	constexpr int WII_SCANOUT_HEIGHT = 224;
#endif
#if defined(PLATFORM_WIIU)
	constexpr int WIIU_SAFE_GAME_WIDTH = 400;
	constexpr int WIIU_SAFE_GAME_HEIGHT = 224;
	constexpr int WIIU_SCANOUT_WIDTH = 427;
	constexpr int WIIU_SCANOUT_HEIGHT = 240;
#endif

	double getElapsedMilliseconds(uint64 start, uint64 end)
	{
		return (double)(end - start) * 1000.0 / (double)SDL_GetPerformanceFrequency();
	}
#endif

	Vec2i sanitizeGameResolution(const Vec2i& screenSize)
	{
#if defined(PLATFORM_WII)
		const Vec2i scanoutSize(WII_SCANOUT_WIDTH, WII_SCANOUT_HEIGHT);
#elif defined(PLATFORM_WIIU)
		const Vec2i scanoutSize(WIIU_SCANOUT_WIDTH, WIIU_SCANOUT_HEIGHT);
#endif
#if defined(PLATFORM_WII) || defined(PLATFORM_WIIU)
		if (screenSize == scanoutSize)
			return screenSize;

		static bool sLoggedWiiUPixelPerfectSize = false;
		if (!sLoggedWiiUPixelPerfectSize)
		{
			sLoggedWiiUPixelPerfectSize = true;
			RMX_LOG_INFO("VideoOut: using Wii U pixel-perfect scanout resolution "
				<< scanoutSize.x << " x " << scanoutSize.y
				<< " because the live size was " << screenSize.x << " x " << screenSize.y);
		}
		return scanoutSize;
#else
		if (screenSize.x >= 128 && screenSize.x <= 1024 && screenSize.y >= 128 && screenSize.y <= 1024)
			return screenSize;

		static bool sLoggedFallback = false;
		if (!sLoggedFallback)
		{
			sLoggedFallback = true;
			RMX_LOG_INFO("VideoOut: falling back to configured game resolution because the live size became invalid: " << screenSize.x << " x " << screenSize.y);
		}
		return Configuration::instance().mGameScreen;
#endif
	}
}

uint32 VideoOut::getScreenWidth() const
{
	return (uint32)getScreenSize().x;
}

uint32 VideoOut::getScreenHeight() const
{
	return (uint32)getScreenSize().y;
}

Vec2i VideoOut::getScreenSize() const
{
#if defined(PLATFORM_WII)
	return Vec2i(WII_SAFE_GAME_WIDTH, WII_SAFE_GAME_HEIGHT);
#elif defined(PLATFORM_WIIU)
	return Vec2i(WIIU_SAFE_GAME_WIDTH, WIIU_SAFE_GAME_HEIGHT);
#else
	return sanitizeGameResolution(mGameResolution);
#endif
}

Recti VideoOut::getScreenRect() const
{
	const Vec2i screenSize = getScreenSize();
	return Recti(0, 0, screenSize.x, screenSize.y);
}

#if defined(PLATFORM_WII) || defined(PLATFORM_WIIU)
Vec2i VideoOut::getScanoutSize() const
{
	return sanitizeGameResolution(mGameResolution);
}

Recti VideoOut::getScanoutRect() const
{
	const Vec2i scanoutSize = getScanoutSize();
	return Recti(0, 0, scanoutSize.x, scanoutSize.y);
}
#endif

void VideoOut::startup()
{
	mGameResolution = sanitizeGameResolution(Configuration::instance().mGameScreen);
#if defined(PLATFORM_WIIU)
	Configuration::instance().mGameScreen = mGameResolution;
#endif

	RMX_LOG_INFO("VideoOut: Setup of game screen");
	RMX_LOG_INFO("VideoOut: Initial game resolution = " << mGameResolution.x << " x " << mGameResolution.y);
	RMX_LOG_INFO("VideoOut: preparing game screen render target texture");
	mGameScreenTexture.setupAsRenderTarget(mGameResolution);
	RMX_LOG_INFO("VideoOut: game screen render target texture ready");

	if (nullptr == mRenderParts)
	{
		RMX_LOG_INFO("VideoOut: Creating render parts");
		mRenderParts = new RenderParts();
	}

	createRenderer(false);
}

void VideoOut::shutdown()
{
	clearGeometries();
}

void VideoOut::reset()
{
	mRenderParts->reset();
	mActiveRenderer->reset();

	mFrameInterpolation.mUseInterpolationLastUpdate = false;
	mFrameInterpolation.mUseInterpolationThisUpdate = false;
	mFrameInterpolation.mCurrentlyInterpolating = false;
	mDebugDrawRenderingRequested = false;
	mPreviouslyHadNewRenderItems = false;
	mFrameState = FrameState::OUTSIDE_FRAME;
	mRequireGameScreenUpdate = false;
}

void VideoOut::handleActiveModsChanged()
{
	// Better reset everything, as sprite references might have become invalid and should be removed
	reset();
}

void VideoOut::createRenderer(bool reset)
{
	setActiveRenderer(Configuration::instance().mRenderMethod, reset);
}

void VideoOut::destroyRenderer()
{
	RMX_LOG_INFO("VideoOut: destroyRenderer software begin");
	SAFE_DELETE(mSoftwareRenderer);
	RMX_LOG_INFO("VideoOut: destroyRenderer software complete");
#if defined(PLATFORM_WINDOWS)
	RMX_LOG_INFO("VideoOut: destroyRenderer d3d11 begin");
	SAFE_DELETE(mD3D11Renderer);
	RMX_LOG_INFO("VideoOut: destroyRenderer d3d11 complete");
#endif
#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)
	RMX_LOG_INFO("VideoOut: destroyRenderer vulkan begin");
	SAFE_DELETE(mVulkanRenderer);
	RMX_LOG_INFO("VideoOut: destroyRenderer vulkan complete");
#endif
#ifdef RMX_WITH_OPENGL_SUPPORT
	RMX_LOG_INFO("VideoOut: destroyRenderer opengl begin");
	SAFE_DELETE(mOpenGLRenderer);
	RMX_LOG_INFO("VideoOut: destroyRenderer opengl complete");
#endif
#if defined(PLATFORM_WII)
	RMX_LOG_INFO("VideoOut: destroyRenderer gx begin");
	SAFE_DELETE(mGXRenderer);
	RMX_LOG_INFO("VideoOut: destroyRenderer gx complete");
#endif
#if defined(PLATFORM_WIIU)
	RMX_LOG_INFO("VideoOut: destroyRenderer gx2 begin");
	if (nullptr != mGX2Renderer && nullptr != FTX::System && FTX::System->isWiiUProcUIExitRequested())
	{
		RMX_LOG_INFO("VideoOut: leaving gx2 renderer allocated for ProcUI process teardown");
		if (mActiveRenderer == mGX2Renderer)
		{
			mActiveRenderer = nullptr;
		}
		mGX2Renderer = nullptr;
	}
	else
	{
		SAFE_DELETE(mGX2Renderer);
	}
	RMX_LOG_INFO("VideoOut: destroyRenderer gx2 complete");
#endif
	mActiveRenderer = nullptr;
	RMX_LOG_INFO("VideoOut: destroyRenderer complete");
}

void VideoOut::setActiveRenderer(Configuration::RenderMethod renderMethod, bool reset)
{
	Renderer* selectedRenderer = nullptr;

#if defined(PLATFORM_WII)
	if (renderMethod == Configuration::RenderMethod::GX_FULL)
	{
		if (nullptr == mGXRenderer)
		{
			RMX_LOG_INFO("VideoOut: Creating GX renderer");
			mGXRenderer = new GXRenderer(*mRenderParts, mGameScreenTexture);

			RMX_LOG_INFO("VideoOut: GX renderer initialization");
			mGXRenderer->initialize();
		}
		selectedRenderer = mGXRenderer;
	}
#endif

#if defined(PLATFORM_WIIU)
	if (renderMethod == Configuration::RenderMethod::GX2_FULL)
	{
		if (nullptr == mGX2Renderer)
		{
			RMX_LOG_INFO("VideoOut: Creating GX2 renderer");
			mGX2Renderer = new GX2Renderer(*mRenderParts, mGameScreenTexture);

			RMX_LOG_INFO("VideoOut: Renderer initialization");
			mGX2Renderer->initialize();
		}
		selectedRenderer = mGX2Renderer;
	}
#endif

#ifdef RMX_WITH_OPENGL_SUPPORT
	if (nullptr == selectedRenderer && renderMethod == Configuration::RenderMethod::OPENGL_FULL)
	{
		if (nullptr == mOpenGLRenderer)
		{
			RMX_LOG_INFO("VideoOut: Creating OpenGL renderer");
			mOpenGLRenderer = new OpenGLRenderer(*mRenderParts, mGameScreenTexture);

			RMX_LOG_INFO("VideoOut: Renderer initialization");
			mOpenGLRenderer->initialize();
		}
		selectedRenderer = mOpenGLRenderer;
	}
#endif
#if defined(PLATFORM_WINDOWS)
	if (nullptr == selectedRenderer && renderMethod == Configuration::RenderMethod::D3D11_FULL)
	{
		if (nullptr == mD3D11Renderer)
		{
			RMX_LOG_INFO("VideoOut: Creating Direct3D 11 renderer");
			mD3D11Renderer = new D3D11Renderer(*mRenderParts, mGameScreenTexture);

			RMX_LOG_INFO("VideoOut: Renderer initialization");
			mD3D11Renderer->initialize();
		}
		selectedRenderer = mD3D11Renderer;
	}
#endif
#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)
	if (nullptr == selectedRenderer && (renderMethod == Configuration::RenderMethod::VULKAN_SOFT || renderMethod == Configuration::RenderMethod::VULKAN_FULL))
	{
		if (nullptr == mVulkanRenderer)
		{
			RMX_LOG_INFO("VideoOut: Creating Vulkan " << ((renderMethod == Configuration::RenderMethod::VULKAN_FULL) ? "hardware" : "software") << " renderer");
			mVulkanRenderer = new VulkanRenderer(*mRenderParts, mGameScreenTexture);

			RMX_LOG_INFO("VideoOut: Renderer initialization");
			mVulkanRenderer->initialize();
		}
		selectedRenderer = mVulkanRenderer;
	}
#endif
	if (nullptr == selectedRenderer)
	{
		if (nullptr == mSoftwareRenderer)
		{
			RMX_LOG_INFO("VideoOut: Creating software renderer");
			mSoftwareRenderer = new SoftwareRenderer(*mRenderParts, mGameScreenTexture);

			RMX_LOG_INFO("VideoOut: Renderer initialization");
			mSoftwareRenderer->initialize();
		}
		selectedRenderer = mSoftwareRenderer;
	}
	mActiveRenderer = selectedRenderer;

	if (reset)
	{
		mRenderParts->reset();
		mActiveRenderer->reset();
		mActiveRenderer->setGameResolution(mGameResolution);
		mFrameState = FrameState::FRAME_READY;
		mRequireGameScreenUpdate = true;
		mFrameInterpolation.mUseInterpolationLastUpdate = false;
		mFrameInterpolation.mUseInterpolationThisUpdate = false;
		mDebugDrawRenderingRequested = false;
		mPreviouslyHadNewRenderItems = false;
	}
}

void VideoOut::setScreenSize(uint32 width, uint32 height)
{
	const Vec2i requestedSize((int)width, (int)height);
	const Vec2i sanitizedSize = sanitizeGameResolution(requestedSize);
	if (sanitizedSize == mGameResolution)
	{
#if defined(PLATFORM_WIIU)
		static uint32 sNoopScreenSizeLogCount = 0;
		if (sNoopScreenSizeLogCount < 4 && requestedSize != sanitizedSize)
		{
			RMX_LOG_INFO("VideoOut: ignored unchanged setScreenSize request " << requestedSize.x << " x " << requestedSize.y
				<< " effective " << sanitizedSize.x << " x " << sanitizedSize.y);
			++sNoopScreenSizeLogCount;
		}
#endif
		return;
	}
	RMX_LOG_INFO("VideoOut: setScreenSize " << mGameResolution.x << " x " << mGameResolution.y << " -> " << requestedSize.x << " x " << requestedSize.y
		<< " effective " << sanitizedSize.x << " x " << sanitizedSize.y);
	mGameResolution = sanitizedSize;
#if defined(PLATFORM_WIIU)
	Configuration::instance().mGameScreen = mGameResolution;
#endif

	mGameScreenTexture.setupAsRenderTarget(mGameResolution);

	mActiveRenderer->setGameResolution(mGameResolution);

	// Render game screen again (this is particularly needed when switching from in-game Options back to the Pause Menu)
	mRequireGameScreenUpdate = true;
}

Vec2i VideoOut::getInterpolatedWorldSpaceOffset() const
{
	Vec2i offset = mRenderParts->getSpacesManager().getWorldSpaceOffset();
	if (mFrameInterpolation.mCurrentlyInterpolating)
	{
		const Vec2f interpolatedDifference = Vec2f(mLastWorldSpaceOffset - offset) * (1.0f - mFrameInterpolation.mInterFramePosition);
		offset += Vec2i(roundToInt(interpolatedDifference.x), roundToInt(interpolatedDifference.y));
	}
	return offset;
}

void VideoOut::preFrameUpdate()
{
	mRenderParts->preFrameUpdate();
	mLastWorldSpaceOffset = mRenderParts->getSpacesManager().getWorldSpaceOffset();

	// Skipped frames without rendering?
	if (mFrameState == FrameState::FRAME_READY)
	{
		// Processing of last frame (to avoid e.g. sprites rendered multiple times)
		RefreshParameters refreshParameters;
		refreshParameters.mSkipThisFrame = true;
		bool hadRefreshException = false;
#if defined(PLATFORM_WINDOWS) && !defined(PLATFORM_UWP)
		__try
		{
			mRenderParts->refresh(refreshParameters);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			hadRefreshException = true;
		}
#else
		mRenderParts->refresh(refreshParameters);
#endif
		if (hadRefreshException)
		{
			recoverFromRenderStateException("preFrameUpdate/skip-refresh");
		}
	}

	mFrameState = FrameState::INSIDE_FRAME;
	mDebugDrawRenderingRequested = false;
}

void VideoOut::postFrameUpdate()
{
	mRenderParts->postFrameUpdate();

	// Signal for rendering
	mFrameState = FrameState::FRAME_READY;
	mLastFrameTicks = SDL_GetTicks();
	mFrameInterpolation.mUseInterpolationLastUpdate = mFrameInterpolation.mUseInterpolationThisUpdate;
	mFrameInterpolation.mUseInterpolationThisUpdate = true;		// Could be set differently, e.g. if we had a script binding to disable interpolation for an update
	mDebugDrawRenderingRequested = false;
}

void VideoOut::initAfterSaveStateLoad()
{
	mFrameState = FrameState::FRAME_READY;
	mLastFrameTicks = SDL_GetTicks();
	mFrameInterpolation.mUseInterpolationThisUpdate = false;
	mDebugDrawRenderingRequested = false;
}

void VideoOut::setInterFramePosition(float position)
{
	mFrameInterpolation.mInterFramePosition = position;
}

bool VideoOut::updateGameScreen()
{
	return updateGameScreenInternal(false);
}

#if defined(PLATFORM_WII) || defined(PLATFORM_WIIU)
bool VideoOut::updateGameScreenOnCurrentTarget(const Recti& targetRect)
{
	mCurrentTargetRect = targetRect;
	const bool result = updateGameScreenInternal(true);
	mCurrentTargetRect = Recti(0, 0, mGameResolution.x, mGameResolution.y);
	return result;
}

bool VideoOut::canDrawGameScreenOnCurrentTarget() const
{
#if defined(PLATFORM_WII)
	return mActiveRenderer == mGXRenderer && nullptr != mGXRenderer && mGXRenderer->canDrawPresentedGameScreenToCurrentTarget();
#else
	return mActiveRenderer == mGX2Renderer && nullptr != mGX2Renderer && mGX2Renderer->canDrawPresentedGameScreenToCurrentTarget();
#endif
}

bool VideoOut::drawGameScreenOnCurrentTarget(const Recti& targetRect)
{
#if defined(PLATFORM_WII)
	if (mActiveRenderer == mGXRenderer && nullptr != mGXRenderer)
	{
		return mGXRenderer->drawPresentedGameScreenToCurrentTarget(targetRect);
	}
#else
	if (mActiveRenderer == mGX2Renderer && nullptr != mGX2Renderer)
	{
		return mGX2Renderer->drawPresentedGameScreenToCurrentTarget(targetRect);
	}
#endif
	return false;
}
#endif

bool VideoOut::updateGameScreenInternal(bool renderToCurrentTarget)
{
	mFrameInterpolation.mCurrentlyInterpolating = (Configuration::useFrameInterpolation(Configuration::instance().mFrameSync) && mFrameInterpolation.mUseInterpolationLastUpdate && mFrameInterpolation.mUseInterpolationThisUpdate);

	// Only render something if a frame simulation was completed in the meantime
	const bool hasNewSimulationFrame = (mFrameState == FrameState::FRAME_READY);
#if defined(PLATFORM_WII)
	const bool redrawCurrentTargetOnly = renderToCurrentTarget && (mActiveRenderer == mGXRenderer);
#else
	const bool redrawCurrentTargetOnly = false;
#endif
	const bool shouldRedrawPreviousFrame = redrawCurrentTargetOnly && !hasNewSimulationFrame && !mFrameInterpolation.mCurrentlyInterpolating && !mDebugDrawRenderingRequested && !mRequireGameScreenUpdate;
	if (!hasNewSimulationFrame && !mFrameInterpolation.mCurrentlyInterpolating && !mDebugDrawRenderingRequested && !mRequireGameScreenUpdate && !shouldRedrawPreviousFrame)
	{
		// No update
		return false;
	}

	mFrameState = FrameState::OUTSIDE_FRAME;
	mRequireGameScreenUpdate = false;

	RefreshParameters refreshParameters;
	refreshParameters.mSkipThisFrame = shouldRedrawPreviousFrame;
	refreshParameters.mHasNewSimulationFrame = hasNewSimulationFrame;
	refreshParameters.mUsingFrameInterpolation = mFrameInterpolation.mCurrentlyInterpolating;
	refreshParameters.mInterFramePosition = mFrameInterpolation.mInterFramePosition;
	bool hadRefreshException = false;
#if defined(PLATFORM_WINDOWS) && !defined(PLATFORM_UWP)
	__try
	{
		mRenderParts->refresh(refreshParameters);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		hadRefreshException = true;
	}
#else
	mRenderParts->refresh(refreshParameters);
#endif
	if (hadRefreshException)
	{
		recoverFromRenderStateException("updateGameScreen/refresh");
		return false;
	}

	// Render a new image
	bool hadRenderException = false;
#if defined(PLATFORM_WINDOWS) && !defined(PLATFORM_UWP)
	__try
	{
		renderGameScreen();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		hadRenderException = true;
	}
#else
	if (renderToCurrentTarget)
	{
#if defined(PLATFORM_WII) || defined(PLATFORM_WIIU)
		renderGameScreenToCurrentTarget(mCurrentTargetRect);
#else
		renderGameScreen();
#endif
	}
	else
	{
		renderGameScreen();
	}
#endif
	if (hadRenderException)
	{
		recoverFromRenderStateException("updateGameScreen/render");
		return false;
	}

	// Game screen got updated
	return true;
}

void VideoOut::blurGameScreen()
{
#ifdef RMX_WITH_OPENGL_SUPPORT
	if (mActiveRenderer == mOpenGLRenderer)
	{
		mOpenGLRenderer->blurGameScreen();
	}
#endif
}

void VideoOut::toggleLayerRendering(int index)
{
	mRenderParts->mLayerRendering[index] = !mRenderParts->mLayerRendering[index];
}

std::string VideoOut::getLayerRenderingDebugString() const
{
	char string[10] = "basc BASC";
	for (int i = 0; i < 8; ++i)
	{
		if (!mRenderParts->mLayerRendering[i])
			string[i + i/4] = '-';
	}
	return string;
}

void VideoOut::getScreenshot(Bitmap& outBitmap)
{
	mGameScreenTexture.writeContentToBitmap(outBitmap);
}

void VideoOut::clearGeometries()
{
	for (Geometry* geometry : mGeometries)
	{
		mGeometryFactory.destroy(*geometry);
	}
	mGeometries.clear();

	// Regularly cleanup old cache items -- it's safe now that no geometry references a texture in there any more
	RenderResources::instance().mPrintedTextCache.regularCleanup();
}

void VideoOut::collectGeometries(std::vector<Geometry*>& geometries)
{
	// Add plane geometries
	{
		const PlaneManager& pm = mRenderParts->getPlaneManager();
		const Recti fullscreenRect = getScreenRect();

		static std::vector<PlaneManager::PlaneRect> planeRects;
#if defined(PLATFORM_WIIU)
		if (planeRects.capacity() < 16)
			planeRects.reserve(16);
#endif
		pm.getPlaneRects(planeRects, fullscreenRect);

		// Render default planes
		for (int pass = 0; pass < 2; ++pass)
		{
			const bool priorityFlag = (pass == 1);

			for (const PlaneManager::PlaneRect& planeRect : planeRects)
			{
				const int layerIndex = ((planeRect.mPlane == PlaneManager::PLANE_B) ? 0 : 1) + (priorityFlag ? 4 : 0);

				if (mRenderParts->mLayerRendering[layerIndex] && pm.isDefaultPlaneEnabled(planeRect.mPlane))
				{
					uint8 scrollOffsets = (uint8)planeRect.mPlane;
					int renderQueue;
					switch (planeRect.mPlane)
					{
						default:
						case PlaneManager::PLANE_B:  renderQueue = priorityFlag ? 0x3000 : 0x1000;  break;
						case PlaneManager::PLANE_A:  renderQueue = priorityFlag ? 0x4000 : 0x2000;  break;
						case PlaneManager::PLANE_W:  renderQueue = priorityFlag ? 0x4200 : 0x2200;  scrollOffsets = 0xff;  break;
					}

					geometries.push_back(&mGeometryFactory.createPlaneGeometry(planeRect.mRect, planeRect.mPlane, priorityFlag, scrollOffsets, renderQueue));
				}
			}
		}

		// render custom planes
		if (!pm.getCustomPlanes().empty())
		{
			for (const auto& customPlane : pm.getCustomPlanes())
			{
				const int planeIndex = customPlane.mSourcePlane & 0x03;
				if (!PlaneManager::isRenderablePlaneIndex(planeIndex))
				{
					static int sLoggedInvalidCustomPlaneGeometryCount = 0;
					if (sLoggedInvalidCustomPlaneGeometryCount < 8)
					{
						++sLoggedInvalidCustomPlaneGeometryCount;
						RMX_LOG_INFO("VideoOut: skipping custom plane geometry with invalid plane index " << planeIndex
							<< " from source plane " << (int)customPlane.mSourcePlane
							<< ", rect=(" << customPlane.mRect.x << "," << customPlane.mRect.y << "," << customPlane.mRect.width << "," << customPlane.mRect.height
							<< "), scrollOffsets=" << (int)customPlane.mScrollOffsets << ", renderQueue=0x" << rmx::hexString(customPlane.mRenderQueue, 4));
					}
					continue;
				}

				geometries.push_back(&mGeometryFactory.createPlaneGeometry(customPlane.mRect, planeIndex, (customPlane.mSourcePlane & 0x10) != 0, customPlane.mScrollOffsets, customPlane.mRenderQueue));
			}
		}
	}

	// Add render item geometries (sprites, texts, etc.)
	{
		SpriteManager& spriteManager = mRenderParts->getSpriteManager();
		const Vec2i worldSpaceOffset = mRenderParts->getSpacesManager().getWorldSpaceOffset();
		FontCollection& fontCollection = FontCollection::instance();

		for (int index = 0; index < RenderItem::NUM_LIFETIME_CONTEXTS; ++index)
		{
			const RenderItem::LifetimeContext lifetimeContext = (RenderItem::LifetimeContext)index;
			const std::vector<RenderItem*>& renderItems = spriteManager.getRenderItems(lifetimeContext);

			for (RenderItem* renderItem : renderItems)
			{
				switch (renderItem->getType())
				{
					case RenderItem::Type::RECTANGLE:
					{
						const renderitems::Rectangle& rectangle = static_cast<const renderitems::Rectangle&>(*renderItem);

						Color color = rectangle.mColor;
						if (rectangle.mUseGlobalComponentTint)
						{
							mRenderParts->getPaletteManager().applyGlobalComponentTint(color);
						}

						Geometry& geometry = mGeometryFactory.createRectGeometry(Recti(rectangle.mPosition, rectangle.mSize), color);
						geometry.mRenderQueue = rectangle.mRenderQueue;
						geometries.push_back(&geometry);
						break;
					}

					case RenderItem::Type::TEXT:
					{
						const renderitems::Text& text = static_cast<const renderitems::Text&>(*renderItem);

						Font* font = fontCollection.getFontByKey(text.mFontKeyHash);
						if (nullptr == font)
						{
							font = fontCollection.createFontByKey(text.mFontKeyString);
						}
						if (nullptr != font)
						{
							const PrintedTextCache::Key key(text.mFontKeyHash, text.mTextHash, (uint8)text.mSpacing);
							PrintedTextCache& cache = RenderResources::instance().mPrintedTextCache;
							PrintedTextCache::CacheItem* cacheItem = cache.getCacheItem(key);
							if (nullptr == cacheItem)
							{
								cacheItem = &cache.addCacheItem(key, *font, text.mTextString);
							}
							const Vec2i drawPosition = Font::applyAlignment(Recti(text.mPosition, Vec2i(0, 0)), cacheItem->mInnerRect, text.mAlignment);
							const Recti rect(drawPosition, cacheItem->mTexture.getSize());

							Color tintColor = text.mColor;
							Color addedColor = Color::TRANSPARENT;
							if (text.mUseGlobalComponentTint)
							{
								mRenderParts->getPaletteManager().applyGlobalComponentTint(tintColor, addedColor);
							}

							Geometry& geometry = mGeometryFactory.createTexturedRectGeometry(rect, cacheItem->mTexture, tintColor, addedColor);
							geometry.mRenderQueue = text.mRenderQueue;
							geometries.push_back(&geometry);
						}
						break;
					}

					case RenderItem::Type::VDP_SPRITE:
					case RenderItem::Type::PALETTE_SPRITE:
					case RenderItem::Type::COMPONENT_SPRITE:
					case RenderItem::Type::SPRITE_MASK:
					{
						renderitems::SpriteInfo& sprite = static_cast<renderitems::SpriteInfo&>(*renderItem);
						bool accept = true;
						switch (renderItem->getType())
						{
							case RenderItem::Type::VDP_SPRITE:
							{
								accept = (mRenderParts->mLayerRendering[sprite.mPriorityFlag ? 6 : 2]);
								break;
							}

							case RenderItem::Type::PALETTE_SPRITE:
							case RenderItem::Type::COMPONENT_SPRITE:
							{
								accept = (mRenderParts->mLayerRendering[sprite.mPriorityFlag ? 7 : 3]);
								break;
							}

							default:
								// Accept everything else
								break;
						}

						if (accept)
						{
							sprite.mInterpolatedPosition = sprite.mPosition;
							if (mFrameInterpolation.mCurrentlyInterpolating)
							{
								Vec2i difference;
								if (sprite.mHasLastPosition)
								{
									difference = sprite.mLastPositionChange;
								}
								else if (sprite.mLogicalSpace == SpriteManager::Space::WORLD)
								{
									// Assume sprite is standing still in world space, i.e. moving entirely with camera
									difference = mLastWorldSpaceOffset - worldSpaceOffset;
								}
								else
								{
									// Assume sprite is standing still in screen space, i.e. not moving on the screen
								}

								if ((difference.x != 0 || difference.y != 0) && (abs(difference.x) < 0x40 && abs(difference.y) < 0x40))
								{
									const Vec2f interpolatedDifference = Vec2f(difference) * (1.0f - mFrameInterpolation.mInterFramePosition);
									sprite.mInterpolatedPosition -= Vec2i(roundToInt(interpolatedDifference.x), roundToInt(interpolatedDifference.y));
								}
							}

							SpriteGeometry& spriteGeometry = mGeometryFactory.createSpriteGeometry(sprite);
							spriteGeometry.mRenderQueue = sprite.mRenderQueue;
							geometries.push_back(&spriteGeometry);
						}
						break;
					}

					case RenderItem::Type::VIEWPORT:
					{
						const renderitems::Viewport& viewport = static_cast<const renderitems::Viewport&>(*renderItem);

						Geometry& geometry = mGeometryFactory.createViewportGeometry(Recti(viewport.mPosition, viewport.mSize));
						geometry.mRenderQueue = viewport.mRenderQueue;
						geometries.push_back(&geometry);
						break;
					}

					default:
						break;
				}
			}
		}
	}

	// Insert blur effect geometry at the right position
	if (Configuration::instance().mBackgroundBlur > 0)
	{
		constexpr uint16 BLUR_RENDER_QUEUE = 0x1800;

		// Anything there to blur at all?
		//  -> There might be no blurred background at all (e.g. in S3K Sky Sanctuary upper levels)
		bool blurNeeded = false;
		for (const Geometry* geometry : geometries)
		{
			if (geometry->mRenderQueue < BLUR_RENDER_QUEUE)
			{
				blurNeeded = true;
				break;
			}
		}

		if (blurNeeded)
		{
			Geometry& geometry = mGeometryFactory.createEffectBlurGeometry(Configuration::instance().mBackgroundBlur);
			geometry.mRenderQueue = BLUR_RENDER_QUEUE - 1;
			geometries.push_back(&geometry);
		}
	}

	// Sort everything by render queue
	std::stable_sort(geometries.begin(), geometries.end(),
					 [](const Geometry* a, const Geometry* b) { return a->mRenderQueue < b->mRenderQueue; });
}

void VideoOut::renderGameScreen()
{
	// Collect geometries to render
	clearGeometries();
	if (mRenderParts->getActiveDisplay())
	{
		collectGeometries(mGeometries);
	}

	// Render them
	mActiveRenderer->renderGameScreen(mGeometries);
}
#if defined(PLATFORM_WII) || defined(PLATFORM_WIIU)
void VideoOut::renderGameScreenToCurrentTarget(const Recti& targetRect)
{
#if defined(PLATFORM_WIIU)
	const uint64 t0 = ENABLE_WIIU_VIDEOOUT_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
#endif
	clearGeometries();
	if (mRenderParts->getActiveDisplay())
	{
		collectGeometries(mGeometries);
	}
#if defined(PLATFORM_WIIU)
	const uint64 t1 = ENABLE_WIIU_VIDEOOUT_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
#endif

#if defined(PLATFORM_WII)
	if (mActiveRenderer == mGXRenderer && nullptr != mGXRenderer)
	{
		mGXRenderer->renderGameScreenToCurrentTarget(mGeometries, targetRect);
	}
	else
#endif
#if defined(PLATFORM_WIIU)
	if (mActiveRenderer == mGX2Renderer && nullptr != mGX2Renderer)
	{
		mGX2Renderer->renderGameScreenToCurrentTarget(mGeometries, targetRect);
	}
	else
#endif
	{
		mActiveRenderer->renderGameScreen(mGeometries);
	}
#if defined(PLATFORM_WIIU)
	const uint64 t2 = ENABLE_WIIU_VIDEOOUT_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
	if constexpr (ENABLE_WIIU_VIDEOOUT_TIMING_LOGS)
	{
		static uint32 sSampleCount = 0;
		static double sCollectMs = 0.0;
		static double sRenderMs = 0.0;
		static uint32 sGeometries = 0;
		++sSampleCount;
		sCollectMs += getElapsedMilliseconds(t0, t1);
		sRenderMs += getElapsedMilliseconds(t1, t2);
		sGeometries += (uint32)mGeometries.size();
		if ((sSampleCount % 180) == 0)
		{
			const double inv = 1.0 / 180.0;
			RMX_LOG_INFO("VideoOut timing avg collect=" << (float)(sCollectMs * inv)
				<< "ms render=" << (float)(sRenderMs * inv)
				<< "ms total=" << (float)((sCollectMs + sRenderMs) * inv)
				<< "ms geometries=" << (uint32)((double)sGeometries * inv));
			sCollectMs = 0.0;
			sRenderMs = 0.0;
			sGeometries = 0;
		}
	}
#endif
}
#endif

void VideoOut::recoverFromRenderStateException(const char* stage)
{
	static int sLoggedRecoveries = 0;
	if (sLoggedRecoveries < 16)
	{
		++sLoggedRecoveries;
		RMX_LOG_INFO("VideoOut: recovered from renderer-state access violation during '" << stage << "', resetting render parts and active renderer");
		RMX_LOG_INFO("VideoOut: renderer=" << (int)Configuration::instance().mRenderMethod
			<< ", gameResolution=" << mGameResolution.x << "x" << mGameResolution.y
			<< ", frameState=" << (int)mFrameState);
	}

	clearGeometries();

	if (nullptr != mRenderParts)
	{
		mRenderParts->reset();
	}

	if (nullptr != mActiveRenderer)
	{
		mActiveRenderer->reset();
		mActiveRenderer->setGameResolution(mGameResolution);
	}

	mFrameState = FrameState::OUTSIDE_FRAME;
	mLastFrameTicks = SDL_GetTicks();
	mFrameInterpolation.mCurrentlyInterpolating = false;
	mFrameInterpolation.mUseInterpolationLastUpdate = false;
	mFrameInterpolation.mUseInterpolationThisUpdate = false;
	mDebugDrawRenderingRequested = false;
	mPreviouslyHadNewRenderItems = false;
	mRequireGameScreenUpdate = true;
}

void VideoOut::preRefreshDebugging()
{
	mRenderParts->getSpriteManager().clearLifetimeContext(RenderItem::LifetimeContext::OUTSIDE_FRAME);
}

void VideoOut::postRefreshDebugging()
{
	const bool hasNewRenderItems = !mRenderParts->getSpriteManager().getAddedItems().empty();
	mDebugDrawRenderingRequested = hasNewRenderItems || mPreviouslyHadNewRenderItems;
	mPreviouslyHadNewRenderItems = hasNewRenderItems;

	if (hasNewRenderItems)
	{
		mRenderParts->getSpriteManager().postRefreshDebugging();
	}
}

void VideoOut::renderDebugDraw(int debugDrawMode, const Recti& rect)
{
	mActiveRenderer->renderDebugDraw(debugDrawMode, rect);
}

void VideoOut::dumpDebugDraw(int debugDrawMode)
{
	if (debugDrawMode < 2)
	{
		mRenderParts->dumpPlaneContent(debugDrawMode);
	}
	else
	{
		mRenderParts->dumpPatternsContent();
	}
}
