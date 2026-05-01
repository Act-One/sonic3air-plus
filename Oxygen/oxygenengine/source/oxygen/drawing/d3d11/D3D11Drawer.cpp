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

#include "oxygen/drawing/d3d11/D3D11Drawer.h"

#include "oxygen/application/Configuration.h"
#include "oxygen/drawing/d3d11/D3D11DrawerResources.h"
#include "oxygen/drawing/d3d11/D3D11DrawerTexture.h"
#include "oxygen/drawing/d3d11/D3D11SpriteTextureManager.h"
#include "oxygen/drawing/DrawCollection.h"
#include "oxygen/drawing/DrawCommand.h"
#include "oxygen/resources/PaletteCollection.h"
#include "oxygen/resources/SpriteCollection.h"


namespace d3d11drawer
{
	inline bool useAlphaTest(const D3D11DrawerResources& resources)
	{
		return resources.getBlendMode() == BlendMode::ALPHA;
	}

	struct Internal
	{
		bool mSetupSuccessful = false;
		D3D11DrawerResources mResources;
		SDL_Window* mOutputWindow = nullptr;
		Recti mCurrentViewport;
		Vec4f mPixelToViewSpaceTransform;
		std::vector<Recti> mScissorStack;
		bool mInvalidScissorRegion = false;
		Bitmap mTempTextBitmap;
		int mTempTextReservedSize = 0;
		d3d11::TextureResource mTempTextTexture;

		inline Vec4f getTransformOfRectInViewport(const Recti& inputRect) const
		{
			Vec4f transform;
			transform.x = mPixelToViewSpaceTransform.x + (float)inputRect.x * mPixelToViewSpaceTransform.z;
			transform.y = mPixelToViewSpaceTransform.y + (float)inputRect.y * mPixelToViewSpaceTransform.w;
			transform.z = (float)inputRect.width * mPixelToViewSpaceTransform.z;
			transform.w = (float)inputRect.height * mPixelToViewSpaceTransform.w;
			return transform;
		}

		inline bool mayRenderAnything() const
		{
			return !mInvalidScissorRegion;
		}

		void setupViewport(const Recti& viewport)
		{
			mCurrentViewport = viewport;
			mPixelToViewSpaceTransform.x = -1.0f;
			mPixelToViewSpaceTransform.y = 1.0f;
			mPixelToViewSpaceTransform.z = 2.0f / (float)viewport.width;
			mPixelToViewSpaceTransform.w = -2.0f / (float)viewport.height;
		}

		void applyCurrentScissor()
		{
			if (mScissorStack.empty())
			{
				mResources.setScissorRect(nullptr);
				mInvalidScissorRegion = false;
			}
			else
			{
				const Recti& scissorRect = mScissorStack.back();
				mResources.setScissorRect(&scissorRect);
				mInvalidScissorRegion = scissorRect.empty();
			}
		}

		template<typename T>
		void printText(Font& font, const T& text, const Recti& rect, const DrawerPrintOptions& printOptions)
		{
			Vec2i drawPosition;
			font.printBitmap(mTempTextBitmap, drawPosition, rect, text, printOptions.mAlignment, printOptions.mSpacing, &mTempTextReservedSize);
			if (mTempTextBitmap.empty())
				return;

			mResources.updateTexture(mTempTextTexture, mTempTextBitmap.getSize(), DXGI_FORMAT_R8G8B8A8_UNORM, mTempTextBitmap.getData(), mTempTextBitmap.getWidth() * sizeof(uint32), false);
			mResources.setBlendMode(BlendMode::ALPHA);
			mResources.drawSimpleRectTextured(Recti(drawPosition, mTempTextBitmap.getSize()), mCurrentViewport.getSize(), mTempTextTexture.mShaderResourceView.Get(), printOptions.mTintColor, Color::TRANSPARENT, true);
		}
	};
}


D3D11Drawer::D3D11Drawer() :
	mInternal(*new d3d11drawer::Internal())
{
	mInternal.mSetupSuccessful = mInternal.mResources.startup();
}

D3D11Drawer::~D3D11Drawer()
{
	delete &mInternal;
}

bool D3D11Drawer::wasSetupSuccessful()
{
	return mInternal.mSetupSuccessful;
}

