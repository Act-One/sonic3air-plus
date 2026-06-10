/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "lemon/pch.h"
#include "lemon/runtime/ControlFlow.h"
#include "lemon/runtime/Runtime.h"
#include "lemon/runtime/RuntimeFunction.h"
#include "lemon/program/Program.h"


namespace lemon
{
	namespace
	{
		FORCE_INLINE uint8* getExternalScalarPointerForType(int64* slot, const DataTypeDefinition& dataType)
		{
			(void)dataType;
			return reinterpret_cast<uint8*>(slot);
		}

		int64 readExternalScalarValue(const ExternalVariable& variable)
		{
			const int64* slot = variable.mAccessor();
			if (nullptr == slot)
				return 0;

			const DataTypeDefinition& dataType = *variable.getDataType();
			const uint8* pointer = getExternalScalarPointerForType(const_cast<int64*>(slot), dataType);
			switch (dataType.getBaseType())
			{
				case BaseType::UINT_8:  { uint8  value = 0; memcpy(&value, pointer, sizeof(value)); return value; }
				case BaseType::INT_8:   { int8   value = 0; memcpy(&value, pointer, sizeof(value)); return value; }
				case BaseType::UINT_16: { uint16 value = 0; memcpy(&value, pointer, sizeof(value)); return value; }
				case BaseType::INT_16:  { int16  value = 0; memcpy(&value, pointer, sizeof(value)); return value; }
				case BaseType::UINT_32: { uint32 value = 0; memcpy(&value, pointer, sizeof(value)); return value; }
				case BaseType::INT_32:  { int32  value = 0; memcpy(&value, pointer, sizeof(value)); return value; }
				case BaseType::UINT_64:
				{
					uint64 value = 0;
					memcpy(&value, pointer, sizeof(value));
					return (int64)value;
				}
				case BaseType::INT_64:
				case BaseType::INT_CONST:
				{
					int64 value = 0;
					memcpy(&value, pointer, sizeof(value));
					return value;
				}
				case BaseType::FLOAT:
				{
					float value = 0.0f;
					memcpy(&value, pointer, sizeof(value));
					return AnyBaseValue(value).get<int64>();
				}
				case BaseType::DOUBLE:
				{
					double value = 0.0;
					memcpy(&value, pointer, sizeof(value));
					return AnyBaseValue(value).get<int64>();
				}
				default:
					return *slot;
			}
		}

		void writeExternalScalarValue(const ExternalVariable& variable, int64 value)
		{
			int64* slot = variable.mAccessor();
			if (nullptr == slot)
				return;

			const DataTypeDefinition& dataType = *variable.getDataType();
			uint8* pointer = getExternalScalarPointerForType(slot, dataType);
			switch (dataType.getBaseType())
			{
				case BaseType::UINT_8:  { const uint8  stored = (uint8)value;  memcpy(pointer, &stored, sizeof(stored)); break; }
				case BaseType::INT_8:   { const int8   stored = (int8)value;   memcpy(pointer, &stored, sizeof(stored)); break; }
				case BaseType::UINT_16: { const uint16 stored = (uint16)value; memcpy(pointer, &stored, sizeof(stored)); break; }
				case BaseType::INT_16:  { const int16  stored = (int16)value;  memcpy(pointer, &stored, sizeof(stored)); break; }
				case BaseType::UINT_32: { const uint32 stored = (uint32)value; memcpy(pointer, &stored, sizeof(stored)); break; }
				case BaseType::INT_32:  { const int32  stored = (int32)value;  memcpy(pointer, &stored, sizeof(stored)); break; }
				case BaseType::UINT_64: { const uint64 stored = (uint64)value; memcpy(pointer, &stored, sizeof(stored)); break; }
				case BaseType::INT_64:
				case BaseType::INT_CONST:
				{
					memcpy(pointer, &value, sizeof(value));
					break;
				}
				case BaseType::FLOAT:
				{
					const float stored = AnyBaseValue((uint64)value).get<float>();
					memcpy(pointer, &stored, sizeof(stored));
					break;
				}
				case BaseType::DOUBLE:
				{
					const double stored = AnyBaseValue((uint64)value).get<double>();
					memcpy(pointer, &stored, sizeof(stored));
					break;
				}
				default:
					*slot = value;
					break;
			}
		}

