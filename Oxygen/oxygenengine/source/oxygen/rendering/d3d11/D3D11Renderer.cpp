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

#include "oxygen/rendering/d3d11/D3D11Renderer.h"

#include "oxygen/application/Configuration.h"
#include "oxygen/drawing/d3d11/D3D11Drawer.h"
#include "oxygen/drawing/d3d11/D3D11DrawerTexture.h"
#include "oxygen/drawing/d3d11/D3D11SpriteTextureManager.h"
#include "oxygen/rendering/Geometry.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/rendering/d3d11/D3D11Shader.h"
#include "oxygen/rendering/sprite/ComponentSprite.h"

#include <cstring>
#include <unordered_set>


namespace
{
	bool hasPrefix(std::string_view str, std::string_view prefix)
	{
		return str.length() >= prefix.length() && str.compare(0, prefix.length(), prefix) == 0;
	}

	bool isBlueSpheresComponentDebugSprite(const SpriteCollection::Item& item)
	{
		if (item.mSourceInfo.mType != SpriteCollection::SourceInfo::Type::SPRITE_FILE)
			return false;

		const std::string_view identifier = item.mSourceInfo.mSourceIdentifier;
		return (identifier == "bluespheres_glow_foreground") || hasPrefix(identifier, "bluespheres_shadow_");
	}

	const char* getBlendModeName(BlendMode blendMode)
	{
		switch (blendMode)
		{
			case BlendMode::OPAQUE:			return "opaque";
			case BlendMode::ALPHA:			return "alpha";
			case BlendMode::ONE_BIT:		return "one-bit";
			case BlendMode::ADDITIVE:		return "additive";
			case BlendMode::SUBTRACTIVE:	return "subtractive";
			case BlendMode::MULTIPLICATIVE: return "multiplicative";
			case BlendMode::MINIMUM:		return "minimum";
			case BlendMode::MAXIMUM:		return "maximum";
			default:						return "unknown";
		}
	}

	void logBlueSpheresComponentSpriteOnce(const SpriteGeometry& geometry, const renderitems::ComponentSpriteInfo& spriteInfo)
	{
		if (nullptr == spriteInfo.mCacheItem || nullptr == spriteInfo.mCacheItem->mSprite || !isBlueSpheresComponentDebugSprite(*spriteInfo.mCacheItem))
			return;

		static std::unordered_set<uint64> sLoggedSpriteKeys;
		if (!sLoggedSpriteKeys.insert(spriteInfo.mCacheItem->mKey).second)
			return;

		const ComponentSprite& sprite = *static_cast<ComponentSprite*>(spriteInfo.mCacheItem->mSprite);
		const Bitmap& bitmap = sprite.getBitmap();
		const uint32* pixels = bitmap.getData();
		const int pixelCount = bitmap.getWidth() * bitmap.getHeight();

		uint8 minAlpha = 0xff;
		uint8 maxAlpha = 0x00;
		int nonZeroAlphaPixels = 0;
		for (int i = 0; i < pixelCount; ++i)
		{
			const uint8 alpha = ((const uint8*)&pixels[i])[3];
			minAlpha = std::min(minAlpha, alpha);
			maxAlpha = std::max(maxAlpha, alpha);
			if (alpha != 0)
				++nonZeroAlphaPixels;
		}

		RMX_LOG_INFO("D3D11 Blue Spheres component sprite '" << spriteInfo.mCacheItem->mSourceInfo.mSourceIdentifier
			<< "': queue=" << geometry.mRenderQueue
			<< ", blend=" << getBlendModeName(spriteInfo.mBlendMode)
			<< ", priority=" << (spriteInfo.mPriorityFlag ? 1 : 0)
			<< ", pos=(" << spriteInfo.mInterpolatedPosition.x << "," << spriteInfo.mInterpolatedPosition.y << ")"
			<< ", size=(" << spriteInfo.mSize.x << "x" << spriteInfo.mSize.y << ")"
			<< ", pivot=(" << spriteInfo.mPivotOffset.x << "," << spriteInfo.mPivotOffset.y << ")"
			<< ", useGlobalTint=" << (spriteInfo.mUseGlobalComponentTint ? 1 : 0)
			<< ", tint=(" << spriteInfo.mTintColor.r << "," << spriteInfo.mTintColor.g << "," << spriteInfo.mTintColor.b << "," << spriteInfo.mTintColor.a << ")"
			<< ", added=(" << spriteInfo.mAddedColor.r << "," << spriteInfo.mAddedColor.g << "," << spriteInfo.mAddedColor.b << "," << spriteInfo.mAddedColor.a << ")"
			<< ", bitmapAlpha=[" << (int)minAlpha << "," << (int)maxAlpha << "]"
			<< ", nonZeroAlphaPixels=" << nonZeroAlphaPixels);
	}

	void dumpBlueSpheresShadowDebugOnce(const renderitems::ComponentSpriteInfo& spriteInfo, const d3d11::TextureResource& spriteTexture, const d3d11::TextureResource& renderTargetTexture, D3D11DrawerResources& drawerResources)
	{
		if (nullptr == spriteInfo.mCacheItem || spriteInfo.mCacheItem->mSourceInfo.mSourceIdentifier.find("bluespheres_shadow_") != 0)
			return;

		static bool sDumped = false;
		if (sDumped)
			return;
		sDumped = true;

		const std::wstring basePath = Configuration::instance().mAppDataPath;

		Bitmap bitmap;
		if (drawerResources.readTextureToBitmap(spriteTexture, bitmap))
		{
			const std::wstring filename = basePath + L"d3d11_bs_shadow_texture.bmp";
			if (bitmap.save(filename))
				RMX_LOG_INFO("Saved D3D11 Blue Spheres shadow texture dump to " << WString(filename).toStdString());
		}

		if (drawerResources.readTextureToBitmap(renderTargetTexture, bitmap))
		{
			const std::wstring filename = basePath + L"d3d11_bs_shadow_target.bmp";
			if (bitmap.save(filename))
				RMX_LOG_INFO("Saved D3D11 Blue Spheres render target dump to " << WString(filename).toStdString());
		}
	}

	D3D11DrawerResources& getDrawerResources()
	{
		DrawerInterface* drawer = EngineMain::instance().getDrawer().getActiveDrawer();
		RMX_ASSERT(nullptr != drawer, "Expected an active drawer");
		RMX_ASSERT(drawer->getType() == Drawer::Type::DIRECT3D11, "Expected the Direct3D 11 drawer");
		return static_cast<D3D11Drawer*>(drawer)->getResources();
	}

	struct FullscreenConstants
	{
		Vec2f mTexelOffset = Vec2f(0.0f, 0.0f);
		Vec2f mPadding0 = Vec2f(0.0f, 0.0f);
		Vec4f mKernel = Vec4f(1.0f, 0.0f, 0.0f, 0.0f);
	};

	struct PlaneConstants
	{
		int32 mActiveRect[4] = { 0, 0, 0, 0 };
		int32 mGameResolution[2] = { 0, 0 };
		float mPaletteOffset = 0.0f;
		int32 mPriorityFlag = 0;
		int32 mPlayfieldSize[4] = { 0, 0, 0, 0 };
		int32 mVScrollOffsetBias = 0;
		int32 mScrollOffsetX = 0;
		int32 mScrollOffsetY = 0;
		int32 mUseHorizontalScrolling = 0;
		int32 mUseVerticalScrolling = 0;
		int32 mNoRepeat = 0;
		int32 mPadding0 = 0;
		int32 mPadding1 = 0;
		int32 mPadding2 = 0;
		int32 mPadding3[3] = { 0, 0, 0 };
	};

