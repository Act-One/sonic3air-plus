/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/drawing/gx/GXDrawerTexture.h"

#if defined(PLATFORM_WII)
#include <malloc.h>
#include <cstring>
#endif


GXDrawerTexture::GXDrawerTexture(DrawerTexture& owner) :
	DrawerTextureImplementation(owner)
{
}

GXDrawerTexture::~GXDrawerTexture()
{
#if defined(PLATFORM_WII)
	releaseTextureData();
#endif
}

void GXDrawerTexture::updateFromBitmap(const Bitmap& bitmap)
{
#if defined(PLATFORM_WII)
	releaseTextureData();

	if (bitmap.empty())
		return;

	const int sourceWidth = bitmap.getWidth();
	const int sourceHeight = bitmap.getHeight();
	int width = sourceWidth;
	int height = sourceHeight;
	if (width > 1024 || height > 1024)
	{
		if (width >= height)
		{
			width = 1024;
			height = std::max(1, (sourceHeight * width + sourceWidth / 2) / sourceWidth);
		}
		else
		{
			height = 1024;
			width = std::max(1, (sourceWidth * height + sourceHeight / 2) / sourceHeight);
		}
	}
	width = clamp(width, 1, 1024);
	height = clamp(height, 1, 1024);
	const uint16 paddedWidth = (uint16)((width + 3) & ~3);
	const uint16 paddedHeight = (uint16)((height + 3) & ~3);
	const uint32 textureSize = GX_GetTexBufferSize(paddedWidth, paddedHeight, GX_TF_RGB5A3, GX_FALSE, 0);
	mTextureData = memalign(32, textureSize);
	RMX_CHECK(nullptr != mTextureData, "Failed to allocate GX texture data", return);
	mTextureDataSize = textureSize;
	mUploadedSize.set(width, height);

	uint16* dst = static_cast<uint16*>(mTextureData);
	for (int blockY = 0; blockY < paddedHeight; blockY += 4)
	{
		for (int blockX = 0; blockX < paddedWidth; blockX += 4)
		{
			for (int y = 0; y < 4; ++y)
			{
				for (int x = 0; x < 4; ++x)
				{
					const int sx = blockX + x;
					const int sy = blockY + y;
					if (sx < width && sy < height)
					{
						const int sourceX = (sx * sourceWidth) / width;
						const int sourceY = (sy * sourceHeight) / height;
						const uint32* color = bitmap.getPixelPointer(sourceX, sourceY);
						*dst++ = packRGB5A3(color);
					}
					else
					{
						*dst++ = packRGB5A3(nullptr);
					}
				}
			}
		}
	}

	DCFlushRange(mTextureData, mTextureDataSize);
	GX_InitTexObj(&mTexObj, mTextureData, (uint16)width, (uint16)height, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjFilterMode(&mTexObj, GX_NEAR, GX_NEAR);
#else
	(void)bitmap;
#endif
}

void GXDrawerTexture::setupAsRenderTarget(const Vec2i& size)
{
	// GX render-to-texture is handled explicitly by the renderer when needed. Keep a CPU bitmap so
	// existing texture lifetime code has valid dimensions and screenshot reads do not crash.
	mOwner.accessBitmap().create(size.x, size.y);
}

void GXDrawerTexture::writeContentToBitmap(Bitmap& outBitmap)
{
	outBitmap = mOwner.accessBitmap();
}

void GXDrawerTexture::refreshImplementation(bool setupRenderTarget, const Vec2i& size)
{
	if (setupRenderTarget)
	{
		setupAsRenderTarget(size);
	}
	else if (!mOwner.getBitmap().empty())
	{
		updateFromBitmap(mOwner.getBitmap());
	}
}

#if defined(PLATFORM_WII)
bool GXDrawerTexture::load(SamplingMode samplingMode, TextureWrapMode wrapMode)
{
	if (nullptr == mTextureData)
	{
		updateFromBitmap(mOwner.getBitmap());
	}
	if (nullptr == mTextureData)
		return false;

	const uint8 gxWrap = (wrapMode == TextureWrapMode::REPEAT) ? GX_REPEAT : GX_CLAMP;
	const uint8 gxFilter = (samplingMode == SamplingMode::BILINEAR) ? GX_LINEAR : GX_NEAR;
	GX_InitTexObjWrapMode(&mTexObj, gxWrap, gxWrap);
	GX_InitTexObjFilterMode(&mTexObj, gxFilter, gxFilter);
	GX_LoadTexObj(&mTexObj, GX_TEXMAP0);
	return true;
}

void GXDrawerTexture::releaseTextureData()
{
	if (nullptr != mTextureData)
	{
		free(mTextureData);
		mTextureData = nullptr;
		mTextureDataSize = 0;
		mUploadedSize.clear();
	}
}

uint16 GXDrawerTexture::packRGB5A3(const uint32* colorABGR)
{
	if (nullptr == colorABGR)
		return 0;

	const uint8* colorBytes = reinterpret_cast<const uint8*>(colorABGR);
	const uint8 r = colorBytes[ABGR32_BYTE_R];
	const uint8 g = colorBytes[ABGR32_BYTE_G];
	const uint8 b = colorBytes[ABGR32_BYTE_B];
	const uint8 a = colorBytes[ABGR32_BYTE_A];
	if (a < 224)
	{
		return (uint16)(((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
	}
	return (uint16)(0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}
#endif
