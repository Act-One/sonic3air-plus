/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "lemon/pch.h"
#include "lemon/program/Module.h"
#include "lemon/program/ModuleSerializer.h"
#include "lemon/program/GlobalsLookup.h"
#include "lemon/program/function/ScriptFunction.h"

#include <iomanip>


namespace lemon
{
	namespace
	{
		static const std::vector<Function*> EMPTY_FUNCTIONS;

		uint32 addUint8ToDependencyHash(uint32 hash, uint8 value)
		{
			return rmx::addToFNV1a_32(hash, &value, sizeof(value));
		}

		uint32 addUint16ToDependencyHash(uint32 hash, uint16 value)
		{
			const uint8 bytes[2] =
			{
				static_cast<uint8>(value),
				static_cast<uint8>(value >> 8)
			};
			return rmx::addToFNV1a_32(hash, bytes, sizeof(bytes));
		}

		uint32 addUint32ToDependencyHash(uint32 hash, uint32 value)
		{
			const uint8 bytes[4] =
			{
				static_cast<uint8>(value),
				static_cast<uint8>(value >> 8),
				static_cast<uint8>(value >> 16),
				static_cast<uint8>(value >> 24)
			};
			return rmx::addToFNV1a_32(hash, bytes, sizeof(bytes));
		}

		uint32 addUint64ToDependencyHash(uint32 hash, uint64 value)
		{
			const uint8 bytes[8] =
			{
				static_cast<uint8>(value),
				static_cast<uint8>(value >> 8),
				static_cast<uint8>(value >> 16),
				static_cast<uint8>(value >> 24),
				static_cast<uint8>(value >> 32),
				static_cast<uint8>(value >> 40),
				static_cast<uint8>(value >> 48),
				static_cast<uint8>(value >> 56)
			};
			return rmx::addToFNV1a_32(hash, bytes, sizeof(bytes));
		}

		uint32 addFlyweightToDependencyHash(uint32 hash, FlyweightString value)
		{
			return addUint64ToDependencyHash(hash, value.getHash());
		}

		uint32 addDataTypeToDependencyHash(uint32 hash, const DataTypeDefinition* dataType)
		{
			if (nullptr == dataType)
			{
				return addUint16ToDependencyHash(hash, 0xffff);
			}
			hash = addUint16ToDependencyHash(hash, dataType->getID());
			hash = addUint8ToDependencyHash(hash, static_cast<uint8>(dataType->getClass()));
			hash = addUint8ToDependencyHash(hash, static_cast<uint8>(dataType->getBaseType()));
			hash = addUint32ToDependencyHash(hash, static_cast<uint32>(dataType->getBytes()));
			return addFlyweightToDependencyHash(hash, dataType->getName());
		}
	}


	Module::Module(const std::string& name, AppendedInfo* appendedInfo) :
		mModuleName(name),
		mModuleId(rmx::getMurmur2_64(name) & 0xffffffffffff0000ull),
		mAppendedInfo(appendedInfo)
	{
	}

	Module::~Module()
	{
		clear();
		delete mAppendedInfo;
	}

	void Module::clear()
	{
		// Preprocessor definitions
		mPreprocessorDefinitions.clear();

		// Functions
		for (Function* func : mFunctions)
		{
			if (func->isA<NativeFunction>())
				mNativeFunctionPool.destroyObject(func->as<NativeFunction>());
			else
				mScriptFunctionPool.destroyObject(func->as<ScriptFunction>());
		}
		mFunctions.clear();
		mScriptFunctions.clear();
		mNativeFunctionPool.clear();
		mScriptFunctionPool.clear();

		// Callable functions
		mCallableFunctions.clear();

		// Variables
		for (Variable* var : mGlobalVariables)
			delete var;
		mGlobalVariables.clear();

		// Constants
		mConstants.clear();

		// Constant arrays
		mConstantArrays.clear();
		mNumGlobalConstantArrays = 0;

		// Defines
		for (Define* define : mDefines)
		{
			mDefinePool.destroyObject(*define);
		}
		mDefines.clear();
		mDefinePool.clear();

		// String literals
		mStringLiterals.clear();

		// Data types
		for (const DataTypeDefinition* dataType : mDataTypes)
		{
			delete dataType;
		}
		mDataTypes.clear();

		// Clear source file infos
		mSourceFileInfoPool.clear();
		mAllSourceFiles.clear();
		mWarnings.clear();
	}