	struct VdpSpriteConstants
	{
		int32 mSize[2] = { 0, 0 };
		int32 mFirstPattern = 0;
		int32 mPadding0 = 0;
		int32 mPosition[3] = { 0, 0, 0 };
		int32 mWaterLevel = 0;
		int32 mGameResolution[2] = { 0, 0 };
		int32 mPadding1[2] = { 0, 0 };
		Vec4f mTintColor = Color::WHITE;
		Vec4f mAddedColor = Color::TRANSPARENT;
		int32 mShadowHighlightMode = 0;
		int32 mPadding2[3] = { 0, 0, 0 };
	};

	struct CustomSpriteConstants
	{
		int32 mSize[2] = { 0, 0 };
		int32 mAlphaTest = 0;
		int32 mPadding0 = 0;
		int32 mPosition[3] = { 0, 0, 0 };
		int32 mWaterLevel = 0;
		int32 mPivotOffset[2] = { 0, 0 };
		int32 mGameResolution[2] = { 0, 0 };
		Vec4f mTransformation = Vec4f(1.0f, 0.0f, 0.0f, 1.0f);
		Vec4f mTintColor = Color::WHITE;
		Vec4f mAddedColor = Color::TRANSPARENT;
		int32 mAtex = 0;
		int32 mShadowHighlightMode = 0;
		int32 mPadding1[2] = { 0, 0 };
	};

	inline bool ensureBuffer(ID3D11Device& device, Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer, UINT size)
	{
		if (buffer != nullptr)
			return true;

		const UINT alignedSize = (size + 15u) & ~15u;
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = alignedSize;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
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

	inline bool createQuadVertexBuffer(ID3D11Device& device, Microsoft::WRL::ComPtr<ID3D11Buffer>& vertexBuffer)
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
		return SUCCEEDED(device.CreateBuffer(&desc, &initialData, &vertexBuffer));
	}

	inline bool createDepthBuffer(ID3D11Device& device, const Vec2i& size, Microsoft::WRL::ComPtr<ID3D11Texture2D>& depthTexture, Microsoft::WRL::ComPtr<ID3D11DepthStencilView>& depthStencilView)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = (UINT)std::max(size.x, 1);
		desc.Height = (UINT)std::max(size.y, 1);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_D32_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

		depthTexture.Reset();
		depthStencilView.Reset();
		if (FAILED(device.CreateTexture2D(&desc, nullptr, &depthTexture)))
			return false;
		return SUCCEEDED(device.CreateDepthStencilView(depthTexture.Get(), nullptr, &depthStencilView));
	}

	const D3D11_INPUT_ELEMENT_DESC INPUT_LAYOUT_P2[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	const char* FULLSCREEN_VS = R"(
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

	const char* COPY_PS = R"(
Texture2D MainTexture : register(t0);
SamplerState MainSampler : register(s0);
float4 PSMain(float4 position : SV_POSITION, float2 uv0 : TEXCOORD0) : SV_TARGET
{
	return MainTexture.SampleLevel(MainSampler, uv0, 0.0f);
})";

	const char* BLUR_PS = R"(
cbuffer BlurCB : register(b0)
{
	float2 TexelOffset;
	float2 Padding0;
	float4 Kernel;
};
Texture2D MainTexture : register(t0);
SamplerState MainSampler : register(s0);
float4 PSMain(float4 position : SV_POSITION, float2 uv0 : TEXCOORD0) : SV_TARGET
{
	float3 color00 = MainTexture.SampleLevel(MainSampler, uv0 + float2(-TexelOffset.x, -TexelOffset.y), 0.0f).rgb;
	float3 color01 = MainTexture.SampleLevel(MainSampler, uv0 + float2(0.0f, -TexelOffset.y), 0.0f).rgb;
	float3 color02 = MainTexture.SampleLevel(MainSampler, uv0 + float2(TexelOffset.x, -TexelOffset.y), 0.0f).rgb;
	float3 color10 = MainTexture.SampleLevel(MainSampler, uv0 + float2(-TexelOffset.x, 0.0f), 0.0f).rgb;
	float3 color11 = MainTexture.SampleLevel(MainSampler, uv0, 0.0f).rgb;
	float3 color12 = MainTexture.SampleLevel(MainSampler, uv0 + float2(TexelOffset.x, 0.0f), 0.0f).rgb;
	float3 color20 = MainTexture.SampleLevel(MainSampler, uv0 + float2(-TexelOffset.x, TexelOffset.y), 0.0f).rgb;
	float3 color21 = MainTexture.SampleLevel(MainSampler, uv0 + float2(0.0f, TexelOffset.y), 0.0f).rgb;
	float3 color22 = MainTexture.SampleLevel(MainSampler, uv0 + float2(TexelOffset.x, TexelOffset.y), 0.0f).rgb;
	float3 color = color00 * Kernel.w + color01 * Kernel.z + color02 * Kernel.w
				 + color10 * Kernel.y + color11 * Kernel.x + color12 * Kernel.y
				 + color20 * Kernel.w + color21 * Kernel.z + color22 * Kernel.w;
	return float4(color, 1.0f);
})";

	const char* PLANE_SHADER = R"(
