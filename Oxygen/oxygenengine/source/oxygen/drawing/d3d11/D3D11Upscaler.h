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

#include <vector>

#include "oxygen/drawing/d3d11/D3D11DrawerResources.h"
#include "oxygen/rendering/d3d11/D3D11Shader.h"


class D3D11DrawerResources;


class D3D11Upscaler
{
public:
	enum class Type
	{
		DEFAULT,
		SOFT,
		XBRZ,
		HQX
	};

	struct UpscalerConstants
	{
		int32 mGameResolution[2] = { 0, 0 };
		int32 mOutputSize[2] = { 0, 0 };
		float mPixelFactor = 1.0f;
		float mScanlinesIntensity = 0.0f;
		float mPadding0[2] = { 0.0f, 0.0f };
	};

public:
	D3D11Upscaler(Type type, D3D11DrawerResources& resources);

	bool startup();
	void shutdown();

	void renderImage(const Recti& rect, const d3d11::TextureResource& texture);

private:
	struct LookupTexture
	{
		bool mInitialized = false;
		std::wstring mImagePath;
		d3d11::TextureResource mTexture;
	};

private:
	bool loadLookupTexture(LookupTexture& lookupTexture);
	bool ensurePass0Texture(const Vec2i& size);
	void drawFullscreenPass(const Recti& viewportRect, D3D11Shader& shader, const UpscalerConstants& constants, ID3D11ShaderResourceView* texture0, ID3D11ShaderResourceView* texture1, ID3D11ShaderResourceView* lookupTexture, SamplingMode samplingMode);

private:
	const Type mType = Type::DEFAULT;
	D3D11DrawerResources& mResources;

	std::vector<D3D11Shader> mShaders;
	std::vector<LookupTexture> mLookupTextures;
	d3d11::TextureResource mPass0Texture;
	Microsoft::WRL::ComPtr<ID3D11Buffer> mConstantsBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> mFullscreenQuadVertexBuffer;

	bool mFilterLinear = false;
};

#endif
