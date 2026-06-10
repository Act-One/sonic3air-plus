/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/rendering/parts/ScrollOffsetsManager.h"
#include "oxygen/rendering/parts/PlaneManager.h"
#include "oxygen/simulation/EmulatorInterface.h"


// This bitmask is applied to reduce the necessary precision in "render_plane.shader"
//  -> That's needed because on some Android devices, we don't even get full 16-bit integers
static const constexpr uint16 SCROLL_OFFSET_VALUE_BITMASK = 0x0fff;


namespace
{
	bool hasVRamChangeInBitRange(const BitArray<0x800>& changeBits, size_t firstBit, size_t lastBit)
	{
		RMX_ASSERT(firstBit <= lastBit && lastBit < 0x800, "Invalid VRAM change bit range");
		for (size_t bit = firstBit; bit <= lastBit; )
		{
			const size_t chunkIndex = bit >> 6;
			if (!changeBits.anyBitSetInChunk(chunkIndex))
			{
				bit = (chunkIndex + 1) << 6;
				continue;
			}

			const size_t chunkEnd = std::min(lastBit, ((chunkIndex + 1) << 6) - 1);
			for (; bit <= chunkEnd; ++bit)
			{
				if (changeBits.isBitSet(bit))
					return true;
			}
		}
		return false;
	}

	bool hasVRamRangeChanges(const BitArray<0x800>& changeBits, uint16 vramAddress, size_t bytes)
	{
		if (bytes == 0)
			return false;

		if (bytes >= 0x10000)
		{
			for (size_t chunkIndex = 0; chunkIndex < BitArray<0x800>::NUM_CHUNKS; ++chunkIndex)
			{
				if (changeBits.anyBitSetInChunk(chunkIndex))
					return true;
			}
			return false;
		}

		const uint32 firstAddress = vramAddress;
		const uint32 lastAddress = firstAddress + (uint32)bytes - 1;
		if (lastAddress < 0x10000)
			return hasVRamChangeInBitRange(changeBits, firstAddress >> 5, lastAddress >> 5);

		return hasVRamChangeInBitRange(changeBits, firstAddress >> 5, 0x7ff) ||
			hasVRamChangeInBitRange(changeBits, 0, (lastAddress & 0xffff) >> 5);
	}

	uint16 sanitizeHorizontalScrollTableBase(uint16 vramAddress)
	{
		const uint16 sanitized = vramAddress & 0xfffe;
		if (sanitized != vramAddress)
		{
			RMX_LOG_INFO("ScrollOffsetsManager: aligned odd horizontal scroll table base from 0x"
				<< rmx::hexString(vramAddress, 4) << " to 0x" << rmx::hexString(sanitized, 4));
		}
		return sanitized;
	}
}


ScrollOffsetsManager::ScrollOffsetsManager(PlaneManager& planeManager) :
	mPlaneManager(planeManager)
{
	reset();
}

void ScrollOffsetsManager::reset()
{
	mVerticalScrolling = false;
	mHorizontalScrollMask = 0xff;
	mHorizontalScrollTableBase = 0xf000;
	mHorizontalScrollSourceDirty = true;
	mVerticalScrollOffsetBias = 0;

	for (int index = 0; index < 4; ++index)
	{
		mSets[index] = { 0 };
		mInterpolatedSets[index] = { 0 };
		mSets[index].mChangeCounter = 1;
		mInterpolatedSets[index].mChangeCounter = 1;
	}
}

