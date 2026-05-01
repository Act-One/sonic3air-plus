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
#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)
#include "oxygen/rendering/vulkan/VulkanRenderer.h"
#endif
#endif
#include "oxygen/rendering/RenderResources.h"
#include "oxygen/rendering/opengl/OpenGLRenderer.h"
#include "oxygen/rendering/software/SoftwareRenderer.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/resources/FontCollection.h"
#include "oxygen/simulation/EmulatorInterface.h"
#include "oxygen/simulation/LogDisplay.h"
#include "oxygen/simulation/Simulation.h"


VideoOut::VideoOut() :
	mRenderResources(*new RenderResources())
{
	mGeometries.reserve(0x100);
}

VideoOut::~VideoOut()
{
	delete mRenderParts;
	delete &mRenderResources;
	delete mSoftwareRenderer;
#if defined(PLATFORM_WINDOWS)
	delete mD3D11Renderer;
#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)
	delete mVulkanRenderer;
#endif
#endif
#ifdef RMX_WITH_OPENGL_SUPPORT
	delete mOpenGLRenderer;
#endif
}

namespace
{
	Vec2i sanitizeGameResolution(const Vec2i& screenSize)
	{
		if (screenSize.x >= 128 && screenSize.x <= 1024 && screenSize.y >= 128 && screenSize.y <= 1024)
			return screenSize;

		static bool sLoggedFallback = false;
		if (!sLoggedFallback)
		{
			sLoggedFallback = true;
			RMX_LOG_INFO("VideoOut: falling back to configured game resolution because the live size became invalid: " << screenSize.x << " x " << screenSize.y);
		}
		return Configuration::instance().mGameScreen;
	}
}

uint32 VideoOut::getScreenWidth() const
{
	return (uint32)sanitizeGameResolution(mGameResolution).x;
}

uint32 VideoOut::getScreenHeight() const
{
	return (uint32)sanitizeGameResolution(mGameResolution).y;
}

Vec2i VideoOut::getScreenSize() const
{
	return sanitizeGameResolution(mGameResolution);
}

Recti VideoOut::getScreenRect() const
{
	const Vec2i screenSize = getScreenSize();
	return Recti(0, 0, screenSize.x, screenSize.y);
}

void VideoOut::startup()
{
	mGameResolution = Configuration::instance().mGameScreen;

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
	mDebugDrawRenderingRequested = false;
	mPreviouslyHadNewRenderItems = false;
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
	SAFE_DELETE(mSoftwareRenderer);
#if defined(PLATFORM_WINDOWS)
	SAFE_DELETE(mD3D11Renderer);
#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)
	SAFE_DELETE(mVulkanRenderer);
#endif
#endif
#ifdef RMX_WITH_OPENGL_SUPPORT
	SAFE_DELETE(mOpenGLRenderer);
#endif
	mActiveRenderer = nullptr;
}

void VideoOut::setActiveRenderer(Configuration::RenderMethod renderMethod, bool reset)
{
#ifdef RMX_WITH_OPENGL_SUPPORT
	if (renderMethod == Configuration::RenderMethod::OPENGL_FULL)
	{
		if (nullptr == mOpenGLRenderer)
		{
			RMX_LOG_INFO("VideoOut: Creating OpenGL renderer");
			mOpenGLRenderer = new OpenGLRenderer(*mRenderParts, mGameScreenTexture);

			RMX_LOG_INFO("VideoOut: Renderer initialization");
			mOpenGLRenderer->initialize();
		}
		mActiveRenderer = mOpenGLRenderer;
	}
	else
#endif
#if defined(PLATFORM_WINDOWS)
	if (renderMethod == Configuration::RenderMethod::D3D11_FULL)
	{
		if (nullptr == mD3D11Renderer)
		{
			RMX_LOG_INFO("VideoOut: Creating Direct3D 11 renderer");
			mD3D11Renderer = new D3D11Renderer(*mRenderParts, mGameScreenTexture);

			RMX_LOG_INFO("VideoOut: Renderer initialization");
			mD3D11Renderer->initialize();
		}
		mActiveRenderer = mD3D11Renderer;
	}
#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)
	else if (renderMethod == Configuration::RenderMethod::VULKAN_SOFT || renderMethod == Configuration::RenderMethod::VULKAN_FULL)
	{
		if (nullptr == mVulkanRenderer)
		{
			RMX_LOG_INFO("VideoOut: Creating Vulkan " << ((renderMethod == Configuration::RenderMethod::VULKAN_FULL) ? "hardware" : "software") << " renderer");
			mVulkanRenderer = new VulkanRenderer(*mRenderParts, mGameScreenTexture);

			RMX_LOG_INFO("VideoOut: Renderer initialization");
			mVulkanRenderer->initialize();
		}
		mActiveRenderer = mVulkanRenderer;
	}
#endif
	else
#endif
	{
		if (nullptr == mSoftwareRenderer)
		{
			RMX_LOG_INFO("VideoOut: Creating software renderer");
			mSoftwareRenderer = new SoftwareRenderer(*mRenderParts, mGameScreenTexture);

			RMX_LOG_INFO("VideoOut: Renderer initialization");
			mSoftwareRenderer->initialize();
		}
		mActiveRenderer = mSoftwareRenderer;
	}

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
	RMX_LOG_INFO("VideoOut: setScreenSize " << mGameResolution.x << " x " << mGameResolution.y << " -> " << width << " x " << height);
	mGameResolution.x = width;
	mGameResolution.y = height;

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
	mFrameInterpolation.mCurrentlyInterpolating = (Configuration::useFrameInterpolation(Configuration::instance().mFrameSync) && mFrameInterpolation.mUseInterpolationLastUpdate && mFrameInterpolation.mUseInterpolationThisUpdate);

	// Only render something if a frame simulation was completed in the meantime
	const bool hasNewSimulationFrame = (mFrameState == FrameState::FRAME_READY);
	if (!hasNewSimulationFrame && !mFrameInterpolation.mCurrentlyInterpolating && !mDebugDrawRenderingRequested && !mRequireGameScreenUpdate)
	{
		// No update
		return false;
	}

	mFrameState = FrameState::OUTSIDE_FRAME;
	mRequireGameScreenUpdate = false;

	RefreshParameters refreshParameters;
	refreshParameters.mSkipThisFrame = false;
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
	renderGameScreen();
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

	static int sGeometryLogCount = 0;
	if (sGeometryLogCount < 6)
	{
		++sGeometryLogCount;
		RMX_LOG_INFO("VideoOut: renderGameScreen activeDisplay=" << (mRenderParts->getActiveDisplay() ? 1 : 0) << ", geometries=" << mGeometries.size());
	}

	// Render them
	mActiveRenderer->renderGameScreen(mGeometries);
}

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
