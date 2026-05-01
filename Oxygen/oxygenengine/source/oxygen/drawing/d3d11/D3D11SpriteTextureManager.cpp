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

#include "oxygen/drawing/d3d11/D3D11SpriteTextureManager.h"


d3d11::TextureResource* D3D11SpriteTextureManager::getPaletteSpriteTexture(const SpriteCollection::Item& cacheItem, bool useUpscaledSprite, D3D11DrawerResources& drawerResources)
{
	RMX_CHECK(!cacheItem.mUsesComponentSprite, "Sprite is not a palette sprite", RMX_REACT_THROW);
	const PaletteSprite& sprite = *static_cast<PaletteSprite*>(cacheItem.mSprite);
	ChangeCounted<d3d11::TextureResource>& texture = mPaletteSpriteTextures[useUpscaledSprite ? (cacheItem.mKey + 0x123456) : cacheItem.mKey];

	if (texture.mChangeCounter != cacheItem.mChangeCounter)
	{
		const PaletteBitmap& bitmap = useUpscaledSprite ? sprite.getUpscaledBitmap() : sprite.getBitmap();
		drawerResources.updateTexture(texture.mTexture, bitmap.getSize(), DXGI_FORMAT_R8_UINT, bitmap.getData(), bitmap.getWidth(), false);
		texture.mChangeCounter = cacheItem.mChangeCounter;
	}
	return &texture.mTexture;
}

d3d11::TextureResource* D3D11SpriteTextureManager::getComponentSpriteTexture(const SpriteCollection::Item& cacheItem, D3D11DrawerResources& drawerResources)
{
	RMX_CHECK(cacheItem.mUsesComponentSprite, "Sprite is not a component sprite", RMX_REACT_THROW);
	ChangeCounted<d3d11::TextureResource>& texture = mComponentSpriteTextures[cacheItem.mKey];

	if (texture.mChangeCounter != cacheItem.mChangeCounter)
	{
		const Bitmap& bitmap = static_cast<ComponentSprite*>(cacheItem.mSprite)->getBitmap();
		drawerResources.updateTexture(texture.mTexture, bitmap.getSize(), DXGI_FORMAT_R8G8B8A8_UNORM, bitmap.getData(), bitmap.getWidth() * sizeof(uint32), false);
		texture.mChangeCounter = cacheItem.mChangeCounter;
	}
	return &texture.mTexture;
}

#endif
