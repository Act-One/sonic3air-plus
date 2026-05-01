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

#include <rmxmedia.h>
#include <memory>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <dxgi1_5.h>
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

class D3D11RenderResources;
class D3D11SpriteTextureManager;
class D3D11Upscaler;
class PaletteBase;

namespace d3d11
{
	struct TextureResource
	{
		Vec2i mSize;
		DXGI_FORMAT mFormat = DXGI_FORMAT_UNKNOWN;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> mTexture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mShaderResourceView;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> mRenderTargetView;

		inline bool isValid() const  { return mTexture != nullptr; }
		inline void reset()
		{
			mTexture.Reset();
			mShaderResourceView.Reset();
			mRenderTargetView.Reset();
			mSize = Vec2i();
			mFormat = DXGI_FORMAT_UNKNOWN;
		}
	};
}


class D3D11DrawerResources final
{
friend class D3D11RenderResources;

public:
	struct PaletteData
	{
		Bitmap mBitmap;
		d3d11::TextureResource mTexture;
		uint16 mChangeCounters[2] = { 0 };
		float mSecondsSinceLastUse = 0.0f;
	};

public:
	D3D11DrawerResources();
	~D3D11DrawerResources();

	bool startup();
	void shutdown();

	void clearAllCaches();
	void refresh(float deltaSeconds);

	inline bool isSetupSuccessful() const  { return mSetupSuccessful; }

	bool ensureWindowResources(SDL_Window* window);
	void present(bool useVSync);

	void setBlendMode(BlendMode blendMode);
	inline void setSamplingMode(SamplingMode samplingMode)	{ mSamplingMode = samplingMode; }
	inline void setWrapMode(TextureWrapMode wrapMode)		{ mWrapMode = wrapMode; }
	inline BlendMode getBlendMode() const					{ return mBlendMode; }
	inline SamplingMode getSamplingMode() const				{ return mSamplingMode; }
	inline TextureWrapMode getWrapMode() const				{ return mWrapMode; }
	ID3D11SamplerState* getSamplerState(SamplingMode samplingMode, TextureWrapMode wrapMode) const;

	inline ID3D11Device* getDevice() const								{ return mDevice.Get(); }
	inline ID3D11DeviceContext* getContext() const						{ return mContext.Get(); }
	inline ID3D11RenderTargetView* getWindowRenderTargetView() const	{ return mWindowRenderTargetView.Get(); }
	inline ID3D11RenderTargetView* getCurrentRenderTargetView() const	{ return mCurrentRenderTargetView.Get(); }
	inline const Vec2i& getWindowSize() const							{ return mWindowSize; }
	inline const Recti& getCurrentViewport() const						{ return mCurrentViewport; }
	inline bool hasScissorRect() const									{ return mHasScissorRect; }
	inline const Recti& getCurrentScissorRect() const					{ return mCurrentScissorRect; }

	void bindRenderTarget(ID3D11RenderTargetView* renderTargetView, ID3D11DepthStencilView* depthStencilView, const Recti& viewport);
	void setScissorRect(const Recti* rect);
	void clearRenderTarget(ID3D11RenderTargetView* renderTargetView, const Color& color);
	void clearDepthStencil(ID3D11DepthStencilView* depthStencilView, float depth = 0.0f);

	bool updateTexture(d3d11::TextureResource& texture, const Vec2i& size, DXGI_FORMAT format, const void* data, UINT rowPitch, bool allowRenderTarget = false);
	bool readTextureToBitmap(const d3d11::TextureResource& texture, Bitmap& outBitmap);

	void drawSimpleRectColored(const Recti& rect, const Vec2i& targetSize, const Color& color);
	void drawSimpleRectTextured(const Recti& rect, const Vec2i& targetSize, ID3D11ShaderResourceView* texture, const Color& tintColor, const Color& addedColor, bool alphaTest);
	void drawSimpleRectTexturedUV(ID3D11ShaderResourceView* texture, const Vec4f& transform, const Color& tintColor, bool alphaTest, const float* vertexData, size_t numVertices);
	void drawSimpleRectIndexed(const Recti& rect, const Vec2i& targetSize, ID3D11ShaderResourceView* texture, const Vec2i& textureSize, ID3D11ShaderResourceView* paletteTexture, const Color& tintColor, const Color& addedColor, bool alphaTest);
	void drawMeshVertexColor(const Vec4f& transform, const float* vertexData, size_t numVertices);
	void drawUpscaledRect(const Recti& rect, const Recti& fullViewport, const d3d11::TextureResource& texture);

	const d3d11::TextureResource& getCustomPaletteTexture(const PaletteBase& primaryPalette, const PaletteBase& secondaryPalette);
	const Vec2i& getPaletteTextureSize() const;

	D3D11SpriteTextureManager& getSpriteTextureManager() const;
	D3D11Upscaler& getUpscaler();

private:
	bool updatePalette(PaletteData& data, const PaletteBase& primaryPalette, const PaletteBase& secondaryPalette);
	bool updatePaletteBitmap(const PaletteBase& palette, Bitmap& bitmap, int offsetY, uint16& changeCounter);

private:
	bool mSetupSuccessful = false;
	BlendMode mBlendMode = BlendMode::OPAQUE;
	SamplingMode mSamplingMode = SamplingMode::POINT;
	TextureWrapMode mWrapMode = TextureWrapMode::CLAMP;
	Vec2i mWindowSize;
	Microsoft::WRL::ComPtr<ID3D11Device> mDevice;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> mContext;
	Microsoft::WRL::ComPtr<IDXGISwapChain1> mSwapChain;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> mWindowRenderTargetView;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> mCurrentRenderTargetView;
	Recti mCurrentViewport;
	UINT mSwapChainFlags = 0;
	bool mAllowTearingSupported = false;
	bool mHasScissorRect = false;
	Recti mCurrentScissorRect;

	struct Internal;
	Internal& mInternal;
	std::unordered_map<uint64, PaletteData> mCustomPalettes;
	float mSecondsSinceLastPaletteCleanup = 0.0f;
};

#endif
