/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)

#include "oxygen/rendering/vulkan/Pipeline.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/rendering/vulkan/VulkanDevice.h"

#include "oxygen/helper/Logging.h"


namespace
{
	struct PipelineDefinition
	{
		vulkan::PipelineID mPipelineId;
		const wchar_t* mVertexShader = nullptr;
		const wchar_t* mFragmentShader = nullptr;
		vulkan::VertexLayout mVertexLayout = vulkan::VertexLayout::P2;
		vulkan::BlendModeKey mBlendMode = vulkan::BlendModeKey::OPAQUE;
		bool mUseDepthTest = false;
		bool mDepthWriteEnable = false;
		VkCompareOp mDepthCompareOp = VK_COMPARE_OP_ALWAYS;
		bool mUseDynamicScissor = true;
	};

	void buildVertexLayout(vulkan::VertexLayout vertexLayout, VkVertexInputBindingDescription& outBinding, std::array<VkVertexInputAttributeDescription, 3>& outAttributes, uint32_t& outAttributeCount)
	{
		outBinding.binding = 0;
		outBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		switch (vertexLayout)
		{
			case vulkan::VertexLayout::P2:
				outBinding.stride = sizeof(float) * 2;
				outAttributes[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 };
				outAttributeCount = 1;
				break;

			case vulkan::VertexLayout::P2_U2:
				outBinding.stride = sizeof(float) * 4;
				outAttributes[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 };
				outAttributes[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2 };
				outAttributeCount = 2;
				break;

			case vulkan::VertexLayout::P2_C4:
				outBinding.stride = sizeof(float) * 6;
				outAttributes[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 };
				outAttributes[1] = { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 2 };
				outAttributeCount = 2;
				break;
		}
	}

	void setupBlendState(vulkan::BlendModeKey blendMode, VkPipelineColorBlendAttachmentState& outAttachment)
	{
		outAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		outAttachment.blendEnable = (blendMode != vulkan::BlendModeKey::OPAQUE) ? VK_TRUE : VK_FALSE;

		switch (blendMode)
		{
			default:
			case vulkan::BlendModeKey::OPAQUE:
				outAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
				outAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
				outAttachment.colorBlendOp = VK_BLEND_OP_ADD;
				outAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				outAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				outAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
				break;

			case vulkan::BlendModeKey::ALPHA:
				outAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				outAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				outAttachment.colorBlendOp = VK_BLEND_OP_ADD;
				outAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				outAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				outAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
				break;

			case vulkan::BlendModeKey::ADDITIVE:
				outAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				outAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
				outAttachment.colorBlendOp = VK_BLEND_OP_ADD;
				outAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				outAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				outAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
				break;

			case vulkan::BlendModeKey::SUBTRACTIVE:
				outAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				outAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
				outAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
				outAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				outAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				outAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
				break;

			case vulkan::BlendModeKey::MULTIPLICATIVE:
				outAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
				outAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
				outAttachment.colorBlendOp = VK_BLEND_OP_ADD;
				outAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				outAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				outAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
				break;

			case vulkan::BlendModeKey::MINIMUM:
				outAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
				outAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
				outAttachment.colorBlendOp = VK_BLEND_OP_MIN;
				outAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				outAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				outAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
				break;

			case vulkan::BlendModeKey::MAXIMUM:
				outAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
				outAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
				outAttachment.colorBlendOp = VK_BLEND_OP_MAX;
				outAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				outAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				outAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
				break;
		}
	}
}


