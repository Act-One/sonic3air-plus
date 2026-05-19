/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/rendering/parts/PlaneManager.h"
#include "oxygen/rendering/parts/PatternManager.h"
#include "oxygen/simulation/EmulatorInterface.h"


namespace
{
	uint16 sanitizePlaneBaseVRAMAddress(uint16 vramAddress, const char* planeName)
	{
		const uint16 sanitized = vramAddress & 0xfffe;
		if (sanitized != vramAddress)
		{
			RMX_LOG_INFO("PlaneManager: aligned odd name table base for plane " << planeName << " from 0x" << rmx::hexString(vramAddress, 4)
				<< " to 0x" << rmx::hexString(sanitized, 4));
		}
		return sanitized;
	}

	Vec2i sanitizePlayfieldPatternSize(const Vec2i& size)
	{
		Vec2i sanitized(size);
		sanitized.x = clamp(sanitized.x, 1, 128);
		sanitized.y = clamp(sanitized.y, 1, 128);

		if (sanitized != size)
		{
			RMX_LOG_INFO("PlaneManager: clamped invalid playfield size in patterns from "
				<< size.x << "x" << size.y << " to " << sanitized.x << "x" << sanitized.y);
		}
		return sanitized;
	}

	const PlaneManager& resolveReadablePlaneManager(const PlaneManager* self, const char* caller)
	{
		const PlaneManager& authoritative = RenderParts::instance().getPlaneManager();
		if (self == &authoritative)
			return authoritative;

		static int sLoggedMismatchedSelfCount = 0;
		if (sLoggedMismatchedSelfCount < 16)
		{
			++sLoggedMismatchedSelfCount;
			RMX_LOG_INFO("PlaneManager: using authoritative instance in " << caller
				<< " because caller object was " << rmx::hexString((uint64)(uintptr_t)self, 16)
				<< " while authoritative object is " << rmx::hexString((uint64)(uintptr_t)&authoritative, 16));
		}
		return authoritative;
	}

	uint16 readVRamWrapped16(uint16 vramAddress)
	{
		return EmulatorInterface::instance().readVRam16(vramAddress);
	}

	void copyPlanePatternsWithWrap(uint16* dst, uint16 planeBaseAddress, int numPatterns)
	{
		for (int k = 0; k < numPatterns; ++k)
		{
			dst[k] = readVRamWrapped16((uint16)(planeBaseAddress + k * 2));
		}
	}

	void fillBufferByAbstraction(uint16* buffer, const Vec2i& cameraPosition, const Vec2i& screenSize)
	{
		// TODO: This is entirely S3AIR-specific
		const uint32 minCameraTileX = cameraPosition.x / 16;
		const uint32 minCameraTileY = cameraPosition.y / 16;

		const uint32 width = (screenSize.x + 15) / 16;		// Usually 32; measured in tiles, not patterns (so we have to multiply by 2 here and there to get patterns)
		const uint32 height = (screenSize.y + 15) / 16;		// Usually 16

		for (uint32 y = 0; y < height; ++y)
		{
			uint16* lineBase0 = &buffer[(y*2) * width*2];
			uint16* lineBase1 = &buffer[(y*2+1) * width*2];

			for (uint32 x = 0; x < width; ++x)
			{
				// Chunk coordinates
				const uint32 globalColumn = minCameraTileX + ((x - minCameraTileX) % width);
				const uint32 globalRow = minCameraTileY + ((y - minCameraTileY) % height);

				const uint32 chunkColumn = globalColumn / 8;
				const uint32 chunkRow = (globalRow / 8) & 0x1f;		// Looks like there are only 32 chunks in y-direction allowed in a level; that makes a maximum level height of 4096 pixels

				const uint32 chunkAddress = 0xffff0000 + EmulatorInterface::instance().readMemory16(0xffff8008 + chunkRow * 4) + chunkColumn;
				const uint8  chunkType = EmulatorInterface::instance().readMemory8(chunkAddress);

				// Tile coordinates inside chunk
				const uint32 tileColumn = globalColumn % 8;
				const uint32 tileRow = globalRow % 8;

				const uint32 tileAddress = 0xffff0000 + EmulatorInterface::instance().readMemory16(0x00f02a + chunkType * 2) + tileRow * 16 + tileColumn * 2;
				const uint16 tile = EmulatorInterface::instance().readMemory16(tileAddress);

				// Access tile graphics (consisting of 2x2 sprite patterns)
				const uint32 tilePatternBaseAddress = 0xffff9000 + (tile & 0x03ff) * 8;

				const bool flipX = (tile & 0x0400) != 0;
				const bool flipY = (tile & 0x0800) != 0;
				const uint32 mx = flipX ? 2 : 0;
				const uint32 my = flipY ? 4 : 0;
				const uint16 xorMask = (tile & 0x0c00) << 1;

				// Write 2x2 sprite patterns of this tile
				lineBase0[0] = EmulatorInterface::instance().readMemory16(tilePatternBaseAddress     + mx + my) ^ xorMask;
				lineBase0[1] = EmulatorInterface::instance().readMemory16(tilePatternBaseAddress + 2 - mx + my) ^ xorMask;
				lineBase1[0] = EmulatorInterface::instance().readMemory16(tilePatternBaseAddress + 4 + mx - my) ^ xorMask;
				lineBase1[1] = EmulatorInterface::instance().readMemory16(tilePatternBaseAddress + 6 - mx - my) ^ xorMask;

				lineBase0 += 2;
				lineBase1 += 2;
			}
		}
	}
}