	void Module::startCompiling(const GlobalsLookup& globalsLookup)
	{
		mScriptFeatureLevel = 1;	// Default for compiled scripts is feature level 1 unless the script explicitly defines feature level 2

		if (mFunctions.empty())
		{
			// It's the same here as for variables, see below
			mFirstFunctionID = globalsLookup.mNextFunctionID;
		}

		if (mGlobalVariables.empty())
		{
			// This here only makes sense if no global variables got set previously
			//  -> That's because the existing global variables are likely part of the globals lookup already
			//  -> Unfortunately, this is only safe to assume for the first module -- TODO: How to handle other cases?
			mFirstVariableID = globalsLookup.mNextVariableID;
		}

		if (mConstantArrays.empty())
		{
			mFirstConstantArrayID = globalsLookup.mNextConstantArrayID;
		}

		if (mDataTypes.empty())
		{
			mFirstDataTypeID = (uint16)globalsLookup.mDataTypes.size();
		}
	}

	void Module::dumpDefinitionsToScriptFile(const std::wstring& filename, bool append)
	{
		String content;
		if (!append)
		{
			content << "// This file was auto-generated";
		}
		content << "\r\n\r\n\r\n";
		content << "// === Module '" << getModuleName() << "' ===";

		// Constants
		if (!mConstants.empty())
		{
			content << "\r\n\r\n";
			content << "// Constants";
			content << "\r\n\r\n";

			for (const Constant* constant : mConstants)
			{
				content << "declare constant " << constant->getDataType()->getName().getString() << " " << constant->getName().getString() << " = ";
				switch (constant->getDataType()->getClass())
				{
					case DataTypeDefinition::Class::INTEGER:
					{
						const uint64 value = constant->getValue().get<uint64>();
						const uint32 minDigits = (uint32)constant->getDataType()->getBytes() * 2;
						content << rmx::hexString(value, minDigits);
						break;
					}

					case DataTypeDefinition::Class::FLOAT:
					{
						std::stringstream str;
						if (constant->getDataType()->getBytes() == 4)
						{
							str << std::setprecision(std::numeric_limits<float>::digits10) << constant->getValue().get<float>() << "f";
						}
						else
						{
							str << std::setprecision(std::numeric_limits<double>::digits10) << constant->getValue().get<double>();
						}
						content << str.str();
						break;
					}

					default:
						break;
				}
				content << "\r\n";
			}
		}

		// Functions
		std::vector<const Function*> currentFunctions;
		for (int pass = 0; pass < 2; ++pass)
		{
			// First pass: Regular functions -- Second pass: Methods
			const bool outputMethods = (pass == 1);

			currentFunctions.clear();
			for (const Function* function : mFunctions)
			{
				const bool isMethod = !function->getContext().isEmpty();
				if (isMethod != outputMethods)
					continue;
				if (function->hasFlag(Function::Flag::EXCLUDE_FROM_DEFINITIONS))
					continue;
				if (function->getName().getString()[0] == '#')	// Exclude hidden built-ins (which can't be accessed by scripts directly anyways)
					continue;

				currentFunctions.push_back(function);
			}

			if (currentFunctions.empty())
				continue;

			content << "\r\n\r\n";
			content << (outputMethods ? "// Methods (to be called on variables directly)" : "// Regular functions");
			content << "\r\n";

			std::string lastPrefix = ".";		// Start with an invalid prefix so that first function will add a line break
			for (const Function* function : currentFunctions)
			{
				// Separate functions with different prefixes
				const size_t dot = function->getName().getString().find_first_of('.');
				std::string_view prefix = (dot == std::string_view::npos) ? std::string_view() : function->getName().getString().substr(0, dot);
				if (prefix != lastPrefix)
				{
					lastPrefix = prefix;
					content << "\r\n";
				}

				// Output signature declaration
				content << "declare function " << function->getReturnType()->getName().getString() << " ";

				size_t firstParam = 0;
				if (outputMethods)
				{
					content << function->getContext().getString() << ":" << function->getName().getString() << "(";
					firstParam = 1;
				}
				else
				{
					content << function->getName().getString() << "(";
				}

				for (size_t i = firstParam; i < function->getParameters().size(); ++i)
				{
					if (i > firstParam)
						content << ", ";
					const Function::Parameter& parameter = function->getParameters()[i];
					content << parameter.mDataType->getName().getString();
					if (parameter.mName.isValid())
						content << " " << parameter.mName.getString();
				}
				content << ")\r\n";
			}
		}

		FileHandle fileHandle;
		fileHandle.open(filename, append ? FILE_ACCESS_APPEND : FILE_ACCESS_WRITE);
		fileHandle.write(&content[0], content.length());
	}

