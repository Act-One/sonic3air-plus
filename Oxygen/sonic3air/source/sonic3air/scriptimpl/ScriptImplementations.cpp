/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"
#include "sonic3air/scriptimpl/ScriptImplementations.h"

#include "oxygen/application/video/VideoOut.h"
#include "oxygen/rendering/utils/Kosinski.h"
#include "oxygen/simulation/EmulatorInterface.h"
#include "oxygen/simulation/RuntimeEnvironment.h"

#include <lemon/program/function/FunctionWrapper.h>
#include <lemon/program/Module.h>
#include <lemon/program/ModuleBindingsBuilder.h>


namespace s3air
{

	inline EmulatorInterface& getEmulatorInterface()
	{
		return *lemon::Runtime::getActiveEnvironmentSafe<RuntimeEnvironment>().mEmulatorInterface;
	}

	void kosinskiDecompress()
	{
		EmulatorInterface& emulatorInterface = getEmulatorInterface();
		uint32& A0 = emulatorInterface.getRegister(EmulatorInterface::Register::A0);
		uint32& A1 = emulatorInterface.getRegister(EmulatorInterface::Register::A1);

		// TODO: The RAM writes here won't trigger watches, though they should
		uint8* initialPointer = emulatorInterface.getMemoryPointer(A1, false, 1);
		uint8* pointer = initialPointer;
		Kosinski::decompress(emulatorInterface, pointer, A0);

		A1 += (uint32)(pointer - initialPointer);
	}

	void writeScrollOffsetsShared(uint32 value)
	{
		EmulatorInterface& emulatorInterface = getEmulatorInterface();
		const int height = VideoOut::instance().getScreenHeight();
		for (int line = 0; line < height; ++line)
		{
			emulatorInterface.writeMemory32(0xffffe000 + line * 4, value);
		}
	}

	void writeScrollOffsets()
	{
		EmulatorInterface& emulatorInterface = getEmulatorInterface();
		const uint16 foregroundX = -(int16)emulatorInterface.readMemory16(0xffffee80);
		const uint16 backgroundX = -(int16)emulatorInterface.readMemory16(0xffffee8c);
		writeScrollOffsetsShared(((uint32)foregroundX << 16) | backgroundX);
	}

	void writeScrollOffsetsFlipped()
	{
		EmulatorInterface& emulatorInterface = getEmulatorInterface();
		const uint16 foregroundX = -(int16)emulatorInterface.readMemory16(0xffffee80);
		const uint16 backgroundX = -(int16)emulatorInterface.readMemory16(0xffffee8c);
		writeScrollOffsetsShared(((uint32)backgroundX << 16) | foregroundX);
	}

	uint32 putNybbles(uint32 input, uint16 count, uint8 value)
	{
		for (uint16 i = 0; i < count; ++i)
		{
			input = (input << 4) | value;
		}
		return input;
	}

	void nemesisRefillBitstream(EmulatorInterface& emulatorInterface, uint32& sourceAddress, uint16& bitstream, uint16& bitsAvailable)
	{
		if (bitsAvailable < 9)
		{
			bitsAvailable += 8;
			bitstream = (uint16)((bitstream << 8) | emulatorInterface.readMemory8(sourceAddress));
			++sourceAddress;
		}
	}

	void nemesisBuildDecodeTable(EmulatorInterface& emulatorInterface, uint32& sourceAddress, uint16* decodeTable)
	{
		memset(decodeTable, 0, 256 * sizeof(uint16));

		uint8 value = emulatorInterface.readMemory8(sourceAddress);
		++sourceAddress;
		while (value != 0xff)
		{
			uint16 descriptor = value;
			while (true)
			{
				value = emulatorInterface.readMemory8(sourceAddress);
				++sourceAddress;
				if (value >= 0x80)
					break;

				descriptor = (uint16)((descriptor & 0x000f) | (value & 0x70));
				const uint8 codeLength = (uint8)(value & 0x0f);
				const int shift = 9 - (int)codeLength;
				RMX_CHECK(shift > 0, "Invalid Nemesis code length " << (int)codeLength, return);

				const uint16 tableByteOffset = (uint16)(emulatorInterface.readMemory8(sourceAddress) << shift);
				++sourceAddress;

				const uint16 tableIndex = tableByteOffset >> 1;
				const uint16 tableCount = (uint16)(1u << (shift - 1));
				const uint16 tableEntry = (uint16)(((uint16)codeLength << 8) | (descriptor & 0x00ff));
				RMX_CHECK(tableIndex + tableCount <= 256, "Invalid Nemesis decode table write at " << tableIndex << " for " << tableCount << " entries", return);
				for (uint16 i = 0; i < tableCount; ++i)
				{
					decodeTable[tableIndex + i] = tableEntry;
				}
			}
		}
	}

	void nemesisGatherData(EmulatorInterface& emulatorInterface, uint32& sourceAddress, const uint16* decodeTable, uint16& bitstream, uint16& bitsAvailable, int16& runLength, uint8& nibble)
	{
		const uint16 lookup = (uint16)(bitstream >> (bitsAvailable - 8));
		if ((uint8)lookup >= 0xfc)
		{
			bitsAvailable -= 6;
			nemesisRefillBitstream(emulatorInterface, sourceAddress, bitstream, bitsAvailable);

			bitsAvailable -= 7;
			const uint16 value = (uint16)(bitstream >> bitsAvailable);
			runLength = (int16)((value & 0x70) >> 4);
			nibble = (uint8)(value & 0x0f);
			nemesisRefillBitstream(emulatorInterface, sourceAddress, bitstream, bitsAvailable);
		}
		else
		{
			const uint16 entry = decodeTable[(uint8)lookup];
			const int8 codeLength = (int8)(entry >> 8);
			bitsAvailable = (uint16)(bitsAvailable - codeLength);
			nemesisRefillBitstream(emulatorInterface, sourceAddress, bitstream, bitsAvailable);

			const uint8 value = (uint8)entry;
			runLength = (int16)((value & 0xf0) >> 4);
			nibble = (uint8)(value & 0x0f);
		}
	}