void ScrollOffsetsManager::refresh(const RefreshParameters& refreshParameters)
{
	if (refreshParameters.mHasNewSimulationFrame)
	{
#if defined(PLATFORM_WIIU)
		const bool hScrollSourceChanged = true;
#else
		const size_t hScrollBytes = (size_t)mHorizontalScrollMask * 4 + 4;
		const bool hScrollSourceChanged = mHorizontalScrollSourceDirty ||
			hasVRamRangeChanges(EmulatorInterface::instance().getVRamChangeBits(), mHorizontalScrollTableBase, hScrollBytes);
#endif

		for (int index = 0; index < 4; ++index)
		{
			bool setChanged = false;

			// Horizontal scrolling
			uint16* hBuffer = mSets[index].mScrollOffsetsH;
			bool* overwriteFlags = mSets[index].mExplicitOverwriteH;
			if (index < 2)
			{
				for (int k = 0; k < 0x100; ++k)
				{
					if (!overwriteFlags[k])
					{
						if (!hScrollSourceChanged)
							continue;

						const int srcIndex = k & mHorizontalScrollMask;
						const uint16 address = (uint16)(mHorizontalScrollTableBase + (1 - index) * 2 + srcIndex * 4);
						const uint16 value = (-EmulatorInterface::instance().readVRam16(address)) & SCROLL_OFFSET_VALUE_BITMASK;
						if (hBuffer[k] != value)
						{
							hBuffer[k] = value;
							setChanged = true;
						}
					}
				}
			}
			else
			{
				for (int k = 0; k < 0x100; ++k)
				{
					if (!overwriteFlags[k])
					{
						// Fallback: Use standard scroll offset values
						const uint16 value = mSets[index - 2].mScrollOffsetsH[k];
						if (hBuffer[k] != value)
						{
							hBuffer[k] = value;
							setChanged = true;
						}
					}
				}
			}

			// Vertical scrolling
			uint16* vBuffer = mSets[index].mScrollOffsetsV;
			overwriteFlags = mSets[index].mExplicitOverwriteV;

			if (index < 2)
			{
				// One entry in VSRAM for each pattern column
				const uint16* src = &EmulatorInterface::instance().getVSRam()[1 - index];
				for (int k = 0; k < 0x20; ++k)
				{
					if (!overwriteFlags[k])
					{
						const uint16 value = src[k*2] & SCROLL_OFFSET_VALUE_BITMASK;
						if (vBuffer[k] != value)
						{
							vBuffer[k] = value;
							setChanged = true;
						}
					}
				}
			}
			else
			{
				for (int k = 0; k < 0x20; ++k)
				{
					if (!overwriteFlags[k])
					{
						// Fallback: Use standard scroll offset values
						const uint16 value = mSets[index - 2].mScrollOffsetsV[k];
						if (vBuffer[k] != value)
						{
							vBuffer[k] = value;
							setChanged = true;
						}
					}
				}
			}

			if (setChanged)
				++mSets[index].mChangeCounter;
		}
		mHorizontalScrollSourceDirty = false;

		if (refreshParameters.mUsingFrameInterpolation)
		{
			const uint16 positionMaskH = (uint16)(mPlaneManager.getPlayfieldSizeInPixels().x - 1);
			const uint16 halfPositionH = (uint16)(mPlaneManager.getPlayfieldSizeInPixels().x / 2);
			const uint16 positionMaskV = (uint16)(mPlaneManager.getPlayfieldSizeInPixels().y - 1);
			const uint16 halfPositionV = (uint16)(mPlaneManager.getPlayfieldSizeInPixels().y / 2);

			for (int index = 0; index < 4; ++index)
			{
				const int verticalDifference = mSets[index].mScrollOffsetsV[0] - mInterpolatedSets[index].mLastScrollOffsetsV[0];
				for (int k = 0; k < 0x100; ++k)
				{
					const int oldK = clamp(k + verticalDifference, 0, 223);
					int16 diff = mSets[index].mScrollOffsetsH[k] - mInterpolatedSets[index].mLastScrollOffsetsH[oldK];
					diff = ((diff & positionMaskH) < halfPositionH) ? (diff & positionMaskH) : -((-diff) & positionMaskH);
					if (abs(diff) >= 0x30)
						diff = 0;
					mInterpolatedSets[index].mDifferenceScrollOffsetsH[k] = diff;
				}
				for (int k = 0; k < 0x20; ++k)
				{
					int16 diff = mSets[index].mScrollOffsetsV[k] - mInterpolatedSets[index].mLastScrollOffsetsV[k];
					diff = ((diff & positionMaskV) < halfPositionV) ? (diff & positionMaskV) : -((-diff) & positionMaskV);
					if (abs(diff) >= 0x30)
						diff = 0;
					mInterpolatedSets[index].mDifferenceScrollOffsetsV[k] = diff;
				}
			}
		}

		for (int index = 0; index < 4; ++index)
		{
			mInterpolatedSets[index].mValid = (refreshParameters.mUsingFrameInterpolation && mInterpolatedSets[index].mHasLastScrollOffsets);
		}
	}

	if (refreshParameters.mUsingFrameInterpolation)
	{
		const float interpolationFactor = (1.0f - refreshParameters.mInterFramePosition);
		const uint16 positionMaskV = (uint16)(mPlaneManager.getPlayfieldSizeInPixels().y - 1);

		for (int index = 0; index < 4; ++index)
		{
			bool interpolatedChanged = false;
			const int verticalDifference = -roundToInt((float)mInterpolatedSets[index].mDifferenceScrollOffsetsV[0] * interpolationFactor);
			for (int k = 0; k < 0x100; ++k)
			{
				const int oldK = clamp(k + verticalDifference, 0, 223);
				const uint16 value = (mSets[index].mScrollOffsetsH[oldK] - roundToInt((float)mInterpolatedSets[index].mDifferenceScrollOffsetsH[oldK] * interpolationFactor));
				if (mInterpolatedSets[index].mInterpolatedScrollOffsetsH[k] != value)
				{
					mInterpolatedSets[index].mInterpolatedScrollOffsetsH[k] = value;
					interpolatedChanged = true;
				}
			}
			for (int k = 0; k < 0x20; ++k)
			{
				const uint16 value = (mSets[index].mScrollOffsetsV[k] - roundToInt((float)mInterpolatedSets[index].mDifferenceScrollOffsetsV[k] * interpolationFactor)) & positionMaskV;
				if (mInterpolatedSets[index].mInterpolatedScrollOffsetsV[k] != value)
				{
					mInterpolatedSets[index].mInterpolatedScrollOffsetsV[k] = value;
					interpolatedChanged = true;
				}
			}
			if (interpolatedChanged)
				++mInterpolatedSets[index].mChangeCounter;
		}
	}
}

