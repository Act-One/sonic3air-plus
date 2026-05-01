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

#include "oxygen/drawing/d3d11/D3D11DrawerResources.h"

#include "oxygen/application/Configuration.h"
#include "oxygen/drawing/d3d11/D3D11Upscaler.h"
#include "oxygen/drawing/d3d11/D3D11SpriteTextureManager.h"
#include "oxygen/helper/FileHelper.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/rendering/d3d11/D3D11Shader.h"
#include "oxygen/rendering/parts/palette/PaletteManager.h"
#include "../../../../../sonic3air/build/_vstudio/UwpBootTrace.h"
#include "SDL_syswm.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>


namespace
{
	const Vec2i PALETTE_TEXTURE_SIZE(256, PaletteManager::MAIN_PALETTE_SIZE / 256 * 2);

	bool checkAllowTearingSupport(IDXGIFactory2& dxgiFactory)
	{
		Microsoft::WRL::ComPtr<IDXGIFactory5> dxgiFactory5;
		if (FAILED(dxgiFactory.QueryInterface(IID_PPV_ARGS(&dxgiFactory5))))
			return false;

		BOOL allowTearing = FALSE;
		return SUCCEEDED(dxgiFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))) && allowTearing == TRUE;
	}

	struct SimpleRectConstants
	{
		Vec4f mTransform;
		Vec4f mTintColor = Color::WHITE;
		Vec4f mAddedColor = Color::TRANSPARENT;
		int32 mTextureSize[2] = { 0, 0 };
		int32 mAlphaTest = 0;
		int32 mShadowHighlightMode = 0;
	};

	struct UpscalerConstants
	{
		int32 mGameResolution[2] = { 0, 0 };
		int32 mOutputSize[2] = { 0, 0 };
		float mPixelFactor = 1.0f;
		float mScanlinesIntensity = 0.0f;
		float mPadding0[2] = { 0.0f, 0.0f };
	};

	enum class UpscalerType
	{
		DEFAULT = 0,
		SOFT,
		XBRZ,
		HQX
	};

	inline UpscalerType getUpscalerType()
	{
		const int filtering = Configuration::instance().mFiltering;
		const int scanlines = Configuration::instance().mScanlines;
		if (scanlines > 0 && filtering < 3)
			return UpscalerType::SOFT;

		switch (filtering)
		{
			default:
			case 0:	return UpscalerType::DEFAULT;
			case 1:
			case 2:	return UpscalerType::SOFT;
			case 3:	return UpscalerType::XBRZ;
			case 4:
			case 5:
			case 6:	return UpscalerType::HQX;
		}
	}

	inline std::wstring getShaderAssetPath(const wchar_t* filename)
	{
		std::wstring path = Configuration::instance().mEngineDataPath;
		rmx::FileIO::normalizePath(path, true);
		path += filename;
		return path;
	}

	inline Vec4f makeTransform(const Recti& rect, const Vec2i& targetSize)
	{
		Vec4f transform;
		transform.x = -1.0f + (float)rect.x * 2.0f / (float)targetSize.x;
		transform.y =  1.0f - (float)rect.y * 2.0f / (float)targetSize.y;
		transform.z = (float)rect.width * 2.0f / (float)targetSize.x;
		transform.w = -(float)rect.height * 2.0f / (float)targetSize.y;
		return transform;
	}

	inline bool ensureBuffer(ID3D11Device& device, Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer, UINT size, UINT bindFlags, D3D11_USAGE usage, UINT cpuAccessFlags)
	{
		if (buffer != nullptr)
			return true;

		const UINT alignedSize = ((bindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0) ? ((size + 15u) & ~15u) : size;
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = alignedSize;
		desc.BindFlags = bindFlags;
		desc.Usage = usage;
		desc.CPUAccessFlags = cpuAccessFlags;
		return SUCCEEDED(device.CreateBuffer(&desc, nullptr, &buffer));
	}

	inline bool ensureDynamicVertexBuffer(ID3D11Device& device, Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer, size_t& capacityBytes, size_t requiredBytes)
	{
		if (buffer != nullptr && capacityBytes >= requiredBytes)
			return true;

		capacityBytes = std::max(requiredBytes, capacityBytes == 0 ? (size_t)1024 : capacityBytes * 2);
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = (UINT)capacityBytes;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		buffer.Reset();
		return SUCCEEDED(device.CreateBuffer(&desc, nullptr, &buffer));
	}

	template<typename T>
	inline void updateConstantBuffer(ID3D11DeviceContext& context, ID3D11Buffer& buffer, const T& data)
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (SUCCEEDED(context.Map(&buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			std::memcpy(mapped.pData, &data, sizeof(T));
			context.Unmap(&buffer, 0);
		}
	}

	inline bool updateDynamicVertexBuffer(ID3D11DeviceContext& context, ID3D11Buffer& buffer, const void* data, size_t dataSize)
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context.Map(&buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return false;

		std::memcpy(mapped.pData, data, dataSize);
		context.Unmap(&buffer, 0);
		return true;
	}

	inline Microsoft::WRL::ComPtr<ID3D11BlendState> createBlendState(ID3D11Device& device, BlendMode blendMode)
	{
		D3D11_BLEND_DESC desc = {};
		desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		switch (blendMode)
		{
			default:
			case BlendMode::OPAQUE:
				desc.RenderTarget[0].BlendEnable = FALSE;
				break;

			case BlendMode::ALPHA:
			case BlendMode::ONE_BIT:
				desc.RenderTarget[0].BlendEnable = TRUE;
				desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
				desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
				desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
				desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
				desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
				desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
				break;

			case BlendMode::ADDITIVE:
				desc.RenderTarget[0].BlendEnable = TRUE;
				desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
				desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
				desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
				desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
				desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
				desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
				break;

			case BlendMode::SUBTRACTIVE:
				desc.RenderTarget[0].BlendEnable = TRUE;
				desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
				desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
				desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
				desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
				desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
				desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
				break;

			case BlendMode::MULTIPLICATIVE:
				desc.RenderTarget[0].BlendEnable = TRUE;
				desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
				desc.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_COLOR;
				desc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
				desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
				desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
				desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
				break;

			case BlendMode::MINIMUM:
				desc.RenderTarget[0].BlendEnable = TRUE;
				desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_MIN;
				desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
				desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
				desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
				desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
				desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
				break;

			case BlendMode::MAXIMUM:
				desc.RenderTarget[0].BlendEnable = TRUE;
				desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_MAX;
				desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
				desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
				desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
				desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
				desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
				break;
		}

		Microsoft::WRL::ComPtr<ID3D11BlendState> blendState;
		device.CreateBlendState(&desc, &blendState);
		return blendState;
	}

	const D3D11_INPUT_ELEMENT_DESC INPUT_LAYOUT_P2[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	const D3D11_INPUT_ELEMENT_DESC INPUT_LAYOUT_P2_T2[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	const D3D11_INPUT_ELEMENT_DESC INPUT_LAYOUT_P2_C4[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	const char* UPSCALER_FULLSCREEN_VS = R"(
struct VSInput
{
	float2 position : POSITION;
};
struct VSOutput
{
	float4 position : SV_POSITION;
	float2 uv0 : TEXCOORD0;
};
VSOutput VSMain(VSInput input)
{
	VSOutput output;
	output.uv0 = input.position;
	output.position = float4(-1.0f + input.position.x * 2.0f, 1.0f - input.position.y * 2.0f, 0.0f, 1.0f);
	return output;
})";

	const char* UPSCALER_SOFT_PS = R"(
cbuffer UpscalerCB : register(b0)
{
	int2 GameResolution;
	int2 OutputSize;
	float PixelFactor;
	float ScanlinesIntensity;
	float2 Padding0;
};
Texture2D MainTexture : register(t0);
SamplerState MainSampler : register(s0);
float4 PSMain(float4 position : SV_POSITION, float2 uv0 : TEXCOORD0) : SV_TARGET
{
	float2 resolution = float2(GameResolution);
	float2 uv;

	float x = uv0.x * resolution.x;
	float ix = floor(x + 0.5f);
	float fx = x - ix;
	fx = clamp(fx * PixelFactor, -0.5f, 0.5f);
	uv.x = (ix + fx) / resolution.x;

	float y = uv0.y * resolution.y;
	float iy = floor(y + 0.5f);
	float fy = y - iy;
	float colorMultiplier = 1.0f;
	if (ScanlinesIntensity > 0.0f)
	{
		colorMultiplier = 1.0f - (0.5f - abs(fy)) * ScanlinesIntensity;
	}

	fy = clamp(fy * PixelFactor, -0.5f, 0.5f);
	uv.y = (iy + fy) / resolution.y;

	float4 color = MainTexture.SampleLevel(MainSampler, uv, 0.0f);
	if (ScanlinesIntensity > 0.0f)
	{
		color.rgb *= colorMultiplier;
	}
	color.a = 1.0f;
	return color;
})";

	const char* UPSCALER_XBRZ_PASS0_PS = R"(
cbuffer UpscalerCB : register(b0)
{
	int2 GameResolution;
	int2 OutputSize;
	float PixelFactor;
	float ScanlinesIntensity;
	float2 Padding0;
};
Texture2D MainTexture : register(t0);
SamplerState MainSampler : register(s0);

static const float BLEND_NONE = 0.0f;
static const float BLEND_NORMAL = 1.0f;
static const float BLEND_DOMINANT = 2.0f;
static const float LUMINANCE_WEIGHT = 1.0f;
static const float EQUAL_COLOR_TOLERANCE = 30.0f / 255.0f;
static const float STEEP_DIRECTION_THRESHOLD = 2.2f;
static const float DOMINANT_DIRECTION_THRESHOLD = 3.6f;

float DistYCbCr(float3 pixA, float3 pixB)
{
	const float3 w = float3(0.2627f, 0.6780f, 0.0593f);
	const float scaleB = 0.5f / (1.0f - w.b);
	const float scaleR = 0.5f / (1.0f - w.r);
	float3 diff = pixA - pixB;
	float Y = dot(diff.rgb, w);
	float Cb = scaleB * (diff.b - Y);
	float Cr = scaleR * (diff.r - Y);
	return sqrt(((LUMINANCE_WEIGHT * Y) * (LUMINANCE_WEIGHT * Y)) + (Cb * Cb) + (Cr * Cr));
}

bool IsPixEqual(float3 pixA, float3 pixB)
{
	return (DistYCbCr(pixA, pixB) < EQUAL_COLOR_TOLERANCE);
}

float3 SampleOffset(float2 coord, float2 offset, float2 invResolution)
{
	return MainTexture.SampleLevel(MainSampler, coord + invResolution * offset, 0.0f).rgb;
}

bool ColorsEqual(float3 a, float3 b)
{
	return all(a == b);
}

bool ColorsNotEqual(float3 a, float3 b)
{
	return any(a != b);
}

float4 PSMain(float4 position : SV_POSITION, float2 uv0 : TEXCOORD0) : SV_TARGET
{
	float2 resolution = float2(GameResolution);
	float2 invResolution = 1.0f / resolution;
	float2 texCoord = uv0 * 1.0001f;
	float2 pos = frac(texCoord * resolution) - float2(0.5f, 0.5f);
	float2 coord = texCoord - pos * invResolution;

	float3 A = SampleOffset(coord, float2(-1.0f, -1.0f), invResolution);
	float3 B = SampleOffset(coord, float2( 0.0f, -1.0f), invResolution);
	float3 C = SampleOffset(coord, float2( 1.0f, -1.0f), invResolution);
	float3 D = SampleOffset(coord, float2(-1.0f,  0.0f), invResolution);
	float3 E = SampleOffset(coord, float2( 0.0f,  0.0f), invResolution);
	float3 F = SampleOffset(coord, float2( 1.0f,  0.0f), invResolution);
	float3 G = SampleOffset(coord, float2(-1.0f,  1.0f), invResolution);
	float3 H = SampleOffset(coord, float2( 0.0f,  1.0f), invResolution);
	float3 I = SampleOffset(coord, float2( 1.0f,  1.0f), invResolution);

	float4 blendResult = float4(BLEND_NONE, BLEND_NONE, BLEND_NONE, BLEND_NONE);

	if (!((ColorsEqual(E, F) && ColorsEqual(H, I)) || (ColorsEqual(E, H) && ColorsEqual(F, I))))
	{
		float dist_H_F = DistYCbCr(G, E) + DistYCbCr(E, C) + DistYCbCr(SampleOffset(coord, float2(0.0f, 2.0f), invResolution), I) + DistYCbCr(I, SampleOffset(coord, float2(2.0f, 0.0f), invResolution)) + (4.0f * DistYCbCr(H, F));
		float dist_E_I = DistYCbCr(D, H) + DistYCbCr(H, SampleOffset(coord, float2(1.0f, 2.0f), invResolution)) + DistYCbCr(B, F) + DistYCbCr(F, SampleOffset(coord, float2(2.0f, 1.0f), invResolution)) + (4.0f * DistYCbCr(E, I));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_H_F) < dist_E_I;
		blendResult.z = ((dist_H_F < dist_E_I) && ColorsNotEqual(E, F) && ColorsNotEqual(E, H)) ? (dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	if (!((ColorsEqual(D, E) && ColorsEqual(G, H)) || (ColorsEqual(D, G) && ColorsEqual(E, H))))
	{
		float dist_G_E = DistYCbCr(SampleOffset(coord, float2(-2.0f, 1.0f), invResolution), D) + DistYCbCr(D, B) + DistYCbCr(SampleOffset(coord, float2(-1.0f, 2.0f), invResolution), H) + DistYCbCr(H, F) + (4.0f * DistYCbCr(G, E));
		float dist_D_H = DistYCbCr(SampleOffset(coord, float2(-2.0f, 0.0f), invResolution), G) + DistYCbCr(G, SampleOffset(coord, float2(0.0f, 2.0f), invResolution)) + DistYCbCr(A, E) + DistYCbCr(E, I) + (4.0f * DistYCbCr(D, H));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_D_H) < dist_G_E;
		blendResult.w = ((dist_G_E > dist_D_H) && ColorsNotEqual(E, D) && ColorsNotEqual(E, H)) ? (dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	if (!((ColorsEqual(B, C) && ColorsEqual(E, F)) || (ColorsEqual(B, E) && ColorsEqual(C, F))))
	{
		float dist_E_C = DistYCbCr(D, B) + DistYCbCr(B, SampleOffset(coord, float2(1.0f, -2.0f), invResolution)) + DistYCbCr(H, F) + DistYCbCr(F, SampleOffset(coord, float2(2.0f, -1.0f), invResolution)) + (4.0f * DistYCbCr(E, C));
		float dist_B_F = DistYCbCr(A, E) + DistYCbCr(E, I) + DistYCbCr(SampleOffset(coord, float2(0.0f, -2.0f), invResolution), C) + DistYCbCr(C, SampleOffset(coord, float2(2.0f, 0.0f), invResolution)) + (4.0f * DistYCbCr(B, F));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_B_F) < dist_E_C;
		blendResult.y = ((dist_E_C > dist_B_F) && ColorsNotEqual(E, B) && ColorsNotEqual(E, F)) ? (dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	if (!((ColorsEqual(A, B) && ColorsEqual(D, E)) || (ColorsEqual(A, D) && ColorsEqual(B, E))))
	{
		float dist_D_B = DistYCbCr(SampleOffset(coord, float2(-2.0f, 0.0f), invResolution), A) + DistYCbCr(A, SampleOffset(coord, float2(0.0f, -2.0f), invResolution)) + DistYCbCr(G, E) + DistYCbCr(E, C) + (4.0f * DistYCbCr(D, B));
		float dist_A_E = DistYCbCr(SampleOffset(coord, float2(-2.0f, -1.0f), invResolution), D) + DistYCbCr(D, H) + DistYCbCr(SampleOffset(coord, float2(-1.0f, -2.0f), invResolution), B) + DistYCbCr(B, F) + (4.0f * DistYCbCr(A, E));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_D_B) < dist_A_E;
		blendResult.x = ((dist_D_B < dist_A_E) && ColorsNotEqual(E, D) && ColorsNotEqual(E, B)) ? (dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	float4 outputColor = blendResult;

	if (blendResult.z == BLEND_DOMINANT || (blendResult.z == BLEND_NORMAL &&
		!((blendResult.y != BLEND_NONE && !IsPixEqual(E, G)) || (blendResult.w != BLEND_NONE && !IsPixEqual(E, C)) ||
		 (IsPixEqual(G, H) && IsPixEqual(H, I) && IsPixEqual(I, F) && IsPixEqual(F, C) && !IsPixEqual(E, I)))))
	{
		outputColor.z += 4.0f;
		float dist_F_G = DistYCbCr(F, G);
		float dist_H_C = DistYCbCr(H, C);
		if ((STEEP_DIRECTION_THRESHOLD * dist_F_G <= dist_H_C) && ColorsNotEqual(E, G) && ColorsNotEqual(D, G))
			outputColor.z += 16.0f;
		if ((STEEP_DIRECTION_THRESHOLD * dist_H_C <= dist_F_G) && ColorsNotEqual(E, C) && ColorsNotEqual(B, C))
			outputColor.z += 64.0f;
	}

	if (blendResult.w == BLEND_DOMINANT || (blendResult.w == BLEND_NORMAL &&
		!((blendResult.z != BLEND_NONE && !IsPixEqual(E, A)) || (blendResult.x != BLEND_NONE && !IsPixEqual(E, I)) ||
		 (IsPixEqual(A, D) && IsPixEqual(D, G) && IsPixEqual(G, H) && IsPixEqual(H, I) && !IsPixEqual(E, G)))))
	{
		outputColor.w += 4.0f;
		float dist_H_A = DistYCbCr(H, A);
		float dist_D_I = DistYCbCr(D, I);
		if ((STEEP_DIRECTION_THRESHOLD * dist_H_A <= dist_D_I) && ColorsNotEqual(E, A) && ColorsNotEqual(B, A))
			outputColor.w += 16.0f;
		if ((STEEP_DIRECTION_THRESHOLD * dist_D_I <= dist_H_A) && ColorsNotEqual(E, I) && ColorsNotEqual(F, I))
			outputColor.w += 64.0f;
	}

	if (blendResult.y == BLEND_DOMINANT || (blendResult.y == BLEND_NORMAL &&
		!((blendResult.x != BLEND_NONE && !IsPixEqual(E, I)) || (blendResult.z != BLEND_NONE && !IsPixEqual(E, A)) ||
		  (IsPixEqual(I, F) && IsPixEqual(F, C) && IsPixEqual(C, B) && IsPixEqual(B, A) && !IsPixEqual(E, C)))))
	{
		outputColor.y += 4.0f;
		float dist_B_I = DistYCbCr(B, I);
		float dist_F_A = DistYCbCr(F, A);
		if ((STEEP_DIRECTION_THRESHOLD * dist_B_I <= dist_F_A) && ColorsNotEqual(E, I) && ColorsNotEqual(H, I))
			outputColor.y += 16.0f;
		if ((STEEP_DIRECTION_THRESHOLD * dist_F_A <= dist_B_I) && ColorsNotEqual(E, A) && ColorsNotEqual(D, A))
			outputColor.y += 64.0f;
	}

	if (blendResult.x == BLEND_DOMINANT || (blendResult.x == BLEND_NORMAL &&
		!((blendResult.w != BLEND_NONE && !IsPixEqual(E, C)) || (blendResult.y != BLEND_NONE && !IsPixEqual(E, G)) ||
		 (IsPixEqual(C, B) && IsPixEqual(B, A) && IsPixEqual(A, D) && IsPixEqual(D, G) && !IsPixEqual(E, A)))))
	{
		outputColor.x += 4.0f;
		float dist_D_C = DistYCbCr(D, C);
		float dist_B_G = DistYCbCr(B, G);
		if ((STEEP_DIRECTION_THRESHOLD * dist_D_C <= dist_B_G) && ColorsNotEqual(E, C) && ColorsNotEqual(F, C))
			outputColor.x += 16.0f;
		if ((STEEP_DIRECTION_THRESHOLD * dist_B_G <= dist_D_C) && ColorsNotEqual(E, G) && ColorsNotEqual(H, G))
			outputColor.x += 64.0f;
	}

	return outputColor / 255.0f;
})";

	const char* UPSCALER_XBRZ_PASS1_PS = R"(
cbuffer UpscalerCB : register(b0)
{
	int2 GameResolution;
	int2 OutputSize;
	float PixelFactor;
	float ScanlinesIntensity;
	float2 Padding0;
};
Texture2D MainTexture : register(t0);
Texture2D OrigTexture : register(t1);
SamplerState MainSampler : register(s0);

static const float LUMINANCE_WEIGHT = 1.0f;

float DistYCbCr(float3 pixA, float3 pixB)
{
	const float3 w = float3(0.2627f, 0.6780f, 0.0593f);
	const float scaleB = 0.5f / (1.0f - w.b);
	const float scaleR = 0.5f / (1.0f - w.r);
	float3 diff = pixA - pixB;
	float Y = dot(diff.rgb, w);
	float Cb = scaleB * (diff.b - Y);
	float Cr = scaleR * (diff.r - Y);
	return sqrt(((LUMINANCE_WEIGHT * Y) * (LUMINANCE_WEIGHT * Y)) + (Cb * Cb) + (Cr * Cr));
}

float get_left_ratio(float2 center, float2 origin, float2 direction, float2 scale)
{
	float2 P0 = center - origin;
	float2 proj = direction * (dot(P0, direction) / dot(direction, direction));
	float2 distv = P0 - proj;
	float2 orth = float2(-direction.y, direction.x);
	float side = sign(dot(P0, orth));
	float v = side * length(distv * scale);
	return smoothstep(-sqrt(2.0f) / 2.0f, sqrt(2.0f) / 2.0f, v);
}

float4 PSMain(float4 position : SV_POSITION, float2 uv0 : TEXCOORD0) : SV_TARGET
{
	float2 originalSize = float2(GameResolution);
	float2 outputSize = float2(OutputSize);
	float2 invOriginalSize = 1.0f / originalSize;
	float2 texCoord = uv0 * 1.0001f;
	float2 scale = outputSize * invOriginalSize;
	float2 pos = frac(texCoord * originalSize) - float2(0.5f, 0.5f);
	float2 coord = texCoord - pos * invOriginalSize;

	float3 B = OrigTexture.SampleLevel(MainSampler, coord + invOriginalSize * float2( 0.0f, -1.0f), 0.0f).rgb;
	float3 D = OrigTexture.SampleLevel(MainSampler, coord + invOriginalSize * float2(-1.0f,  0.0f), 0.0f).rgb;
	float3 E = OrigTexture.SampleLevel(MainSampler, coord, 0.0f).rgb;
	float3 F = OrigTexture.SampleLevel(MainSampler, coord + invOriginalSize * float2( 1.0f,  0.0f), 0.0f).rgb;
	float3 H = OrigTexture.SampleLevel(MainSampler, coord + invOriginalSize * float2( 0.0f,  1.0f), 0.0f).rgb;

	float4 info = floor(MainTexture.SampleLevel(MainSampler, coord, 0.0f) * 255.0f + 0.5f);
	float4 blendResult = floor(fmod(info, 4.0f));
	float4 doLineBlend = floor(fmod(info / 4.0f, 4.0f));
	float4 haveShallowLine = floor(fmod(info / 16.0f, 4.0f));
	float4 haveSteepLine = floor(fmod(info / 64.0f, 4.0f));

	float3 res = E;

	if (blendResult.z > 0.0f)
	{
		float2 origin = float2(0.0f, 1.0f / sqrt(2.0f));
		float2 direction = float2(1.0f, -1.0f);
		if (doLineBlend.z > 0.0f)
		{
			origin = (haveShallowLine.z > 0.0f) ? float2(0.0f, 0.25f) : float2(0.0f, 0.5f);
			direction.x += haveShallowLine.z;
			direction.y -= haveSteepLine.z;
		}
		float3 blendPix = lerp(H, F, step(DistYCbCr(E, F), DistYCbCr(E, H)));
		res = lerp(res, blendPix, get_left_ratio(pos, origin, direction, scale));
	}

	if (blendResult.w > 0.0f)
	{
		float2 origin = float2(-1.0f / sqrt(2.0f), 0.0f);
		float2 direction = float2(1.0f, 1.0f);
		if (doLineBlend.w > 0.0f)
		{
			origin = (haveShallowLine.w > 0.0f) ? float2(-0.25f, 0.0f) : float2(-0.5f, 0.0f);
			direction.y += haveShallowLine.w;
			direction.x += haveSteepLine.w;
		}
		float3 blendPix = lerp(H, D, step(DistYCbCr(E, D), DistYCbCr(E, H)));
		res = lerp(res, blendPix, get_left_ratio(pos, origin, direction, scale));
	}

	if (blendResult.y > 0.0f)
	{
		float2 origin = float2(1.0f / sqrt(2.0f), 0.0f);
		float2 direction = float2(-1.0f, -1.0f);
		if (doLineBlend.y > 0.0f)
		{
			origin = (haveShallowLine.y > 0.0f) ? float2(0.25f, 0.0f) : float2(0.5f, 0.0f);
			direction.y -= haveShallowLine.y;
			direction.x -= haveSteepLine.y;
		}
		float3 blendPix = lerp(F, B, step(DistYCbCr(E, B), DistYCbCr(E, F)));
		res = lerp(res, blendPix, get_left_ratio(pos, origin, direction, scale));
	}

	if (blendResult.x > 0.0f)
	{
		float2 origin = float2(0.0f, -1.0f / sqrt(2.0f));
		float2 direction = float2(-1.0f, 1.0f);
		if (doLineBlend.x > 0.0f)
		{
			origin = (haveShallowLine.x > 0.0f) ? float2(0.0f, -0.25f) : float2(0.0f, -0.5f);
			direction.x -= haveShallowLine.x;
			direction.y += haveSteepLine.x;
		}
		float3 blendPix = lerp(D, B, step(DistYCbCr(E, B), DistYCbCr(E, D)));
		res = lerp(res, blendPix, get_left_ratio(pos, origin, direction, scale));
	}

	return float4(res, 1.0f);
})";

	const char* UPSCALER_HQX_PS = R"(
cbuffer UpscalerCB : register(b0)
{
	int2 GameResolution;
	int2 OutputSize;
	float PixelFactor;
	float ScanlinesIntensity;
	float2 Padding0;
};
Texture2D MainTexture : register(t0);
Texture2D LUT : register(t1);
SamplerState MainSampler : register(s0);

#if defined(HQ2X)
	#define SCALE 2.0f
#elif defined(HQ3X)
	#define SCALE 3.0f
#else
	#define SCALE 4.0f
#endif

static const float3x3 yuv_matrix = float3x3(
	0.299f, -0.169f, 0.5f,
	0.587f, -0.331f, -0.419f,
	0.114f, 0.5f, -0.081f
);
static const float3 yuv_threshold = float3(48.0f / 255.0f, 7.0f / 255.0f, 6.0f / 255.0f);
static const float3 yuv_offset = float3(0.0f, 0.5f, 0.5f);

bool diff3(float3 yuv1, float3 yuv2)
{
	bool3 res = abs((yuv1 + yuv_offset) - (yuv2 + yuv_offset)) > yuv_threshold;
	return res.x || res.y || res.z;
}

float4 PSMain(float4 position : SV_POSITION, float2 uv0 : TEXCOORD0) : SV_TARGET
{
	float2 gameResolution = float2(GameResolution);
	float2 fp = frac(uv0 * gameResolution);
	float2 quad = sign(-0.5f + fp);
	float2 ps = 1.0f / gameResolution;
	float dx = ps.x;
	float dy = ps.y;

	float3 p1 = MainTexture.SampleLevel(MainSampler, uv0, 0.0f).rgb;
	float3 p2 = MainTexture.SampleLevel(MainSampler, uv0 + float2(dx, dy) * quad, 0.0f).rgb;
	float3 p3 = MainTexture.SampleLevel(MainSampler, uv0 + float2(dx, 0.0f) * quad, 0.0f).rgb;
	float3 p4 = MainTexture.SampleLevel(MainSampler, uv0 + float2(0.0f, dy) * quad, 0.0f).rgb;

	float3 w1 = mul(yuv_matrix, MainTexture.SampleLevel(MainSampler, uv0 + float2(-dx, -dy), 0.0f).rgb);
	float3 w2 = mul(yuv_matrix, MainTexture.SampleLevel(MainSampler, uv0 + float2(0.0f, -dy), 0.0f).rgb);
	float3 w3 = mul(yuv_matrix, MainTexture.SampleLevel(MainSampler, uv0 + float2(dx, -dy), 0.0f).rgb);
	float3 w4 = mul(yuv_matrix, MainTexture.SampleLevel(MainSampler, uv0 + float2(-dx, 0.0f), 0.0f).rgb);
	float3 w5 = mul(yuv_matrix, p1);
	float3 w6 = mul(yuv_matrix, MainTexture.SampleLevel(MainSampler, uv0 + float2(dx, 0.0f), 0.0f).rgb);
	float3 w7 = mul(yuv_matrix, MainTexture.SampleLevel(MainSampler, uv0 + float2(-dx, dy), 0.0f).rgb);
	float3 w8 = mul(yuv_matrix, MainTexture.SampleLevel(MainSampler, uv0 + float2(0.0f, dy), 0.0f).rgb);
	float3 w9 = mul(yuv_matrix, MainTexture.SampleLevel(MainSampler, uv0 + float2(dx, dy), 0.0f).rgb);

	float3 pattern0 = float3(diff3(w5, w1) ? 1.0f : 0.0f, diff3(w5, w2) ? 1.0f : 0.0f, diff3(w5, w3) ? 1.0f : 0.0f);
	float3 pattern1 = float3(diff3(w5, w4) ? 1.0f : 0.0f, 0.0f, diff3(w5, w6) ? 1.0f : 0.0f);
	float3 pattern2 = float3(diff3(w5, w7) ? 1.0f : 0.0f, diff3(w5, w8) ? 1.0f : 0.0f, diff3(w5, w9) ? 1.0f : 0.0f);
	float4 cross = float4(diff3(w4, w2) ? 1.0f : 0.0f, diff3(w2, w6) ? 1.0f : 0.0f, diff3(w8, w4) ? 1.0f : 0.0f, diff3(w6, w8) ? 1.0f : 0.0f);

	float2 index;
	index.x = dot(pattern0, float3(1.0f, 2.0f, 4.0f)) +
			  dot(pattern1, float3(8.0f, 0.0f, 16.0f)) +
			  dot(pattern2, float3(32.0f, 64.0f, 128.0f));
	index.y = dot(cross, float4(1.0f, 2.0f, 4.0f, 8.0f)) * (SCALE * SCALE) +
			  dot(floor(fp * SCALE), float2(1.0f, SCALE));

	float2 stepSize = 1.0f / float2(256.0f, 16.0f * (SCALE * SCALE));
	float2 offset = stepSize * 0.5f;
	float4 weights = LUT.SampleLevel(MainSampler, index * stepSize + offset, 0.0f);
	float sum = dot(weights, float4(1.0f, 1.0f, 1.0f, 1.0f));
	float3 res = (p1 * weights.x + p2 * weights.y + p3 * weights.z + p4 * weights.w) / sum;
	return float4(res, 1.0f);
})";

	const char* SIMPLE_RECT_VS = R"(
cbuffer SimpleRectCB : register(b0)
{
	float4 Transform;
	float4 TintColor;
	float4 AddedColor;
	int2 TextureSize;
	int AlphaTest;
	int Padding0;
};
struct VSInput
{
	float2 position : POSITION;
};
struct VSOutput
{
	float4 position : SV_POSITION;
	float2 uv0 : TEXCOORD0;
};
VSOutput VSMain(VSInput input)
{
	VSOutput output;
	output.uv0 = input.position;
	float2 pos = Transform.xy + input.position * Transform.zw;
	output.position = float4(pos, 0.5f, 1.0f);
	return output;
})";

	const char* SIMPLE_RECT_COLORED_PS = R"(
cbuffer SimpleRectCB : register(b0)
{
	float4 Transform;
	float4 TintColor;
	float4 AddedColor;
	int2 TextureSize;
	int AlphaTest;
	int Padding0;
};
float4 PSMain(float4 position : SV_POSITION, float2 uv0 : TEXCOORD0) : SV_TARGET
{
	return TintColor;
})";

	const char* SIMPLE_RECT_TEXTURED_PS = R"(
cbuffer SimpleRectCB : register(b0)
{
	float4 Transform;
	float4 TintColor;
	float4 AddedColor;
	int2 TextureSize;
	int AlphaTest;
	int Padding0;
};
Texture2D MainTexture : register(t0);
SamplerState MainSampler : register(s0);
float4 PSMain(float4 position : SV_POSITION, float2 uv0 : TEXCOORD0) : SV_TARGET
{
	float4 color = MainTexture.Sample(MainSampler, uv0);
	color = float4(AddedColor.rgb, 0.0f) + color * TintColor;
	if (AlphaTest != 0 && color.a < 0.01f)
		discard;
	return color;
})";

	const char* SIMPLE_RECT_TEXTURED_UV_VS = R"(
cbuffer SimpleRectCB : register(b0)
{
	float4 Transform;
	float4 TintColor;
	float4 AddedColor;
	int2 TextureSize;
	int AlphaTest;
	int Padding0;
};
struct VSInput
{
	float2 position : POSITION;
	float2 texcoords0 : TEXCOORD0;
};
struct VSOutput
{
	float4 position : SV_POSITION;
	float2 uv0 : TEXCOORD0;
};
VSOutput VSMain(VSInput input)
{
	VSOutput output;
	output.uv0 = input.texcoords0;
	float2 pos = Transform.xy + input.position * Transform.zw;
	output.position = float4(pos, 0.5f, 1.0f);
	return output;
})";

	const char* SIMPLE_RECT_INDEXED_PS = R"(