bool vulkan::PipelineLibrary::startup(VulkanDevice& device, const Descriptors& descriptors, VkFormat colorFormat, VkFormat depthFormat, const std::wstring& pipelineCacheFilename)
{
	shutdown(device);

	mPipelineCacheFilename = pipelineCacheFilename;

	std::vector<uint8_t> cacheBytes;
	rmx::FileIO::readFile(mPipelineCacheFilename, cacheBytes);

	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	pipelineCacheCreateInfo.initialDataSize = cacheBytes.size();
	pipelineCacheCreateInfo.pInitialData = cacheBytes.empty() ? nullptr : cacheBytes.data();
	if (device.vk().CreatePipelineCache(device.getDevice(), &pipelineCacheCreateInfo, nullptr, &mPipelineCache) != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkCreatePipelineCache failed");
		return false;
	}

	std::array<VkDescriptorSetLayout, 2> setLayouts = { descriptors.getFrameSetLayout(), descriptors.getMaterialSetLayout() };
	VkPipelineLayoutCreateInfo layoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	layoutCreateInfo.setLayoutCount = (uint32_t)setLayouts.size();
	layoutCreateInfo.pSetLayouts = setLayouts.data();
	if (device.vk().CreatePipelineLayout(device.getDevice(), &layoutCreateInfo, nullptr, &mPipelineLayout) != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkCreatePipelineLayout failed");
		return false;
	}

	for (VkPipeline& pipeline : mPipelines)
		pipeline = VK_NULL_HANDLE;

	const PipelineDefinition pipelineDefinitions[] =
	{
		{ PipelineID::SIMPLE_COLORED,            L"simple_colored.vert.spv",         L"simple_colored.frag.spv",         VertexLayout::P2,    BlendModeKey::ALPHA,         false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_COLORED_OPAQUE,     L"simple_colored.vert.spv",         L"simple_colored.frag.spv",         VertexLayout::P2,    BlendModeKey::OPAQUE,        false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_COLORED_ADDITIVE,   L"simple_colored.vert.spv",         L"simple_colored.frag.spv",         VertexLayout::P2,    BlendModeKey::ADDITIVE,      false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_COLORED_SUBTRACTIVE, L"simple_colored.vert.spv",        L"simple_colored.frag.spv",         VertexLayout::P2,    BlendModeKey::SUBTRACTIVE,   false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_COLORED_MULTIPLICATIVE, L"simple_colored.vert.spv",     L"simple_colored.frag.spv",         VertexLayout::P2,    BlendModeKey::MULTIPLICATIVE,false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_COLORED_MINIMUM,    L"simple_colored.vert.spv",         L"simple_colored.frag.spv",         VertexLayout::P2,    BlendModeKey::MINIMUM,       false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_COLORED_MAXIMUM,    L"simple_colored.vert.spv",         L"simple_colored.frag.spv",         VertexLayout::P2,    BlendModeKey::MAXIMUM,       false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_TEXTURED_OPAQUE,    L"simple_textured.vert.spv",        L"simple_textured.frag.spv",        VertexLayout::P2,    BlendModeKey::OPAQUE,        false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_TEXTURED_ALPHA,     L"simple_textured.vert.spv",        L"simple_textured.frag.spv",        VertexLayout::P2,    BlendModeKey::ALPHA,         false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_TEXTURED_ADDITIVE,  L"simple_textured.vert.spv",        L"simple_textured.frag.spv",        VertexLayout::P2,    BlendModeKey::ADDITIVE,      false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_TEXTURED_SUBTRACTIVE, L"simple_textured.vert.spv",      L"simple_textured.frag.spv",        VertexLayout::P2,    BlendModeKey::SUBTRACTIVE,   false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_TEXTURED_MULTIPLICATIVE, L"simple_textured.vert.spv",   L"simple_textured.frag.spv",        VertexLayout::P2,    BlendModeKey::MULTIPLICATIVE,false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_TEXTURED_MINIMUM,   L"simple_textured.vert.spv",        L"simple_textured.frag.spv",        VertexLayout::P2,    BlendModeKey::MINIMUM,       false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_TEXTURED_MAXIMUM,   L"simple_textured.vert.spv",        L"simple_textured.frag.spv",        VertexLayout::P2,    BlendModeKey::MAXIMUM,       false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_TEXTURED_UV_OPAQUE, L"simple_textured_uv.vert.spv",     L"simple_textured.frag.spv",        VertexLayout::P2_U2, BlendModeKey::OPAQUE,        false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_TEXTURED_UV_ALPHA,  L"simple_textured_uv.vert.spv",     L"simple_textured.frag.spv",        VertexLayout::P2_U2, BlendModeKey::ALPHA,         false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_TEXTURED_UV_ADDITIVE, L"simple_textured_uv.vert.spv",   L"simple_textured.frag.spv",        VertexLayout::P2_U2, BlendModeKey::ADDITIVE,      false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_TEXTURED_UV_SUBTRACTIVE, L"simple_textured_uv.vert.spv", L"simple_textured.frag.spv",      VertexLayout::P2_U2, BlendModeKey::SUBTRACTIVE,   false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_TEXTURED_UV_MULTIPLICATIVE, L"simple_textured_uv.vert.spv", L"simple_textured.frag.spv",   VertexLayout::P2_U2, BlendModeKey::MULTIPLICATIVE,false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_TEXTURED_UV_MINIMUM, L"simple_textured_uv.vert.spv",    L"simple_textured.frag.spv",        VertexLayout::P2_U2, BlendModeKey::MINIMUM,       false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_TEXTURED_UV_MAXIMUM, L"simple_textured_uv.vert.spv",    L"simple_textured.frag.spv",        VertexLayout::P2_U2, BlendModeKey::MAXIMUM,       false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_INDEXED_OPAQUE,     L"simple_indexed.vert.spv",         L"simple_indexed.frag.spv",         VertexLayout::P2,    BlendModeKey::OPAQUE,        false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_INDEXED_ALPHA,      L"simple_indexed.vert.spv",         L"simple_indexed.frag.spv",         VertexLayout::P2,    BlendModeKey::ALPHA,         false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_INDEXED_ADDITIVE,   L"simple_indexed.vert.spv",         L"simple_indexed.frag.spv",         VertexLayout::P2,    BlendModeKey::ADDITIVE,      false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_INDEXED_SUBTRACTIVE, L"simple_indexed.vert.spv",        L"simple_indexed.frag.spv",         VertexLayout::P2,    BlendModeKey::SUBTRACTIVE,   false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_INDEXED_MULTIPLICATIVE, L"simple_indexed.vert.spv",     L"simple_indexed.frag.spv",         VertexLayout::P2,    BlendModeKey::MULTIPLICATIVE,false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_INDEXED_MINIMUM,    L"simple_indexed.vert.spv",         L"simple_indexed.frag.spv",         VertexLayout::P2,    BlendModeKey::MINIMUM,       false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_INDEXED_MAXIMUM,    L"simple_indexed.vert.spv",         L"simple_indexed.frag.spv",         VertexLayout::P2,    BlendModeKey::MAXIMUM,       false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_VERTEX_COLOR_OPAQUE, L"simple_vertex_color.vert.spv",   L"simple_vertex_color.frag.spv",    VertexLayout::P2_C4, BlendModeKey::OPAQUE,        false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_VERTEX_COLOR_ALPHA, L"simple_vertex_color.vert.spv",    L"simple_vertex_color.frag.spv",    VertexLayout::P2_C4, BlendModeKey::ALPHA,         false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_VERTEX_COLOR_ADDITIVE, L"simple_vertex_color.vert.spv", L"simple_vertex_color.frag.spv",    VertexLayout::P2_C4, BlendModeKey::ADDITIVE,      false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_VERTEX_COLOR_SUBTRACTIVE, L"simple_vertex_color.vert.spv", L"simple_vertex_color.frag.spv", VertexLayout::P2_C4, BlendModeKey::SUBTRACTIVE, false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_VERTEX_COLOR_MULTIPLICATIVE, L"simple_vertex_color.vert.spv", L"simple_vertex_color.frag.spv", VertexLayout::P2_C4, BlendModeKey::MULTIPLICATIVE,false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_VERTEX_COLOR_MINIMUM, L"simple_vertex_color.vert.spv",  L"simple_vertex_color.frag.spv",    VertexLayout::P2_C4, BlendModeKey::MINIMUM,       false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::SIMPLE_VERTEX_COLOR_MAXIMUM, L"simple_vertex_color.vert.spv",  L"simple_vertex_color.frag.spv",    VertexLayout::P2_C4, BlendModeKey::MAXIMUM,       false, false, VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::COPY,                      L"copy.vert.spv",                   L"copy.frag.spv",                   VertexLayout::P2,    BlendModeKey::OPAQUE,        false, false, VK_COMPARE_OP_ALWAYS, false },
		{ PipelineID::BLUR,                      L"copy.vert.spv",                   L"blur.frag.spv",                   VertexLayout::P2,    BlendModeKey::OPAQUE,        false, false, VK_COMPARE_OP_ALWAYS, false },
		{ PipelineID::UPSCALE_SOFT,              L"copy.vert.spv",                   L"upscaler_soft.frag.spv",          VertexLayout::P2,    BlendModeKey::OPAQUE,        false, false, VK_COMPARE_OP_ALWAYS, false },
		{ PipelineID::UPSCALE_XBRZ_PASS0,        L"copy.vert.spv",                   L"upscaler_xbrz_pass0.frag.spv",    VertexLayout::P2,    BlendModeKey::OPAQUE,        false, false, VK_COMPARE_OP_ALWAYS, false },
		{ PipelineID::UPSCALE_XBRZ_PASS1,        L"copy.vert.spv",                   L"upscaler_xbrz_pass1.frag.spv",    VertexLayout::P2,    BlendModeKey::OPAQUE,        false, false, VK_COMPARE_OP_ALWAYS, false },
		{ PipelineID::UPSCALE_HQ2X,              L"copy.vert.spv",                   L"upscaler_hq2x.frag.spv",          VertexLayout::P2,    BlendModeKey::OPAQUE,        false, false, VK_COMPARE_OP_ALWAYS, false },
		{ PipelineID::UPSCALE_HQ3X,              L"copy.vert.spv",                   L"upscaler_hq3x.frag.spv",          VertexLayout::P2,    BlendModeKey::OPAQUE,        false, false, VK_COMPARE_OP_ALWAYS, false },
		{ PipelineID::UPSCALE_HQ4X,              L"copy.vert.spv",                   L"upscaler_hq4x.frag.spv",          VertexLayout::P2,    BlendModeKey::OPAQUE,        false, false, VK_COMPARE_OP_ALWAYS, false },
		{ PipelineID::PLANE,                     L"plane.vert.spv",                  L"plane.frag.spv",                  VertexLayout::P2,    BlendModeKey::ALPHA,         true,  true,  VK_COMPARE_OP_ALWAYS, true },
		{ PipelineID::VDP_SPRITE_OPAQUE,         L"vdp_sprite.vert.spv",             L"vdp_sprite.frag.spv",             VertexLayout::P2,    BlendModeKey::OPAQUE,        true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::VDP_SPRITE,                L"vdp_sprite.vert.spv",             L"vdp_sprite.frag.spv",             VertexLayout::P2,    BlendModeKey::ALPHA,         true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::VDP_SPRITE_ADDITIVE,       L"vdp_sprite.vert.spv",             L"vdp_sprite.frag.spv",             VertexLayout::P2,    BlendModeKey::ADDITIVE,      true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::VDP_SPRITE_SUBTRACTIVE,    L"vdp_sprite.vert.spv",             L"vdp_sprite.frag.spv",             VertexLayout::P2,    BlendModeKey::SUBTRACTIVE,   true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::VDP_SPRITE_MULTIPLICATIVE, L"vdp_sprite.vert.spv",             L"vdp_sprite.frag.spv",             VertexLayout::P2,    BlendModeKey::MULTIPLICATIVE,true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::VDP_SPRITE_MINIMUM,        L"vdp_sprite.vert.spv",             L"vdp_sprite.frag.spv",             VertexLayout::P2,    BlendModeKey::MINIMUM,       true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::VDP_SPRITE_MAXIMUM,        L"vdp_sprite.vert.spv",             L"vdp_sprite.frag.spv",             VertexLayout::P2,    BlendModeKey::MAXIMUM,       true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::PALETTE_SPRITE_OPAQUE,     L"sprite.vert.spv",                 L"palette_sprite.frag.spv",         VertexLayout::P2,    BlendModeKey::OPAQUE,        true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::PALETTE_SPRITE,            L"sprite.vert.spv",                 L"palette_sprite.frag.spv",         VertexLayout::P2,    BlendModeKey::ALPHA,         true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::PALETTE_SPRITE_ADDITIVE,   L"sprite.vert.spv",                 L"palette_sprite.frag.spv",         VertexLayout::P2,    BlendModeKey::ADDITIVE,      true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::PALETTE_SPRITE_SUBTRACTIVE, L"sprite.vert.spv",                L"palette_sprite.frag.spv",         VertexLayout::P2,    BlendModeKey::SUBTRACTIVE,   true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::PALETTE_SPRITE_MULTIPLICATIVE, L"sprite.vert.spv",             L"palette_sprite.frag.spv",         VertexLayout::P2,    BlendModeKey::MULTIPLICATIVE,true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::PALETTE_SPRITE_MINIMUM,    L"sprite.vert.spv",                 L"palette_sprite.frag.spv",         VertexLayout::P2,    BlendModeKey::MINIMUM,       true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::PALETTE_SPRITE_MAXIMUM,    L"sprite.vert.spv",                 L"palette_sprite.frag.spv",         VertexLayout::P2,    BlendModeKey::MAXIMUM,       true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::COMPONENT_SPRITE_OPAQUE,   L"sprite.vert.spv",                 L"component_sprite.frag.spv",       VertexLayout::P2,    BlendModeKey::OPAQUE,        true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::COMPONENT_SPRITE,          L"sprite.vert.spv",                 L"component_sprite.frag.spv",       VertexLayout::P2,    BlendModeKey::ALPHA,         true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::COMPONENT_SPRITE_ADDITIVE, L"sprite.vert.spv",                 L"component_sprite.frag.spv",       VertexLayout::P2,    BlendModeKey::ADDITIVE,      true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::COMPONENT_SPRITE_SUBTRACTIVE, L"sprite.vert.spv",              L"component_sprite.frag.spv",       VertexLayout::P2,    BlendModeKey::SUBTRACTIVE,   true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::COMPONENT_SPRITE_MULTIPLICATIVE, L"sprite.vert.spv",           L"component_sprite.frag.spv",       VertexLayout::P2,    BlendModeKey::MULTIPLICATIVE,true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::COMPONENT_SPRITE_MINIMUM,  L"sprite.vert.spv",                 L"component_sprite.frag.spv",       VertexLayout::P2,    BlendModeKey::MINIMUM,       true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true },
		{ PipelineID::COMPONENT_SPRITE_MAXIMUM,  L"sprite.vert.spv",                 L"component_sprite.frag.spv",       VertexLayout::P2,    BlendModeKey::MAXIMUM,       true,  false, VK_COMPARE_OP_GREATER_OR_EQUAL, true }
	};

	for (const PipelineDefinition& pipelineDefinition : pipelineDefinitions)
	{
		mVertexLayouts[(size_t)pipelineDefinition.mPipelineId] = pipelineDefinition.mVertexLayout;
		createPipeline(device, pipelineDefinition.mPipelineId, descriptors, colorFormat, depthFormat, pipelineDefinition.mVertexShader, pipelineDefinition.mFragmentShader, pipelineDefinition.mVertexLayout, pipelineDefinition.mBlendMode, pipelineDefinition.mUseDepthTest, pipelineDefinition.mDepthWriteEnable, pipelineDefinition.mDepthCompareOp, pipelineDefinition.mUseDynamicScissor);
	}

	return true;
}