void ScrollOffsetsManager::setHorizontalScrollTableBase(uint16 vramAddress)
{
	const uint16 sanitized = sanitizeHorizontalScrollTableBase(vramAddress);
	if (mHorizontalScrollTableBase != sanitized)
	{
		mHorizontalScrollTableBase = sanitized;
		mHorizontalScrollSourceDirty = true;
	}
}

void ScrollOffsetsManager::setHorizontalScrollMask(uint8 scrollMask)
{
	if (mHorizontalScrollMask != scrollMask)
	{
		mHorizontalScrollMask = scrollMask;
		mHorizontalScrollSourceDirty = true;
	}
}

void ScrollOffsetsManager::preFrameUpdate()
{
	// Reset this again on each frame
	for (int index = 0; index < 4; ++index)
		mSets[index].mHorizontalScrollNoRepeat = false;

	// Backup scroll offsets before frame update
	for (int index = 0; index < 4; ++index)
	{
		InterpolatedScrollOffsetSet& interpolatedSet = mInterpolatedSets[index];
		memcpy(interpolatedSet.mLastScrollOffsetsH, mSets[index].mScrollOffsetsH, sizeof(interpolatedSet.mLastScrollOffsetsH));
		memcpy(interpolatedSet.mLastScrollOffsetsV, mSets[index].mScrollOffsetsV, sizeof(interpolatedSet.mLastScrollOffsetsV));
		interpolatedSet.mValid = false;
		interpolatedSet.mHasLastScrollOffsets = true;
	}
}

void ScrollOffsetsManager::postFrameUpdate()
{
}

void ScrollOffsetsManager::resetOverwriteFlags()
{
	bool hadHorizontalOverwrite = false;
	for (int index = 0; index < 4; ++index)
	{
		// Horizontal scrolling
		bool* overwriteFlags = mSets[index].mExplicitOverwriteH;
		for (int k = 0; k < 0x100; ++k)
		{
			hadHorizontalOverwrite |= overwriteFlags[k];
			overwriteFlags[k] = false;
		}

		// Vertical scrolling
		overwriteFlags = mSets[index].mExplicitOverwriteV;
		for (int k = 0; k < 0x20; ++k)
		{
			overwriteFlags[k] = false;
		}
	}
	if (hadHorizontalOverwrite)
		mHorizontalScrollSourceDirty = true;
}

bool ScrollOffsetsManager::getHorizontalScrollNoRepeat(int setIndex) const
{
	RMX_ASSERT(setIndex >= 0 && setIndex < 4, "Invalid scroll offset set index " << setIndex);

	const ScrollOffsetSet& set = mSets[setIndex];
	return set.mHorizontalScrollNoRepeat;
}

void ScrollOffsetsManager::setHorizontalScrollNoRepeat(int setIndex, bool enable)
{
	RMX_ASSERT(setIndex >= 0 && setIndex < 4, "Invalid scroll offset set index " << setIndex);

	ScrollOffsetSet& set = mSets[setIndex];
	set.mHorizontalScrollNoRepeat = enable;
}

void ScrollOffsetsManager::overwriteScrollOffsetH(int setIndex, int index, uint16 value)
{
	RMX_ASSERT(setIndex >= 0 && setIndex < 4, "Invalid scroll offset set index " << setIndex);
	RMX_ASSERT(index >= 0 && index < 0x100, "Invalid index " << index);

	ScrollOffsetSet& set = mSets[setIndex];
	const uint16 sanitized = value & SCROLL_OFFSET_VALUE_BITMASK;
	if (set.mScrollOffsetsH[index] != sanitized)
	{
		set.mScrollOffsetsH[index] = sanitized;
		++set.mChangeCounter;
	}
	set.mExplicitOverwriteH[index] = true;
}