cbuffer SimpleRectCB : register(b0)
{
	float4 Transform;
	float4 TintColor;
	float4 AddedColor;
	int2 TextureSize;
	int AlphaTest;
	int ShadowHighlightMode;
};
Texture2D<uint> MainTexture : register(t0);
Texture2D<float4> PaletteTexture : register(t1);
SamplerState PaletteSampler : register(s0);
float4 PSMain(float4 position : SV_POSITION, float2 uv0 : TEXCOORD0) : SV_TARGET
{
	int2 coord = int2(uv0 * float2(TextureSize));
	coord = clamp(coord, int2(0, 0), TextureSize - 1);
	uint paletteIndex = MainTexture.Load(int3(coord, 0)).r;
	if (ShadowHighlightMode != 0 && (paletteIndex == 0x3eu || paletteIndex == 0x3fu))
	{
		const float intensity = (paletteIndex == 0x3fu) ? 1.0f : 0.0f;
		return float4(intensity, intensity, intensity, 0.5f);
	}
	uint paletteX = paletteIndex & 0xff;
	uint paletteY = paletteIndex >> 8;
	float2 paletteUV = float2((float(paletteX) + 0.5f) / 256.0f, (float(paletteY) + 0.5f) / 4.0f);
	float4 color = PaletteTexture.SampleLevel(PaletteSampler, paletteUV, 0.0f);
	color = float4(AddedColor.rgb, 0.0f) + color * TintColor;
	if (AlphaTest != 0 && color.a < 0.01f)
		discard;
	return color;
})";

	const char* SIMPLE_RECT_VERTEX_COLOR_VS = R"(
