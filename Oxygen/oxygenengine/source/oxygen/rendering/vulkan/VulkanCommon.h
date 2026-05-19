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

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <rmxmedia.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef min
	#undef min
#endif
#ifdef max
	#undef max
#endif
#ifdef OPAQUE
	#undef OPAQUE
#endif
#ifdef TRANSPARENT
	#undef TRANSPARENT
#endif
#ifdef ERROR
	#undef ERROR
#endif
#ifdef VOID
	#undef VOID
#endif
#ifdef IGNORE
	#undef IGNORE
#endif
#ifdef DUPLICATE
	#undef DUPLICATE
#endif

namespace vulkan
{
	static constexpr uint32_t VULKAN_FRAMES_IN_FLIGHT = 2;
	static constexpr VkFormat OFFSCREEN_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

	inline bool isSuccess(VkResult result)
	{
		return (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR);
	}

	inline size_t alignUp(size_t value, size_t alignment)
	{
		return (alignment > 0) ? ((value + alignment - 1) / alignment * alignment) : value;
	}
}

#endif
