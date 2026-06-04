/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include <rmxbase.h>
#include "oxygen/rendering/RenderingDefinitions.h"

class DrawerTexture;
class DrawCommandFactory;
class GX2RenderResources;


struct DrawerMeshVertex		// TODO: Rename to "DrawerMeshVertex_P2_T2" to be more specific here
{
	Vec2f mPosition;
	Vec2f mTexcoords;
};

struct DrawerMeshVertex_P2_C4
{
	Vec2f mPosition;
	Color mColor;
};

struct DrawerPrintOptions
{
	int   mAlignment = 1;
	int   mSpacing = 0;
	Color mTintColor = Color::WHITE;
};


class DrawCommand
{
public:
	enum class Type
	{
		UNDEFINED = 0,
		SET_WINDOW_RENDER_TARGET,
		SET_RENDER_TARGET,
		RECT,
		UPSCALED_RECT,
		SPRITE,
		SPRITE_RECT,
		MESH,
		MESH_VERTEX_COLOR,
		SET_BLEND_MODE,
		SET_SAMPLING_MODE,
		SET_WRAP_MODE,
		PRINT_TEXT,
		PRINT_TEXT_W,
		PUSH_SCISSOR,
		POP_SCISSOR,
#if defined(PLATFORM_WIIU)
		GX2_PLANE,
		GX2_VDP_SPRITE,
		GX2_PALETTE_SPRITE,
		GX2_TEXTURE_SPRITE,
		GX2_BLUR,
		GX2_CLEAR_DEPTH
#endif
	};

public:
	static DrawCommandFactory mFactory;

public:
	inline Type getType() const  { return mType; }

	template<typename T> T& as()			  { return static_cast<T&>(*this); }
	template<typename T> const T& as() const  { return static_cast<const T&>(*this); }

protected:
	inline virtual ~DrawCommand() {}
	inline DrawCommand(Type type) : mType(type) {}

private:
	Type mType;
};



class SetWindowRenderTargetDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<SetWindowRenderTargetDrawCommand>;

protected:
	SetWindowRenderTargetDrawCommand(const Recti& viewport) : DrawCommand(Type::SET_WINDOW_RENDER_TARGET), mViewport(viewport) {}

public:
	Recti mViewport;
};


class SetRenderTargetDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<SetRenderTargetDrawCommand>;

protected:
	SetRenderTargetDrawCommand(DrawerTexture& texture, const Recti& viewport) : DrawCommand(Type::SET_RENDER_TARGET), mTexture(&texture), mViewport(viewport) {}

public:
	DrawerTexture* mTexture = nullptr;
	Recti mViewport;
};


class RectDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<RectDrawCommand>;

protected:
	RectDrawCommand(const Recti& rect, const Color& color) : DrawCommand(Type::RECT), mRect(rect), mColor(color) {}
	RectDrawCommand(const Recti& rect, DrawerTexture& texture) : DrawCommand(Type::RECT), mRect(rect), mTexture(&texture) {}
	RectDrawCommand(const Recti& rect, DrawerTexture& texture, const Color& tintColor) : DrawCommand(Type::RECT), mRect(rect), mTexture(&texture), mColor(tintColor) {}
	RectDrawCommand(const Recti& rect, DrawerTexture& texture, const Color& tintColor, const Color& addedColor) : DrawCommand(Type::RECT), mRect(rect), mTexture(&texture), mColor(tintColor), mAddedColor(addedColor) {}
	RectDrawCommand(const Recti& rect, DrawerTexture& texture, const Vec2f& uv0, const Vec2f& uv1, const Color& tintColor) : DrawCommand(Type::RECT), mRect(rect), mTexture(&texture), mColor(tintColor), mUV0(uv0), mUV1(uv1) {}
	RectDrawCommand(const Recti& rect, DrawerTexture& texture, const Vec2f& uv0, const Vec2f& uv1, const Color& tintColor, const Color& addedColor) : DrawCommand(Type::RECT), mRect(rect), mTexture(&texture), mColor(tintColor), mAddedColor(addedColor), mUV0(uv0), mUV1(uv1) {}

public:
	Recti mRect;
	DrawerTexture* mTexture = nullptr;
	Color mColor = Color::WHITE;
	Color mAddedColor = Color::TRANSPARENT;
	Vec2f mUV0 = Vec2f(0.0f, 0.0f);
	Vec2f mUV1 = Vec2f(1.0f, 1.0f);
};


class UpscaledRectDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<UpscaledRectDrawCommand>;

protected:
	UpscaledRectDrawCommand(const Recti& rect, DrawerTexture& texture) : DrawCommand(Type::UPSCALED_RECT), mRect(rect), mTexture(&texture) {}