cbuffer SimpleRectCB : register(b0)
{
	float4 Transform;
	float4 TintColor;
	float4 AddedColor;
	int2 TextureSize;
	int AlphaTest;
	int Padding0;
};
struct VSInput
{
	float2 position : POSITION;
	float4 color : COLOR;
};
struct VSOutput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
};
VSOutput VSMain(VSInput input)
{
	VSOutput output;
	float2 pos = Transform.xy + input.position * Transform.zw;
	output.position = float4(pos, 0.5f, 1.0f);
	output.color = input.color;
	return output;
})";

	const char* SIMPLE_RECT_VERTEX_COLOR_PS = R"(
float4 PSMain(float4 position : SV_POSITION, float4 color : COLOR) : SV_TARGET
{
	return color;
})";
}


struct D3D11DrawerResources::Internal
{
	D3D11Shader mSimpleRectColoredShader;
	D3D11Shader mSimpleRectTexturedShader;
	D3D11Shader mSimpleRectTexturedUVShader;
	D3D11Shader mSimpleRectIndexedShader;
	D3D11Shader mSimpleRectVertexColorShader;
	D3D11SpriteTextureManager mSpriteTextureManager;
	std::array<std::unique_ptr<D3D11Upscaler>, 4> mUpscalers;

	Microsoft::WRL::ComPtr<ID3D11Buffer> mSimpleRectConstants;
	Microsoft::WRL::ComPtr<ID3D11Buffer> mSimpleQuadVertexBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> mDynamicTexturedVertexBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> mDynamicVertexColorBuffer;
	size_t mDynamicTexturedVertexCapacity = 0;
	size_t mDynamicVertexColorCapacity = 0;

