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

#include "oxygen/rendering/vulkan/Renderer.h"


class VulkanDrawerResources;


class VulkanUpscaler
{
public:
	enum class Type
	{
		DEFAULT,
		SOFT,
		XBRZ,
		HQX
	};

	struct UpscalerUniforms
	{
		int32 mGameResolution[2] = { 0, 0 };
		int32 mOutputSize[2] = { 0, 0 };
		float mPixelFactor = 1.0f;
		float mScanlinesIntensity = 0.0f;
		float mPadding0[2] = { 0.0f, 0.0f };
	};

public:
	VulkanUpscaler(Type type, VulkanDrawerResources& resources);
	~VulkanUpscaler();

	bool startup();
	void shutdown();

	bool renderImage(vulkan::Texture& sourceTexture, SDL_Window& window, bool useVSync);
	bool renderImage(const Recti& rect, const Recti& fullViewport, vulkan::Texture& sourceTexture, vulkan::Texture& targetTexture);

private:
	struct LookupTexture
	{
		bool mInitialized = false;
		std::wstring mImagePath;
		vulkan::Texture mTexture;
	};

private:
	bool loadLookupTexture(LookupTexture& lookupTexture);
	bool createSampler(VkFilter filter, VkSampler& outSampler);
	bool drawFullscreenPass(vulkan::PipelineID pipelineId, const UpscalerUniforms& uniforms, vulkan::Texture& mainTexture, VkSampler mainSampler, vulkan::Texture* auxTexture, VkSampler auxSampler);
	bool drawPassIntoTarget(vulkan::PipelineID pipelineId, const Recti& viewportRect, const UpscalerUniforms& uniforms, vulkan::Texture& mainTexture, VkSampler mainSampler, vulkan::Texture* auxTexture, VkSampler auxSampler);

private:
	const Type mType = Type::DEFAULT;
	VulkanDrawerResources& mResources;
	std::vector<LookupTexture> mLookupTextures;
	vulkan::BufferAllocation mFullscreenQuadVertexBuffer;
	VkSampler mPointClampSampler = VK_NULL_HANDLE;
	VkSampler mLinearClampSampler = VK_NULL_HANDLE;
};

#endif
