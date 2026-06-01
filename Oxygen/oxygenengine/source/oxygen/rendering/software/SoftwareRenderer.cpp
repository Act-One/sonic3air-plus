/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/rendering/software/SoftwareRenderer.h"
#include "oxygen/rendering/software/SoftwareBlur.h"
#include "oxygen/rendering/Geometry.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/drawing/Drawer.h"
#include "oxygen/drawing/DrawerTexture.h"
#include "oxygen/drawing/software/BlitterHelper.h"

#if defined(PLATFORM_VITA)
	#include <psp2/kernel/clib.h>
#endif
#if defined(PLATFORM_WIIU)
	#include <coreinit/thread.h>
#endif


namespace detail
{
	class PixelBlockWriter
	{
	public:
		PixelBlockWriter(SoftwareRenderer::BufferedPlaneData& data, const PatternManager::CacheItem* patternCache) :
			PixelBlockWriter(data.mContent.data(), data.mPrioBlocks, data.mNonPrioBlocks, patternCache)
		{}

		PixelBlockWriter(uint8* content, std::vector<SoftwareRenderer::BufferedPlaneData::PixelBlock>& prioBlocks, std::vector<SoftwareRenderer::BufferedPlaneData::PixelBlock>& nonPrioBlocks, const PatternManager::CacheItem* patternCache) :
			mContent(content),
			mPrioBlocks(&prioBlocks),
			mNonPrioBlocks(&nonPrioBlocks),
			mPatternCache(patternCache)
		{}

		void newLine(int lineNumber, int position, int paletteIndex)
		{
			mLineNumber = lineNumber;
			mPosition = position;
			mPaletteIndex = paletteIndex;
			mLastPatternBits = 0xffff;
		}

		FORCE_INLINE void addPixels(int x, uint16 patternIndex, int pixels)
		{
			const PatternManager::CacheItem::Pattern& pattern = mPatternCache[patternIndex & 0x07ff].mFlipVariation[(patternIndex >> 11) & 3];
			uint8* dst = &mContent[mPosition + x];
			const uint8* srcPatternPixels = &pattern.mPixels[mPatternPixelOffset];
			memcpy(dst, srcPatternPixels, pixels);
			const bool hasTransparentPixels = patternHasTransparentPixels(pattern, mPatternPixelOffset, pixels);

			const uint16 patternBits = (patternIndex & 0xe000);		// Includes priority bit and atex
			if (mLastPatternBits != patternBits)
			{
				mLastPatternBits = patternBits;

				mCurrentPixelBlock = &vectorAdd((patternBits & 0x8000) ? *mPrioBlocks : *mNonPrioBlocks);
				mCurrentPixelBlock->mStartCoords.set(x, mLineNumber);
				mCurrentPixelBlock->mLinearPosition = mPosition + x;
				mCurrentPixelBlock->mNumPixels = pixels;
				mCurrentPixelBlock->mAtex = (patternIndex >> 9) & 0x30;
				mCurrentPixelBlock->mPaletteIndex = mPaletteIndex;
				mCurrentPixelBlock->mHasTransparentPixels = hasTransparentPixels;
			}
			else
			{
				mCurrentPixelBlock->mNumPixels += pixels;
				mCurrentPixelBlock->mHasTransparentPixels |= hasTransparentPixels;
			}
		}

		FORCE_INLINE void addPixels8(int x, uint16 patternIndex)
		{
			// Same as above, but with hardcoded "pixels == 8"
			const PatternManager::CacheItem::Pattern& pattern = mPatternCache[patternIndex & 0x07ff].mFlipVariation[(patternIndex >> 11) & 3];
			const bool hasTransparentPixels = (pattern.mOpaqueMaskByRow[mPatternPixelOffset >> 3] != 0xff);

		#if !defined(PLATFORM_VITA) && !defined(PLATFORM_WIIU)
			uint64* dst = (uint64*)&mContent[mPosition + x];
			const uint64* srcPatternPixels = (uint64*)&pattern.mPixels[mPatternPixelOffset];
			*dst = *srcPatternPixels;
		#else
			// PPC/Vita can fault on unaligned 64-bit accesses here; x is not guaranteed to be 8-byte aligned.
			memcpy(&mContent[mPosition + x], &pattern.mPixels[mPatternPixelOffset], 8);
		#endif

			const uint16 patternBits = (patternIndex & 0xe000);		// Includes priority bit and atex
			if (mLastPatternBits != patternBits)
			{
				mLastPatternBits = patternBits;

				mCurrentPixelBlock = &vectorAdd((patternBits & 0x8000) ? *mPrioBlocks : *mNonPrioBlocks);
				mCurrentPixelBlock->mStartCoords.set(x, mLineNumber);
				mCurrentPixelBlock->mLinearPosition = mPosition + x;
				mCurrentPixelBlock->mNumPixels = 8;
				mCurrentPixelBlock->mAtex = (patternIndex >> 9) & 0x30;
				mCurrentPixelBlock->mPaletteIndex = mPaletteIndex;
				mCurrentPixelBlock->mHasTransparentPixels = hasTransparentPixels;
			}
			else
			{
				mCurrentPixelBlock->mNumPixels += 8;
				mCurrentPixelBlock->mHasTransparentPixels |= hasTransparentPixels;
			}
		}

	public:
		int mPatternPixelOffset = 0;

	private:
		FORCE_INLINE bool patternHasTransparentPixels(const PatternManager::CacheItem::Pattern& pattern, int pixelOffset, int pixels) const
		{
			const uint32 requestedMask = ((1u << pixels) - 1u) << (pixelOffset & 7);
			const uint32 opaqueMask = pattern.mOpaqueMaskByRow[pixelOffset >> 3];
			return (opaqueMask & requestedMask) != requestedMask;
		}

		uint8* mContent = nullptr;
		std::vector<SoftwareRenderer::BufferedPlaneData::PixelBlock>* mPrioBlocks = nullptr;
		std::vector<SoftwareRenderer::BufferedPlaneData::PixelBlock>* mNonPrioBlocks = nullptr;
		const PatternManager::CacheItem* mPatternCache = nullptr;

		int mLineNumber = 0;
		int mPosition = 0;
		int mPaletteIndex = 0;

		SoftwareRenderer::BufferedPlaneData::PixelBlock* mCurrentPixelBlock = nullptr;
		uint16 mLastPatternBits = 0xffff;
	};
// try to split up blitting work to let CPU2 handle some of it 
#if defined(PLATFORM_WIIU)
	class SoftwareRendererWorkerTask
	{
	public:
		virtual ~SoftwareRendererWorkerTask() {}
		virtual void execute() = 0;
	};

	class SoftwareRendererWorkerThread final : public rmx::ThreadBase
	{
	public:
		SoftwareRendererWorkerThread() :
			ThreadBase("Software Renderer Worker")
		{
			setWiiUThreadAffinity(OS_THREAD_ATTRIB_AFFINITY_CPU2);
			mConditionVariable = SDL_CreateCond();
			mConditionLock = SDL_CreateMutex();
			startThread();
		}

		~SoftwareRendererWorkerThread()
		{
			signalStopThread(false);
			SDL_LockMutex(mConditionLock);
			SDL_CondBroadcast(mConditionVariable);
			SDL_UnlockMutex(mConditionLock);
			joinThread();
			SDL_DestroyCond(mConditionVariable);
			SDL_DestroyMutex(mConditionLock);
		}

