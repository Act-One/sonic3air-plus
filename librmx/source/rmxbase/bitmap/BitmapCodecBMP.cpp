/*
*	rmx Library
*	Copyright (C) 2008-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "rmxbase.h"


namespace rmx
{
	#pragma pack(1)
	struct BmpHeader
	{
		uint8  signature[2];
		uint32 fileSize;
		uint16 creator1;
		uint16 creator2;
		uint32 headerSize;
		uint32 dibHeaderSize;
		int32  width;
		int32  height;
		uint16 numPlanes;
		uint16 bpp;
		uint32 compression;
		uint32 dataSize;
		int32  resolutionX;
		int32  resolutionY;
		uint32 numColors;
		uint32 importantColors;
	};
	#pragma pack()

	inline uint32 swapRedBlue(uint32 color)
	{
		return (color & 0xff00ff00) | ((color & 0x00ff0000) >> 16) | ((color & 0x000000ff) << 16);
	}

	inline bool isLittleEndianHost()
	{
		const uint16 value = 0x1234;
		return (*(const uint8*)&value == 0x34);
	}

	inline uint16 swap16(uint16 value)
	{
		return (uint16)((value >> 8) | (value << 8));
	}

	inline uint32 swap32(uint32 value)
	{
		return ((value & 0x000000ffu) << 24) |
			   ((value & 0x0000ff00u) << 8) |
			   ((value & 0x00ff0000u) >> 8) |
			   ((value & 0xff000000u) >> 24);
	}

	inline void convertBmpHeaderEndian(BmpHeader& header)
	{
		if (isLittleEndianHost())
			return;

		header.fileSize = swap32(header.fileSize);
		header.creator1 = swap16(header.creator1);
		header.creator2 = swap16(header.creator2);
		header.headerSize = swap32(header.headerSize);
		header.dibHeaderSize = swap32(header.dibHeaderSize);
		header.width = (int32)swap32((uint32)header.width);
		header.height = (int32)swap32((uint32)header.height);
		header.numPlanes = swap16(header.numPlanes);
		header.bpp = swap16(header.bpp);
		header.compression = swap32(header.compression);
		header.dataSize = swap32(header.dataSize);
		header.resolutionX = (int32)swap32((uint32)header.resolutionX);
		header.resolutionY = (int32)swap32((uint32)header.resolutionY);
		header.numColors = swap32(header.numColors);
		header.importantColors = swap32(header.importantColors);
	}

	inline uint32 makePixelABGR(uint8 r, uint8 g, uint8 b, uint8 a)
	{
		return ((uint32)a << 24) | ((uint32)b << 16) | ((uint32)g << 8) | (uint32)r;
	}

	#define RETURN(errcode) \
	{ \
		outResult.mError = errcode; \
		return (errcode == Bitmap::LoadResult::Error::OK); \
	}



	bool BitmapCodecBMP::canDecode(const String& format) const
	{
		return (format == "bmp");
	}

	bool BitmapCodecBMP::canEncode(const String& format) const
	{
		return (format == "bmp");
	}

	bool BitmapCodecBMP::decode(Bitmap& bitmap, InputStream& stream, Bitmap::LoadResult& outResult)
	{
		// Read header
		BmpHeader header;
		stream >> header;
		convertBmpHeaderEndian(header);
		if (memcmp(header.signature, "BM", 2) != 0)
			RETURN(Bitmap::LoadResult::Error::INVALID_FILE);

		// Size
		const int width = header.width;
		const int height = header.height;
		const int bitdepth = header.bpp;
		const int stride = (width * bitdepth + 31) / 32 * 4;

		// Skip unrecognized parts of the header
		if (header.dibHeaderSize > 0x28)
		{
			stream.skip(header.dibHeaderSize - 0x28);
		}

		// Load palette
		int palSize = 0;
		if (bitdepth == 1 || bitdepth == 4 || bitdepth == 8)
		{
			palSize = (header.numColors != 0) ? header.numColors : (1 << bitdepth);
		}

		// Read and convert palette
		uint32 palette[256];
		uint8 paletteBytes[256 * 4];
		stream.read(paletteBytes, palSize * 4);
		for (int i = 0; i < palSize; ++i)
		{
			const uint8* entry = &paletteBytes[i * 4];
			palette[i] = makePixelABGR(entry[2], entry[1], entry[0], 0xff);
		}

		// Skip unrecognized parts of the header
		if (header.headerSize > stream.getPosition())
		{
			stream.skip(header.headerSize - stream.getPosition());
		}

		// Create data buffer
		bitmap.create(width, height);
		uint32* data = bitmap.getData();
		MemInputStream mstream(stream);
		const uint8* buffer = mstream.getCursor();

		// Load image data
		for (int y = 0; y < height; ++y)
		{
			uint32* dataPtr = &data[(height-y-1)*width];
			switch (bitdepth)
			{
				case 1:
					for (int x = 0; x < width; ++x)
						dataPtr[x] = palette[(buffer[x/8] >> (7 - (x & 0x07))) & 0x01];
					break;

				case 4:
					for (int x = 0; x < width; ++x)
						dataPtr[x] = palette[(buffer[x/2] >> ((1-x%2)*4)) & 0x0f];
					break;

				case 8:
					for (int x = 0; x < width; ++x)
						dataPtr[x] = palette[buffer[x]];
					break;

				case 24:
					for (int x = 0; x < width; ++x)
						dataPtr[x] = ((uint32)buffer[x*3] << 16) | ((uint32)buffer[x*3+1] << 8) | ((uint32)buffer[x*3+2]) | 0xff000000;
					break;

				case 32:
					for (int x = 0; x < width; ++x)
					{
						const uint8* pixel = &buffer[x * 4];
						dataPtr[x] = makePixelABGR(pixel[2], pixel[1], pixel[0], pixel[3]);
					}
					break;
			}
			buffer += stride;
		}

		// 32bit-BMP with or without alpha channel?
		if (bitdepth == 32)
		{
			bool noAlpha = true;
			const int size = width * height;
			for (int i = 0; i < size; ++i)
			{
				if (data[i] >= 0x01000000)
				{
					noAlpha = false;
					break;
				}
			}
			if (noAlpha)
			{
				for (int i = 0; i < size; ++i)
					data[i] |= 0xff000000;
			}
		}

		RETURN(Bitmap::LoadResult::Error::OK);
	}

	bool BitmapCodecBMP::encode(const Bitmap& bitmap, OutputStream& stream)
	{
		const int width = bitmap.getWidth();
		const int height = bitmap.getHeight();
		const int dataSize = width * height * 4;
		const int headerSize = sizeof(BmpHeader);

		// Header
		BmpHeader header;
		memset(&header, 0, sizeof(BmpHeader));
		memcpy(header.signature, "BM", 2);
		header.fileSize = headerSize + dataSize;
		header.headerSize = headerSize;
		header.dibHeaderSize = 40;
		header.width = width;
		header.height = height;
		header.numPlanes = 1;
		header.bpp = 32;
		header.dataSize = dataSize;
		header.resolutionX = 0xb40;
		header.resolutionY = 0xb40;
		BmpHeader fileHeader = header;
		convertBmpHeaderEndian(fileHeader);
		stream.write(&fileHeader, sizeof(fileHeader));

		uint8* output = new uint8[width * 4];

		for (int y = 0; y < height; ++y)
		{
			const uint32* src = bitmap.getPixelPointer(0, height-y-1);
			for (int x = 0; x < width; ++x)
			{
				output[x * 4] = (uint8)((src[x] >> 16) & 0xff);
				output[x * 4 + 1] = (uint8)((src[x] >> 8) & 0xff);
				output[x * 4 + 2] = (uint8)(src[x] & 0xff);
				output[x * 4 + 3] = (uint8)((src[x] >> 24) & 0xff);
			}
			stream.write(output, width * 4);
		}

		delete[] output;
		return true;
	}

	#undef RETURN
}
