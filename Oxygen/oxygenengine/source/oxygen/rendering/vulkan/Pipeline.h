/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen/rendering/vulkan/Descriptors.h"

#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)

namespace vulkan
{
	enum class VertexLayout : uint8_t
	{
		P2 = 0,
		P2_U2,
		P2_C4
	};

	enum class BlendModeKey : uint8_t
	{
		OPAQUE = 0,
		ALPHA,
		ADDITIVE,
		SUBTRACTIVE,
		MULTIPLICATIVE,
		MINIMUM,
		MAXIMUM
	};

	enum class PipelineID : uint8_t
	{
		SIMPLE_COLORED = 0,
		SIMPLE_COLORED_OPAQUE,
		SIMPLE_COLORED_ADDITIVE,
		SIMPLE_COLORED_SUBTRACTIVE,
		SIMPLE_COLORED_MULTIPLICATIVE,
		SIMPLE_COLORED_MINIMUM,
		SIMPLE_COLORED_MAXIMUM,
		SIMPLE_TEXTURED_OPAQUE,
		SIMPLE_TEXTURED_ALPHA,
		SIMPLE_TEXTURED_ADDITIVE,
		SIMPLE_TEXTURED_SUBTRACTIVE,
		SIMPLE_TEXTURED_MULTIPLICATIVE,
		SIMPLE_TEXTURED_MINIMUM,
		SIMPLE_TEXTURED_MAXIMUM,
		SIMPLE_TEXTURED_UV_OPAQUE,
		SIMPLE_TEXTURED_UV_ALPHA,
		SIMPLE_TEXTURED_UV_ADDITIVE,
		SIMPLE_TEXTURED_UV_SUBTRACTIVE,
		SIMPLE_TEXTURED_UV_MULTIPLICATIVE,
		SIMPLE_TEXTURED_UV_MINIMUM,
		SIMPLE_TEXTURED_UV_MAXIMUM,
		SIMPLE_INDEXED_OPAQUE,
		SIMPLE_INDEXED_ALPHA,
		SIMPLE_INDEXED_ADDITIVE,
		SIMPLE_INDEXED_SUBTRACTIVE,
		SIMPLE_INDEXED_MULTIPLICATIVE,
		SIMPLE_INDEXED_MINIMUM,
		SIMPLE_INDEXED_MAXIMUM,
		SIMPLE_VERTEX_COLOR_OPAQUE,
		SIMPLE_VERTEX_COLOR_ALPHA,
		SIMPLE_VERTEX_COLOR_ADDITIVE,
		SIMPLE_VERTEX_COLOR_SUBTRACTIVE,
		SIMPLE_VERTEX_COLOR_MULTIPLICATIVE,
		SIMPLE_VERTEX_COLOR_MINIMUM,
		SIMPLE_VERTEX_COLOR_MAXIMUM,
		COPY,
		BLUR,
		UPSCALE_SOFT,
		UPSCALE_XBRZ_PASS0,
		UPSCALE_XBRZ_PASS1,
		UPSCALE_HQ2X,
		UPSCALE_HQ3X,
		UPSCALE_HQ4X,
		PLANE,
		VDP_SPRITE_OPAQUE,
		VDP_SPRITE,
		VDP_SPRITE_ADDITIVE,
		VDP_SPRITE_SUBTRACTIVE,
		VDP_SPRITE_MULTIPLICATIVE,
		VDP_SPRITE_MINIMUM,
		VDP_SPRITE_MAXIMUM,
		PALETTE_SPRITE_OPAQUE,
		PALETTE_SPRITE,
		PALETTE_SPRITE_ADDITIVE,
		PALETTE_SPRITE_SUBTRACTIVE,
		PALETTE_SPRITE_MULTIPLICATIVE,
		PALETTE_SPRITE_MINIMUM,
		PALETTE_SPRITE_MAXIMUM,
		COMPONENT_SPRITE_OPAQUE,
		COMPONENT_SPRITE,
		COMPONENT_SPRITE_ADDITIVE,
		COMPONENT_SPRITE_SUBTRACTIVE,
		COMPONENT_SPRITE_MULTIPLICATIVE,
		COMPONENT_SPRITE_MINIMUM,
		COMPONENT_SPRITE_MAXIMUM,
		COUNT
	};

	class PipelineLibrary
	{
	public:
		bool startup(class VulkanDevice& device, const Descriptors& descriptors, VkFormat colorFormat, VkFormat depthFormat, const std::wstring& pipelineCacheFilename);
		void shutdown(class VulkanDevice& device);

		inline VkPipeline getPipeline(PipelineID pipelineId) const			{ return mPipelines[(size_t)pipelineId]; }
		inline VkPipelineLayout getPipelineLayout() const					{ return mPipelineLayout; }
		inline VertexLayout getVertexLayout(PipelineID pipelineId) const	{ return mVertexLayouts[(size_t)pipelineId]; }

	private:
		bool createPipeline(class VulkanDevice& device, PipelineID pipelineId, const Descriptors& descriptors, VkFormat colorFormat, VkFormat depthFormat, const wchar_t* vertexShaderFilename, const wchar_t* fragmentShaderFilename, VertexLayout vertexLayout, BlendModeKey blendMode, bool useDepthTest, bool depthWriteEnable, VkCompareOp depthCompareOp, bool useDynamicScissor);
		std::vector<uint8_t> loadSpirv(std::wstring_view filename) const;
		VkShaderModule createShaderModule(class VulkanDevice& device, const std::vector<uint8_t>& bytes) const;
		void storePipelineCache(class VulkanDevice& device);

	private:
		std::array<VkPipeline, (size_t)PipelineID::COUNT> mPipelines = {};
		std::array<VertexLayout, (size_t)PipelineID::COUNT> mVertexLayouts = {};
		VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
		VkPipelineCache mPipelineCache = VK_NULL_HANDLE;
		std::wstring mPipelineCacheFilename;
	};
}

#endif