	void nemesisWriteLongToVRAM(EmulatorInterface& emulatorInterface, uint16& targetInVRAM, uint32 value)
	{
		emulatorInterface.writeVRam16(targetInVRAM, (uint16)(value >> 16));
		targetInVRAM = (uint16)(targetInVRAM + 2);
		emulatorInterface.writeVRam16(targetInVRAM, (uint16)value);
		targetInVRAM = (uint16)(targetInVRAM + 2);
	}

	void nemesisLoadDataToVRAM(uint32 sourceAddress, uint16 targetInVRAM)
	{
		EmulatorInterface& emulatorInterface = getEmulatorInterface();

		const uint16 header = emulatorInterface.readMemory16(sourceAddress);
		const bool xorOutput = (header & 0x8000) != 0;
		uint16 remainingLongs = (uint16)((header & 0x7fff) * 8);
		sourceAddress += 2;

		uint16 decodeTable[256];
		nemesisBuildDecodeTable(emulatorInterface, sourceAddress, decodeTable);

		uint16 bitstream = emulatorInterface.readMemory16(sourceAddress);
		sourceAddress += 2;
		uint16 bitsAvailable = 0x10;
		uint32 pendingPixels = 0;
		uint32 xorAccumulator = 0;
		uint16 pendingNibbles = 8;
		int16 runLength = -1;
		uint8 nibble = 0;

		while (remainingLongs > 0)
		{
			if (runLength < 0)
			{
				nemesisGatherData(emulatorInterface, sourceAddress, decodeTable, bitstream, bitsAvailable, runLength, nibble);
			}

			const uint16 count = std::min<uint16>(pendingNibbles, (uint16)(runLength + 1));
			pendingPixels = putNybbles(pendingPixels, count, nibble);

			if (count < pendingNibbles)
			{
				pendingNibbles = (uint16)(pendingNibbles - count);
				runLength = -1;
			}
			else
			{
				const uint32 outputPixels = xorOutput ? (xorAccumulator ^ pendingPixels) : pendingPixels;
				xorAccumulator = outputPixels;
				nemesisWriteLongToVRAM(emulatorInterface, targetInVRAM, outputPixels);
				pendingPixels = 0;
				--remainingLongs;
				if (remainingLongs == 0)
					return;

				runLength = (int16)(runLength - pendingNibbles);
				pendingNibbles = 8;
			}
		}
	}


	// TEST!
	void decompressKosinskiData(uint32 sourceAddress, uint16 targetInVRAM)
	{
		// Get the decompressed size
		EmulatorInterface& emulatorInterface = getEmulatorInterface();
		uint16 size = emulatorInterface.readMemory16(sourceAddress);
		if (size == 0xa000)
			size = 0x8000;
		uint32 inputAddress = sourceAddress + 2;

		while (size > 0)
		{
			uint8 buffer[0x1000];
			uint8* pointer = buffer;
			const uint32 moduleSourceAddress = inputAddress;
			Kosinski::decompress(emulatorInterface, pointer, inputAddress);

			const uint16 bytes = std::min<uint16>(size, 0x1000);
			const uint8* src = buffer;
			for (uint16 i = 0; i < bytes; i += 2)
			{
				const uint16 value = ((uint16)src[0] << 8) | (uint16)src[1];
				emulatorInterface.writeVRam16((uint16)(targetInVRAM + i), value);
				src += 2;
			}

			if (size < 0x1000)
				break;

			targetInVRAM += 0x1000;
			size -= bytes;
			// Kosinski modules are padded up to the next 16-byte boundary.
			inputAddress += (moduleSourceAddress - inputAddress) & 0x0f;
		}
	}

}



void ScriptImplementations::registerScriptBindings(lemon::Module& module)
{
	lemon::ModuleBindingsBuilder builder(module);

	const BitFlagSet<lemon::Function::Flag> defaultFlags(lemon::Function::Flag::ALLOW_INLINE_EXECUTION);

	builder.addNativeFunction("Kosinski.Decompress", lemon::wrap(&s3air::kosinskiDecompress), defaultFlags);
	builder.addNativeFunction("WriteScrollOffsets", lemon::wrap(&s3air::writeScrollOffsets), defaultFlags);
	builder.addNativeFunction("WriteScrollOffsetsFlipped", lemon::wrap(&s3air::writeScrollOffsetsFlipped), defaultFlags);

	builder.addNativeFunction("putNybbles", lemon::wrap(&s3air::putNybbles), defaultFlags)
		.setParameters("input", "count", "value");

	builder.addNativeFunction("Nemesis.loadDataToVRAMNative", lemon::wrap(&s3air::nemesisLoadDataToVRAM), defaultFlags)
		.setParameters("sourceInROM", "targetInVRAM");

	builder.addNativeFunction("Kosinski.loadDataToVRAMNative", lemon::wrap(&s3air::decompressKosinskiData), defaultFlags)
		.setParameters("sourceAddress", "targetInVRAM");

	builder.addNativeFunction("uncompressKosinskiData", lemon::wrap(&s3air::decompressKosinskiData), defaultFlags)
		.setParameters("sourceAddress", "targetInVRAM");
}