		int64 readLocalScalarValue(const ControlFlow& controlFlow, const LocalVariable& variable)
		{
			const DataTypeDefinition& dataType = *variable.getDataType();
			const size_t baseBytes = BaseTypeHelper::getSizeOfBaseType(dataType.getBaseType());
			if (baseBytes == 0 || dataType.getBytes() != baseBytes)
			{
				return controlFlow.readLocalVariable<int64>(variable.getLocalMemoryOffset());
			}

			const size_t offset = variable.getLocalMemoryOffset();
			switch (dataType.getBaseType())
			{
				case BaseType::UINT_8:  return controlFlow.readLocalVariable<uint8>(offset);
				case BaseType::INT_8:   return controlFlow.readLocalVariable<int8>(offset);
				case BaseType::UINT_16: return controlFlow.readLocalVariable<uint16>(offset);
				case BaseType::INT_16:  return controlFlow.readLocalVariable<int16>(offset);
				case BaseType::UINT_32: return controlFlow.readLocalVariable<uint32>(offset);
				case BaseType::INT_32:  return controlFlow.readLocalVariable<int32>(offset);
				case BaseType::UINT_64: return (int64)controlFlow.readLocalVariable<uint64>(offset);
				case BaseType::INT_64:
				case BaseType::INT_CONST:
					return controlFlow.readLocalVariable<int64>(offset);
				case BaseType::FLOAT:
					return AnyBaseValue(controlFlow.readLocalVariable<float>(offset)).get<int64>();
				case BaseType::DOUBLE:
					return AnyBaseValue(controlFlow.readLocalVariable<double>(offset)).get<int64>();
				default:
					return 0;
			}
		}

		void writeLocalScalarValue(const ControlFlow& controlFlow, const LocalVariable& variable, int64 value)
		{
			const DataTypeDefinition& dataType = *variable.getDataType();
			const size_t baseBytes = BaseTypeHelper::getSizeOfBaseType(dataType.getBaseType());
			if (baseBytes == 0 || dataType.getBytes() != baseBytes)
			{
				controlFlow.writeLocalVariable(variable.getLocalMemoryOffset(), value);
				return;
			}

			const size_t offset = variable.getLocalMemoryOffset();
			switch (dataType.getBaseType())
			{
				case BaseType::UINT_8:  controlFlow.writeLocalVariable<uint8>(offset, (uint8)value); break;
				case BaseType::INT_8:   controlFlow.writeLocalVariable<int8>(offset, (int8)value); break;
				case BaseType::UINT_16: controlFlow.writeLocalVariable<uint16>(offset, (uint16)value); break;
				case BaseType::INT_16:  controlFlow.writeLocalVariable<int16>(offset, (int16)value); break;
				case BaseType::UINT_32: controlFlow.writeLocalVariable<uint32>(offset, (uint32)value); break;
				case BaseType::INT_32:  controlFlow.writeLocalVariable<int32>(offset, (int32)value); break;
				case BaseType::UINT_64: controlFlow.writeLocalVariable<uint64>(offset, (uint64)value); break;
				case BaseType::INT_64:
				case BaseType::INT_CONST:
					controlFlow.writeLocalVariable<int64>(offset, value);
					break;
				case BaseType::FLOAT:
					controlFlow.writeLocalVariable<float>(offset, AnyBaseValue((uint64)value).get<float>());
					break;
				case BaseType::DOUBLE:
					controlFlow.writeLocalVariable<double>(offset, AnyBaseValue((uint64)value).get<double>());
					break;
				default:
					break;
			}
		}
	}

	ControlFlow::ControlFlow(Runtime& runtime) :
		mRuntime(runtime)
	{
	}

	void ControlFlow::reset()
	{
		mCallStack.clear();
		memset(mValueStackBuffer, 0, sizeof(mValueStackBuffer));
		mValueStackStart = &mValueStackBuffer[VALUE_STACK_FIRST_INDEX];
		mValueStackPtr   = &mValueStackBuffer[VALUE_STACK_FIRST_INDEX];

		memset(mLocalVariablesBuffer, 0, sizeof(mLocalVariablesBuffer));
		mLocalVariablesSize = 0;
	}

	void ControlFlow::getCallStack(std::vector<Location>& outLocations) const
	{
		outLocations.resize(getCallStack().count);
		for (size_t i = 0; i < getCallStack().count; ++i)
		{
			const State& state = getCallStack()[i];
			if (nullptr != state.mRuntimeFunction)
			{
				outLocations[i].mFunction = state.mRuntimeFunction->mFunction;
				outLocations[i].mProgramCounter = state.mRuntimeFunction->translateFromRuntimeProgramCounter(state.mProgramCounter);
			}
			else
			{
				outLocations[i].mFunction = nullptr;
				outLocations[i].mProgramCounter = 0;
			}
		}
	}

	void ControlFlow::getRecentExecutionLocation(Location& outLocation) const
	{
		if (!mCallStack.empty())
		{
			const State& state = mCallStack.back();
			outLocation.mFunction = state.mRuntimeFunction->mFunction;
			outLocation.mProgramCounter = state.mRuntimeFunction->translateFromRuntimeProgramCounter(state.mProgramCounter);
		}
		else
		{
			outLocation.mFunction = nullptr;
			outLocation.mProgramCounter = 0;
		}
	}

