/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/rendering/gx2/GX2RenderResources.h"

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
	constexpr bool ENABLE_WIIU_GX2_RESOURCE_TIMING_LOGS = false;
	const Vec2i PALETTE_TEXTURE_SIZE(256, (int)(PaletteManager::MAIN_PALETTE_SIZE / 256) * 2);
	const Vec2i PATTERN_CACHE_TEXTURE_SIZE(0x40, 0x800);
	const Vec2i PLANE_PATTERN_TEXTURE_SIZE(128, 128);
	const Vec2i H_SCROLL_TEXTURE_SIZE(0x100, 1);
	const Vec2i V_SCROLL_TEXTURE_SIZE(0x20, 1);
	const Vec2i PLANE_DATA_TEXTURE_SIZE(512, 1024);
	constexpr int PLANE_DATA_PATTERN_Y = 0;
	constexpr int PLANE_DATA_PALETTE_Y = 256;
	constexpr int PLANE_DATA_INDEX_Y = 384;
	constexpr int PLANE_DATA_HSCROLL_Y = 512;
	constexpr int PLANE_DATA_VSCROLL_Y = 520;
	constexpr bool UPLOAD_LEGACY_PLANE_TEXTURES = false;
	uint32 packDataPixel(uint8 r, uint8 g = 0, uint8 b = 0, uint8 a = 0xff)
	{
		// Keep packed helper data in the same ABGR32 layout as normal Bitmap pixels.
		return (uint32)r | ((uint32)g << 8) | ((uint32)b << 16) | ((uint32)a << 24);
	}

	uint64 hashPointer(uint64 seed, const void* ptr)
	{
		const uint64 value = (uint64)(uintptr_t)ptr;
		return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
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

#if defined(PLATFORM_WIIU)
	double getElapsedMilliseconds(uint64 start, uint64 end)
	{
		return (double)(end - start) * 1000.0 / (double)SDL_GetPerformanceFrequency();
	}
#endif

	int getVdpSpriteSplitRelativeY(const renderitems::VdpSpriteInfo& spriteInfo, int splitY)
	{
		const int height = spriteInfo.mSize.y * 8;
		const int relativeY = splitY - spriteInfo.mInterpolatedPosition.y;
		return clamp(relativeY, 0, height);
	}

	uint64 getVdpSpriteCacheKey(const renderitems::VdpSpriteInfo& spriteInfo, int splitY)
	{
		const uint64 splitRelativeY = (uint64)(uint16)getVdpSpriteSplitRelativeY(spriteInfo, splitY);
		return ((uint64)(uint16)spriteInfo.mFirstPattern << 48)
			^ ((uint64)(uint16)spriteInfo.mSize.x << 32)
			^ ((uint64)(uint16)spriteInfo.mSize.y << 16)
			^ splitRelativeY;
	}

	uint64 hashVdpSpritePatterns(const renderitems::VdpSpriteInfo& spriteInfo, const PatternManager::CacheItem* patternCache)
	{
		uint64 hash = 1469598103934665603ull;
		for (int patternX = 0; patternX < spriteInfo.mSize.x; ++patternX)
		{
			for (int patternY = 0; patternY < spriteInfo.mSize.y; ++patternY)
			{
				const uint16 patternIndex = spriteInfo.mFirstPattern + patternY + patternX * spriteInfo.mSize.y;
				hash ^= (uint64)patternIndex;
				hash *= 1099511628211ull;
				hash = hashBytes(hash, patternCache[patternIndex & 0x07ff].mFlipVariation[0].mPixels, 64);
			}
		}
		return hash;
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

	void copyPaletteSpriteIndexRows(Bitmap& outBitmap, const PaletteBitmap& source)
	{
		const uint8* src = source.getData();
		for (int y = 0; y < source.getHeight(); ++y)
		{
			uint32* dst = outBitmap.getPixelPointer(0, y);
			for (int x = 0; x < source.getWidth(); ++x)
			{
				dst[x] = packDataPixel(src[x + y * source.getWidth()]);
			}
		}
	}

	void includeDirtyRect(Recti& dirtyRect, const Recti& rect)
	{
		if (rect.empty())
			return;

		if (dirtyRect.empty())
		{
			dirtyRect = rect;
			return;
		}

		const int minX = std::min(dirtyRect.x, rect.x);
		const int minY = std::min(dirtyRect.y, rect.y);
		const int maxX = std::max(dirtyRect.x + dirtyRect.width, rect.x + rect.width);
		const int maxY = std::max(dirtyRect.y + dirtyRect.height, rect.y + rect.height);
		dirtyRect.set(minX, minY, maxX - minX, maxY - minY);
	}

	void appendDirtyRegion(std::vector<Recti>& dirtyRegions, Recti& dirtyRect, const Recti& rect)
	{
		if (rect.empty())
			return;

		includeDirtyRect(dirtyRect, rect);
		if (!dirtyRegions.empty())
		{
			Recti& last = dirtyRegions.back();
			if (last.x == rect.x && last.width == rect.width && last.y + last.height == rect.y)
			{
				last.height += rect.height;
				return;
			}
			if (last.y == rect.y && last.height == rect.height && last.x + last.width == rect.x)
			{
				last.width += rect.width;
				return;
			}
		}
		dirtyRegions.push_back(rect);
	}

	void includePackedPatternRowRegions(std::vector<Recti>& dirtyRegions, Recti& dirtyRect, const Bitmap& destination, const std::vector<uint16>& patternIndices)
	{
		const int patternsPerRow = std::max(1, destination.getWidth() / PATTERN_CACHE_TEXTURE_SIZE.x);
		constexpr int patternRowCount = PLANE_DATA_PALETTE_Y - PLANE_DATA_PATTERN_Y;
		std::array<uint32, patternRowCount> rowMasks = {};
		for (uint16 patternIndex : patternIndices)
		{
			const int packedPatternX = patternIndex % patternsPerRow;
			const int packedX = packedPatternX * PATTERN_CACHE_TEXTURE_SIZE.x;
			const int packedY = patternIndex / patternsPerRow;
			if (packedY >= patternRowCount || packedY >= destination.getHeight() || packedX + PATTERN_CACHE_TEXTURE_SIZE.x > destination.getWidth() || packedPatternX >= 32)
				continue;
			rowMasks[packedY] |= 1u << packedPatternX;
		}

		for (int y = 0; y < patternRowCount; ++y)
		{
			const uint32 mask = rowMasks[y];
			if (mask == 0)
				continue;

			int firstPatternX = 0;
			while (firstPatternX < patternsPerRow && (mask & (1u << firstPatternX)) == 0)
				++firstPatternX;
			int lastPatternX = patternsPerRow - 1;
			while (lastPatternX > firstPatternX && (mask & (1u << lastPatternX)) == 0)
				--lastPatternX;
			appendDirtyRegion(dirtyRegions, dirtyRect, Recti(firstPatternX * PATTERN_CACHE_TEXTURE_SIZE.x, PLANE_DATA_PATTERN_Y + y, (lastPatternX - firstPatternX + 1) * PATTERN_CACHE_TEXTURE_SIZE.x, 1));
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

	void uploadBitmap(DrawerTexture& texture, Bitmap& bitmap)
	{
		texture.accessBitmap() = bitmap;
		texture.bitmapUpdated();
	}

	void uploadBitmapRegion(DrawerTexture& texture, Bitmap& bitmap, const Recti& dirtyRect)
	{
		Bitmap& textureBitmap = texture.accessBitmap();
		if (!texture.isValid() || texture.getSize() != bitmap.getSize() || textureBitmap.getSize() != bitmap.getSize() || dirtyRect.empty())
		{
			textureBitmap = bitmap;
			texture.bitmapUpdated();
			return;
		}

		const Recti rect = Recti::getIntersection(dirtyRect, Recti(0, 0, bitmap.getWidth(), bitmap.getHeight()));
		if (rect.empty())
			return;

		for (int y = 0; y < rect.height; ++y)
		{
			memcpy(textureBitmap.getPixelPointer(rect.x, rect.y + y), bitmap.getPixelPointer(rect.x, rect.y + y), (size_t)rect.width * sizeof(uint32));
		}
		texture.bitmapRegionUpdated(rect);
	}

	void uploadBitmapRegions(DrawerTexture& texture, Bitmap& bitmap, const std::vector<Recti>& dirtyRects)
	{
		if (dirtyRects.empty())
			return;

		Bitmap& textureBitmap = texture.accessBitmap();
		if (!texture.isValid() || texture.getSize() != bitmap.getSize() || textureBitmap.getSize() != bitmap.getSize())
		{
			textureBitmap = bitmap;
			texture.bitmapUpdated();
			return;
		}

		const Recti bounds(0, 0, bitmap.getWidth(), bitmap.getHeight());
		std::vector<Recti> clampedRects;
		clampedRects.reserve(dirtyRects.size());
		for (const Recti& dirtyRect : dirtyRects)
		{
			const Recti rect = Recti::getIntersection(dirtyRect, bounds);
			if (rect.empty())
				continue;

			for (int y = 0; y < rect.height; ++y)
			{
				memcpy(textureBitmap.getPixelPointer(rect.x, rect.y + y), bitmap.getPixelPointer(rect.x, rect.y + y), (size_t)rect.width * sizeof(uint32));
			}
			clampedRects.push_back(rect);
		}

		if (!clampedRects.empty())
		{
			texture.bitmapRegionsUpdated(clampedRects);
		}
	}

	bool updatePaletteBitmap(const PaletteBase& palette, Bitmap& bitmap, int offsetY, uint16& changeCounter)
	{
		if (changeCounter == palette.getChangeCounter())
			return false;

		palette.dumpColors(bitmap.getPixelPointer(0, offsetY), palette.getSize());
		changeCounter = palette.getChangeCounter();
		return true;
	}

	void copyBitmap(Bitmap& destination, int dstX, int dstY, const Bitmap& source)
	{
		if (destination.empty() || source.empty())
			return;

		const int copyWidth = std::min(source.getWidth(), destination.getWidth() - dstX);
		const int copyHeight = std::min(source.getHeight(), destination.getHeight() - dstY);
		if (copyWidth <= 0 || copyHeight <= 0)
			return;

		for (int y = 0; y < copyHeight; ++y)
		{
			memcpy(destination.getPixelPointer(dstX, dstY + y), source.getPixelPointer(0, y), (size_t)copyWidth * sizeof(uint32));
		}
	}

	void copyPackedPatternCache(Bitmap& destination, int dstX, int dstY, const Bitmap& source)
	{
		if (destination.empty() || source.empty())
			return;

		const int patternsPerRow = std::max(1, destination.getWidth() / PATTERN_CACHE_TEXTURE_SIZE.x);
		for (int patternIndex = 0; patternIndex < source.getHeight(); ++patternIndex)
		{
			const int packedX = dstX + (patternIndex % patternsPerRow) * PATTERN_CACHE_TEXTURE_SIZE.x;
			const int packedY = dstY + patternIndex / patternsPerRow;
			if (packedY >= destination.getHeight() || packedX + PATTERN_CACHE_TEXTURE_SIZE.x > destination.getWidth())
				break;
			memcpy(destination.getPixelPointer(packedX, packedY), source.getPixelPointer(0, patternIndex), (size_t)PATTERN_CACHE_TEXTURE_SIZE.x * sizeof(uint32));
		}
	}

	void copyPackedPatternRows(Bitmap& destination, int dstX, int dstY, const Bitmap& source, const std::vector<uint16>& patternIndices)
	{
		if (destination.empty() || source.empty())
			return;

		const int patternsPerRow = std::max(1, destination.getWidth() / PATTERN_CACHE_TEXTURE_SIZE.x);
		for (uint16 patternIndex : patternIndices)
		{
			if (patternIndex >= source.getHeight())
				continue;

			const int packedX = dstX + (patternIndex % patternsPerRow) * PATTERN_CACHE_TEXTURE_SIZE.x;
			const int packedY = dstY + patternIndex / patternsPerRow;
			if (packedY >= destination.getHeight() || packedX + PATTERN_CACHE_TEXTURE_SIZE.x > destination.getWidth())
				continue;
			memcpy(destination.getPixelPointer(packedX, packedY), source.getPixelPointer(0, patternIndex), (size_t)PATTERN_CACHE_TEXTURE_SIZE.x * sizeof(uint32));
		}
	}

	bool writePatternCacheRow(Bitmap& bitmap, int patternIndex, const PatternManager::CacheItem* patternCache)
	{
		uint32* dst = bitmap.getPixelPointer(0, patternIndex);
		const uint8* src = patternCache[patternIndex].mFlipVariation[0].mPixels;
		bool changed = false;
		for (int x = 0; x < 0x40; ++x)
		{
			const uint32 packed = packDataPixel(src[x]);
			changed |= (dst[x] != packed);
			dst[x] = packed;
		}
		return changed;
	}

	bool writePlanePatternEntry(Bitmap& bitmap, int x, int y, uint16 value)
	{
		uint32* dst = bitmap.getPixelPointer(x, y);
		const uint32 packed = packDataPixel((uint8)(value & 0xff), (uint8)(value >> 8));
		if (*dst == packed)
			return false;
		*dst = packed;
		return true;
	}

	bool writeScrollOffsetEntry(Bitmap& bitmap, int x, uint16 value)
	{
		uint32* dst = bitmap.getPixelPointer(x, 0);
		const uint32 packed = packDataPixel((uint8)(value & 0xff), (uint8)(value >> 8));
		if (*dst == packed)
			return false;
		*dst = packed;
		return true;
	}

	uint32 countNonZeroPixels(const Bitmap& bitmap, int maxRows)
	{
		if (bitmap.empty())
			return 0;

		uint32 count = 0;
		const int rows = std::min(bitmap.getHeight(), maxRows);
		for (int y = 0; y < rows; ++y)
		{
			const uint32* src = bitmap.getPixelPointer(0, y);
			for (int x = 0; x < bitmap.getWidth(); ++x)
			{
				if ((src[x] & 0x0000ffffu) != 0)
					++count;
			}
		}
		return count;
	}

	uint32 countPriorityPixels(const Bitmap& bitmap, int maxRows)
	{
		if (bitmap.empty())
			return 0;

		uint32 count = 0;
		const int rows = std::min(bitmap.getHeight(), maxRows);
		for (int y = 0; y < rows; ++y)
		{
			const uint32* src = bitmap.getPixelPointer(0, y);
			for (int x = 0; x < bitmap.getWidth(); ++x)
			{
				if ((src[x] & 0x00008000u) != 0)
					++count;
			}
		}
		return count;
	}

}


GX2RenderResources::GX2RenderResources(RenderParts& renderParts) :
	mRenderParts(renderParts)
{
}

void GX2RenderResources::initialize()
{
	mMainPaletteBitmap.create(PALETTE_TEXTURE_SIZE);
	mPatternCacheBitmap.create(PATTERN_CACHE_TEXTURE_SIZE);
	mPlaneDataBitmap.create(PLANE_DATA_TEXTURE_SIZE);
	mPlaneDataBitmap.clear(0);
	mPlaneDataTexture.setContentKnownOpaque(true);
	for (int index = 0; index < 4; ++index)
	{
		mPlanePatternBitmap[index].create(PLANE_PATTERN_TEXTURE_SIZE);
		mHScrollOffsetBitmap[index].create(H_SCROLL_TEXTURE_SIZE);
		mVScrollOffsetBitmap[index].create(V_SCROLL_TEXTURE_SIZE);
	}
	mHScrollOffsetBitmap[4].create(H_SCROLL_TEXTURE_SIZE);
	mVScrollOffsetBitmap[4].create(V_SCROLL_TEXTURE_SIZE);
	mHScrollOffsetBitmap[4].clear(packDataPixel(0, 0));
	mVScrollOffsetBitmap[4].clear(packDataPixel(0, 0));
	mPatternCacheInitialized = false;
	mPlanePatternPlayfieldSize.clear();
	for (int index = 0; index < 4; ++index)
	{
		mPlanePatternChangeCounters[index] = 0xffffffff;
		mPlanePatternBitmapInitialized[index] = false;
		mPlanePatternBitmapUsed[index] = false;
		mPlanePatternSourceCacheInitialized[index] = false;
		mPlanePatternSourceCache[index].clear();
		mPlanePatternPriorityCounts[index] = 0;
		mPlaneDataPlaneDirty[index] = true;
	}
	for (int index = 0; index < 5; ++index)
	{
		mScrollOffsetBitmapInitialized[index] = false;
		mScrollOffsetChangeCounters[index] = 0xffffffff;
		mPlaneDataScrollDirty[index] = true;
	}
	mPlaneDataInitialized = false;
	mPlaneDataPatternFullDirty = true;
	mPlaneDataPatternDirty = true;
	mPlaneDataPaletteDirty = true;
	mChangedPatternIndices.clear();
}

void GX2RenderResources::refresh()
{
#if defined(PLATFORM_WIIU)
	const uint64 t0 = ENABLE_WIIU_GX2_RESOURCE_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
#endif
	refreshMainPaletteTexture();
#if defined(PLATFORM_WIIU)
	const uint64 t1 = ENABLE_WIIU_GX2_RESOURCE_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
#endif
	refreshPatternCacheTexture();
#if defined(PLATFORM_WIIU)
	const uint64 t2 = ENABLE_WIIU_GX2_RESOURCE_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
#endif
	refreshPlanePatternTextures();
#if defined(PLATFORM_WIIU)
	const uint64 t3 = ENABLE_WIIU_GX2_RESOURCE_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
#endif
	refreshScrollOffsetTextures();
#if defined(PLATFORM_WIIU)
	const uint64 t4 = ENABLE_WIIU_GX2_RESOURCE_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
#endif
	refreshPlaneDataTexture();
#if defined(PLATFORM_WIIU)
	const uint64 t5 = ENABLE_WIIU_GX2_RESOURCE_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
	if constexpr (ENABLE_WIIU_GX2_RESOURCE_TIMING_LOGS)
	{
		static uint32 sSampleCount = 0;
		static double sPaletteMs = 0.0;
		static double sPatternMs = 0.0;
		static double sPlaneMs = 0.0;
		static double sScrollMs = 0.0;
		static double sPlaneDataMs = 0.0;
		++sSampleCount;
		sPaletteMs += getElapsedMilliseconds(t0, t1);
		sPatternMs += getElapsedMilliseconds(t1, t2);
		sPlaneMs += getElapsedMilliseconds(t2, t3);
		sScrollMs += getElapsedMilliseconds(t3, t4);
		sPlaneDataMs += getElapsedMilliseconds(t4, t5);
		if ((sSampleCount % 180) == 0)
		{
			const double inv = 1.0 / 180.0;
			RMX_LOG_INFO("GX2 resources timing avg palette=" << (float)(sPaletteMs * inv)
				<< "ms pattern=" << (float)(sPatternMs * inv)
				<< "ms plane=" << (float)(sPlaneMs * inv)
				<< "ms scroll=" << (float)(sScrollMs * inv)
				<< "ms planeData=" << (float)(sPlaneDataMs * inv)
				<< "ms total=" << (float)((sPaletteMs + sPatternMs + sPlaneMs + sScrollMs + sPlaneDataMs) * inv)
				<< "ms");
			sPaletteMs = 0.0;
			sPatternMs = 0.0;
			sPlaneMs = 0.0;
			sScrollMs = 0.0;
			sPlaneDataMs = 0.0;
		}
	}
#endif
}

void GX2RenderResources::clearAllCaches()
{
	mVdpSpriteTextures.clear();
	mComponentSpriteTextures.clear();
	mPaletteSpriteIndexTextures.clear();
	mPaletteSpriteTextures.clear();
	mPaletteTextures.clear();
	for (uint16& counter : mMainPaletteChangeCounters)
	{
		--counter;
	}
	mPatternCacheInitialized = false;
	mPlanePatternPlayfieldSize.clear();
	for (bool& initialized : mPlanePatternBitmapInitialized)
	{
		initialized = false;
	}
	for (uint32& counter : mPlanePatternChangeCounters)
	{
		counter = 0xffffffff;
	}
	for (bool& used : mPlanePatternBitmapUsed)
	{
		used = false;
	}
	for (bool& initialized : mPlanePatternSourceCacheInitialized)
	{
		initialized = false;
	}
	for (uint32& priorityCount : mPlanePatternPriorityCounts)
	{
		priorityCount = 0;
	}
	for (std::vector<uint16>& cache : mPlanePatternSourceCache)
	{
		cache.clear();
	}
	for (bool& initialized : mScrollOffsetBitmapInitialized)
	{
		initialized = false;
	}
	for (uint32& counter : mScrollOffsetChangeCounters)
	{
		counter = 0xffffffff;
	}
	mPlaneDataInitialized = false;
	mPlaneDataPatternFullDirty = true;
	mPlaneDataPatternDirty = true;
	mPlaneDataPaletteDirty = true;
	for (bool& dirty : mPlaneDataPlaneDirty)
	{
		dirty = true;
	}
	for (bool& dirty : mPlaneDataScrollDirty)
	{
		dirty = true;
	}
	mChangedPatternIndices.clear();
}

DrawerTexture& GX2RenderResources::getMainPaletteTexture()
{
	return mMainPaletteTexture;
}

DrawerTexture& GX2RenderResources::getPatternCacheTexture()
{
	return mPatternCacheTexture;
}

DrawerTexture& GX2RenderResources::getPlanePatternsTexture(int planeIndex)
{
	RMX_ASSERT(planeIndex >= 0 && planeIndex < 4, "Invalid plane index " << planeIndex);
	return mPlanePatternTextures[clamp(planeIndex, 0, 3)];
}

DrawerTexture& GX2RenderResources::getHScrollOffsetsTexture(int scrollOffsetsIndex)
{
	return mHScrollOffsetTextures[(scrollOffsetsIndex == 0xff) ? 4 : clamp(scrollOffsetsIndex, 0, 3)];
}

DrawerTexture& GX2RenderResources::getVScrollOffsetsTexture(int scrollOffsetsIndex)
{
	return mVScrollOffsetTextures[(scrollOffsetsIndex == 0xff) ? 4 : clamp(scrollOffsetsIndex, 0, 3)];
}

DrawerTexture& GX2RenderResources::getPlaneDataTexture()
{
	return mPlaneDataTexture;
}

bool GX2RenderResources::hasPriorityPlanePatterns(int planeIndex) const
{
	return (planeIndex >= 0 && planeIndex < 4) && mPlanePatternPriorityCounts[planeIndex] > 0;
}

DrawerTexture& GX2RenderResources::getVdpSpriteTexture(const renderitems::VdpSpriteInfo& spriteInfo)
{
	const PaletteManager& paletteManager = mRenderParts.getPaletteManager();
	const uint64 cacheKey = getVdpSpriteCacheKey(spriteInfo, paletteManager.mSplitPositionY);

	CachedSpriteTexture& cached = mVdpSpriteTextures[cacheKey];
	if (!cached.mTexture)
	{
		cached.mTexture.reset(new DrawerTexture());
	}

	const Vec2i sourceSize(spriteInfo.mSize.x * 8, spriteInfo.mSize.y * 8);
	const uint16 primaryPaletteCounter = paletteManager.getMainPalette(0).getChangeCounter();
	const uint16 secondaryPaletteCounter = paletteManager.getMainPalette(1).getChangeCounter();
	const uint64 patternSignature = hashVdpSpritePatterns(spriteInfo, mRenderParts.getPatternManager().getPatternCache());
	if (cached.mSourceSize == sourceSize
		&& cached.mPaletteChangeCounter == primaryPaletteCounter
		&& cached.mSecondaryPaletteChangeCounter == secondaryPaletteCounter
		&& cached.mPatternSignature == patternSignature)
	{
		return *cached.mTexture;
	}

	Bitmap bitmap;
	buildVdpSpriteBitmap(bitmap, spriteInfo, mRenderParts);
	cached.mTexture->accessBitmap().swap(bitmap);
	cached.mTexture->bitmapUpdated();
	cached.mSourceSize = sourceSize;
	cached.mPaletteChangeCounter = primaryPaletteCounter;
	cached.mSecondaryPaletteChangeCounter = secondaryPaletteCounter;
	cached.mPatternSignature = patternSignature;
	return *cached.mTexture;
}

DrawerTexture& GX2RenderResources::getComponentSpriteTexture(const renderitems::ComponentSpriteInfo& spriteInfo)
{
	CachedSpriteTexture& cached = mComponentSpriteTextures[spriteInfo.mKey];
	if (!cached.mTexture)
	{
		cached.mTexture.reset(new DrawerTexture());
	}

	const ComponentSprite& sprite = *static_cast<ComponentSprite*>(spriteInfo.mCacheItem->mSprite);
	const Bitmap& source = sprite.getBitmap();
	const uint32 spriteChangeCounter = spriteInfo.mCacheItem->mChangeCounter;
	if (cached.mSourceSize != source.getSize() || cached.mSpriteChangeCounter != spriteChangeCounter)
	{
		cached.mTexture->accessBitmap() = source;
		cached.mTexture->bitmapUpdated();
		cached.mSourceSize = source.getSize();
		cached.mSpriteChangeCounter = spriteChangeCounter;
	}
	return *cached.mTexture;
}

DrawerTexture& GX2RenderResources::getPaletteTexture(const PaletteBase* primaryPalette, const PaletteBase* secondaryPalette)
{
	const PaletteManager& paletteManager = mRenderParts.getPaletteManager();
	if (nullptr == primaryPalette)
		primaryPalette = &paletteManager.getMainPalette(0);
	if (nullptr == secondaryPalette)
		secondaryPalette = &paletteManager.getMainPalette(1);

	if (primaryPalette == &paletteManager.getMainPalette(0) && secondaryPalette == &paletteManager.getMainPalette(1))
	{
		return mMainPaletteTexture;
	}

	uint64 cacheKey = hashPointer(0xcbf29ce484222325ull, primaryPalette);
	cacheKey = hashPointer(cacheKey, secondaryPalette);
	CachedPaletteTexture& cached = mPaletteTextures[cacheKey];
	if (!cached.mTexture)
	{
		cached.mTexture.reset(new DrawerTexture());
	}
	if (cached.mBitmap.empty())
	{
		cached.mBitmap.create(PALETTE_TEXTURE_SIZE);
		cached.mBitmap.clear(0);
	}

	const bool primaryChanged = updatePaletteBitmap(*primaryPalette, cached.mBitmap, 0, cached.mChangeCounters[0]);
	const bool secondaryChanged = updatePaletteBitmap(*secondaryPalette, cached.mBitmap, 2, cached.mChangeCounters[1]);
	if (primaryChanged || secondaryChanged)
	{
		uploadBitmap(*cached.mTexture, cached.mBitmap);
	}
	return *cached.mTexture;
}

DrawerTexture& GX2RenderResources::getPaletteSpriteDataTexture(const renderitems::PaletteSpriteInfo& spriteInfo, const PaletteBase& primaryPalette, const PaletteBase& secondaryPalette)
{
	uint64 cacheKey = spriteInfo.mKey;
	if (spriteInfo.mUseUpscaledSprite)
		cacheKey ^= 0x8000000000000000ull;
	cacheKey = hashPointer(cacheKey, &primaryPalette);
	cacheKey = hashPointer(cacheKey, &secondaryPalette);

	CachedSpriteTexture& cached = mPaletteSpriteIndexTextures[cacheKey];
	if (!cached.mTexture)
	{
		cached.mTexture.reset(new DrawerTexture());
		cached.mTexture->setContentKnownOpaque(true);
	}

	const PaletteSprite& sprite = *static_cast<PaletteSprite*>(spriteInfo.mCacheItem->mSprite);
	const PaletteBitmap& source = spriteInfo.mUseUpscaledSprite ? sprite.getUpscaledBitmap() : sprite.getBitmap();
	const uint32 spriteChangeCounter = spriteInfo.mCacheItem->mChangeCounter;
	const Vec2i dataSize(std::max(source.getWidth(), PALETTE_TEXTURE_SIZE.x), source.getHeight() + PALETTE_TEXTURE_SIZE.y);
	bool changed = false;
	bool forceFullUpload = false;
	Recti dirtyRect;
	if (cached.mSourceSize != source.getSize() || cached.mTexture->getBitmap().getSize() != dataSize)
	{
		cached.mTexture->accessBitmap().create(dataSize);
		cached.mTexture->accessBitmap().clear(0);
		cached.mSourceSize = source.getSize();
		cached.mSpriteChangeCounter = 0xffffffff;
		cached.mPaletteChangeCounter = 0xffff;
		cached.mSecondaryPaletteChangeCounter = 0xffff;
		changed = true;
		forceFullUpload = true;
	}
	if (cached.mSpriteChangeCounter != spriteChangeCounter)
	{
		copyPaletteSpriteIndexRows(cached.mTexture->accessBitmap(), source);
		cached.mSpriteChangeCounter = spriteChangeCounter;
		changed = true;
		includeDirtyRect(dirtyRect, Recti(0, 0, source.getWidth(), source.getHeight()));
	}
	if (updatePaletteBitmap(primaryPalette, cached.mTexture->accessBitmap(), source.getHeight(), cached.mPaletteChangeCounter))
	{
		changed = true;
		includeDirtyRect(dirtyRect, Recti(0, source.getHeight(), PALETTE_TEXTURE_SIZE.x, PALETTE_TEXTURE_SIZE.y / 2));
	}
	if (updatePaletteBitmap(secondaryPalette, cached.mTexture->accessBitmap(), source.getHeight() + 2, cached.mSecondaryPaletteChangeCounter))
	{
		changed = true;
		includeDirtyRect(dirtyRect, Recti(0, source.getHeight() + PALETTE_TEXTURE_SIZE.y / 2, PALETTE_TEXTURE_SIZE.x, PALETTE_TEXTURE_SIZE.y / 2));
	}
	if (changed)
	{
		if (forceFullUpload || dirtyRect.empty())
		{
			cached.mTexture->bitmapUpdated();
		}
		else
		{
			cached.mTexture->bitmapRegionUpdated(dirtyRect);
		}
	}
	return *cached.mTexture;
}

DrawerTexture& GX2RenderResources::getPaletteSpriteTexture(const renderitems::PaletteSpriteInfo& spriteInfo, const PaletteBase& palette)
{
	uint64 cacheKey = spriteInfo.mKey ^ ((uint64)spriteInfo.mAtex << 48);
	cacheKey = hashPointer(cacheKey, &palette);
	if (spriteInfo.mUseUpscaledSprite)
		cacheKey ^= 0x8000000000000000ull;

	CachedSpriteTexture& cached = mPaletteSpriteTextures[cacheKey];
	if (!cached.mTexture)
	{
		cached.mTexture.reset(new DrawerTexture());
	}

	const PaletteSprite& sprite = *static_cast<PaletteSprite*>(spriteInfo.mCacheItem->mSprite);
	const PaletteBitmap& source = spriteInfo.mUseUpscaledSprite ? sprite.getUpscaledBitmap() : sprite.getBitmap();
	const uint32 spriteChangeCounter = spriteInfo.mCacheItem->mChangeCounter;
	const uint16 paletteChangeCounter = palette.getChangeCounter();
	if (cached.mSourceSize != source.getSize()
		|| cached.mSpriteChangeCounter != spriteChangeCounter
		|| cached.mPaletteChangeCounter != paletteChangeCounter)
	{
		Bitmap bitmap;
		buildPaletteSpriteBitmap(bitmap, source, palette, spriteInfo.mAtex);
		cached.mTexture->accessBitmap().swap(bitmap);
		cached.mTexture->bitmapUpdated();
		cached.mSourceSize = source.getSize();
		cached.mSpriteChangeCounter = spriteChangeCounter;
		cached.mPaletteChangeCounter = paletteChangeCounter;
	}
	return *cached.mTexture;
}

void GX2RenderResources::refreshMainPaletteTexture()
{
	PaletteManager& paletteManager = mRenderParts.getPaletteManager();
	Palette& primaryPalette = paletteManager.getMainPalette(0);
	Palette& secondaryPalette = paletteManager.getMainPalette(1);
	if (mMainPaletteBitmap.empty())
		mMainPaletteBitmap.create(PALETTE_TEXTURE_SIZE);
	if (mMainPaletteChangeCounters[0] == primaryPalette.getChangeCounter()
		&& mMainPaletteChangeCounters[1] == secondaryPalette.getChangeCounter())
	{
		return;
	}

	primaryPalette.dumpColors(mMainPaletteBitmap.getPixelPointer(0, 0), primaryPalette.getSize());
	secondaryPalette.dumpColors(mMainPaletteBitmap.getPixelPointer(0, 2), secondaryPalette.getSize());
	uploadBitmap(mMainPaletteTexture, mMainPaletteBitmap);
	mMainPaletteChangeCounters[0] = primaryPalette.getChangeCounter();
	mMainPaletteChangeCounters[1] = secondaryPalette.getChangeCounter();
	mPlaneDataPaletteDirty = true;
}

void GX2RenderResources::refreshPatternCacheTexture()
{
	if (mPatternCacheBitmap.empty())
		mPatternCacheBitmap.create(PATTERN_CACHE_TEXTURE_SIZE);

	mChangedPatternIndices.clear();
	const PatternManager::CacheItem* patternCache = mRenderParts.getPatternManager().getPatternCache();
	if (!mPatternCacheInitialized)
	{
		for (int patternIndex = 0; patternIndex < 0x800; ++patternIndex)
		{
			writePatternCacheRow(mPatternCacheBitmap, patternIndex, patternCache);
		}
		if constexpr (UPLOAD_LEGACY_PLANE_TEXTURES)
		{
			uploadBitmap(mPatternCacheTexture, mPatternCacheBitmap);
		}
		mPatternCacheInitialized = true;
		mPlaneDataPatternFullDirty = true;
		mPlaneDataPatternDirty = true;
		return;
	}

	const BitArray<0x800>& changeBits = mRenderParts.getPatternManager().getChangeBits();
	for (int bitSetChunkIndex = 0; bitSetChunkIndex < 32; ++bitSetChunkIndex)
	{
		if (!changeBits.anyBitSetInChunk(bitSetChunkIndex))
			continue;

		for (int bitIndex = 0; bitIndex < 64; ++bitIndex)
		{
			const int patternIndex = (bitSetChunkIndex << 6) + bitIndex;
			if (changeBits.isBitSet(patternIndex) && writePatternCacheRow(mPatternCacheBitmap, patternIndex, patternCache))
			{
				mChangedPatternIndices.push_back((uint16)patternIndex);
			}
		}
	}
	if (!mChangedPatternIndices.empty())
	{
		if constexpr (UPLOAD_LEGACY_PLANE_TEXTURES)
		{
			uploadBitmap(mPatternCacheTexture, mPatternCacheBitmap);
		}
		mPlaneDataPatternDirty = true;
	}
}

void GX2RenderResources::refreshPlanePatternTextures()
{
	PlaneManager& planeManager = mRenderParts.getPlaneManager();
	const Vec2i playfieldSize = planeManager.getPlayfieldSizeInPatterns();
	const bool playfieldSizeChanged = (mPlanePatternPlayfieldSize != playfieldSize);
	if (playfieldSizeChanged)
	{
		mPlanePatternPlayfieldSize = playfieldSize;
	}
	for (int index = 0; index < 4; ++index)
	{
		if (mPlanePatternBitmap[index].empty())
			mPlanePatternBitmap[index].create(PLANE_PATTERN_TEXTURE_SIZE);

		const bool planeUsed = planeManager.isPlaneUsed(index);
		if (!mPlanePatternBitmapInitialized[index] || playfieldSizeChanged || mPlanePatternBitmapUsed[index] != planeUsed)
		{
			mPlanePatternBitmap[index].clear(packDataPixel(0, 0));
			mPlanePatternBitmapInitialized[index] = true;
			mPlanePatternBitmapUsed[index] = planeUsed;
			mPlanePatternSourceCacheInitialized[index] = false;
			mPlanePatternChangeCounters[index] = 0xffffffff;
			mPlanePatternPriorityCounts[index] = 0;
			mPlaneDataPlaneDirty[index] = true;
		}

		if (!planeUsed)
		{
			mPlanePatternSourceCache[index].clear();
			mPlanePatternChangeCounters[index] = 0xffffffff;
			mPlanePatternPriorityCounts[index] = 0;
			if (mPlaneDataPlaneDirty[index])
			{
				if constexpr (UPLOAD_LEGACY_PLANE_TEXTURES)
				{
					uploadBitmap(mPlanePatternTextures[index], mPlanePatternBitmap[index]);
				}
			}
			continue;
		}

		const uint32 planeChangeCounter = planeManager.getPlanePatternsChangeCounter((uint8)index);
		if (mPlanePatternChangeCounters[index] == planeChangeCounter)
			continue;
		mPlanePatternChangeCounters[index] = planeChangeCounter;

		const uint16* source = planeManager.getPlanePatternsBuffer((uint8)index);
		const int copyWidth = std::min(playfieldSize.x, PLANE_PATTERN_TEXTURE_SIZE.x);
		const int copyHeight = std::min(playfieldSize.y, PLANE_PATTERN_TEXTURE_SIZE.y);
		const size_t sourceCacheSize = (size_t)copyWidth * (size_t)copyHeight;
		if (mPlanePatternSourceCache[index].size() != sourceCacheSize)
		{
			mPlanePatternSourceCache[index].resize(sourceCacheSize);
			mPlanePatternSourceCacheInitialized[index] = false;
		}

		bool changed = false;
		for (int y = 0; y < copyHeight; ++y)
		{
			const uint16* src = source + (size_t)y * (size_t)playfieldSize.x;
			uint16* cached = &mPlanePatternSourceCache[index][(size_t)y * (size_t)copyWidth];
			const bool rowChanged = !mPlanePatternSourceCacheInitialized[index]
				|| memcmp(cached, src, (size_t)copyWidth * sizeof(uint16)) != 0;
			if (!rowChanged)
				continue;

			memcpy(cached, src, (size_t)copyWidth * sizeof(uint16));
			for (int x = 0; x < copyWidth; ++x)
			{
				changed |= writePlanePatternEntry(mPlanePatternBitmap[index], x, y, src[x]);
			}
		}
		mPlanePatternSourceCacheInitialized[index] = true;
		if (changed)
		{
			if constexpr (UPLOAD_LEGACY_PLANE_TEXTURES)
			{
				uploadBitmap(mPlanePatternTextures[index], mPlanePatternBitmap[index]);
			}
			mPlanePatternPriorityCounts[index] = countPriorityPixels(mPlanePatternBitmap[index], PLANE_PATTERN_TEXTURE_SIZE.y);
		}
		mPlaneDataPlaneDirty[index] |= changed;
	}
}

void GX2RenderResources::refreshScrollOffsetTextures()
{
	const ScrollOffsetsManager& scrollOffsetsManager = mRenderParts.getScrollOffsetsManager();
	for (int index = 0; index < 4; ++index)
	{
		if (mHScrollOffsetBitmap[index].empty())
			mHScrollOffsetBitmap[index].create(H_SCROLL_TEXTURE_SIZE);
		if (mVScrollOffsetBitmap[index].empty())
			mVScrollOffsetBitmap[index].create(V_SCROLL_TEXTURE_SIZE);
		if (!mScrollOffsetBitmapInitialized[index])
		{
			mHScrollOffsetBitmap[index].clear(packDataPixel(0, 0));
			mVScrollOffsetBitmap[index].clear(packDataPixel(0, 0));
			mScrollOffsetBitmapInitialized[index] = true;
			mScrollOffsetChangeCounters[index] = 0xffffffff;
			mPlaneDataScrollDirty[index] = true;
		}

		const uint32 scrollChangeCounter = scrollOffsetsManager.getScrollOffsetsChangeCounter(index);
		if (mScrollOffsetChangeCounters[index] == scrollChangeCounter)
			continue;
		mScrollOffsetChangeCounters[index] = scrollChangeCounter;

		const uint16* hSource = scrollOffsetsManager.getScrollOffsetsH(index);
		bool changed = false;
		for (int x = 0; x < H_SCROLL_TEXTURE_SIZE.x; ++x)
		{
			changed |= writeScrollOffsetEntry(mHScrollOffsetBitmap[index], x, hSource[x]);
		}

		const uint16* vSource = scrollOffsetsManager.getScrollOffsetsV(index);
		for (int x = 0; x < V_SCROLL_TEXTURE_SIZE.x; ++x)
		{
			changed |= writeScrollOffsetEntry(mVScrollOffsetBitmap[index], x, vSource[x]);
		}
		if (changed)
		{
			if constexpr (UPLOAD_LEGACY_PLANE_TEXTURES)
			{
				uploadBitmap(mHScrollOffsetTextures[index], mHScrollOffsetBitmap[index]);
				uploadBitmap(mVScrollOffsetTextures[index], mVScrollOffsetBitmap[index]);
			}
		}
		mPlaneDataScrollDirty[index] |= changed;
	}

	if (!mScrollOffsetBitmapInitialized[4])
	{
		if (mHScrollOffsetBitmap[4].empty())
			mHScrollOffsetBitmap[4].create(H_SCROLL_TEXTURE_SIZE);
		if (mVScrollOffsetBitmap[4].empty())
			mVScrollOffsetBitmap[4].create(V_SCROLL_TEXTURE_SIZE);
		mHScrollOffsetBitmap[4].clear(packDataPixel(0, 0));
		mVScrollOffsetBitmap[4].clear(packDataPixel(0, 0));
		mScrollOffsetBitmapInitialized[4] = true;
		mPlaneDataScrollDirty[4] = true;
		if constexpr (UPLOAD_LEGACY_PLANE_TEXTURES)
		{
			uploadBitmap(mHScrollOffsetTextures[4], mHScrollOffsetBitmap[4]);
			uploadBitmap(mVScrollOffsetTextures[4], mVScrollOffsetBitmap[4]);
		}
	}
}

void GX2RenderResources::refreshPlaneDataTexture()
{
	if (mPlaneDataBitmap.empty())
		mPlaneDataBitmap.create(PLANE_DATA_TEXTURE_SIZE);

	if (!mPlaneDataInitialized)
	{
		mPlaneDataBitmap.clear(0);
		mPlaneDataPatternFullDirty = true;
		mPlaneDataPatternDirty = true;
		mPlaneDataPaletteDirty = true;
		for (int index = 0; index < 4; ++index)
		{
			mPlaneDataPlaneDirty[index] = true;
		}
		for (int index = 0; index < 5; ++index)
		{
			mPlaneDataScrollDirty[index] = true;
		}
		mPlaneDataInitialized = true;
	}

	bool dirty = mPlaneDataPatternDirty || mPlaneDataPaletteDirty;
	uint32 planeDirtyMask = 0;
	uint32 scrollDirtyMask = 0;
	for (int index = 0; index < 4; ++index)
	{
		dirty |= mPlaneDataPlaneDirty[index];
		if (mPlaneDataPlaneDirty[index])
			planeDirtyMask |= 1u << index;
	}
	for (int index = 0; index < 5; ++index)
	{
		dirty |= mPlaneDataScrollDirty[index];
		if (mPlaneDataScrollDirty[index])
			scrollDirtyMask |= 1u << index;
	}
	if (!dirty)
		return;

	Recti dirtyRect;
	std::vector<Recti> dirtyRegions;
	dirtyRegions.reserve(24);
	if (mPlaneDataPatternDirty)
	{
		if (mPlaneDataPatternFullDirty || mChangedPatternIndices.size() > 128)
		{
			copyPackedPatternCache(mPlaneDataBitmap, 0, PLANE_DATA_PATTERN_Y, mPatternCacheBitmap);
			appendDirtyRegion(dirtyRegions, dirtyRect, Recti(0, PLANE_DATA_PATTERN_Y, mPlaneDataBitmap.getWidth(), PLANE_DATA_PALETTE_Y - PLANE_DATA_PATTERN_Y));
		}
		else
		{
			copyPackedPatternRows(mPlaneDataBitmap, 0, PLANE_DATA_PATTERN_Y, mPatternCacheBitmap, mChangedPatternIndices);
			includePackedPatternRowRegions(dirtyRegions, dirtyRect, mPlaneDataBitmap, mChangedPatternIndices);
		}
	}
	if (mPlaneDataPaletteDirty)
	{
		copyBitmap(mPlaneDataBitmap, 0, PLANE_DATA_PALETTE_Y, mMainPaletteBitmap);
		appendDirtyRegion(dirtyRegions, dirtyRect, Recti(0, PLANE_DATA_PALETTE_Y, PALETTE_TEXTURE_SIZE.x, PALETTE_TEXTURE_SIZE.y));
	}
	for (int index = 0; index < 4; ++index)
	{
		if (mPlaneDataPlaneDirty[index])
		{
			const int planeX = index * PLANE_PATTERN_TEXTURE_SIZE.x;
			copyBitmap(mPlaneDataBitmap, planeX, PLANE_DATA_INDEX_Y, mPlanePatternBitmap[index]);
			appendDirtyRegion(dirtyRegions, dirtyRect, Recti(planeX, PLANE_DATA_INDEX_Y, PLANE_PATTERN_TEXTURE_SIZE.x, PLANE_PATTERN_TEXTURE_SIZE.y));
		}
	}
	for (int index = 0; index < 5; ++index)
		if (mPlaneDataScrollDirty[index])
		{
			copyBitmap(mPlaneDataBitmap, 0, PLANE_DATA_HSCROLL_Y + index, mHScrollOffsetBitmap[index]);
			copyBitmap(mPlaneDataBitmap, 0, PLANE_DATA_VSCROLL_Y + index, mVScrollOffsetBitmap[index]);
			appendDirtyRegion(dirtyRegions, dirtyRect, Recti(0, PLANE_DATA_HSCROLL_Y + index, H_SCROLL_TEXTURE_SIZE.x, H_SCROLL_TEXTURE_SIZE.y));
			appendDirtyRegion(dirtyRegions, dirtyRect, Recti(0, PLANE_DATA_VSCROLL_Y + index, V_SCROLL_TEXTURE_SIZE.x, V_SCROLL_TEXTURE_SIZE.y));
		}

	if (!dirtyRegions.empty())
	{
		uploadBitmapRegions(mPlaneDataTexture, mPlaneDataBitmap, dirtyRegions);
	}
#if defined(PLATFORM_WIIU)
	static constexpr bool ENABLE_WIIU_PLANE_UPLOAD_DIAGNOSTICS = false;
	static uint32 sPlaneDataUploadLogCount = 0;
	static uint32 sPriorityPlaneBUploadLogCount = 0;
	if constexpr (ENABLE_WIIU_PLANE_UPLOAD_DIAGNOSTICS)
	{
		const bool mayLogStartupUpload = sPlaneDataUploadLogCount < 2;
		const bool mayLogPriorityPlaneBUpload = ((planeDirtyMask & 0x01) != 0 && sPriorityPlaneBUploadLogCount < 2);
		const uint32 planeBPriority = mayLogPriorityPlaneBUpload ? mPlanePatternPriorityCounts[0] : 0;
		const bool logPriorityPlaneBUpload = mayLogPriorityPlaneBUpload && planeBPriority > 0;
		if (mayLogStartupUpload || logPriorityPlaneBUpload)
		{
			const uint32 planeBNonZero = countNonZeroPixels(mPlanePatternBitmap[0], PLANE_PATTERN_TEXTURE_SIZE.y);
			RMX_LOG_INFO("GX2RenderResources: plane atlas upload " << sPlaneDataUploadLogCount
				<< " patternDirty=" << (mPlaneDataPatternDirty ? 1 : 0)
				<< " patternFull=" << (mPlaneDataPatternFullDirty ? 1 : 0)
				<< " changedPatterns=" << (uint32)mChangedPatternIndices.size()
				<< " paletteDirty=" << (mPlaneDataPaletteDirty ? 1 : 0)
				<< " planeMask=" << rmx::hexString(planeDirtyMask, 2)
				<< " scrollMask=" << rmx::hexString(scrollDirtyMask, 2)
				<< " dirtyRect=" << dirtyRect.x << "," << dirtyRect.y << " " << dirtyRect.width << "x" << dirtyRect.height
				<< " planeBNonZero=" << planeBNonZero
				<< " planeBPriority=" << planeBPriority
				<< " planeANonZero=" << countNonZeroPixels(mPlanePatternBitmap[1], PLANE_PATTERN_TEXTURE_SIZE.y)
				<< " planeAPriority=" << mPlanePatternPriorityCounts[1]
				<< " planeWNonZero=" << countNonZeroPixels(mPlanePatternBitmap[2], PLANE_PATTERN_TEXTURE_SIZE.y)
				<< " planeWPriority=" << mPlanePatternPriorityCounts[2]);
			++sPlaneDataUploadLogCount;
			if (logPriorityPlaneBUpload)
				++sPriorityPlaneBUploadLogCount;
		}
	}
#endif
	mPlaneDataPatternFullDirty = false;
	mPlaneDataPatternDirty = false;
	mPlaneDataPaletteDirty = false;
	for (int index = 0; index < 4; ++index)
		mPlaneDataPlaneDirty[index] = false;
	for (int index = 0; index < 5; ++index)
		mPlaneDataScrollDirty[index] = false;
}
