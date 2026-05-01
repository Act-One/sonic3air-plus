/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_WINDOWS)

#include "oxygen/rendering/d3d11/D3D11RenderResources.h"
#include "oxygen/rendering/parts/RenderParts.h"


D3D11RenderResources::D3D11RenderResources(RenderParts& renderParts, D3D11DrawerResources& drawerResources) :
	mRenderParts(renderParts),
	mDrawerResources(drawerResources)
{
	clearAllCaches();
}

void D3D11RenderResources::initialize()
{
	mMainPalette.mBitmap.create(mDrawerResources.getPaletteTextureSize());
	mPatternCacheBitmap.create(0x40, 0x800);

	for (int index = 0; index < 4; ++index)
	{
		mDrawerResources.updateTexture(mPlanePatternsTexture[index], Vec2i(PlaneManager::MAX_PLANE_PATTERNS, 1), DXGI_FORMAT_R16_UINT, nullptr, 0, false);
		mDrawerResources.updateTexture(mHScrollOffsetsTexture[index], Vec2i(0x100, 1), DXGI_FORMAT_R16_SINT, nullptr, 0, false);
		mDrawerResources.updateTexture(mVScrollOffsetsTexture[index], Vec2i(0x20, 1), DXGI_FORMAT_R16_SINT, nullptr, 0, false);
	}
	mDrawerResources.updateTexture(mEmptyScrollOffsetsTexture, Vec2i(0x100, 1), DXGI_FORMAT_R16_SINT, RenderParts::instance().getScrollOffsetsManager().getScrollOffsetsH(0xff), 0x100 * sizeof(uint16), false);
}

void D3D11RenderResources::refresh()
{
	RenderParts& renderParts = RenderParts::instance();
	PaletteManager& paletteManager = renderParts.getPaletteManager();
	mDrawerResources.updatePalette(mMainPalette, paletteManager.getMainPalette(0), paletteManager.getMainPalette(1));

	// Pattern cache
	{
		const PatternManager::CacheItem* patternCache = renderParts.getPatternManager().getPatternCache();
		for (int patternIndex = 0; patternIndex < 0x800; ++patternIndex)
		{
			const uint8* src = patternCache[patternIndex].mFlipVariation[0].mPixels;
			uint8* dst = &mPatternCacheBitmap[patternIndex * 0x40];
			std::memcpy(dst, src, 0x40);
		}
		mDrawerResources.updateTexture(mPatternCacheTexture, mPatternCacheBitmap.getSize(), DXGI_FORMAT_R8_UINT, mPatternCacheBitmap.getData(), mPatternCacheBitmap.getWidth(), false);
	}

	// Plane patterns
	{
		const PlaneManager& planeManager = renderParts.getPlaneManager();
		for (int index = 0; index < 4; ++index)
		{
			if (!planeManager.isPlaneUsed(index))
				continue;
			mDrawerResources.updateTexture(mPlanePatternsTexture[index], Vec2i(PlaneManager::MAX_PLANE_PATTERNS, 1), DXGI_FORMAT_R16_UINT, planeManager.getPlanePatternsBuffer((uint8)index), PlaneManager::MAX_PLANE_PATTERNS * sizeof(uint16), false);
		}
	}

	// Scroll offsets
	{
		const ScrollOffsetsManager& scrollOffsetsManager = renderParts.getScrollOffsetsManager();
		for (int index = 0; index < 4; ++index)
		{
			mDrawerResources.updateTexture(mHScrollOffsetsTexture[index], Vec2i(0x100, 1), DXGI_FORMAT_R16_SINT, scrollOffsetsManager.getScrollOffsetsH(index), 0x100 * sizeof(uint16), false);
			mDrawerResources.updateTexture(mVScrollOffsetsTexture[index], Vec2i(0x20, 1), DXGI_FORMAT_R16_SINT, scrollOffsetsManager.getScrollOffsetsV(index), 0x20 * sizeof(uint16), false);
		}
	}
}

void D3D11RenderResources::clearAllCaches()
{
	for (int k = 0; k < 2; ++k)
		--mMainPalette.mChangeCounters[k];
}

const d3d11::TextureResource& D3D11RenderResources::getPaletteTexture(const PaletteBase* primaryPalette, const PaletteBase* secondaryPalette)
{
	if (nullptr == primaryPalette && nullptr == secondaryPalette)
	{
		return getMainPaletteTexture();
	}
	else
	{
		const PaletteManager& paletteManager = RenderParts::instance().getPaletteManager();
		if (nullptr == primaryPalette)
			primaryPalette = &paletteManager.getMainPalette(0);
		if (nullptr == secondaryPalette)
			secondaryPalette = &paletteManager.getMainPalette(1);
		return mDrawerResources.getCustomPaletteTexture(*primaryPalette, *secondaryPalette);
	}
}

const d3d11::TextureResource& D3D11RenderResources::getPlanePatternsTexture(int planeIndex) const
{
	RMX_ASSERT(planeIndex >= 0 && planeIndex < 4, "Invalid plane index " << planeIndex);
	return mPlanePatternsTexture[planeIndex];
}

const d3d11::TextureResource& D3D11RenderResources::getHScrollOffsetsTexture(int scrollOffsetsIndex) const
{
	if (scrollOffsetsIndex == 0xff)
		return mEmptyScrollOffsetsTexture;
	RMX_ASSERT(scrollOffsetsIndex >= 0 && scrollOffsetsIndex < 4, "Invalid scroll offsets index " << scrollOffsetsIndex);
	return mHScrollOffsetsTexture[scrollOffsetsIndex];
}

const d3d11::TextureResource& D3D11RenderResources::getVScrollOffsetsTexture(int scrollOffsetsIndex) const
{
	if (scrollOffsetsIndex == 0xff)
		return mEmptyScrollOffsetsTexture;
	RMX_ASSERT(scrollOffsetsIndex >= 0 && scrollOffsetsIndex < 4, "Invalid scroll offsets index " << scrollOffsetsIndex);
	return mVScrollOffsetsTexture[scrollOffsetsIndex];
}

#endif