void vulkan::PipelineLibrary::shutdown(VulkanDevice& device)
{
	storePipelineCache(device);

	for (VkPipeline& pipeline : mPipelines)
	{
		if (pipeline != VK_NULL_HANDLE)
		{
			device.vk().DestroyPipeline(device.getDevice(), pipeline, nullptr);
			pipeline = VK_NULL_HANDLE;
		}
	}

	if (mPipelineLayout != VK_NULL_HANDLE)
	{
		device.vk().DestroyPipelineLayout(device.getDevice(), mPipelineLayout, nullptr);
		mPipelineLayout = VK_NULL_HANDLE;
	}
	if (mPipelineCache != VK_NULL_HANDLE)
	{
		device.vk().DestroyPipelineCache(device.getDevice(), mPipelineCache, nullptr);
		mPipelineCache = VK_NULL_HANDLE;
	}
}

bool vulkan::PipelineLibrary::createPipeline(VulkanDevice& device, PipelineID pipelineId, const Descriptors& descriptors, VkFormat colorFormat, VkFormat depthFormat, const wchar_t* vertexShaderFilename, const wchar_t* fragmentShaderFilename, VertexLayout vertexLayout, BlendModeKey blendMode, bool useDepthTest, bool depthWriteEnable, VkCompareOp depthCompareOp, bool useDynamicScissor)
{
	(void)descriptors;
	(void)useDynamicScissor;

	const std::vector<uint8_t> vertexShaderBytes = loadSpirv(vertexShaderFilename);
	const std::vector<uint8_t> fragmentShaderBytes = loadSpirv(fragmentShaderFilename);
	if (vertexShaderBytes.empty() || fragmentShaderBytes.empty())
		return false;

	VkShaderModule vertexShaderModule = createShaderModule(device, vertexShaderBytes);
	VkShaderModule fragmentShaderModule = createShaderModule(device, fragmentShaderBytes);
	if (vertexShaderModule == VK_NULL_HANDLE || fragmentShaderModule == VK_NULL_HANDLE)
	{
		if (vertexShaderModule != VK_NULL_HANDLE)
			device.vk().DestroyShaderModule(device.getDevice(), vertexShaderModule, nullptr);
		if (fragmentShaderModule != VK_NULL_HANDLE)
			device.vk().DestroyShaderModule(device.getDevice(), fragmentShaderModule, nullptr);
		return false;
	}

	const char mainName[] = "main";
	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {};
	shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStages[0].module = vertexShaderModule;
	shaderStages[0].pName = mainName;
	shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStages[1].module = fragmentShaderModule;
	shaderStages[1].pName = mainName;

	VkVertexInputBindingDescription bindingDescription = {};
	std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions = {};
	uint32_t attributeCount = 0;
	buildVertexLayout(vertexLayout, bindingDescription, attributeDescriptions, attributeCount);

	VkPipelineVertexInputStateCreateInfo vertexInputState = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	vertexInputState.vertexBindingDescriptionCount = 1;
	vertexInputState.pVertexBindingDescriptions = &bindingDescription;
	vertexInputState.vertexAttributeDescriptionCount = attributeCount;
	vertexInputState.pVertexAttributeDescriptions = attributeDescriptions.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizationState = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationState.cullMode = VK_CULL_MODE_NONE;
	rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationState.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisampleState = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	depthStencilState.depthTestEnable = useDepthTest ? VK_TRUE : VK_FALSE;
	depthStencilState.depthWriteEnable = (useDepthTest && depthWriteEnable) ? VK_TRUE : VK_FALSE;
	depthStencilState.depthCompareOp = depthCompareOp;
	depthStencilState.stencilTestEnable = VK_FALSE;

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	setupBlendState(blendMode, colorBlendAttachment);

	VkPipelineColorBlendStateCreateInfo colorBlendState = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	colorBlendState.attachmentCount = 1;
	colorBlendState.pAttachments = &colorBlendAttachment;

	std::array<VkDynamicState, 2> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
	dynamicState.pDynamicStates = dynamicStates.data();

	VkPipelineRenderingCreateInfo renderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	renderingCreateInfo.colorAttachmentCount = 1;
	renderingCreateInfo.pColorAttachmentFormats = &colorFormat;
	renderingCreateInfo.depthAttachmentFormat = useDepthTest ? depthFormat : VK_FORMAT_UNDEFINED;

	VkGraphicsPipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineCreateInfo.pNext = &renderingCreateInfo;
	pipelineCreateInfo.stageCount = (uint32_t)shaderStages.size();
	pipelineCreateInfo.pStages = shaderStages.data();
	pipelineCreateInfo.pVertexInputState = &vertexInputState;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.pDynamicState = &dynamicState;
	pipelineCreateInfo.layout = mPipelineLayout;

	VkPipeline pipeline = VK_NULL_HANDLE;
	const VkResult result = device.vk().CreateGraphicsPipelines(device.getDevice(), mPipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline);
	device.vk().DestroyShaderModule(device.getDevice(), vertexShaderModule, nullptr);
	device.vk().DestroyShaderModule(device.getDevice(), fragmentShaderModule, nullptr);

	if (result != VK_SUCCESS)
	{
		RMX_LOG_WARNING("Failed to create Vulkan pipeline " << (int)pipelineId << " from " << WString(vertexShaderFilename).toStdString() << " / " << WString(fragmentShaderFilename).toStdString() << " with result " << (int)result);
		return false;
	}

	mPipelines[(size_t)pipelineId] = pipeline;
	return true;
}

