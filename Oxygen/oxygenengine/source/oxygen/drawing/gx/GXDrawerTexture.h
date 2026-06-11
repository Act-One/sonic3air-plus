/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen/drawing/DrawerTexture.h"

#if defined(PLATFORM_WII)
#include <gccore.h>
#endif


class GXDrawerTexture final : public DrawerTextureImplementation
{
public:
	explicit GXDrawerTexture(DrawerTexture& owner);
	~GXDrawerTexture();

	void updateFromBitmap(const Bitmap& bitmap) override;
	void setupAsRenderTarget(const Vec2i& size) override;
	void writeContentToBitmap(Bitmap& outBitmap) override;
	void refreshImplementation(bool setupRenderTarget, const Vec2i& size) override;

#if defined(PLATFORM_WII)
	bool load(SamplingMode samplingMode, TextureWrapMode wrapMode);
#endif

private:
#if defined(PLATFORM_WII)
	void releaseTextureData();
	static uint16 packRGB5A3(const uint32* colorABGR);

private:
	void* mTextureData = nullptr;
	uint32 mTextureDataSize = 0;
	Vec2i mUploadedSize;
	GXTexObj mTexObj = {};
#endif
};
