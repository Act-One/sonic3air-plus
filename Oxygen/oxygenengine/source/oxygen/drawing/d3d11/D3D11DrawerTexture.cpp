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

#include "oxygen/drawing/d3d11/D3D11DrawerTexture.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/drawing/d3d11/D3D11Drawer.h"


namespace
{
	D3D11DrawerResources& getDrawerResources()
	{
		DrawerInterface* drawer = EngineMain::instance().getDrawer().getActiveDrawer();
		RMX_ASSERT(nullptr != drawer, "Expected an active drawer");
		RMX_ASSERT(drawer->getType() == Drawer::Type::DIRECT3D11, "Expected the Direct3D 11 drawer");
		return static_cast<D3D11Drawer*>(drawer)->getResources();
	}
}


void D3D11DrawerTexture::updateFromBitmap(const Bitmap& bitmap)
{
	getDrawerResources().updateTexture(mTextureResource, bitmap.getSize(), DXGI_FORMAT_R8G8B8A8_UNORM, bitmap.getData(), bitmap.getWidth() * sizeof(uint32), false);
}

void D3D11DrawerTexture::setupAsRenderTarget(const Vec2i& size)
{
	getDrawerResources().updateTexture(mTextureResource, size, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, 0, true);
}

void D3D11DrawerTexture::writeContentToBitmap(Bitmap& outBitmap)
{
	getDrawerResources().readTextureToBitmap(mTextureResource, outBitmap);
}

void D3D11DrawerTexture::refreshImplementation(bool setupRenderTarget, const Vec2i& size)
{
	if (setupRenderTarget)
	{
		setupAsRenderTarget(size);
	}
	else if (!mOwner.accessBitmap().empty())
	{
		updateFromBitmap(mOwner.accessBitmap());
	}
}

#endif
