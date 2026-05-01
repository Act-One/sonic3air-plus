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

#include <array>
#include <memory>

#include "oxygen/drawing/vulkan/VulkanUpscaler.h"
#include "oxygen/rendering/vulkan/Renderer.h"
#include "oxygen/rendering/vulkan/Texture.h"

struct SDL_Window;

class VulkanDrawerResources final
{
public:
	~VulkanDrawerResources();

	bool startup();
	void shutdown();
	void clearAllCaches();
	void refresh(float deltaSeconds);
	bool ensureWindowResources(SDL_Window* window);
	bool presentBitmap(const Bitmap& bitmap, bool useVSync);
	bool presentTexture(vulkan::Texture& texture, bool useVSync);
	bool presentRawTexture(vulkan::Texture& texture, bool useVSync);

	inline bool isSetupSuccessful() const  { return mSetupSuccessful; }
	inline vulkan::RendererBackend* getBackend() const  { return mBackend.get(); }
	VulkanUpscaler& getUpscaler();

private:
	bool mSetupSuccessful = false;
	SDL_Window* mOutputWindow = nullptr;
	std::unique_ptr<vulkan::RendererBackend> mBackend;
	vulkan::Texture mPresentedBitmapTexture;
	std::array<std::unique_ptr<VulkanUpscaler>, 4> mUpscalers;
};

#endif