std::vector<uint8_t> vulkan::PipelineLibrary::loadSpirv(std::wstring_view filename) const
{
	std::vector<uint8_t> bytes;
	std::wstring resolvedPath = Configuration::instance().mEngineDataPath;
	rmx::FileIO::normalizePath(resolvedPath, true);
	resolvedPath += L"shader/vulkan/";
	resolvedPath += filename;

	if (!rmx::FileIO::readFile(resolvedPath, bytes))
	{
		std::wstring fallbackPath = L"../oxygenengine/data/shader/vulkan/";
		fallbackPath += filename;
		rmx::FileIO::normalizePath(fallbackPath, false);
		if (!rmx::FileIO::readFile(fallbackPath, bytes))
		{
			RMX_LOG_WARNING("Missing Vulkan shader bytecode: " << WString(resolvedPath).toStdString() << " (fallback also missing: " << WString(fallbackPath).toStdString() << ")");
		}
	}
	return bytes;
}

VkShaderModule vulkan::PipelineLibrary::createShaderModule(VulkanDevice& device, const std::vector<uint8_t>& bytes) const
{
	if (bytes.empty() || (bytes.size() & 3u) != 0)
		return VK_NULL_HANDLE;

	VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	createInfo.codeSize = bytes.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(bytes.data());

	VkShaderModule shaderModule = VK_NULL_HANDLE;
	if (device.vk().CreateShaderModule(device.getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkCreateShaderModule failed");
		return VK_NULL_HANDLE;
	}
	return shaderModule;
}

void vulkan::PipelineLibrary::storePipelineCache(VulkanDevice& device)
{
	if (mPipelineCache == VK_NULL_HANDLE || mPipelineCacheFilename.empty())
		return;

	size_t cacheSize = 0;
	if (device.vk().GetPipelineCacheData(device.getDevice(), mPipelineCache, &cacheSize, nullptr) != VK_SUCCESS || cacheSize == 0)
		return;

	std::vector<uint8_t> cacheBytes(cacheSize);
	if (device.vk().GetPipelineCacheData(device.getDevice(), mPipelineCache, &cacheSize, cacheBytes.data()) == VK_SUCCESS)
	{
		rmx::FileIO::saveFile(mPipelineCacheFilename, cacheBytes.data(), cacheBytes.size());
	}
}

#endif
