/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen/drawing/DrawerTexture.h"

class PaletteBase;
class RenderParts;

namespace renderitems
{
	struct ComponentSpriteInfo;
	struct PaletteSpriteInfo;
	struct VdpSpriteInfo;
}


class GX2RenderResources
{
public:
	explicit GX2RenderResources(RenderParts& renderParts);

	void initialize();
	void refresh();
	void clearAllCaches();

	inline RenderParts& getRenderParts() const  { return mRenderParts; }
	DrawerTexture& getMainPaletteTexture();
	DrawerTexture& getPatternCacheTexture();
	DrawerTexture& getPlanePatternsTexture(int planeIndex);
	DrawerTexture& getHScrollOffsetsTexture(int scrollOffsetsIndex);
	DrawerTexture& getVScrollOffsetsTexture(int scrollOffsetsIndex);
	DrawerTexture& getPlaneDataTexture();

	DrawerTexture& getVdpSpriteTexture(const renderitems::VdpSpriteInfo& spriteInfo);
	DrawerTexture& getComponentSpriteTexture(const renderitems::ComponentSpriteInfo& spriteInfo);
	DrawerTexture& getPaletteTexture(const PaletteBase* primaryPalette, const PaletteBase* secondaryPalette);
	DrawerTexture& getPaletteSpriteDataTexture(const renderitems::PaletteSpriteInfo& spriteInfo, const PaletteBase& primaryPalette, const PaletteBase& secondaryPalette);
	DrawerTexture& getPaletteSpriteTexture(const renderitems::PaletteSpriteInfo& spriteInfo, const PaletteBase& palette);

private:
	struct CachedSpriteTexture
	{
		std::unique_ptr<DrawerTexture> mTexture;
		uint32 mSpriteChangeCounter = 0xffffffff;
		uint16 mPaletteChangeCounter = 0xffff;
		uint16 mSecondaryPaletteChangeCounter = 0xffff;
		Vec2i mSourceSize;
	};

	struct CachedPaletteTexture
	{
		std::unique_ptr<DrawerTexture> mTexture;
		Bitmap mBitmap;
		uint16 mChangeCounters[2] = { 0xffff, 0xffff };
	};

private:
	void refreshMainPaletteTexture();
	void refreshPatternCacheTexture();
	void refreshPlanePatternTextures();
	void refreshScrollOffsetTextures();
	void refreshPlaneDataTexture();

private:
	RenderParts& mRenderParts;
	Bitmap mMainPaletteBitmap;
	Bitmap mPatternCacheBitmap;
	Bitmap mPlanePatternBitmap[4];
	Bitmap mHScrollOffsetBitmap[5];
	Bitmap mVScrollOffsetBitmap[5];
	Bitmap mPlaneDataBitmap;
	DrawerTexture mMainPaletteTexture;
	DrawerTexture mPatternCacheTexture;
	DrawerTexture mPlanePatternTextures[4];
	DrawerTexture mHScrollOffsetTextures[5];
	DrawerTexture mVScrollOffsetTextures[5];
	DrawerTexture mPlaneDataTexture;
	uint16 mMainPaletteChangeCounters[2] = { 0xffff, 0xffff };
	Vec2i mPlanePatternPlayfieldSize;
	uint32 mPlanePatternChangeCounters[4] = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };
	bool mPatternCacheInitialized = false;
	bool mPlanePatternBitmapInitialized[4] = { false, false, false, false };
	bool mPlanePatternBitmapUsed[4] = { false, false, false, false };
	bool mPlanePatternSourceCacheInitialized[4] = { false, false, false, false };
	bool mScrollOffsetBitmapInitialized[5] = { false, false, false, false, false };
	bool mPlaneDataInitialized = false;
	bool mPlaneDataPatternFullDirty = true;
	bool mPlaneDataPatternDirty = true;
	bool mPlaneDataPaletteDirty = true;
	bool mPlaneDataPlaneDirty[4] = { true, true, true, true };
	bool mPlaneDataScrollDirty[5] = { true, true, true, true, true };
	std::vector<uint16> mChangedPatternIndices;
	std::vector<uint16> mPlanePatternSourceCache[4];

	std::unordered_map<uint64, CachedSpriteTexture> mVdpSpriteTextures;
	std::unordered_map<uint64, CachedSpriteTexture> mComponentSpriteTextures;
	std::unordered_map<uint64, CachedSpriteTexture> mPaletteSpriteIndexTextures;
	std::unordered_map<uint64, CachedSpriteTexture> mPaletteSpriteTextures;
	std::unordered_map<uint64, CachedPaletteTexture> mPaletteTextures;
};
