/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "lemon/pch.h"
#include "lemon/runtime/RuntimeFunction.h"
#include "lemon/runtime/Runtime.h"
#include "lemon/runtime/OpcodeExecUtils.h"
#include "lemon/runtime/OpcodeProcessor.h"
#include "lemon/runtime/provider/DefaultOpcodeProvider.h"
#include "lemon/runtime/provider/OptimizedOpcodeProvider.h"
#include "lemon/runtime/provider/NativizedOpcodeProvider.h"
#include "lemon/program/Program.h"

namespace lemon
{
	namespace
	{
	#if defined(PLATFORM_WIIU)
		static constexpr bool ENABLE_WIIU_LEMON_RUNTIME_FUNCTION_TRACE = false;
	#endif

		static constexpr size_t RUNTIME_OPCODE_ALIGNMENT = RuntimeOpcode::PARAMETER_ALIGNMENT;

		static constexpr size_t alignRuntimeOpcodeOffset(size_t value)
		{
			return (value + RUNTIME_OPCODE_ALIGNMENT - 1) & ~(RUNTIME_OPCODE_ALIGNMENT - 1);
		}
	}

	RuntimeOpcodeBuffer::~RuntimeOpcodeBuffer()
	{
		if (mSelfManagedBuffer)
			delete[] mBuffer;
	}

	void RuntimeOpcodeBuffer::clear()
	{
		mSize = 0;
		mOpcodePointers.clear();
		// Not touching the reserved memory here
	}

	void RuntimeOpcodeBuffer::reserveForOpcodes(size_t numOpcodes)
	{
		const size_t memoryRequired = numOpcodes * alignRuntimeOpcodeOffset(RuntimeOpcode::PARAMETER_OFFSET + 16);	// Estimate for maximum size
		if (memoryRequired > mReserved)
		{
			if (mSelfManagedBuffer)
				delete[] mBuffer;

			mReserved = memoryRequired;
			mBuffer = new uint8[mReserved];
			mSelfManagedBuffer = true;
		}
	}

	RuntimeOpcode& RuntimeOpcodeBuffer::addOpcode(size_t parameterSize)
	{
		mSize = alignRuntimeOpcodeOffset(mSize);
		const size_t size = alignRuntimeOpcodeOffset(RuntimeOpcode::PARAMETER_OFFSET + parameterSize);
		RMX_ASSERT(mSize + size <= mReserved, "Exceeding reserved size of runtime opcode buffer");
		RMX_ASSERT(size <= 0xc0, "Got large parameter size of " << parameterSize << " bytes");		// Actual hard limit is 0xff, but everything larger than 0xc0 is suspicious and a hint that the limit might be too low

		uint8* opcodePointer = mBuffer + mSize;
		mSize += size;
		mOpcodePointers.push_back((RuntimeOpcode*)opcodePointer);

		// Setup some default values
		memset(opcodePointer, 0, size);
		RuntimeOpcode& runtimeOpcode = *(RuntimeOpcode*)opcodePointer;
		runtimeOpcode.mExecFunc = nullptr;
		runtimeOpcode.mOpcodeType = Opcode::Type::NOP;
		runtimeOpcode.mSize = (uint8)size;
		runtimeOpcode.mFlags.clearAll();
		runtimeOpcode.mSuccessiveHandledOpcodes = 1;
		return runtimeOpcode;
	}

	void RuntimeOpcodeBuffer::copyFrom(const RuntimeOpcodeBuffer& other, rmx::OneTimeAllocPool& memoryPool)
	{
		if (mSelfManagedBuffer)
			delete[] mBuffer;

		mBuffer = memoryPool.allocateMemory(other.mSize);
		mSelfManagedBuffer = false;
		mSize = other.mSize;
		mReserved = other.mSize;
		memcpy(mBuffer, other.mBuffer, mSize);

		mOpcodePointers.resize(other.mOpcodePointers.size());
		for (size_t k = 0; k < mOpcodePointers.size(); ++k)
		{
			const size_t offset = (size_t)((uint8*)other.mOpcodePointers[k] - other.mBuffer);
			mOpcodePointers[k] = (RuntimeOpcode*)(mBuffer + offset);
		}
	}


