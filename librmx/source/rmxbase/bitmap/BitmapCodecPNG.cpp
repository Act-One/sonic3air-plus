/*
*	rmx Library
*	Copyright (C) 2008-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "rmxbase.h"

// For data decompression, either use zlib (which is faster) or alternatively the RmxDeflate class.
// Wii U currently routes PNGs through the engine deflate path; the zlib inflate path has produced
// blanket PNG decode failures there while other package reads continue working.
#if !defined(PLATFORM_WIIU)
	#define USE_ZLIB
#endif


namespace rmx
{
	namespace
	{
		uint32 readUint32BE(const uint8* pointer)
		{
			// Read as big endian
			return ((uint32)pointer[0] << 24) + ((uint32)pointer[1] << 16) + ((uint32)pointer[2] << 8) + ((uint32)pointer[3]);
		}

		void writeUint32BE(uint8* pointer, uint32 value)
		{
			pointer[0] = (uint8)(value >> 24);
			pointer[1] = (uint8)(value >> 16);
			pointer[2] = (uint8)(value >> 8);
			pointer[3] = (uint8)value;
		}

		uint32 makePixelRGBA(uint8 red, uint8 green, uint8 blue, uint8 alpha)
		{
			return ((uint32)red) + ((uint32)green << 8) + ((uint32)blue << 16) + ((uint32)alpha << 24);
		}

		void logPNGFailure(const char* reason, Bitmap::LoadResult::Error error, int width, int height, uint8 bitdepth, uint8 colortype, size_t compressedBytes)
		{
			static int sLogCount = 0;
			if (sLogCount >= 32)
				return;
			++sLogCount;
			RMX_LOG_INFO("PNG decode failed: reason=" << reason
				<< ", error=" << (int)error
				<< ", size=" << width << "x" << height
				<< ", bitdepth=" << (int)bitdepth
				<< ", colortype=" << (int)colortype
				<< ", compressed=" << compressedBytes << " bytes");
		}
	}


	#define PNG_IHDR 0x49484452
	#define PNG_IDAT 0x49444154
	#define PNG_IEND 0x49454e44
	#define PNG_PLTE 0x504c5445

	const uint8 PNGSignature[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };

	// PNG header
	struct PNGHeader
	{
		uint32 width;
		uint32 height;
		uint8  bitdepth;
		uint8  colortype;
		uint8  compression;
		uint8  filter;
		uint8  interlace;
	};

	#define RETURN(errcode) \
	{ \
		outResult.mError = errcode; \
		return (errcode == Bitmap::LoadResult::Error::OK); \
	}



	bool BitmapCodecPNG::canDecode(const String& format) const
	{
		return (format == "png");
	}

	bool BitmapCodecPNG::canEncode(const String& format) const
	{
		return (format == "png");
	}

	bool BitmapCodecPNG::decode(Bitmap& bitmap, InputStream& stream, Bitmap::LoadResult& outResult)
	{
		// Load from PNG image data in memory
		MemInputStream mstream(stream);
		if (mstream.getRemaining() < 8)
		{
			logPNGFailure("short-stream", Bitmap::LoadResult::Error::INVALID_FILE, 0, 0, 0, 0, mstream.getRemaining());
			RETURN(Bitmap::LoadResult::Error::INVALID_FILE);
		}

		const uint8* mem = mstream.getCursor();
		const uint8* end = mstream.getCursor() + mstream.getRemaining();
		if (memcmp(mem, PNGSignature, 8) != 0)
		{
			logPNGFailure("bad-signature", Bitmap::LoadResult::Error::INVALID_FILE, 0, 0, 0, 0, mstream.getRemaining());
			RETURN(Bitmap::LoadResult::Error::INVALID_FILE);
		}
		mem += 8;

		// Read header
		PNGHeader header = {};
		int width = 0;
		int height = 0;
		uint32 palette[0x100];
		int palette_size = 0;

		// Temporary buffer for the image data
		std::vector<uint8> content;
		content.reserve(mstream.getRemaining());

		// Read chunks
		bool finished = false;
		while (!finished)
		{
			// Length & chunk type
			if ((size_t)(end - mem) < 8)
			{
				logPNGFailure("short-chunk-header", Bitmap::LoadResult::Error::INVALID_FILE, width, height, header.bitdepth, header.colortype, content.size());
				RETURN(Bitmap::LoadResult::Error::INVALID_FILE);
			}

			const uint8* chunkStart = mem;
			const uint32 length = readUint32BE(mem);
			const uint32 type   = readUint32BE(mem + 4);
			mem += 8;
			if ((size_t)(end - mem) < length + 4)
			{
				logPNGFailure("short-chunk-payload", Bitmap::LoadResult::Error::INVALID_FILE, width, height, header.bitdepth, header.colortype, content.size());
				RETURN(Bitmap::LoadResult::Error::INVALID_FILE);
			}

			switch (type)
			{
				// IHDR
				case PNG_IHDR:
				{
					if (length != 13)
					{
						logPNGFailure("bad-ihdr-length", Bitmap::LoadResult::Error::FILE_ERROR, width, height, header.bitdepth, header.colortype, content.size());
						RETURN(Bitmap::LoadResult::Error::FILE_ERROR);
					}
					width = readUint32BE(mem);
					height = readUint32BE(mem + 4);
					header.width = width;
					header.height = height;
					header.bitdepth = mem[8];
					header.colortype = mem[9];
					header.compression = mem[10];
					header.filter = mem[11];
					header.interlace = mem[12];
					break;
				}

				// IEND
				case PNG_IEND:
				{
					finished = true;
					break;
				}

				// IDAT
				case PNG_IDAT:
				{
					const size_t position = content.size();
					content.resize(position + length);
					memcpy(&content[position], &mem[0], length);
					break;
				}

				// PLTE
				case PNG_PLTE:
				{
					palette_size = length / 3;
					for (int i = 0; i < palette_size; ++i)
						palette[i] = makePixelRGBA(mem[i * 3], mem[i * 3 + 1], mem[i * 3 + 2], 0xff);
					for (int i = palette_size; i < 0x100; ++i)
						palette[i] = 0x00000000;
					break;
				}
			}

			// CRC
			const uint32 crc = rmx::getCRC32(chunkStart+4, length+4);
			mem += length;
			if (readUint32BE(mem) != crc)
			{
				logPNGFailure("crc", Bitmap::LoadResult::Error::INVALID_FILE, width, height, header.bitdepth, header.colortype, content.size());
				RETURN(Bitmap::LoadResult::Error::INVALID_FILE);
			}
			mem += 4;
		}

		// Check for empty image data
		if (content.empty())
		{
			logPNGFailure("empty-idat", Bitmap::LoadResult::Error::INVALID_FILE, width, height, header.bitdepth, header.colortype, content.size());
			RETURN(Bitmap::LoadResult::Error::INVALID_FILE);
		}

		// This function supports only 8-bit depth, nothing else
		if (header.bitdepth != 8)
		{
			logPNGFailure("unsupported-bitdepth", Bitmap::LoadResult::Error::UNSUPPORTED, width, height, header.bitdepth, header.colortype, content.size());
			RETURN(Bitmap::LoadResult::Error::UNSUPPORTED);
		}

		int bpp = 0;
		switch (header.colortype)
		{
			case 0:  bpp = 1;  break;
			case 2:  bpp = 3;  break;
			case 3:  bpp = 1;  break;
			case 4:  bpp = 2;  break;
			case 6:  bpp = 4;  break;
			default:
				logPNGFailure("unsupported-colortype", Bitmap::LoadResult::Error::INVALID_FILE, width, height, header.bitdepth, header.colortype, content.size());
				RETURN(Bitmap::LoadResult::Error::INVALID_FILE);
		}
		const int bytesPerLine = width * bpp;

		// Decompress image data
	#if !defined(USE_ZLIB)

		int outsize = 0;
		if ((content[0] & 15) != 8)		// Check zlib header for deflate algorithm
		{
			logPNGFailure("bad-zlib-header", Bitmap::LoadResult::Error::INVALID_FILE, width, height, header.bitdepth, header.colortype, content.size());
			RETURN(Bitmap::LoadResult::Error::INVALID_FILE);
		}

		uint8* output = Deflate::decode(outsize, &content[2], (int)content.size() - 2);		// Skip the zlib header
		if (nullptr == output)
		{
			logPNGFailure("rmx-deflate", Bitmap::LoadResult::Error::INVALID_FILE, width, height, header.bitdepth, header.colortype, content.size());
			RETURN(Bitmap::LoadResult::Error::INVALID_FILE);
		}

	#else

		std::vector<uint8> outputMemory;
		outputMemory.reserve(bytesPerLine * height + 0x4000);	// 0x4000 is the internal chunk size used by "ZlibDeflate::decode"
		if (!ZlibDeflate::decode(outputMemory, &content[0], content.size()))
		{
			logPNGFailure("zlib-inflate", Bitmap::LoadResult::Error::INVALID_FILE, width, height, header.bitdepth, header.colortype, content.size());
			RETURN(Bitmap::LoadResult::Error::INVALID_FILE);
		}

		uint8* output = &outputMemory[0];
		const int outsize = (int)outputMemory.size();

	#endif

		// Create output bitmap
		bitmap.create(width, height);
		uint32* data = bitmap.getData();

		// Remove the filter
		int outpos = 0;
		uint8* currentLineBuffer = nullptr;		// Pointer to current line
		for (int line = 0; line < height; ++line)
		{
			uint8* previousLineBuffer = currentLineBuffer;
			if (line == 0)
			{
				// Misuse the last line (or parts of it) of the output as a temporary buffer storing zeroes
				previousLineBuffer = (uint8*)&data[width * (height - 1)];
				memset(previousLineBuffer, 0, width * bpp);
			}

			if (outpos >= outsize)
			{
				logPNGFailure("short-filter-line", Bitmap::LoadResult::Error::INVALID_FILE, width, height, header.bitdepth, header.colortype, content.size());
				RETURN(Bitmap::LoadResult::Error::INVALID_FILE);
			}
			const uint8 filter = output[outpos];
			currentLineBuffer = &output[outpos+1];
			outpos += bytesPerLine + 1;
			if (outpos > outsize)
			{
				logPNGFailure("short-pixel-line", Bitmap::LoadResult::Error::INVALID_FILE, width, height, header.bitdepth, header.colortype, content.size());
				RETURN(Bitmap::LoadResult::Error::INVALID_FILE);
			}

			if (filter != 0)
			{
				if (filter > 4)
				{
					logPNGFailure("unsupported-filter", Bitmap::LoadResult::Error::INVALID_FILE, width, height, header.bitdepth, header.colortype, content.size());
					RETURN(Bitmap::LoadResult::Error::INVALID_FILE);
				}
				uint8* buf = currentLineBuffer;
				uint8* buf0 = previousLineBuffer;
				const int leftPixelOffset = -bpp;
				switch (filter)
				{
					// "Sub" filter
					case 1:
					{
						buf += bpp;
						for (int i = bpp; i < bytesPerLine; ++i)
						{
							*buf += buf[leftPixelOffset];
							++buf;
						}
						break;
					}

					// "Up" filter
					case 2:
					{
						for (int i = 0; i < bytesPerLine; ++i)
						{
							*buf += *buf0;
							++buf;
							++buf0;
						}
						break;
					}

					// "Average" filter
					case 3:
					{
						for (int i = 0; i < bpp; ++i)
						{
							*buf += *buf0 / 2;
							++buf;
							++buf0;
						}
						for (int i = bpp; i < bytesPerLine; ++i)
						{
							const uint8 left = buf[leftPixelOffset];
							const uint8 up = *buf0;
							*buf += (left + up) / 2;
							++buf;
							++buf0;
						}
						break;
					}

					// "Paeth" filter
					case 4:
					{
						for (int i = 0; i < bpp; ++i)
						{
							*buf += *buf0;
							++buf;
							++buf0;
						}
						for (int i = bpp; i < bytesPerLine; ++i)
						{
							const uint8 left = buf[leftPixelOffset];
							const uint8 up = *buf0;
							const uint8 upLeft = buf0[leftPixelOffset];
							const int paeth = left + up - upLeft;
							const int d1 = abs(paeth - left);
							const int d2 = abs(paeth - up);
							const int d3 = abs(paeth - upLeft);
							if ((d1 <= d2) && (d1 <= d3))
								*buf += left;
							else if (d2 <= d3)
								*buf += up;
							else
								*buf += upLeft;
							++buf;
							++buf0;
						}
						break;
					}
				}
			}

			// Convert to 32-bit
			{
				const uint8* src = currentLineBuffer;
				uint32* dst = &data[line*width];
				switch (header.colortype)
				{
					// 8-bit grayscale
					case 0:
						for (int i = 0; i < width; ++i)
							dst[i] = 0xff000000 + (0x010101 * src[i]);
						break;

					// 24-bit RGB
					case 2:
						for (int i = 0; i < width; ++i)
							dst[i] = makePixelRGBA(src[i*3], src[i*3+1], src[i*3+2], 0xff);
						break;

					// Palette image
					case 3:
						for (int i = 0; i < width; ++i)
							dst[i] = palette[src[i]];
						break;

					// 16-bit gray + alpha
					case 4:
						for (int i = 0; i < width; ++i)
							dst[i] = (0x010101 * src[i*2]) + (src[i*2+1] << 24);
						break;

					// 32-bit RGB + alpha
					case 6:
						for (int i = 0; i < width; ++i)
							dst[i] = makePixelRGBA(src[i*4], src[i*4+1], src[i*4+2], src[i*4+3]);
						break;
				}
			}
		}

	#if !defined(USE_ZLIB)
		delete[] output;
	#endif
		RETURN(Bitmap::LoadResult::Error::OK);
	}

	bool BitmapCodecPNG::encode(const Bitmap& bitmap, OutputStream& stream)
	{
		// Save image data to memory in PNG format
		if (bitmap.empty())
			return false;

		int width = bitmap.getWidth();
		int height = bitmap.getHeight();

		// Setup PNG header
		uint8 header[13];
		writeUint32BE(&header[0], bitmap.getWidth());
		writeUint32BE(&header[4], bitmap.getHeight());
		header[8] = 8;
		header[9] = 6;
		header[10] = 0;
		header[11] = 0;
		header[12] = 0;

		// Pack image data
		uint8* tmpdata = new uint8[(width*4+1)*height];
		for (int line = 0; line < height; ++line)
		{
			tmpdata[line*(width*4+1)] = 0;
			uint8* dst = &tmpdata[line*(width*4+1)+1];
			const uint32* src = bitmap.getPixelPointer(0, line);
			for (int i = 0; i < width; ++i)
			{
				const uint32 pixel = src[i];
				dst[i*4] = (uint8)pixel;
				dst[i*4+1] = (uint8)(pixel >> 8);
				dst[i*4+2] = (uint8)(pixel >> 16);
				dst[i*4+3] = (uint8)(pixel >> 24);
			}
		}
		int outsize = 0;
		uint8* output = Deflate::encode(outsize, tmpdata, (width*4+1)*height);			// TODO: Optionally use ZlibDeflate here as well
		const uint32 adler = rmx::getAdler32(tmpdata, (width*4+1)*height);
		delete[] tmpdata;

		// Write PNG data
		const int bufsize = 3*12 + 13 + outsize+6;
		uint8* buffer = new uint8[bufsize];
		uint8* mem = buffer;

		// Write chunks
		for (int chunknum = 0; chunknum < 3; ++chunknum)
		{
			uint8* chunkStart = mem;
			mem += 8;
			uint32 type = 0;
			uint32 length = 0;
			if (chunknum == 0)
			{
				type = PNG_IHDR;
				length = 13;
				memcpy(mem, &header, length);
			}
			else if (chunknum == 1)
			{
				type = PNG_IDAT;
				length = outsize + 6;
				mem[0] = 0x78;		// zlib header
				mem[1] = 0xda;		// zlib header
				memcpy(&mem[2], output, outsize);
				writeUint32BE(&mem[2 + outsize], adler);
			}
			else
			{
				type = PNG_IEND;
			}

			writeUint32BE(&chunkStart[0], length);
			writeUint32BE(&chunkStart[4], type);
			const uint32 crc = rmx::getCRC32(chunkStart+4, length+4);
			writeUint32BE(&chunkStart[length+8], crc);
			mem += length + 4;
		}
		delete[] output;

		stream.write(PNGSignature, 8);
		stream.write(buffer, bufsize);
		delete[] buffer;

		return true;
	}
}