	Microsoft::WRL::ComPtr<ID3D11RasterizerState> mRasterizerNoScissor;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> mRasterizerScissor;
	std::array<Microsoft::WRL::ComPtr<ID3D11SamplerState>, 4> mSamplers;
	std::array<Microsoft::WRL::ComPtr<ID3D11BlendState>, 8> mBlendStates;
};


D3D11DrawerResources::D3D11DrawerResources() :
	mInternal(*new Internal())
{
}

D3D11DrawerResources::~D3D11DrawerResources()
{
	delete &mInternal;
}

bool D3D11DrawerResources::startup()
{
	shutdown();

#if defined(PLATFORM_UWP) && defined(_DEBUG)
	s3air::uwp::appendBootTrace(L"D3D11Drawer", L"startup begin");
#endif

	UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	static const D3D_FEATURE_LEVEL FEATURE_LEVELS[] =
	{
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0
	};

	const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags, FEATURE_LEVELS, _countof(FEATURE_LEVELS), D3D11_SDK_VERSION, &mDevice, &featureLevel, &mContext);
	if (FAILED(hr))
	{
#if defined(PLATFORM_UWP) && defined(_DEBUG)
		s3air::uwp::appendBootTrace(L"D3D11Drawer", L"D3D11CreateDevice failed");
#endif
		RMX_LOG_INFO("Direct3D 11 device creation failed");
		return false;
	}