		void startTask(SoftwareRendererWorkerTask& task)
		{
			SDL_LockMutex(mConditionLock);
			RMX_CHECK(nullptr == mActiveTask, "SoftwareRenderer worker already has an active task", SDL_UnlockMutex(mConditionLock); return);
			mActiveTask = &task;
			SDL_CondSignal(mConditionVariable);
			SDL_UnlockMutex(mConditionLock);
		}

		void waitForTask(SoftwareRendererWorkerTask& task)
		{
			SDL_LockMutex(mConditionLock);
			while (mActiveTask == &task)
			{
				SDL_CondWait(mConditionVariable, mConditionLock);
			}
			SDL_UnlockMutex(mConditionLock);
		}

	protected:
		void threadFunc() override
		{
			while (mShouldBeRunning)
			{
				SDL_LockMutex(mConditionLock);
				while (nullptr == mActiveTask && mShouldBeRunning)
				{
					SDL_CondWait(mConditionVariable, mConditionLock);
				}
				SoftwareRendererWorkerTask* task = mActiveTask;
				SDL_UnlockMutex(mConditionLock);

				if (nullptr != task)
				{
					task->execute();
					SDL_LockMutex(mConditionLock);
					if (mActiveTask == task)
					{
						mActiveTask = nullptr;
					}
					SDL_CondBroadcast(mConditionVariable);
					SDL_UnlockMutex(mConditionLock);
				}
			}
		}

	private:
		SDL_cond* mConditionVariable = nullptr;
		SDL_mutex* mConditionLock = nullptr;
		SoftwareRendererWorkerTask* mActiveTask = nullptr;
	};

	bool splitWiiUBlitRect(const Recti& rect, Recti& mainRect, Recti& workerRect)
	{
		constexpr int MIN_PARALLEL_BLIT_PIXELS = 4 * 1024;
		if (rect.width <= 0 || rect.height < 16 || rect.width * rect.height < MIN_PARALLEL_BLIT_PIXELS)
			return false;

		const int splitY = rect.y + rect.height * 2 / 5;
		if (splitY <= rect.y || splitY >= rect.y + rect.height)
			return false;

		mainRect.set(rect.x, rect.y, rect.width, splitY - rect.y);
		workerRect.set(rect.x, splitY, rect.width, rect.y + rect.height - splitY);
		return true;
	}

	class IndexedSpriteBlitTask final : public SoftwareRendererWorkerTask
	{
	public:
		IndexedSpriteBlitTask(Bitmap& bitmap, const Recti& viewport, const Blitter::IndexedSpriteWrapper& sprite, const Blitter::PaletteWrapper& palette, Vec2i position, const Blitter::Options& options) :
			mBitmap(bitmap),
			mViewport(viewport),
			mSprite(sprite),
			mPalette(palette),
			mPosition(position),
			mOptions(options)
		{}

		void execute() override
		{
			Blitter blitter;
			blitter.blitIndexed(Blitter::OutputWrapper(mBitmap, mViewport), mSprite, mPalette, mPosition, mOptions);
		}

	private:
		Bitmap& mBitmap;
		Recti mViewport;
		Blitter::IndexedSpriteWrapper mSprite;
		Blitter::PaletteWrapper mPalette;
		Vec2i mPosition;
		Blitter::Options mOptions;
	};

	class ComponentSpriteBlitTask final : public SoftwareRendererWorkerTask
	{
	public:
		ComponentSpriteBlitTask(Bitmap& bitmap, const Recti& viewport, const Blitter::SpriteWrapper& sprite, Vec2i position, const Blitter::Options& options) :
			mBitmap(bitmap),
			mViewport(viewport),
			mSprite(sprite),
			mPosition(position),
			mOptions(options)
		{}

		void execute() override
		{
			Blitter blitter;
			blitter.blitSprite(Blitter::OutputWrapper(mBitmap, mViewport), mSprite, mPosition, mOptions);
		}

	private:
		Bitmap& mBitmap;
		Recti mViewport;
		Blitter::SpriteWrapper mSprite;
		Vec2i mPosition;
		Blitter::Options mOptions;
	};

	void applyOpaqueAlphaRange(uint32* pixelPtr, int pixelCount)
	{
		if (nullptr == pixelPtr || pixelCount <= 0)
			return;

		if ((((uintptr_t)pixelPtr & 7) == 0) && ((pixelCount & 1) == 0))
		{
			uint64* RESTRICT ptr = (uint64*)pixelPtr;
			uint64* RESTRICT end = ptr + pixelCount / 2;
			for (; ptr < end; ++ptr)
			{
				*ptr |= 0xff000000ff000000ull;
			}
		}
		else
		{
			uint32* RESTRICT end = pixelPtr + pixelCount;
			for (; pixelPtr < end; ++pixelPtr)
			{
				*pixelPtr |= 0xff000000u;
			}
		}
	}

	class AlphaFixupTask final : public SoftwareRendererWorkerTask
	{
	public:
		AlphaFixupTask(uint32* pixelPtr, int pixelCount) :
			mPixelPtr(pixelPtr),
			mPixelCount(pixelCount)
		{}

		void execute() override
		{
			applyOpaqueAlphaRange(mPixelPtr, mPixelCount);
		}

	private:
		uint32* mPixelPtr = nullptr;
		int mPixelCount = 0;
	};

	void clearBitmapRange(uint32* pixels, size_t beginIndex, size_t endIndex, uint32 color)
	{
		for (size_t i = beginIndex; i < endIndex; ++i)
		{
			pixels[i] = color;
		}
	}

	class FrameClearTask final : public SoftwareRendererWorkerTask
	{
	public:
		FrameClearTask(uint32* pixels, size_t pixelBegin, size_t pixelEnd, uint32 color, uint8* depthBuffer, size_t depthBegin, size_t depthEnd) :
			mPixels(pixels),
			mPixelBegin(pixelBegin),
			mPixelEnd(pixelEnd),
			mColor(color),
			mDepthBuffer(depthBuffer),
			mDepthBegin(depthBegin),
			mDepthEnd(depthEnd)
		{}

		void execute() override
		{
			clearBitmapRange(mPixels, mPixelBegin, mPixelEnd, mColor);
			if (nullptr != mDepthBuffer && mDepthEnd > mDepthBegin)
			{
				memset(mDepthBuffer + mDepthBegin, 0, mDepthEnd - mDepthBegin);
			}
		}

	private:
		uint32* mPixels = nullptr;
		size_t mPixelBegin = 0;
		size_t mPixelEnd = 0;
		uint32 mColor = 0;
		uint8* mDepthBuffer = nullptr;
		size_t mDepthBegin = 0;
		size_t mDepthEnd = 0;
	};

	void clearFrameBuffers(Bitmap& bitmap, uint32 color, uint8* depthBuffer, size_t depthBytes, SoftwareRendererWorkerThread* worker)
	{
		uint32* pixels = bitmap.getData();
		const size_t pixelCount = (size_t)bitmap.getPixelCount();
		if (nullptr == pixels || pixelCount == 0)
			return;

		constexpr size_t MIN_PARALLEL_PIXELS = 4 * 1024;
		if (nullptr != worker && pixelCount >= MIN_PARALLEL_PIXELS)
		{
			const size_t pixelSplit = pixelCount * 2 / 5;
			const size_t depthSplit = depthBytes * 2 / 5;
			FrameClearTask workerTask(pixels, pixelSplit, pixelCount, color, depthBuffer, depthSplit, depthBytes);
			worker->startTask(workerTask);
			clearBitmapRange(pixels, 0, pixelSplit, color);
			if (nullptr != depthBuffer && depthSplit > 0)
			{
				memset(depthBuffer, 0, depthSplit);
			}
			worker->waitForTask(workerTask);
			return;
		}

		bitmap.clear(color);
		if (nullptr != depthBuffer && depthBytes > 0)
		{
			memset(depthBuffer, 0, depthBytes);
		}
	}