	const SourceFileInfo& Module::addSourceFileInfo(const std::wstring& localPath, const std::wstring& filename)
	{
		SourceFileInfo& sourceFileInfo = mSourceFileInfoPool.createObject();
		sourceFileInfo.mModule = this;
		sourceFileInfo.mFilename = filename;
		sourceFileInfo.mLocalPath = localPath;
		sourceFileInfo.mIndex = mAllSourceFiles.size();
		mAllSourceFiles.push_back(&sourceFileInfo);
		return sourceFileInfo;
	}

	void Module::registerNewPreprocessorDefinitions(PreprocessorDefinitionMap& preprocessorDefinitions)
	{
		for (uint64 hash : preprocessorDefinitions.getNewDefinitions())
		{
			const PreprocessorDefinition* definition = preprocessorDefinitions.getDefinition(hash);
			RMX_ASSERT(nullptr != definition, "Invalid entry in PreprocessorDefinitionMap's new definitions set");
			Constant& constant = addPreprocessorDefinition(definition->mIdentifier, definition->mValue);
			mPreprocessorDefinitions.emplace_back(&constant);
		}
		preprocessorDefinitions.clearNewDefinitions();
	}

	Constant& Module::addPreprocessorDefinition(FlyweightString name, int64 value)
	{
		Constant& constant = mConstantPool.createObject();
		constant.mName = name;
		constant.mDataType = &PredefinedDataTypes::INT_64;
		constant.mValue.set(value);
		mPreprocessorDefinitions.emplace_back(&constant);
		return constant;
	}

	const Function* Module::getFunctionByUniqueId(uint64 uniqueId) const
	{
		RMX_ASSERT(mModuleId == (uniqueId & 0xffffffffffff0000ull), "Function unique ID is not valid for this module");
		return mFunctions[uniqueId & 0xffff];
	}

	ScriptFunction& Module::addScriptFunction(FlyweightString name, const DataTypeDefinition* returnType, const Function::ParameterList& parameters, std::vector<Function::AliasName>* aliasNames)
	{
		ScriptFunction& func = mScriptFunctionPool.createObject();
		func.setModule(*this);
		func.mName = name;
		func.mReturnType = returnType;
		func.mParameters = parameters;
		if (nullptr != aliasNames)
			func.mAliasNames = *aliasNames;

		addFunctionInternal(func);
		mScriptFunctions.push_back(&func);
		return func;
	}

	NativeFunction& Module::addNativeFunction(FlyweightString name, const NativeFunction::FunctionWrapper& functionWrapper, BitFlagSet<Function::Flag> flags)
	{
		NativeFunction& func = mNativeFunctionPool.createObject();
		func.mName = name;
		func.setFunction(functionWrapper);
		func.mFlags = flags;

		addFunctionInternal(func);
		return func;
	}

	NativeFunction& Module::addNativeMethod(FlyweightString context, FlyweightString name, const NativeFunction::FunctionWrapper& functionWrapper, BitFlagSet<Function::Flag> flags)
	{
		NativeFunction& func = mNativeFunctionPool.createObject();
		func.mContext = context;
		func.mName = name;
		func.setFunction(functionWrapper);
		func.mFlags = flags;

		addFunctionInternal(func);
		return func;
	}

	uint32 Module::addOrFindCallableFunctionAddress(const Function& function)
	{
		const uint64 nameHash = function.getName().getHash();
		const uint32 address = ((uint32)nameHash & 0x0fffffff) | 0x10000000;	// Value 1 in the uppermost 4 bits tells us that this is referring to a function
		uint64& ref = mCallableFunctions[address];
		if (ref == 0)
		{
			ref = nameHash;
		}
		else
		{
			RMX_ASSERT(ref == nameHash, "Conflict: Function '" << function.getName() << "' uses the same callable address " << rmx::hexString(address, 8) << " as a different function");
		}
		return address;
	}