#if defined(PLATFORM_UWP) && defined(_DEBUG)
	s3air::uwp::appendBootTrace(L"D3D11Drawer", L"D3D11CreateDevice succeeded");
#endif

	if (!ensureBuffer(*mDevice.Get(), mInternal.mSimpleRectConstants, sizeof(SimpleRectConstants), D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE))
		return false;

	{
		const float vertexData[] =
		{
			0.0f, 0.0f,
			0.0f, 1.0f,
			1.0f, 1.0f,
			1.0f, 1.0f,
			1.0f, 0.0f,
			0.0f, 0.0f
		};
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(vertexData);
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		D3D11_SUBRESOURCE_DATA initialData = {};
		initialData.pSysMem = vertexData;
		if (FAILED(mDevice->CreateBuffer(&desc, &initialData, &mInternal.mSimpleQuadVertexBuffer)))
			return false;
	}

	if (!mInternal.mSimpleRectColoredShader.initialize(*mDevice.Get(), SIMPLE_RECT_VS, SIMPLE_RECT_COLORED_PS, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "SimpleRectColored"))
		return false;
	if (!mInternal.mSimpleRectTexturedShader.initialize(*mDevice.Get(), SIMPLE_RECT_VS, SIMPLE_RECT_TEXTURED_PS, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "SimpleRectTextured"))
		return false;
	if (!mInternal.mSimpleRectTexturedUVShader.initialize(*mDevice.Get(), SIMPLE_RECT_TEXTURED_UV_VS, SIMPLE_RECT_TEXTURED_PS, INPUT_LAYOUT_P2_T2, _countof(INPUT_LAYOUT_P2_T2), "SimpleRectTexturedUV"))
		return false;
	if (!mInternal.mSimpleRectIndexedShader.initialize(*mDevice.Get(), SIMPLE_RECT_VS, SIMPLE_RECT_INDEXED_PS, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "SimpleRectIndexed"))
		return false;
	if (!mInternal.mSimpleRectVertexColorShader.initialize(*mDevice.Get(), SIMPLE_RECT_VERTEX_COLOR_VS, SIMPLE_RECT_VERTEX_COLOR_PS, INPUT_LAYOUT_P2_C4, _countof(INPUT_LAYOUT_P2_C4), "SimpleRectVertexColor"))
		return false;

	{
		D3D11_RASTERIZER_DESC desc = {};
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;
		desc.DepthClipEnable = TRUE;
		if (FAILED(mDevice->CreateRasterizerState(&desc, &mInternal.mRasterizerNoScissor)))
			return false;

		desc.ScissorEnable = TRUE;
		if (FAILED(mDevice->CreateRasterizerState(&desc, &mInternal.mRasterizerScissor)))
			return false;
	}

	for (int samplingIndex = 0; samplingIndex < 2; ++samplingIndex)
	{
		for (int wrapIndex = 0; wrapIndex < 2; ++wrapIndex)
		{
			D3D11_SAMPLER_DESC desc = {};
			desc.Filter = (samplingIndex == 0) ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			desc.AddressU = (wrapIndex == 0) ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
			desc.AddressV = desc.AddressU;
			desc.AddressW = desc.AddressU;
			desc.MaxLOD = D3D11_FLOAT32_MAX;
			const int index = samplingIndex * 2 + wrapIndex;
			if (FAILED(mDevice->CreateSamplerState(&desc, &mInternal.mSamplers[index])))
				return false;
		}
	}

	for (size_t i = 0; i < mInternal.mBlendStates.size(); ++i)
	{
		mInternal.mBlendStates[i] = createBlendState(*mDevice.Get(), (BlendMode)i);
	}

	for (size_t i = 0; i < mInternal.mUpscalers.size(); ++i)
	{
		mInternal.mUpscalers[i] = std::make_unique<D3D11Upscaler>((D3D11Upscaler::Type)i, *this);
		if (!mInternal.mUpscalers[i]->startup())
			return false;
	}

	setBlendMode(BlendMode::OPAQUE);
	setScissorRect(nullptr);
	mSetupSuccessful = true;