void D3D11Drawer::updateDrawer(float deltaSeconds)
{
	mInternal.mResources.refresh(deltaSeconds);
}

void D3D11Drawer::createTexture(DrawerTexture& outTexture)
{
	outTexture.setImplementation(new D3D11DrawerTexture(outTexture));
}

void D3D11Drawer::refreshTexture(DrawerTexture& texture)
{
	createTexture(texture);
}

void D3D11Drawer::setupRenderWindow(SDL_Window* window)
{
	mInternal.mOutputWindow = window;
	if (nullptr != window)
	{
		mInternal.mResources.ensureWindowResources(window);
	}
}

void D3D11Drawer::performRendering(const DrawCollection& drawCollection)
{
	bool clearedWindowRenderTarget = false;
	for (DrawCommand* drawCommand : drawCollection.getDrawCommands())
	{
		switch (drawCommand->getType())
		{
			case DrawCommand::Type::UNDEFINED:
				RMX_ERROR("Got invalid draw command", );
				break;

			case DrawCommand::Type::SET_WINDOW_RENDER_TARGET:
			{
				SetWindowRenderTargetDrawCommand& dc = drawCommand->as<SetWindowRenderTargetDrawCommand>();
				if (!mInternal.mResources.ensureWindowResources(mInternal.mOutputWindow))
					break;
				mInternal.mScissorStack.clear();
				mInternal.mResources.bindRenderTarget(mInternal.mResources.getWindowRenderTargetView(), nullptr, dc.mViewport);
				if (!clearedWindowRenderTarget)
				{
					mInternal.mResources.clearRenderTarget(mInternal.mResources.getWindowRenderTargetView(), Color::BLACK);
					clearedWindowRenderTarget = true;
				}
				mInternal.setupViewport(dc.mViewport);
				mInternal.applyCurrentScissor();
				break;
			}

			case DrawCommand::Type::SET_RENDER_TARGET:
			{
				SetRenderTargetDrawCommand& dc = drawCommand->as<SetRenderTargetDrawCommand>();
				D3D11DrawerTexture* drawerTexture = dc.mTexture->getImplementation<D3D11DrawerTexture>();
				if (nullptr == drawerTexture || drawerTexture->mTextureResource.mRenderTargetView == nullptr)
					break;
				mInternal.mScissorStack.clear();
				mInternal.mResources.bindRenderTarget(drawerTexture->mTextureResource.mRenderTargetView.Get(), nullptr, dc.mViewport);
				mInternal.setupViewport(dc.mViewport);
				mInternal.applyCurrentScissor();
				break;
			}

			case DrawCommand::Type::RECT:
			{
				if (!mInternal.mayRenderAnything())
					break;

				RectDrawCommand& dc = drawCommand->as<RectDrawCommand>();
				if (nullptr == dc.mTexture)
				{
					mInternal.mResources.drawSimpleRectColored(dc.mRect, mInternal.mCurrentViewport.getSize(), dc.mColor);
					break;
				}

				D3D11DrawerTexture* texture = dc.mTexture->getImplementation<D3D11DrawerTexture>();
				if (nullptr == texture || !texture->mTextureResource.isValid())
					break;

				if (dc.mUV0 == Vec2f(0.0f, 0.0f) && dc.mUV1 == Vec2f(1.0f, 1.0f))
				{
					mInternal.mResources.drawSimpleRectTextured(dc.mRect, mInternal.mCurrentViewport.getSize(), texture->mTextureResource.mShaderResourceView.Get(), dc.mColor, Color::TRANSPARENT, d3d11drawer::useAlphaTest(mInternal.mResources));
				}
				else
				{
					const float vertexData[] =
					{
						0.0f, 0.0f, dc.mUV0.x, dc.mUV0.y,
						0.0f, 1.0f, dc.mUV0.x, dc.mUV1.y,
						1.0f, 1.0f, dc.mUV1.x, dc.mUV1.y,
						1.0f, 1.0f, dc.mUV1.x, dc.mUV1.y,
						1.0f, 0.0f, dc.mUV1.x, dc.mUV0.y,
						0.0f, 0.0f, dc.mUV0.x, dc.mUV0.y
					};
					mInternal.mResources.drawSimpleRectTexturedUV(texture->mTextureResource.mShaderResourceView.Get(), mInternal.getTransformOfRectInViewport(dc.mRect), dc.mColor, d3d11drawer::useAlphaTest(mInternal.mResources), vertexData, 6);
				}
				break;
			}

			case DrawCommand::Type::UPSCALED_RECT:
			{
				if (!mInternal.mayRenderAnything())
					break;

				UpscaledRectDrawCommand& dc = drawCommand->as<UpscaledRectDrawCommand>();
				if (nullptr == dc.mTexture)
					break;
				D3D11DrawerTexture* texture = dc.mTexture->getImplementation<D3D11DrawerTexture>();
				if (nullptr == texture || !texture->mTextureResource.isValid())
					break;
				mInternal.mResources.drawUpscaledRect(dc.mRect, mInternal.mCurrentViewport, texture->mTextureResource);
				break;
			}

			case DrawCommand::Type::SPRITE:
			{
				SpriteDrawCommand& sc = drawCommand->as<SpriteDrawCommand>();
				const SpriteCollection::Item* item = SpriteCollection::instance().getSprite(sc.mSpriteKey);
				if (nullptr == item)
					break;

				SpriteBase& sprite = *item->mSprite;
				Vec2i offset = sprite.mOffset;
				Vec2i size = sprite.getSize();
				if (sc.mScale.x != 1.0f || sc.mScale.y != 1.0f)
				{
					offset.x = roundToInt((float)offset.x * sc.mScale.x);
					offset.y = roundToInt((float)offset.y * sc.mScale.y);
					size.x = roundToInt((float)size.x * sc.mScale.x);
					size.y = roundToInt((float)size.y * sc.mScale.y);
				}
				const Recti targetRect(sc.mPosition + offset, size);

				if (item->mUsesComponentSprite)
				{
					d3d11::TextureResource* texture = mInternal.mResources.getSpriteTextureManager().getComponentSpriteTexture(*item, mInternal.mResources);
					if (nullptr != texture)
					{
						mInternal.mResources.drawSimpleRectTextured(targetRect, mInternal.mCurrentViewport.getSize(), texture->mShaderResourceView.Get(), sc.mTintColor, Color::TRANSPARENT, d3d11drawer::useAlphaTest(mInternal.mResources));
					}
				}
				else
				{
					d3d11::TextureResource* texture = mInternal.mResources.getSpriteTextureManager().getPaletteSpriteTexture(*item, false, mInternal.mResources);
					const PaletteBase* palette = PaletteCollection::instance().getPalette(sc.mPaletteKey, 0);
					if (nullptr != texture && nullptr != palette)
					{
						const d3d11::TextureResource& paletteTexture = mInternal.mResources.getCustomPaletteTexture(*palette, *palette);
						mInternal.mResources.drawSimpleRectIndexed(targetRect, mInternal.mCurrentViewport.getSize(), texture->mShaderResourceView.Get(), texture->mSize, paletteTexture.mShaderResourceView.Get(), sc.mTintColor, Color::TRANSPARENT, d3d11drawer::useAlphaTest(mInternal.mResources));
					}
				}
				break;
			}

			case DrawCommand::Type::SPRITE_RECT:
			{
				SpriteRectDrawCommand& sc = drawCommand->as<SpriteRectDrawCommand>();
				const SpriteCollection::Item* item = SpriteCollection::instance().getSprite(sc.mSpriteKey);
				if (nullptr == item || !item->mUsesComponentSprite)
					break;

				d3d11::TextureResource* texture = mInternal.mResources.getSpriteTextureManager().getComponentSpriteTexture(*item, mInternal.mResources);
				if (nullptr != texture)
				{
					mInternal.mResources.drawSimpleRectTextured(sc.mRect, mInternal.mCurrentViewport.getSize(), texture->mShaderResourceView.Get(), sc.mTintColor, Color::TRANSPARENT, d3d11drawer::useAlphaTest(mInternal.mResources));
				}
				break;
			}

			case DrawCommand::Type::MESH:
			{
				if (!mInternal.mayRenderAnything())
					break;

				MeshDrawCommand& dc = drawCommand->as<MeshDrawCommand>();
				if (dc.mTriangles.empty() || nullptr == dc.mTexture)
					break;

				D3D11DrawerTexture* texture = dc.mTexture->getImplementation<D3D11DrawerTexture>();
				if (nullptr == texture || !texture->mTextureResource.isValid())
					break;

				std::vector<float> vertexData;
				vertexData.resize(dc.mTriangles.size() * 4);
				for (size_t i = 0; i < dc.mTriangles.size(); ++i)
				{
					const DrawerMeshVertex& src = dc.mTriangles[i];
					float* dst = &vertexData[i * 4];
					dst[0] = src.mPosition.x;
					dst[1] = src.mPosition.y;
					dst[2] = src.mTexcoords.x;
					dst[3] = src.mTexcoords.y;
				}
				mInternal.mResources.drawSimpleRectTexturedUV(texture->mTextureResource.mShaderResourceView.Get(), mInternal.mPixelToViewSpaceTransform, Color::WHITE, true, vertexData.data(), dc.mTriangles.size());
				break;
			}

			case DrawCommand::Type::MESH_VERTEX_COLOR:
			{
				if (!mInternal.mayRenderAnything())
					break;

				MeshVertexColorDrawCommand& dc = drawCommand->as<MeshVertexColorDrawCommand>();
				if (dc.mTriangles.empty())
					break;

				std::vector<float> vertexData;
				vertexData.resize(dc.mTriangles.size() * 6);
				for (size_t i = 0; i < dc.mTriangles.size(); ++i)
				{
					const DrawerMeshVertex_P2_C4& src = dc.mTriangles[i];
					float* dst = &vertexData[i * 6];
					dst[0] = src.mPosition.x;
					dst[1] = src.mPosition.y;
					dst[2] = src.mColor.r;
					dst[3] = src.mColor.g;
					dst[4] = src.mColor.b;
					dst[5] = src.mColor.a;
				}
				mInternal.mResources.drawMeshVertexColor(mInternal.mPixelToViewSpaceTransform, vertexData.data(), dc.mTriangles.size());
				break;
			}

			case DrawCommand::Type::SET_BLEND_MODE:
				mInternal.mResources.setBlendMode(drawCommand->as<SetBlendModeDrawCommand>().mBlendMode);
				break;

			case DrawCommand::Type::SET_SAMPLING_MODE:
				mInternal.mResources.setSamplingMode(drawCommand->as<SetSamplingModeDrawCommand>().mSamplingMode);
				break;

			case DrawCommand::Type::SET_WRAP_MODE:
				mInternal.mResources.setWrapMode(drawCommand->as<SetWrapModeDrawCommand>().mWrapMode);
				break;

			case DrawCommand::Type::PRINT_TEXT:
			{
				if (!mInternal.mayRenderAnything())
					break;
				PrintTextDrawCommand& dc = drawCommand->as<PrintTextDrawCommand>();
				mInternal.printText(*dc.mFont, dc.mText, dc.mRect, dc.mPrintOptions);
				break;
			}

			case DrawCommand::Type::PRINT_TEXT_W:
			{
				if (!mInternal.mayRenderAnything())
					break;
				PrintTextWDrawCommand& dc = drawCommand->as<PrintTextWDrawCommand>();
				mInternal.printText(*dc.mFont, dc.mText, dc.mRect, dc.mPrintOptions);
				break;
			}

			case DrawCommand::Type::PUSH_SCISSOR:
			{
				Recti scissorRect = drawCommand->as<PushScissorDrawCommand>().mRect;
				if (!mInternal.mScissorStack.empty())
					scissorRect.intersect(mInternal.mScissorStack.back());
				mInternal.mScissorStack.emplace_back(scissorRect);
				mInternal.applyCurrentScissor();
				break;
			}

			case DrawCommand::Type::POP_SCISSOR:
			{
				if (!mInternal.mScissorStack.empty())
					mInternal.mScissorStack.pop_back();
				mInternal.applyCurrentScissor();
				break;
			}
		}
	}
}

void D3D11Drawer::presentScreen()
{
	mInternal.mResources.present(Configuration::useVSync(Configuration::instance().mFrameSync));
}

D3D11DrawerResources& D3D11Drawer::getResources()
{
	return mInternal.mResources;
}

#endif
