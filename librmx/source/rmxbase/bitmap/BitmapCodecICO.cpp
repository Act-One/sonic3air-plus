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
	struct IcoHeader
	{
		uint16 idReserved;
		uint16 idType;
		uint16 idCount;
	};

	struct IconDirEntry
	{
		uint8  width;
		uint8  height;
		uint8  colorCount;
		uint8  reserved;
		uint16 planes;
		uint16 bitCount;
		uint32 bytesInRes;
		uint32 imageOffset;
	};
	#pragma pack()

	inline uint16 readLittleEndian16(const uint8* data)
	{
		return (uint16)((uint16)data[0] | ((uint16)data[1] << 8));
	}

	inline uint32 readLittleEndian32(const uint8* data)
	{
		return (uint32)data[0] | ((uint32)data[1] << 8) | ((uint32)data[2] << 16) | ((uint32)data[3] << 24);
	}

	inline void writeLittleEndian32(uint8* data, uint32 value)
	{
		data[0] = (uint8)value;
		data[1] = (uint8)(value >> 8);
		data[2] = (uint8)(value >> 16);
		data[3] = (uint8)(value >> 24);
	}

	#define RETURN(errcode) \
	{ \
		outResult.mError = errcode; \
		return (errcode == Bitmap::LoadResult::Error::OK); \
	}



	bool BitmapCodecICO::canDecode(const String& format) const
	{
		return (format == "ico");
	}

	bool BitmapCodecICO::canEncode(const String& format) const
	{
		return false;
	}

	bool BitmapCodecICO::decode(Bitmap& bitmap, InputStream& stream, Bitmap::LoadResult& outResult)
	{
		MemInputStream mstream(stream);
		const uint8* buffer = mstream.getCursor();
		const size_t bufferSize = mstream.getRemaining();

		if (bufferSize < sizeof(IcoHeader))
			RETURN(Bitmap::LoadResult::Error::INVALID_FILE);
		if (readLittleEndian16(buffer) != 0 || readLittleEndian16(buffer + 2) != 1)
			RETURN(Bitmap::LoadResult::Error::INVALID_FILE);

		const int imageCount = readLittleEndian16(buffer + 4);
		if (imageCount <= 0 || bufferSize < sizeof(IcoHeader) + (size_t)imageCount * sizeof(IconDirEntry))
			RETURN(Bitmap::LoadResult::Error::INVALID_FILE);

		const uint8* entries = buffer + sizeof(IcoHeader);

		// Choose the best fitting one from the icons
		const int optimalWidth = (bitmap.getWidth() > 0)  ? bitmap.getWidth()  : 32;
		const int optimalHeight = (bitmap.getHeight() > 0) ? bitmap.getHeight() : 32;
		int optimalBpp = 32;
		int bestImageIndex = -1;
		uint32 bestDifference = 0xffffffff;

		for (int i = 0; i < imageCount; ++i)
		{
			const uint8* entry = entries + i * sizeof(IconDirEntry);
			const int width = (entry[0] != 0) ? entry[0] : 256;
			const int height = (entry[1] != 0) ? entry[1] : 256;
			const uint32 imageOffset = readLittleEndian32(entry + 12);
			int bpp = readLittleEndian16(entry + 6);
			if (bpp == 0 && imageOffset + 16 <= bufferSize)
				bpp = readLittleEndian16(&buffer[imageOffset + 14]);

			const int dx = abs(width - optimalWidth);
			const int dy = abs(height - optimalHeight);
			const int db = abs(bpp - optimalBpp) * 1000;

			uint32 difference = dx*dx + dy*dy + db*db;
			if (difference < bestDifference)
			{
				bestImageIndex = i;
				bestDifference = difference;
			}
		}

		if (bestImageIndex == -1)
			RETURN(Bitmap::LoadResult::Error::INVALID_FILE);

		// Load icon
		const uint8* bestEntry = entries + bestImageIndex * sizeof(IconDirEntry);
		const uint32 size = readLittleEndian32(bestEntry + 8);
		const uint32 offset = readLittleEndian32(bestEntry + 12);
		if (offset > bufferSize || size > bufferSize - offset || size < 40)
			RETURN(Bitmap::LoadResult::Error::INVALID_FILE);

		uint8* mem = new uint8[size+14];
		memcpy(&mem[14], &buffer[offset], size);
		mem[0] = 'B';
		mem[1] = 'M';
		writeLittleEndian32(&mem[2], 14 + size);
		writeLittleEndian32(&mem[6], 0);
		writeLittleEndian32(&mem[10], 54);
		writeLittleEndian32(&mem[22], readLittleEndian32(&mem[22]) / 2);		// File contains double image height for some reason
		const int width  = (int)readLittleEndian32(&mem[18]);
		const int height = (int)readLittleEndian32(&mem[22]);
		const int bpp    = (int)readLittleEndian16(&mem[28]);

		// Decode as BMP
		MemInputStream bmpstream(mem, size+14, true);
		rmx::BitmapCodecBMP codec;
		bool result = codec.decode(bitmap, bmpstream, outResult);
		if (!result)
			RETURN(Bitmap::LoadResult::Error::INVALID_FILE);

		// Add alpha channel
		if (bpp < 32)
		{
			int palSize = (bpp <= 8) ? ((1 << bpp) * 4) : 0;
			int imageSize = width * height * bpp / 8;
			int mask_line = (width + 31) / 32 * 4;
			uint8* bitmask = &mem[54 + palSize + imageSize];
			for (int y = 0; y < height; ++y)
			{
				uint32* src = bitmap.getPixelPointer(0, y);
				for (int x = 0; x < width; ++x)
				{
					if ((bitmask[x/8 + (height-1-y) * mask_line] >> (7-x%8)) & 1)
						src[x] &= 0x00ffffff;
				}
			}
		}

		RETURN(Bitmap::LoadResult::Error::OK);
	}

	bool BitmapCodecICO::encode(const Bitmap& bitmap, OutputStream& stream)
	{
		return false;
	}
}