	void ControlFlow::getCurrentExecutionLocation(Location& outLocation) const
	{
		if (!mCallStack.empty() && &mRuntime == Runtime::getActiveRuntime())
		{
			const State& state = mCallStack.back();
			outLocation.mFunction = state.mRuntimeFunction->mFunction;

			// Don't use the state's program counter, as it can be slightly out-dated
			//  -> Instead, get the actual program counter directly from the runtime
			//  -> However, this is only valid during actual code execution
			outLocation.mProgramCounter = state.mRuntimeFunction->translateFromRuntimeProgramCounter((const uint8*)mRuntime.getCurrentOpcode());
		}
		else
		{
			outLocation.mFunction = nullptr;
			outLocation.mProgramCounter = 0;
		}
	}

	const ScriptFunction* ControlFlow::getCurrentFunction() const
	{
		if (mCallStack.empty())
			return nullptr;

		const RuntimeFunction* runtimeFunction = mCallStack.back().mRuntimeFunction;
		return (nullptr != runtimeFunction) ? runtimeFunction->mFunction : nullptr;
	}

	const Module* ControlFlow::getCurrentModule() const
	{
		const ScriptFunction* function = getCurrentFunction();
		return (nullptr != function) ? &function->getModule() : nullptr;
	}

	int64 ControlFlow::readVariableGeneric(uint32 variableId)
	{
		const Variable::Type type = (Variable::Type)(variableId >> 28);
		switch (type)
		{
			default:
			case Variable::Type::LOCAL:
			{
				const LocalVariable& variable = getCurrentFunction()->getLocalVariableByID(variableId);
				return readLocalScalarValue(*this, variable);
			}

			case Variable::Type::GLOBAL:
			{
				const GlobalVariable& variable = mProgram->getGlobalVariableByID(variableId).as<GlobalVariable>();
				return getRuntime().readGlobalVariableValue(variable);
			}

			case Variable::Type::USER:
			{
				const UserDefinedVariable& variable = mProgram->getGlobalVariableByID(variableId).as<UserDefinedVariable>();
				variable.mGetter(*this);		// This is supposed to write a value to the value stack
				return popValueStack<int64>();
			}

			case Variable::Type::EXTERNAL:
			{
				const ExternalVariable& variable = mProgram->getGlobalVariableByID(variableId).as<ExternalVariable>();
				return readExternalScalarValue(variable);
			}
		}
	}

	void ControlFlow::writeVariableGeneric(uint32 variableId, int64 value)
	{
		const Variable::Type type = (Variable::Type)(variableId >> 28);
		switch (type)
		{
			default:
			case Variable::Type::LOCAL:
			{
				const LocalVariable& variable = getCurrentFunction()->getLocalVariableByID(variableId);
				writeLocalScalarValue(*this, variable, value);
				break;
			}

			case Variable::Type::GLOBAL:
			{
				const GlobalVariable& variable = mProgram->getGlobalVariableByID(variableId).as<GlobalVariable>();
				getRuntime().writeGlobalVariableValue(variable, value);
				break;
			}

			case Variable::Type::USER:
			{
				const UserDefinedVariable& variable = mProgram->getGlobalVariableByID(variableId).as<UserDefinedVariable>();
				pushValueStack(value);
				variable.mSetter(*this);		// This is supposed to read a value from the value stack
				break;
			}

			case Variable::Type::EXTERNAL:
			{
				const ExternalVariable& variable = mProgram->getGlobalVariableByID(variableId).as<ExternalVariable>();
				writeExternalScalarValue(variable, value);
				break;
			}
		}
	}

	uint8* ControlFlow::accessVariableGeneric(uint32 variableId)
	{
		const Variable::Type type = (Variable::Type)(variableId >> 28);
		switch (type)
		{
			case Variable::Type::LOCAL:
			{
				// This requires local variables with different sizes, currently they're all just one uint64 each
				//return reinterpret_cast<uint8*>(mCurrentLocalVariables[variableId]);

				RMX_ASSERT(false, "Unhandled variable type for 'accessVariableGeneric'");
				return nullptr;
			}

			case Variable::Type::GLOBAL:
			{
				const GlobalVariable& variable = mProgram->getGlobalVariableByID(variableId).as<GlobalVariable>();
				return getRuntime().accessGlobalVariableStorage(variable);
			}

			default:
			{
				RMX_ASSERT(false, "Unhandled variable type for 'accessVariableGeneric'");
				return nullptr;
			}
		}
	}

}