void ScrollOffsetsManager::overwriteScrollOffsetV(int setIndex, int index, uint16 value)
{
	RMX_ASSERT(setIndex >= 0 && setIndex < 4, "Invalid scroll offset set index " << setIndex);
	RMX_ASSERT(index >= 0 && index < 0x20, "Invalid index " << index);

	ScrollOffsetSet& set = mSets[setIndex];
	const uint16 sanitized = value & SCROLL_OFFSET_VALUE_BITMASK;
	if (set.mScrollOffsetsV[index] != sanitized)
	{
		set.mScrollOffsetsV[index] = sanitized;
		++set.mChangeCounter;
	}
	set.mExplicitOverwriteV[index] = true;
}

const uint16* ScrollOffsetsManager::getScrollOffsetsH(int setIndex) const
{
	if (setIndex == 0xff)
	{
		static uint16 emptyScrollOffsetsH[0x100] = { 0 };
		return emptyScrollOffsetsH;
	}

	RMX_ASSERT(setIndex >= 0 && setIndex < 4, "Invalid scroll offset set index " << setIndex);
	if (mInterpolatedSets[setIndex].mValid)
		return mInterpolatedSets[setIndex].mInterpolatedScrollOffsetsH;
	return mSets[setIndex].mScrollOffsetsH;
}

const uint16* ScrollOffsetsManager::getScrollOffsetsV(int setIndex) const
{
	if (setIndex == 0xff)
	{
		static uint16 emptyScrollOffsetsV[0x20] = { 0 };
		return emptyScrollOffsetsV;
	}

	RMX_ASSERT(setIndex >= 0 && setIndex < 4, "Invalid scroll offset set index " << setIndex);
	if (mInterpolatedSets[setIndex].mValid)
		return mInterpolatedSets[setIndex].mInterpolatedScrollOffsetsV;
	return mSets[setIndex].mScrollOffsetsV;
}

uint32 ScrollOffsetsManager::getScrollOffsetsChangeCounter(int setIndex) const
{
	if (setIndex == 0xff)
		return 1;

	RMX_ASSERT(setIndex >= 0 && setIndex < 4, "Invalid scroll offset set index " << setIndex);
	if (mInterpolatedSets[setIndex].mValid)
		return 0x80000000u ^ mInterpolatedSets[setIndex].mChangeCounter;
	return mSets[setIndex].mChangeCounter;
}

void ScrollOffsetsManager::setVerticalScrollOffsetBias(int16 bias)
{
	mVerticalScrollOffsetBias = bias;
}

void ScrollOffsetsManager::serializeSaveState(VectorBinarySerializer& serializer, uint8 formatVersion)
{
	serializer.serializeAs<uint8>(mVerticalScrolling);
	serializer.serialize(mHorizontalScrollMask);

	if (formatVersion >= 2)
	{
		serializer.serialize(mHorizontalScrollTableBase);
		if (serializer.isReading())
		{
			setHorizontalScrollTableBase(mHorizontalScrollTableBase);
			mHorizontalScrollSourceDirty = true;
		}
	}

	if (formatVersion >= 4)
	{
		for (int k = 0; k < 4; ++k)
		{
			ScrollOffsetSet& set = mSets[k];
			for (int i = 0; i < 0x100; ++i)
			{
				serializer.serialize(set.mScrollOffsetsH[i]);
				serializer.serialize(set.mExplicitOverwriteH[i]);
			}
			for (int i = 0; i < 0x20; ++i)
			{
				serializer.serialize(set.mScrollOffsetsV[i]);
				serializer.serialize(set.mExplicitOverwriteV[i]);
			}
			serializer.serialize(set.mHorizontalScrollNoRepeat);
		}

		serializer.serializeAs<int16>(mScrollOffsetW.x);
		serializer.serializeAs<int16>(mScrollOffsetW.y);
		serializer.serialize(mVerticalScrollOffsetBias);
	}

	if (serializer.isReading())
	{
		for (int k = 0; k < 4; ++k)
		{
			++mSets[k].mChangeCounter;
			++mInterpolatedSets[k].mChangeCounter;
			mInterpolatedSets[k].mValid = false;
			mInterpolatedSets[k].mHasLastScrollOffsets = false;
		}
		mHorizontalScrollSourceDirty = true;
	}
}