#if defined(PLATFORM_UWP) && defined(_DEBUG)
	s3air::uwp::appendBootTrace(L"D3D11Drawer", L"startup complete");
#endif
	return true;
}

void D3D11DrawerResources::shutdown()
{
	mContext.Reset();
	mDevice.Reset();
	mSwapChain.Reset();
	mWindowRenderTargetView.Reset();
	mCurrentRenderTargetView.Reset();
	mWindowSize = Vec2i();
	mCurrentViewport = Recti();
	mSwapChainFlags = 0;
	mAllowTearingSupported = false;
	mHasScissorRect = false;
	mCurrentScissorRect = Recti();
	mBlendMode = BlendMode::OPAQUE;
	mSamplingMode = SamplingMode::POINT;
	mWrapMode = TextureWrapMode::CLAMP;
	for (std::unique_ptr<D3D11Upscaler>& upscaler : mInternal.mUpscalers)
	{
		if (nullptr != upscaler)
			upscaler->shutdown();
		upscaler.reset();
	}
	clearAllCaches();
	mSetupSuccessful = false;
}

void D3D11DrawerResources::clearAllCaches()
{
	mCustomPalettes.clear();
}

void D3D11DrawerResources::refresh(float deltaSeconds)
{
	mSecondsSinceLastPaletteCleanup += clamp(deltaSeconds, 0.0f, 0.1f);
	if (mSecondsSinceLastPaletteCleanup < 1.0f)
		return;

	for (auto it = mCustomPalettes.begin(); it != mCustomPalettes.end(); )
	{
		PaletteData& data = it->second;
		data.mSecondsSinceLastUse += mSecondsSinceLastPaletteCleanup;
		if (data.mSecondsSinceLastUse >= 5.0f)
			it = mCustomPalettes.erase(it);
		else
			++it;
	}
	mSecondsSinceLastPaletteCleanup = 0.0f;
}

bool D3D11DrawerResources::ensureWindowResources(SDL_Window* window)
{
	if (nullptr == window || nullptr == mDevice || nullptr == mContext)
		return false;

	int width = 0;
	int height = 0;
	SDL_GetWindowSize(window, &width, &height);
	width = std::max(width, 1);
	height = std::max(height, 1);

	if (mSwapChain != nullptr && mWindowRenderTargetView != nullptr && mWindowSize == Vec2i(width, height))
		return true;

	if (mSwapChain == nullptr)
	{
#if defined(PLATFORM_UWP) && defined(_DEBUG)
		{
			std::wstringstream stream;
			stream << L"ensureWindowResources create swapchain size=" << width << L"x" << height;
			s3air::uwp::appendBootTrace(L"D3D11Drawer", stream.str());
		}
#endif
		SDL_SysWMinfo windowInfo;
		SDL_VERSION(&windowInfo.version);
		if (!SDL_GetWindowWMInfo(window, &windowInfo))
			return false;

		Microsoft::WRL::ComPtr<IDXGIDevice1> dxgiDevice;
		Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
		Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory;
		if (FAILED(mDevice.As(&dxgiDevice)) ||
			FAILED(dxgiDevice->GetAdapter(&dxgiAdapter)) ||
			FAILED(dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory))))
			return false;

#if !defined(PLATFORM_UWP)
		mAllowTearingSupported = checkAllowTearingSupport(*dxgiFactory.Get());
		mSwapChainFlags = mAllowTearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
#else
		mAllowTearingSupported = false;
		mSwapChainFlags = 0;
#endif

		DXGI_SWAP_CHAIN_DESC1 desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = 2;
		desc.Scaling = DXGI_SCALING_STRETCH;
#if !defined(PLATFORM_UWP)
		desc.SwapEffect = mAllowTearingSupported ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
#else
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
#endif
		desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		desc.Flags = mSwapChainFlags;

		HRESULT hr = E_FAIL;
#if defined(PLATFORM_UWP)
		if (windowInfo.subsystem == SDL_SYSWM_WINRT)
		{
			hr = dxgiFactory->CreateSwapChainForCoreWindow(mDevice.Get(), static_cast<IUnknown*>(windowInfo.info.winrt.window), &desc, nullptr, &mSwapChain);
		}
#else
		if (windowInfo.subsystem == SDL_SYSWM_WINDOWS)
		{
			hr = dxgiFactory->CreateSwapChainForHwnd(mDevice.Get(), windowInfo.info.win.window, &desc, nullptr, nullptr, mSwapChain.GetAddressOf());
		}
#endif
		if (FAILED(hr))
			return false;

#if defined(PLATFORM_UWP) && defined(_DEBUG)
		s3air::uwp::appendBootTrace(L"D3D11Drawer", L"CreateSwapChainForCoreWindow succeeded");
#endif
	}
	else
	{
		ID3D11RenderTargetView* nullRenderTarget = nullptr;
		mContext->OMSetRenderTargets(1, &nullRenderTarget, nullptr);
		mWindowRenderTargetView.Reset();
		if (FAILED(mSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, mSwapChainFlags)))
			return false;
	}

	Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
	if (FAILED(mSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
		return false;
	if (FAILED(mDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, &mWindowRenderTargetView)))
		return false;

	mWindowSize.set(width, height);
#if defined(PLATFORM_UWP) && defined(_DEBUG)
	{
		std::wstringstream stream;
		stream << L"Window RTV ready size=" << width << L"x" << height;
		s3air::uwp::appendBootTrace(L"D3D11Drawer", stream.str());
	}
#endif
	return true;
}