	void Module::addFunctionInternal(Function& func)
	{
		RMX_ASSERT(mFunctions.size() < 0x10000, "Too many functions in module");
		func.mID = mFirstFunctionID + (uint32)mFunctions.size();
		if (func.mContext.isEmpty())
			func.mNameAndSignatureHash = func.mName.getHash() + func.getSignatureHash();
		else
			func.mNameAndSignatureHash = func.mContext.getHash() + func.mName.getHash() + func.getSignatureHash();
		mFunctions.push_back(&func);
	}

	GlobalVariable& Module::addGlobalVariable(FlyweightString name, const DataTypeDefinition* dataType)
	{
		// TODO: Add an object pool for this
		GlobalVariable& variable = *new GlobalVariable();
		addGlobalVariable(variable, name, dataType);
		return variable;
	}

	UserDefinedVariable& Module::addUserDefinedVariable(FlyweightString name, const DataTypeDefinition* dataType)
	{
		// TODO: Add an object pool for this
		UserDefinedVariable& variable = *new UserDefinedVariable();
		addGlobalVariable(variable, name, dataType);
		return variable;
	}

	ExternalVariable& Module::addExternalVariable(FlyweightString name, const DataTypeDefinition* dataType, std::function<int64*()>&& accessor)
	{
		// TODO: Add an object pool for this
		ExternalVariable& variable = *new ExternalVariable();
		variable.mAccessor = std::move(accessor);
		addGlobalVariable(variable, name, dataType);
		return variable;
	}

	void Module::addGlobalVariable(Variable& variable, FlyweightString name, const DataTypeDefinition* dataType)
	{
		variable.mName = name;
		variable.mDataType = dataType;
		variable.mID = mFirstVariableID + (uint32)mGlobalVariables.size() + ((uint32)variable.mType << 28);
		mGlobalVariables.emplace_back(&variable);
	}

	LocalVariable& Module::createLocalVariable()
	{
		return mLocalVariablesPool.createObject();
	}

	void Module::destroyLocalVariable(LocalVariable& variable)
	{
		mLocalVariablesPool.destroyObject(variable);
	}

	Constant& Module::addConstant(FlyweightString name, const DataTypeDefinition* dataType, AnyBaseValue value)
	{
		Constant& constant = mConstantPool.createObject();
		constant.mName = name;
		constant.mDataType = dataType;
		constant.mValue = value;
		mConstants.emplace_back(&constant);
		return constant;
	}

	ConstantArray& Module::addConstantArray(FlyweightString name, const DataTypeDefinition* elementDataType, const AnyBaseValue* values, size_t size, bool isGlobalDefinition)
	{
		ConstantArray& constantArray = mConstantArrayPool.createObject();
		constantArray.mName = name;
		constantArray.mElementDataType = elementDataType;
		constantArray.mID = mFirstConstantArrayID + (uint32)mConstantArrays.size();
		if (nullptr != values)
			constantArray.setContent(values, size);
		else if (size > 0)
			constantArray.setSize(size);
		mConstantArrays.emplace_back(&constantArray);

		if (isGlobalDefinition)
		{
			RMX_ASSERT(mNumGlobalConstantArrays + 1 == mConstantArrays.size(), "Gap in global constant arrays list");
			mNumGlobalConstantArrays = mConstantArrays.size();
		}
		return constantArray;
	}

	Define& Module::addDefine(FlyweightString name, const DataTypeDefinition* dataType)
	{
		Define& define = mDefinePool.createObject();
		define.mName = name;
		define.mDataType = dataType;
		mDefines.emplace_back(&define);
		return define;
	}

	void Module::addStringLiteral(FlyweightString str)
	{
		if (mStringLiterals.empty())
			mStringLiterals.reserve(0x100);
		mStringLiterals.push_back(str);
	}