	bool RuntimeFunction::build(Runtime& runtime)
	{
		// First check if it is built already
		if (!mRuntimeOpcodeBuffer.empty() || mFunction->mOpcodes.empty())
			return true;

#if defined(PLATFORM_WIIU)
		static int sBuildLogCount = 0;
		static int sBuildDetailLogCount = 0;
		const bool logBuild = ENABLE_WIIU_LEMON_RUNTIME_FUNCTION_TRACE && (sBuildLogCount < 256);
		const bool logDetail = logBuild && (sBuildDetailLogCount < 160);
		if (logBuild)
		{
			RMX_LOG_INFO("[WiiU Lemon] RuntimeFunction::build begin function=" << mFunction->getName() << " opcodes=" << mFunction->mOpcodes.size() << " opt=" << runtime.getProgram().getOptimizationLevel() << " nativeProvider=" << (nullptr != runtime.getProgram().mNativizedOpcodeProvider));
			++sBuildLogCount;
		}
#endif

		// Create the runtime opcodes
		{
			// Initialize runtime opcodes now that they are needed
			const std::vector<Opcode>& opcodes = mFunction->mOpcodes;
			const size_t numOpcodes = opcodes.size();

			// Preparation: Build some useful information about opcodes
			static std::vector<OpcodeProcessor::OpcodeData> opcodeData;
#if defined(PLATFORM_WIIU)
			if (logDetail)
			{
				RMX_LOG_INFO("[WiiU Lemon] RuntimeFunction::build opcodeData begin function=" << mFunction->getName());
				++sBuildDetailLogCount;
			}
#endif
			OpcodeProcessor::buildOpcodeData(opcodeData, *mFunction);
#if defined(PLATFORM_WIIU)
			if (logDetail)
			{
				RMX_LOG_INFO("[WiiU Lemon] RuntimeFunction::build opcodeData end function=" << mFunction->getName());
				++sBuildDetailLogCount;
			}
#endif

			// Using a static buffer as temporary buffer before knowing the final size
			static RuntimeOpcodeBuffer tempBuffer;
			tempBuffer.clear();
#if defined(PLATFORM_WIIU)
			if (logDetail)
			{
				RMX_LOG_INFO("[WiiU Lemon] RuntimeFunction::build reserve begin function=" << mFunction->getName() << " numOpcodes=" << numOpcodes);
				++sBuildDetailLogCount;
			}
#endif
			tempBuffer.reserveForOpcodes(numOpcodes);
#if defined(PLATFORM_WIIU)
			if (logDetail)
			{
				RMX_LOG_INFO("[WiiU Lemon] RuntimeFunction::build reserve end function=" << mFunction->getName());
				++sBuildDetailLogCount;
			}
#endif

			mProgramCounterByOpcodeIndex.resize(numOpcodes, 0xffffffff);

			try
			{
				// Let the opcode providers create runtime opcodes
				//  -> They may choose to merge more than one opcode into a runtime opcode, where that's feasible
				for (size_t i = 0; i < numOpcodes; )
				{
					const size_t opcodePointerIndex = tempBuffer.getOpcodePointers().size();
					int numOpcodesConsumed = 1;
#if defined(PLATFORM_WIIU)
					if (logDetail)
					{
						const Opcode& opcode = opcodes[i];
						RMX_LOG_INFO("[WiiU Lemon] RuntimeFunction::build emit begin function=" << mFunction->getName()
							<< " index=" << i
							<< " type=" << (int)opcode.mType
							<< " dataType=0x" << rmx::hexString((uint8)opcode.mDataType, 2)
							<< " param=0x" << rmx::hexString((uint64)opcode.mParameter, 16)
							<< " remain=" << opcodeData[i].mRemainingSequenceLength);
						++sBuildDetailLogCount;
					}
#endif
					createRuntimeOpcode(tempBuffer, &opcodes[i], opcodeData[i].mRemainingSequenceLength, (int)i, numOpcodesConsumed, runtime);
#if defined(PLATFORM_WIIU)
					if (logDetail)
					{
						RMX_LOG_INFO("[WiiU Lemon] RuntimeFunction::build emit end function=" << mFunction->getName()
							<< " index=" << i
							<< " consumed=" << numOpcodesConsumed
							<< " tempBytes=" << tempBuffer.size()
							<< " runtimeOpcodes=" << tempBuffer.getOpcodePointers().size());
						++sBuildDetailLogCount;
					}
#endif

					RMX_ASSERT(opcodePointerIndex < tempBuffer.getOpcodePointers().size(), "Runtime opcode provider did not emit an opcode");
					const size_t start = (size_t)((const uint8*)tempBuffer.getOpcodePointers()[opcodePointerIndex] - tempBuffer.getStart());
					for (int k = 0; k < numOpcodesConsumed; ++k)
					{
						mProgramCounterByOpcodeIndex[k + i] = start;
					}
					i += numOpcodesConsumed;
				}
			}
			catch (const std::exception& e)
			{
				RMX_ERROR("Build of lemonscript runtime function \"" << mFunction->getName() << "\" failed due to error: " << e.what(), );
				return false;
			}

			// Copy the runtime opcodes over into the actual opcode buffer for this function
#if defined(PLATFORM_WIIU)
			if (logDetail)
			{
				RMX_LOG_INFO("[WiiU Lemon] RuntimeFunction::build copy begin function=" << mFunction->getName() << " tempBytes=" << tempBuffer.size());
				++sBuildDetailLogCount;
			}
#endif
			mRuntimeOpcodeBuffer.copyFrom(tempBuffer, runtime.mRuntimeOpcodesPool);
#if defined(PLATFORM_WIIU)
			if (logDetail)
			{
				RMX_LOG_INFO("[WiiU Lemon] RuntimeFunction::build copy end function=" << mFunction->getName() << " bytes=" << mRuntimeOpcodeBuffer.size() << " first=" << (const void*)mRuntimeOpcodeBuffer.getStart());
				++sBuildDetailLogCount;
			}
#endif
		}

		// Post-processing
		{
#if defined(PLATFORM_WIIU)
			if (logDetail)
			{
				RMX_LOG_INFO("[WiiU Lemon] RuntimeFunction::build post begin function=" << mFunction->getName());
				++sBuildDetailLogCount;
			}
#endif
			// Translation of jumps
			const std::vector<RuntimeOpcode*>& runtimeOpcodePointers = mRuntimeOpcodeBuffer.getOpcodePointers();
			for (size_t i = 0; i < runtimeOpcodePointers.size(); ++i)
			{
				RuntimeOpcode& runtimeOpcode = *runtimeOpcodePointers[i];
				if (runtimeOpcode.mOpcodeType == Opcode::Type::JUMP || runtimeOpcode.mOpcodeType == Opcode::Type::JUMP_SWITCH)
				{
					runtimeOpcode.setParameter(translateJumpTarget((uint32)runtimeOpcode.getParameter<uint64>()));
				}
				else if (runtimeOpcode.mOpcodeType == Opcode::Type::JUMP_CONDITIONAL)
				{
					runtimeOpcode.setParameter(translateJumpTarget((uint32)runtimeOpcode.getParameter<uint64>(0)), 0);
				#ifdef USE_JUMP_CONDITIONAL_RUNTIME_EXEC
					runtimeOpcode.setParameter(translateJumpTarget(runtimeOpcode.getParameter<uint32>(8)), 8);
				#endif
				}
			}

			// Update successive handled opcode counts
			uint8 sequenceLength = 0;
			for (int i = (int)runtimeOpcodePointers.size()-1; i >= 0; --i)
			{
				if (runtimeOpcodePointers[i]->mSuccessiveHandledOpcodes == 0)
				{
					sequenceLength = 0;
				}
				else if (runtimeOpcodePointers[i]->mOpcodeType == Opcode::Type::JUMP_CONDITIONAL)
				{
					sequenceLength = 1;		// Sequence needs to stop after executing the conditional jump
				}
				else
				{
					if (sequenceLength < 0xff)
						++sequenceLength;
				}
				runtimeOpcodePointers[i]->mSuccessiveHandledOpcodes = sequenceLength;
			}

			// Fill in the pointers to the next opcode
			for (size_t i = 0; i < runtimeOpcodePointers.size() - 1; ++i)
			{
				RuntimeOpcode& runtimeOpcode = *runtimeOpcodePointers[i];
				runtimeOpcode.mNext = (RuntimeOpcode*)((uint8*)&runtimeOpcode + (size_t)runtimeOpcode.mSize);

				for (int runs = 0; runs < 5; ++runs)
				{
					if (runtimeOpcode.mNext->mOpcodeType != Opcode::Type::JUMP)
						break;

					// Take a shortcut by skipping the jump opcode and directly pointing to its target as next opcode
					//  -> But only do that for jumps forward, otherwise it's possible that script execution can get stuck in an infinite loop
					//  -> That's because counted steps are only checked in actually executed jumps, but not in those that we optimize away here
					RuntimeOpcode* targetPointer = runtimeOpcode.mNext->getParameter<RuntimeOpcode*>();
					RuntimeOpcode* ownPointer = &runtimeOpcode;
					if (targetPointer <= ownPointer)
						break;

					runtimeOpcode.mNext = targetPointer;
					// Continue the for-loop, in case mNext is yet another jump that can be resolved by a shortcut
				}
			}

#if defined(PLATFORM_WII)
			if (!runtimeOpcodePointers.empty())
			{
				const uint8* bufferStart = mRuntimeOpcodeBuffer.getStart();
				const uint8* bufferEnd = mRuntimeOpcodeBuffer.getEnd();
				for (size_t i = 0; i + 1 < runtimeOpcodePointers.size(); ++i)
				{
					RuntimeOpcode& runtimeOpcode = *runtimeOpcodePointers[i];
					const uint8* next = reinterpret_cast<const uint8*>(runtimeOpcode.mNext);
					if (nullptr != bufferStart && bufferStart < bufferEnd && (next < bufferStart || next >= bufferEnd))
					{
						const uint8* fallback = reinterpret_cast<const uint8*>(&runtimeOpcode) + runtimeOpcode.mSize;
						if (runtimeOpcode.mSize > 0 && fallback >= bufferStart && fallback < bufferEnd)
							runtimeOpcode.mNext = reinterpret_cast<RuntimeOpcode*>(const_cast<uint8*>(fallback));
					}
				}
			}
#endif
#if defined(PLATFORM_WIIU)
			if (logDetail)
			{
				RMX_LOG_INFO("[WiiU Lemon] RuntimeFunction::build post end function=" << mFunction->getName());
				++sBuildDetailLogCount;
			}
#endif
		}

#if defined(PLATFORM_WIIU)
		if (logBuild)
		{
			RMX_LOG_INFO("[WiiU Lemon] RuntimeFunction::build end function=" << mFunction->getName() << " bytes=" << mRuntimeOpcodeBuffer.size() << " first=" << (const void*)mRuntimeOpcodeBuffer.getStart());
		}
#endif
		return true;
	}