cbuffer PlaneCB : register(b0)
{
	int4 ActiveRect;
	int2 GameResolution;
	float PaletteOffset;
	int PriorityFlag;
	int4 PlayfieldSize;
	int VScrollOffsetBias;
	int ScrollOffsetX;
	int ScrollOffsetY;
	int UseHorizontalScrolling;
	int UseVerticalScrolling;
	int NoRepeat;
	int Padding0;
	int Padding1;
	int Padding2;
};
Texture2D<uint> PatternCacheTexture : register(t0);
Texture2D<float4> PaletteTexture : register(t1);
Texture2D<uint> IndexTexture : register(t2);
Texture2D<int> HScrollOffsetsTexture : register(t3);
Texture2D<int> VScrollOffsetsTexture : register(t4);
SamplerState PaletteSampler : register(s0);
struct VSInput
{
	float2 position : POSITION;
};
struct VSOutput
{
	float4 position : SV_POSITION;
	float2 localOffset : TEXCOORD0;
};
float4 getPaletteColor(uint paletteIndex, float paletteOffsetY)
{
	uint paletteX = paletteIndex & 0xff;
	uint paletteY = paletteIndex >> 8;
	float2 samplePosition = float2((float(paletteX) + 0.5f) / 256.0f, (float(paletteY) + 0.5f) / 4.0f + paletteOffsetY);
	return PaletteTexture.SampleLevel(PaletteSampler, samplePosition, 0.0f);
}
VSOutput VSMain(VSInput input)
{
	VSOutput output;
	int2 screenPosition;
	screenPosition.x = ActiveRect.x + (int)(input.position.x * (float)ActiveRect.z + 0.5f);
	screenPosition.y = ActiveRect.y + (int)(input.position.y * (float)ActiveRect.w + 0.5f);
	output.localOffset = float2(screenPosition);
	output.position.x = float(screenPosition.x) / float(GameResolution.x) * 2.0f - 1.0f;
	output.position.y = 1.0f - float(screenPosition.y) / float(GameResolution.y) * 2.0f;
	output.position.z = (PriorityFlag != 0) ? 0.75f : 0.0f;
	output.position.w = 1.0f;
	return output;
}
float4 PSMain(VSOutput input) : SV_TARGET
{
	int ix = (int)input.localOffset.x;
	int iy = (int)input.localOffset.y;
	int scrollOffsetLookupX = (UseHorizontalScrolling != 0) ? HScrollOffsetsTexture.Load(int3(iy & 0xff, 0, 0)).r : ScrollOffsetX;
	int scrollOffsetLookupY = ScrollOffsetY;
	if (UseVerticalScrolling != 0)
	{
		int vx = ix - VScrollOffsetBias;
		vx = (vx & 0x1f0) >> 4;
		scrollOffsetLookupY = VScrollOffsetsTexture.Load(int3(vx, 0, 0)).r;
	}
	ix += scrollOffsetLookupX;
	iy += scrollOffsetLookupY;
	ix = ix & 0x0fff;
	iy = iy & (PlayfieldSize.y - 1);
	if (NoRepeat != 0)
	{
		if (ix >= PlayfieldSize.x)
			discard;
	}
	else
	{
		ix &= (PlayfieldSize.x - 1);
	}
	int patternX = ix / 8;
	int patternY = iy / 8;
	int localX = ix - patternX * 8;
	int localY = iy - patternY * 8;
	uint patternIndex = IndexTexture.Load(int3(patternX + patternY * PlayfieldSize.z, 0, 0)).r;
	if ((((patternIndex & 0x8000u) != 0u) ? 1 : 0) != PriorityFlag)
		discard;
	uint atex = (patternIndex >> 9) & 0x30u;
	localX = ((patternIndex & 0x0800u) == 0u) ? localX : (7 - localX);
	localY = ((patternIndex & 0x1000u) == 0u) ? localY : (7 - localY);
	uint paletteIndex = PatternCacheTexture.Load(int3(localX + localY * 8, (int)(patternIndex & 0x07ffu), 0)).r + atex;
	float4 color = getPaletteColor(paletteIndex, PaletteOffset);
	if (color.a < 0.01f)
		discard;
	return color;
})";

	const char* VDP_SPRITE_SHADER = R"(
cbuffer SpriteCB : register(b0)
{
	int2 Size;
	int FirstPattern;
	int Padding0;
	int3 Position;
	int WaterLevel;
	int2 GameResolution;
	int2 Padding1;
	float4 TintColor;
	float4 AddedColor;
	int ShadowHighlightMode;
	int3 Padding2;
};
Texture2D<uint> PatternCacheTexture : register(t0);
Texture2D<float4> PaletteTexture : register(t1);
SamplerState PaletteSampler : register(s0);
struct VSInput
{
	float2 position : POSITION;
};
struct VSOutput
{
	float4 position : SV_POSITION;
	float3 localOffset : TEXCOORD0;
};
float4 getPaletteColor(uint paletteIndex, float paletteOffsetY)
{
	uint paletteX = paletteIndex & 0xff;
	uint paletteY = paletteIndex >> 8;
	float2 samplePosition = float2((float(paletteX) + 0.5f) / 256.0f, (float(paletteY) + 0.5f) / 4.0f + paletteOffsetY);
	return PaletteTexture.SampleLevel(PaletteSampler, samplePosition, 0.0f);
}
VSOutput VSMain(VSInput input)
{
	VSOutput output;
	output.localOffset.x = input.position.x * float(Size.x * 8);
	output.localOffset.y = input.position.y * float(Size.y * 8);
	float2 transformedVertex = input.position;
	if ((FirstPattern & 0x0800) != 0)
		transformedVertex.x = 1.0f - transformedVertex.x;
	if ((FirstPattern & 0x1000) != 0)
		transformedVertex.y = 1.0f - transformedVertex.y;
	transformedVertex.x = float(Position.x) + transformedVertex.x * float(Size.x * 8);
	transformedVertex.y = float(Position.y) + transformedVertex.y * float(Size.y * 8);
	output.position.x = transformedVertex.x / float(GameResolution.x) * 2.0f - 1.0f;
	output.position.y = 1.0f - transformedVertex.y / float(GameResolution.y) * 2.0f;
	output.position.z = (Position.z != 0) ? 0.75f : 0.5f;
	output.position.w = 1.0f;
	output.localOffset.z = transformedVertex.y - float(WaterLevel);
	return output;
}
float4 PSMain(VSOutput input) : SV_TARGET
{
	int ix = (int)input.localOffset.x;
	int iy = (int)input.localOffset.y;
	int patternX = ix / 8;
	int patternY = iy / 8;
	int localX = ix - patternX * 8;
	int localY = iy - patternY * 8;
	uint patternIndex = (uint)(FirstPattern + patternX * Size.y + patternY);
	uint atex = (patternIndex >> 9) & 0x30u;
	uint paletteIndex = PatternCacheTexture.Load(int3(localX + localY * 8, (int)(patternIndex & 0x07ffu), 0)).r + atex;
	if (ShadowHighlightMode != 0 && (paletteIndex == 0x3eu || paletteIndex == 0x3fu))
	{
		const float intensity = (paletteIndex == 0x3fu) ? 1.0f : 0.0f;
		return float4(intensity, intensity, intensity, 0.5f);
	}
	float4 color = getPaletteColor(paletteIndex, clamp(input.localOffset.z, 0.0f, 0.5f));
	color = float4(AddedColor.rgb, 0.0f) + color * TintColor;
	if (color.a < 0.01f)
		discard;
	return color;
})";

	const char* PALETTE_SPRITE_SHADER = R"(
cbuffer SpriteCB : register(b0)
{
	int2 Size;
	int AlphaTest;
	int Padding0;
	int3 Position;
	int WaterLevel;
	int2 PivotOffset;
	int2 GameResolution;
	float4 Transformation;
	float4 TintColor;
	float4 AddedColor;
	int Atex;
	int ShadowHighlightMode;
	int2 Padding1;
};
Texture2D<uint> SpriteTexture : register(t0);
Texture2D<float4> PaletteTexture : register(t1);
SamplerState PointSampler : register(s0);
struct VSInput
{
	float2 position : POSITION;
};
struct VSOutput
{
	float4 position : SV_POSITION;
	float3 localOffset : TEXCOORD0;
};
float4 getPaletteColor(uint paletteIndex, float paletteOffsetY)
{
	uint paletteX = paletteIndex & 0xff;
	uint paletteY = paletteIndex >> 8;
	float2 samplePosition = float2((float(paletteX) + 0.5f) / 256.0f, (float(paletteY) + 0.5f) / 4.0f + paletteOffsetY);
	return PaletteTexture.SampleLevel(PointSampler, samplePosition, 0.0f);
}
VSOutput VSMain(VSInput input)
{
	VSOutput output;
	output.localOffset.x = input.position.x * float(Size.x);
	output.localOffset.y = input.position.y * float(Size.y);
	float2 v = output.localOffset.xy + float2(PivotOffset);
	float2 transformedVertex;
	transformedVertex.x = v.x * Transformation.x + v.y * Transformation.y;
	transformedVertex.y = v.x * Transformation.z + v.y * Transformation.w;
	transformedVertex.x = float(Position.x) + transformedVertex.x;
	transformedVertex.y = float(Position.y) + transformedVertex.y;
	output.position.x = transformedVertex.x / float(GameResolution.x) * 2.0f - 1.0f;
	output.position.y = 1.0f - transformedVertex.y / float(GameResolution.y) * 2.0f;
	output.position.z = (Position.z != 0) ? 0.75f : 0.5f;
	output.position.w = 1.0f;
	output.localOffset.z = transformedVertex.y - float(WaterLevel);
	return output;
}
float4 PSMain(VSOutput input) : SV_TARGET
{
	int2 coord = int2(input.localOffset.xy);
	coord = clamp(coord, int2(0, 0), Size - 1);
	uint paletteIndex = (uint)Atex + SpriteTexture.Load(int3(coord, 0)).r;
	if (ShadowHighlightMode != 0 && (paletteIndex == 0x3eu || paletteIndex == 0x3fu))
	{
		const float intensity = (paletteIndex == 0x3fu) ? 1.0f : 0.0f;
		return float4(intensity, intensity, intensity, 0.5f);
	}
	float4 color = getPaletteColor(paletteIndex, clamp(input.localOffset.z, 0.0f, 0.5f));
	color = float4(AddedColor.rgb, 0.0f) + color * TintColor;
	if (AlphaTest != 0 && color.a < 0.01f)
		discard;
	return color;
})";

	const char* COMPONENT_SPRITE_SHADER = R"(
