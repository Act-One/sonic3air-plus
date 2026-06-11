/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/drawing/gx/GXDrawer.h"
#include "oxygen/drawing/gx/GXDrawerTexture.h"
#include "oxygen/drawing/DrawCollection.h"
#include "oxygen/drawing/DrawCommand.h"
#include "oxygen/drawing/DrawerTexture.h"
#include "oxygen/helper/Logging.h"
#include "oxygen/rendering/sprite/ComponentSprite.h"
#include "oxygen/rendering/sprite/PaletteSprite.h"
#include "oxygen/resources/PaletteCollection.h"
#include "oxygen/resources/SpriteCollection.h"

#if defined(PLATFORM_WII)
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <malloc.h>
#include <cstring>
#endif


namespace
{
#if defined(PLATFORM_WII)
	static constexpr uint32 FIFO_SIZE = 512 * 1024;

	GXColor toGXColor(Color color)
	{
		color.r = saturate(color.r);
		color.g = saturate(color.g);
		color.b = saturate(color.b);
		color.a = saturate(color.a);
		return GXColor
		{
			(uint8)roundToInt(color.r * 255.0f),
			(uint8)roundToInt(color.g * 255.0f),
			(uint8)roundToInt(color.b * 255.0f),
			(uint8)roundToInt(color.a * 255.0f)
		};
	}

	Color approximateAddedColor(Color tintColor, const Color& addedColor)
	{
		if (addedColor == Color::TRANSPARENT)
			return tintColor;

		tintColor.r = saturate(tintColor.r + addedColor.r);
		tintColor.g = saturate(tintColor.g + addedColor.g);
		tintColor.b = saturate(tintColor.b + addedColor.b);
		return tintColor;
	}
#endif
}

struct GXDrawer::Internal
{
#if defined(PLATFORM_WII)
	Internal()
	{
		initializeVideo();
	}

	~Internal()
	{
		if (nullptr != mFifoBuffer)
		{
			free(mFifoBuffer);
			mFifoBuffer = nullptr;
		}
	}