	size_t RuntimeFunction::translateFromRuntimeProgramCounter(const uint8* runtimeProgramCounter) const
	{
		const int result = translateFromRuntimeProgramCounterOptional(runtimeProgramCounter);
		RMX_ASSERT(result >= 0, "Program counter couldn't be translated");
		return (size_t)std::max(result, 0);
	}

	int RuntimeFunction::translateFromRuntimeProgramCounterOptional(const uint8* runtimeProgramCounter) const
	{
		// Binary search
		if (mProgramCounterByOpcodeIndex.empty())
			return -1;

		const size_t programCounter = (size_t)(runtimeProgramCounter - getFirstRuntimeOpcode());
		size_t minimum = 0;
		size_t maximum = mProgramCounterByOpcodeIndex.size() - 1;
		while (minimum <= maximum)
		{
			const size_t median = (minimum + maximum) / 2;
			if (programCounter < mProgramCounterByOpcodeIndex[median])
			{
				maximum = median - 1;
			}
			else if (programCounter > mProgramCounterByOpcodeIndex[median])
			{
				minimum = median + 1;
			}
			else
			{
				return (int)median;
			}
		}
		return -1;
	}

	const uint8* RuntimeFunction::translateToRuntimeProgramCounter(size_t originalProgramCounter) const
	{
		const size_t index = (originalProgramCounter < mProgramCounterByOpcodeIndex.size()) ? mProgramCounterByOpcodeIndex[originalProgramCounter] : 0;
		return &mRuntimeOpcodeBuffer[index];
	}