void D3D11DrawerResources::present(bool useVSync)
{
	if (mSwapChain != nullptr)
	{
		const UINT presentFlags = (!useVSync && mAllowTearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
		const HRESULT hr = mSwapChain->Present(useVSync ? 1 : 0, presentFlags);
#if defined(PLATFORM_UWP) && defined(_DEBUG)
		static bool loggedFirstPresent = false;
		if (!loggedFirstPresent)
		{
			loggedFirstPresent = true;
			std::wstringstream stream;
			stream << L"first Present hr=0x" << std::hex << (uint32_t)hr << std::dec;
			s3air::uwp::appendBootTrace(L"D3D11Drawer", stream.str());
		}
#endif
	}
}

void D3D11DrawerResources::setBlendMode(BlendMode blendMode)
{
	if (mBlendMode == blendMode || nullptr == mContext)
		return;

	mBlendMode = blendMode;
	const float blendFactor[4] = { 0, 0, 0, 0 };
	ID3D11BlendState* blendState = mInternal.mBlendStates[(size_t)blendMode].Get();
	mContext->OMSetBlendState(blendState, blendFactor, 0xffffffff);
}

ID3D11SamplerState* D3D11DrawerResources::getSamplerState(SamplingMode samplingMode, TextureWrapMode wrapMode) const
{
	const int index = ((samplingMode == SamplingMode::POINT) ? 0 : 2) + ((wrapMode == TextureWrapMode::CLAMP) ? 0 : 1);
	return mInternal.mSamplers[index].Get();
}

void D3D11DrawerResources::bindRenderTarget(ID3D11RenderTargetView* renderTargetView, ID3D11DepthStencilView* depthStencilView, const Recti& viewport)
{
	static ID3D11ShaderResourceView* const NULL_SRVS[4] = { nullptr, nullptr, nullptr, nullptr };
	mContext->PSSetShaderResources(0, 4, NULL_SRVS);
	mContext->OMSetRenderTargets(1, &renderTargetView, depthStencilView);
	mCurrentRenderTargetView = renderTargetView;
	mCurrentViewport = viewport;

	D3D11_VIEWPORT d3dViewport = {};
	d3dViewport.TopLeftX = (float)viewport.x;
	d3dViewport.TopLeftY = (float)viewport.y;
	d3dViewport.Width = (float)std::max(viewport.width, 1);
	d3dViewport.Height = (float)std::max(viewport.height, 1);
	d3dViewport.MinDepth = 0.0f;
	d3dViewport.MaxDepth = 1.0f;
	mContext->RSSetViewports(1, &d3dViewport);
	setScissorRect(nullptr);
}

void D3D11DrawerResources::setScissorRect(const Recti* rect)
{
	if (nullptr == rect)
	{
		mHasScissorRect = false;
		mCurrentScissorRect = Recti();
		mContext->RSSetState(mInternal.mRasterizerNoScissor.Get());
		return;
	}

	mHasScissorRect = true;
	mCurrentScissorRect = *rect;
	D3D11_RECT d3dRect = {};
	d3dRect.left = rect->x;
	d3dRect.top = rect->y;
	d3dRect.right = rect->x + std::max(rect->width, 0);
	d3dRect.bottom = rect->y + std::max(rect->height, 0);
	mContext->RSSetState(mInternal.mRasterizerScissor.Get());
	mContext->RSSetScissorRects(1, &d3dRect);
}

void D3D11DrawerResources::clearRenderTarget(ID3D11RenderTargetView* renderTargetView, const Color& color)
{
	const float rgba[4] = { color.r, color.g, color.b, color.a };
	mContext->ClearRenderTargetView(renderTargetView, rgba);
}

void D3D11DrawerResources::clearDepthStencil(ID3D11DepthStencilView* depthStencilView, float depth)
{
	mContext->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH, depth, 0);
}

bool D3D11DrawerResources::updateTexture(d3d11::TextureResource& texture, const Vec2i& size, DXGI_FORMAT format, const void* data, UINT rowPitch, bool allowRenderTarget)
{
	const bool needsRecreate = !texture.isValid() || texture.mSize != size || texture.mFormat != format || ((texture.mRenderTargetView != nullptr) != allowRenderTarget);
	if (needsRecreate)
	{
		texture.reset();

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = (UINT)std::max(size.x, 1);
		desc.Height = (UINT)std::max(size.y, 1);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (allowRenderTarget ? D3D11_BIND_RENDER_TARGET : 0);

		D3D11_SUBRESOURCE_DATA initialData = {};
		if (nullptr != data)
		{
			initialData.pSysMem = data;
			initialData.SysMemPitch = rowPitch;
		}

		if (FAILED(mDevice->CreateTexture2D(&desc, (nullptr != data) ? &initialData : nullptr, &texture.mTexture)))
			return false;
		if (FAILED(mDevice->CreateShaderResourceView(texture.mTexture.Get(), nullptr, &texture.mShaderResourceView)))
			return false;
		if (allowRenderTarget && FAILED(mDevice->CreateRenderTargetView(texture.mTexture.Get(), nullptr, &texture.mRenderTargetView)))
			return false;

		texture.mSize = size;
		texture.mFormat = format;
	}
	else if (nullptr != data)
	{
		D3D11_BOX box = {};
		box.left = 0;
		box.top = 0;
		box.front = 0;
		box.right = (UINT)size.x;
		box.bottom = (UINT)size.y;
		box.back = 1;
		mContext->UpdateSubresource(texture.mTexture.Get(), 0, &box, data, rowPitch, 0);
	}

	return true;
}