	void applyOpaqueAlpha(Bitmap& bitmap, SoftwareRendererWorkerThread* worker)
	{
		uint32* pixels = bitmap.getData();
		const int pixelCount = bitmap.getPixelCount();
		if (nullptr == pixels || pixelCount <= 0)
			return;

		constexpr int MIN_PARALLEL_PIXELS = 4 * 1024;
		if (nullptr != worker && pixelCount >= MIN_PARALLEL_PIXELS)
		{
			const int split = ((pixelCount * 2 / 5) & ~1);
			AlphaFixupTask workerTask(pixels + split, pixelCount - split);
			worker->startTask(workerTask);
			applyOpaqueAlphaRange(pixels, split);
			worker->waitForTask(workerTask);
			return;
		}

		applyOpaqueAlphaRange(pixels, pixelCount);
	}
#endif
}


SoftwareRenderer::SoftwareRenderer(RenderParts& renderParts, DrawerTexture& outputTexture) :
	Renderer(RENDERER_TYPE_ID, renderParts, outputTexture)
{
}

SoftwareRenderer::~SoftwareRenderer() = default;

void SoftwareRenderer::initialize()
{
	mGameResolution = Configuration::instance().mGameScreen;
	mGameScreenTexture.accessBitmap().create(mGameResolution.x, mGameResolution.y);
#if defined(PLATFORM_WIIU)
	if (!mWiiURenderWorker)
	{
		mWiiURenderWorker.reset(new detail::SoftwareRendererWorkerThread());
		RMX_LOG_INFO("SoftwareRenderer: CPU2 render worker enabled");
	}
#endif
}

void SoftwareRenderer::reset()
{
	clearGameScreen();
}

void SoftwareRenderer::setGameResolution(const Vec2i& gameResolution)
{
	if (mGameResolution != gameResolution)
	{
		mGameResolution = gameResolution;
		mGameScreenTexture.accessBitmap().create(mGameResolution.x, mGameResolution.y);
	}
}

void SoftwareRenderer::clearGameScreen()
{
	mGameScreenTexture.accessBitmap().clear(0xff000000);
	mGameScreenTexture.bitmapUpdated();
}

void SoftwareRenderer::renderGameScreen(const std::vector<Geometry*>& geometries)
{
	startRendering();
	Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();

	// Clear the screen
#if defined(PLATFORM_WIIU)
	detail::clearFrameBuffers(gameScreenBitmap, mRenderParts.getPaletteManager().getBackdropColor().getABGR32(), mDepthBuffer, sizeof(mDepthBuffer), mWiiURenderWorker.get());
#else
	gameScreenBitmap.clear(mRenderParts.getPaletteManager().getBackdropColor());
	memset(mDepthBuffer, 0, sizeof(mDepthBuffer));
#endif
	mEmptyDepthBuffer = true;

	mCurrentViewport.set(0, 0, mGameResolution.x, mGameResolution.y);
	mFullViewport = true;

	for (int i = 0; i < MAX_BUFFER_PLANE_DATA; ++i)
	{
		mBufferedPlaneData[i].mValid = false;
	}

	// Check if sprite masking needed
	const bool usingSpriteMask = isUsingSpriteMask(geometries);

	// Render geometries
	{
		uint16 lastRenderQueue = 0xffff;
		for (size_t i = 0; i < geometries.size(); ++i)
		{
			if (!progressRendering())
				break;

			const uint16 renderQueue = geometries[i]->mRenderQueue;
			if (usingSpriteMask && lastRenderQueue < 0x8000 && renderQueue >= 0x8000)
			{
				// Copy planes (needed for sprite masking)
				mGameScreenCopy = gameScreenBitmap;
			}

			renderGeometry(*geometries[i]);
			lastRenderQueue = renderQueue;
		}
	}

	// Set alpha channel to 0xff to make sure nothing gets lost due to alpha test
	{
#if defined(PLATFORM_WIIU)
		detail::applyOpaqueAlpha(gameScreenBitmap, mWiiURenderWorker.get());
#else
		uint64* RESTRICT ptr = (uint64*)gameScreenBitmap.getData();
		uint64* RESTRICT end = ptr + gameScreenBitmap.getPixelCount() / 2;
		for (; ptr < end; ++ptr)
		{
			*ptr |= 0xff000000ff000000ull;
		}
#endif
	}

	mGameScreenTexture.bitmapUpdated();
}

void SoftwareRenderer::renderDebugDraw(int debugDrawMode, const Recti& rect)
{
	// TODO: This whole function and how GameView interacts with it, could be improved on (it's a bit of a mess...)

	Drawer& drawer = EngineMain::instance().getDrawer();
	Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();
	const Vec2i oldSize = gameScreenBitmap.getSize();

	// First render to palette bitmap
	static PaletteBitmap paletteBitmap;
	const bool highlightPrioPatterns = (FTX::keyState(SDLK_LSHIFT) != 0);
	mRenderParts.getPlaneManager().dumpAsPaletteBitmap(paletteBitmap, debugDrawMode, highlightPrioPatterns);

	const Vec2i bitmapSize = paletteBitmap.getSize();
	mGameScreenTexture.setupAsRenderTarget(bitmapSize);
	gameScreenBitmap.create(bitmapSize);

	// Convert from palette bitmap to RGBA
	{
		const uint32* palette = mRenderParts.getPaletteManager().getMainPalette(0).getRawColors();
		const uint8* src = paletteBitmap.getData();
		uint32* dst = gameScreenBitmap.getData();

		for (int k = 0; k < paletteBitmap.getPixelCount(); ++k)
		{
			uint8 index = *src;
			if (index & 0x80)
			{
				*dst = 0xff000000 | ((palette[index & 0x7f] & 0x00fcfcfc) >> 2);
			}
			else
			{
				*dst = 0xff000000 | palette[index];
			}
			++src;
			++dst;
		}
	}
	mGameScreenTexture.bitmapUpdated();

	mCurrentViewport.set(0, 0, bitmapSize.x, bitmapSize.y);
	mFullViewport = true;

	drawer.setWindowRenderTarget(FTX::screenRect());
	drawer.setBlendMode(BlendMode::OPAQUE);
	drawer.drawUpscaledRect(RenderUtils::getLetterBoxRect(rect, (float)bitmapSize.x / (float)bitmapSize.y), mGameScreenTexture);
	drawer.performRendering();

	mGameScreenTexture.setupAsRenderTarget(oldSize);
	gameScreenBitmap.create(oldSize);
}