	void RuntimeFunction::createRuntimeOpcode(RuntimeOpcodeBuffer& buffer, const Opcode* opcodes, int numOpcodesAvailable, int firstOpcodeIndex, int& outNumOpcodesConsumed, const Runtime& runtime)
	{
		const Program& program = runtime.getProgram();
		if (program.getOptimizationLevel() >= 2 && nullptr != program.mNativizedOpcodeProvider)
		{
			const bool success = program.mNativizedOpcodeProvider->buildRuntimeOpcode(buffer, opcodes, numOpcodesAvailable, firstOpcodeIndex, outNumOpcodesConsumed, runtime, *mFunction);
			if (success)
				return;
		}

		// Runtime opcode generation by merging multiple opcodes where possible
		if (program.getOptimizationLevel() >= 1)
		{
			const bool success = OptimizedOpcodeProvider::buildRuntimeOpcodeStatic(buffer, opcodes, numOpcodesAvailable, firstOpcodeIndex, outNumOpcodesConsumed, runtime, *mFunction);
			if (success)
				return;
		}

		// Fallback: Direct translation of one opcode to the respective runtime opcode
		DefaultOpcodeProvider::buildRuntimeOpcodeStatic(buffer, opcodes, numOpcodesAvailable, firstOpcodeIndex, outNumOpcodesConsumed, runtime, *mFunction);
	}

	const uint8* RuntimeFunction::translateJumpTarget(uint32 targetOpcodeIndex) const
	{
		const size_t oldJumpTarget = (size_t)targetOpcodeIndex;
		if (oldJumpTarget < mProgramCounterByOpcodeIndex.size())
		{
			return mRuntimeOpcodeBuffer.getStart() + mProgramCounterByOpcodeIndex[oldJumpTarget];
		}
		else
		{
			RMX_ASSERT(mRuntimeOpcodeBuffer.getOpcodePointers().back()->mOpcodeType == Opcode::Type::RETURN, "Functions must end with a return in all cases");
			return (const uint8*)mRuntimeOpcodeBuffer.getOpcodePointers().back();
		}
	}

}