PlaneManager::PlaneManager(PatternManager& patternManager) :
	mPatternManager(patternManager),
	mPlayfieldSize(64, 32)
{
}

void PlaneManager::reset()
{
	mNameTableBaseA = 0xc000;
	mNameTableBaseB = 0xe000;
	mNameTableBaseW = 0x8000;

	mPlayfieldSize.set(64, 32);
	mIsPlaneWBelowSplitY = false;
	mPlaneAWSplitY = 0;

	resetCustomPlanes();
}

void PlaneManager::refresh()
{
	// Build plane pattern textures
	const bool isDeveloperMode = EngineMain::getDelegate().useDeveloperFeatures();
	const int numPatterns = mPlayfieldSize.x * mPlayfieldSize.y;
	RMX_CHECK(numPatterns <= MAX_PLANE_PATTERNS, "Playfield uses " << numPatterns << " patterns, but PlaneManager only supports " << MAX_PLANE_PATTERNS, return);
	for (int index = 0; index < 4; ++index)
	{
		uint16* buffer = mPlanePatternsBuffer[index];
		switch (index)
		{
			case PLANE_DEBUG:
			{
				if (isDeveloperMode)
				{
					for (uint16 k = 0; k < 0x800; ++k)
					{
						buffer[k] = k + ((uint16)mPatternManager.getLastUsedAtex(k) << 9);
					}
				}
				break;
			}

			default:
			{
				if (isPlaneUsed(index))
				{
					copyPlanePatternsWithWrap(buffer, getPlaneBaseVRAMAddress(index), numPatterns);
					if (isDeveloperMode)
					{
						for (int k = 0; k < numPatterns; ++k)
						{
							mPatternManager.setLastUsedAtex(buffer[k], (buffer[k] >> 9) & 0x70);
						}
					}
				}
				break;
			}
		}
	}
}

void PlaneManager::resetCustomPlanes()
{
	for (int i = 0; i < 4; ++i)
		mDisabledDefaultPlane[i] = false;
	mCustomPlanes.clear();
}

bool PlaneManager::isPlaneUsed(int index) const
{
	// Is index valid at all?
	if (EngineMain::getDelegate().useDeveloperFeatures())
	{
		if (index > PLANE_DEBUG)
			return false;
	}
	else
	{
		if (index >= PLANE_DEBUG)
			return false;
	}

	// Plane A or W may be unused
	if (mPlaneAWSplitY == 0)
	{
		if (mIsPlaneWBelowSplitY)
		{
			if (index == PLANE_A)
				return false;
		}
		else
		{
			if (index == PLANE_W)
				return false;
		}
	}

	return true;
}

Vec2i PlaneManager::getPlayfieldSizeInPatterns() const
{
	return resolveReadablePlaneManager(this, "getPlayfieldSizeInPatterns").mPlayfieldSize;
}

Vec2i PlaneManager::getPlayfieldSizeInPixels() const
{
	const Vec2i playfieldSize = getPlayfieldSizeInPatterns();
	return Vec2i(playfieldSize.x * 8, playfieldSize.y * 8);
}

Vec4i PlaneManager::getPlayfieldSizeForShaders() const
{
	const Vec2i playfieldSize = getPlayfieldSizeInPatterns();
	return Vec4i(playfieldSize.x * 8, playfieldSize.y * 8, playfieldSize.x, playfieldSize.y);
}