	ArrayDataType& Module::addArrayDataType(const DataTypeDefinition& elementType, size_t arraySize)
	{
		const uint16 id = mFirstDataTypeID + (uint16)mDataTypes.size();
		ArrayDataType* arrayDataType = new ArrayDataType(id, elementType, arraySize);
		mDataTypes.push_back(arrayDataType);
		return *arrayDataType;
	}

	const CustomDataType* Module::addCustomDataType(const char* name, BaseType baseType)
	{
		const uint16 id = mFirstDataTypeID + (uint16)mDataTypes.size();
		CustomDataType* customDataType = new CustomDataType(name, id, baseType);
		mDataTypes.push_back(customDataType);
		return customDataType;
	}

	uint32 Module::buildDependencyHash() const
	{
		uint32 hash = rmx::startFNV1a_32();
		hash = addUint32ToDependencyHash(hash, 0x4c4d4432);	// LMD2: dependency hash includes binding identities, not just counts
		hash = addUint64ToDependencyHash(hash, mModuleId);

		hash = addUint32ToDependencyHash(hash, static_cast<uint32>(mFunctions.size()));
		for (const Function* function : mFunctions)
		{
			hash = addUint8ToDependencyHash(hash, static_cast<uint8>(function->getType()));
			hash = addUint32ToDependencyHash(hash, function->getID());
			hash = addFlyweightToDependencyHash(hash, function->getContext());
			hash = addFlyweightToDependencyHash(hash, function->getName());
			hash = addUint32ToDependencyHash(hash, function->getSignatureHash());
			hash = addDataTypeToDependencyHash(hash, function->getReturnType());
			hash = addUint32ToDependencyHash(hash, static_cast<uint32>(function->getAliasNames().size()));
			for (const Function::AliasName& aliasName : function->getAliasNames())
			{
				hash = addFlyweightToDependencyHash(hash, aliasName.mName);
				hash = addUint8ToDependencyHash(hash, aliasName.mIsDeprecated ? 1 : 0);
			}
			hash = addUint32ToDependencyHash(hash, static_cast<uint32>(function->getParameters().size()));
			for (const Function::Parameter& parameter : function->getParameters())
			{
				hash = addFlyweightToDependencyHash(hash, parameter.mName);
				hash = addDataTypeToDependencyHash(hash, parameter.mDataType);
			}
		}

		hash = addUint32ToDependencyHash(hash, static_cast<uint32>(mGlobalVariables.size()));
		for (const Variable* variable : mGlobalVariables)
		{
			hash = addUint8ToDependencyHash(hash, static_cast<uint8>(variable->getType()));
			hash = addUint32ToDependencyHash(hash, variable->getID());
			hash = addFlyweightToDependencyHash(hash, variable->getName());
			hash = addDataTypeToDependencyHash(hash, variable->getDataType());
		}

		hash = addUint32ToDependencyHash(hash, static_cast<uint32>(mConstants.size()));
		for (const Constant* constant : mConstants)
		{
			hash = addFlyweightToDependencyHash(hash, constant->getName());
			hash = addDataTypeToDependencyHash(hash, constant->getDataType());
		}

		hash = addUint32ToDependencyHash(hash, static_cast<uint32>(mConstantArrays.size()));
		for (const ConstantArray* constantArray : mConstantArrays)
		{
			hash = addFlyweightToDependencyHash(hash, constantArray->getName());
			hash = addDataTypeToDependencyHash(hash, constantArray->getElementDataType());
		}

		hash = addUint32ToDependencyHash(hash, static_cast<uint32>(mDefines.size()));
		for (const Define* define : mDefines)
		{
			hash = addFlyweightToDependencyHash(hash, define->getName());
			hash = addDataTypeToDependencyHash(hash, define->getDataType());
		}

		hash = addUint32ToDependencyHash(hash, static_cast<uint32>(mDataTypes.size()));
		for (const DataTypeDefinition* dataType : mDataTypes)
		{
			hash = addDataTypeToDependencyHash(hash, dataType);
		}

		return (hash != 0) ? hash : 1;
	}

	bool Module::serialize(VectorBinarySerializer& outerSerializer, const GlobalsLookup& globalsLookup, uint32 dependencyHash, uint32 appVersion)
	{
		return ModuleSerializer::serialize(*this, outerSerializer, globalsLookup, dependencyHash, appVersion);
	}

}