cbuffer SpriteCB : register(b0)
{
	int2 Size;
	int AlphaTest;
	int Padding0;
	int3 Position;
	int WaterLevel;
	int2 PivotOffset;
	int2 GameResolution;
	float4 Transformation;
	float4 TintColor;
	float4 AddedColor;
	int Atex;
	int3 Padding1;
};
Texture2D<float4> SpriteTexture : register(t0);
SamplerState PointSampler : register(s0);
struct VSInput
{
	float2 position : POSITION;
};
struct VSOutput
{
	float4 position : SV_POSITION;
	float2 localOffset : TEXCOORD0;
};
VSOutput VSMain(VSInput input)
{
	VSOutput output;
	float2 localOffset;
	localOffset.x = input.position.x * float(Size.x);
	localOffset.y = input.position.y * float(Size.y);
	float2 v = localOffset.xy + float2(PivotOffset);
	float2 transformedVertex;
	transformedVertex.x = v.x * Transformation.x + v.y * Transformation.y;
	transformedVertex.y = v.x * Transformation.z + v.y * Transformation.w;
	transformedVertex.x = float(Position.x) + transformedVertex.x;
	transformedVertex.y = float(Position.y) + transformedVertex.y;
	output.position.x = transformedVertex.x / float(GameResolution.x) * 2.0f - 1.0f;
	output.position.y = 1.0f - transformedVertex.y / float(GameResolution.y) * 2.0f;
	output.position.z = (Position.z != 0) ? 0.75f : 0.5f;
	output.position.w = 1.0f;
	output.localOffset = localOffset;
	return output;
}
float4 PSMain(VSOutput input) : SV_TARGET
{
	int2 coord = int2(input.localOffset.xy);
	coord = clamp(coord, int2(0, 0), Size - 1);
	float4 color = SpriteTexture.Load(int3(coord, 0));
	color = float4(AddedColor.rgb, 0.0f) + color * TintColor;
	if (AlphaTest != 0 && color.a < 0.01f)
		discard;
	return color;
})";

	inline const Vec4f& getBlurKernel(int blurValue)
	{
		static const Vec4f kernels[] =
		{
			Vec4f(1.0f, 0.0f, 0.0f, 0.0f),
			Vec4f(0.765625f, 0.05859375f, 0.05859375f, 0.00390625f),
			Vec4f(0.5625f, 0.09375f, 0.09375f, 0.015625f),
			Vec4f(0.390625f, 0.1171875f, 0.1171875f, 0.03515625f),
			Vec4f(0.25f, 0.125f, 0.125f, 0.0625f)
		};
		return kernels[blurValue % 5];
	}

	static_assert(sizeof(FullscreenConstants) % 16 == 0, "Fullscreen constant buffer must stay 16-byte aligned");
	static_assert(sizeof(PlaneConstants) % 16 == 0, "Plane constant buffer must stay 16-byte aligned");
	static_assert(sizeof(VdpSpriteConstants) % 16 == 0, "VDP sprite constant buffer must stay 16-byte aligned");
	static_assert(sizeof(CustomSpriteConstants) % 16 == 0, "Custom sprite constant buffer must stay 16-byte aligned");
}


struct d3d11renderer::Internal
{
	D3D11Shader mCopyShader;
	D3D11Shader mBlurShader;
	D3D11Shader mPlaneShader;
	D3D11Shader mVdpSpriteShader;
	D3D11Shader mPaletteSpriteShader;
	D3D11Shader mComponentSpriteShader;

	Microsoft::WRL::ComPtr<ID3D11Buffer> mFullscreenConstants;
	Microsoft::WRL::ComPtr<ID3D11Buffer> mPlaneConstants;
	Microsoft::WRL::ComPtr<ID3D11Buffer> mVdpSpriteConstants;
	Microsoft::WRL::ComPtr<ID3D11Buffer> mCustomSpriteConstants;
	Microsoft::WRL::ComPtr<ID3D11Buffer> mQuadVertexBuffer;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> mDepthTexture;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> mDepthStencilView;
	d3d11::TextureResource mProcessingTexture;

	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> mDepthDisabledState;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> mDepthWriteState;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> mDepthTestState;

	Geometry::Type mLastRenderedGeometryType = Geometry::Type::UNDEFINED;
	RenderItem::Type mLastRenderedSpriteType = RenderItem::Type::INVALID;
	bool mIsRenderingToProcessingBuffer = false;
};


D3D11Renderer::D3D11Renderer(RenderParts& renderParts, DrawerTexture& outputTexture) :
	Renderer(RENDERER_TYPE_ID, renderParts, outputTexture),
	mDrawerResources(getDrawerResources()),
	mRenderResources(renderParts, mDrawerResources),
	mInternal(*new d3d11renderer::Internal())
{
}

D3D11Renderer::~D3D11Renderer()
{
	delete &mInternal;
}