void PlaneManager::setNameTableBaseB(uint16 vramAddress)
{
	mNameTableBaseB = sanitizePlaneBaseVRAMAddress(vramAddress, "B");
}

void PlaneManager::setNameTableBaseA(uint16 vramAddress)
{
	mNameTableBaseA = sanitizePlaneBaseVRAMAddress(vramAddress, "A");
}

void PlaneManager::setNameTableBaseW(uint16 vramAddress)
{
	mNameTableBaseW = sanitizePlaneBaseVRAMAddress(vramAddress, "W");
}

void PlaneManager::setPlayfieldSizeInPatterns(const Vec2i& size)
{
	mPlayfieldSize = sanitizePlayfieldPatternSize(size);
}

void PlaneManager::setPlayfieldSizeInPixels(const Vec2i& size)
{
	setPlayfieldSizeInPatterns(size / 8);
}

const uint16* PlaneManager::getPlanePatternsBuffer(uint8 index) const
{
	RMX_ASSERT(index < 4, "Invalid plane index " << index);
	return resolveReadablePlaneManager(this, "getPlanePatternsBuffer").mPlanePatternsBuffer[index];
}

uint16 PlaneManager::getPlaneBaseVRAMAddress(int planeIndex) const
{
	const PlaneManager& self = resolveReadablePlaneManager(this, "getPlaneBaseVRAMAddress");
	if (planeIndex == PLANE_B)
		return self.mNameTableBaseB;
	if (planeIndex == PLANE_A)
		return self.mNameTableBaseA;
	if (planeIndex == PLANE_W)
		return self.mNameTableBaseW;

	static int sLoggedInvalidPlaneIndexCount = 0;
	if (sLoggedInvalidPlaneIndexCount < 8)
	{
		++sLoggedInvalidPlaneIndexCount;
		RMX_LOG_INFO("PlaneManager: falling back to plane B for invalid runtime plane index " << planeIndex);
	}
	return self.mNameTableBaseB;
}

const uint16* PlaneManager::getPlaneDataInVRAM(int planeIndex) const
{
	const PlaneManager& self = resolveReadablePlaneManager(this, "getPlaneDataInVRAM");
	return (const uint16*)(EmulatorInterface::instance().getVRam() + self.getPlaneBaseVRAMAddress(planeIndex));
}

size_t PlaneManager::getPlaneSizeInVRAM(int planeIndex) const
{
	const PlaneManager& self = resolveReadablePlaneManager(this, "getPlaneSizeInVRAM");
	return (size_t)(self.mPlayfieldSize.x * self.mPlayfieldSize.y * 2);
}

uint16 PlaneManager::getPatternVRAMAddress(int planeIndex, uint16 patternIndex) const
{
	return getPlaneBaseVRAMAddress(planeIndex) + patternIndex * 2;
}

uint16 PlaneManager::getPatternAtIndex(int planeIndex, uint16 patternIndex) const
{
	if (EngineMain::getDelegate().useDeveloperFeatures())
	{
		if (planeIndex == PLANE_DEBUG)
			return patternIndex;
	}
	return readVRamWrapped16(getPatternVRAMAddress(planeIndex, patternIndex));
}

void PlaneManager::setPatternAtIndex(int planeIndex, uint16 patternIndex, uint16 value)
{
	EmulatorInterface::instance().writeVRam16(getPatternVRAMAddress(planeIndex, patternIndex), value);
}

const uint16* PlaneManager::getPlaneContent(int planeIndex, uint16 patternIndex) const
{
	return (uint16*)(EmulatorInterface::instance().getVRam() + getPatternVRAMAddress(planeIndex, patternIndex));
}

void PlaneManager::setWindowPlaneSplitX(bool rightSideWindow, uint16 splitX)
{
	mIsPlaneWRightOfSplitX = rightSideWindow;
	mPlaneAWSplitX = splitX;
}

void PlaneManager::setWindowPlaneSplitY(bool bottomWindow, uint16 splitY)
{
	mIsPlaneWBelowSplitY = bottomWindow;
	mPlaneAWSplitY = splitY;
}

void PlaneManager::setRenderPlaneABehindW(bool renderPlaneABehindW)
{
	mRenderPlaneABehindW = renderPlaneABehindW;
}