void SoftwareRenderer::renderGeometry(const Geometry& geometry)
{
#if defined(PLATFORM_WIIU)
	const bool logHighQueueGeometry = (geometry.mRenderQueue >= 0xfe00);
#endif
	switch (geometry.getType())
	{
		case Geometry::Type::UNDEFINED:
			break;	// This should never happen anyways

		case Geometry::Type::PLANE:
		{
			renderPlane(static_cast<const PlaneGeometry&>(geometry));
			break;
		}

		case Geometry::Type::SPRITE:
		{
#if defined(PLATFORM_WIIU)
			if (logHighQueueGeometry)
			{
				static uint32 sHighQueueSpriteLogCount = 0;
				if (sHighQueueSpriteLogCount < 24)
				{
					const SpriteGeometry& sg = static_cast<const SpriteGeometry&>(geometry);
					RMX_LOG_INFO("SoftwareRenderer: high queue sprite queue=0x" << rmx::hexString(geometry.mRenderQueue, 4)
						<< " type=" << (int)sg.mSpriteInfo.getType()
						<< " pos=" << sg.mSpriteInfo.mInterpolatedPosition.x << "," << sg.mSpriteInfo.mInterpolatedPosition.y);
					++sHighQueueSpriteLogCount;
				}
			}
#endif
			renderSprite(static_cast<const SpriteGeometry&>(geometry));
			break;
		}

		case Geometry::Type::RECT:
		{
			const RectGeometry& rg = static_cast<const RectGeometry&>(geometry);
#if defined(PLATFORM_WIIU)
			if (logHighQueueGeometry)
			{
				static uint32 sHighQueueRectLogCount = 0;
				if (sHighQueueRectLogCount < 12)
				{
					RMX_LOG_INFO("SoftwareRenderer: high queue rect queue=0x" << rmx::hexString(geometry.mRenderQueue, 4)
						<< " rect=" << rg.mRect.x << "," << rg.mRect.y << " " << rg.mRect.width << "x" << rg.mRect.height);
					++sHighQueueRectLogCount;
				}
			}
#endif
			const Recti rect = Recti::getIntersection(rg.mRect, mCurrentViewport);
			mBlitter.blitColor(Blitter::OutputWrapper(mGameScreenTexture.accessBitmap(), rect), rg.mColor, BlendMode::ALPHA);
			break;
		}

		case Geometry::Type::TEXTURED_RECT:
		{
			const TexturedRectGeometry& tg = static_cast<const TexturedRectGeometry&>(geometry);
			Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();

			Blitter::Options blitterOptions;
			blitterOptions.mBlendMode = BlendMode::ALPHA;
			blitterOptions.mTintColor = &tg.mTintColor;
			blitterOptions.mAddedColor = &tg.mAddedColor;
// needed to check something
#if defined(PLATFORM_WIIU)
			if (logHighQueueGeometry)
			{
				static uint32 sHighQueueTexturedRectLogCount = 0;
				if (sHighQueueTexturedRectLogCount < 24)
				{
					RMX_LOG_INFO("SoftwareRenderer: high queue textured rect queue=0x" << rmx::hexString(geometry.mRenderQueue, 4)
						<< " rect=" << tg.mRect.x << "," << tg.mRect.y << " " << tg.mRect.width << "x" << tg.mRect.height
						<< " viewport=" << mCurrentViewport.x << "," << mCurrentViewport.y << " " << mCurrentViewport.width << "x" << mCurrentViewport.height);
					++sHighQueueTexturedRectLogCount;
				}
			}
#endif
			mBlitter.blitSprite(Blitter::OutputWrapper(mGameScreenTexture.accessBitmap(), mCurrentViewport), Blitter::SpriteWrapper(tg.mDrawerTexture.accessBitmap(), Vec2i()), tg.mRect.getPos(), blitterOptions);
			break;
		}

		case Geometry::Type::EFFECT_BLUR:
		{
			const EffectBlurGeometry& ebg = static_cast<const EffectBlurGeometry&>(geometry);
			Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();

			if (ebg.mBlurValue >= 1)
			{
				SoftwareBlur::blurBitmap(gameScreenBitmap, ebg.mBlurValue);
			}
			break;
		}

		case Geometry::Type::VIEWPORT:
		{
			const ViewportGeometry& vg = static_cast<const ViewportGeometry&>(geometry);
			const Recti fullViewport(0, 0, mGameResolution.x, mGameResolution.y);
			mCurrentViewport = fullViewport;
			mCurrentViewport.intersect(vg.mRect);
			mFullViewport = (mCurrentViewport == fullViewport);
#if defined(PLATFORM_WIIU)
			if (logHighQueueGeometry)
			{
				static uint32 sHighQueueViewportLogCount = 0;
				if (sHighQueueViewportLogCount < 12)
				{
					RMX_LOG_INFO("SoftwareRenderer: high queue viewport queue=0x" << rmx::hexString(geometry.mRenderQueue, 4)
						<< " rect=" << vg.mRect.x << "," << vg.mRect.y << " " << vg.mRect.width << "x" << vg.mRect.height
						<< " active=" << mCurrentViewport.x << "," << mCurrentViewport.y << " " << mCurrentViewport.width << "x" << mCurrentViewport.height);
					++sHighQueueViewportLogCount;
				}
			}
#endif
			break;
		}
	}
}

