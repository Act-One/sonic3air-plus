/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)

#include "oxygen/rendering/vulkan/VulkanRenderResources.h"

#include "oxygen/drawing/vulkan/VulkanDrawerResources.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/rendering/parts/palette/PaletteManager.h"
#include "oxygen/rendering/sprite/ComponentSprite.h"
#include "oxygen/rendering/sprite/PaletteSprite.h"
#include "oxygen/resources/SpriteCollection.h"


namespace
{
	const Vec2i PALETTE_TEXTURE_SIZE(256, (int)(PaletteManager::MAIN_PALETTE_SIZE / 256) * 2);
}


VulkanRenderResources::VulkanRenderResources(RenderParts& renderParts, VulkanDrawerResources& drawerResources) :
	mRenderParts(renderParts),
	mDrawerResources(drawerResources)
{
	clearAllCaches();
}

VulkanRenderResources::~VulkanRenderResources()
{
	clearAllCaches();
}

void VulkanRenderResources::initialize()
{
	mMainPalette.mBitmap.create(PALETTE_TEXTURE_SIZE.x, PALETTE_TEXTURE_SIZE.y);
	mPatternCacheBitmap.create(0x40, 0x800);
}

void VulkanRenderResources::refresh()
{
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr == backend)
		return;

	RenderParts& renderParts = RenderParts::instance();
	PaletteManager& paletteManager = renderParts.getPaletteManager();
	updatePalette(mMainPalette, paletteManager.getMainPalette(0), paletteManager.getMainPalette(1));

	// Pattern cache
	{
		const PatternManager::CacheItem* patternCache = renderParts.getPatternManager().getPatternCache();
		for (int patternIndex = 0; patternIndex < 0x800; ++patternIndex)
		{
			const uint8* src = patternCache[patternIndex].mFlipVariation[0].mPixels;
			uint8* dst = &mPatternCacheBitmap[patternIndex * 0x40];
			std::memcpy(dst, src, 0x40);
		}
		mPatternCacheTexture.updateRaw(backend->getDevice(), backend->getAllocator(), nullptr, mPatternCacheBitmap.getSize(), VK_FORMAT_R8_UINT, mPatternCacheBitmap.getData(), mPatternCacheBitmap.getWidth());
	}

	// Plane patterns
	{
		const PlaneManager& planeManager = renderParts.getPlaneManager();
		for (int index = 0; index < 4; ++index)
		{
			if (!planeManager.isPlaneUsed(index))
				continue;
			mPlanePatternsTexture[index].updateRaw(backend->getDevice(), backend->getAllocator(), nullptr, Vec2i(PlaneManager::MAX_PLANE_PATTERNS, 1), VK_FORMAT_R16_UINT, planeManager.getPlanePatternsBuffer((uint8)index), PlaneManager::MAX_PLANE_PATTERNS * sizeof(uint16));
		}
	}

	// Scroll offsets
	{
		const ScrollOffsetsManager& scrollOffsetsManager = renderParts.getScrollOffsetsManager();
		for (int index = 0; index < 4; ++index)
		{
			mHScrollOffsetsTexture[index].updateRaw(backend->getDevice(), backend->getAllocator(), nullptr, Vec2i(0x100, 1), VK_FORMAT_R16_SINT, scrollOffsetsManager.getScrollOffsetsH(index), 0x100 * sizeof(uint16));
			mVScrollOffsetsTexture[index].updateRaw(backend->getDevice(), backend->getAllocator(), nullptr, Vec2i(0x20, 1), VK_FORMAT_R16_SINT, scrollOffsetsManager.getScrollOffsetsV(index), 0x20 * sizeof(uint16));
		}
		mEmptyScrollOffsetsTexture.updateRaw(backend->getDevice(), backend->getAllocator(), nullptr, Vec2i(0x100, 1), VK_FORMAT_R16_SINT, renderParts.getScrollOffsetsManager().getScrollOffsetsH(0xff), 0x100 * sizeof(uint16));
	}
}

void VulkanRenderResources::clearAllCaches()
{
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr != backend)
	{
		destroyTexture(mMainPalette.mTexture);
		destroyTexture(mPatternCacheTexture);
		destroyTexture(mEmptyScrollOffsetsTexture);
		for (int index = 0; index < 4; ++index)
		{
			destroyTexture(mPlanePatternsTexture[index]);
			destroyTexture(mHScrollOffsetsTexture[index]);
			destroyTexture(mVScrollOffsetsTexture[index]);
		}
		for (auto& pair : mCustomPalettes)
			destroyTexture(pair.second.mTexture);
		for (auto& pair : mPaletteSpriteTextures)
			destroyTexture(pair.second.mTexture);
		for (auto& pair : mComponentSpriteTextures)
			destroyTexture(pair.second.mTexture);
	}

	for (int k = 0; k < 2; ++k)
		--mMainPalette.mChangeCounters[k];

	mCustomPalettes.clear();
	mPaletteSpriteTextures.clear();
	mComponentSpriteTextures.clear();
}

const vulkan::Texture& VulkanRenderResources::getPaletteTexture(const PaletteBase* primaryPalette, const PaletteBase* secondaryPalette)
{
	if (nullptr == primaryPalette && nullptr == secondaryPalette)
	{
		return getMainPaletteTexture();
	}

	const PaletteManager& paletteManager = RenderParts::instance().getPaletteManager();
	if (nullptr == primaryPalette)
		primaryPalette = &paletteManager.getMainPalette(0);
	if (nullptr == secondaryPalette)
		secondaryPalette = &paletteManager.getMainPalette(1);

	const uint64 combinedKey = primaryPalette->getKey() ^ (secondaryPalette->getKey() << 32) ^ (secondaryPalette->getKey() >> 32);
	PaletteData& data = mCustomPalettes[combinedKey];
	if (data.mBitmap.empty())
	{
		data.mBitmap.create(PALETTE_TEXTURE_SIZE.x, PALETTE_TEXTURE_SIZE.y);
	}
	updatePalette(data, *primaryPalette, *secondaryPalette);
	return data.mTexture;
}