void PlaneManager::getPlaneRects(std::vector<PlaneRect>& output, const Recti& fullscreenRect) const
{
	output.clear();

	const int splitX = clamp(mPlaneAWSplitX, 0, fullscreenRect.width);
	const int splitY = clamp(mPlaneAWSplitY, 0, fullscreenRect.height);

	Recti rectA = fullscreenRect;	// Rectangle where plane A is rendered (unless it's enforced to be fullscreen in any case)
	Recti invA;						// Component-wise "inverse" of that rectangle: includes exactly the remaining intervals in both x and y directions individually
	{
		if (mIsPlaneWRightOfSplitX)
		{
			rectA.width = splitX;
			invA.x = splitX;
			invA.width = fullscreenRect.width - splitX;
		}
		else
		{
			invA.width = splitX;
			rectA.x = splitX;
			rectA.width = fullscreenRect.width - splitX;
		}

		if (mIsPlaneWBelowSplitY)
		{
			rectA.height = splitY;
			invA.y = splitY;
			invA.height = fullscreenRect.height - splitY;
		}
		else
		{
			invA.height = splitY;
			rectA.y = splitY;
			rectA.height = fullscreenRect.height - splitY;
		}
	}

	// Plane B
	PlaneRect& planeRectB = vectorAdd(output);
	planeRectB.mPlane = PLANE_B;
	planeRectB.mRect = fullscreenRect;

	if (mRenderPlaneABehindW || !rectA.isEmpty())
	{
		// Plane A
		PlaneRect& planeRectA = vectorAdd(output);
		planeRectA.mPlane = PLANE_A;
		planeRectA.mRect = mRenderPlaneABehindW ? fullscreenRect : rectA;
	}

	if (rectA != fullscreenRect)
	{
		// Plane W
		PlaneRect& planeRectW = vectorAdd(output);
		planeRectW.mPlane = PLANE_W;
		planeRectW.mRect = fullscreenRect;

		if (rectA.isEmpty())
		{
			// Plane W is fullscreen
		}
		else if (rectA.width == fullscreenRect.width)
		{
			// Plane W is full width, but limited height
			planeRectW.mRect.y = invA.y;
			planeRectW.mRect.height = invA.height;
		}
		else if (rectA.height == fullscreenRect.height)
		{
			// Plane W is full height, but limited width
			planeRectW.mRect.x = invA.x;
			planeRectW.mRect.width = invA.width;
		}
		else
		{
			// The screen is split both horizontally and vertically, and three of the four areas are covered by plane W
			PlaneRect& planeRectW2 = vectorAdd(output);
			planeRectW2.mPlane = PLANE_W;
			planeRectW2.mRect = fullscreenRect;

			if (rectA.y == 0)
			{
				// Upper part is limited width
				planeRectW.mRect.x = invA.x;
				planeRectW.mRect.width = invA.width;
				planeRectW.mRect.height = rectA.height;

				// Lower part is full width
				planeRectW2.mRect.y = invA.y;
				planeRectW2.mRect.height = invA.height;
			}
			else
			{
				// Upper part is full width
				planeRectW.mRect.height = invA.height;

				// Lower part is limited width
				planeRectW2.mRect.x = invA.x;
				planeRectW2.mRect.width = invA.width;
				planeRectW2.mRect.y = rectA.y;
				planeRectW2.mRect.height = rectA.height;
			}
		}
	}
}

void PlaneManager::dumpAsPaletteBitmap(PaletteBitmap& output, int planeIndex, bool highlightPrioPatterns) const
{
	Vec2i bitmapSize;
	if (planeIndex <= PLANE_A)
	{
		bitmapSize = getPlayfieldSizeInPixels();
	}
	else
	{
		bitmapSize.set(512, 256);
	}
	output.create(bitmapSize.x, bitmapSize.y);

	const PatternManager::CacheItem* patternCache = mPatternManager.getPatternCache();
	const uint16 numPatternsPerLine = (uint16)(bitmapSize.x / 8);

	uint8* dest = output.getData();
	for (int y = 0; y < bitmapSize.y; ++y)
	{
		for (int x = 0; x < bitmapSize.x; x += 8, dest += 8)
		{
			const uint16 patternIndex = getPatternAtIndex(planeIndex, (x / 8) + (y / 8) * numPatternsPerLine);
			const PatternManager::CacheItem::Pattern& pattern = patternCache[patternIndex & 0x07ff].mFlipVariation[(patternIndex >> 11) & 3];
			const uint8* srcPatternPixels = &pattern.mPixels[(x & 0x07) + (y & 0x07) * 8];
			const uint8 atex = ((planeIndex != PLANE_DEBUG) ? (patternIndex >> 9) : mPatternManager.getLastUsedAtex(patternIndex)) & 0x30;

			for (int k = 0; k < 8; ++k)
			{
				const uint8 colorIndex = srcPatternPixels[k];
				dest[k] = colorIndex + atex;
			}

			const bool lowerBrightness = (highlightPrioPatterns && (patternIndex & 0x8000) == 0);
			if (lowerBrightness)
			{
				for (int k = 0; k < 8; ++k)
				{
					dest[k] |= 0x80;
				}
			}
		}
	}
}