	void initializeVideo()
	{
		VIDEO_Init();
		WPAD_Init();

		mRenderMode = VIDEO_GetPreferredMode(nullptr);
		RMX_CHECK(nullptr != mRenderMode, "VIDEO_GetPreferredMode failed", mSetupSuccessful = false; return);

		mXfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mRenderMode));
		mXfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mRenderMode));
		RMX_CHECK(nullptr != mXfb[0] && nullptr != mXfb[1], "Failed to allocate Wii framebuffers", mSetupSuccessful = false; return);
		memset(mXfb[0], 0, VIDEO_GetFrameBufferSize(mRenderMode));
		memset(mXfb[1], 0, VIDEO_GetFrameBufferSize(mRenderMode));

		VIDEO_Configure(mRenderMode);
		VIDEO_SetNextFramebuffer(mXfb[mCurrentXfb]);
		VIDEO_SetBlack(false);
		VIDEO_Flush();
		VIDEO_WaitVSync();
		if (mRenderMode->viTVMode & VI_NON_INTERLACE)
			VIDEO_WaitVSync();

		mFifoBuffer = MEM_K0_TO_K1(memalign(32, FIFO_SIZE));
		RMX_CHECK(nullptr != mFifoBuffer, "Failed to allocate GX FIFO", mSetupSuccessful = false; return);
		memset(mFifoBuffer, 0, FIFO_SIZE);

		GX_Init(mFifoBuffer, FIFO_SIZE);
		GXColor clear = { 0, 0, 0, 255 };
		GX_SetCopyClear(clear, GX_MAX_Z24);
		GX_SetViewport(0, 0, mRenderMode->fbWidth, mRenderMode->efbHeight, 0, 1);
		const f32 yScale = GX_GetYScaleFactor(mRenderMode->efbHeight, mRenderMode->xfbHeight);
		GX_SetDispCopyYScale(yScale);
		GX_SetScissor(0, 0, mRenderMode->fbWidth, mRenderMode->efbHeight);
		GX_SetDispCopySrc(0, 0, mRenderMode->fbWidth, mRenderMode->efbHeight);
		GX_SetDispCopyDst(mRenderMode->fbWidth, mRenderMode->xfbHeight);
		GX_SetCopyFilter(mRenderMode->aa, mRenderMode->sample_pattern, GX_TRUE, mRenderMode->vfilter);
		GX_SetFieldMode(mRenderMode->field_rendering, (mRenderMode->viHeight == 2 * mRenderMode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
		GX_SetPixelFmt(mRenderMode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
		GX_SetCullMode(GX_CULL_NONE);
		GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
		GX_SetColorUpdate(GX_TRUE);
		GX_SetAlphaUpdate(GX_TRUE);
		GX_SetDispCopyGamma(GX_GM_1_0);

		GX_ClearVtxDesc();
		GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
		GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
		GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
		GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
		GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
		GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
		GX_SetNumChans(1);
		GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
		useTexturedPipeline();

		Mtx44 projection;
		guOrtho(projection, 0.0f, (f32)mRenderMode->efbHeight, 0.0f, (f32)mRenderMode->fbWidth, 0.0f, 1.0f);
		GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
		Mtx identity;
		guMtxIdentity(identity);
		GX_LoadPosMtxImm(identity, GX_PNMTX0);

		mTargetRect.set(0, 0, 400, 224);
		mScissorRect = mTargetRect;
		mSetupSuccessful = true;
	}

	void useTexturedPipeline()
	{
		if (mUsingTexturedPipeline)
			return;

		GX_SetNumTexGens(1);
		GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
		GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
		GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
		mUsingTexturedPipeline = true;
	}

	void useColorPipeline()
	{
		if (!mUsingTexturedPipeline)
			return;

		GX_SetNumTexGens(0);
		GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
		GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
		mUsingTexturedPipeline = false;
	}

	Recti toPhysicalRect(const Recti& logical) const
	{
		const float sx = (float)mRenderMode->fbWidth / (float)std::max(1, mTargetRect.width);
		const float sy = (float)mRenderMode->efbHeight / (float)std::max(1, mTargetRect.height);
		const int x0 = roundToInt((float)(logical.x - mTargetRect.x) * sx);
		const int y0 = roundToInt((float)(logical.y - mTargetRect.y) * sy);
		const int x1 = roundToInt((float)(logical.x + logical.width - mTargetRect.x) * sx);
		const int y1 = roundToInt((float)(logical.y + logical.height - mTargetRect.y) * sy);
		return Recti(x0, y0, x1 - x0, y1 - y0);
	}

	void applyScissor()
	{
		Recti physical = toPhysicalRect(mScissorRect);
		physical.intersect(Recti(0, 0, mRenderMode->fbWidth, mRenderMode->efbHeight));
		if (physical.empty())
			physical.set(0, 0, 1, 1);
		GX_SetScissor((u32)physical.x, (u32)physical.y, (u32)physical.width, (u32)physical.height);
	}

	void setWindowRenderTarget(const Recti& rect)
	{
		mTargetRect = rect.empty() ? Recti(0, 0, 400, 224) : rect;
		mScissorStack.clear();
		mScissorRect = mTargetRect;
		applyScissor();
	}

	void setBlendMode(BlendMode blendMode)
	{
		mBlendMode = blendMode;
		switch (blendMode)
		{
			case BlendMode::OPAQUE:
				GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
				break;
			case BlendMode::ALPHA:
			case BlendMode::ONE_BIT:
				GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
				break;
			case BlendMode::ADDITIVE:
				GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
				break;
			case BlendMode::SUBTRACTIVE:
				GX_SetBlendMode(GX_BM_SUBTRACT, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
				break;
			case BlendMode::MULTIPLICATIVE:
				GX_SetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_ZERO, GX_LO_CLEAR);
				break;
			default:
				GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
				break;
		}
	}

	void drawQuad(const Recti& rect, DrawerTexture& texture, const Vec2f& uv0, const Vec2f& uv1, Color color, const Color& addedColor = Color::TRANSPARENT)
	{
		if (rect.empty())
			return;

		GXDrawerTexture* impl = texture.getImplementation<GXDrawerTexture>();
		if (nullptr == impl || !impl->load(mSamplingMode, mWrapMode))
			return;

		useTexturedPipeline();
		const Recti p = toPhysicalRect(rect);
		const GXColor gxColor = toGXColor(approximateAddedColor(color, addedColor));
		GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
			GX_Position2f32((f32)p.x, (f32)p.y);
			GX_Color4u8(gxColor.r, gxColor.g, gxColor.b, gxColor.a);
			GX_TexCoord2f32(uv0.x, uv0.y);
			GX_Position2f32((f32)(p.x + p.width), (f32)p.y);
			GX_Color4u8(gxColor.r, gxColor.g, gxColor.b, gxColor.a);
			GX_TexCoord2f32(uv1.x, uv0.y);
			GX_Position2f32((f32)(p.x + p.width), (f32)(p.y + p.height));
			GX_Color4u8(gxColor.r, gxColor.g, gxColor.b, gxColor.a);
			GX_TexCoord2f32(uv1.x, uv1.y);
			GX_Position2f32((f32)p.x, (f32)(p.y + p.height));
			GX_Color4u8(gxColor.r, gxColor.g, gxColor.b, gxColor.a);
			GX_TexCoord2f32(uv0.x, uv1.y);
		GX_End();
	}

	void drawColorRect(const Recti& rect, const Color& color)
	{
		if (rect.empty())
			return;

		useColorPipeline();
		const Recti p = toPhysicalRect(rect);
		const GXColor gxColor = toGXColor(color);
		GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
			GX_Position2f32((f32)p.x, (f32)p.y);
			GX_Color4u8(gxColor.r, gxColor.g, gxColor.b, gxColor.a);
			GX_TexCoord2f32(0.0f, 0.0f);
			GX_Position2f32((f32)(p.x + p.width), (f32)p.y);
			GX_Color4u8(gxColor.r, gxColor.g, gxColor.b, gxColor.a);
			GX_TexCoord2f32(0.0f, 0.0f);
			GX_Position2f32((f32)(p.x + p.width), (f32)(p.y + p.height));
			GX_Color4u8(gxColor.r, gxColor.g, gxColor.b, gxColor.a);
			GX_TexCoord2f32(0.0f, 0.0f);
			GX_Position2f32((f32)p.x, (f32)(p.y + p.height));
			GX_Color4u8(gxColor.r, gxColor.g, gxColor.b, gxColor.a);
			GX_TexCoord2f32(0.0f, 0.0f);
		GX_End();
	}

	DrawerTexture& createFrameTexture(const Bitmap& bitmap)
	{
		std::unique_ptr<DrawerTexture> texture = std::make_unique<DrawerTexture>();
		texture->accessBitmap() = bitmap;
		texture->bitmapUpdated();
		mFrameTextures.emplace_back(std::move(texture));
		return *mFrameTextures.back();
	}

	void printText(Font& font, const StringReader& text, const Recti& rect, const DrawerPrintOptions& printOptions)
	{
		Bitmap bitmap;
		int reservedSize = 0;
		Vec2i drawPosition;
		font.printBitmap(bitmap, drawPosition, rect, text, printOptions.mAlignment, printOptions.mSpacing, &reservedSize);
		if (bitmap.empty())
			return;

		DrawerTexture& temp = createFrameTexture(bitmap);
		drawQuad(Recti(drawPosition, bitmap.getSize()), temp, Vec2f(0.0f, 0.0f), Vec2f(1.0f, 1.0f), printOptions.mTintColor);
	}

	bool mSetupSuccessful = false;
	GXRModeObj* mRenderMode = nullptr;
	void* mXfb[2] = { nullptr, nullptr };
	uint32 mCurrentXfb = 0;
	void* mFifoBuffer = nullptr;
	DrawerTexture mWhiteTexture;
	Recti mTargetRect;
	Recti mScissorRect;
	std::vector<Recti> mScissorStack;
	BlendMode mBlendMode = BlendMode::OPAQUE;
	SamplingMode mSamplingMode = SamplingMode::POINT;
	TextureWrapMode mWrapMode = TextureWrapMode::CLAMP;
	bool mUsingTexturedPipeline = false;
	std::vector<std::unique_ptr<DrawerTexture>> mFrameTextures;
#endif
};

GXDrawer::GXDrawer() :
	mInternal(*new Internal())
{
}

GXDrawer::~GXDrawer()
{
	delete &mInternal;
}

Drawer::Type GXDrawer::getType()
{
	return Drawer::Type::GX;
}

bool GXDrawer::wasSetupSuccessful()
{
#if defined(PLATFORM_WII)
	return mInternal.mSetupSuccessful;
#else
	return false;
#endif
}

void GXDrawer::createTexture(DrawerTexture& outTexture)
{
	outTexture.setImplementation(new GXDrawerTexture(outTexture));
}

void GXDrawer::refreshTexture(DrawerTexture& texture)
{
	createTexture(texture);
}

void GXDrawer::setupRenderWindow(SDL_Window* window)
{
	(void)window;
}

void GXDrawer::performRendering(const DrawCollection& drawCollection)
{
#if defined(PLATFORM_WII)
	if (!mInternal.mSetupSuccessful)
		return;

	for (DrawCommand* drawCommand : drawCollection.getDrawCommands())
	{
		switch (drawCommand->getType())
		{
			case DrawCommand::Type::UNDEFINED:
				continue;

			case DrawCommand::Type::SET_WINDOW_RENDER_TARGET:
				mInternal.setWindowRenderTarget(drawCommand->as<SetWindowRenderTargetDrawCommand>().mViewport);
				break;

			case DrawCommand::Type::SET_RENDER_TARGET:
				RMX_LOG_WARNING("GXDrawer: render-target draw command ignored; Wii GX path renders directly to the EFB");
				break;

			case DrawCommand::Type::RECT:
			{
				RectDrawCommand& dc = drawCommand->as<RectDrawCommand>();
				if (nullptr != dc.mTexture)
				{
					mInternal.drawQuad(dc.mRect, *dc.mTexture, dc.mUV0, dc.mUV1, dc.mColor, dc.mAddedColor);
				}
				else
				{
					mInternal.drawColorRect(dc.mRect, dc.mColor);
				}
				break;
			}

			case DrawCommand::Type::UPSCALED_RECT:
			{
				UpscaledRectDrawCommand& dc = drawCommand->as<UpscaledRectDrawCommand>();
				if (nullptr != dc.mTexture)
					mInternal.drawQuad(dc.mRect, *dc.mTexture, Vec2f(0.0f, 0.0f), Vec2f(1.0f, 1.0f), Color::WHITE);
				break;
			}

			case DrawCommand::Type::SPRITE:
			{
				SpriteDrawCommand& sc = drawCommand->as<SpriteDrawCommand>();
				const SpriteCollection::Item* item = SpriteCollection::instance().getSprite(sc.mSpriteKey);
				if (nullptr == item || nullptr == item->mSprite)
					break;
				if (item->mUsesComponentSprite)
				{
					ComponentSprite& sprite = static_cast<ComponentSprite&>(*item->mSprite);
					Vec2i size = sprite.getSize();
					Vec2i offset = sprite.mOffset;
					if (sc.mScale.x != 1.0f || sc.mScale.y != 1.0f)
					{
						offset.x = roundToInt((float)offset.x * sc.mScale.x);
						offset.y = roundToInt((float)offset.y * sc.mScale.y);
						size.x = roundToInt((float)size.x * sc.mScale.x);
						size.y = roundToInt((float)size.y * sc.mScale.y);
					}
					DrawerTexture& temp = mInternal.createFrameTexture(sprite.accessBitmap());
					mInternal.drawQuad(Recti(sc.mPosition + offset, size), temp, Vec2f(0.0f, 0.0f), Vec2f(1.0f, 1.0f), sc.mTintColor);
				}
				break;
			}

			case DrawCommand::Type::SPRITE_RECT:
			{
				SpriteRectDrawCommand& sc = drawCommand->as<SpriteRectDrawCommand>();
				const SpriteCollection::Item* item = SpriteCollection::instance().getSprite(sc.mSpriteKey);
				if (nullptr == item || nullptr == item->mSprite || !item->mUsesComponentSprite)
					break;
				ComponentSprite& sprite = static_cast<ComponentSprite&>(*item->mSprite);
				DrawerTexture& temp = mInternal.createFrameTexture(sprite.accessBitmap());
				mInternal.drawQuad(sc.mRect, temp, Vec2f(0.0f, 0.0f), Vec2f(1.0f, 1.0f), sc.mTintColor);
				break;
			}

			case DrawCommand::Type::MESH:
			{
				MeshDrawCommand& dc = drawCommand->as<MeshDrawCommand>();
				if (nullptr == dc.mTexture)
					break;
				GXDrawerTexture* impl = dc.mTexture->getImplementation<GXDrawerTexture>();
				if (nullptr == impl || !impl->load(mInternal.mSamplingMode, mInternal.mWrapMode))
					break;
				mInternal.useTexturedPipeline();
				const GXColor color = toGXColor(approximateAddedColor(dc.mTintColor, dc.mAddedColor));
				GX_Begin(GX_TRIANGLES, GX_VTXFMT0, (u16)dc.mTriangles.size());
				for (const DrawerMeshVertex& vertex : dc.mTriangles)
				{
					const Recti p = mInternal.toPhysicalRect(Recti(roundToInt(vertex.mPosition.x), roundToInt(vertex.mPosition.y), 0, 0));
					GX_Position2f32((f32)p.x, (f32)p.y);
					GX_Color4u8(color.r, color.g, color.b, color.a);
					GX_TexCoord2f32(vertex.mTexcoords.x, vertex.mTexcoords.y);
				}
				GX_End();
				break;
			}

			case DrawCommand::Type::MESH_VERTEX_COLOR:
			{
				MeshVertexColorDrawCommand& dc = drawCommand->as<MeshVertexColorDrawCommand>();
				mInternal.useColorPipeline();
				GX_Begin(GX_TRIANGLES, GX_VTXFMT0, (u16)dc.mTriangles.size());
				for (const DrawerMeshVertex_P2_C4& vertex : dc.mTriangles)
				{
					const Recti p = mInternal.toPhysicalRect(Recti(roundToInt(vertex.mPosition.x), roundToInt(vertex.mPosition.y), 0, 0));
					const GXColor color = toGXColor(vertex.mColor);
					GX_Position2f32((f32)p.x, (f32)p.y);
					GX_Color4u8(color.r, color.g, color.b, color.a);
					GX_TexCoord2f32(0.0f, 0.0f);
				}
				GX_End();
				break;
			}

			case DrawCommand::Type::SET_BLEND_MODE:
				mInternal.setBlendMode(drawCommand->as<SetBlendModeDrawCommand>().mBlendMode);
				break;

			case DrawCommand::Type::SET_SAMPLING_MODE:
				mInternal.mSamplingMode = drawCommand->as<SetSamplingModeDrawCommand>().mSamplingMode;
				break;

			case DrawCommand::Type::SET_WRAP_MODE:
				mInternal.mWrapMode = drawCommand->as<SetWrapModeDrawCommand>().mWrapMode;
				break;

			case DrawCommand::Type::PRINT_TEXT:
			{
				PrintTextDrawCommand& dc = drawCommand->as<PrintTextDrawCommand>();
				mInternal.printText(*dc.mFont, dc.mText, dc.mRect, dc.mPrintOptions);
				break;
			}

			case DrawCommand::Type::PRINT_TEXT_W:
			{
				PrintTextWDrawCommand& dc = drawCommand->as<PrintTextWDrawCommand>();
				mInternal.printText(*dc.mFont, dc.mText, dc.mRect, dc.mPrintOptions);
				break;
			}

			case DrawCommand::Type::PUSH_SCISSOR:
				mInternal.mScissorRect.intersect(drawCommand->as<PushScissorDrawCommand>().mRect);
				mInternal.mScissorStack.push_back(mInternal.mScissorRect);
				mInternal.applyScissor();
				break;

			case DrawCommand::Type::POP_SCISSOR:
				if (!mInternal.mScissorStack.empty())
					mInternal.mScissorStack.pop_back();
				mInternal.mScissorRect = mInternal.mScissorStack.empty() ? mInternal.mTargetRect : mInternal.mScissorStack.back();
				mInternal.applyScissor();
				break;

			default:
				break;
		}
	}
#else
	(void)drawCollection;
#endif
}

void GXDrawer::presentScreen()
{
#if defined(PLATFORM_WII)
	if (!mInternal.mSetupSuccessful)
		return;

	GX_DrawDone();
	mInternal.mFrameTextures.clear();
	GX_CopyDisp(mInternal.mXfb[mInternal.mCurrentXfb], GX_TRUE);
	GX_Flush();
	VIDEO_SetNextFramebuffer(mInternal.mXfb[mInternal.mCurrentXfb]);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	mInternal.mCurrentXfb ^= 1;
#endif
}