void D3D11Renderer::initialize()
{
	mGameResolution = Configuration::instance().mGameScreen;

	ID3D11Device* device = mDrawerResources.getDevice();
	RMX_ASSERT(nullptr != device, "Expected a Direct3D 11 device");

	ensureBuffer(*device, mInternal.mFullscreenConstants, sizeof(FullscreenConstants));
	ensureBuffer(*device, mInternal.mPlaneConstants, sizeof(PlaneConstants));
	ensureBuffer(*device, mInternal.mVdpSpriteConstants, sizeof(VdpSpriteConstants));
	ensureBuffer(*device, mInternal.mCustomSpriteConstants, sizeof(CustomSpriteConstants));
	createQuadVertexBuffer(*device, mInternal.mQuadVertexBuffer);

	mInternal.mCopyShader.initialize(*device, FULLSCREEN_VS, COPY_PS, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "D3D11Copy");
	mInternal.mBlurShader.initialize(*device, FULLSCREEN_VS, BLUR_PS, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "D3D11Blur");
	mInternal.mPlaneShader.initialize(*device, PLANE_SHADER, PLANE_SHADER, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "D3D11Plane");
	mInternal.mVdpSpriteShader.initialize(*device, VDP_SPRITE_SHADER, VDP_SPRITE_SHADER, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "D3D11VdpSprite");
	mInternal.mPaletteSpriteShader.initialize(*device, PALETTE_SPRITE_SHADER, PALETTE_SPRITE_SHADER, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "D3D11PaletteSprite");
	mInternal.mComponentSpriteShader.initialize(*device, COMPONENT_SPRITE_SHADER, COMPONENT_SPRITE_SHADER, INPUT_LAYOUT_P2, _countof(INPUT_LAYOUT_P2), "D3D11ComponentSprite");

	{
		D3D11_DEPTH_STENCIL_DESC desc = {};
		desc.DepthEnable = FALSE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		device->CreateDepthStencilState(&desc, &mInternal.mDepthDisabledState);

		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		device->CreateDepthStencilState(&desc, &mInternal.mDepthWriteState);

		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		desc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
		device->CreateDepthStencilState(&desc, &mInternal.mDepthTestState);
	}

	setGameResolution(mGameResolution);
	mRenderResources.initialize();
}

void D3D11Renderer::reset()
{
	mRenderResources.clearAllCaches();
}

void D3D11Renderer::setGameResolution(const Vec2i& gameResolution)
{
	mGameResolution = gameResolution;
	mGameScreenTexture.setupAsRenderTarget(mGameResolution);
	mDrawerResources.updateTexture(mInternal.mProcessingTexture, mGameResolution, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, 0, true);
	createDepthBuffer(*mDrawerResources.getDevice(), mGameResolution, mInternal.mDepthTexture, mInternal.mDepthStencilView);
	clearGameScreen();
}

void D3D11Renderer::clearGameScreen()
{
	D3D11DrawerTexture* gameScreenTexture = mGameScreenTexture.getImplementation<D3D11DrawerTexture>();
	if (nullptr == gameScreenTexture || nullptr == gameScreenTexture->mTextureResource.mRenderTargetView)
		return;

	mDrawerResources.bindRenderTarget(gameScreenTexture->mTextureResource.mRenderTargetView.Get(), mInternal.mDepthStencilView.Get(), Recti(0, 0, mGameResolution.x, mGameResolution.y));
	mDrawerResources.clearRenderTarget(gameScreenTexture->mTextureResource.mRenderTargetView.Get(), Color::BLACK);
	mDrawerResources.clearDepthStencil(mInternal.mDepthStencilView.Get(), 0.0f);
}