void PlaneManager::setDefaultPlaneEnabled(uint8 index, bool enabled)
{
	mDisabledDefaultPlane[index] = !enabled;
}

void PlaneManager::setupCustomPlane(const Recti& rect, uint8 sourcePlane, uint8 scrollOffsets, uint16 renderQueue)
{
	const int planeIndex = sourcePlane & 0x03;
	if (!isRenderablePlaneIndex(planeIndex))
	{
		static int sLoggedInvalidCustomPlaneCount = 0;
		if (sLoggedInvalidCustomPlaneCount < 8)
		{
			++sLoggedInvalidCustomPlaneCount;
			RMX_LOG_INFO("PlaneManager: ignoring custom plane with invalid source plane " << (int)sourcePlane
				<< " (resolved plane index " << planeIndex << "), rect=(" << rect.x << "," << rect.y << "," << rect.width << "," << rect.height
				<< "), scrollOffsets=" << (int)scrollOffsets << ", renderQueue=0x" << rmx::hexString(renderQueue, 4));
		}
		return;
	}

	CustomPlane& plane = vectorAdd(mCustomPlanes);
	plane.mRect = rect;
	plane.mSourcePlane = sourcePlane;
	plane.mScrollOffsets = scrollOffsets;
	plane.mRenderQueue = renderQueue;
}

void PlaneManager::serializeSaveState(VectorBinarySerializer& serializer, uint8 formatVersion)
{
	serializer.serialize(mNameTableBaseA);
	serializer.serialize(mNameTableBaseB);

	if (serializer.isReading())
	{
		Vec2i playfieldSize;
		playfieldSize.x = serializer.read<uint16>();
		playfieldSize.y = serializer.read<uint16>();
		setPlayfieldSizeInPixels(playfieldSize);
	}
	else
	{
		const Vec2i playfieldSize = getPlayfieldSizeInPixels();
		serializer.write<uint16>(playfieldSize.x);
		serializer.write<uint16>(playfieldSize.y);
	}

	if (formatVersion >= 4)
	{
		serializer.serialize(mNameTableBaseW);
		serializer.serializeAs<uint8>(mIsPlaneWBelowSplitY);
		serializer.serialize(mPlaneAWSplitY);

		if (formatVersion >= 7)
		{
			serializer.serializeAs<uint8>(mRenderPlaneABehindW);
		}

		for (int k = 0; k < 4; ++k)
		{
			serializer.serialize(mDisabledDefaultPlane[k]);
		}

		serializer.serializeArraySize(mCustomPlanes, 64);
		for (CustomPlane& customPlane : mCustomPlanes)
		{
			serializer.serializeAs<int16>(customPlane.mRect.x);
			serializer.serializeAs<int16>(customPlane.mRect.y);
			serializer.serializeAs<int16>(customPlane.mRect.width);
			serializer.serializeAs<int16>(customPlane.mRect.height);
			serializer.serialize(customPlane.mSourcePlane);
			serializer.serialize(customPlane.mScrollOffsets);
			serializer.serialize(customPlane.mRenderQueue);

			if (serializer.isReading())
			{
				const int planeIndex = customPlane.mSourcePlane & 0x03;
				if (!isRenderablePlaneIndex(planeIndex))
				{
					customPlane.mSourcePlane = (customPlane.mSourcePlane & 0x10) | PLANE_B;
				}
			}
		}
	}

	if (serializer.isReading())
	{
		setNameTableBaseA(mNameTableBaseA);
		setNameTableBaseB(mNameTableBaseB);
		setNameTableBaseW(mNameTableBaseW);
	}
}