const vulkan::Texture& VulkanRenderResources::getPlanePatternsTexture(int planeIndex) const
{
	RMX_ASSERT(planeIndex >= 0 && planeIndex < 4, "Invalid plane index " << planeIndex);
	return mPlanePatternsTexture[planeIndex];
}

const vulkan::Texture& VulkanRenderResources::getHScrollOffsetsTexture(int scrollOffsetsIndex) const
{
	if (scrollOffsetsIndex == 0xff)
		return mEmptyScrollOffsetsTexture;
	RMX_ASSERT(scrollOffsetsIndex >= 0 && scrollOffsetsIndex < 4, "Invalid scroll offsets index " << scrollOffsetsIndex);
	return mHScrollOffsetsTexture[scrollOffsetsIndex];
}

const vulkan::Texture& VulkanRenderResources::getVScrollOffsetsTexture(int scrollOffsetsIndex) const
{
	if (scrollOffsetsIndex == 0xff)
		return mEmptyScrollOffsetsTexture;
	RMX_ASSERT(scrollOffsetsIndex >= 0 && scrollOffsetsIndex < 4, "Invalid scroll offsets index " << scrollOffsetsIndex);
	return mVScrollOffsetsTexture[scrollOffsetsIndex];
}

const vulkan::Texture* VulkanRenderResources::getPaletteSpriteTexture(const SpriteCollection::Item& cacheItem, bool useUpscaledSprite)
{
	RMX_CHECK(!cacheItem.mUsesComponentSprite, "Sprite is not a palette sprite", return nullptr);
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr == backend)
		return nullptr;

	const PaletteSprite& sprite = *static_cast<PaletteSprite*>(cacheItem.mSprite);
	CachedTexture& texture = mPaletteSpriteTextures[useUpscaledSprite ? (cacheItem.mKey + 0x123456ull) : cacheItem.mKey];
	if (texture.mChangeCounter != cacheItem.mChangeCounter)
	{
		const PaletteBitmap& bitmap = useUpscaledSprite ? sprite.getUpscaledBitmap() : sprite.getBitmap();
		texture.mTexture.updateRaw(backend->getDevice(), backend->getAllocator(), nullptr, bitmap.getSize(), VK_FORMAT_R8_UINT, bitmap.getData(), bitmap.getWidth());
		texture.mChangeCounter = cacheItem.mChangeCounter;
	}
	return &texture.mTexture;
}

const vulkan::Texture* VulkanRenderResources::getComponentSpriteTexture(const SpriteCollection::Item& cacheItem)
{
	RMX_CHECK(cacheItem.mUsesComponentSprite, "Sprite is not a component sprite", return nullptr);
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr == backend)
		return nullptr;

	CachedTexture& texture = mComponentSpriteTextures[cacheItem.mKey];
	if (texture.mChangeCounter != cacheItem.mChangeCounter)
	{
		const Bitmap& bitmap = static_cast<ComponentSprite*>(cacheItem.mSprite)->getBitmap();
		texture.mTexture.updateFromBitmap(backend->getDevice(), backend->getAllocator(), nullptr, bitmap, VK_FORMAT_R8G8B8A8_UNORM);
		texture.mChangeCounter = cacheItem.mChangeCounter;
	}
	return &texture.mTexture;
}

void VulkanRenderResources::destroyTexture(vulkan::Texture& texture)
{
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr != backend && texture.isValid())
	{
		texture.destroy(backend->getDevice(), backend->getAllocator());
	}
}

bool VulkanRenderResources::updatePalette(PaletteData& data, const PaletteBase& primaryPalette, const PaletteBase& secondaryPalette)
{
	vulkan::RendererBackend* backend = mDrawerResources.getBackend();
	if (nullptr == backend)
		return false;

	data.mSecondsSinceLastUse = 0.0f;
	if (data.mBitmap.empty())
	{
		data.mBitmap.create(PALETTE_TEXTURE_SIZE.x, PALETTE_TEXTURE_SIZE.y);
	}
	if (!data.mTexture.isValid())
	{
		data.mChangeCounters[0] = (uint16)(primaryPalette.getChangeCounter() - 1);
		data.mChangeCounters[1] = (uint16)(secondaryPalette.getChangeCounter() - 1);
	}

	const bool primaryChanged = updatePaletteBitmap(primaryPalette, data.mBitmap, 0, data.mChangeCounters[0]);
	const bool secondaryChanged = updatePaletteBitmap(secondaryPalette, data.mBitmap, 2, data.mChangeCounters[1]);
	if (!primaryChanged && !secondaryChanged)
		return false;

	return data.mTexture.updateFromBitmap(backend->getDevice(), backend->getAllocator(), nullptr, data.mBitmap, VK_FORMAT_R8G8B8A8_UNORM);
}

bool VulkanRenderResources::updatePaletteBitmap(const PaletteBase& palette, Bitmap& bitmap, int offsetY, uint16& changeCounter)
{
	if (changeCounter == palette.getChangeCounter())
		return false;

	uint32* dst = bitmap.getPixelPointer(0, offsetY);
	palette.dumpColors(dst, palette.getSize());
	changeCounter = palette.getChangeCounter();
	return true;
}

#endif
