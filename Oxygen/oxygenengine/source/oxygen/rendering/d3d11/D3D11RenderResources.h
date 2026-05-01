/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_WINDOWS)

#include "oxygen/drawing/d3d11/D3D11DrawerResources.h"

class PaletteBase;
class RenderParts;


class D3D11RenderResources
{
public:
	D3D11RenderResources(RenderParts& renderParts, D3D11DrawerResources& drawerResources);

	void initialize();
	void refresh();
	void clearAllCaches();

	inline RenderParts& getRenderParts() const  { return mRenderParts; }
	inline const d3d11::TextureResource& getMainPaletteTexture() const  { return mMainPalette.mTexture; }
	const d3d11::TextureResource& getPaletteTexture(const PaletteBase* primaryPalette, const PaletteBase* secondaryPalette);
	inline const d3d11::TextureResource& getPatternCacheTexture() const  { return mPatternCacheTexture; }
	const d3d11::TextureResource& getPlanePatternsTexture(int planeIndex) const;
	const d3d11::TextureResource& getHScrollOffsetsTexture(int scrollOffsetsIndex) const;
	const d3d11::TextureResource& getVScrollOffsetsTexture(int scrollOffsetsIndex) const;

private:
	RenderParts& mRenderParts;
	D3D11DrawerResources& mDrawerResources;
	D3D11DrawerResources::PaletteData mMainPalette;
	PaletteBitmap mPatternCacheBitmap;
	d3d11::TextureResource mPatternCacheTexture;
	d3d11::TextureResource mPlanePatternsTexture[4];
	d3d11::TextureResource mHScrollOffsetsTexture[4];
	d3d11::TextureResource mVScrollOffsetsTexture[4];
	d3d11::TextureResource mEmptyScrollOffsetsTexture;
};

#endif
