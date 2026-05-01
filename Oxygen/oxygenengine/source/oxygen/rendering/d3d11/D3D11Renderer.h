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

#include "oxygen/rendering/Renderer.h"
#include "oxygen/rendering/d3d11/D3D11RenderResources.h"

class D3D11DrawerResources;
class RenderParts;
namespace d3d11renderer
{
	struct Internal;
}


class D3D11Renderer : public Renderer
{
public:
	static constexpr int8 RENDERER_TYPE_ID = 0x30;

public:
	D3D11Renderer(RenderParts& renderParts, DrawerTexture& outputTexture);
	~D3D11Renderer();

	void initialize() override;
	void reset() override;
	void setGameResolution(const Vec2i& gameResolution) override;
	void clearGameScreen() override;
	void renderGameScreen(const std::vector<Geometry*>& geometries) override;
	void renderDebugDraw(int debugDrawMode, const Recti& rect) override;

private:
	D3D11DrawerResources& mDrawerResources;
	D3D11RenderResources mRenderResources;
	Vec2i mGameResolution;
	d3d11renderer::Internal& mInternal;
};

#endif