void D3D11Renderer::renderGameScreen(const std::vector<Geometry*>& geometries)
{
	startRendering();
	mRenderResources.refresh();
	mDrawerResources.setSamplingMode(SamplingMode::POINT);
	mDrawerResources.setWrapMode(TextureWrapMode::CLAMP);
	RenderParts& renderParts = RenderParts::instance();

	D3D11DrawerTexture* gameScreenTexture = mGameScreenTexture.getImplementation<D3D11DrawerTexture>();
	if (nullptr == gameScreenTexture || nullptr == gameScreenTexture->mTextureResource.mRenderTargetView)
		return;

	mDrawerResources.bindRenderTarget(gameScreenTexture->mTextureResource.mRenderTargetView.Get(), mInternal.mDepthStencilView.Get(), Recti(0, 0, mGameResolution.x, mGameResolution.y));

	const Color backdropColor = renderParts.getPaletteManager().getBackdropColor();
	mDrawerResources.clearRenderTarget(gameScreenTexture->mTextureResource.mRenderTargetView.Get(), backdropColor);
	mDrawerResources.clearDepthStencil(mInternal.mDepthStencilView.Get(), 0.0f);

	mInternal.mIsRenderingToProcessingBuffer = false;
	if (Configuration::instance().mBackgroundBlur > 0)
	{
		for (Geometry* geometry : geometries)
		{
			if (geometry->getType() == Geometry::Type::EFFECT_BLUR)
			{
				mInternal.mIsRenderingToProcessingBuffer = true;
				mDrawerResources.bindRenderTarget(mInternal.mProcessingTexture.mRenderTargetView.Get(), nullptr, Recti(0, 0, mGameResolution.x, mGameResolution.y));
				mDrawerResources.clearRenderTarget(mInternal.mProcessingTexture.mRenderTargetView.Get(), backdropColor);
				break;
			}
		}
	}

	const bool usingSpriteMask = isUsingSpriteMask(geometries);
	mInternal.mLastRenderedGeometryType = Geometry::Type::UNDEFINED;
	mInternal.mLastRenderedSpriteType = RenderItem::Type::INVALID;

	uint16 lastRenderQueue = 0xffff;
	for (size_t i = 0; i < geometries.size(); ++i)
	{
		if (!progressRendering())
			break;

		const uint16 renderQueue = geometries[i]->mRenderQueue;
		if (usingSpriteMask && lastRenderQueue < 0x8000 && renderQueue >= 0x8000)
		{
			mDrawerResources.bindRenderTarget(mInternal.mProcessingTexture.mRenderTargetView.Get(), nullptr, Recti(0, 0, mGameResolution.x, mGameResolution.y));
			mDrawerResources.setBlendMode(BlendMode::OPAQUE);
			mDrawerResources.drawSimpleRectTextured(Recti(0, 0, mGameResolution.x, mGameResolution.y), mGameResolution, gameScreenTexture->mTextureResource.mShaderResourceView.Get(), Color::WHITE, Color::TRANSPARENT, false);
			mDrawerResources.bindRenderTarget(gameScreenTexture->mTextureResource.mRenderTargetView.Get(), mInternal.mDepthStencilView.Get(), Recti(0, 0, mGameResolution.x, mGameResolution.y));
		}

		const Geometry& geometry = *geometries[i];
		switch (geometry.getType())
		{
			case Geometry::Type::PLANE:
			{
				const PlaneGeometry& pg = static_cast<const PlaneGeometry&>(geometry);
				if (!PlaneManager::isRenderablePlaneIndex(pg.mPlaneIndex))
				{
					static int sLoggedInvalidPlaneGeometryCount = 0;
					if (sLoggedInvalidPlaneGeometryCount < 8)
					{
						++sLoggedInvalidPlaneGeometryCount;
						RMX_LOG_INFO("D3D11Renderer: skipping invalid plane geometry with plane index " << pg.mPlaneIndex
							<< ", rect=(" << pg.mActiveRect.x << "," << pg.mActiveRect.y << "," << pg.mActiveRect.width << "," << pg.mActiveRect.height
							<< "), scrollOffsets=" << (int)pg.mScrollOffsets << ", renderQueue=0x" << rmx::hexString(pg.mRenderQueue, 4));
					}
					break;
				}

				if (!mInternal.mIsRenderingToProcessingBuffer)
				{
					if (pg.mPriorityFlag)
						mDrawerResources.getContext()->OMSetDepthStencilState(mInternal.mDepthWriteState.Get(), 0);
					else
						mDrawerResources.getContext()->OMSetDepthStencilState(mInternal.mDepthDisabledState.Get(), 0);
				}
				else
				{
					mDrawerResources.getContext()->OMSetDepthStencilState(mInternal.mDepthDisabledState.Get(), 0);
				}

				mDrawerResources.setBlendMode(BlendMode::ONE_BIT);

				const ScrollOffsetsManager& som = renderParts.getScrollOffsetsManager();
				PlaneConstants constants;
				constants.mActiveRect[0] = pg.mActiveRect.x;
				constants.mActiveRect[1] = pg.mActiveRect.y;
				constants.mActiveRect[2] = pg.mActiveRect.width;
				constants.mActiveRect[3] = pg.mActiveRect.height;
				constants.mGameResolution[0] = mGameResolution.x;
				constants.mGameResolution[1] = mGameResolution.y;
				const Vec4i playfieldSize = (pg.mPlaneIndex <= PlaneManager::PLANE_A) ? renderParts.getPlaneManager().getPlayfieldSizeForShaders() : Vec4i(512, 256, 64, 32);
				constants.mPlayfieldSize[0] = playfieldSize.x;
				constants.mPlayfieldSize[1] = playfieldSize.y;
				constants.mPlayfieldSize[2] = playfieldSize.z;
				constants.mPlayfieldSize[3] = playfieldSize.w;
				constants.mPriorityFlag = pg.mPriorityFlag ? 1 : 0;
				constants.mUseHorizontalScrolling = (pg.mPlaneIndex != PlaneManager::PLANE_W) ? 1 : 0;
				constants.mUseVerticalScrolling = (pg.mPlaneIndex != PlaneManager::PLANE_W && som.getVerticalScrolling()) ? 1 : 0;
				constants.mNoRepeat = som.getHorizontalScrollNoRepeat(pg.mScrollOffsets) ? 1 : 0;
				constants.mVScrollOffsetBias = renderParts.getScrollOffsetsManager().getVerticalScrollOffsetBias();
				if (pg.mPlaneIndex == PlaneManager::PLANE_W)
				{
					const Vec2i& wScroll = renderParts.getScrollOffsetsManager().getPlaneWScrollOffset();
					constants.mUseHorizontalScrolling = 0;
					constants.mUseVerticalScrolling = 0;
					constants.mScrollOffsetX = wScroll.x;
					constants.mScrollOffsetY = wScroll.y;
				}
				else
				{
					constants.mScrollOffsetX = renderParts.getScrollOffsetsManager().getScrollOffsetsH(pg.mScrollOffsets)[0];
					constants.mScrollOffsetY = renderParts.getScrollOffsetsManager().getScrollOffsetsV(pg.mScrollOffsets)[0];
				}

				updateConstantBuffer(*mDrawerResources.getContext(), *mInternal.mPlaneConstants.Get(), constants);
				const UINT stride = sizeof(float) * 2;
				const UINT offset = 0;
				ID3D11Buffer* vertexBuffer = mInternal.mQuadVertexBuffer.Get();
				mDrawerResources.getContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				mDrawerResources.getContext()->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
				mInternal.mPlaneShader.bind(*mDrawerResources.getContext());
				ID3D11Buffer* constantBuffer = mInternal.mPlaneConstants.Get();
				mDrawerResources.getContext()->VSSetConstantBuffers(0, 1, &constantBuffer);
				mDrawerResources.getContext()->PSSetConstantBuffers(0, 1, &constantBuffer);

				ID3D11ShaderResourceView* shaderResources[] =
				{
					mRenderResources.getPatternCacheTexture().mShaderResourceView.Get(),
					mRenderResources.getMainPaletteTexture().mShaderResourceView.Get(),
					mRenderResources.getPlanePatternsTexture(pg.mPlaneIndex).mShaderResourceView.Get(),
					mRenderResources.getHScrollOffsetsTexture(pg.mScrollOffsets).mShaderResourceView.Get(),
					mRenderResources.getVScrollOffsetsTexture(pg.mScrollOffsets).mShaderResourceView.Get()
				};
				mDrawerResources.getContext()->PSSetShaderResources(0, 5, shaderResources);
				ID3D11SamplerState* sampler = mDrawerResources.getSamplerState(SamplingMode::POINT, TextureWrapMode::CLAMP);
				mDrawerResources.getContext()->PSSetSamplers(0, 1, &sampler);

				const int splitY = renderParts.getPaletteManager().mSplitPositionY;
				if (splitY > pg.mActiveRect.y && splitY < pg.mActiveRect.y + pg.mActiveRect.height)
				{
					PlaneConstants upperConstants = constants;
					upperConstants.mActiveRect[3] = splitY - pg.mActiveRect.y;
					upperConstants.mPaletteOffset = 0.0f;
					updateConstantBuffer(*mDrawerResources.getContext(), *mInternal.mPlaneConstants.Get(), upperConstants);
					mDrawerResources.getContext()->Draw(6, 0);

					PlaneConstants lowerConstants = constants;
					lowerConstants.mActiveRect[1] = splitY;
					lowerConstants.mActiveRect[3] = pg.mActiveRect.y + pg.mActiveRect.height - splitY;
					lowerConstants.mPaletteOffset = 0.5f;
					updateConstantBuffer(*mDrawerResources.getContext(), *mInternal.mPlaneConstants.Get(), lowerConstants);
					mDrawerResources.getContext()->Draw(6, 0);
				}
				else
				{
					constants.mPaletteOffset = (pg.mActiveRect.y >= splitY) ? 0.5f : 0.0f;
					updateConstantBuffer(*mDrawerResources.getContext(), *mInternal.mPlaneConstants.Get(), constants);
					mDrawerResources.getContext()->Draw(6, 0);
				}
				mInternal.mLastRenderedGeometryType = Geometry::Type::PLANE;
				break;
			}

			case Geometry::Type::SPRITE:
			{
				const SpriteGeometry& sg = static_cast<const SpriteGeometry&>(geometry);
				const bool needsRefresh = (mInternal.mLastRenderedGeometryType != Geometry::Type::SPRITE || mInternal.mLastRenderedSpriteType != sg.mSpriteInfo.getType());
				if (needsRefresh)
				{
					if (sg.mSpriteInfo.getType() != RenderItem::Type::SPRITE_MASK)
						mDrawerResources.getContext()->OMSetDepthStencilState(mInternal.mDepthTestState.Get(), 0);
					else
						mDrawerResources.getContext()->OMSetDepthStencilState(mInternal.mDepthDisabledState.Get(), 0);
					mInternal.mLastRenderedGeometryType = Geometry::Type::SPRITE;
					mInternal.mLastRenderedSpriteType = sg.mSpriteInfo.getType();
				}

				switch (sg.mSpriteInfo.getType())
				{
					case RenderItem::Type::VDP_SPRITE:
					{
						const renderitems::VdpSpriteInfo& spriteInfo = static_cast<const renderitems::VdpSpriteInfo&>(sg.mSpriteInfo);
						mDrawerResources.setBlendMode(spriteInfo.mBlendMode);

						VdpSpriteConstants constants;
						constants.mSize[0] = spriteInfo.mSize.x;
						constants.mSize[1] = spriteInfo.mSize.y;
						constants.mFirstPattern = spriteInfo.mFirstPattern;
						constants.mPosition[0] = spriteInfo.mInterpolatedPosition.x;
						constants.mPosition[1] = spriteInfo.mInterpolatedPosition.y;
						constants.mPosition[2] = spriteInfo.mPriorityFlag ? 1 : 0;
						constants.mWaterLevel = renderParts.getPaletteManager().mSplitPositionY;
						constants.mGameResolution[0] = mGameResolution.x;
						constants.mGameResolution[1] = mGameResolution.y;
						const PaletteManager& paletteManager = renderParts.getPaletteManager();
						constants.mTintColor = spriteInfo.mTintColor;
						constants.mAddedColor = spriteInfo.mAddedColor;
						constants.mShadowHighlightMode = paletteManager.useShadowHighlightMode() ? 1 : 0;
						if (spriteInfo.mUseGlobalComponentTint)
							paletteManager.applyGlobalComponentTint(constants.mTintColor, constants.mAddedColor);

						updateConstantBuffer(*mDrawerResources.getContext(), *mInternal.mVdpSpriteConstants.Get(), constants);
						const UINT stride = sizeof(float) * 2;
						const UINT offset = 0;
						ID3D11Buffer* vertexBuffer = mInternal.mQuadVertexBuffer.Get();
						mDrawerResources.getContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
						mDrawerResources.getContext()->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
						mInternal.mVdpSpriteShader.bind(*mDrawerResources.getContext());
						ID3D11Buffer* constantBuffer = mInternal.mVdpSpriteConstants.Get();
						mDrawerResources.getContext()->VSSetConstantBuffers(0, 1, &constantBuffer);
						mDrawerResources.getContext()->PSSetConstantBuffers(0, 1, &constantBuffer);
						ID3D11ShaderResourceView* shaderResources[] =
						{
							mRenderResources.getPatternCacheTexture().mShaderResourceView.Get(),
							mRenderResources.getMainPaletteTexture().mShaderResourceView.Get()
						};
						mDrawerResources.getContext()->PSSetShaderResources(0, 2, shaderResources);
						ID3D11SamplerState* sampler = mDrawerResources.getSamplerState(SamplingMode::POINT, TextureWrapMode::CLAMP);
						mDrawerResources.getContext()->PSSetSamplers(0, 1, &sampler);
						mDrawerResources.getContext()->Draw(6, 0);
						break;
					}

					case RenderItem::Type::PALETTE_SPRITE:
					{
						const renderitems::PaletteSpriteInfo& spriteInfo = static_cast<const renderitems::PaletteSpriteInfo&>(sg.mSpriteInfo);
						if (nullptr == spriteInfo.mCacheItem || spriteInfo.mSize.x == 0 || spriteInfo.mSize.y == 0)
							break;

						mDrawerResources.setBlendMode(spriteInfo.mBlendMode);
						d3d11::TextureResource* texture = mDrawerResources.getSpriteTextureManager().getPaletteSpriteTexture(*spriteInfo.mCacheItem, spriteInfo.mUseUpscaledSprite, mDrawerResources);
						if (nullptr == texture)
							break;

						CustomSpriteConstants constants;
						constants.mSize[0] = spriteInfo.mSize.x;
						constants.mSize[1] = spriteInfo.mSize.y;
						constants.mAlphaTest = (spriteInfo.mBlendMode != BlendMode::OPAQUE) ? 1 : 0;
						constants.mPosition[0] = spriteInfo.mInterpolatedPosition.x;
						constants.mPosition[1] = spriteInfo.mInterpolatedPosition.y;
						constants.mPosition[2] = spriteInfo.mPriorityFlag ? 1 : 0;
						constants.mWaterLevel = renderParts.getPaletteManager().mSplitPositionY;
						constants.mPivotOffset[0] = spriteInfo.mPivotOffset.x;
						constants.mPivotOffset[1] = spriteInfo.mPivotOffset.y;
						constants.mGameResolution[0] = mGameResolution.x;
						constants.mGameResolution[1] = mGameResolution.y;
						constants.mTransformation = spriteInfo.mTransformation.mMatrix;
						const PaletteManager& paletteManager = renderParts.getPaletteManager();
						constants.mTintColor = spriteInfo.mTintColor;
						constants.mAddedColor = spriteInfo.mAddedColor;
						constants.mShadowHighlightMode = paletteManager.useShadowHighlightMode() ? 1 : 0;
						if (spriteInfo.mUseGlobalComponentTint)
							paletteManager.applyGlobalComponentTint(constants.mTintColor, constants.mAddedColor);
						constants.mAtex = spriteInfo.mAtex;

						updateConstantBuffer(*mDrawerResources.getContext(), *mInternal.mCustomSpriteConstants.Get(), constants);
						const UINT stride = sizeof(float) * 2;
						const UINT offset = 0;
						ID3D11Buffer* vertexBuffer = mInternal.mQuadVertexBuffer.Get();
						mDrawerResources.getContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
						mDrawerResources.getContext()->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
						mInternal.mPaletteSpriteShader.bind(*mDrawerResources.getContext());
						ID3D11Buffer* constantBuffer = mInternal.mCustomSpriteConstants.Get();
						mDrawerResources.getContext()->VSSetConstantBuffers(0, 1, &constantBuffer);
						mDrawerResources.getContext()->PSSetConstantBuffers(0, 1, &constantBuffer);
						const d3d11::TextureResource& paletteTexture = mRenderResources.getPaletteTexture(spriteInfo.mPrimaryPalette, spriteInfo.mSecondaryPalette);
						ID3D11ShaderResourceView* shaderResources[] =
						{
							texture->mShaderResourceView.Get(),
							paletteTexture.mShaderResourceView.Get()
						};
						mDrawerResources.getContext()->PSSetShaderResources(0, 2, shaderResources);
						ID3D11SamplerState* sampler = mDrawerResources.getSamplerState(SamplingMode::POINT, TextureWrapMode::CLAMP);
						mDrawerResources.getContext()->PSSetSamplers(0, 1, &sampler);
						mDrawerResources.getContext()->Draw(6, 0);
						break;
					}

					case RenderItem::Type::COMPONENT_SPRITE:
					{
						const renderitems::ComponentSpriteInfo& spriteInfo = static_cast<const renderitems::ComponentSpriteInfo&>(sg.mSpriteInfo);
						if (nullptr == spriteInfo.mCacheItem)
							break;

						logBlueSpheresComponentSpriteOnce(sg, spriteInfo);
						mDrawerResources.setBlendMode(spriteInfo.mBlendMode);
						d3d11::TextureResource* texture = mDrawerResources.getSpriteTextureManager().getComponentSpriteTexture(*spriteInfo.mCacheItem, mDrawerResources);
						if (nullptr == texture)
							break;

						CustomSpriteConstants constants;
						constants.mSize[0] = spriteInfo.mSize.x;
						constants.mSize[1] = spriteInfo.mSize.y;
						constants.mAlphaTest = (spriteInfo.mBlendMode != BlendMode::OPAQUE) ? 1 : 0;
						constants.mPosition[0] = spriteInfo.mInterpolatedPosition.x;
						constants.mPosition[1] = spriteInfo.mInterpolatedPosition.y;
						constants.mPosition[2] = spriteInfo.mPriorityFlag ? 1 : 0;
						constants.mPivotOffset[0] = spriteInfo.mPivotOffset.x;
						constants.mPivotOffset[1] = spriteInfo.mPivotOffset.y;
						constants.mGameResolution[0] = mGameResolution.x;
						constants.mGameResolution[1] = mGameResolution.y;
						constants.mTransformation = spriteInfo.mTransformation.mMatrix;
						const PaletteManager& paletteManager = renderParts.getPaletteManager();
						constants.mTintColor = spriteInfo.mTintColor;
						constants.mAddedColor = spriteInfo.mAddedColor;
						if (spriteInfo.mUseGlobalComponentTint)
							paletteManager.applyGlobalComponentTint(constants.mTintColor, constants.mAddedColor);

						updateConstantBuffer(*mDrawerResources.getContext(), *mInternal.mCustomSpriteConstants.Get(), constants);
						const UINT stride = sizeof(float) * 2;
						const UINT offset = 0;
						ID3D11Buffer* vertexBuffer = mInternal.mQuadVertexBuffer.Get();
						mDrawerResources.getContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
						mDrawerResources.getContext()->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
						mInternal.mComponentSpriteShader.bind(*mDrawerResources.getContext());
						ID3D11Buffer* constantBuffer = mInternal.mCustomSpriteConstants.Get();
						mDrawerResources.getContext()->VSSetConstantBuffers(0, 1, &constantBuffer);
						mDrawerResources.getContext()->PSSetConstantBuffers(0, 1, &constantBuffer);
						ID3D11ShaderResourceView* shaderResource = texture->mShaderResourceView.Get();
						mDrawerResources.getContext()->PSSetShaderResources(0, 1, &shaderResource);
						ID3D11SamplerState* sampler = mDrawerResources.getSamplerState(SamplingMode::POINT, TextureWrapMode::CLAMP);
						mDrawerResources.getContext()->PSSetSamplers(0, 1, &sampler);
						mDrawerResources.getContext()->Draw(6, 0);
						dumpBlueSpheresShadowDebugOnce(spriteInfo, *texture, gameScreenTexture->mTextureResource, mDrawerResources);
						break;
					}

					case RenderItem::Type::SPRITE_MASK:
					{
						const renderitems::SpriteMaskInfo& mask = static_cast<const renderitems::SpriteMaskInfo&>(sg.mSpriteInfo);
						mDrawerResources.setBlendMode(BlendMode::OPAQUE);
						const Vec2f uv0((float)mask.mPosition.x / (float)mGameResolution.x, (float)mask.mPosition.y / (float)mGameResolution.y);
						const Vec2f uv1((float)(mask.mPosition.x + mask.mSize.x) / (float)mGameResolution.x, (float)(mask.mPosition.y + mask.mSize.y) / (float)mGameResolution.y);
						const float vertexData[] =
						{
							0.0f, 0.0f, uv0.x, uv0.y,
							0.0f, 1.0f, uv0.x, uv1.y,
							1.0f, 1.0f, uv1.x, uv1.y,
							1.0f, 1.0f, uv1.x, uv1.y,
							1.0f, 0.0f, uv1.x, uv0.y,
							0.0f, 0.0f, uv0.x, uv0.y
						};
						mDrawerResources.drawSimpleRectTexturedUV(mInternal.mProcessingTexture.mShaderResourceView.Get(), Vec4f(-1.0f + 2.0f * (float)mask.mPosition.x / (float)mGameResolution.x, 1.0f - 2.0f * (float)mask.mPosition.y / (float)mGameResolution.y, 2.0f * (float)mask.mSize.x / (float)mGameResolution.x, -2.0f * (float)mask.mSize.y / (float)mGameResolution.y), Color::WHITE, false, vertexData, 6);
						break;
					}

					default:
						break;
				}
				break;
			}

			case Geometry::Type::RECT:
			{
				mDrawerResources.getContext()->OMSetDepthStencilState(mInternal.mDepthDisabledState.Get(), 0);
				mDrawerResources.setBlendMode(BlendMode::ALPHA);
				const RectGeometry& rg = static_cast<const RectGeometry&>(geometry);
				mDrawerResources.drawSimpleRectColored(rg.mRect, mGameResolution, rg.mColor);
				break;
			}

			case Geometry::Type::TEXTURED_RECT:
			{
				mDrawerResources.getContext()->OMSetDepthStencilState(mInternal.mDepthDisabledState.Get(), 0);
				mDrawerResources.setBlendMode(BlendMode::ALPHA);
				const TexturedRectGeometry& tg = static_cast<const TexturedRectGeometry&>(geometry);
				D3D11DrawerTexture* texture = tg.mDrawerTexture.getImplementation<D3D11DrawerTexture>();
				if (nullptr != texture && texture->mTextureResource.isValid())
					mDrawerResources.drawSimpleRectTextured(tg.mRect, mGameResolution, texture->mTextureResource.mShaderResourceView.Get(), tg.mTintColor, tg.mAddedColor, true);
				break;
			}

			case Geometry::Type::EFFECT_BLUR:
			{
				const EffectBlurGeometry& ebg = static_cast<const EffectBlurGeometry&>(geometry);
				mInternal.mIsRenderingToProcessingBuffer = false;
				mDrawerResources.bindRenderTarget(gameScreenTexture->mTextureResource.mRenderTargetView.Get(), nullptr, Recti(0, 0, mGameResolution.x, mGameResolution.y));
				mDrawerResources.setBlendMode(BlendMode::OPAQUE);

				const FullscreenConstants constants =
				{
					Vec2f(1.0f / (float)mGameResolution.x, 1.0f / (float)mGameResolution.y),
			Vec2f(0.0f, 0.0f),
					getBlurKernel(ebg.mBlurValue)
				};
				updateConstantBuffer(*mDrawerResources.getContext(), *mInternal.mFullscreenConstants.Get(), constants);
				const UINT stride = sizeof(float) * 2;
				const UINT offset = 0;
				ID3D11Buffer* vertexBuffer = mInternal.mQuadVertexBuffer.Get();
				mDrawerResources.getContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				mDrawerResources.getContext()->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
				mInternal.mBlurShader.bind(*mDrawerResources.getContext());
				ID3D11Buffer* constantBuffer = mInternal.mFullscreenConstants.Get();
				mDrawerResources.getContext()->PSSetConstantBuffers(0, 1, &constantBuffer);
				ID3D11ShaderResourceView* shaderResource = mInternal.mProcessingTexture.mShaderResourceView.Get();
				mDrawerResources.getContext()->PSSetShaderResources(0, 1, &shaderResource);
				ID3D11SamplerState* sampler = mDrawerResources.getSamplerState(SamplingMode::POINT, TextureWrapMode::CLAMP);
				mDrawerResources.getContext()->PSSetSamplers(0, 1, &sampler);
				mDrawerResources.getContext()->Draw(6, 0);
				break;
			}

			case Geometry::Type::VIEWPORT:
			{
				const ViewportGeometry& vg = static_cast<const ViewportGeometry&>(geometry);
				mDrawerResources.setScissorRect(&vg.mRect);
				break;
			}

			default:
				break;
		}

		lastRenderQueue = renderQueue;
	}

	mDrawerResources.getContext()->OMSetDepthStencilState(mInternal.mDepthDisabledState.Get(), 0);
	mDrawerResources.setScissorRect(nullptr);
}

void D3D11Renderer::renderDebugDraw(int debugDrawMode, const Recti& rect)
{
	(void)debugDrawMode;
	(void)rect;
}

#endif
