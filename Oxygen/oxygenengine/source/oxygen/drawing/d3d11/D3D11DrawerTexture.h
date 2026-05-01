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

#include "oxygen/drawing/DrawerTexture.h"
#include "oxygen/drawing/DrawCommand.h"
#include "oxygen/drawing/d3d11/D3D11DrawerResources.h"


class D3D11DrawerTexture final : public DrawerTextureImplementation
{
public:
	inline explicit D3D11DrawerTexture(DrawerTexture& owner) : DrawerTextureImplementation(owner) {}

	void updateFromBitmap(const Bitmap& bitmap) override;
	void setupAsRenderTarget(const Vec2i& size) override;
	void writeContentToBitmap(Bitmap& outBitmap) override;
	void refreshImplementation(bool setupRenderTarget, const Vec2i& size) override;

public:
	SamplingMode mSamplingMode = SamplingMode::POINT;
	TextureWrapMode mWrapMode = TextureWrapMode::CLAMP;
	d3d11::TextureResource mTextureResource;
};

#endif