bool D3D11DrawerResources::readTextureToBitmap(const d3d11::TextureResource& texture, Bitmap& outBitmap)
{
	if (!texture.isValid())
		return false;

	D3D11_TEXTURE2D_DESC sourceDesc = {};
	texture.mTexture->GetDesc(&sourceDesc);

	D3D11_TEXTURE2D_DESC stagingDesc = sourceDesc;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.MiscFlags = 0;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
	if (FAILED(mDevice->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture)))
		return false;

	mContext->CopyResource(stagingTexture.Get(), texture.mTexture.Get());

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (FAILED(mContext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
		return false;

	outBitmap.create((int)sourceDesc.Width, (int)sourceDesc.Height);
	for (UINT y = 0; y < sourceDesc.Height; ++y)
	{
		const uint8* src = (const uint8*)mapped.pData + y * mapped.RowPitch;
		uint32* dst = outBitmap.getPixelPointer(0, (int)y);
		if (sourceDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM)
		{
			std::memcpy(dst, src, sourceDesc.Width * sizeof(uint32));
		}
		else if (sourceDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM)
		{
			for (UINT x = 0; x < sourceDesc.Width; ++x)
			{
				const uint32 color = ((const uint32*)src)[x];
				dst[x] = ((color & 0x00ff0000) >> 16) | (color & 0xff00ff00) | ((color & 0x000000ff) << 16);
			}
		}
	}
	mContext->Unmap(stagingTexture.Get(), 0);
	return true;
}

void D3D11DrawerResources::drawSimpleRectColored(const Recti& rect, const Vec2i& targetSize, const Color& color)
{
	SimpleRectConstants constants;
	constants.mTransform = makeTransform(rect, targetSize);
	constants.mTintColor = color;
	updateConstantBuffer(*mContext.Get(), *mInternal.mSimpleRectConstants.Get(), constants);

	const UINT stride = sizeof(float) * 2;
	const UINT offset = 0;
	ID3D11Buffer* vertexBuffer = mInternal.mSimpleQuadVertexBuffer.Get();
	mContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
	mInternal.mSimpleRectColoredShader.bind(*mContext.Get());
	ID3D11Buffer* constantBuffer = mInternal.mSimpleRectConstants.Get();
	mContext->VSSetConstantBuffers(0, 1, &constantBuffer);
	mContext->PSSetConstantBuffers(0, 1, &constantBuffer);
	mContext->Draw(6, 0);
}

void D3D11DrawerResources::drawSimpleRectTextured(const Recti& rect, const Vec2i& targetSize, ID3D11ShaderResourceView* texture, const Color& tintColor, const Color& addedColor, bool alphaTest)
{
	SimpleRectConstants constants;
	constants.mTransform = makeTransform(rect, targetSize);
	constants.mTintColor = tintColor;
	constants.mAddedColor = addedColor;
	constants.mAlphaTest = alphaTest ? 1 : 0;
	updateConstantBuffer(*mContext.Get(), *mInternal.mSimpleRectConstants.Get(), constants);

	const UINT stride = sizeof(float) * 2;
	const UINT offset = 0;
	ID3D11Buffer* vertexBuffer = mInternal.mSimpleQuadVertexBuffer.Get();
	mContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
	mInternal.mSimpleRectTexturedShader.bind(*mContext.Get());
	ID3D11Buffer* constantBuffer = mInternal.mSimpleRectConstants.Get();
	mContext->VSSetConstantBuffers(0, 1, &constantBuffer);
	mContext->PSSetConstantBuffers(0, 1, &constantBuffer);
	mContext->PSSetShaderResources(0, 1, &texture);
	ID3D11SamplerState* sampler = getSamplerState(mSamplingMode, mWrapMode);
	mContext->PSSetSamplers(0, 1, &sampler);
	mContext->Draw(6, 0);
}

void D3D11DrawerResources::drawSimpleRectTexturedUV(ID3D11ShaderResourceView* texture, const Vec4f& transform, const Color& tintColor, bool alphaTest, const float* vertexData, size_t numVertices)
{
	if (numVertices == 0)
		return;

	if (!ensureDynamicVertexBuffer(*mDevice.Get(), mInternal.mDynamicTexturedVertexBuffer, mInternal.mDynamicTexturedVertexCapacity, numVertices * sizeof(float) * 4))
		return;
	if (!updateDynamicVertexBuffer(*mContext.Get(), *mInternal.mDynamicTexturedVertexBuffer.Get(), vertexData, numVertices * sizeof(float) * 4))
		return;

	SimpleRectConstants constants;
	constants.mTransform = transform;
	constants.mTintColor = tintColor;
	constants.mAddedColor = Color::TRANSPARENT;
	constants.mAlphaTest = alphaTest ? 1 : 0;
	updateConstantBuffer(*mContext.Get(), *mInternal.mSimpleRectConstants.Get(), constants);

	const UINT stride = sizeof(float) * 4;
	const UINT offset = 0;
	ID3D11Buffer* vertexBuffer = mInternal.mDynamicTexturedVertexBuffer.Get();
	mContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
	mInternal.mSimpleRectTexturedUVShader.bind(*mContext.Get());
	ID3D11Buffer* constantBuffer = mInternal.mSimpleRectConstants.Get();
	mContext->VSSetConstantBuffers(0, 1, &constantBuffer);
	mContext->PSSetConstantBuffers(0, 1, &constantBuffer);
	mContext->PSSetShaderResources(0, 1, &texture);
	ID3D11SamplerState* sampler = getSamplerState(mSamplingMode, mWrapMode);
	mContext->PSSetSamplers(0, 1, &sampler);
	mContext->Draw((UINT)numVertices, 0);
}

void D3D11DrawerResources::drawSimpleRectIndexed(const Recti& rect, const Vec2i& targetSize, ID3D11ShaderResourceView* texture, const Vec2i& textureSize, ID3D11ShaderResourceView* paletteTexture, const Color& tintColor, const Color& addedColor, bool alphaTest)
{
	SimpleRectConstants constants;
	constants.mTransform = makeTransform(rect, targetSize);
	constants.mTintColor = tintColor;
	constants.mAddedColor = addedColor;
	constants.mTextureSize[0] = textureSize.x;
	constants.mTextureSize[1] = textureSize.y;
	constants.mAlphaTest = alphaTest ? 1 : 0;
	constants.mShadowHighlightMode = RenderParts::instance().getPaletteManager().useShadowHighlightMode() ? 1 : 0;
	updateConstantBuffer(*mContext.Get(), *mInternal.mSimpleRectConstants.Get(), constants);

	const UINT stride = sizeof(float) * 2;
	const UINT offset = 0;
	ID3D11Buffer* vertexBuffer = mInternal.mSimpleQuadVertexBuffer.Get();
	mContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
	mInternal.mSimpleRectIndexedShader.bind(*mContext.Get());
	ID3D11Buffer* constantBuffer = mInternal.mSimpleRectConstants.Get();
	mContext->VSSetConstantBuffers(0, 1, &constantBuffer);
	mContext->PSSetConstantBuffers(0, 1, &constantBuffer);

	ID3D11ShaderResourceView* shaderResources[2] = { texture, paletteTexture };
	mContext->PSSetShaderResources(0, 2, shaderResources);
	ID3D11SamplerState* sampler = getSamplerState(SamplingMode::POINT, TextureWrapMode::CLAMP);
	mContext->PSSetSamplers(0, 1, &sampler);
	mContext->Draw(6, 0);
}

void D3D11DrawerResources::drawUpscaledRect(const Recti& rect, const Recti& fullViewport, const d3d11::TextureResource& texture)
{
	(void)fullViewport;
	getUpscaler().renderImage(rect, texture);
}

void D3D11DrawerResources::drawMeshVertexColor(const Vec4f& transform, const float* vertexData, size_t numVertices)
{
	if (numVertices == 0)
		return;

	if (!ensureDynamicVertexBuffer(*mDevice.Get(), mInternal.mDynamicVertexColorBuffer, mInternal.mDynamicVertexColorCapacity, numVertices * sizeof(float) * 6))
		return;
	if (!updateDynamicVertexBuffer(*mContext.Get(), *mInternal.mDynamicVertexColorBuffer.Get(), vertexData, numVertices * sizeof(float) * 6))
		return;

	SimpleRectConstants constants;
	constants.mTransform = transform;
	updateConstantBuffer(*mContext.Get(), *mInternal.mSimpleRectConstants.Get(), constants);

	const UINT stride = sizeof(float) * 6;
	const UINT offset = 0;
	ID3D11Buffer* vertexBuffer = mInternal.mDynamicVertexColorBuffer.Get();
	mContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
	mInternal.mSimpleRectVertexColorShader.bind(*mContext.Get());
	ID3D11Buffer* constantBuffer = mInternal.mSimpleRectConstants.Get();
	mContext->VSSetConstantBuffers(0, 1, &constantBuffer);
	mContext->PSSetConstantBuffers(0, 1, &constantBuffer);
	mContext->Draw((UINT)numVertices, 0);
}

const d3d11::TextureResource& D3D11DrawerResources::getCustomPaletteTexture(const PaletteBase& primaryPalette, const PaletteBase& secondaryPalette)
{
	const uint64 combinedKey = primaryPalette.getKey() ^ (secondaryPalette.getKey() << 32) ^ (secondaryPalette.getKey() >> 32);
	PaletteData& data = mCustomPalettes[combinedKey];
	if (data.mBitmap.getSize() != PALETTE_TEXTURE_SIZE)
		data.mBitmap.create(PALETTE_TEXTURE_SIZE);

	updatePalette(data, primaryPalette, secondaryPalette);
	return data.mTexture;
}

const Vec2i& D3D11DrawerResources::getPaletteTextureSize() const
{
	return PALETTE_TEXTURE_SIZE;
}

D3D11SpriteTextureManager& D3D11DrawerResources::getSpriteTextureManager() const
{
	return mInternal.mSpriteTextureManager;
}

D3D11Upscaler& D3D11DrawerResources::getUpscaler()
{
	D3D11Upscaler::Type upscalerType = D3D11Upscaler::Type::DEFAULT;

	const int filtering = Configuration::instance().mFiltering;
	const int scanlines = Configuration::instance().mScanlines;
	if (scanlines > 0 && filtering < 3)
	{
		upscalerType = D3D11Upscaler::Type::SOFT;
	}
	else
	{
		switch (filtering)
		{
			default:
			case 0:
				upscalerType = D3D11Upscaler::Type::DEFAULT;
				break;

			case 1:
			case 2:
				upscalerType = D3D11Upscaler::Type::SOFT;
				break;

			case 3:
				upscalerType = D3D11Upscaler::Type::XBRZ;
				break;

			case 4:
			case 5:
			case 6:
				upscalerType = D3D11Upscaler::Type::HQX;
				break;
		}
	}

	return *mInternal.mUpscalers[(size_t)upscalerType];
}

bool D3D11DrawerResources::updatePalette(PaletteData& data, const PaletteBase& primaryPalette, const PaletteBase& secondaryPalette)
{
	data.mSecondsSinceLastUse = 0.0f;
	if (!data.mTexture.isValid())
	{
		data.mChangeCounters[0] = (uint16)(primaryPalette.getChangeCounter() - 1);
		data.mChangeCounters[1] = (uint16)(secondaryPalette.getChangeCounter() - 1);
	}
	const bool primaryChanged = updatePaletteBitmap(primaryPalette, data.mBitmap, 0, data.mChangeCounters[0]);
	const bool secondaryChanged = updatePaletteBitmap(secondaryPalette, data.mBitmap, 2, data.mChangeCounters[1]);
	if (!primaryChanged && !secondaryChanged)
		return false;

	return updateTexture(data.mTexture, data.mBitmap.getSize(), DXGI_FORMAT_R8G8B8A8_UNORM, data.mBitmap.getData(), data.mBitmap.getWidth() * sizeof(uint32), false);
}

bool D3D11DrawerResources::updatePaletteBitmap(const PaletteBase& palette, Bitmap& bitmap, int offsetY, uint16& changeCounter)
{
	if (changeCounter == palette.getChangeCounter())
		return false;

	uint32* dst = bitmap.getPixelPointer(0, offsetY);
	palette.dumpColors(dst, palette.getSize());
	changeCounter = palette.getChangeCounter();
	return true;
}

#endif
