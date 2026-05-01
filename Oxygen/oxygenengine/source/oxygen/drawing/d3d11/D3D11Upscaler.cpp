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

#include "oxygen/drawing/d3d11/D3D11Upscaler.h"

#include "oxygen/application/Configuration.h"
#include "oxygen/drawing/d3d11/D3D11DrawerResources.h"
#include "oxygen/helper/FileHelper.h"


namespace
{
	inline std::wstring getShaderAssetPath(const wchar_t* filename)
	{
		std::wstring path = Configuration::instance().mEngineDataPath;
		rmx::FileIO::normalizePath(path, true);
		path += filename;
		return path;
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

	const D3D11_INPUT_ELEMENT_DESC INPUT_LAYOUT_P2[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	const D3D_SHADER_MACRO HQ2X_MACROS[] = { { "HQ2X", "1" }, { nullptr, nullptr } };
	const D3D_SHADER_MACRO HQ3X_MACROS[] = { { "HQ3X", "1" }, { nullptr, nullptr } };
	const D3D_SHADER_MACRO HQ4X_MACROS[] = { { "HQ4X", "1" }, { nullptr, nullptr } };

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
}


D3D11Upscaler::D3D11Upscaler(Type type, D3D11DrawerResources& resources) :
	mType(type),
	mResources(resources)
{
}

bool D3D11Upscaler::startup()
{
	shutdown();

	ID3D11Device* device = mResources.getDevice();
	if (nullptr == device)
		return false;

	if (!ensureBuffer(*device, mConstantsBuffer, sizeof(UpscalerConstants), D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE))
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
		if (FAILED(device->CreateBuffer(&desc, &initialData, &mFullscreenQuadVertexBuffer)))
			return false;
	}

	mFilterLinear = false;

	switch (mType)
	{
		default:
		case Type::DEFAULT:
			return true;

		case Type::SOFT:
		{
			mFilterLinear = true;
			mShaders.resize(2);
			return mShaders[0].initialize(*device, UPSCALER_FULLSCREEN_VS, UPSCALER_SOFT_PS, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "D3D11UpscalerSoft0")
				&& mShaders[1].initialize(*device, UPSCALER_FULLSCREEN_VS, UPSCALER_SOFT_PS, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "D3D11UpscalerSoft1");
		}

		case Type::XBRZ:
		{
			mShaders.resize(2);
			return mShaders[0].initialize(*device, UPSCALER_FULLSCREEN_VS, UPSCALER_XBRZ_PASS0_PS, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "D3D11UpscalerXBRZPass0")
				&& mShaders[1].initialize(*device, UPSCALER_FULLSCREEN_VS, UPSCALER_XBRZ_PASS1_PS, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "D3D11UpscalerXBRZPass1");
		}

		case Type::HQX:
		{
			mShaders.resize(3);
			mLookupTextures.resize(3);
			mLookupTextures[0].mImagePath = getShaderAssetPath(L"data/shader/hq2x.png");
			mLookupTextures[1].mImagePath = getShaderAssetPath(L"data/shader/hq3x.png");
			mLookupTextures[2].mImagePath = getShaderAssetPath(L"data/shader/hq4x.png");
			return mShaders[0].initialize(*device, UPSCALER_FULLSCREEN_VS, UPSCALER_HQX_PS, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "D3D11UpscalerHQ2X", HQ2X_MACROS)
				&& mShaders[1].initialize(*device, UPSCALER_FULLSCREEN_VS, UPSCALER_HQX_PS, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "D3D11UpscalerHQ3X", HQ3X_MACROS)
				&& mShaders[2].initialize(*device, UPSCALER_FULLSCREEN_VS, UPSCALER_HQX_PS, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "D3D11UpscalerHQ4X", HQ4X_MACROS);
		}
	}
}

void D3D11Upscaler::shutdown()
{
	mShaders.clear();
	mLookupTextures.clear();
	mPass0Texture.reset();
	mConstantsBuffer.Reset();
	mFullscreenQuadVertexBuffer.Reset();
}

bool D3D11Upscaler::loadLookupTexture(LookupTexture& lookupTexture)
{
	if (lookupTexture.mInitialized)
		return lookupTexture.mTexture.isValid();

	Bitmap bitmap;
	if (!FileHelper::loadBitmap(bitmap, lookupTexture.mImagePath))
	{
		RMX_ERROR("Failed to load upscaler texture " << WString(lookupTexture.mImagePath).toStdString(), return false);
	}

	lookupTexture.mInitialized = true;
	return mResources.updateTexture(lookupTexture.mTexture, bitmap.getSize(), DXGI_FORMAT_R8G8B8A8_UNORM, bitmap.getData(), bitmap.getWidth() * sizeof(uint32), false);
}

bool D3D11Upscaler::ensurePass0Texture(const Vec2i& size)
{
	return mResources.updateTexture(mPass0Texture, size, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, 0, true);
}

void D3D11Upscaler::drawFullscreenPass(const Recti& viewportRect, D3D11Shader& shader, const UpscalerConstants& constants, ID3D11ShaderResourceView* texture0, ID3D11ShaderResourceView* texture1, ID3D11ShaderResourceView* lookupTexture, SamplingMode samplingMode)
{
	ID3D11DeviceContext* context = mResources.getContext();
	if (nullptr == context)
		return;

	updateConstantBuffer(*context, *mConstantsBuffer.Get(), constants);

	const UINT stride = sizeof(float) * 2;
	const UINT offset = 0;
	ID3D11Buffer* vertexBuffer = mFullscreenQuadVertexBuffer.Get();
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
	shader.bind(*context);

	ID3D11Buffer* constantBuffer = mConstantsBuffer.Get();
	context->VSSetConstantBuffers(0, 1, &constantBuffer);
	context->PSSetConstantBuffers(0, 1, &constantBuffer);

	ID3D11ShaderResourceView* shaderResourceViews[2] = { texture0, (nullptr != lookupTexture) ? lookupTexture : texture1 };
	context->PSSetShaderResources(0, 2, shaderResourceViews);

	ID3D11SamplerState* sampler = mResources.getSamplerState(samplingMode, TextureWrapMode::CLAMP);
	context->PSSetSamplers(0, 1, &sampler);
	context->Draw(6, 0);

	static ID3D11ShaderResourceView* const NULL_SRVS[2] = { nullptr, nullptr };
	context->PSSetShaderResources(0, 2, NULL_SRVS);
}

void D3D11Upscaler::renderImage(const Recti& rect, const d3d11::TextureResource& texture)
{
	if (!texture.isValid() || rect.empty())
		return;

	const int filtering = Configuration::instance().mFiltering;
	const int scanlines = Configuration::instance().mScanlines;

	if (mType == Type::DEFAULT)
	{
		const SamplingMode previousSamplingMode = mResources.getSamplingMode();
		const TextureWrapMode previousWrapMode = mResources.getWrapMode();
		mResources.setSamplingMode(SamplingMode::POINT);
		mResources.setWrapMode(TextureWrapMode::CLAMP);
		mResources.drawSimpleRectTextured(rect, mResources.getCurrentViewport().getSize(), texture.mShaderResourceView.Get(), Color::WHITE, Color::TRANSPARENT, false);
		mResources.setSamplingMode(previousSamplingMode);
		mResources.setWrapMode(previousWrapMode);
		return;
	}

	D3D11Shader* firstShader = nullptr;
	D3D11Shader* secondShader = nullptr;
	LookupTexture* lookupTexture = nullptr;
	bool isMultiPass = false;
	SamplingMode samplingMode = mFilterLinear ? SamplingMode::BILINEAR : SamplingMode::POINT;

	switch (mType)
	{
		case Type::SOFT:
			firstShader = &mShaders[(scanlines > 0) ? 1 : 0];
			secondShader = firstShader;
			break;

		case Type::XBRZ:
			firstShader = &mShaders[0];
			secondShader = &mShaders[1];
			isMultiPass = true;
			samplingMode = SamplingMode::POINT;
			break;

		case Type::HQX:
		{
			const int lookupTextureIndex = clamp(filtering - 4, 0, 2);
			firstShader = &mShaders[lookupTextureIndex];
			secondShader = firstShader;
			lookupTexture = &mLookupTextures[lookupTextureIndex];
			samplingMode = SamplingMode::POINT;
			if (!loadLookupTexture(*lookupTexture))
				return;
			break;
		}

		default:
			return;
	}

	if (nullptr == firstShader || nullptr == secondShader)
		return;

	const Recti savedViewport = mResources.getCurrentViewport();
	const Recti savedScissorRect = mResources.getCurrentScissorRect();
	const bool hadScissorRect = mResources.hasScissorRect();
	ID3D11RenderTargetView* const savedRenderTargetView = mResources.getCurrentRenderTargetView();
	const BlendMode previousBlendMode = mResources.getBlendMode();
	const SamplingMode previousSamplingMode = mResources.getSamplingMode();
	const TextureWrapMode previousWrapMode = mResources.getWrapMode();

	mResources.setBlendMode(BlendMode::OPAQUE);
	mResources.setSamplingMode(samplingMode);
	mResources.setWrapMode(TextureWrapMode::CLAMP);

	UpscalerConstants constants;
	constants.mGameResolution[0] = texture.mSize.x;
	constants.mGameResolution[1] = texture.mSize.y;
	constants.mOutputSize[0] = rect.width;
	constants.mOutputSize[1] = rect.height;
	if (mType == Type::SOFT)
	{
		float pixelFactor = rect.height / (float)std::max(texture.mSize.y, 1);
		pixelFactor *= (filtering == 1) ? 2.0f : 1.0f;
		constants.mPixelFactor = clamp(pixelFactor, 1.0f, 1000.0f);
		constants.mScanlinesIntensity = (scanlines > 0) ? ((float)scanlines * 0.25f) : 0.0f;
	}

	if (isMultiPass)
	{
		if (!ensurePass0Texture(texture.mSize))
			return;

		mResources.bindRenderTarget(mPass0Texture.mRenderTargetView.Get(), nullptr, Recti(0, 0, texture.mSize.x, texture.mSize.y));
		mResources.setScissorRect(nullptr);
		drawFullscreenPass(Recti(0, 0, texture.mSize.x, texture.mSize.y), *firstShader, constants, texture.mShaderResourceView.Get(), nullptr, nullptr, samplingMode);
	}

	const Recti drawViewport(savedViewport.x + rect.x, savedViewport.y + rect.y, rect.width, rect.height);
	mResources.bindRenderTarget(savedRenderTargetView, nullptr, drawViewport);
	if (hadScissorRect)
		mResources.setScissorRect(&savedScissorRect);
	else
		mResources.setScissorRect(nullptr);

	if (isMultiPass)
	{
		drawFullscreenPass(drawViewport, *secondShader, constants, mPass0Texture.mShaderResourceView.Get(), texture.mShaderResourceView.Get(), nullptr, samplingMode);
	}
	else
	{
		drawFullscreenPass(drawViewport, *secondShader, constants, texture.mShaderResourceView.Get(), nullptr, (nullptr != lookupTexture) ? lookupTexture->mTexture.mShaderResourceView.Get() : nullptr, samplingMode);
	}

	mResources.bindRenderTarget(savedRenderTargetView, nullptr, savedViewport);
	if (hadScissorRect)
		mResources.setScissorRect(&savedScissorRect);
	else
		mResources.setScissorRect(nullptr);

	mResources.setSamplingMode(previousSamplingMode);
	mResources.setWrapMode(previousWrapMode);
	mResources.setBlendMode(previousBlendMode);
}

#endif
