/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_WINDOWS)

#include "oxygen/rendering/d3d11/D3D11Shader.h"

#include <cstring>

#pragma comment(lib, "d3dcompiler.lib")


namespace
{
	inline UINT getCompileFlags()
	{
		UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
	#if defined(DEBUG)
		flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
	#else
		flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
	#endif
		return flags;
	}
}


bool D3D11Shader::initialize(ID3D11Device& device, const char* vertexSource, const char* pixelSource, const D3D11_INPUT_ELEMENT_DESC* inputElements, UINT inputElementCount, const char* debugName, const D3D_SHADER_MACRO* macros)
{
	reset();
	mCompileLog.clear();

	Microsoft::WRL::ComPtr<ID3DBlob> vertexBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> pixelBlob;
	if (!compileStage(vertexSource, "VSMain", "vs_4_0", macros, vertexBlob, debugName))
		return false;
	if (!compileStage(pixelSource, "PSMain", "ps_4_0", macros, pixelBlob, debugName))
		return false;

	HRESULT hr = device.CreateVertexShader(vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &mVertexShader);
	if (FAILED(hr))
	{
		mCompileLog += "CreateVertexShader failed\n";
		return false;
	}

	hr = device.CreatePixelShader(pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(), nullptr, &mPixelShader);
	if (FAILED(hr))
	{
		mCompileLog += "CreatePixelShader failed\n";
		return false;
	}

	hr = device.CreateInputLayout(inputElements, inputElementCount, vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), &mInputLayout);
	if (FAILED(hr))
	{
		mCompileLog += "CreateInputLayout failed\n";
		return false;
	}

	return true;
}

void D3D11Shader::reset()
{
	mVertexShader.Reset();
	mPixelShader.Reset();
	mInputLayout.Reset();
	mCompileLog.clear();
}

void D3D11Shader::bind(ID3D11DeviceContext& context) const
{
	context.IASetInputLayout(mInputLayout.Get());
	context.VSSetShader(mVertexShader.Get(), nullptr, 0);
	context.PSSetShader(mPixelShader.Get(), nullptr, 0);
}

bool D3D11Shader::compileStage(const char* source, const char* entryPoint, const char* target, const D3D_SHADER_MACRO* macros, Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, const char* debugName)
{
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	const HRESULT hr = D3DCompile(source, std::strlen(source), debugName, macros, nullptr, entryPoint, target, getCompileFlags(), 0, &outBlob, &errorBlob);
	if (FAILED(hr))
	{
		if (errorBlob)
			mCompileLog.append((const char*)errorBlob->GetBufferPointer(), errorBlob->GetBufferSize());
		else
			mCompileLog += "D3DCompile failed without an error blob\n";
		return false;
	}
	return true;
}

#endif
