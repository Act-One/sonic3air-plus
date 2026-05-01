/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)

#include "oxygen/rendering/sprite/PaletteSprite.h"
#include "oxygen/resources/SpriteCollection.h"
#include "oxygen/rendering/vulkan/Texture.h"

class PaletteBase;
class RenderParts;
class VulkanDrawerResources;

class VulkanRenderResources
{
public:
	struct PaletteData
	{
		Bitmap mBitmap;
		vulkan::Texture mTexture;
		uint16 mChangeCounters[2] = { 0, 0 };
		float mSecondsSinceLastUse = 0.0f;
	};

	struct CachedTexture
	{
		vulkan::Texture mTexture;
		uint16 mChangeCounter = 0xffff;
	};

public:
	VulkanRenderResources(RenderParts& renderParts, VulkanDrawerResources& drawerResources);
	~VulkanRenderResources();

	void initialize();
	void refresh();
	void clearAllCaches();

	inline RenderParts& getRenderParts() const  { return mRenderParts; }
	inline const vulkan::Texture& getMainPaletteTexture() const  { return mMainPalette.mTexture; }
	const vulkan::Texture& getPaletteTexture(const PaletteBase* primaryPalette, const PaletteBase* secondaryPalette);
	inline const vulkan::Texture& getPatternCacheTexture() const  { return mPatternCacheTexture; }
	const vulkan::Texture& getPlanePatternsTexture(int planeIndex) const;
	const vulkan::Texture& getHScrollOffsetsTexture(int scrollOffsetsIndex) const;
	const vulkan::Texture& getVScrollOffsetsTexture(int scrollOffsetsIndex) const;
	const vulkan::Texture* getPaletteSpriteTexture(const SpriteCollection::Item& cacheItem, bool useUpscaledSprite);
	const vulkan::Texture* getComponentSpriteTexture(const SpriteCollection::Item& cacheItem);

private:
	void destroyTexture(vulkan::Texture& texture);
	bool updatePalette(PaletteData& data, const PaletteBase& primaryPalette, const PaletteBase& secondaryPalette);
	bool updatePaletteBitmap(const PaletteBase& palette, Bitmap& bitmap, int offsetY, uint16& changeCounter);

private:
	RenderParts& mRenderParts;
	VulkanDrawerResources& mDrawerResources;
	PaletteData mMainPalette;
	PaletteBitmap mPatternCacheBitmap;
	vulkan::Texture mPatternCacheTexture;
	vulkan::Texture mPlanePatternsTexture[4];
	vulkan::Texture mHScrollOffsetsTexture[4];
	vulkan::Texture mVScrollOffsetsTexture[4];
	vulkan::Texture mEmptyScrollOffsetsTexture;
	std::unordered_map<uint64, PaletteData> mCustomPalettes;
	std::unordered_map<uint64, CachedTexture> mPaletteSpriteTextures;
	std::unordered_map<uint64, CachedTexture> mComponentSpriteTextures;
};

#endif
