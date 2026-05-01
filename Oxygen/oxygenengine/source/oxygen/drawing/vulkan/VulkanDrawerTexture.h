/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)

#include "oxygen/drawing/DrawerTexture.h"
#include "oxygen/rendering/vulkan/Texture.h"


class VulkanDrawerTexture final : public DrawerTextureImplementation
{
public:
	inline explicit VulkanDrawerTexture(DrawerTexture& owner) : DrawerTextureImplementation(owner) {}
	~VulkanDrawerTexture() override;

	void updateFromBitmap(const Bitmap& bitmap) override;
	void setupAsRenderTarget(const Vec2i& size) override;
	void writeContentToBitmap(Bitmap& outBitmap) override;
	void refreshImplementation(bool setupRenderTarget, const Vec2i& size) override;

public:
	SamplingMode mSamplingMode = SamplingMode::POINT;
	TextureWrapMode mWrapMode = TextureWrapMode::CLAMP;
	vulkan::Texture mTexture;
};

#endif