void SoftwareRenderer::buildBufferedPlaneDataRange(BufferedPlaneData& bufferedPlaneData, const PlaneGeometry& geometry, const Recti& rect, int beginY, int endY, std::vector<BufferedPlaneData::PixelBlock>& prioBlocks, std::vector<BufferedPlaneData::PixelBlock>& nonPrioBlocks)
{
	if (beginY >= endY || rect.empty())
		return;

	const int minX = rect.x;
	const int maxX = rect.x + rect.width;
	beginY = clamp(beginY, rect.y, rect.y + rect.height);
	endY = clamp(endY, rect.y, rect.y + rect.height);

	const PlaneManager& planeManager = mRenderParts.getPlaneManager();
	const ScrollOffsetsManager& scrollOffsetsManager = mRenderParts.getScrollOffsetsManager();
	const PaletteManager& paletteManager = mRenderParts.getPaletteManager();

	const uint16* planeData = planeManager.getPlanePatternsBuffer((uint8)geometry.mPlaneIndex);
	const uint16 numPatternsPerLine = (geometry.mPlaneIndex <= PlaneManager::PLANE_A) ? planeManager.getPlayfieldSizeInPatterns().x : 64;
	const uint16* scrollOffsetsH = nullptr;
	const uint16* scrollOffsetsV = nullptr;
	uint16 scrollMaskH = 0xff;
	uint16 scrollMaskV = 0;
	bool scrollNoRepeat = false;

	if (geometry.mPlaneIndex == PlaneManager::PLANE_W)
	{
		static uint16 wScrollOffsetX;
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
	const uint16 positionMaskH = planeManager.getPlayfieldSizeInPixels().x - 1;
	const uint16 positionMaskV = planeManager.getPlayfieldSizeInPixels().y - 1;
	const int16 verticalScrollOffsetBias = scrollOffsetsManager.getVerticalScrollOffsetBias();

	detail::PixelBlockWriter pixelBlockWriter(bufferedPlaneData.mContent.data(), prioBlocks, nonPrioBlocks, mRenderParts.getPatternManager().getPatternCache());

	for (int y = beginY; y < endY; ++y)
	{
		const int position = y * mGameScreenTexture.getBitmap().getWidth();
		pixelBlockWriter.newLine(y, position, (y < paletteManager.mSplitPositionY) ? 0 : 1);

		int vx = minX;
		if (nullptr != scrollOffsetsH)
			vx += (int16)scrollOffsetsH[y & scrollMaskH];

		int startX = minX;
		int endX = maxX;
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

		if (scrollMaskV == 0)
		{
			const int vy = ((nullptr == scrollOffsetsV) ? y : (y + scrollOffsetsV[0])) & positionMaskV;
			const uint16* planeDataForThisLine = &planeData[(vy / 8) * numPatternsPerLine];
			const int patternPixelBaseOffset = (vy & 0x07) * 8;

			int x = startX;
			{
				vx &= positionMaskH;
				const uint16 patternIndex = planeDataForThisLine[vx / 8];
				const int vxMod8 = vx & 0x07;
				const int pixels = std::min(8 - vxMod8, endX - x);
				pixelBlockWriter.mPatternPixelOffset = patternPixelBaseOffset + vxMod8;
				pixelBlockWriter.addPixels(x, patternIndex, pixels);
				x += pixels;
				vx += pixels;
			}

			pixelBlockWriter.mPatternPixelOffset = patternPixelBaseOffset;
			while (true)
			{
				vx &= positionMaskH;
				const int remainingVx = (positionMaskH - vx + 1);
				const int numPixels = std::min(remainingVx, endX - x) / 8 * 8;
				if (numPixels < 8)
					break;

				const uint16* planeDataPointer = &planeDataForThisLine[vx / 8];
				const int localEndX = x + numPixels;
				while (x < localEndX)
				{
					const uint16 patternIndex = *planeDataPointer;
					pixelBlockWriter.addPixels8(x, patternIndex);
					x += 8;
					++planeDataPointer;
				}
				vx += numPixels;
			}

			if (x < endX)
			{
				vx &= positionMaskH;
				const uint16 patternIndex = planeDataForThisLine[vx / 8];
				const int pixels = endX - x;
				pixelBlockWriter.addPixels(x, patternIndex, pixels);
			}
		}
		else
		{
			for (int x = startX; x < endX; )
			{
				vx &= positionMaskH;

				int vy;
				if (nullptr == scrollOffsetsV)
				{
					vy = y;
				}
				else
				{
					const int verticalScrollOffset = (scrollMaskV == 0) ? scrollOffsetsV[0] : scrollOffsetsV[((x - verticalScrollOffsetBias) >> 4) & scrollMaskV];
					vy = (y + verticalScrollOffset) & positionMaskV;
				}

				const uint16 patternIndex = planeData[(vx / 8) + (vy / 8) * numPatternsPerLine];
				const int vxMod8 = vx & 0x07;
				const int pixels = std::min(8 - vxMod8, endX - x);

				pixelBlockWriter.mPatternPixelOffset = vxMod8 + (vy & 0x07) * 8;
				pixelBlockWriter.addPixels(x, patternIndex, pixels);
				x += pixels;
				vx += pixels;
			}
		}
	}
}
// i don't remember what this does and i cannot be bothered to
void SoftwareRenderer::renderPlane(const PlaneGeometry& geometry)
{
	if (!PlaneManager::isRenderablePlaneIndex(geometry.mPlaneIndex))
	{
		static int sLoggedInvalidPlaneGeometryCount = 0;
		if (sLoggedInvalidPlaneGeometryCount < 8)
		{
			++sLoggedInvalidPlaneGeometryCount;
			RMX_LOG_INFO("SoftwareRenderer: skipping invalid plane geometry with plane index " << geometry.mPlaneIndex
				<< ", rect=(" << geometry.mActiveRect.x << "," << geometry.mActiveRect.y << "," << geometry.mActiveRect.width << "," << geometry.mActiveRect.height
				<< "), scrollOffsets=" << (int)geometry.mScrollOffsets << ", renderQueue=0x" << rmx::hexString(geometry.mRenderQueue, 4));
		}
		return;
	}

	Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();

	Recti rect(0, 0, mGameResolution.x, mGameResolution.y);
	rect.intersect(geometry.mActiveRect);
	rect.intersect(mCurrentViewport);
	const int minY = rect.y;
	const int maxY = rect.y + rect.height;

	const PaletteManager& paletteManager = mRenderParts.getPaletteManager();

	// Search for already setup buffered plane data fitting this geometry
	int foundFittingBufferedPlaneDataIndex = -1;
	for (int i = 0; i < MAX_BUFFER_PLANE_DATA; ++i)
	{
		const BufferedPlaneData& bufferedPlaneData = mBufferedPlaneData[i];
		if (bufferedPlaneData.mValid &&
			bufferedPlaneData.mPlaneIndex == geometry.mPlaneIndex &&
			bufferedPlaneData.mScrollOffsets == geometry.mScrollOffsets &&
			bufferedPlaneData.mActiveRect == geometry.mActiveRect)
		{
			foundFittingBufferedPlaneDataIndex = i;
			break;
		}
	}

	if (foundFittingBufferedPlaneDataIndex == -1)
	{
		// Find a free index
		for (int i = 0; i < MAX_BUFFER_PLANE_DATA; ++i)
		{
			if (!mBufferedPlaneData[i].mValid)
			{
				foundFittingBufferedPlaneDataIndex = i;
				break;
			}
		}
		RMX_CHECK(foundFittingBufferedPlaneDataIndex != -1, "No free buffered plane data structure found", return);

		BufferedPlaneData& bufferedPlaneData = mBufferedPlaneData[foundFittingBufferedPlaneDataIndex];
		bufferedPlaneData.mPlaneIndex = geometry.mPlaneIndex;
		bufferedPlaneData.mScrollOffsets = geometry.mScrollOffsets;
		bufferedPlaneData.mActiveRect = geometry.mActiveRect;
		bufferedPlaneData.mContent.resize(gameScreenBitmap.getPixelCount());
		bufferedPlaneData.mPrioBlocks.clear();
		bufferedPlaneData.mPrioBlocks.reserve(0x800);
		bufferedPlaneData.mNonPrioBlocks.clear();
		bufferedPlaneData.mNonPrioBlocks.reserve(0x800);

	#if defined(PLATFORM_WIIU)
		const int planePixels = rect.width * rect.height;
		if (nullptr != mWiiURenderWorker && planePixels >= 8192 && rect.height >= 16)
		{
			std::vector<BufferedPlaneData::PixelBlock> mainPrioBlocks;
			std::vector<BufferedPlaneData::PixelBlock> mainNonPrioBlocks;
			std::vector<BufferedPlaneData::PixelBlock> workerPrioBlocks;
			std::vector<BufferedPlaneData::PixelBlock> workerNonPrioBlocks;
			mainPrioBlocks.reserve(0x400);
			mainNonPrioBlocks.reserve(0x400);
			workerPrioBlocks.reserve(0x400);
			workerNonPrioBlocks.reserve(0x400);

			class PlaneBuildTask final : public detail::SoftwareRendererWorkerTask
			{
			public:
				PlaneBuildTask(SoftwareRenderer& renderer, BufferedPlaneData& bufferedPlaneData, const PlaneGeometry& geometry, const Recti& rect, int beginY, int endY, std::vector<BufferedPlaneData::PixelBlock>& prioBlocks, std::vector<BufferedPlaneData::PixelBlock>& nonPrioBlocks) :
					mRenderer(renderer),
					mBufferedPlaneData(bufferedPlaneData),
					mGeometry(geometry),
					mRect(rect),
					mBeginY(beginY),
					mEndY(endY),
					mPrioBlocks(prioBlocks),
					mNonPrioBlocks(nonPrioBlocks)
				{}

				void execute() override
				{
					mRenderer.buildBufferedPlaneDataRange(mBufferedPlaneData, mGeometry, mRect, mBeginY, mEndY, mPrioBlocks, mNonPrioBlocks);
				}

			private:
				SoftwareRenderer& mRenderer;
				BufferedPlaneData& mBufferedPlaneData;
				const PlaneGeometry& mGeometry;
				Recti mRect;
				int mBeginY = 0;
				int mEndY = 0;
				std::vector<BufferedPlaneData::PixelBlock>& mPrioBlocks;
				std::vector<BufferedPlaneData::PixelBlock>& mNonPrioBlocks;
			};

			const int splitY = minY + (rect.height * 2) / 5;
			static uint32 sPlaneBuildLogCount = 0;
			if (sPlaneBuildLogCount < 10)
			{
				RMX_LOG_INFO("SoftwareRenderer: CPU2 plane build rect=" << rect.width << "x" << rect.height
					<< " mainRows=" << (splitY - minY)
					<< " workerRows=" << (maxY - splitY));
				++sPlaneBuildLogCount;
			}
			PlaneBuildTask workerTask(*this, bufferedPlaneData, geometry, rect, splitY, maxY, workerPrioBlocks, workerNonPrioBlocks);
			mWiiURenderWorker->startTask(workerTask);
			buildBufferedPlaneDataRange(bufferedPlaneData, geometry, rect, minY, splitY, mainPrioBlocks, mainNonPrioBlocks);
			mWiiURenderWorker->waitForTask(workerTask);

			bufferedPlaneData.mPrioBlocks.swap(mainPrioBlocks);
			bufferedPlaneData.mPrioBlocks.insert(bufferedPlaneData.mPrioBlocks.end(), workerPrioBlocks.begin(), workerPrioBlocks.end());
			bufferedPlaneData.mNonPrioBlocks.swap(mainNonPrioBlocks);
			bufferedPlaneData.mNonPrioBlocks.insert(bufferedPlaneData.mNonPrioBlocks.end(), workerNonPrioBlocks.begin(), workerNonPrioBlocks.end());
		}
		else
	#endif
		{
			buildBufferedPlaneDataRange(bufferedPlaneData, geometry, rect, minY, maxY, bufferedPlaneData.mPrioBlocks, bufferedPlaneData.mNonPrioBlocks);
		}

		bufferedPlaneData.mValid = true;
	}

	// Write plane data to output
	{
		BufferedPlaneData& bufferedPlaneData = mBufferedPlaneData[foundFittingBufferedPlaneDataIndex];

		const uint32* palettes[2] = { paletteManager.getMainPalette(0).getRawColors(), paletteManager.getMainPalette(1).getRawColors() };

		const std::vector<BufferedPlaneData::PixelBlock>& blocks = geometry.mPriorityFlag ? bufferedPlaneData.mPrioBlocks : bufferedPlaneData.mNonPrioBlocks;
#if defined(PLATFORM_WIIU)
		size_t totalPixels = 0;
		for (const BufferedPlaneData::PixelBlock& block : blocks)
		{
			totalPixels += (size_t)block.mNumPixels;
		}
		if (nullptr != mWiiURenderWorker && totalPixels >= 8192 && blocks.size() >= 24)
		{
			class PlaneBlockWriteTask final : public detail::SoftwareRendererWorkerTask
			{
			public:
				PlaneBlockWriteTask(SoftwareRenderer& renderer, const std::vector<BufferedPlaneData::PixelBlock>& blocks, const uint8* planeContent, size_t beginIndex, size_t endIndex, bool priorityFlag, const uint32* const* palettes, Bitmap& gameScreenBitmap) :
					mRenderer(renderer),
					mBlocks(blocks),
					mPlaneContent(planeContent),
					mBeginIndex(beginIndex),
					mEndIndex(endIndex),
					mPriorityFlag(priorityFlag),
					mPalettes(palettes),
					mGameScreenBitmap(gameScreenBitmap)
				{}

				void execute() override
				{
					mRenderer.writePlaneBlockRange(mBlocks, mPlaneContent, mBeginIndex, mEndIndex, mPriorityFlag, mPalettes, mGameScreenBitmap);
				}

			private:
				SoftwareRenderer& mRenderer;
				const std::vector<BufferedPlaneData::PixelBlock>& mBlocks;
				const uint8* mPlaneContent = nullptr;
				size_t mBeginIndex = 0;
				size_t mEndIndex = 0;
				bool mPriorityFlag = false;
				const uint32* const* mPalettes = nullptr;
				Bitmap& mGameScreenBitmap;
			};

			const size_t splitIndex = blocks.size() * 2 / 5;
			static uint32 sPlaneWriteLogCount = 0;
			if (sPlaneWriteLogCount < 10)
			{
				RMX_LOG_INFO("SoftwareRenderer: CPU2 plane write blocks=" << blocks.size()
					<< " pixels=" << totalPixels
					<< " mainBlocks=" << splitIndex
					<< " workerBlocks=" << (blocks.size() - splitIndex));
				++sPlaneWriteLogCount;
			}
			PlaneBlockWriteTask workerTask(*this, blocks, bufferedPlaneData.mContent.data(), splitIndex, blocks.size(), geometry.mPriorityFlag, palettes, gameScreenBitmap);
			mWiiURenderWorker->startTask(workerTask);
			writePlaneBlockRange(blocks, bufferedPlaneData.mContent.data(), 0, splitIndex, geometry.mPriorityFlag, palettes, gameScreenBitmap);
			mWiiURenderWorker->waitForTask(workerTask);
		}
		else
#endif
		{
			writePlaneBlockRange(blocks, bufferedPlaneData.mContent.data(), 0, blocks.size(), geometry.mPriorityFlag, palettes, gameScreenBitmap);
		}

		if (!blocks.empty() && geometry.mPriorityFlag)
			mEmptyDepthBuffer = false;
	}
}

void SoftwareRenderer::writePlaneBlockRange(const std::vector<BufferedPlaneData::PixelBlock>& blocks, const uint8* planeContent, size_t beginIndex, size_t endIndex, bool priorityFlag, const uint32* const* palettes, Bitmap& gameScreenBitmap)
{
	endIndex = std::min(endIndex, blocks.size());
	for (size_t blockIndex = beginIndex; blockIndex < endIndex; ++blockIndex)
	{
		const BufferedPlaneData::PixelBlock& block = blocks[blockIndex];
		const uint8* RESTRICT src = &planeContent[block.mLinearPosition];
		uint32* RESTRICT dstRGBA = &gameScreenBitmap.getData()[block.mLinearPosition];
		const uint32* RESTRICT paletteWithAtex = &palettes[block.mPaletteIndex][block.mAtex];

		if (priorityFlag)
		{
			uint8* RESTRICT dstDepth = &mDepthBuffer[block.mStartCoords.x + block.mStartCoords.y * 0x200];
			if (block.mHasTransparentPixels)
			{
				for (int i = 0; i < block.mNumPixels; ++i)
				{
					if (src[i] & 0x0f)
					{
						dstRGBA[i] = paletteWithAtex[src[i]];
						dstDepth[i] = 0x80;
					}
				}
			}
			else
			{
				for (int i = 0; i < block.mNumPixels; ++i)
				{
					dstRGBA[i] = paletteWithAtex[src[i]];
				}
				memset(dstDepth, 0x80, (size_t)block.mNumPixels);
			}
		}
		else
		{
			if (block.mHasTransparentPixels)
			{
				for (int i = 0; i < block.mNumPixels; ++i)
				{
					if (src[i] & 0x0f)
					{
						dstRGBA[i] = paletteWithAtex[src[i]];
					}
				}
			}
			else
			{
				for (int i = 0; i < block.mNumPixels; ++i)
				{
					dstRGBA[i] = paletteWithAtex[src[i]];
				}
			}
		}
	}
}

void SoftwareRenderer::renderSprite(const SpriteGeometry& geometry)
{
	Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();

	switch (geometry.mSpriteInfo.getType())
	{
		case RenderItem::Type::VDP_SPRITE:
		{
			const renderitems::VdpSpriteInfo& sprite = static_cast<const renderitems::VdpSpriteInfo&>(geometry.mSpriteInfo);

			const PaletteManager& paletteManager = mRenderParts.getPaletteManager();
			const uint32* palettes[2] = { paletteManager.getMainPalette(0).getRawColors(), paletteManager.getMainPalette(1).getRawColors() };
			const PatternManager::CacheItem* patternCache = mRenderParts.getPatternManager().getPatternCache();

			const uint8 depthValue = (sprite.mPriorityFlag) ? 0x80 : 0;
			const bool useDepthTest = !mEmptyDepthBuffer;
			const bool useTintColor = (sprite.mTintColor != Color::WHITE || sprite.mAddedColor != Color::TRANSPARENT);

			Recti rect(sprite.mInterpolatedPosition.x, sprite.mInterpolatedPosition.y, sprite.mSize.x * 8, sprite.mSize.y * 8);
			rect = Recti::getIntersection(rect, mCurrentViewport);

			const int minX = rect.x;
			const int maxX = rect.x + rect.width;
			const int minY = rect.y;
			const int maxY = rect.y + rect.height;

			for (int y = minY; y < maxY; ++y)
			{
				const uint32* palette = (y < paletteManager.mSplitPositionY) ? palettes[0] : palettes[1];
				const int vy = y - sprite.mInterpolatedPosition.y;
				int patternY = vy >> 3;
				if (sprite.mFirstPattern & 0x1000)
					patternY = sprite.mSize.y - patternY - 1;
				const int patternRow = vy & 0x07;

				for (int x = minX; x < maxX; )
				{
					const int vx = x - sprite.mInterpolatedPosition.x;
					int patternX = vx >> 3;
					if (sprite.mFirstPattern & 0x0800)
						patternX = sprite.mSize.x - patternX - 1;

					const uint16 patternIndex = sprite.mFirstPattern + patternY + patternX * sprite.mSize.y;
					const PatternManager::CacheItem::Pattern& pattern = patternCache[patternIndex & 0x07ff].mFlipVariation[(patternIndex >> 11) & 3];

					const int patternColumn = vx & 0x07;
					const int pixels = std::min(8 - patternColumn, maxX - x);
					const uint8* RESTRICT src = &pattern.mPixels[patternColumn + patternRow * 8];
					uint32* RESTRICT dst = gameScreenBitmap.getPixelPointer(x, y);
					const uint8 atex = (patternIndex >> 9) & 0x30;
					const uint32 requestedMask = ((1u << pixels) - 1u) << patternColumn;
					const bool hasTransparentPixels = (pattern.mOpaqueMaskByRow[patternRow] & requestedMask) != requestedMask;

					if (useDepthTest)
					{
						uint8* RESTRICT dstDepth = &mDepthBuffer[x + y * 0x200];
						for (int i = 0; i < pixels; ++i)
						{
							if (depthValue < dstDepth[i])
								continue;
							const uint8 colorIndex = src[i] + atex;
							if (colorIndex & 0x0f)
							{
								if (useTintColor)
								{
									Color color = Color::fromABGR32(palette[colorIndex]);
									color.r = saturate(sprite.mAddedColor.r + color.r * sprite.mTintColor.r);
									color.g = saturate(sprite.mAddedColor.g + color.g * sprite.mTintColor.g);
									color.b = saturate(sprite.mAddedColor.b + color.b * sprite.mTintColor.b);
									color.a = saturate(sprite.mAddedColor.a + color.a * sprite.mTintColor.a);
									uint32 srcColor = color.getABGR32();
									BlitterHelper::blendPixelAlpha((uint8*)&dst[i], (uint8*)&srcColor);
								}
								else
								{
									dst[i] = palette[colorIndex];
								}
							}
						}
					}
					else if (useTintColor)
					{
						for (int i = 0; i < pixels; ++i)
						{
							const uint8 colorIndex = src[i] + atex;
							if (colorIndex & 0x0f)
							{
								Color color = Color::fromABGR32(palette[colorIndex]);
								color.r = saturate(sprite.mAddedColor.r + color.r * sprite.mTintColor.r);
								color.g = saturate(sprite.mAddedColor.g + color.g * sprite.mTintColor.g);
								color.b = saturate(sprite.mAddedColor.b + color.b * sprite.mTintColor.b);
								color.a = saturate(sprite.mAddedColor.a + color.a * sprite.mTintColor.a);
								uint32 srcColor = color.getABGR32();
								BlitterHelper::blendPixelAlpha((uint8*)&dst[i], (uint8*)&srcColor);
							}
						}
					}
					else if (hasTransparentPixels)
					{
						for (int i = 0; i < pixels; ++i)
						{
							const uint8 colorIndex = src[i] + atex;
							if (colorIndex & 0x0f)
								dst[i] = palette[colorIndex];
						}
					}
					else
					{
						for (int i = 0; i < pixels; ++i)
						{
							dst[i] = palette[src[i] + atex];
						}
					}
					x += pixels;
				}
			}
			break;
		}

		case RenderItem::Type::PALETTE_SPRITE:
		case RenderItem::Type::COMPONENT_SPRITE:
		{
			// Shared code for palette & component sprite rendering
			const renderitems::CustomSpriteInfoBase& spriteBase = static_cast<const renderitems::CustomSpriteInfoBase&>(geometry.mSpriteInfo);
			if (nullptr == spriteBase.mCacheItem)
				break;

			const bool isPaletteSprite = (geometry.mSpriteInfo.getType() == RenderItem::Type::PALETTE_SPRITE);

			const PaletteManager& paletteManager = mRenderParts.getPaletteManager();
			BitmapViewMutable<uint8> depthBufferView(mDepthBuffer, Vec2i(0x200, 0x100));	// Depth buffer uses a fixed size...

			// Build blitter options
			Blitter::Options blitterOptions;
			Vec4f tintColor = spriteBase.mTintColor;		// Using Vec4f instead of Color to prevent clamp into [0.0f, 1.0f]
			Vec4f addedColor = spriteBase.mAddedColor;
			{
				if (spriteBase.mUseGlobalComponentTint)
				{
					paletteManager.applyGlobalComponentTint(tintColor, addedColor);
				}

				const bool hasTransform = !spriteBase.mTransformation.isIdentity();
				blitterOptions.mTransform = hasTransform ? *spriteBase.mTransformation.mMatrix : nullptr;
				blitterOptions.mInvTransform = hasTransform ? *spriteBase.mTransformation.mInverse : nullptr;
				blitterOptions.mSamplingMode = SamplingMode::POINT;
				blitterOptions.mBlendMode = spriteBase.mBlendMode;
				blitterOptions.mTintColor = (tintColor != Color::WHITE) ? &tintColor : nullptr;
				blitterOptions.mAddedColor = (addedColor != Color::TRANSPARENT) ? &addedColor : nullptr;
				blitterOptions.mDepthBuffer = (mEmptyDepthBuffer && !spriteBase.mPriorityFlag) ? nullptr : &depthBufferView;
				blitterOptions.mDepthTestValue = (spriteBase.mPriorityFlag) ? 0x80 : 0;
			}

#if defined(PLATFORM_WIIU)
			auto blitIndexed = [&](const Recti& targetRect, const Blitter::IndexedSpriteWrapper& spriteWrapper, const Blitter::PaletteWrapper& paletteWrapper, Vec2i position)
			{
				Recti mainRect;
				Recti workerRect;
				if (nullptr != mWiiURenderWorker && nullptr == blitterOptions.mTransform && detail::splitWiiUBlitRect(targetRect, mainRect, workerRect))
				{
					static uint32 sIndexedSpriteLogCount = 0;
					if (sIndexedSpriteLogCount < 8)
					{
						RMX_LOG_INFO("SoftwareRenderer: CPU2 indexed sprite blit rect=" << targetRect.width << "x" << targetRect.height
							<< " mainRows=" << mainRect.height
							<< " workerRows=" << workerRect.height);
						++sIndexedSpriteLogCount;
					}
					Blitter::Options workerOptions = blitterOptions;
					detail::IndexedSpriteBlitTask workerTask(gameScreenBitmap, workerRect, spriteWrapper, paletteWrapper, position, workerOptions);
					mWiiURenderWorker->startTask(workerTask);
					Blitter::Options mainOptions = blitterOptions;
					mBlitter.blitIndexed(Blitter::OutputWrapper(gameScreenBitmap, mainRect), spriteWrapper, paletteWrapper, position, mainOptions);
					mWiiURenderWorker->waitForTask(workerTask);
				}
				else
				{
					mBlitter.blitIndexed(Blitter::OutputWrapper(gameScreenBitmap, targetRect), spriteWrapper, paletteWrapper, position, blitterOptions);
				}
			};

			auto blitComponent = [&](const Recti& targetRect, const Blitter::SpriteWrapper& spriteWrapper, Vec2i position)
			{
				Recti mainRect;
				Recti workerRect;
				if (nullptr != mWiiURenderWorker && nullptr == blitterOptions.mTransform && detail::splitWiiUBlitRect(targetRect, mainRect, workerRect))
				{
					static uint32 sComponentSpriteLogCount = 0;
					if (sComponentSpriteLogCount < 8)
					{
						RMX_LOG_INFO("SoftwareRenderer: CPU2 component sprite blit rect=" << targetRect.width << "x" << targetRect.height
							<< " mainRows=" << mainRect.height
							<< " workerRows=" << workerRect.height);
						++sComponentSpriteLogCount;
					}
					Blitter::Options workerOptions = blitterOptions;
					detail::ComponentSpriteBlitTask workerTask(gameScreenBitmap, workerRect, spriteWrapper, position, workerOptions);
					mWiiURenderWorker->startTask(workerTask);
					Blitter::Options mainOptions = blitterOptions;
					mBlitter.blitSprite(Blitter::OutputWrapper(gameScreenBitmap, mainRect), spriteWrapper, position, mainOptions);
					mWiiURenderWorker->waitForTask(workerTask);
				}
				else
				{
					mBlitter.blitSprite(Blitter::OutputWrapper(gameScreenBitmap, targetRect), spriteWrapper, position, blitterOptions);
				}
			};
#endif

			if (isPaletteSprite)
			{
				// Palette sprite specific code
				const renderitems::PaletteSpriteInfo& spriteInfo = static_cast<const renderitems::PaletteSpriteInfo&>(spriteBase);

				const PaletteSprite& paletteSprite = *static_cast<PaletteSprite*>(spriteInfo.mCacheItem->mSprite);
				const PaletteBitmap& paletteBitmap = spriteInfo.mUseUpscaledSprite ? paletteSprite.getUpscaledBitmap() : paletteSprite.getBitmap();
				const Blitter::IndexedSpriteWrapper spriteWrapper(paletteBitmap.getData(), paletteBitmap.getSize(), -paletteSprite.mOffset);

				const PaletteBase& primaryPalette = (nullptr == spriteInfo.mPrimaryPalette) ? paletteManager.getMainPalette(0) : *spriteInfo.mPrimaryPalette;
				const Blitter::PaletteWrapper paletteWrapper(primaryPalette.getRawColors() + spriteInfo.mAtex, primaryPalette.getSize() - spriteInfo.mAtex);

				// Handle screen palette split
				const int splitY = paletteManager.mSplitPositionY;
				if (splitY < mGameResolution.y)
				{
					const PaletteBase& secondaryPalette = (nullptr == spriteInfo.mSecondaryPalette) ? paletteManager.getMainPalette(1) : *spriteInfo.mSecondaryPalette;
					const Blitter::PaletteWrapper paletteWrapper2(secondaryPalette.getRawColors() + spriteInfo.mAtex, secondaryPalette.getSize() - spriteInfo.mAtex);

					Recti targetRect = Recti::getIntersection(mCurrentViewport, Recti(0, 0, mGameResolution.x, splitY));
#if defined(PLATFORM_WIIU)
					blitIndexed(targetRect, spriteWrapper, paletteWrapper, spriteInfo.mInterpolatedPosition);
#else
					mBlitter.blitIndexed(Blitter::OutputWrapper(gameScreenBitmap, targetRect), spriteWrapper, paletteWrapper, spriteInfo.mInterpolatedPosition, blitterOptions);
#endif

					targetRect = Recti::getIntersection(mCurrentViewport, Recti(0, splitY, mGameResolution.x, mGameResolution.y - splitY));
#if defined(PLATFORM_WIIU)
					blitIndexed(targetRect, spriteWrapper, paletteWrapper2, spriteInfo.mInterpolatedPosition);
#else
					mBlitter.blitIndexed(Blitter::OutputWrapper(gameScreenBitmap, targetRect), spriteWrapper, paletteWrapper2, spriteInfo.mInterpolatedPosition, blitterOptions);
#endif
				}
				else
				{
#if defined(PLATFORM_WIIU)
					blitIndexed(mCurrentViewport, spriteWrapper, paletteWrapper, spriteInfo.mInterpolatedPosition);
#else
					mBlitter.blitIndexed(Blitter::OutputWrapper(gameScreenBitmap, mCurrentViewport), spriteWrapper, paletteWrapper, spriteInfo.mInterpolatedPosition, blitterOptions);
#endif
				}
			}
			else
			{
				// Component sprite specific code
				const renderitems::ComponentSpriteInfo& spriteInfo = static_cast<const renderitems::ComponentSpriteInfo&>(spriteBase);

				const ComponentSprite& componentSprite = *static_cast<ComponentSprite*>(spriteInfo.mCacheItem->mSprite);
				const Blitter::SpriteWrapper spriteWrapper(componentSprite.getBitmap(), -componentSprite.mOffset);

#if defined(PLATFORM_WIIU)
				blitComponent(mCurrentViewport, spriteWrapper, spriteInfo.mInterpolatedPosition);
#else
				mBlitter.blitSprite(Blitter::OutputWrapper(gameScreenBitmap, mCurrentViewport), spriteWrapper, spriteInfo.mInterpolatedPosition, blitterOptions);
#endif
			}

			if (spriteBase.mPriorityFlag)
				mEmptyDepthBuffer = false;
			break;
		}

		case RenderItem::Type::SPRITE_MASK:
		{
			// Overwrite sprites with plane rendering results in given rect
			const renderitems::SpriteMaskInfo& mask = static_cast<const renderitems::SpriteMaskInfo&>(geometry.mSpriteInfo);
			if (mask.mSize.x > 0 && mask.mSize.y > 0)
			{
				const int minX = clamp(mask.mInterpolatedPosition.x, 0, gameScreenBitmap.getWidth());
				const int maxX = clamp(mask.mInterpolatedPosition.x + mask.mSize.x, 0, gameScreenBitmap.getWidth());
				const int bytes = (maxX - minX) * 4;
				if (bytes > 0)
				{
					const int minY = clamp(mask.mInterpolatedPosition.y, 0, gameScreenBitmap.getHeight());
					const int maxY = clamp(mask.mInterpolatedPosition.y + mask.mSize.y, 0, gameScreenBitmap.getHeight());

					for (int line = minY; line < maxY; ++line)
					{
						const uint32 offset = minX + line * gameScreenBitmap.getWidth();
						memcpy(&gameScreenBitmap[offset], &mGameScreenCopy[offset], bytes);
					}
				}
			}
			break;
		}

		case RenderItem::Type::RECTANGLE:
		case RenderItem::Type::TEXT:
		case RenderItem::Type::VIEWPORT:
		case RenderItem::Type::INVALID:
			break;
	}
}
