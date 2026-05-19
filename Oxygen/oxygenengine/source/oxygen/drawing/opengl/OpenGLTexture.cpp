/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#ifdef RMX_WITH_OPENGL_SUPPORT

#include "oxygen/drawing/opengl/OpenGLTexture.h"

#include <vector>


namespace
{
	const void* getBitmapUploadDataRGBA(const Bitmap& bitmap, std::vector<uint8>& scratch)
	{
	#if RMX_IS_BIG_ENDIAN
		const size_t numPixels = (size_t)bitmap.getWidth() * (size_t)bitmap.getHeight();
		scratch.resize(numPixels * 4);

		const uint8* src = reinterpret_cast<const uint8*>(bitmap.getData());
		for (size_t i = 0; i < numPixels; ++i)
		{
			const uint8* pixel = src + i * 4;
			scratch[i * 4 + 0] = pixel[ABGR32_BYTE_R];
			scratch[i * 4 + 1] = pixel[ABGR32_BYTE_G];
			scratch[i * 4 + 2] = pixel[ABGR32_BYTE_B];
			scratch[i * 4 + 3] = pixel[ABGR32_BYTE_A];
		}
		return scratch.data();
	#else
		(void)scratch;
		return bitmap.getData();
	#endif
	}
}


OpenGLTexture::~OpenGLTexture()
{
	if (mTextureHandle != 0)
	{
		glDeleteTextures(1, &mTextureHandle);
	}
}

void OpenGLTexture::loadBitmap(const Bitmap& bitmap)
{
	if (mTextureHandle == 0)
	{
		glGenTextures(1, &mTextureHandle);
	}

	if (!bitmap.isEmpty())
	{
		glBindTexture(GL_TEXTURE_2D, mTextureHandle);
		std::vector<uint8> scratch;
		glTexImage2D(GL_TEXTURE_2D, 0, rmx::OpenGLHelper::FORMAT_RGBA, bitmap.getWidth(), bitmap.getHeight(), 0, GL_RGBA, GL_UNSIGNED_BYTE, getBitmapUploadDataRGBA(bitmap, scratch));

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	mSize = bitmap.getSize();
}

void OpenGLTexture::setup(Vec2i size, GLint format)
{
	if (mTextureHandle == 0)
	{
		glGenTextures(1, &mTextureHandle);
	}

	glBindTexture(GL_TEXTURE_2D, mTextureHandle);
	glTexImage2D(GL_TEXTURE_2D, 0, format, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	mSize = size;

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

#endif
