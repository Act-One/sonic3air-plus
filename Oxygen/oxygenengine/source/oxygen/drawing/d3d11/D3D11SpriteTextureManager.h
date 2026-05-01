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
#include "oxygen/resources/SpriteCollection.h"


class D3D11SpriteTextureManager : public SingleInstance<D3D11SpriteTextureManager>
{
public:
	d3d11::TextureResource* getPaletteSpriteTexture(const SpriteCollection::Item& cacheItem, bool useUpscaledSprite, D3D11DrawerResources& drawerResources);
	d3d11::TextureResource* getComponentSpriteTexture(const SpriteCollection::Item& cacheItem, D3D11DrawerResources& drawerResources);

private:
	template<typename T> struct ChangeCounted
	{
		T mTexture;
		uint32 mChangeCounter = 0xffffffff;
	};

	std::unordered_map<uint64, ChangeCounted<d3d11::TextureResource>> mPaletteSpriteTextures;
	std::unordered_map<uint64, ChangeCounted<d3d11::TextureResource>> mComponentSpriteTextures;
};

#endif