public:
	Recti mRect;
	DrawerTexture* mTexture = nullptr;
};


class SpriteDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<SpriteDrawCommand>;

protected:
	SpriteDrawCommand(Vec2i position, uint64 spriteKey, uint64 paletteKey, const Color& tintColor, Vec2f scale) : DrawCommand(Type::SPRITE), mPosition(position), mSpriteKey(spriteKey), mPaletteKey(paletteKey), mTintColor(tintColor), mScale(scale) {}

public:
	Vec2i mPosition;
	uint64 mSpriteKey;
	uint64 mPaletteKey;
	Color mTintColor;
	Vec2f mScale;
};


class SpriteRectDrawCommand final : public DrawCommand
{
	friend class ObjectPoolBase<SpriteRectDrawCommand>;

protected:
	SpriteRectDrawCommand(const Recti& rect, uint64 spriteKey, const Color& tintColor) : DrawCommand(Type::SPRITE_RECT), mRect(rect), mSpriteKey(spriteKey), mTintColor(tintColor) {}

public:
	Recti mRect;
	uint64 mSpriteKey;
	Color mTintColor;
};


class MeshDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<MeshDrawCommand>;

protected:
	MeshDrawCommand(const std::vector<DrawerMeshVertex>& triangles, DrawerTexture& texture, const Color& tintColor = Color::WHITE, const Color& addedColor = Color::TRANSPARENT) :
		DrawCommand(Type::MESH), mTriangles(triangles), mTexture(&texture), mTintColor(tintColor), mAddedColor(addedColor) {}
	MeshDrawCommand(std::vector<DrawerMeshVertex>&& triangles, DrawerTexture& texture, const Color& tintColor = Color::WHITE, const Color& addedColor = Color::TRANSPARENT) :
		DrawCommand(Type::MESH), mTriangles(triangles), mTexture(&texture), mTintColor(tintColor), mAddedColor(addedColor) {}

public:
	std::vector<DrawerMeshVertex> mTriangles;
	DrawerTexture* mTexture = nullptr;
	Color mTintColor = Color::WHITE;
	Color mAddedColor = Color::TRANSPARENT;
};


class MeshVertexColorDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<MeshVertexColorDrawCommand>;

protected:
	MeshVertexColorDrawCommand(const std::vector<DrawerMeshVertex_P2_C4>& triangles) : DrawCommand(Type::MESH_VERTEX_COLOR), mTriangles(triangles) {}
	MeshVertexColorDrawCommand(std::vector<DrawerMeshVertex_P2_C4>&& triangles) : DrawCommand(Type::MESH_VERTEX_COLOR), mTriangles(triangles) {}

public:
	std::vector<DrawerMeshVertex_P2_C4> mTriangles;
};


class SetBlendModeDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<SetBlendModeDrawCommand>;

protected:
	SetBlendModeDrawCommand(BlendMode blendMode) : DrawCommand(Type::SET_BLEND_MODE), mBlendMode(blendMode) {}

public:
	BlendMode mBlendMode;
};


class SetSamplingModeDrawCommand final : public DrawCommand
{
	friend class ObjectPoolBase<SetSamplingModeDrawCommand>;

protected:
	SetSamplingModeDrawCommand(SamplingMode samplingMode) : DrawCommand(Type::SET_SAMPLING_MODE), mSamplingMode(samplingMode) {}

public:
	SamplingMode mSamplingMode;
};


class SetWrapModeDrawCommand final : public DrawCommand
{
	friend class ObjectPoolBase<SetWrapModeDrawCommand>;

protected:
	SetWrapModeDrawCommand(TextureWrapMode wrapMode) : DrawCommand(Type::SET_WRAP_MODE), mWrapMode(wrapMode) {}

public:
	TextureWrapMode mWrapMode;
};


class PrintTextDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<PrintTextDrawCommand>;

protected:
	PrintTextDrawCommand(Font& font, const Recti& rect, const String& text, int alignment = 1, Color color = Color::WHITE) :
		DrawCommand(Type::PRINT_TEXT), mFont(&font), mRect(rect), mText(text)
	{
		mPrintOptions.mAlignment = alignment;
		mPrintOptions.mTintColor = color;
	}

	PrintTextDrawCommand(Font& font, const Recti& rect, const String& text, const DrawerPrintOptions& printOptions) :
		DrawCommand(Type::PRINT_TEXT), mFont(&font), mRect(rect), mText(text), mPrintOptions(printOptions)
	{}

public:
	Font* mFont = nullptr;
	Recti mRect;
	String mText;
	DrawerPrintOptions mPrintOptions;
};


class PrintTextWDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<PrintTextWDrawCommand>;

protected:
	PrintTextWDrawCommand(Font& font, const Recti& rect, const WString& text, int alignment = 1, Color color = Color::WHITE) :
		DrawCommand(Type::PRINT_TEXT_W), mFont(&font), mRect(rect), mText(text)
	{
		mPrintOptions.mAlignment = alignment;
		mPrintOptions.mTintColor = color;
	}

	PrintTextWDrawCommand(Font& font, const Recti& rect, const WString& text, const DrawerPrintOptions& printOptions) :
		DrawCommand(Type::PRINT_TEXT_W), mFont(&font), mRect(rect), mText(text), mPrintOptions(printOptions)
	{}

public:
	Font* mFont = nullptr;
	Recti mRect;
	WString mText;
	DrawerPrintOptions mPrintOptions;
};


class PushScissorDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<PushScissorDrawCommand>;

protected:
	PushScissorDrawCommand(const Recti& rect) : DrawCommand(Type::PUSH_SCISSOR), mRect(rect) {}

public:
	Recti mRect;
};


class PopScissorDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<PopScissorDrawCommand>;

protected:
	PopScissorDrawCommand() : DrawCommand(Type::POP_SCISSOR) {}
};

#if defined(PLATFORM_WIIU)
class GX2PlaneDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<GX2PlaneDrawCommand>;

protected:
	GX2PlaneDrawCommand(GX2RenderResources& resources, const Recti& activeRect, int planeIndex, bool priorityFlag, uint8 scrollOffsets, const Vec2i& gameResolution) :
		DrawCommand(Type::GX2_PLANE),
		mResources(&resources),
		mActiveRect(activeRect),
		mPlaneIndex(planeIndex),
		mPriorityFlag(priorityFlag),
		mScrollOffsets(scrollOffsets),
		mGameResolution(gameResolution)
	{}

public:
	GX2RenderResources* mResources = nullptr;
	Recti mActiveRect;
	int mPlaneIndex = 0;
	bool mPriorityFlag = false;
	uint8 mScrollOffsets = 0;
	Vec2i mGameResolution;
};

class GX2VdpSpriteDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<GX2VdpSpriteDrawCommand>;

protected:
	GX2VdpSpriteDrawCommand(GX2RenderResources& resources, const Recti& rect, const Vec2i& sizeInPatterns, const Vec4i& patternSplitPriorityShadow, const Color& tintColor, const Color& addedColor) :
		DrawCommand(Type::GX2_VDP_SPRITE),
		mResources(&resources),
		mRect(rect),
		mSizeInPatterns(sizeInPatterns),
		mFirstPattern((uint16)patternSplitPriorityShadow.x),
		mSplitY(patternSplitPriorityShadow.y),
		mTintColor(tintColor),
		mAddedColor(addedColor),
		mPriorityFlag(patternSplitPriorityShadow.z != 0),
		mShadowHighlightMode(patternSplitPriorityShadow.w != 0)
	{}

public:
	GX2RenderResources* mResources = nullptr;
	Recti mRect;
	Vec2i mSizeInPatterns;
	uint16 mFirstPattern = 0;
	int mSplitY = 0;
	Color mTintColor = Color::WHITE;
	Color mAddedColor = Color::TRANSPARENT;
	bool mPriorityFlag = false;
	bool mShadowHighlightMode = false;
};

class GX2PaletteSpriteDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<GX2PaletteSpriteDrawCommand>;

protected:
	GX2PaletteSpriteDrawCommand(const Recti& rect, DrawerTexture& dataTexture, const Vec4i& splitAtexPriorityShadow, const Color& tintColor, const Color& addedColor) :
		DrawCommand(Type::GX2_PALETTE_SPRITE),
		mRect(rect),
		mSourceSize(rect.getSize()),
		mDataTexture(&dataTexture),
		mSplitY(splitAtexPriorityShadow.x),
		mAtex((uint16)splitAtexPriorityShadow.y),
		mTintColor(tintColor),
		mAddedColor(addedColor),
		mPriorityFlag(splitAtexPriorityShadow.z != 0),
		mShadowHighlightMode(splitAtexPriorityShadow.w != 0)
	{}

	GX2PaletteSpriteDrawCommand(const std::vector<DrawerMeshVertex>& triangles, const Vec2i& sourceSize, DrawerTexture& dataTexture, const Vec4i& splitAtexPriorityShadow, const Color& tintColor, const Color& addedColor) :
		DrawCommand(Type::GX2_PALETTE_SPRITE),
		mTriangles(triangles),
		mSourceSize(sourceSize),
		mDataTexture(&dataTexture),
		mSplitY(splitAtexPriorityShadow.x),
		mAtex((uint16)splitAtexPriorityShadow.y),
		mTintColor(tintColor),
		mAddedColor(addedColor),
		mPriorityFlag(splitAtexPriorityShadow.z != 0),
		mShadowHighlightMode(splitAtexPriorityShadow.w != 0),
		mUseMesh(true)
	{}

