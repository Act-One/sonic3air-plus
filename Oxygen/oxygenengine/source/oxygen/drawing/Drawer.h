/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include <rmxmedia.h>
#include "oxygen/drawing/DrawCollection.h"
#include "oxygen/drawing/DrawCommand.h"


class DrawerInterface;
class DrawCollection;
class DrawerTexture;
class GX2RenderResources;
class PlaneGeometry;
struct SDL_Window;

class Drawer
{
friend class DrawerTexture;

public:
	enum class Type
	{
		SOFTWARE,
		OPENGL,
		DIRECT3D11,
		VULKAN,
		GX,
		GX2
	};

public:
	Drawer();
	~Drawer();

	Type getType() const;

	template<typename T>
	bool createDrawer()
	{
		destroyDrawer();
		mActiveDrawer = new T();
		return onDrawerCreated();
	}

	void destroyDrawer();

	void shutdown();
	void updateDrawer(float deltaSeconds);

	inline DrawerInterface* getActiveDrawer() const  { return mActiveDrawer; }

	void createTexture(DrawerTexture& outTexture);
	const DrawerTexture* getTextureByID(uint32 uniqueID) const;

	Recti getSpriteRect(uint64 spriteKey) const;	// Return sprite size and pivot offset (usually negative)

	void setWindowRenderTarget(const Recti& rect);
	void setRenderTarget(DrawerTexture& texture, const Recti& rect);

	void setBlendMode(BlendMode blendMode);
	void setSamplingMode(SamplingMode samplingMode);
	void setWrapMode(TextureWrapMode wrapMode);

	void drawRect(const Rectf& rect, const Color& color);
	void drawRect(const Rectf& rect, DrawerTexture& texture);
	void drawRect(const Rectf& rect, DrawerTexture& texture, const Color& tintColor);
	void drawRect(const Rectf& rect, DrawerTexture& texture, const Color& tintColor, const Color& addedColor);
	void drawRect(const Rectf& rect, DrawerTexture& texture, const Vec2f& uv0, const Vec2f& uv1, const Color& tintColor);
	void drawRect(const Rectf& rect, DrawerTexture& texture, const Vec2f& uv0, const Vec2f& uv1, const Color& tintColor, const Color& addedColor);
	void drawRect(const Rectf& rect, DrawerTexture& texture, const Recti& textureInnerRect, const Color& tintColor = Color::WHITE);
	void drawUpscaledRect(const Rectf& rect, DrawerTexture& texture);
	void drawSprite(Vec2i position, uint64 spriteKey, const Color& tintColor = Color::WHITE, Vec2f scale = Vec2f(1.0f, 1.0f));
	void drawSprite(Vec2i position, uint64 spriteKey, uint64 paletteKey, const Color& tintColor = Color::WHITE, Vec2f scale = Vec2f(1.0f, 1.0f));
	void drawSpriteRect(const Recti& rect, uint64 spriteKey, const Color& tintColor = Color::WHITE);
	void drawMesh(const std::vector<DrawerMeshVertex>& triangles, DrawerTexture& texture, const Color& tintColor = Color::WHITE, const Color& addedColor = Color::TRANSPARENT);
	void drawMesh(const std::vector<DrawerMeshVertex_P2_C4>& triangles);
	void drawQuad(const DrawerMeshVertex* quad, DrawerTexture& texture);
#if defined(PLATFORM_WIIU)
	void drawGX2Plane(const PlaneGeometry& geometry, const Vec2i& gameResolution, GX2RenderResources& resources);
	void drawGX2VdpSprite(const Recti& rect, const Vec2i& sizeInPatterns, uint16 firstPattern, int splitY, const Color& tintColor, const Color& addedColor, bool priorityFlag, bool shadowHighlightMode, GX2RenderResources& resources);
	void drawGX2PaletteSprite(const Recti& rect, DrawerTexture& dataTexture, int splitY, uint16 atex, const Color& tintColor, const Color& addedColor, bool priorityFlag, bool shadowHighlightMode);
	void drawGX2PaletteSprite(const std::vector<DrawerMeshVertex>& triangles, const Vec2i& sourceSize, DrawerTexture& dataTexture, int splitY, uint16 atex, const Color& tintColor, const Color& addedColor, bool priorityFlag, bool shadowHighlightMode);
	void drawGX2TextureSprite(const Recti& rect, DrawerTexture& texture, const Color& tintColor, const Color& addedColor, bool priorityFlag);
	void drawGX2TextureSprite(const std::vector<DrawerMeshVertex>& triangles, DrawerTexture& texture, const Color& tintColor, const Color& addedColor, bool priorityFlag);
	void drawGX2Blur(DrawerTexture& processingTexture, const Vec2i& resolution, const Vec4f& kernel);
	void clearGX2Depth();
#endif

	void printText(Font& font, const Recti& rect, const String& text, int alignment = 1, Color color = Color::WHITE);
	void printText(Font& font, const Vec2i& position, const String& text, int alignment = 1, Color color = Color::WHITE);
	void printText(Font& font, const Recti& rect, const String& text, const DrawerPrintOptions& printOptions);
	void printText(Font& font, const Vec2i& position, const String& text, const DrawerPrintOptions& printOptions);
	void printText(Font& font, const Recti& rect, const WString& text, int alignment = 1, Color color = Color::WHITE);
	void printText(Font& font, const Vec2i& position, const WString& text, int alignment = 1, Color color = Color::WHITE);
	void printText(Font& font, const Recti& rect, const WString& text, const DrawerPrintOptions& printOptions);
	void printText(Font& font, const Vec2i& position, const WString& text, const DrawerPrintOptions& printOptions);

	void pushScissor(const Recti& rect);
	void popScissor();

	void setupRenderWindow(SDL_Window* window);
	void performRendering();
	void presentScreen();

private:
	bool onDrawerCreated();
	void unregisterTexture(DrawerTexture& texture);
	void addDrawCommand(DrawCommand& drawCommand);

private:
	DrawerInterface* mActiveDrawer = nullptr;
	DrawCollection mDrawCollection;
	std::vector<DrawerTexture*> mDrawerTextures;
	std::unordered_map<uint32, DrawerTexture*> mTexturesByID;
	uint32 mNextUniqueID = 1;
};
