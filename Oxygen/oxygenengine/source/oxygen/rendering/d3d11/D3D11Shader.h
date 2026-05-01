/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(PLATFORM_WINDOWS)

#include <string>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#ifdef min
	#undef min
#endif
#ifdef max
	#undef max
#endif
#ifdef OPAQUE
	#undef OPAQUE
#endif
#ifdef TRANSPARENT
	#undef TRANSPARENT
#endif
#ifdef ERROR
	#undef ERROR
#endif
#ifdef VOID
	#undef VOID
#endif
#ifdef IGNORE
	#undef IGNORE
#endif
#ifdef DUPLICATE
	#undef DUPLICATE
#endif


class D3D11Shader
{
public:
	bool initialize(ID3D11Device& device, const char* vertexSource, const char* pixelSource, const D3D11_INPUT_ELEMENT_DESC* inputElements, UINT inputElementCount, const char* debugName = nullptr, const D3D_SHADER_MACRO* macros = nullptr);
	void reset();

	inline bool isValid() const  { return mVertexShader != nullptr && mPixelShader != nullptr && mInputLayout != nullptr; }
	inline ID3D11InputLayout* getInputLayout() const  { return mInputLayout.Get(); }
	inline const std::string& getCompileLog() const  { return mCompileLog; }

	void bind(ID3D11DeviceContext& context) const;

private:
	bool compileStage(const char* source, const char* entryPoint, const char* target, const D3D_SHADER_MACRO* macros, Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, const char* debugName);

private:
	Microsoft::WRL::ComPtr<ID3D11VertexShader> mVertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> mPixelShader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> mInputLayout;
	std::string mCompileLog;
};

#endif