public:
	Recti mRect;
	std::vector<DrawerMeshVertex> mTriangles;
	Vec2i mSourceSize;
	DrawerTexture* mDataTexture = nullptr;
	int mSplitY = 0;
	uint16 mAtex = 0;
	Color mTintColor = Color::WHITE;
	Color mAddedColor = Color::TRANSPARENT;
	bool mPriorityFlag = false;
	bool mShadowHighlightMode = false;
	bool mUseMesh = false;
};

class GX2TextureSpriteDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<GX2TextureSpriteDrawCommand>;

protected:
	GX2TextureSpriteDrawCommand(const Recti& rect, DrawerTexture& texture, const Color& tintColor, const Color& addedColor, bool priorityFlag) :
		DrawCommand(Type::GX2_TEXTURE_SPRITE),
		mRect(rect),
		mTexture(&texture),
		mTintColor(tintColor),
		mAddedColor(addedColor),
		mPriorityFlag(priorityFlag)
	{}

	GX2TextureSpriteDrawCommand(const std::vector<DrawerMeshVertex>& triangles, DrawerTexture& texture, const Color& tintColor, const Color& addedColor, bool priorityFlag) :
		DrawCommand(Type::GX2_TEXTURE_SPRITE),
		mTriangles(triangles),
		mTexture(&texture),
		mTintColor(tintColor),
		mAddedColor(addedColor),
		mPriorityFlag(priorityFlag),
		mUseMesh(true)
	{}

public:
	Recti mRect;
	std::vector<DrawerMeshVertex> mTriangles;
	DrawerTexture* mTexture = nullptr;
	Color mTintColor = Color::WHITE;
	Color mAddedColor = Color::TRANSPARENT;
	bool mPriorityFlag = false;
	bool mUseMesh = false;
};

class GX2BlurDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<GX2BlurDrawCommand>;

protected:
	GX2BlurDrawCommand(DrawerTexture& processingTexture, const Vec2i& resolution, const Vec4f& kernel) :
		DrawCommand(Type::GX2_BLUR),
		mProcessingTexture(&processingTexture),
		mResolution(resolution),
		mKernel(kernel)
	{}

public:
	DrawerTexture* mProcessingTexture = nullptr;
	Vec2i mResolution;
	Vec4f mKernel;
};

class GX2ClearDepthDrawCommand final : public DrawCommand
{
friend class ObjectPoolBase<GX2ClearDepthDrawCommand>;

protected:
	GX2ClearDepthDrawCommand() : DrawCommand(Type::GX2_CLEAR_DEPTH) {}
};
#endif


class DrawCommandFactory
{
public:
	ObjectPool<SetWindowRenderTargetDrawCommand> mSetWindowRenderTargetDrawCommands;
	ObjectPool<SetRenderTargetDrawCommand>		 mSetRenderTargetDrawCommands;
	ObjectPool<RectDrawCommand>					 mRectDrawCommands;
	ObjectPool<UpscaledRectDrawCommand>			 mUpscaledRectDrawCommands;
	ObjectPool<SpriteDrawCommand>				 mSpriteDrawCommands;
	ObjectPool<SpriteRectDrawCommand>			 mSpriteRectDrawCommands;
	ObjectPool<MeshDrawCommand>					 mMeshDrawCommands;
	ObjectPool<MeshVertexColorDrawCommand>		 mMeshVertexColorDrawCommands;
	ObjectPool<SetBlendModeDrawCommand>			 mSetBlendModeDrawCommands;
	ObjectPool<SetSamplingModeDrawCommand>		 mSetSamplingModeDrawCommands;
	ObjectPool<SetWrapModeDrawCommand>			 mSetWrapModeDrawCommands;
	ObjectPool<PrintTextDrawCommand>			 mPrintTextDrawCommands;
	ObjectPool<PrintTextWDrawCommand>			 mPrintTextWDrawCommands;
	ObjectPool<PushScissorDrawCommand>			 mPushScissorDrawCommands;
	ObjectPool<PopScissorDrawCommand>			 mPopScissorDrawCommands;
#if defined(PLATFORM_WIIU)
	ObjectPool<GX2PlaneDrawCommand>				 mGX2PlaneDrawCommands;
	ObjectPool<GX2VdpSpriteDrawCommand>			 mGX2VdpSpriteDrawCommands;
	ObjectPool<GX2PaletteSpriteDrawCommand>		 mGX2PaletteSpriteDrawCommands;
	ObjectPool<GX2TextureSpriteDrawCommand>		 mGX2TextureSpriteDrawCommands;
	ObjectPool<GX2BlurDrawCommand>				 mGX2BlurDrawCommands;
	ObjectPool<GX2ClearDepthDrawCommand>		 mGX2ClearDepthDrawCommands;
#endif

public:
	void destroy(DrawCommand& drawCommand)
	{
		switch (drawCommand.getType())
		{
			case DrawCommand::Type::SET_WINDOW_RENDER_TARGET:	mSetWindowRenderTargetDrawCommands.destroyObject(drawCommand.as<SetWindowRenderTargetDrawCommand>());  break;
			case DrawCommand::Type::SET_RENDER_TARGET:			mSetRenderTargetDrawCommands.destroyObject(drawCommand.as<SetRenderTargetDrawCommand>());  break;
			case DrawCommand::Type::RECT:						mRectDrawCommands.destroyObject(drawCommand.as<RectDrawCommand>());  break;
			case DrawCommand::Type::UPSCALED_RECT:				mUpscaledRectDrawCommands.destroyObject(drawCommand.as<UpscaledRectDrawCommand>());  break;
			case DrawCommand::Type::SPRITE:						mSpriteDrawCommands.destroyObject(drawCommand.as<SpriteDrawCommand>());  break;
			case DrawCommand::Type::SPRITE_RECT:				mSpriteRectDrawCommands.destroyObject(drawCommand.as<SpriteRectDrawCommand>());  break;
			case DrawCommand::Type::MESH:						mMeshDrawCommands.destroyObject(drawCommand.as<MeshDrawCommand>());  break;
			case DrawCommand::Type::MESH_VERTEX_COLOR:			mMeshVertexColorDrawCommands.destroyObject(drawCommand.as<MeshVertexColorDrawCommand>());  break;
			case DrawCommand::Type::SET_BLEND_MODE:				mSetBlendModeDrawCommands.destroyObject(drawCommand.as<SetBlendModeDrawCommand>());  break;
			case DrawCommand::Type::SET_SAMPLING_MODE:			mSetSamplingModeDrawCommands.destroyObject(drawCommand.as<SetSamplingModeDrawCommand>());  break;
			case DrawCommand::Type::SET_WRAP_MODE:				mSetWrapModeDrawCommands.destroyObject(drawCommand.as<SetWrapModeDrawCommand>());  break;
			case DrawCommand::Type::PRINT_TEXT:					mPrintTextDrawCommands.destroyObject(drawCommand.as<PrintTextDrawCommand>());  break;
			case DrawCommand::Type::PRINT_TEXT_W:				mPrintTextWDrawCommands.destroyObject(drawCommand.as<PrintTextWDrawCommand>());  break;
			case DrawCommand::Type::PUSH_SCISSOR:				mPushScissorDrawCommands.destroyObject(drawCommand.as<PushScissorDrawCommand>());  break;
			case DrawCommand::Type::POP_SCISSOR:				mPopScissorDrawCommands.destroyObject(drawCommand.as<PopScissorDrawCommand>());  break;
#if defined(PLATFORM_WIIU)
			case DrawCommand::Type::GX2_PLANE:					mGX2PlaneDrawCommands.destroyObject(drawCommand.as<GX2PlaneDrawCommand>());  break;
			case DrawCommand::Type::GX2_VDP_SPRITE:				mGX2VdpSpriteDrawCommands.destroyObject(drawCommand.as<GX2VdpSpriteDrawCommand>());  break;
			case DrawCommand::Type::GX2_PALETTE_SPRITE:			mGX2PaletteSpriteDrawCommands.destroyObject(drawCommand.as<GX2PaletteSpriteDrawCommand>());  break;
			case DrawCommand::Type::GX2_TEXTURE_SPRITE:			mGX2TextureSpriteDrawCommands.destroyObject(drawCommand.as<GX2TextureSpriteDrawCommand>());  break;
			case DrawCommand::Type::GX2_BLUR:					mGX2BlurDrawCommands.destroyObject(drawCommand.as<GX2BlurDrawCommand>());  break;
			case DrawCommand::Type::GX2_CLEAR_DEPTH:			mGX2ClearDepthDrawCommands.destroyObject(drawCommand.as<GX2ClearDepthDrawCommand>());  break;
#endif
			default:
				break;	// This should never happen anyways
		}
	}
};
