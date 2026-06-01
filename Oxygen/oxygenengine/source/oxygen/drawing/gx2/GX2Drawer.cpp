/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/
// got a behemoth of a drawer this time, but we DO bounce back into software for some stuff
#include "oxygen/pch.h"
#include "oxygen/drawing/gx2/GX2Drawer.h"
#include "oxygen/drawing/DrawCollection.h"
#include "oxygen/drawing/DrawCommand.h"
#include "oxygen/drawing/DrawerTexture.h"
#include "oxygen/helper/Logging.h"
#include "oxygen/rendering/gx2/GX2RenderResources.h"
#include "oxygen/rendering/parts/PlaneManager.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/rendering/parts/ScrollOffsetsManager.h"
#include "oxygen/rendering/parts/palette/PaletteManager.h"
#include "oxygen/resources/PaletteCollection.h"
#include "oxygen/resources/SpriteCollection.h"

#if defined(PLATFORM_WIIU)
	#include <coreinit/cache.h>
	#include <coreinit/dynload.h>
	#include <coreinit/memdefaultheap.h>
	#include <coreinit/memheap.h>
	#include <gx2/clear.h>
	#include <gx2/context.h>
	#include <gx2/draw.h>
	#include <gx2/display.h>
	#include <gx2/event.h>
	#include <gx2/mem.h>
	#include <gx2/registers.h>
	#include <gx2/sampler.h>
	#include <gx2/shaders.h>
	#include <gx2/state.h>
	#include <gx2/surface.h>
	#include <gx2/swap.h>
	#include <gx2/texture.h>
	#include <gx2/utils.h>
	#include <gx2r/resource.h>
	#include <gx2r/surface.h>
	#include <whb/gfx.h>
	#include <whb/proc.h>
#endif


#if defined(PLATFORM_WIIU)

namespace
{
	struct PresentVertex
	{
		float x;
		float y;
		float u;
		float v;
	};

	struct ColorVertex
	{
		float x;
		float y;
		float r;
		float g;
		float b;
		float a;
	};

	struct PlaneVertex
	{
		float x;
		float y;
		float localX;
		float localY;
	};

	struct VdpSpriteBatchVertex
	{
		float x;
		float y;
		float localX;
		float localY;
		float spriteX;
		float spriteY;
		float sizeX;
		float sizeY;
		float firstPattern;
		float splitY;
		float tintR;
		float tintG;
		float tintB;
		float tintA;
		float addedR;
		float addedG;
		float addedB;
		float addedA;
	};

	struct PixelUniformSlot
	{
		uint32 offset = UINT32_MAX;
		int32 block = -1;
	};

	using CafeInitFn = void (*)();
	using CafeCompileVSFn = GX2VertexShader* (*)(const char*, char*, int, uint32);
	using CafeCompilePSFn = GX2PixelShader* (*)(const char*, char*, int, uint32);
	using CafeFreeVSFn = void (*)(GX2VertexShader*);
	using CafeFreePSFn = void (*)(GX2PixelShader*);
	using CafeDoneFn = void (*)();

	struct CafeCompiler
	{
		OSDynLoad_Module module = nullptr;
		CafeInitFn init = nullptr;
		CafeCompileVSFn compileVS = nullptr;
		CafeCompilePSFn compilePS = nullptr;
		CafeFreeVSFn freeVS = nullptr;
		CafeFreePSFn freePS = nullptr;
		CafeDoneFn done = nullptr;
		bool ready = false;
	};

	static constexpr uint32 FALLBACK_TV_WIDTH = 1280;
	static constexpr uint32 FALLBACK_TV_HEIGHT = 720;
	static constexpr uint32 PRESENT_VERTEX_COUNT = 6;
	static constexpr bool USE_WHB_PRESENT = false;
	static constexpr bool USE_SOFTWARE_WINDOW_DRAWER = false;
	static constexpr bool USE_CPU_COLOR_PRESENT = false;
	static constexpr bool FORCE_CPU_PRESENT_PATTERN = false;
	static constexpr bool FORCE_GPU_PRESENT_CLEAR = false;
	static constexpr bool FORCE_TEXTURE_PRESENT_PATTERN = false;
	static constexpr bool FORCE_COLOR_SHADER_TEST_RECT = false;
	static constexpr bool SKIP_GX2_TEARDOWN_ON_EXIT = true;
	static constexpr bool WAIT_FOR_SCAN_FLIP = true;
	static constexpr bool PRESENT_LOGS = true;
	static constexpr uint32 PRESENT_LOG_LIMIT = 8;
	static constexpr bool ENABLE_GX2_DRAWER_DIAGNOSTICS = false;
	static constexpr bool ENABLE_GX2_TEXT_DIAGNOSTICS = false;
	static constexpr bool ENABLE_GX2_MENU_COMMAND_TRACE = false;
	static constexpr uint32 GX2_BLEND_MASK_NONE = 0x00;
	static constexpr uint32 GX2_BLEND_MASK_ALL_TARGETS = 0xff;
	// originally going to be called GX2_COMMAND_BUFFER_SIZE, but there was a naming collision. Oops
	static constexpr uint32 NATIVE_GX2_COMMAND_BUFFER_SIZE = 0x400000;
	static constexpr size_t NATIVE_GX2_UNIFORM_BUFFER_MIN_SIZE = 0x40000;
	static CafeCompiler gCafeCompiler;

	static constexpr const char* PRESENT_VS = R"(
#version 330
in vec2 aPosition;
in vec2 aTexCoord;
out vec2 vTexCoord;
void main()
{
	vTexCoord = aTexCoord;
	gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

	static constexpr const char* PRESENT_PS = R"(
#version 330
in vec2 vTexCoord;
uniform sampler2D uTexture;
uniform PixelUniforms
{
	vec4 uTintColor;
	vec4 uAddedColor;
};
void main()
{
	vec4 color = texture(uTexture, vTexCoord);
	color = vec4(uAddedColor.rgb, 0.0) + color * uTintColor;
	gl_FragColor = color;
}
)";

	static constexpr const char* VDP_SPRITE_PS = R"(
#version 330
in vec2 vLocalOffset;
uniform sampler2D uPlaneDataTexture;
uniform vec4 uConfig0;
uniform vec4 uConfig1;
uniform vec4 uTintColor;
uniform vec4 uAddedColor;

vec4 samplePlaneData(float x, float y)
{
	return texture(uPlaneDataTexture, vec2((x + 0.5) / 256.0, (y + 0.5) / 1040.0));
}

float decodeByte(float value)
{
	return floor(value * 255.5);
}

void main()
{
	float posX = uConfig0.x;
	float posY = uConfig0.y;
	float sizePatternsX = uConfig0.z;
	float sizePatternsY = uConfig0.w;
	float firstPattern = uConfig1.x;
	float splitY = uConfig1.y;

	vec2 local = floor(vLocalOffset - vec2(posX, posY) + vec2(0.0001));
	float tileX = floor(local.x / 8.0);
	float tileY = floor(local.y / 8.0);
	float pixelX = local.x - tileX * 8.0;
	float pixelY = local.y - tileY * 8.0;

	if (mod(floor(firstPattern / 2048.0), 2.0) >= 1.0)
	{
		tileX = sizePatternsX - tileX - 1.0;
		pixelX = 7.0 - pixelX;
	}
	if (mod(floor(firstPattern / 4096.0), 2.0) >= 1.0)
	{
		tileY = sizePatternsY - tileY - 1.0;
		pixelY = 7.0 - pixelY;
	}

	float rawPattern = firstPattern + tileY + tileX * sizePatternsY;
	float patternNumber = mod(rawPattern, 2048.0);
	float patternPixelIndex = pixelX + pixelY * 8.0;
	vec4 patternColor = samplePlaneData(mod(patternNumber, 4.0) * 64.0 + patternPixelIndex, floor(patternNumber / 4.0));
	float paletteIndex = decodeByte(patternColor.r) + floor(mod(floor(rawPattern / 8192.0), 4.0)) * 16.0;
	if (mod(paletteIndex, 16.0) < 0.5)
		discard;

	float paletteX = mod(paletteIndex, 256.0);
	float paletteY = floor(paletteIndex / 256.0);
	float paletteOffset = (vLocalOffset.y < splitY) ? 0.0 : 2.0;
	vec4 color = samplePlaneData(paletteX, 512.0 + paletteY + paletteOffset);
	color = vec4(uAddedColor.rgb, 0.0) + color * uTintColor;
	if (color.a < 0.01)
		discard;
	gl_FragColor = color;
}
)";

	static constexpr const char* VDP_SPRITE_BATCH_VS = R"(
#version 330
in vec2 aPosition;
in vec2 aLocalOffset;
in vec4 aConfig0;
in vec2 aConfig1;
in vec4 aTintColor;
in vec4 aAddedColor;
out vec2 vLocalOffset;
out vec4 vConfig0;
out vec2 vConfig1;
out vec4 vTintColor;
out vec4 vAddedColor;
void main()
{
	vLocalOffset = aLocalOffset;
	vConfig0 = aConfig0;
	vConfig1 = aConfig1;
	vTintColor = aTintColor;
	vAddedColor = aAddedColor;
	gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

	static constexpr const char* VDP_SPRITE_BATCH_PS = R"(
#version 330
in vec2 vLocalOffset;
in vec4 vConfig0;
in vec2 vConfig1;
in vec4 vTintColor;
in vec4 vAddedColor;
uniform sampler2D uPlaneDataTexture;

vec4 samplePlaneData(float x, float y)
{
	return texture(uPlaneDataTexture, vec2((x + 0.5) / 256.0, (y + 0.5) / 1040.0));
}

float decodeByte(float value)
{
	return floor(value * 255.5);
}

void main()
{
	float posX = vConfig0.x;
	float posY = vConfig0.y;
	float sizePatternsX = vConfig0.z;
	float sizePatternsY = vConfig0.w;
	float firstPattern = vConfig1.x;
	float splitY = vConfig1.y;

	vec2 local = floor(vLocalOffset - vec2(posX, posY) + vec2(0.0001));
	float tileX = floor(local.x / 8.0);
	float tileY = floor(local.y / 8.0);
	float pixelX = local.x - tileX * 8.0;
	float pixelY = local.y - tileY * 8.0;

	if (mod(floor(firstPattern / 2048.0), 2.0) >= 1.0)
	{
		tileX = sizePatternsX - tileX - 1.0;
		pixelX = 7.0 - pixelX;
	}
	if (mod(floor(firstPattern / 4096.0), 2.0) >= 1.0)
	{
		tileY = sizePatternsY - tileY - 1.0;
		pixelY = 7.0 - pixelY;
	}

	float rawPattern = firstPattern + tileY + tileX * sizePatternsY;
	float patternNumber = mod(rawPattern, 2048.0);
	float patternPixelIndex = pixelX + pixelY * 8.0;
	vec4 patternColor = samplePlaneData(mod(patternNumber, 4.0) * 64.0 + patternPixelIndex, floor(patternNumber / 4.0));
	float paletteIndex = decodeByte(patternColor.r) + floor(mod(floor(rawPattern / 8192.0), 4.0)) * 16.0;
	if (mod(paletteIndex, 16.0) < 0.5)
		discard;

	float paletteX = mod(paletteIndex, 256.0);
	float paletteY = floor(paletteIndex / 256.0);
	float paletteOffset = (vLocalOffset.y < splitY) ? 0.0 : 2.0;
	vec4 color = samplePlaneData(paletteX, 512.0 + paletteY + paletteOffset);
	color = vec4(vAddedColor.rgb, 0.0) + color * vTintColor;
	if (color.a < 0.01)
		discard;
	gl_FragColor = color;
}
)";

static constexpr const char* PALETTE_SPRITE_PS = R"(
#version 330
in vec2 vLocalOffset;
uniform sampler2D uSpriteDataTexture;
uniform vec4 uConfig0;
uniform vec4 uConfig1;
uniform vec4 uTintColor;
uniform vec4 uAddedColor;

float decodeByte(float value)
{
	return floor(value * 255.5);
}

void main()
{
	vec2 rectPos = vec2(uConfig0.x, uConfig0.y);
	vec2 rectSize = vec2(uConfig0.z, uConfig0.w);
	vec2 dataSize = vec2(uConfig1.z, uConfig1.w);
	vec2 local = floor(vLocalOffset - rectPos + vec2(0.0001));
	vec2 uv = (local + vec2(0.5)) / dataSize;
	float sourceIndex = decodeByte(texture(uSpriteDataTexture, uv).r);
	if (mod(sourceIndex, 16.0) < 0.5)
		discard;

	float paletteIndex = sourceIndex + uConfig1.y;
	float paletteX = mod(paletteIndex, 256.0);
	float paletteY = floor(paletteIndex / 256.0);
	float paletteOffset = (vLocalOffset.y < uConfig1.x) ? 0.0 : 2.0;
	vec4 color = texture(uSpriteDataTexture, vec2((paletteX + 0.5) / dataSize.x, (rectSize.y + paletteY + paletteOffset + 0.5) / dataSize.y));
	color = vec4(uAddedColor.rgb, 0.0) + color * uTintColor;
	if (color.a < 0.01)
		discard;
	gl_FragColor = color;
}
)";

	static constexpr const char* COLOR_VS = R"(
#version 330
in vec2 aPosition;
in vec4 aColor;
out vec4 vColor;
void main()
{
	vColor = aColor;
	gl_Position = vec4(aPosition, 0.5, 1.0);
}
)";

	static constexpr const char* COLOR_PS = R"(
#version 330
in vec4 vColor;
void main()
{
	gl_FragColor = vColor;
}
)";

	static constexpr const char* PLANE_VS = R"(
#version 330
in vec2 aPosition;
in vec2 aLocalOffset;
out vec2 vLocalOffset;
void main()
{
	vLocalOffset = aLocalOffset;
	gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

	static constexpr const char* PLANE_PS = R"(
#version 330
in vec2 vLocalOffset;
uniform sampler2D uPlaneDataTexture;
uniform vec4 uConfig0;
uniform vec4 uConfig1;
uniform vec4 uConfig2;
uniform vec4 uConfig3;

vec4 samplePlaneData(float x, float y)
{
	return texture(uPlaneDataTexture, vec2((x + 0.5) / 256.0, (y + 0.5) / 1040.0));
}

float decodeByte(float value)
{
	return floor(value * 255.5);
}

void main()
{
	float playfieldPixelsX = uConfig0.x;
	float playfieldPixelsY = uConfig0.y;
	float playfieldPatternsX = uConfig0.z;
	float playfieldPatternsY = uConfig0.w;
	float priorityFlag = uConfig1.x;
	float paletteOffset = uConfig1.y;
	float vScrollOffsetBias = uConfig1.z;
	float noRepeat = uConfig1.w;
	float scrollOffsetX = uConfig2.x;
	float scrollOffsetY = uConfig2.y;
	float useHorizontalScrolling = uConfig2.z;
	float useVerticalScrolling = uConfig2.w;
	float planeIndex = uConfig3.x;
	float scrollIndex = uConfig3.y;

	float ix = floor(vLocalOffset.x + 0.0001);
	float iy = floor(vLocalOffset.y + 0.0001);
	if (useHorizontalScrolling > 0.5)
	{
		float scrollY = mod(iy, 256.0);
		vec4 scrollColor = samplePlaneData(scrollY, 1028.0 + scrollIndex);
		scrollOffsetX = decodeByte(scrollColor.r) + decodeByte(scrollColor.g) * 256.0;
	}
	if (useVerticalScrolling > 0.5)
	{
		float scrollX = floor(mod(ix - vScrollOffsetBias, 512.0) / 16.0);
		vec4 scrollColor = samplePlaneData(scrollX, 1033.0 + scrollIndex);
		scrollOffsetY = decodeByte(scrollColor.r) + decodeByte(scrollColor.g) * 256.0;
	}

	ix = mod(ix + scrollOffsetX, 4096.0);
	iy = mod(iy + scrollOffsetY, playfieldPixelsY);
	if (noRepeat > 0.5)
	{
		if (ix >= playfieldPixelsX)
			discard;
	}
	else
	{
		ix = mod(ix, playfieldPixelsX);
	}

	float patternX = floor(ix / 8.0);
	float patternY = floor(iy / 8.0);
	float localX = ix - patternX * 8.0;
	float localY = iy - patternY * 8.0;
	vec4 indexColor = samplePlaneData(patternX, 516.0 + planeIndex * 128.0 + patternY);
	float patternIndexLow = decodeByte(indexColor.r);
	float patternIndexHigh = decodeByte(indexColor.g);
	if ((patternIndexHigh >= 128.0) != (priorityFlag >= 0.5))
		discard;

	float atex = floor(mod(patternIndexHigh, 128.0) / 32.0) * 16.0;
	if (mod(patternIndexHigh, 16.0) >= 8.0)
		localX = 7.0 - localX;
	if (mod(patternIndexHigh, 32.0) >= 16.0)
		localY = 7.0 - localY;

	float patternNumber = patternIndexLow + mod(patternIndexHigh, 8.0) * 256.0;
	float patternPixelIndex = localX + localY * 8.0;
	vec4 patternColor = samplePlaneData(mod(patternNumber, 4.0) * 64.0 + patternPixelIndex, floor(patternNumber / 4.0));
	float paletteIndex = decodeByte(patternColor.r) + atex;
	if (mod(paletteIndex, 16.0) < 0.5)
		discard;

	float paletteX = mod(paletteIndex, 256.0);
	float paletteY = floor(paletteIndex / 256.0);
	vec4 color = samplePlaneData(paletteX, 512.0 + paletteY + paletteOffset * 4.0);
	if (color.a < 0.01)
		discard;
	gl_FragColor = color;
}
)";

	uint32 toGpuLittleEndian32(uint32 value)
	{
		return ((value & 0x000000ffu) << 24)
			| ((value & 0x0000ff00u) << 8)
			| ((value & 0x00ff0000u) >> 8)
			| ((value & 0xff000000u) >> 24);
	}

	void* gx2Alloc(uint32 size, uint32 alignment)
	{
		void* ptr = MEMAllocFromDefaultHeapEx(size, (int32)alignment);
		RMX_CHECK(nullptr != ptr, "GX2Drawer: GPU allocation failed for " << size << " bytes", return nullptr);
		return ptr;
	}

	void gx2Free(void* ptr)
	{
		if (nullptr != ptr)
		{
			MEMFreeToDefaultHeap(ptr);
		}
	}

	GX2RResourceFlags cpuWriteToGpuReadFlags()
	{
		return (GX2RResourceFlags)(GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ);
	}

	const char* dynLoadErrorName(OSDynLoad_Error error)
	{
		switch (error)
		{
			case OS_DYNLOAD_OK:					   return "OK";
			case OS_DYNLOAD_OUT_OF_MEMORY:		   return "OUT_OF_MEMORY";
			case OS_DYNLOAD_INVALID_NOTIFY_PTR:	   return "INVALID_NOTIFY_PTR";
			case OS_DYNLOAD_INVALID_MODULE_NAME_PTR: return "INVALID_MODULE_NAME_PTR";
			case OS_DYNLOAD_INVALID_MODULE_NAME:	   return "INVALID_MODULE_NAME";
			case OS_DYNLOAD_INVALID_ACQUIRE_PTR:	   return "INVALID_ACQUIRE_PTR";
			case OS_DYNLOAD_EMPTY_MODULE_NAME:	   return "EMPTY_MODULE_NAME";
			case OS_DYNLOAD_INVALID_ALLOCATOR_PTR:  return "INVALID_ALLOCATOR_PTR";
			case OS_DYNLOAD_OUT_OF_SYSTEM_MEMORY:   return "OUT_OF_SYSTEM_MEMORY";
			case OS_DYNLOAD_TLS_ALLOCATOR_LOCKED:   return "TLS_ALLOCATOR_LOCKED";
			case OS_DYNLOAD_MODULE_NOT_FOUND:	   return "MODULE_NOT_FOUND";
			default:							   return "UNKNOWN";
		}
	}

	bool loadCafeExport(OSDynLoad_Module module, const char* name, void** out)
	{
		*out = nullptr;
		const OSDynLoad_Error result = OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, name, out);
		if (result != OS_DYNLOAD_OK)
		{
			RMX_LOG_WARNING("CafeGLSL: missing export " << name
				<< " result=" << rmx::hexString((uint32)result, 8)
				<< " " << dynLoadErrorName(result));
			return false;
		}
		RMX_LOG_INFO("CafeGLSL: resolved export " << name << " at " << *out);
		return true;
	}

	bool loadCafeExports(OSDynLoad_Module module)
	{
		return loadCafeExport(module, "InitGLSLCompiler", (void**)&gCafeCompiler.init)
			&& loadCafeExport(module, "CompileVertexShader", (void**)&gCafeCompiler.compileVS)
			&& loadCafeExport(module, "CompilePixelShader", (void**)&gCafeCompiler.compilePS)
			&& loadCafeExport(module, "FreeVertexShader", (void**)&gCafeCompiler.freeVS)
			&& loadCafeExport(module, "FreePixelShader", (void**)&gCafeCompiler.freePS)
			&& loadCafeExport(module, "DestroyGLSLCompiler", (void**)&gCafeCompiler.done);
	}

	bool initCafe()
	{
		if (gCafeCompiler.ready)
			return true;

		static const char* paths[] =
		{
			"~/wiiu/libs/glslcompiler.rpl",
			"/vol/external01/wiiu/libs/glslcompiler.rpl",
			"/vol/external01/wiiu/libs/glslcompiler",
			"/vol/content/glslcompiler.rpl",
			"/vol/content/wuhb-content/glslcompiler.rpl",
			"./content/glslcompiler.rpl",
			"./wuhb-content/glslcompiler.rpl",
			"/vol/code/glslcompiler.rpl",
			"./glslcompiler.rpl",
			"glslcompiler.rpl",
			"glslcompiler",
			"/vol/external01/glslcompiler.rpl",
		};

		RMX_LOG_INFO("CafeGLSL: runtime compiler lookup begin, candidates=" << (uint32)(sizeof(paths) / sizeof(paths[0])));
		for (const char* path : paths)
		{
			OSDynLoad_Module module = nullptr;
			const OSDynLoad_Error result = OSDynLoad_Acquire(path, &module);
			if (result != OS_DYNLOAD_OK)
			{
				RMX_LOG_WARNING("CafeGLSL: acquire failed path='" << path
					<< "' result=" << rmx::hexString((uint32)result, 8)
					<< " " << dynLoadErrorName(result));
				continue;
			}

			RMX_LOG_INFO("CafeGLSL: acquired path='" << path << "' module=" << module);
			if (loadCafeExports(module))
			{
				gCafeCompiler.module = module;
				RMX_LOG_INFO("CafeGLSL: calling InitGLSLCompiler for path='" << path << "'");
				gCafeCompiler.init();
				gCafeCompiler.ready = true;
				RMX_LOG_INFO("CafeGLSL: initialized runtime compiler from path='" << path << "'");
				return true;
			}

			OSDynLoad_Release(module);
			RMX_LOG_WARNING("CafeGLSL: released path='" << path << "' because required exports were missing");
		}

		RMX_ERROR("CafeGLSL: glslcompiler.rpl was not found", return false);
	}

	void shutdownCafe()
	{
		if (!gCafeCompiler.ready)
			return;
		RMX_LOG_INFO("CafeGLSL: shutting down runtime compiler");
		gCafeCompiler.done();
		RMX_LOG_INFO("CafeGLSL: runtime compiler destroyed");
		OSDynLoad_Release(gCafeCompiler.module);
		RMX_LOG_INFO("CafeGLSL: runtime compiler module released");
		gCafeCompiler = CafeCompiler();
	}

	uint32 hashShaderSource(const char* text)
	{
		uint32 hash = 2166136261u;
		for (const char* it = text; nullptr != it && *it != '\0'; ++it)
		{
			hash ^= (uint8)*it;
			hash *= 16777619u;
		}
		return hash;
	}

	const char* cafeLogText(const char* log)
	{
		return (nullptr != log && log[0] != '\0') ? log : "<empty>";
	}

	void logVertexShaderInfo(const char* name, const GX2VertexShader* shader)
	{
		if (nullptr == shader)
			return;
		RMX_LOG_INFO("CafeGLSL: vertex shader '" << name
			<< "' ptr=" << shader
			<< " program=" << shader->program
			<< " size=" << shader->size
			<< " mode=" << (uint32)shader->mode
			<< " attribs=" << shader->attribVarCount
			<< " uniformBlocks=" << shader->uniformBlockCount
			<< " uniforms=" << shader->uniformVarCount
			<< " samplers=" << shader->samplerVarCount);
	}

	void logPixelShaderInfo(const char* name, const GX2PixelShader* shader)
	{
		if (nullptr == shader)
			return;
		RMX_LOG_INFO("CafeGLSL: pixel shader '" << name
			<< "' ptr=" << shader
			<< " program=" << shader->program
			<< " size=" << shader->size
			<< " mode=" << (uint32)shader->mode
			<< " uniformBlocks=" << shader->uniformBlockCount
			<< " uniforms=" << shader->uniformVarCount
			<< " samplers=" << shader->samplerVarCount);
	}

	uint32 toRGBA8(uint32 abgr)
	{
		const uint32 r = abgr & 0xff;
		const uint32 g = (abgr >> 8) & 0xff;
		const uint32 b = (abgr >> 16) & 0xff;
		const uint32 a = (abgr >> 24) & 0xff;
		return (r << 24) | (g << 16) | (b << 8) | a;
	}

	PresentVertex makePresentVertex(float x, float y, float u, float v)
	{
		return { x, y, u, v };
	}

	struct NativeTVConfig
	{
		uint32 width = FALLBACK_TV_WIDTH;
		uint32 height = FALLBACK_TV_HEIGHT;
		GX2TVRenderMode renderMode = GX2_TV_RENDER_MODE_WIDE_720P;
	};
// this was too hard to figure out on the drc, so the values are just hard coded for now. The size of it never changes anyway, so it's not a big deal.
	NativeTVConfig queryNativeTVConfig()
	{
		NativeTVConfig config;
		const GX2TVScanMode scanMode = GX2GetSystemTVScanMode();
		const GX2AspectRatio aspectRatio = GX2GetSystemTVAspectRatio();
		switch (scanMode)
		{
			case GX2_TV_SCAN_MODE_480I:
			case GX2_TV_SCAN_MODE_480P:
			case GX2_TV_SCAN_MODE_576I:
				if (aspectRatio == GX2_ASPECT_RATIO_4_3)
				{
					config.width = 640;
					config.height = 480;
					config.renderMode = GX2_TV_RENDER_MODE_STANDARD_480P;
				}
				else
				{
					config.width = 854;
					config.height = 480;
					config.renderMode = GX2_TV_RENDER_MODE_WIDE_480P;
				}
				break;

			case GX2_TV_SCAN_MODE_720P:
			case GX2_TV_SCAN_MODE_1080I:
			case GX2_TV_SCAN_MODE_1080P:
			default:
				config.width = FALLBACK_TV_WIDTH;
				config.height = FALLBACK_TV_HEIGHT;
				config.renderMode = GX2_TV_RENDER_MODE_WIDE_720P;
				break;
		}
		return config;
	}

	Vec2i getDrawableSize()
	{
		if constexpr (USE_WHB_PRESENT)
		{
			if (FTX::Video.valid() && FTX::Video->getScreenWidth() > 0 && FTX::Video->getScreenHeight() > 0)
			{
				return Vec2i((int)FTX::Video->getScreenWidth(), (int)FTX::Video->getScreenHeight());
			}
		}

		GX2ColorBuffer* tvColorBuffer = WHBGfxGetTVColourBuffer();
		if (nullptr != tvColorBuffer && tvColorBuffer->surface.width > 0 && tvColorBuffer->surface.height > 0)
		{
			return Vec2i((int)tvColorBuffer->surface.width, (int)tvColorBuffer->surface.height);
		}

		if (FTX::Video.valid() && FTX::Video->getScreenWidth() > 0 && FTX::Video->getScreenHeight() > 0)
		{
			return Vec2i((int)FTX::Video->getScreenWidth(), (int)FTX::Video->getScreenHeight());
		}

		return Vec2i((int)FALLBACK_TV_WIDTH, (int)FALLBACK_TV_HEIGHT);
	}

	uint32 getSamplerLocation(const WHBGfxShaderGroup& shaderGroup)
	{
		if (nullptr != shaderGroup.pixelShader && shaderGroup.pixelShader->samplerVarCount > 0)
		{
			return shaderGroup.pixelShader->samplerVars[0].location;
		}
		return 0;
	}

	uint32 getSamplerLocation(const WHBGfxShaderGroup& shaderGroup, const char* name)
	{
		if (nullptr != shaderGroup.pixelShader)
		{
			for (uint32 i = 0; i < shaderGroup.pixelShader->samplerVarCount; ++i)
			{
				const GX2SamplerVar& sampler = shaderGroup.pixelShader->samplerVars[i];
				if (nullptr != sampler.name && strcmp(sampler.name, name) == 0)
				{
					return sampler.location;
				}
			}
		}
		RMX_LOG_WARNING("GX2Drawer: sampler '" << name << "' not found");
		return UINT32_MAX;
	}

	PixelUniformSlot getPixelUniformSlot(const WHBGfxShaderGroup& shaderGroup, const char* name)
	{
		PixelUniformSlot slot;
		if (nullptr == shaderGroup.pixelShader)
			return slot;
		const GX2UniformVar* uniform = GX2GetPixelUniformVar(shaderGroup.pixelShader, name);
		if (nullptr != uniform)
		{
			slot.offset = uniform->offset;
			slot.block = uniform->block;
		}
		return slot;
	}

	PixelUniformSlot getVertexUniformSlot(const WHBGfxShaderGroup& shaderGroup, const char* name)
	{
		PixelUniformSlot slot;
		if (nullptr == shaderGroup.vertexShader)
			return slot;
		const GX2UniformVar* uniform = GX2GetVertexUniformVar(shaderGroup.vertexShader, name);
		if (nullptr != uniform)
		{
			slot.offset = uniform->offset;
			slot.block = uniform->block;
		}
		return slot;
	}

	bool getPixelUniformBlockIndex(const WHBGfxShaderGroup& shaderGroup, const char* name, uint32& outIndex)
	{
		if (nullptr == shaderGroup.pixelShader)
			return false;
		for (uint32 i = 0; i < shaderGroup.pixelShader->uniformBlockCount; ++i)
		{
			if (0 == strcmp(name, shaderGroup.pixelShader->uniformBlocks[i].name))
			{
				outIndex = i;
				return true;
			}
		}
		return false;
	}

	uint32 attribMaskForFormat(GX2AttribFormat format)
	{
		switch (format)
		{
			case GX2_ATTRIB_FORMAT_FLOAT_32_32:
				return GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
			case GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32:
				return GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
			default:
				return GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
		}
	}

	bool addAttrib(WHBGfxShaderGroup& shaderGroup, const char* name, uint32 buffer, uint32 offset, GX2AttribFormat format)
	{
		if (nullptr == shaderGroup.vertexShader || shaderGroup.numAttributes >= 16)
			return false;

		for (uint32 i = 0; i < shaderGroup.vertexShader->attribVarCount; ++i)
		{
			const GX2AttribVar& var = shaderGroup.vertexShader->attribVars[i];
			if (nullptr != var.name && strcmp(var.name, name) == 0)
			{
				GX2AttribStream& attrib = shaderGroup.attributes[shaderGroup.numAttributes++];
				memset(&attrib, 0, sizeof(attrib));
				attrib.location = var.location;
				attrib.buffer = buffer;
				attrib.offset = offset;
				attrib.format = format;
				attrib.type = GX2_ATTRIB_INDEX_PER_VERTEX;
				attrib.mask = attribMaskForFormat(format);
				attrib.endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
				RMX_LOG_INFO("GX2Drawer: bound attrib '" << name
					<< "' location=" << attrib.location
					<< " buffer=" << attrib.buffer
					<< " offset=" << attrib.offset
					<< " format=" << rmx::hexString((uint32)attrib.format, 8)
					<< " mask=" << rmx::hexString(attrib.mask, 8));
				return true;
			}
		}
		RMX_LOG_WARNING("GX2Drawer: attrib '" << name << "' not found; shader attribs="
			<< shaderGroup.vertexShader->attribVarCount);
		for (uint32 i = 0; i < shaderGroup.vertexShader->attribVarCount; ++i)
		{
			const GX2AttribVar& var = shaderGroup.vertexShader->attribVars[i];
			RMX_LOG_WARNING("GX2Drawer: vertex attrib[" << i << "] name="
				<< (nullptr != var.name ? var.name : "(null)")
				<< " location=" << var.location);
		}
		return false;
	}

	bool makeFetchShader(WHBGfxShaderGroup& shaderGroup)
	{
		const uint32 size = GX2CalcFetchShaderSizeEx(shaderGroup.numAttributes, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
		shaderGroup.fetchShaderProgram = gx2Alloc(size, GX2_SHADER_PROGRAM_ALIGNMENT);
		if (nullptr == shaderGroup.fetchShaderProgram)
			return false;

		GX2InitFetchShaderEx(&shaderGroup.fetchShader, static_cast<uint8*>(shaderGroup.fetchShaderProgram), shaderGroup.numAttributes, shaderGroup.attributes, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
		// Wii U cache stuff, do not remove unless you enjoy invisible bugs.
		DCFlushRange(shaderGroup.fetchShaderProgram, size);
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, shaderGroup.fetchShaderProgram, size);
		RMX_LOG_INFO("GX2Drawer: fetch shader ready attribs=" << shaderGroup.numAttributes
			<< " size=" << size
			<< " program=" << shaderGroup.fetchShaderProgram);
		return true;
	}

	void clearShaderGroup(WHBGfxShaderGroup& shaderGroup)
	{
		if (nullptr != shaderGroup.fetchShaderProgram)
		{
			gx2Free(shaderGroup.fetchShaderProgram);
		}
		memset(&shaderGroup, 0, sizeof(shaderGroup));
	}

	void freeCompiledShaderGroup(WHBGfxShaderGroup& shaderGroup)
	{
		if (nullptr != shaderGroup.vertexShader && nullptr != gCafeCompiler.freeVS)
		{
			gCafeCompiler.freeVS(shaderGroup.vertexShader);
		}
		if (nullptr != shaderGroup.pixelShader && nullptr != gCafeCompiler.freePS)
		{
			gCafeCompiler.freePS(shaderGroup.pixelShader);
		}
		clearShaderGroup(shaderGroup);
	}

	bool compileShaderPair(WHBGfxShaderGroup& shaderGroup, const char* name, const char* vs, const char* ps)
	{
		memset(&shaderGroup, 0, sizeof(shaderGroup));
		if (!initCafe())
			return false;

		char log[2048] = {};
		RMX_LOG_INFO("CafeGLSL: compiling '" << name
			<< "' vsBytes=" << strlen(vs)
			<< " vsHash=" << rmx::hexString(hashShaderSource(vs), 8)
			<< " psBytes=" << strlen(ps)
			<< " psHash=" << rmx::hexString(hashShaderSource(ps), 8)
			<< " flags=0");
		shaderGroup.vertexShader = gCafeCompiler.compileVS(vs, log, sizeof(log), 0);
		if (nullptr == shaderGroup.vertexShader)
		{
			RMX_ERROR("CafeGLSL: vertex compile failed for '" << name << "': " << cafeLogText(log), return false);
		}
		if (log[0] != '\0')
			RMX_LOG_INFO("CafeGLSL: vertex compiler log for '" << name << "': " << log);
		logVertexShaderInfo(name, shaderGroup.vertexShader);

		memset(log, 0, sizeof(log));
		shaderGroup.pixelShader = gCafeCompiler.compilePS(ps, log, sizeof(log), 0);
		if (nullptr == shaderGroup.pixelShader)
		{
			RMX_ERROR("CafeGLSL: pixel compile failed for '" << name << "': " << cafeLogText(log), freeCompiledShaderGroup(shaderGroup); return false);
		}
		if (log[0] != '\0')
			RMX_LOG_INFO("CafeGLSL: pixel compiler log for '" << name << "': " << log);
		logPixelShaderInfo(name, shaderGroup.pixelShader);

		RMX_LOG_INFO("CafeGLSL: compiled '" << name << "'");
		return true;
	}
// CafeGLSL needs this
	void setCafeGLSLUniformBlockShaderMode(const WHBGfxShaderGroup& shaderGroup)
	{
		GX2ShaderMode mode = GX2_SHADER_MODE_UNIFORM_REGISTER;
		if (nullptr != shaderGroup.vertexShader)
			mode = shaderGroup.vertexShader->mode;
		else if (nullptr != shaderGroup.pixelShader)
			mode = shaderGroup.pixelShader->mode;
		GX2SetShaderMode(mode);
	}

	struct BitmapSample
	{
		uint32 center = 0;
		uint32 sampleXor = 0;
		uint32 nonBlackSamples = 0;
		uint32 alphaSamples = 0;
		uint32 rgbWithoutAlphaSamples = 0;
	};

	BitmapSample sampleBitmap(const Bitmap& bitmap)
	{
		BitmapSample sample;
		if (bitmap.empty())
			return sample;

		sample.center = bitmap.getPixel(bitmap.getWidth() / 2, bitmap.getHeight() / 2);
		for (int y = 0; y < bitmap.getHeight(); y += std::max(1, bitmap.getHeight() / 8))
		{
			for (int x = 0; x < bitmap.getWidth(); x += std::max(1, bitmap.getWidth() / 8))
			{
				const uint32 pixel = bitmap.getPixel(x, y);
				sample.sampleXor ^= pixel;
				if ((pixel & 0x00ffffff) != 0)
				{
					++sample.nonBlackSamples;
					if ((pixel & 0xff000000) == 0)
					{
						++sample.rgbWithoutAlphaSamples;
					}
				}
				if ((pixel & 0xff000000) != 0)
				{
					++sample.alphaSamples;
				}
			}
		}
		return sample;
	}

	bool bitmapContainsTransparentPixels(const Bitmap& bitmap)
	{
		if (bitmap.empty())
			return false;

		const uint32* pixels = bitmap.getData();
		const int count = bitmap.getPixelCount();
		for (int i = 0; i < count; ++i)
		{
			if ((pixels[i] & 0xff000000) != 0xff000000)
				return true;
		}
		return false;
	}

	void logBitmapSample(const char* label, const Bitmap& bitmap)
	{
		const BitmapSample sample = sampleBitmap(bitmap);
		RMX_LOG_INFO(label << " size=" << bitmap.getWidth() << "x" << bitmap.getHeight()
			<< " nonBlack=" << sample.nonBlackSamples
			<< " alpha=" << sample.alphaSamples
			<< " rgbNoAlpha=" << sample.rgbWithoutAlphaSamples
			<< " xor=" << rmx::hexString(sample.sampleXor, 8)
			<< " center=" << rmx::hexString(sample.center, 8));
	}


	void logTextureSample(const char* label, const DrawerTexture* texture)
	{
		if (nullptr == texture)
		{
			RMX_LOG_INFO(label << " texture=null");
			return;
		}
		logBitmapSample(label, texture->getBitmap());
	}

	void destroyTextureStorage(GX2Texture& texture, bool ownsImage)
	{
		if (ownsImage && nullptr != texture.surface.image)
		{
			GX2DrawDone();
			GX2RDestroySurfaceEx(&texture.surface, texture.surface.resourceFlags);
		}
		memset(&texture, 0, sizeof(texture));
	}

	bool initializeTextureStorage(GX2Texture& texture, const Vec2i& size)
	{
		destroyTextureStorage(texture, true);
		if (size.x <= 0 || size.y <= 0)
			return false;

		memset(&texture, 0, sizeof(texture));
		texture.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
		texture.surface.width = (uint32)size.x;
		texture.surface.height = (uint32)size.y;
		texture.surface.depth = 1;
		texture.surface.mipLevels = 1;
		texture.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
		texture.surface.aa = GX2_AA_MODE1X;
		texture.surface.use = GX2_SURFACE_USE_TEXTURE;
		texture.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
		GX2CalcSurfaceSizeAndAlignment(&texture.surface);
		texture.viewFirstMip = 0;
		texture.viewNumMips = 1;
		texture.viewFirstSlice = 0;
		texture.viewNumSlices = 1;
		texture.compMap = GX2_COMP_MAP(GX2_SQ_SEL_A, GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R);

		const GX2RResourceFlags flags = (GX2RResourceFlags)(
			GX2R_RESOURCE_BIND_TEXTURE |
			GX2R_RESOURCE_USAGE_CPU_WRITE |
			GX2R_RESOURCE_USAGE_GPU_READ |
			GX2R_RESOURCE_USAGE_FORCE_MEM2);
		if (!GX2RCreateSurface(&texture.surface, flags))
		{
			RMX_ERROR("GX2Drawer: GX2R texture allocation failed for " << size.x << "x" << size.y, return false);
		}
		GX2InitTextureRegs(&texture);

		void* pixels = GX2RLockSurfaceEx(&texture.surface, 0, GX2R_RESOURCE_USAGE_CPU_WRITE);
		if (nullptr != pixels)
		{
			memset(pixels, 0, texture.surface.imageSize);
			GX2RUnlockSurfaceEx(&texture.surface, 0, GX2R_RESOURCE_USAGE_CPU_WRITE);
			GX2RInvalidateSurface(&texture.surface, 0, cpuWriteToGpuReadFlags());
		}
		return true;
	}

	void uploadBitmapToTexture(GX2Texture& texture, const Bitmap& bitmap)
	{
		if (bitmap.empty() || nullptr == texture.surface.image)
			return;

		uint8* dstBase = static_cast<uint8*>(GX2RLockSurfaceEx(&texture.surface, 0, GX2R_RESOURCE_USAGE_CPU_WRITE));
		if (nullptr == dstBase)
		{
			RMX_ERROR("GX2Drawer: failed to lock GX2R texture upload surface", return);
		}
		const uint32 dstPitchBytes = texture.surface.pitch * 4;
		const int width = std::min<int>(bitmap.getWidth(), texture.surface.width);
		const int height = std::min<int>(bitmap.getHeight(), texture.surface.height);

		for (int y = 0; y < height; ++y)
		{
			const uint32* src = bitmap.getPixelPointer(0, y);
			uint8* dst = dstBase + (size_t)y * dstPitchBytes;
			memcpy(dst, src, (size_t)width * sizeof(uint32));
		}

		GX2RUnlockSurfaceEx(&texture.surface, 0, GX2R_RESOURCE_USAGE_CPU_WRITE);
		GX2RInvalidateSurface(&texture.surface, 0, cpuWriteToGpuReadFlags());
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, texture.surface.image, texture.surface.imageSize);
		GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE, texture.surface.image, texture.surface.imageSize);
	}

	void uploadBitmapRegionToTexture(GX2Texture& texture, const Bitmap& bitmap, const Recti& inputRect)
	{
		if (bitmap.empty() || nullptr == texture.surface.image)
			return;

		Recti rect = Recti::getIntersection(inputRect, Recti(0, 0, std::min<int>(bitmap.getWidth(), texture.surface.width), std::min<int>(bitmap.getHeight(), texture.surface.height)));
		if (rect.empty())
			return;

		uint8* dstBase = static_cast<uint8*>(GX2RLockSurfaceEx(&texture.surface, 0, GX2R_RESOURCE_USAGE_CPU_WRITE));
		if (nullptr == dstBase)
		{
			RMX_ERROR("GX2Drawer: failed to lock GX2R texture region upload surface", return);
		}

		const uint32 dstPitchBytes = texture.surface.pitch * 4;
		for (int y = 0; y < rect.height; ++y)
		{
			const uint32* src = bitmap.getPixelPointer(rect.x, rect.y + y);
			uint8* dst = dstBase + (size_t)(rect.y + y) * dstPitchBytes + (size_t)rect.x * sizeof(uint32);
			memcpy(dst, src, (size_t)rect.width * sizeof(uint32));
		}

		GX2RUnlockSurfaceEx(&texture.surface, 0, GX2R_RESOURCE_USAGE_CPU_WRITE);
		GX2RInvalidateSurface(&texture.surface, 0, cpuWriteToGpuReadFlags());
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, texture.surface.image, texture.surface.imageSize);
		GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE, texture.surface.image, texture.surface.imageSize);
	}

	void invalidateSurfaceAfterColorWrite(const GX2Surface& surface)
	{
		if (nullptr != surface.image && surface.imageSize > 0)
		{
			GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), surface.image, surface.imageSize);
		}
		if (nullptr != surface.mipmaps && surface.mipmapSize > 0)
		{
			GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE, surface.mipmaps, surface.mipmapSize);
		}
	}

	bool initializeColorBuffer(GX2ColorBuffer& colorBuffer, const Vec2i& size, GX2SurfaceUse useFlags)
	{
		if (size.x <= 0 || size.y <= 0)
			return false;

		memset(&colorBuffer, 0, sizeof(colorBuffer));
		colorBuffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
		colorBuffer.surface.width = (uint32)size.x;
		colorBuffer.surface.height = (uint32)size.y;
		colorBuffer.surface.depth = 1;
		colorBuffer.surface.mipLevels = 1;
		colorBuffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
		colorBuffer.surface.aa = GX2_AA_MODE1X;
		colorBuffer.surface.use = useFlags;
		colorBuffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
		colorBuffer.surface.swizzle = 0;
		GX2CalcSurfaceSizeAndAlignment(&colorBuffer.surface);
		colorBuffer.surface.image = gx2Alloc(colorBuffer.surface.imageSize, colorBuffer.surface.alignment);
		if (nullptr == colorBuffer.surface.image)
			return false;

		memset(colorBuffer.surface.image, 0, colorBuffer.surface.imageSize);
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, colorBuffer.surface.image, colorBuffer.surface.imageSize);
		colorBuffer.viewMip = 0;
		colorBuffer.viewFirstSlice = 0;
		colorBuffer.viewNumSlices = 1;
		colorBuffer.aaBuffer = nullptr;
		colorBuffer.aaSize = 0;
		GX2InitColorBufferRegs(&colorBuffer);
		return true;
	}

	void initializeTextureViewFromColorBuffer(GX2Texture& texture, const GX2ColorBuffer& colorBuffer)
	{
		memset(&texture, 0, sizeof(texture));
		texture.surface = colorBuffer.surface;
		texture.viewFirstMip = 0;
		texture.viewNumMips = 1;
		texture.viewFirstSlice = 0;
		texture.viewNumSlices = 1;
		texture.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
		GX2InitTextureRegs(&texture);
	}

	void buildPaletteSpriteBitmap(Bitmap& output, const PaletteSprite& sprite, const PaletteBase& palette)
	{
		const PaletteBitmap& indexed = sprite.getBitmap();
		output.create(indexed.getSize());

		const uint32* colors = palette.getRawColors();
		const size_t colorCount = palette.getSize();
		uint32* dst = output.getData();
		for (int i = 0; i < indexed.getPixelCount(); ++i)
		{
			const uint8 index = indexed[i];
			dst[i] = (index < colorCount) ? colors[index] : 0;
		}
	}

	class GX2DrawerTextureImplementation final : public DrawerTextureImplementation
	{
	public:
		explicit GX2DrawerTextureImplementation(DrawerTexture& owner) : DrawerTextureImplementation(owner) {}

		~GX2DrawerTextureImplementation() override
		{
			destroy();
		}

		void updateFromBitmap(const Bitmap& bitmap) override
		{
			const Vec2i size = bitmap.getSize();
			if (mSize != size || nullptr == mTexture.surface.image || mRenderTarget)
			{
				destroy();
				if (!initializeTextureStorage(mTexture, size))
					return;
				mOwnsTextureImage = true;
				mRenderTarget = false;
				mSize = size;
			}
			uploadBitmapToTexture(mTexture, bitmap);
			mContainsTransparentPixels = bitmapContainsTransparentPixels(bitmap);
			++mChangeCounter;
		}

		void updateFromBitmapRegion(const Bitmap& bitmap, const Recti& rect) override
		{
			const Vec2i size = bitmap.getSize();
			if (mSize != size || nullptr == mTexture.surface.image || mRenderTarget)
			{
				updateFromBitmap(bitmap);
				return;
			}
			uploadBitmapRegionToTexture(mTexture, bitmap, rect);
			if (!mContainsTransparentPixels)
			{
				const Recti clampedRect = Recti::getIntersection(rect, Recti(0, 0, bitmap.getWidth(), bitmap.getHeight()));
				for (int y = 0; y < clampedRect.height && !mContainsTransparentPixels; ++y)
				{
					const uint32* src = bitmap.getPixelPointer(clampedRect.x, clampedRect.y + y);
					for (int x = 0; x < clampedRect.width; ++x)
					{
						if ((src[x] & 0xff000000u) != 0xff000000u)
						{
							mContainsTransparentPixels = true;
							break;
						}
					}
				}
			}
			++mChangeCounter;
		}

		void setupAsRenderTarget(const Vec2i& size) override
		{
			mOwner.accessBitmap().create(size.x, size.y);
			if (mRenderTarget && mSize == size && nullptr != mColorBuffer.surface.image)
				return;

			destroy();
			const GX2SurfaceUse useFlags = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
			if (!initializeColorBuffer(mColorBuffer, size, useFlags))
				return;
			initializeTextureViewFromColorBuffer(mTexture, mColorBuffer);
			mOwnsTextureImage = false;
			mRenderTarget = true;
			mSize = size;
			mContainsTransparentPixels = false;
			++mChangeCounter;
		}

		void writeContentToBitmap(Bitmap& outBitmap) override
		{
			// Readback from a tiled GX2 render target is intentionally not on the hot path yet.
			// Keep the CPU-side owner bitmap alive for code that asks for a debug/snapshot copy.
			outBitmap = mOwner.accessBitmap();
		}

		void refreshImplementation(bool setupRenderTarget, const Vec2i& size) override
		{
			if (setupRenderTarget)
			{
				setupAsRenderTarget(size);
			}
			else if (!mOwner.accessBitmap().empty())
			{
				updateFromBitmap(mOwner.accessBitmap());
			}
		}

		void destroy()
		{
			if (nullptr != mColorBuffer.surface.image)
			{
				GX2DrawDone();
				gx2Free(mColorBuffer.surface.image);
				memset(&mColorBuffer, 0, sizeof(mColorBuffer));
			}
			destroyTextureStorage(mTexture, mOwnsTextureImage);
			mOwnsTextureImage = false;
			mRenderTarget = false;
			mSize.clear();
		}

	public:
		GX2Texture mTexture = {};
		GX2ColorBuffer mColorBuffer = {};
		Vec2i mSize;
		bool mRenderTarget = false;
		bool mOwnsTextureImage = false;
		bool mContainsTransparentPixels = false;
		SamplingMode mSamplingMode = SamplingMode::POINT;
		TextureWrapMode mWrapMode = TextureWrapMode::CLAMP;
		uint32 mChangeCounter = 0;
	};
}

struct GX2Drawer::Internal
{
public:
	struct CachedSpriteTexture
	{
		std::unique_ptr<DrawerTexture> mTexture;
		uint32 mSpriteChangeCounter = 0xffffffff;
		uint16 mPaletteChangeCounter = 0xffff;
		Vec2i mSourceSize;
	};

	Internal()
	{
		if (!initializeNativeDisplay())
		{
			RMX_LOG_ERROR("GX2Drawer: native display setup failed");
			return;
		}

		if (!initializeShader())
		{
			RMX_LOG_ERROR("GX2Drawer: present shader setup failed");
			showStartupFailureColor(1.0f, 0.0f, 0.0f);
			return;
		}

		if (!initializeColorShader())
		{
			RMX_LOG_ERROR("GX2Drawer: color shader setup failed");
			showStartupFailureColor(1.0f, 0.0f, 1.0f);
			return;
		}

		if (!initializePlaneShader())
		{
			RMX_LOG_ERROR("GX2Drawer: plane shader setup failed");
			showStartupFailureColor(1.0f, 1.0f, 0.0f);
			return;
		}

		if (!initializeVdpSpriteShader())
		{
			RMX_LOG_ERROR("GX2Drawer: VDP sprite shader setup failed");
			showStartupFailureColor(0.0f, 1.0f, 1.0f);
			return;
		}

		if (!initializeVdpSpriteBatchShader())
		{
			RMX_LOG_WARNING("GX2Drawer: VDP sprite batch shader setup failed; batching disabled");
		}

		if (!initializePaletteSpriteShader())
		{
			RMX_LOG_ERROR("GX2Drawer: palette sprite shader setup failed");
			showStartupFailureColor(0.2f, 1.0f, 0.2f);
			return;
		}

		initializeSampler();
		initializeVertexBuffer();
		if (nullptr == mVertexBuffer)
		{
			RMX_LOG_ERROR("GX2Drawer: vertex buffer setup failed");
			showStartupFailureColor(0.0f, 0.0f, 1.0f);
			return;
		}

		mSetupSuccessful = true;
		RMX_LOG_INFO("GX2Drawer: native GX2 presenter ready");
	}

	~Internal()
	{
		RMX_LOG_INFO("GX2Drawer: shutdown begin");
		if constexpr (SKIP_GX2_TEARDOWN_ON_EXIT)
		{
			RMX_LOG_INFO("GX2Drawer: leaving GX2 resources for process teardown");
			return;
		}
		destroyCpuColorBuffer();
		destroyTexture();
		destroyTextureStorage(mScratchTexture, true);
		if (nullptr != mDynamicVertexBuffer)
		{
			gx2Free(mDynamicVertexBuffer);
			mDynamicVertexBuffer = nullptr;
			mDynamicVertexCapacity = 0;
		}
		if (nullptr != mDynamicUniformBuffer)
		{
			gx2Free(mDynamicUniformBuffer);
			mDynamicUniformBuffer = nullptr;
			mDynamicUniformCapacity = 0;
		}
		if (nullptr != mVertexBuffer)
		{
			gx2Free(mVertexBuffer);
			mVertexBuffer = nullptr;
		}
		if (mShaderReady)
		{
			freeCompiledShaderGroup(mShaderGroup);
			mShaderReady = false;
		}
		if (mColorShaderReady)
		{
			freeCompiledShaderGroup(mColorShaderGroup);
			mColorShaderReady = false;
		}
		if (mPlaneShaderReady)
		{
			freeCompiledShaderGroup(mPlaneShaderGroup);
			mPlaneShaderReady = false;
		}
		if (mVdpSpriteShaderReady)
		{
			freeCompiledShaderGroup(mVdpSpriteShaderGroup);
			mVdpSpriteShaderReady = false;
		}
		if (mVdpSpriteBatchShaderReady)
		{
			freeCompiledShaderGroup(mVdpSpriteBatchShaderGroup);
			mVdpSpriteBatchShaderReady = false;
		}
		if (mPaletteSpriteShaderReady)
		{
			freeCompiledShaderGroup(mPaletteSpriteShaderGroup);
			mPaletteSpriteShaderReady = false;
		}
		if (nullptr != mPixelUniformBlock)
		{
			gx2Free(mPixelUniformBlock);
			mPixelUniformBlock = nullptr;
			mPixelUniformBlockSize = 0;
		}
		if (mOwnsGfx)
		{
			RMX_LOG_INFO("GX2Drawer: WHBGfxShutdown begin");
			WHBGfxShutdown();
			RMX_LOG_INFO("GX2Drawer: WHBGfxShutdown complete");
			mOwnsGfx = false;
		}
		if (mNativeContext)
		{
			gx2Free(mNativeContext);
			mNativeContext = nullptr;
		}
		if (mTvScanBuffer)
		{
			gx2Free(mTvScanBuffer);
			mTvScanBuffer = nullptr;
		}
		if (mDrcScanBuffer)
		{
			gx2Free(mDrcScanBuffer);
			mDrcScanBuffer = nullptr;
		}
		if (mCommandBuffer)
		{
			RMX_LOG_INFO("GX2Drawer: GX2Shutdown begin");
			GX2Shutdown();
			RMX_LOG_INFO("GX2Drawer: GX2Shutdown complete");
			gx2Free(mCommandBuffer);
			mCommandBuffer = nullptr;
		}
		shutdownCafe();
		RMX_LOG_INFO("GX2Drawer: shutdown complete");
	}

	bool initializeNativeDisplay()
	{
		if (!WHBProcIsRunning())
		{
			RMX_LOG_WARNING("GX2Drawer: ProcUI was not initialized before GX2 setup");
		}

		if constexpr (USE_WHB_PRESENT)
		{
			if (!WHBGfxInit())
				return false;
			mOwnsGfx = true;

			GX2ColorBuffer* tvColorBuffer = WHBGfxGetTVColourBuffer();
			if (nullptr != tvColorBuffer && tvColorBuffer->surface.width > 0 && tvColorBuffer->surface.height > 0)
				mNativeDrawableSize = Vec2i((int)tvColorBuffer->surface.width, (int)tvColorBuffer->surface.height);
			else
				mNativeDrawableSize = Vec2i((int)FALLBACK_TV_WIDTH, (int)FALLBACK_TV_HEIGHT);

			RMX_LOG_INFO("GX2Drawer: initialized WHB GX2 display tv=" << mNativeDrawableSize.x << "x" << mNativeDrawableSize.y);
			return true;
		}

		mCommandBuffer = gx2Alloc(NATIVE_GX2_COMMAND_BUFFER_SIZE, GX2_COMMAND_BUFFER_ALIGNMENT);
		if (nullptr == mCommandBuffer)
			return false;

		uint32 initAttributes[] =
		{
			GX2_INIT_CMD_BUF_BASE, (uint32)(uintptr_t)mCommandBuffer,
			GX2_INIT_CMD_BUF_POOL_SIZE, NATIVE_GX2_COMMAND_BUFFER_SIZE,
			GX2_INIT_ARGC, 0,
			GX2_INIT_ARGV, 0,
			GX2_INIT_END
		};
		GX2Init(initAttributes);

		const NativeTVConfig tvConfig = queryNativeTVConfig();
		uint32 tvScanBufferSize = 0;
		uint32 tvUnknown = 0;
		GX2CalcTVSize(tvConfig.renderMode, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE, &tvScanBufferSize, &tvUnknown);
		mTvScanBuffer = gx2Alloc(tvScanBufferSize, GX2_SCAN_BUFFER_ALIGNMENT);
		if (nullptr == mTvScanBuffer)
			return false;
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU, mTvScanBuffer, tvScanBufferSize);
		GX2SetTVBuffer(mTvScanBuffer, tvScanBufferSize, tvConfig.renderMode, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE);
		GX2SetTVScale(tvConfig.width, tvConfig.height);
		GX2SetTVEnable(TRUE);
		mNativeDrawableSize = Vec2i((int)tvConfig.width, (int)tvConfig.height);

		uint32 drcScanBufferSize = 0;
		uint32 drcUnknown = 0;
		GX2CalcDRCSize(GX2_DRC_RENDER_MODE_SINGLE, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE, &drcScanBufferSize, &drcUnknown);
		mDrcScanBuffer = gx2Alloc(drcScanBufferSize, GX2_SCAN_BUFFER_ALIGNMENT);
		if (nullptr != mDrcScanBuffer)
		{
			GX2Invalidate(GX2_INVALIDATE_MODE_CPU, mDrcScanBuffer, drcScanBufferSize);
			GX2SetDRCBuffer(mDrcScanBuffer, drcScanBufferSize, GX2_DRC_RENDER_MODE_SINGLE, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE);
			GX2SetDRCScale(854, 480);
			GX2SetDRCEnable(TRUE);
		}

		mNativeContext = static_cast<GX2ContextState*>(gx2Alloc(sizeof(GX2ContextState), GX2_CONTEXT_STATE_ALIGNMENT));
		if (nullptr == mNativeContext)
			return false;
		GX2SetupContextStateEx(mNativeContext, FALSE);
		GX2SetContextState(mNativeContext);
		GX2SetSwapInterval(1);
		RMX_LOG_INFO("GX2Drawer: swap interval set to 1");
		GX2SetViewport(0.0f, 0.0f, (float)mNativeDrawableSize.x, (float)mNativeDrawableSize.y, 0.0f, 1.0f);
		GX2SetScissor(0, 0, (uint32)mNativeDrawableSize.x, (uint32)mNativeDrawableSize.y);

		RMX_LOG_INFO("GX2Drawer: initialized owned GX2 display tv=" << mNativeDrawableSize.x << "x" << mNativeDrawableSize.y
			<< " tvScanBytes=" << tvScanBufferSize
			<< " drcScanBytes=" << drcScanBufferSize);
		return true;
	}

	void showStartupFailureColor(float r, float g, float b)
	{
		if (nullptr == mNativeContext)
			return;

		const Vec2i targetSize = (mNativeDrawableSize.x > 0 && mNativeDrawableSize.y > 0) ? mNativeDrawableSize : Vec2i((int)FALLBACK_TV_WIDTH, (int)FALLBACK_TV_HEIGHT);
		if (!ensureCpuColorBuffer(targetSize))
			return;

		GX2SetContextState(mNativeContext);
		GX2SetColorBuffer(&mCpuColorBuffer, GX2_RENDER_TARGET_0);
		GX2ClearColor(&mCpuColorBuffer, r, g, b, 1.0f);
		invalidateSurfaceAfterColorWrite(mCpuColorBuffer.surface);
		GX2SetTVScale(mCpuColorBuffer.surface.width, mCpuColorBuffer.surface.height);
		GX2Flush();
		GX2DrawDone();
		GX2CopyColorBufferToScanBuffer(&mCpuColorBuffer, GX2_SCAN_TARGET_TV);
		if (nullptr != mDrcScanBuffer)
		{
			GX2SetDRCScale(mCpuColorBuffer.surface.width, mCpuColorBuffer.surface.height);
			GX2CopyColorBufferToScanBuffer(&mCpuColorBuffer, GX2_SCAN_TARGET_DRC);
		}
		GX2Flush();
		GX2SwapScanBuffers();
		GX2SetTVEnable(TRUE);
		GX2SetDRCEnable(nullptr != mDrcScanBuffer);
		RMX_LOG_INFO("GX2Drawer: startup failure color shown r=" << r << " g=" << g << " b=" << b);
	}

	void destroyCpuColorBuffer()
	{
		if (nullptr != mCpuColorBuffer.surface.image)
		{
			GX2DrawDone();
			if (mCpuColorBuffer.surface.resourceFlags != 0)
				GX2RDestroySurfaceEx(&mCpuColorBuffer.surface, mCpuColorBuffer.surface.resourceFlags);
			else
				gx2Free(mCpuColorBuffer.surface.image);
			memset(&mCpuColorBuffer, 0, sizeof(mCpuColorBuffer));
			mCpuColorBufferSize.clear();
		}
	}

	bool ensureCpuColorBuffer(const Vec2i& size)
	{
		if (mCpuColorBufferSize == size && nullptr != mCpuColorBuffer.surface.image)
			return true;

		destroyCpuColorBuffer();

		memset(&mCpuColorBuffer, 0, sizeof(mCpuColorBuffer));
		mCpuColorBuffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
		mCpuColorBuffer.surface.width = (uint32)size.x;
		mCpuColorBuffer.surface.height = (uint32)size.y;
		mCpuColorBuffer.surface.depth = 1;
		mCpuColorBuffer.surface.mipLevels = 1;
		mCpuColorBuffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
		mCpuColorBuffer.surface.aa = GX2_AA_MODE1X;
		mCpuColorBuffer.surface.use = GX2_SURFACE_USE_TEXTURE_COLOR_BUFFER_TV;
		mCpuColorBuffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
		mCpuColorBuffer.surface.swizzle = 0;
		const GX2RResourceFlags flags = (GX2RResourceFlags)(
			GX2R_RESOURCE_BIND_COLOR_BUFFER |
			GX2R_RESOURCE_USAGE_CPU_WRITE |
			GX2R_RESOURCE_USAGE_GPU_READ |
			GX2R_RESOURCE_USAGE_GPU_WRITE |
			GX2R_RESOURCE_USAGE_FORCE_MEM2);
		if (!GX2RCreateSurface(&mCpuColorBuffer.surface, flags))
			return false;

		mCpuColorBuffer.viewMip = 0;
		mCpuColorBuffer.viewFirstSlice = 0;
		mCpuColorBuffer.viewNumSlices = 1;
		mCpuColorBuffer.aaBuffer = nullptr;
		mCpuColorBuffer.aaSize = 0;
		GX2InitColorBufferRegs(&mCpuColorBuffer);

		mCpuColorBufferSize = size;
		RMX_LOG_INFO("GX2Drawer: GPU-present color buffer " << size.x << " x " << size.y
			<< " pitch=" << mCpuColorBuffer.surface.pitch
			<< " bytes=" << mCpuColorBuffer.surface.imageSize);
		return true;
	}

	bool initializeShader()
	{
		if (!compileShaderPair(mShaderGroup, "present", PRESENT_VS, PRESENT_PS))
			return false;
		if (!addAttrib(mShaderGroup, "aPosition", 0, offsetof(PresentVertex, x), GX2_ATTRIB_FORMAT_FLOAT_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize position", freeCompiledShaderGroup(mShaderGroup); return false);
		}
		if (!addAttrib(mShaderGroup, "aTexCoord", 0, offsetof(PresentVertex, u), GX2_ATTRIB_FORMAT_FLOAT_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize texcoord", freeCompiledShaderGroup(mShaderGroup); return false);
		}
		if (!makeFetchShader(mShaderGroup))
		{
			RMX_ERROR("GX2Drawer: failed to initialize fetch shader", freeCompiledShaderGroup(mShaderGroup); return false);
		}
		mSamplerLocation = getSamplerLocation(mShaderGroup);
		mTintUniform = getPixelUniformSlot(mShaderGroup, "uTintColor");
		mAddedUniform = getPixelUniformSlot(mShaderGroup, "uAddedColor");
		if (mTintUniform.offset == UINT32_MAX || mAddedUniform.offset == UINT32_MAX)
		{
			uint32 blockIndex = 0;
			if (getPixelUniformBlockIndex(mShaderGroup, "PixelUniforms", blockIndex))
			{
				mTintUniform.offset = 0;
				mTintUniform.block = (int32)blockIndex;
				mAddedUniform.offset = 16;
				mAddedUniform.block = (int32)blockIndex;
			}
		}
		initializePixelUniformBlock();
		RMX_LOG_INFO("GX2Drawer: texture shader samplers="
			<< (nullptr != mShaderGroup.pixelShader ? (uint32)mShaderGroup.pixelShader->samplerVarCount : 0)
			<< " samplerLocation=" << mSamplerLocation
			<< " tintOffset=" << mTintUniform.offset
			<< " tintBlock=" << mTintUniform.block
			<< " addedOffset=" << mAddedUniform.offset
			<< " addedBlock=" << mAddedUniform.block);
		if (nullptr != mShaderGroup.pixelShader)
		{
			for (uint32 i = 0; i < mShaderGroup.pixelShader->uniformBlockCount; ++i)
			{
				const GX2UniformBlock& block = mShaderGroup.pixelShader->uniformBlocks[i];
				RMX_LOG_INFO("GX2Drawer: pixel uniform block[" << i << "] name="
					<< (nullptr != block.name ? block.name : "(null)")
					<< " location=" << block.offset
					<< " size=" << block.size);
			}
			for (uint32 i = 0; i < mShaderGroup.pixelShader->uniformVarCount; ++i)
			{
				const GX2UniformVar& uniform = mShaderGroup.pixelShader->uniformVars[i];
				RMX_LOG_INFO("GX2Drawer: pixel uniform[" << i << "] name="
					<< (nullptr != uniform.name ? uniform.name : "(null)")
					<< " offset=" << uniform.offset
					<< " block=" << uniform.block);
			}
			for (uint32 i = 0; i < mShaderGroup.pixelShader->samplerVarCount; ++i)
			{
				const GX2SamplerVar& sampler = mShaderGroup.pixelShader->samplerVars[i];
				RMX_LOG_INFO("GX2Drawer: sampler[" << i << "] name="
					<< (nullptr != sampler.name ? sampler.name : "(null)")
					<< " location=" << sampler.location);
			}
		}
		mShaderReady = true;
		return true;
	}

	bool initializeColorShader()
	{
		if (!compileShaderPair(mColorShaderGroup, "color", COLOR_VS, COLOR_PS))
			return false;
		if (!addAttrib(mColorShaderGroup, "aPosition", 0, offsetof(ColorVertex, x), GX2_ATTRIB_FORMAT_FLOAT_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize color position", freeCompiledShaderGroup(mColorShaderGroup); return false);
		}
		if (!addAttrib(mColorShaderGroup, "aColor", 0, offsetof(ColorVertex, r), GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize color attribute", freeCompiledShaderGroup(mColorShaderGroup); return false);
		}
		if (!makeFetchShader(mColorShaderGroup))
		{
			RMX_ERROR("GX2Drawer: failed to initialize color fetch shader", freeCompiledShaderGroup(mColorShaderGroup); return false);
		}
		mColorShaderReady = true;
		return true;
	}

	bool initializePlaneShader()
	{
		if (!compileShaderPair(mPlaneShaderGroup, "plane", PLANE_VS, PLANE_PS))
			return false;
		if (!addAttrib(mPlaneShaderGroup, "aPosition", 0, offsetof(PlaneVertex, x), GX2_ATTRIB_FORMAT_FLOAT_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize plane position", freeCompiledShaderGroup(mPlaneShaderGroup); return false);
		}
		if (!addAttrib(mPlaneShaderGroup, "aLocalOffset", 0, offsetof(PlaneVertex, localX), GX2_ATTRIB_FORMAT_FLOAT_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize plane local offset", freeCompiledShaderGroup(mPlaneShaderGroup); return false);
		}
		if (!makeFetchShader(mPlaneShaderGroup))
		{
			RMX_ERROR("GX2Drawer: failed to initialize plane fetch shader", freeCompiledShaderGroup(mPlaneShaderGroup); return false);
		}

		mPlaneSamplerLocations[0] = getSamplerLocation(mPlaneShaderGroup, "uPlaneDataTexture");
		if (mPlaneSamplerLocations[0] == UINT32_MAX && nullptr != mPlaneShaderGroup.pixelShader && mPlaneShaderGroup.pixelShader->samplerVarCount >= 1)
		{
			mPlaneSamplerLocations[0] = mPlaneShaderGroup.pixelShader->samplerVars[0].location;
			RMX_LOG_INFO("GX2Drawer: plane sampler name unavailable; using declaration-order location");
		}
		RMX_LOG_INFO("GX2Drawer: plane sampler location data=" << mPlaneSamplerLocations[0]);
		mPlaneConfigUniforms[0] = getPixelUniformSlot(mPlaneShaderGroup, "uConfig0");
		mPlaneConfigUniforms[1] = getPixelUniformSlot(mPlaneShaderGroup, "uConfig1");
		mPlaneConfigUniforms[2] = getPixelUniformSlot(mPlaneShaderGroup, "uConfig2");
		mPlaneConfigUniforms[3] = getPixelUniformSlot(mPlaneShaderGroup, "uConfig3");
		bool missingNamedUniform = false;
		for (const PixelUniformSlot& uniform : mPlaneConfigUniforms)
		{
			missingNamedUniform = missingNamedUniform || uniform.offset == UINT32_MAX;
		}
		if (missingNamedUniform && nullptr != mPlaneShaderGroup.pixelShader && mPlaneShaderGroup.pixelShader->uniformVarCount >= 4)
		{
			for (uint32 index = 0; index < 4; ++index)
			{
				const GX2UniformVar& uniform = mPlaneShaderGroup.pixelShader->uniformVars[index];
				mPlaneConfigUniforms[index].offset = uniform.offset;
				mPlaneConfigUniforms[index].block = uniform.block;
			}
			RMX_LOG_INFO("GX2Drawer: plane uniform names unavailable; using declaration-order locations");
		}
		if (nullptr != mPlaneShaderGroup.pixelShader)
		{
			for (uint32 i = 0; i < mPlaneShaderGroup.pixelShader->uniformVarCount; ++i)
			{
				const GX2UniformVar& uniform = mPlaneShaderGroup.pixelShader->uniformVars[i];
				RMX_LOG_INFO("GX2Drawer: plane uniform[" << i << "] name="
					<< (nullptr != uniform.name ? uniform.name : "(null)")
					<< " offset=" << uniform.offset
					<< " block=" << uniform.block);
			}
			for (uint32 i = 0; i < mPlaneShaderGroup.pixelShader->samplerVarCount; ++i)
			{
				const GX2SamplerVar& sampler = mPlaneShaderGroup.pixelShader->samplerVars[i];
				RMX_LOG_INFO("GX2Drawer: plane sampler[" << i << "] name="
					<< (nullptr != sampler.name ? sampler.name : "(null)")
					<< " location=" << sampler.location);
			}
		}
		RMX_LOG_INFO("GX2Drawer: plane uniform locations config0=" << mPlaneConfigUniforms[0].offset
			<< " block0=" << mPlaneConfigUniforms[0].block
			<< " config1=" << mPlaneConfigUniforms[1].offset
			<< " block1=" << mPlaneConfigUniforms[1].block
			<< " config2=" << mPlaneConfigUniforms[2].offset
			<< " block2=" << mPlaneConfigUniforms[2].block
			<< " config3=" << mPlaneConfigUniforms[3].offset
			<< " block3=" << mPlaneConfigUniforms[3].block);
		mPlaneShaderReady = true;
		RMX_LOG_INFO("GX2Drawer: plane shader ready");
		return true;
	}

	bool initializeVdpSpriteShader()
	{
		if (!compileShaderPair(mVdpSpriteShaderGroup, "vdp_sprite", PLANE_VS, VDP_SPRITE_PS))
			return false;
		if (!addAttrib(mVdpSpriteShaderGroup, "aPosition", 0, offsetof(PlaneVertex, x), GX2_ATTRIB_FORMAT_FLOAT_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize VDP sprite position", freeCompiledShaderGroup(mVdpSpriteShaderGroup); return false);
		}
		if (!addAttrib(mVdpSpriteShaderGroup, "aLocalOffset", 0, offsetof(PlaneVertex, localX), GX2_ATTRIB_FORMAT_FLOAT_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize VDP sprite local offset", freeCompiledShaderGroup(mVdpSpriteShaderGroup); return false);
		}
		if (!makeFetchShader(mVdpSpriteShaderGroup))
		{
			RMX_ERROR("GX2Drawer: failed to initialize VDP sprite fetch shader", freeCompiledShaderGroup(mVdpSpriteShaderGroup); return false);
		}

		mVdpSpriteSamplerLocations[0] = getSamplerLocation(mVdpSpriteShaderGroup, "uPlaneDataTexture");
		if (mVdpSpriteSamplerLocations[0] == UINT32_MAX && nullptr != mVdpSpriteShaderGroup.pixelShader && mVdpSpriteShaderGroup.pixelShader->samplerVarCount >= 1)
		{
			mVdpSpriteSamplerLocations[0] = mVdpSpriteShaderGroup.pixelShader->samplerVars[0].location;
			RMX_LOG_INFO("GX2Drawer: VDP sprite sampler name unavailable; using declaration-order location");
		}
		mVdpSpriteUniforms[0] = getPixelUniformSlot(mVdpSpriteShaderGroup, "uConfig0");
		mVdpSpriteUniforms[1] = getPixelUniformSlot(mVdpSpriteShaderGroup, "uConfig1");
		mVdpSpriteUniforms[2] = getPixelUniformSlot(mVdpSpriteShaderGroup, "uTintColor");
		mVdpSpriteUniforms[3] = getPixelUniformSlot(mVdpSpriteShaderGroup, "uAddedColor");
		mVdpSpriteShaderReady = true;
		RMX_LOG_INFO("GX2Drawer: VDP sprite shader ready sampler=" << mVdpSpriteSamplerLocations[0]
			<< " config0=" << mVdpSpriteUniforms[0].offset
			<< " config1=" << mVdpSpriteUniforms[1].offset
			<< " tint=" << mVdpSpriteUniforms[2].offset
			<< " added=" << mVdpSpriteUniforms[3].offset);
		return true;
	}

	bool initializeVdpSpriteBatchShader()
	{
		if (!compileShaderPair(mVdpSpriteBatchShaderGroup, "vdp_sprite_batch", VDP_SPRITE_BATCH_VS, VDP_SPRITE_BATCH_PS))
			return false;
		if (!addAttrib(mVdpSpriteBatchShaderGroup, "aPosition", 0, offsetof(VdpSpriteBatchVertex, x), GX2_ATTRIB_FORMAT_FLOAT_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize VDP batch position", freeCompiledShaderGroup(mVdpSpriteBatchShaderGroup); return false);
		}
		if (!addAttrib(mVdpSpriteBatchShaderGroup, "aLocalOffset", 0, offsetof(VdpSpriteBatchVertex, localX), GX2_ATTRIB_FORMAT_FLOAT_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize VDP batch local offset", freeCompiledShaderGroup(mVdpSpriteBatchShaderGroup); return false);
		}
		if (!addAttrib(mVdpSpriteBatchShaderGroup, "aConfig0", 0, offsetof(VdpSpriteBatchVertex, spriteX), GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize VDP batch config0", freeCompiledShaderGroup(mVdpSpriteBatchShaderGroup); return false);
		}
		if (!addAttrib(mVdpSpriteBatchShaderGroup, "aConfig1", 0, offsetof(VdpSpriteBatchVertex, firstPattern), GX2_ATTRIB_FORMAT_FLOAT_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize VDP batch config1", freeCompiledShaderGroup(mVdpSpriteBatchShaderGroup); return false);
		}
		if (!addAttrib(mVdpSpriteBatchShaderGroup, "aTintColor", 0, offsetof(VdpSpriteBatchVertex, tintR), GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize VDP batch tint", freeCompiledShaderGroup(mVdpSpriteBatchShaderGroup); return false);
		}
		if (!addAttrib(mVdpSpriteBatchShaderGroup, "aAddedColor", 0, offsetof(VdpSpriteBatchVertex, addedR), GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize VDP batch added", freeCompiledShaderGroup(mVdpSpriteBatchShaderGroup); return false);
		}
		if (!makeFetchShader(mVdpSpriteBatchShaderGroup))
		{
			RMX_ERROR("GX2Drawer: failed to initialize VDP batch fetch shader", freeCompiledShaderGroup(mVdpSpriteBatchShaderGroup); return false);
		}

		mVdpSpriteBatchSamplerLocations[0] = getSamplerLocation(mVdpSpriteBatchShaderGroup, "uPlaneDataTexture");
		if (mVdpSpriteBatchSamplerLocations[0] == UINT32_MAX && nullptr != mVdpSpriteBatchShaderGroup.pixelShader && mVdpSpriteBatchShaderGroup.pixelShader->samplerVarCount >= 1)
		{
			mVdpSpriteBatchSamplerLocations[0] = mVdpSpriteBatchShaderGroup.pixelShader->samplerVars[0].location;
			RMX_LOG_INFO("GX2Drawer: VDP batch sampler name unavailable; using declaration-order location");
		}
		mVdpSpriteBatchShaderReady = true;
		RMX_LOG_INFO("GX2Drawer: VDP sprite batch shader ready sampler=" << mVdpSpriteBatchSamplerLocations[0]);
		return true;
	}

	bool initializePaletteSpriteShader()
	{
		if (!compileShaderPair(mPaletteSpriteShaderGroup, "palette_sprite", PLANE_VS, PALETTE_SPRITE_PS))
			return false;
		if (!addAttrib(mPaletteSpriteShaderGroup, "aPosition", 0, offsetof(PlaneVertex, x), GX2_ATTRIB_FORMAT_FLOAT_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize palette sprite position", freeCompiledShaderGroup(mPaletteSpriteShaderGroup); return false);
		}
		if (!addAttrib(mPaletteSpriteShaderGroup, "aLocalOffset", 0, offsetof(PlaneVertex, localX), GX2_ATTRIB_FORMAT_FLOAT_32_32))
		{
			RMX_ERROR("GX2Drawer: failed to initialize palette sprite local offset", freeCompiledShaderGroup(mPaletteSpriteShaderGroup); return false);
		}
		if (!makeFetchShader(mPaletteSpriteShaderGroup))
		{
			RMX_ERROR("GX2Drawer: failed to initialize palette sprite fetch shader", freeCompiledShaderGroup(mPaletteSpriteShaderGroup); return false);
		}

		mPaletteSpriteSamplerLocations[0] = getSamplerLocation(mPaletteSpriteShaderGroup, "uSpriteDataTexture");
		if (mPaletteSpriteSamplerLocations[0] == UINT32_MAX
			&& nullptr != mPaletteSpriteShaderGroup.pixelShader && mPaletteSpriteShaderGroup.pixelShader->samplerVarCount >= 1)
		{
			mPaletteSpriteSamplerLocations[0] = mPaletteSpriteShaderGroup.pixelShader->samplerVars[0].location;
			RMX_LOG_INFO("GX2Drawer: palette sprite sampler names unavailable; using declaration-order locations");
		}
		mPaletteSpriteUniforms[0] = getPixelUniformSlot(mPaletteSpriteShaderGroup, "uConfig0");
		mPaletteSpriteUniforms[1] = getPixelUniformSlot(mPaletteSpriteShaderGroup, "uConfig1");
		mPaletteSpriteUniforms[2] = getPixelUniformSlot(mPaletteSpriteShaderGroup, "uTintColor");
		mPaletteSpriteUniforms[3] = getPixelUniformSlot(mPaletteSpriteShaderGroup, "uAddedColor");
		mPaletteSpriteShaderReady = true;
		RMX_LOG_INFO("GX2Drawer: palette sprite shader ready dataSampler=" << mPaletteSpriteSamplerLocations[0]
			<< " config0=" << mPaletteSpriteUniforms[0].offset
			<< " config1=" << mPaletteSpriteUniforms[1].offset
			<< " tint=" << mPaletteSpriteUniforms[2].offset
			<< " added=" << mPaletteSpriteUniforms[3].offset);
		return true;
	}

	void initializePixelUniformBlock()
	{
		if (nullptr == mShaderGroup.pixelShader)
			return;

		int32 blockIndex = -1;
		if (mTintUniform.block >= 0)
			blockIndex = mTintUniform.block;
		else if (mAddedUniform.block >= 0)
			blockIndex = mAddedUniform.block;
		if (blockIndex < 0 || blockIndex >= (int32)mShaderGroup.pixelShader->uniformBlockCount)
			return;

		const GX2UniformBlock& block = mShaderGroup.pixelShader->uniformBlocks[blockIndex];
		mPixelUniformBlockLocation = block.offset;
		mPixelUniformBlockSize = std::max<uint32>(block.size, 0x20);
		mPixelUniformBlock = static_cast<uint8*>(gx2Alloc(mPixelUniformBlockSize, GX2_UNIFORM_BLOCK_ALIGNMENT));
		if (nullptr != mPixelUniformBlock)
		{
			memset(mPixelUniformBlock, 0, mPixelUniformBlockSize);
			RMX_LOG_INFO("GX2Drawer: pixel uniform block location=" << mPixelUniformBlockLocation
				<< " size=" << mPixelUniformBlockSize
				<< " tintBlock=" << mTintUniform.block
				<< " tintOffset=" << mTintUniform.offset
				<< " addedBlock=" << mAddedUniform.block
				<< " addedOffset=" << mAddedUniform.offset);
		}
	}

	void initializeSampler()
	{
		for (int sampling = 0; sampling < 2; ++sampling)
		{
			for (int wrap = 0; wrap < 2; ++wrap)
			{
				const int index = sampling * 2 + wrap;
				const GX2TexClampMode clampMode = (wrap == 0) ? GX2_TEX_CLAMP_MODE_CLAMP : GX2_TEX_CLAMP_MODE_WRAP;
				const GX2TexXYFilterMode filterMode = (sampling == 0) ? GX2_TEX_XY_FILTER_MODE_POINT : GX2_TEX_XY_FILTER_MODE_LINEAR;
				GX2InitSampler(&mSamplers[index], clampMode, filterMode);
				GX2InitSamplerZMFilter(&mSamplers[index], GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_NONE);
			}
		}
	}

	void initializeVertexBuffer()
	{
		mVertexBuffer = static_cast<PresentVertex*>(gx2Alloc(sizeof(PresentVertex) * PRESENT_VERTEX_COUNT, 0x40));
		if (nullptr == mVertexBuffer)
		{
			mSetupSuccessful = false;
			return;
		}

		const PresentVertex vertices[PRESENT_VERTEX_COUNT] =
		{
			makePresentVertex(-1.0f, -1.0f, 0.0f, 1.0f),
			makePresentVertex( 1.0f, -1.0f, 1.0f, 1.0f),
			makePresentVertex(-1.0f,  1.0f, 0.0f, 0.0f),
			makePresentVertex(-1.0f,  1.0f, 0.0f, 0.0f),
			makePresentVertex( 1.0f, -1.0f, 1.0f, 1.0f),
			makePresentVertex( 1.0f,  1.0f, 1.0f, 0.0f),
		};
		memcpy(mVertexBuffer, vertices, sizeof(vertices));
		DCFlushRange(mVertexBuffer, sizeof(vertices));
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, mVertexBuffer, sizeof(vertices));
	}

	GX2Sampler& getSampler(SamplingMode samplingMode, TextureWrapMode wrapMode)
	{
		const int sampling = (samplingMode == SamplingMode::BILINEAR) ? 1 : 0;
		const int wrap = (wrapMode == TextureWrapMode::REPEAT) ? 1 : 0;
		return mSamplers[sampling * 2 + wrap];
	}

	bool ensureDynamicVertexBuffer(size_t requiredBytes)
	{
		if (requiredBytes <= mDynamicVertexCapacity && nullptr != mDynamicVertexBuffer)
			return true;

		if (nullptr != mDynamicVertexBuffer)
		{
			GX2DrawDone();
			gx2Free(mDynamicVertexBuffer);
			mDynamicVertexBuffer = nullptr;
			mDynamicVertexCapacity = 0;
			mDynamicVertexWriteOffset = 0;
		}

		mDynamicVertexCapacity = std::max<size_t>(requiredBytes, 1024 * 1024);
		mDynamicVertexBuffer = static_cast<uint8*>(gx2Alloc((uint32)mDynamicVertexCapacity, 0x40));
		return nullptr != mDynamicVertexBuffer;
	}

	template<typename VertexType>
	VertexType* allocateDynamicVertices(uint32 count)
	{
		const size_t bytes = (size_t)count * sizeof(VertexType);
		size_t alignedOffset = (mDynamicVertexWriteOffset + 0x3f) & ~(size_t)0x3f;
		if (!ensureDynamicVertexBuffer(alignedOffset + bytes))
			return nullptr;
		if (alignedOffset + bytes > mDynamicVertexCapacity)
		{
			GX2DrawDone();
			alignedOffset = 0;
		}
		VertexType* vertices = reinterpret_cast<VertexType*>(mDynamicVertexBuffer + alignedOffset);
		mDynamicVertexWriteOffset = alignedOffset + bytes;
		return vertices;
	}

	PresentVertex* allocateVertices(uint32 count)
	{
		return allocateDynamicVertices<PresentVertex>(count);
	}

	ColorVertex* allocateColorVertices(uint32 count)
	{
		return allocateDynamicVertices<ColorVertex>(count);
	}

	PlaneVertex* allocatePlaneVertices(uint32 count)
	{
		return allocateDynamicVertices<PlaneVertex>(count);
	}

	float toClipX(float x) const
	{
		return mPixelToViewSpaceTransform.x + x * mPixelToViewSpaceTransform.z;
	}

	float toClipY(float y) const
	{
		return mPixelToViewSpaceTransform.y + y * mPixelToViewSpaceTransform.w;
	}

	void fillRectVertices(PresentVertex* vertices, const Recti& rect, const Vec2f& uv0, const Vec2f& uv1) const
	{
		const float x0 = toClipX((float)rect.x);
		const float x1 = toClipX((float)(rect.x + rect.width));
		const float y0 = toClipY((float)rect.y);
		const float y1 = toClipY((float)(rect.y + rect.height));

		vertices[0] = makePresentVertex(x0, y0, uv0.x, uv0.y);
		vertices[1] = makePresentVertex(x0, y1, uv0.x, uv1.y);
		vertices[2] = makePresentVertex(x1, y1, uv1.x, uv1.y);
		vertices[3] = makePresentVertex(x1, y1, uv1.x, uv1.y);
		vertices[4] = makePresentVertex(x1, y0, uv1.x, uv0.y);
		vertices[5] = makePresentVertex(x0, y0, uv0.x, uv0.y);
	}

	void fillColorRectVertices(ColorVertex* vertices, const Recti& rect, const Color& color) const
	{
		const float x0 = toClipX((float)rect.x);
		const float x1 = toClipX((float)(rect.x + rect.width));
		const float y0 = toClipY((float)rect.y);
		const float y1 = toClipY((float)(rect.y + rect.height));
		const ColorVertex v0 = { x0, y0, color.r, color.g, color.b, color.a };
		const ColorVertex v1 = { x0, y1, color.r, color.g, color.b, color.a };
		const ColorVertex v2 = { x1, y1, color.r, color.g, color.b, color.a };
		const ColorVertex v3 = { x1, y0, color.r, color.g, color.b, color.a };
		vertices[0] = v0;
		vertices[1] = v1;
		vertices[2] = v2;
		vertices[3] = v2;
		vertices[4] = v3;
		vertices[5] = v0;
	}

	void fillPlaneVertices(PlaneVertex* vertices, const Recti& rect) const
	{
		const float x0 = toClipX((float)rect.x);
		const float x1 = toClipX((float)(rect.x + rect.width));
		const float y0 = toClipY((float)rect.y);
		const float y1 = toClipY((float)(rect.y + rect.height));
		const float lx0 = (float)rect.x;
		const float lx1 = (float)(rect.x + rect.width);
		const float ly0 = (float)rect.y;
		const float ly1 = (float)(rect.y + rect.height);

		vertices[0] = { x0, y0, lx0, ly0 };
		vertices[1] = { x0, y1, lx0, ly1 };
		vertices[2] = { x1, y1, lx1, ly1 };
		vertices[3] = { x1, y1, lx1, ly1 };
		vertices[4] = { x1, y0, lx1, ly0 };
		vertices[5] = { x0, y0, lx0, ly0 };
	}

	void setupViewport(const Recti& viewport, bool windowTarget)
	{
		(void)windowTarget;
		mCurrentViewport = viewport;
		mPixelToViewSpaceTransform.x = -1.0f;
		mPixelToViewSpaceTransform.y = 1.0f;
		mPixelToViewSpaceTransform.z = 2.0f / (float)std::max(1, viewport.width);
		mPixelToViewSpaceTransform.w = -2.0f / (float)std::max(1, viewport.height);
	}

	void applyScissor()
	{
		if (nullptr == mCurrentColorBuffer)
			return;

		Recti scissorRect = mCurrentViewport;
		if (!mScissorStack.empty())
		{
			scissorRect.intersect(mScissorStack.back());
		}
		mInvalidScissorRegion = scissorRect.empty();
		if (mInvalidScissorRegion)
		{
			GX2SetScissor(0, 0, 0, 0);
			return;
		}

		const float scaleX = (float)mCurrentColorBuffer->surface.width / (float)std::max(1, mCurrentViewport.width);
		const float scaleY = (float)mCurrentColorBuffer->surface.height / (float)std::max(1, mCurrentViewport.height);
		const int x0 = roundToInt((float)(scissorRect.x - mCurrentViewport.x) * scaleX);
		const int y0 = roundToInt((float)(scissorRect.y - mCurrentViewport.y) * scaleY);
		const int x1 = roundToInt((float)(scissorRect.x + scissorRect.width - mCurrentViewport.x) * scaleX);
		const int y1 = roundToInt((float)(scissorRect.y + scissorRect.height - mCurrentViewport.y) * scaleY);
		const int targetWidth = (int)mCurrentColorBuffer->surface.width;
		const int targetHeight = (int)mCurrentColorBuffer->surface.height;
		const int clippedX0 = clamp(x0, 0, targetWidth);
		const int clippedY0 = clamp(y0, 0, targetHeight);
		const int clippedX1 = clamp(x1, clippedX0, targetWidth);
		const int clippedY1 = clamp(y1, clippedY0, targetHeight);
		GX2SetScissor((uint32)clippedX0, (uint32)clippedY0, (uint32)(clippedX1 - clippedX0), (uint32)(clippedY1 - clippedY0));
	}

	void applyFullTargetScissor(uint32 width, uint32 height)
	{
		mInvalidScissorRegion = false;
		GX2SetScissor(0, 0, width, height);
	}

	void applyBlendMode()
	{
		switch (mCurrentBlendMode)
		{
			case BlendMode::OPAQUE:
			{
				GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);
				GX2SetColorControl(GX2_LOGIC_OP_COPY, GX2_BLEND_MASK_ALL_TARGETS, FALSE, TRUE);
				GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD, GX2_DISABLE,
					GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD);
				break;
			}
			case BlendMode::ADDITIVE:
			{
				GX2SetAlphaTest(TRUE, GX2_COMPARE_FUNC_GREATER, 0.0f);
				GX2SetColorControl(GX2_LOGIC_OP_COPY, GX2_BLEND_MASK_ALL_TARGETS, FALSE, TRUE);
				GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_ONE, GX2_BLEND_COMBINE_MODE_ADD, TRUE,
					GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD);
				break;
			}
			case BlendMode::SUBTRACTIVE:
			{
				GX2SetAlphaTest(TRUE, GX2_COMPARE_FUNC_GREATER, 0.0f);
				GX2SetColorControl(GX2_LOGIC_OP_COPY, GX2_BLEND_MASK_ALL_TARGETS, FALSE, TRUE);
				GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_ONE, GX2_BLEND_COMBINE_MODE_REV_SUB, TRUE,
					GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD);
				break;
			}
			case BlendMode::MINIMUM:
			{
				GX2SetAlphaTest(TRUE, GX2_COMPARE_FUNC_GREATER, 0.0f);
				GX2SetColorControl(GX2_LOGIC_OP_COPY, GX2_BLEND_MASK_ALL_TARGETS, FALSE, TRUE);
				GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ONE, GX2_BLEND_COMBINE_MODE_MIN, TRUE,
					GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ONE, GX2_BLEND_COMBINE_MODE_MIN);
				break;
			}
			case BlendMode::MAXIMUM:
			{
				GX2SetAlphaTest(TRUE, GX2_COMPARE_FUNC_GREATER, 0.0f);
				GX2SetColorControl(GX2_LOGIC_OP_COPY, GX2_BLEND_MASK_ALL_TARGETS, FALSE, TRUE);
				GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ONE, GX2_BLEND_COMBINE_MODE_MAX, TRUE,
					GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ONE, GX2_BLEND_COMBINE_MODE_MAX);
				break;
			}
			case BlendMode::MULTIPLICATIVE:
			{
				GX2SetAlphaTest(TRUE, GX2_COMPARE_FUNC_GREATER, 0.0f);
				GX2SetColorControl(GX2_LOGIC_OP_COPY, GX2_BLEND_MASK_ALL_TARGETS, FALSE, TRUE);
				GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_DST_COLOR, GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD, TRUE,
					GX2_BLEND_MODE_DST_ALPHA, GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD);
				break;
			}
			case BlendMode::ONE_BIT:
			{
				GX2SetAlphaTest(TRUE, GX2_COMPARE_FUNC_GREATER, 0.0f);
				GX2SetColorControl(GX2_LOGIC_OP_COPY, GX2_BLEND_MASK_NONE, FALSE, TRUE);
				GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD, GX2_DISABLE,
					GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD);
				break;
			}
			case BlendMode::ALPHA:
			default:
			{
				GX2SetAlphaTest(TRUE, GX2_COMPARE_FUNC_GREATER, 0.0f);
				GX2SetColorControl(GX2_LOGIC_OP_COPY, GX2_BLEND_MASK_ALL_TARGETS, FALSE, TRUE);
				GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD, TRUE,
					GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD);
				break;
			}
		}
	}

	bool bindColorBuffer(GX2ColorBuffer& colorBuffer, const Recti& viewport, bool windowTarget)
	{
		if (nullptr == colorBuffer.surface.image)
			return false;
		if (nullptr != mNativeContext)
		{
			GX2SetContextState(mNativeContext);
		}
		mCurrentColorBuffer = &colorBuffer;
		GX2SetColorBuffer(&colorBuffer, GX2_RENDER_TARGET_0);
		GX2SetViewport(0.0f, 0.0f, (float)colorBuffer.surface.width, (float)colorBuffer.surface.height, 0.0f, 1.0f);
		setupViewport(viewport, windowTarget);
		mScissorStack.clear();
		applyScissor();
		applyBlendMode();
		return true;
	}

	bool beginFrame()
	{
		if (!mSetupSuccessful)
			return false;
		if (mFrameActive)
			return true;

		mFrameActive = true;
		mDynamicVertexWriteOffset = 0;
		mDynamicUniformWriteOffset = 0;
		mCurrentBlendMode = BlendMode::OPAQUE;
		mCurrentSamplingMode = SamplingMode::POINT;
		mCurrentWrapMode = TextureWrapMode::CLAMP;
		mScissorStack.clear();
		mInvalidScissorRegion = false;
		mWindowTargetBound = false;
		mWindowTargetCleared = false;
		mCurrentColorBuffer = nullptr;
		mScratchUploadPending = false;
		return true;
	}

	void finishFrame()
	{
		mFrameActive = false;
		mCurrentColorBuffer = nullptr;
		mScissorStack.clear();
		mInvalidScissorRegion = false;
		mWindowTargetBound = false;
		mWindowTargetCleared = false;
		mScratchUploadPending = false;
	}

	bool bindWindowTarget(const Recti& viewport)
	{
		const Vec2i size = mNativeDrawableSize;
		if (!ensureCpuColorBuffer(size))
			return false;
		if (!bindColorBuffer(mCpuColorBuffer, viewport, true))
			return false;
		if (!mWindowTargetCleared)
		{
			GX2ClearColor(&mCpuColorBuffer, 0.0f, 0.0f, 0.0f, 1.0f);
			mWindowTargetCleared = true;
		}
		mWindowTargetBound = true;
		if (FORCE_COLOR_SHADER_TEST_RECT)
		{
			drawColoredRect(Recti(16, 16, 96, 64), Color(1.0f, 0.0f, 0.0f, 1.0f));
		}
		return true;
	}

	bool bindRenderTarget(DrawerTexture& texture, const Recti& viewport)
	{
		GX2DrawerTextureImplementation* implementation = texture.getImplementation<GX2DrawerTextureImplementation>();
		if (nullptr == implementation)
			return false;
		if (!implementation->mRenderTarget)
		{
			implementation->setupAsRenderTarget(texture.getSize());
		}
		if (nullptr == implementation->mColorBuffer.surface.image)
			return false;
		return bindColorBuffer(implementation->mColorBuffer, viewport, false);
	}

	bool mayRenderAnything() const
	{
		return nullptr != mCurrentColorBuffer && !mInvalidScissorRegion;
	}

	void writeUniformBlockColorBytes(uint8* uniformBlock, uint32 uniformBlockSize, const PixelUniformSlot& slot, const Color& color)
	{
		if (nullptr == uniformBlock || slot.block < 0 || slot.offset + 16 > uniformBlockSize)
			return;

		const float data[4] = { color.r, color.g, color.b, color.a };
		uint8* dst = uniformBlock + slot.offset;
		for (int i = 0; i < 4; ++i)
		{
			uint32 bits;
			memcpy(&bits, &data[i], sizeof(bits));
			bits = toGpuLittleEndian32(bits);
			memcpy(dst + i * sizeof(uint32), &bits, sizeof(bits));
		}
	}

	void setPixelUniformColor(const PixelUniformSlot& slot, const Color& color)
	{
		if (slot.offset == UINT32_MAX)
			return;
		if (slot.block >= 0)
			return;
		const float data[4] = { color.r, color.g, color.b, color.a };
		GX2SetPixelUniformReg(slot.offset, 4, data);
	}

	void setPixelUniformVec4(const PixelUniformSlot& slot, const Vec4f& value)
	{
		if (slot.offset == UINT32_MAX || slot.block >= 0)
			return;
		const float data[4] = { value.x, value.y, value.z, value.w };
		GX2SetPixelUniformReg(slot.offset, 4, data);
	}

	bool ensureDynamicUniformBuffer(size_t requiredBytes)
	{
		if (requiredBytes <= mDynamicUniformCapacity && nullptr != mDynamicUniformBuffer)
			return true;

		if (nullptr != mDynamicUniformBuffer)
		{
			GX2DrawDone();
			gx2Free(mDynamicUniformBuffer);
			mDynamicUniformBuffer = nullptr;
			mDynamicUniformCapacity = 0;
			mDynamicUniformWriteOffset = 0;
		}

		mDynamicUniformCapacity = std::max<size_t>(requiredBytes, NATIVE_GX2_UNIFORM_BUFFER_MIN_SIZE);
		mDynamicUniformBuffer = static_cast<uint8*>(gx2Alloc((uint32)mDynamicUniformCapacity, GX2_UNIFORM_BLOCK_ALIGNMENT));
		return nullptr != mDynamicUniformBuffer;
	}

	uint8* allocateUniformBlock(uint32 size)
	{
		if (size == 0)
			return nullptr;

		size_t alignedOffset = (mDynamicUniformWriteOffset + (GX2_UNIFORM_BLOCK_ALIGNMENT - 1)) & ~(size_t)(GX2_UNIFORM_BLOCK_ALIGNMENT - 1);
		if (!ensureDynamicUniformBuffer(alignedOffset + size))
			return nullptr;
		if (alignedOffset + size > mDynamicUniformCapacity)
		{
			GX2DrawDone();
			alignedOffset = 0;
		}
		uint8* uniformBlock = mDynamicUniformBuffer + alignedOffset;
		mDynamicUniformWriteOffset = alignedOffset + size;
		return uniformBlock;
	}

	void setPixelUniformColors(const Color& tintColor, const Color& addedColor)
	{
		if (mPixelUniformBlockLocation != UINT32_MAX && mPixelUniformBlockSize > 0 && (mTintUniform.block >= 0 || mAddedUniform.block >= 0))
		{
			uint8* uniformBlock = allocateUniformBlock(mPixelUniformBlockSize);
			if (nullptr == uniformBlock)
				return;
			memset(uniformBlock, 0, mPixelUniformBlockSize);
			writeUniformBlockColorBytes(uniformBlock, mPixelUniformBlockSize, mTintUniform, tintColor);
			writeUniformBlockColorBytes(uniformBlock, mPixelUniformBlockSize, mAddedUniform, addedColor);
			GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_CPU | GX2_INVALIDATE_MODE_UNIFORM_BLOCK), uniformBlock, mPixelUniformBlockSize);
			GX2SetPixelUniformBlock(mPixelUniformBlockLocation, mPixelUniformBlockSize, uniformBlock);
			return;
		}

		setPixelUniformColor(mTintUniform, tintColor);
		setPixelUniformColor(mAddedUniform, addedColor);
	}

	void setVertexUniformTransform(const PixelUniformSlot& slot, const Vec4f& transform)
	{
		if (slot.offset == UINT32_MAX)
			return;
		if (slot.block >= 0)
			return;
		const float data[4] = { transform.x, transform.y, transform.z, transform.w };
		GX2SetVertexUniformReg(slot.offset, 4, data);
	}

	bool submitTextureVertices(GX2Texture& texture, PresentVertex* gpuVertices, uint32 vertexCount, SamplingMode samplingMode, TextureWrapMode wrapMode, const Color& tintColor = Color::WHITE, const Color& addedColor = Color::TRANSPARENT)
	{
		if (!mayRenderAnything() || !mShaderReady || nullptr == texture.surface.image || nullptr == gpuVertices || vertexCount == 0)
			return false;

		const uint32 vertexDataSize = (uint32)((size_t)vertexCount * sizeof(PresentVertex));
		DCFlushRange(gpuVertices, vertexDataSize);
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, gpuVertices, vertexDataSize);

		bindState(mCurrentColorBuffer->surface.width, mCurrentColorBuffer->surface.height, texture, getSampler(samplingMode, wrapMode));
		setPixelUniformColors(tintColor, addedColor);
		applyBlendMode();
		applyScissor();
		GX2SetAttribBuffer(0, vertexDataSize, sizeof(PresentVertex), gpuVertices);
		GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, vertexCount, 0, 1);
		return true;
	}

	bool shouldUseAlphaBlendForTexture(bool textureContainsTransparentPixels, const Color& tintColor, const Color& addedColor) const
	{
		return textureContainsTransparentPixels || tintColor.a < 0.999f || addedColor.a > 0.001f;
	}

	template<typename DrawFunc>
	bool drawWithTextureAlphaMode(bool useAlphaBlend, DrawFunc&& drawFunc)
	{
		if (!useAlphaBlend || mCurrentBlendMode != BlendMode::OPAQUE)
			return drawFunc();

		const BlendMode previousBlendMode = mCurrentBlendMode;
		mCurrentBlendMode = BlendMode::ALPHA;
		const bool result = drawFunc();
		mCurrentBlendMode = previousBlendMode;
		return result;
	}

	bool drawTextureVertices(GX2Texture& texture, const PresentVertex* cpuVertices, uint32 vertexCount, SamplingMode samplingMode, TextureWrapMode wrapMode, const Color& tintColor = Color::WHITE, const Color& addedColor = Color::TRANSPARENT)
	{
		if (nullptr == cpuVertices || vertexCount == 0)
			return false;

		PresentVertex* gpuVertices = allocateVertices(vertexCount);
		if (nullptr == gpuVertices)
			return false;

		memcpy(gpuVertices, cpuVertices, (size_t)vertexCount * sizeof(PresentVertex));
		return submitTextureVertices(texture, gpuVertices, vertexCount, samplingMode, wrapMode, tintColor, addedColor);
	}

	bool drawTexturedRect(const Recti& rect, GX2Texture& texture, const Vec2f& uv0, const Vec2f& uv1, SamplingMode samplingMode, TextureWrapMode wrapMode, const Color& tintColor = Color::WHITE, const Color& addedColor = Color::TRANSPARENT)
	{
		PresentVertex vertices[PRESENT_VERTEX_COUNT];
		fillRectVertices(vertices, rect, uv0, uv1);
		return drawTextureVertices(texture, vertices, PRESENT_VERTEX_COUNT, samplingMode, wrapMode, tintColor, addedColor);
	}

	bool uploadScratchBitmap(const Bitmap& bitmap)
	{
		if (bitmap.empty())
			return false;
		if (mScratchUploadPending)
		{
			GX2DrawDone();
			mScratchUploadPending = false;
		}
		if (mScratchTextureSize != bitmap.getSize() || nullptr == mScratchTexture.surface.image)
		{
			destroyTextureStorage(mScratchTexture, true);
			if (!initializeTextureStorage(mScratchTexture, bitmap.getSize()))
				return false;
			mScratchTextureSize = bitmap.getSize();
		}
		uploadBitmapToTexture(mScratchTexture, bitmap);
		return true;
	}

	bool drawScratchBitmap(const Recti& rect, const Bitmap& bitmap, SamplingMode samplingMode = SamplingMode::POINT, TextureWrapMode wrapMode = TextureWrapMode::CLAMP, const Color& tintColor = Color::WHITE, const Color& addedColor = Color::TRANSPARENT)
	{
		if (!uploadScratchBitmap(bitmap))
			return false;
		const bool containsTransparentPixels = bitmapContainsTransparentPixels(bitmap);
		if constexpr (ENABLE_GX2_DRAWER_DIAGNOSTICS)
		{
			if (mScratchDebugLogCount < 24 || ((mPresentCount % 300) == 0 && mScratchDebugLogCount < 96))
			{
				RMX_LOG_INFO("GX2Drawer: scratch draw rect=" << rect.x << "," << rect.y << " " << rect.width << "x" << rect.height
					<< " blend=" << (int)mCurrentBlendMode
					<< " transparent=" << containsTransparentPixels
					<< " tintA=" << tintColor.a);
				logBitmapSample("GX2Drawer:   scratch bitmap", bitmap);
				++mScratchDebugLogCount;
			}
		}
		const bool result = drawWithTextureAlphaMode(shouldUseAlphaBlendForTexture(containsTransparentPixels, tintColor, addedColor), [&]()
		{
			return drawTexturedRect(rect, mScratchTexture, Vec2f(0.0f, 0.0f), Vec2f(1.0f, 1.0f), samplingMode, wrapMode, tintColor, addedColor);
		});
		mScratchUploadPending = result;
		return result;
	}

	bool drawColoredRect(const Recti& rect, const Color& color)
	{
		if (!mayRenderAnything() || !mColorShaderReady)
			return false;

		ColorVertex* gpuVertices = allocateColorVertices(PRESENT_VERTEX_COUNT);
		if (nullptr == gpuVertices)
			return false;

		fillColorRectVertices(gpuVertices, rect, color);
		const uint32 vertexDataSize = sizeof(ColorVertex) * PRESENT_VERTEX_COUNT;
		DCFlushRange(gpuVertices, vertexDataSize);
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, gpuVertices, vertexDataSize);

		const BlendMode previousBlendMode = mCurrentBlendMode;
		if (color.a < 0.999f && mCurrentBlendMode == BlendMode::OPAQUE)
		{
			mCurrentBlendMode = BlendMode::ALPHA;
		}

		bindColorState(mCurrentColorBuffer->surface.width, mCurrentColorBuffer->surface.height);
		applyBlendMode();
		applyScissor();
		GX2SetAttribBuffer(0, sizeof(ColorVertex) * PRESENT_VERTEX_COUNT, sizeof(ColorVertex), gpuVertices);
		GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, PRESENT_VERTEX_COUNT, 0, 1);
		mCurrentBlendMode = previousBlendMode;
		return true;
	}

	bool drawDrawerTextureRect(const Recti& rect, DrawerTexture& texture, const Color& tintColor, const Color& addedColor, const Vec2f& uv0, const Vec2f& uv1)
	{
		GX2DrawerTextureImplementation* implementation = texture.getImplementation<GX2DrawerTextureImplementation>();
		if (nullptr == implementation)
			return false;

		if (implementation->mRenderTarget)
		{
			GX2DrawDone();
			invalidateSurfaceAfterColorWrite(implementation->mColorBuffer.surface);
			return drawWithTextureAlphaMode(shouldUseAlphaBlendForTexture(implementation->mContainsTransparentPixels, tintColor, addedColor), [&]()
			{
				return drawTexturedRect(rect, implementation->mTexture, uv0, uv1, mCurrentSamplingMode, mCurrentWrapMode, tintColor, addedColor);
			});
		}

		if (nullptr == implementation->mTexture.surface.image && !texture.getBitmap().empty())
		{
			implementation->updateFromBitmap(texture.getBitmap());
		}
		if (nullptr == implementation->mTexture.surface.image)
			return false;

		return drawWithTextureAlphaMode(shouldUseAlphaBlendForTexture(implementation->mContainsTransparentPixels, tintColor, addedColor), [&]()
		{
			return drawTexturedRect(rect, implementation->mTexture, uv0, uv1, mCurrentSamplingMode, mCurrentWrapMode, tintColor, addedColor);
		});
	}

	bool drawMesh(const MeshDrawCommand& dc)
	{
		if (dc.mTriangles.empty() || nullptr == dc.mTexture)
			return false;
		GX2DrawerTextureImplementation* implementation = dc.mTexture->getImplementation<GX2DrawerTextureImplementation>();
		if (nullptr == implementation || nullptr == implementation->mTexture.surface.image)
			return false;

		if (implementation->mRenderTarget)
		{
			GX2DrawDone();
			invalidateSurfaceAfterColorWrite(implementation->mColorBuffer.surface);
		}

		PresentVertex* vertices = allocateVertices((uint32)dc.mTriangles.size());
		if (nullptr == vertices)
			return false;

		for (size_t i = 0; i < dc.mTriangles.size(); ++i)
		{
			const DrawerMeshVertex& src = dc.mTriangles[i];
			vertices[i] = { toClipX(src.mPosition.x), toClipY(src.mPosition.y), src.mTexcoords.x, src.mTexcoords.y };
		}
		return drawWithTextureAlphaMode(shouldUseAlphaBlendForTexture(implementation->mContainsTransparentPixels, dc.mTintColor, dc.mAddedColor), [&]()
		{
			return submitTextureVertices(implementation->mTexture, vertices, (uint32)dc.mTriangles.size(), mCurrentSamplingMode, mCurrentWrapMode, dc.mTintColor, dc.mAddedColor);
		});
	}

	bool drawMeshVertexColor(const MeshVertexColorDrawCommand& dc)
	{
		if (dc.mTriangles.empty() || !mayRenderAnything() || !mColorShaderReady)
			return false;

		ColorVertex* gpuVertices = allocateColorVertices((uint32)dc.mTriangles.size());
		if (nullptr == gpuVertices)
			return false;

		for (size_t i = 0; i < dc.mTriangles.size(); ++i)
		{
			const DrawerMeshVertex_P2_C4& src = dc.mTriangles[i];
			gpuVertices[i] =
			{
				toClipX(src.mPosition.x),
				toClipY(src.mPosition.y),
				src.mColor.r,
				src.mColor.g,
				src.mColor.b,
				src.mColor.a
			};
		}
		const uint32 vertexDataSize = (uint32)((size_t)dc.mTriangles.size() * sizeof(ColorVertex));
		DCFlushRange(gpuVertices, vertexDataSize);
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, gpuVertices, vertexDataSize);

		bindColorState(mCurrentColorBuffer->surface.width, mCurrentColorBuffer->surface.height);
		applyBlendMode();
		applyScissor();
		GX2SetAttribBuffer(0, vertexDataSize, sizeof(ColorVertex), gpuVertices);
		GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, (uint32)dc.mTriangles.size(), 0, 1);
		return true;
	}

	GX2Texture* getNativeTexture(DrawerTexture& texture)
	{
		GX2DrawerTextureImplementation* implementation = texture.getImplementation<GX2DrawerTextureImplementation>();
		if (nullptr == implementation)
		{
			texture.ensureValidity();
			implementation = texture.getImplementation<GX2DrawerTextureImplementation>();
		}
		if (nullptr == implementation)
			return nullptr;
		if (nullptr == implementation->mTexture.surface.image && !texture.getBitmap().empty())
		{
			implementation->updateFromBitmap(texture.getBitmap());
		}
		return (nullptr != implementation->mTexture.surface.image) ? &implementation->mTexture : nullptr;
	}

	void bindPlaneState(uint32 targetWidth, uint32 targetHeight, GX2Texture& planeDataTexture)
	{
		if (nullptr != mCurrentColorBuffer)
			GX2SetColorBuffer(mCurrentColorBuffer, GX2_RENDER_TARGET_0);
		GX2SetFetchShader(&mPlaneShaderGroup.fetchShader);
		GX2SetVertexShader(mPlaneShaderGroup.vertexShader);
		GX2SetPixelShader(mPlaneShaderGroup.pixelShader);
		setCafeGLSLUniformBlockShaderMode(mPlaneShaderGroup);
		GX2SetStreamOutEnable(FALSE);

		GX2SetViewport(0.0f, 0.0f, (float)targetWidth, (float)targetHeight, 0.0f, 1.0f);
		GX2SetTargetChannelMasks(
			GX2_CHANNEL_MASK_RGBA,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0);
		GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
		GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
		GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);

		GX2Sampler& sampler = getSampler(SamplingMode::POINT, TextureWrapMode::CLAMP);
		const uint32 location = mPlaneSamplerLocations[0];
		if (location != UINT32_MAX)
		{
			GX2SetPixelTexture(&planeDataTexture, location);
			GX2SetPixelSampler(&sampler, location);
		}
	}

	bool submitGX2PlaneRect(const GX2PlaneDrawCommand& dc, const Recti& rect, float paletteOffset)
	{
		if (rect.empty() || nullptr == dc.mResources || !mayRenderAnything() || !mPlaneShaderReady)
			return false;

		RenderParts& renderParts = dc.mResources->getRenderParts();
		const PlaneManager& planeManager = renderParts.getPlaneManager();
		const ScrollOffsetsManager& scrollOffsetsManager = renderParts.getScrollOffsetsManager();
		const Vec4i playfieldSize = (dc.mPlaneIndex <= PlaneManager::PLANE_A) ? planeManager.getPlayfieldSizeForShaders() : Vec4i(512, 256, 64, 32);

		int useHorizontalScrolling = (dc.mPlaneIndex != PlaneManager::PLANE_W) ? 1 : 0;
		int useVerticalScrolling = (dc.mPlaneIndex != PlaneManager::PLANE_W && scrollOffsetsManager.getVerticalScrolling()) ? 1 : 0;
		int scrollOffsetX = 0;
		int scrollOffsetY = 0;
		if (dc.mPlaneIndex == PlaneManager::PLANE_W)
		{
			const Vec2i& wScroll = scrollOffsetsManager.getPlaneWScrollOffset();
			scrollOffsetX = wScroll.x;
			scrollOffsetY = wScroll.y;
			useHorizontalScrolling = 0;
			useVerticalScrolling = 0;
		}
		else
		{
			scrollOffsetX = scrollOffsetsManager.getScrollOffsetsH(dc.mScrollOffsets)[0];
			scrollOffsetY = scrollOffsetsManager.getScrollOffsetsV(dc.mScrollOffsets)[0];
		}
		static uint32 sPlaneDrawLogCount = 0;
		static uint32 sNonEmptyPlaneDrawLogCount = 0;
		if (sPlaneDrawLogCount < 8 || sNonEmptyPlaneDrawLogCount < 24)
		{
			const uint16* planeBuffer = planeManager.getPlanePatternsBuffer((uint8)clamp(dc.mPlaneIndex, 0, 3));
			const int sampleX = clamp((int)(std::abs(scrollOffsetX) / 8) % std::max(1, playfieldSize.z), 0, std::max(0, playfieldSize.z - 1));
			const int sampleY = clamp((int)(std::abs(scrollOffsetY) / 8) % std::max(1, playfieldSize.w), 0, std::max(0, playfieldSize.w - 1));
			const uint16 sampleEntry = (nullptr != planeBuffer) ? planeBuffer[sampleX + sampleY * playfieldSize.z] : 0;
			uint32 nonZeroEntries = 0;
			if (nullptr != planeBuffer)
			{
				const int maxEntries = std::min(playfieldSize.z * playfieldSize.w, 2048);
				for (int index = 0; index < maxEntries; ++index)
				{
					if (planeBuffer[index] != 0)
						++nonZeroEntries;
				}
			}
			const bool shouldLogPlane = (sPlaneDrawLogCount < 8) || (nonZeroEntries > 0 && sNonEmptyPlaneDrawLogCount < 24);
			if (shouldLogPlane)
			{
				RMX_LOG_INFO("GX2Drawer: plane draw " << sPlaneDrawLogCount
					<< " nonEmpty=" << sNonEmptyPlaneDrawLogCount
					<< " plane=" << dc.mPlaneIndex
					<< " prio=" << (dc.mPriorityFlag ? 1 : 0)
					<< " rect=" << rect.x << "," << rect.y << " " << rect.width << "x" << rect.height
					<< " scrollId=" << (int)dc.mScrollOffsets
					<< " scroll=" << scrollOffsetX << "," << scrollOffsetY
					<< " useHV=" << useHorizontalScrolling << "," << useVerticalScrolling
					<< " playfield=" << playfieldSize.x << "x" << playfieldSize.y
					<< " patterns=" << playfieldSize.z << "x" << playfieldSize.w
					<< " sample=" << rmx::hexString(sampleEntry, 4)
					<< " nonZero=" << nonZeroEntries);
				if (nonZeroEntries > 0)
					++sNonEmptyPlaneDrawLogCount;
			}
			++sPlaneDrawLogCount;
		}

		GX2Texture* planeDataTexture = getNativeTexture(dc.mResources->getPlaneDataTexture());
		if (nullptr == planeDataTexture)
			return false;

		PlaneVertex* vertices = allocatePlaneVertices(PRESENT_VERTEX_COUNT);
		if (nullptr == vertices)
			return false;
		fillPlaneVertices(vertices, rect);
		const uint32 vertexDataSize = sizeof(PlaneVertex) * PRESENT_VERTEX_COUNT;
		DCFlushRange(vertices, vertexDataSize);
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, vertices, vertexDataSize);

		const BlendMode previousBlendMode = mCurrentBlendMode;
		mCurrentBlendMode = BlendMode::OPAQUE;
		bindPlaneState(mCurrentColorBuffer->surface.width, mCurrentColorBuffer->surface.height, *planeDataTexture);
		setPixelUniformVec4(mPlaneConfigUniforms[0], Vec4f((float)playfieldSize.x, (float)playfieldSize.y, (float)playfieldSize.z, (float)playfieldSize.w));
		setPixelUniformVec4(mPlaneConfigUniforms[1], Vec4f(dc.mPriorityFlag ? 1.0f : 0.0f, paletteOffset, (float)scrollOffsetsManager.getVerticalScrollOffsetBias(), scrollOffsetsManager.getHorizontalScrollNoRepeat(dc.mScrollOffsets) ? 1.0f : 0.0f));
		setPixelUniformVec4(mPlaneConfigUniforms[2], Vec4f((float)scrollOffsetX, (float)scrollOffsetY, (float)useHorizontalScrolling, (float)useVerticalScrolling));
		const int scrollTextureIndex = (dc.mScrollOffsets == 0xff) ? 4 : clamp((int)dc.mScrollOffsets, 0, 3);
		setPixelUniformVec4(mPlaneConfigUniforms[3], Vec4f((float)dc.mPlaneIndex, (float)scrollTextureIndex, 0.0f, 0.0f));
		applyBlendMode();
		applyScissor();
		GX2SetAttribBuffer(0, vertexDataSize, sizeof(PlaneVertex), vertices);
		GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, PRESENT_VERTEX_COUNT, 0, 1);
		mCurrentBlendMode = previousBlendMode;
		return true;
	}

	bool drawGX2Plane(const GX2PlaneDrawCommand& dc)
	{
		if (nullptr == dc.mResources || !PlaneManager::isRenderablePlaneIndex(dc.mPlaneIndex))
			return false;

		RenderParts& renderParts = dc.mResources->getRenderParts();
		const int splitY = renderParts.getPaletteManager().mSplitPositionY;
		if (splitY > dc.mActiveRect.y && splitY < dc.mActiveRect.y + dc.mActiveRect.height)
		{
			const Recti upperRect(dc.mActiveRect.x, dc.mActiveRect.y, dc.mActiveRect.width, splitY - dc.mActiveRect.y);
			const Recti lowerRect(dc.mActiveRect.x, splitY, dc.mActiveRect.width, dc.mActiveRect.y + dc.mActiveRect.height - splitY);
			bool ok = submitGX2PlaneRect(dc, upperRect, 0.0f);
			ok = submitGX2PlaneRect(dc, lowerRect, 0.5f) || ok;
			return ok;
		}
		const float paletteOffset = (dc.mActiveRect.y >= splitY) ? 0.5f : 0.0f;
		return submitGX2PlaneRect(dc, dc.mActiveRect, paletteOffset);
	}

	void bindVdpSpriteState(uint32 targetWidth, uint32 targetHeight, GX2Texture& planeDataTexture)
	{
		if (nullptr != mCurrentColorBuffer)
			GX2SetColorBuffer(mCurrentColorBuffer, GX2_RENDER_TARGET_0);
		GX2SetFetchShader(&mVdpSpriteShaderGroup.fetchShader);
		GX2SetVertexShader(mVdpSpriteShaderGroup.vertexShader);
		GX2SetPixelShader(mVdpSpriteShaderGroup.pixelShader);
		setCafeGLSLUniformBlockShaderMode(mVdpSpriteShaderGroup);
		GX2SetStreamOutEnable(FALSE);

		GX2SetViewport(0.0f, 0.0f, (float)targetWidth, (float)targetHeight, 0.0f, 1.0f);
		GX2SetTargetChannelMasks(
			GX2_CHANNEL_MASK_RGBA,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0);
		GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
		GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
		GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);

		const uint32 location = mVdpSpriteSamplerLocations[0];
		if (location != UINT32_MAX)
		{
			GX2Sampler& sampler = getSampler(SamplingMode::POINT, TextureWrapMode::CLAMP);
			GX2SetPixelTexture(&planeDataTexture, location);
			GX2SetPixelSampler(&sampler, location);
		}
	}

	bool drawGX2VdpSprite(const GX2VdpSpriteDrawCommand& dc)
	{
		if (nullptr == dc.mResources || dc.mRect.empty() || !mayRenderAnything() || !mVdpSpriteShaderReady)
			return false;

		GX2Texture* planeDataTexture = getNativeTexture(dc.mResources->getPlaneDataTexture());
		if (nullptr == planeDataTexture)
			return false;

		PlaneVertex* vertices = allocatePlaneVertices(PRESENT_VERTEX_COUNT);
		if (nullptr == vertices)
			return false;
		fillPlaneVertices(vertices, dc.mRect);
		const uint32 vertexDataSize = sizeof(PlaneVertex) * PRESENT_VERTEX_COUNT;
		DCFlushRange(vertices, vertexDataSize);
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, vertices, vertexDataSize);

		bindVdpSpriteState(mCurrentColorBuffer->surface.width, mCurrentColorBuffer->surface.height, *planeDataTexture);
		setPixelUniformVec4(mVdpSpriteUniforms[0], Vec4f((float)dc.mRect.x, (float)dc.mRect.y, (float)dc.mSizeInPatterns.x, (float)dc.mSizeInPatterns.y));
		setPixelUniformVec4(mVdpSpriteUniforms[1], Vec4f((float)dc.mFirstPattern, (float)dc.mSplitY, 0.0f, 0.0f));
		setPixelUniformVec4(mVdpSpriteUniforms[2], Vec4f(dc.mTintColor.r, dc.mTintColor.g, dc.mTintColor.b, dc.mTintColor.a));
		setPixelUniformVec4(mVdpSpriteUniforms[3], Vec4f(dc.mAddedColor.r, dc.mAddedColor.g, dc.mAddedColor.b, dc.mAddedColor.a));
		applyBlendMode();
		applyScissor();
		GX2SetAttribBuffer(0, vertexDataSize, sizeof(PlaneVertex), vertices);
		GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, PRESENT_VERTEX_COUNT, 0, 1);
		return true;
	}

	void bindVdpSpriteBatchState(uint32 targetWidth, uint32 targetHeight, GX2Texture& planeDataTexture)
	{
		if (nullptr != mCurrentColorBuffer)
			GX2SetColorBuffer(mCurrentColorBuffer, GX2_RENDER_TARGET_0);
		GX2SetFetchShader(&mVdpSpriteBatchShaderGroup.fetchShader);
		GX2SetVertexShader(mVdpSpriteBatchShaderGroup.vertexShader);
		GX2SetPixelShader(mVdpSpriteBatchShaderGroup.pixelShader);
		setCafeGLSLUniformBlockShaderMode(mVdpSpriteBatchShaderGroup);
		GX2SetStreamOutEnable(FALSE);

		GX2SetViewport(0.0f, 0.0f, (float)targetWidth, (float)targetHeight, 0.0f, 1.0f);
		GX2SetTargetChannelMasks(
			GX2_CHANNEL_MASK_RGBA,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0);
		GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
		GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
		GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);

		const uint32 location = mVdpSpriteBatchSamplerLocations[0];
		if (location != UINT32_MAX)
		{
			GX2Sampler& sampler = getSampler(SamplingMode::POINT, TextureWrapMode::CLAMP);
			GX2SetPixelTexture(&planeDataTexture, location);
			GX2SetPixelSampler(&sampler, location);
		}
	}

	bool drawGX2VdpSpriteBatch(const std::vector<const GX2VdpSpriteDrawCommand*>& commands)
	{
		if (commands.empty())
			return true;
		if (commands.size() == 1 || !mVdpSpriteBatchShaderReady)
		{
			bool ok = false;
			for (const GX2VdpSpriteDrawCommand* command : commands)
			{
				if (nullptr != command)
					ok = drawGX2VdpSprite(*command) || ok;
			}
			return ok;
		}

		const GX2VdpSpriteDrawCommand* firstCommand = commands.front();
		if (nullptr == firstCommand || nullptr == firstCommand->mResources || !mayRenderAnything())
			return false;

		GX2Texture* planeDataTexture = getNativeTexture(firstCommand->mResources->getPlaneDataTexture());
		if (nullptr == planeDataTexture)
			return false;

		const uint32 vertexCount = (uint32)(commands.size() * PRESENT_VERTEX_COUNT);
		VdpSpriteBatchVertex* vertices = allocateDynamicVertices<VdpSpriteBatchVertex>(vertexCount);
		if (nullptr == vertices)
			return false;

		uint32 vertexIndex = 0;
		for (const GX2VdpSpriteDrawCommand* command : commands)
		{
			if (nullptr == command)
				continue;

			const Recti& rect = command->mRect;
			const float x0 = toClipX((float)rect.x);
			const float x1 = toClipX((float)(rect.x + rect.width));
			const float y0 = toClipY((float)rect.y);
			const float y1 = toClipY((float)(rect.y + rect.height));
			const float lx0 = (float)rect.x;
			const float lx1 = (float)(rect.x + rect.width);
			const float ly0 = (float)rect.y;
			const float ly1 = (float)(rect.y + rect.height);
			const float config0[4] = { (float)rect.x, (float)rect.y, (float)command->mSizeInPatterns.x, (float)command->mSizeInPatterns.y };
			const float config1[2] = { (float)command->mFirstPattern, (float)command->mSplitY };
			const float tint[4] = { command->mTintColor.r, command->mTintColor.g, command->mTintColor.b, command->mTintColor.a };
			const float added[4] = { command->mAddedColor.r, command->mAddedColor.g, command->mAddedColor.b, command->mAddedColor.a };
			const float positions[6][4] =
			{
				{ x0, y0, lx0, ly0 },
				{ x0, y1, lx0, ly1 },
				{ x1, y1, lx1, ly1 },
				{ x1, y1, lx1, ly1 },
				{ x1, y0, lx1, ly0 },
				{ x0, y0, lx0, ly0 },
			};
			for (int i = 0; i < 6; ++i)
			{
				VdpSpriteBatchVertex& vertex = vertices[vertexIndex++];
				vertex.x = positions[i][0];
				vertex.y = positions[i][1];
				vertex.localX = positions[i][2];
				vertex.localY = positions[i][3];
				vertex.spriteX = config0[0];
				vertex.spriteY = config0[1];
				vertex.sizeX = config0[2];
				vertex.sizeY = config0[3];
				vertex.firstPattern = config1[0];
				vertex.splitY = config1[1];
				vertex.tintR = tint[0];
				vertex.tintG = tint[1];
				vertex.tintB = tint[2];
				vertex.tintA = tint[3];
				vertex.addedR = added[0];
				vertex.addedG = added[1];
				vertex.addedB = added[2];
				vertex.addedA = added[3];
			}
		}

		const uint32 vertexDataSize = (uint32)((size_t)vertexIndex * sizeof(VdpSpriteBatchVertex));
		DCFlushRange(vertices, vertexDataSize);
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, vertices, vertexDataSize);

		bindVdpSpriteBatchState(mCurrentColorBuffer->surface.width, mCurrentColorBuffer->surface.height, *planeDataTexture);
		applyBlendMode();
		applyScissor();
		GX2SetAttribBuffer(0, vertexDataSize, sizeof(VdpSpriteBatchVertex), vertices);
		GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, vertexIndex, 0, 1);
		static uint32 sVdpBatchLogCount = 0;
		if (sVdpBatchLogCount < 6)
		{
			RMX_LOG_INFO("GX2Drawer: VDP sprite batch submitted sprites=" << (uint32)commands.size()
				<< " vertices=" << vertexIndex
				<< " blend=" << (int)mCurrentBlendMode);
			++sVdpBatchLogCount;
		}
		return true;
	}

	void bindPaletteSpriteState(uint32 targetWidth, uint32 targetHeight, GX2Texture& dataTexture)
	{
		if (nullptr != mCurrentColorBuffer)
			GX2SetColorBuffer(mCurrentColorBuffer, GX2_RENDER_TARGET_0);
		GX2SetFetchShader(&mPaletteSpriteShaderGroup.fetchShader);
		GX2SetVertexShader(mPaletteSpriteShaderGroup.vertexShader);
		GX2SetPixelShader(mPaletteSpriteShaderGroup.pixelShader);
		setCafeGLSLUniformBlockShaderMode(mPaletteSpriteShaderGroup);
		GX2SetStreamOutEnable(FALSE);

		GX2SetViewport(0.0f, 0.0f, (float)targetWidth, (float)targetHeight, 0.0f, 1.0f);
		GX2SetTargetChannelMasks(
			GX2_CHANNEL_MASK_RGBA,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0);
		GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
		GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
		GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);

		GX2Sampler& sampler = getSampler(SamplingMode::POINT, TextureWrapMode::CLAMP);
		if (mPaletteSpriteSamplerLocations[0] != UINT32_MAX)
		{
			GX2SetPixelTexture(&dataTexture, mPaletteSpriteSamplerLocations[0]);
			GX2SetPixelSampler(&sampler, mPaletteSpriteSamplerLocations[0]);
		}
	}

	bool drawGX2PaletteSprite(const GX2PaletteSpriteDrawCommand& dc)
	{
		if (dc.mRect.empty() || nullptr == dc.mDataTexture || !mayRenderAnything() || !mPaletteSpriteShaderReady)
			return false;

		GX2Texture* dataTexture = getNativeTexture(*dc.mDataTexture);
		if (nullptr == dataTexture)
			return false;

		PlaneVertex* vertices = allocatePlaneVertices(PRESENT_VERTEX_COUNT);
		if (nullptr == vertices)
			return false;
		fillPlaneVertices(vertices, dc.mRect);
		const uint32 vertexDataSize = sizeof(PlaneVertex) * PRESENT_VERTEX_COUNT;
		DCFlushRange(vertices, vertexDataSize);
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, vertices, vertexDataSize);

		bindPaletteSpriteState(mCurrentColorBuffer->surface.width, mCurrentColorBuffer->surface.height, *dataTexture);
		setPixelUniformVec4(mPaletteSpriteUniforms[0], Vec4f((float)dc.mRect.x, (float)dc.mRect.y, (float)dc.mRect.width, (float)dc.mRect.height));
		setPixelUniformVec4(mPaletteSpriteUniforms[1], Vec4f((float)dc.mSplitY, (float)dc.mAtex, (float)dataTexture->surface.width, (float)dataTexture->surface.height));
		setPixelUniformVec4(mPaletteSpriteUniforms[2], Vec4f(dc.mTintColor.r, dc.mTintColor.g, dc.mTintColor.b, dc.mTintColor.a));
		setPixelUniformVec4(mPaletteSpriteUniforms[3], Vec4f(dc.mAddedColor.r, dc.mAddedColor.g, dc.mAddedColor.b, dc.mAddedColor.a));
		applyBlendMode();
		applyScissor();
		GX2SetAttribBuffer(0, vertexDataSize, sizeof(PlaneVertex), vertices);
		GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, PRESENT_VERTEX_COUNT, 0, 1);
		return true;
	}

	DrawerTexture* getComponentSpriteTexture(const SpriteCollection::Item& item)
	{
		if (nullptr == item.mSprite || !item.mUsesComponentSprite)
			return nullptr;

		const ComponentSprite& sprite = static_cast<const ComponentSprite&>(*item.mSprite);
		const Bitmap& source = sprite.getBitmap();
		CachedSpriteTexture& cached = mComponentSpriteTextures[item.mKey];
		if (!cached.mTexture)
		{
			cached.mTexture.reset(new DrawerTexture());
		}
		if (cached.mSourceSize != source.getSize() || cached.mSpriteChangeCounter != item.mChangeCounter)
		{
			cached.mTexture->accessBitmap() = source;
			cached.mTexture->bitmapUpdated();
			cached.mSourceSize = source.getSize();
			cached.mSpriteChangeCounter = item.mChangeCounter;
		}
		return cached.mTexture.get();
	}

	DrawerTexture* getPaletteSpriteTexture(const SpriteCollection::Item& item, const PaletteBase& palette)
	{
		if (nullptr == item.mSprite || item.mUsesComponentSprite)
			return nullptr;

		const PaletteSprite& sprite = static_cast<const PaletteSprite&>(*item.mSprite);
		const PaletteBitmap& indexed = sprite.getBitmap();
		const uint64 cacheKey = item.mKey ^ (palette.getKey() + 0x9e3779b97f4a7c15ull + (item.mKey << 6) + (item.mKey >> 2));
		CachedSpriteTexture& cached = mPaletteSpriteTextures[cacheKey];
		if (!cached.mTexture)
		{
			cached.mTexture.reset(new DrawerTexture());
		}
		if (cached.mSourceSize != indexed.getSize()
			|| cached.mSpriteChangeCounter != item.mChangeCounter
			|| cached.mPaletteChangeCounter != palette.getChangeCounter())
		{
			Bitmap bitmap;
			buildPaletteSpriteBitmap(bitmap, sprite, palette);
			cached.mTexture->accessBitmap().swap(bitmap);
			cached.mTexture->bitmapUpdated();
			cached.mSourceSize = indexed.getSize();
			cached.mSpriteChangeCounter = item.mChangeCounter;
			cached.mPaletteChangeCounter = palette.getChangeCounter();
		}
		return cached.mTexture.get();
	}

	bool drawComponentSpriteRect(const Recti& rect, const SpriteCollection::Item& item, const Color& tintColor)
	{
		DrawerTexture* texture = getComponentSpriteTexture(item);
		if (nullptr == texture)
			return false;
		return drawDrawerTextureRect(rect, *texture, tintColor, Color::TRANSPARENT, Vec2f(0.0f, 0.0f), Vec2f(1.0f, 1.0f));
	}

	bool drawPaletteSpriteRect(const Recti& rect, const SpriteCollection::Item& item, const PaletteBase& palette, const Color& tintColor)
	{
		DrawerTexture* texture = getPaletteSpriteTexture(item, palette);
		if (nullptr == texture)
			return false;
		return drawDrawerTextureRect(rect, *texture, tintColor, Color::TRANSPARENT, Vec2f(0.0f, 0.0f), Vec2f(1.0f, 1.0f));
	}

	bool drawSprite(const SpriteDrawCommand& sc)
	{
		const SpriteCollection::Item* item = SpriteCollection::instance().getSprite(sc.mSpriteKey);
		if (nullptr == item || nullptr == item->mSprite)
			return false;

		SpriteBase& sprite = *item->mSprite;
		Vec2i offset = sprite.mOffset;
		Vec2i size = sprite.getSize();
		if (sc.mScale.x != 1.0f || sc.mScale.y != 1.0f)
		{
			offset.x = roundToInt((float)offset.x * sc.mScale.x);
			offset.y = roundToInt((float)offset.y * sc.mScale.y);
			size.x = roundToInt((float)size.x * sc.mScale.x);
			size.y = roundToInt((float)size.y * sc.mScale.y);
		}
		const Recti targetRect(sc.mPosition + offset, size);

		if (item->mUsesComponentSprite)
		{
			return drawComponentSpriteRect(targetRect, *item, sc.mTintColor);
		}

		const PaletteBase* palette = PaletteCollection::instance().getPalette(sc.mPaletteKey, 0);
		return (nullptr != palette) ? drawPaletteSpriteRect(targetRect, *item, *palette, sc.mTintColor) : false;
	}

	bool drawSpriteRect(const SpriteRectDrawCommand& sc)
	{
		const SpriteCollection::Item* item = SpriteCollection::instance().getSprite(sc.mSpriteKey);
		if (nullptr == item || nullptr == item->mSprite || !item->mUsesComponentSprite)
			return false;
		return drawComponentSpriteRect(sc.mRect, *item, sc.mTintColor);
	}

	template<typename T>
	void printText(Font& font, const T& text, const Recti& rect, const DrawerPrintOptions& printOptions)
	{
		Vec2i drawPosition;
		font.printBitmap(mTempTextBitmap, drawPosition, rect, text, printOptions.mAlignment, printOptions.mSpacing, &mTempTextReservedSize);
		if (mTempTextBitmap.empty())
			return;

		const BlendMode previousBlendMode = mCurrentBlendMode;
		// Match the mature Vulkan/D3D text path: font bitmaps carry antialiasing
		// in alpha, so one-bit alpha testing makes menu text look washed out.
		mCurrentBlendMode = BlendMode::ALPHA;
		if constexpr (ENABLE_GX2_DRAWER_DIAGNOSTICS || ENABLE_GX2_TEXT_DIAGNOSTICS)
		{
			if (mTextDebugLogCount < 16 || ((mPresentCount % 300) == 0 && mTextDebugLogCount < 64))
			{
				RMX_LOG_INFO("GX2Drawer: text draw rect=" << rect.x << "," << rect.y << " " << rect.width << "x" << rect.height
					<< " out=" << drawPosition.x << "," << drawPosition.y << " " << mTempTextBitmap.getWidth() << "x" << mTempTextBitmap.getHeight()
					<< " align=" << printOptions.mAlignment
					<< " tintA=" << printOptions.mTintColor.a);
				logBitmapSample("GX2Drawer:   text bitmap", mTempTextBitmap);
				++mTextDebugLogCount;
			}
		}
		drawScratchBitmap(Recti(drawPosition, mTempTextBitmap.getSize()), mTempTextBitmap, SamplingMode::POINT, TextureWrapMode::CLAMP, printOptions.mTintColor);
		mCurrentBlendMode = previousBlendMode;
	}

	void ensureScreenBitmap()
	{
		const Vec2i size = getDrawableSize();
		if (mScreenBitmap.getSize() != size)
		{
			mScreenBitmap.createReusingMemory(size, mScreenBitmapReservedSize, 0xff000000);
			mScreenDirty = true;
			RMX_LOG_INFO("GX2Drawer: software command target " << size.x << " x " << size.y);
		}
	}

	void destroyTexture()
	{
		if (nullptr != mPresentTexture.surface.image)
		{
			destroyTextureStorage(mPresentTexture, true);
			mPresentTextureSize.clear();
		}
	}

	bool ensureTexture(const Vec2i& size)
	{
		if (mPresentTextureSize == size && nullptr != mPresentTexture.surface.image)
			return true;

		destroyTexture();

		if (!initializeTextureStorage(mPresentTexture, size))
			return false;

		mPresentTextureSize = size;
		if (mPresentTextureCreateLogCount < PRESENT_LOG_LIMIT)
		{
			RMX_LOG_INFO("GX2Drawer: present texture " << size.x << " x " << size.y
				<< " pitch=" << mPresentTexture.surface.pitch
				<< " bytes=" << mPresentTexture.surface.imageSize);
			++mPresentTextureCreateLogCount;
		}
		return true;
	}

	void uploadScreenTexture()
	{
		if (mScreenBitmap.empty())
			return;
		destroyTexture();
		if (!ensureTexture(mScreenBitmap.getSize()))
			return;

		if constexpr (PRESENT_LOGS)
		{
			if (mTexturePresentLogCount < PRESENT_LOG_LIMIT)
			{
				const BitmapSample sample = sampleBitmap(mScreenBitmap);
				RMX_LOG_INFO("GX2Drawer: texture present upload frame=" << mPresentCount
					<< " screen=" << mScreenBitmap.getWidth() << "x" << mScreenBitmap.getHeight()
					<< " nonBlack=" << sample.nonBlackSamples
					<< " alpha=" << sample.alphaSamples
					<< " rgbNoAlpha=" << sample.rgbWithoutAlphaSamples
					<< " xor=" << rmx::hexString(sample.sampleXor, 8)
					<< " center=" << rmx::hexString(sample.center, 8));
				++mTexturePresentLogCount;
			}
		}

		uint8* dstBase = static_cast<uint8*>(GX2RLockSurfaceEx(&mPresentTexture.surface, 0, cpuWriteToGpuReadFlags()));
		if (nullptr == dstBase)
		{
			RMX_ERROR("GX2Drawer: failed to lock present texture upload surface", return);
		}
		const uint32 dstPitchBytes = mPresentTexture.surface.pitch * 4;
		const int width = mScreenBitmap.getWidth();
		const int height = mScreenBitmap.getHeight();

		for (int y = 0; y < height; ++y)
		{
			const uint32* src = mScreenBitmap.getPixelPointer(0, y);
			uint8* dst = dstBase + (size_t)y * dstPitchBytes;
			if (FORCE_TEXTURE_PRESENT_PATTERN)
			{
				for (int x = 0; x < width; ++x)
				{
					dst[x * 4 + 0] = (uint8)((x * 255) / std::max(1, width - 1));
					dst[x * 4 + 1] = (uint8)((y * 255) / std::max(1, height - 1));
					dst[x * 4 + 2] = (uint8)(((x / 32) ^ (y / 32)) & 1 ? 0x20 : 0xff);
					dst[x * 4 + 3] = 0xff;
				}
			}
			else
			{
				memcpy(dst, src, (size_t)width * sizeof(uint32));
			}
		}

		GX2RUnlockSurfaceEx(&mPresentTexture.surface, 0, cpuWriteToGpuReadFlags());
		GX2RInvalidateSurface(&mPresentTexture.surface, 0, cpuWriteToGpuReadFlags());
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, mPresentTexture.surface.image, mPresentTexture.surface.imageSize);
		mScreenDirty = false;
	}

	bool uploadScreenToCpuColorBuffer()
	{
		if (mScreenBitmap.empty())
			return false;

		const Vec2i targetSize = getDrawableSize();
		if (!ensureCpuColorBuffer(targetSize))
			return false;

		GX2DrawDone();
		uint32* dstBase = static_cast<uint32*>(GX2RLockSurfaceEx(&mCpuColorBuffer.surface, 0, GX2R_RESOURCE_USAGE_CPU_WRITE));
		if (nullptr == dstBase)
		{
			RMX_ERROR("GX2Drawer: failed to lock CPU-present color buffer", return false);
		}

		const uint32 pitch = mCpuColorBuffer.surface.pitch;
		const int srcWidth = mScreenBitmap.getWidth();
		const int srcHeight = mScreenBitmap.getHeight();
		const int dstWidth = targetSize.x;
		const int dstHeight = targetSize.y;

		if constexpr (PRESENT_LOGS)
		{
			if (mCpuPresentLogCount < PRESENT_LOG_LIMIT)
			{
				const BitmapSample sample = sampleBitmap(mScreenBitmap);
				RMX_LOG_INFO("GX2Drawer: CPU present upload frame=" << mPresentCount
					<< " screen=" << srcWidth << "x" << srcHeight
					<< " target=" << dstWidth << "x" << dstHeight
					<< " nonBlack=" << sample.nonBlackSamples
					<< " alpha=" << sample.alphaSamples
					<< " rgbNoAlpha=" << sample.rgbWithoutAlphaSamples
					<< " xor=" << rmx::hexString(sample.sampleXor, 8)
					<< " center=" << rmx::hexString(sample.center, 8));
				++mCpuPresentLogCount;
			}
		}

		for (int y = 0; y < dstHeight; ++y)
		{
			uint32* dst = dstBase + (size_t)y * pitch;
			if (FORCE_CPU_PRESENT_PATTERN)
			{
				for (int x = 0; x < dstWidth; ++x)
				{
					const uint32 r = (uint32)((x * 255) / std::max(1, dstWidth - 1));
					const uint32 g = (uint32)((y * 255) / std::max(1, dstHeight - 1));
					const uint32 b = (((x / 48) ^ (y / 48)) & 1) ? 0x20 : 0xff;
					dst[x] = (r << 24) | (g << 16) | (b << 8) | 0xff;
				}
				continue;
			}

			const int srcY = (srcHeight > 0) ? (int)(((uint64)y * (uint64)srcHeight) / (uint64)dstHeight) : 0;
			const uint32* src = mScreenBitmap.getPixelPointer(0, std::min(srcY, srcHeight - 1));
			for (int x = 0; x < dstWidth; ++x)
			{
				const int srcX = (srcWidth > 0) ? (int)(((uint64)x * (uint64)srcWidth) / (uint64)dstWidth) : 0;
				dst[x] = toRGBA8(src[std::min(srcX, srcWidth - 1)]);
			}
		}

		GX2RUnlockSurfaceEx(&mCpuColorBuffer.surface, 0, GX2R_RESOURCE_USAGE_CPU_WRITE);
		GX2RInvalidateSurface(&mCpuColorBuffer.surface, 0, GX2R_RESOURCE_USAGE_CPU_WRITE);
		mScreenDirty = false;
		return true;
	}

	void bindState(uint32 targetWidth, uint32 targetHeight, GX2Texture& texture, GX2Sampler& sampler)
	{
		if (nullptr != mCurrentColorBuffer)
			GX2SetColorBuffer(mCurrentColorBuffer, GX2_RENDER_TARGET_0);
		GX2SetFetchShader(&mShaderGroup.fetchShader);
		GX2SetVertexShader(mShaderGroup.vertexShader);
		GX2SetPixelShader(mShaderGroup.pixelShader);
		setCafeGLSLUniformBlockShaderMode(mShaderGroup);
		GX2SetStreamOutEnable(FALSE);

		GX2SetViewport(0.0f, 0.0f, (float)targetWidth, (float)targetHeight, 0.0f, 1.0f);
		GX2SetTargetChannelMasks(
			GX2_CHANNEL_MASK_RGBA,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0);
		GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
		GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
		GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);

		GX2SetPixelTexture(&texture, mSamplerLocation);
		GX2SetPixelSampler(&sampler, mSamplerLocation);
		setPixelUniformColors(Color::WHITE, Color::TRANSPARENT);
	}

	void bindColorState(uint32 targetWidth, uint32 targetHeight)
	{
		if (nullptr != mCurrentColorBuffer)
			GX2SetColorBuffer(mCurrentColorBuffer, GX2_RENDER_TARGET_0);
		GX2SetFetchShader(&mColorShaderGroup.fetchShader);
		GX2SetVertexShader(mColorShaderGroup.vertexShader);
		GX2SetPixelShader(mColorShaderGroup.pixelShader);
		setCafeGLSLUniformBlockShaderMode(mColorShaderGroup);
		GX2SetStreamOutEnable(FALSE);

		GX2SetViewport(0.0f, 0.0f, (float)targetWidth, (float)targetHeight, 0.0f, 1.0f);
		GX2SetTargetChannelMasks(
			GX2_CHANNEL_MASK_RGBA,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0,
			(GX2ChannelMask)0);
		GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
		GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
		GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);
	}

	void drawToCurrentTarget(uint32 targetWidth, uint32 targetHeight)
	{
		if (!mShaderReady || nullptr == mVertexBuffer || nullptr == mPresentTexture.surface.image)
			return;

		bindState(targetWidth, targetHeight, mPresentTexture, getSampler(SamplingMode::POINT, TextureWrapMode::CLAMP));
		setPixelUniformColors(Color::WHITE, Color::TRANSPARENT);
		mCurrentBlendMode = BlendMode::OPAQUE;
		applyBlendMode();
		applyFullTargetScissor(targetWidth, targetHeight);
		GX2SetAttribBuffer(0, sizeof(PresentVertex) * PRESENT_VERTEX_COUNT, sizeof(PresentVertex), mVertexBuffer);
		GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, PRESENT_VERTEX_COUNT, 0, 1);
	}

	void present()
	{
		if (!mSetupSuccessful)
			return;

		if (!mWindowTargetBound)
		{
			if (mScreenBitmap.empty())
			{
				finishFrame();
				return;
			}
			uploadScreenTexture();

			if constexpr (!USE_WHB_PRESENT)
			{
				const Vec2i targetSize = getDrawableSize();
				if (!ensureCpuColorBuffer(targetSize))
				{
					finishFrame();
					return;
				}
			}
		}

		GX2ColorBuffer* drcColorBuffer = WHBGfxGetDRCColourBuffer();
		GX2ColorBuffer* tvColorBuffer = WHBGfxGetTVColourBuffer();
		if constexpr (ENABLE_GX2_DRAWER_DIAGNOSTICS)
		{
			if (mPresentCount < 8)
			{
				RMX_LOG_INFO("GX2Drawer: present buffers native="
					<< mNativeDrawableSize.x << "x" << mNativeDrawableSize.y
					<< " whb-tv="
					<< (nullptr != tvColorBuffer ? (int)tvColorBuffer->surface.width : 0)
					<< "x" << (nullptr != tvColorBuffer ? (int)tvColorBuffer->surface.height : 0)
					<< " whb-drc=" << (nullptr != drcColorBuffer ? (int)drcColorBuffer->surface.width : 0)
					<< "x" << (nullptr != drcColorBuffer ? (int)drcColorBuffer->surface.height : 0));
			}
		}

		if constexpr (!USE_WHB_PRESENT)
		{
			if (mCpuColorBuffer.surface.width > 0 && mCpuColorBuffer.surface.height > 0)
			{
				const bool cpuPresent = (!mWindowTargetBound && (USE_CPU_COLOR_PRESENT || FORCE_CPU_PRESENT_PATTERN));
				if (nullptr != mNativeContext && !mWindowTargetBound)
				{
					GX2SetContextState(mNativeContext);
				}
				if (cpuPresent)
				{
					if (!uploadScreenToCpuColorBuffer())
					{
						RMX_LOG_WARNING("GX2Drawer: CPU present upload failed");
						finishFrame();
						return;
					}
				}
				else if (!mWindowTargetBound)
				{
					GX2SetColorBuffer(&mCpuColorBuffer, GX2_RENDER_TARGET_0);
					GX2ClearColor(&mCpuColorBuffer, 0.0f, 0.0f, 0.0f, 1.0f);
					if (!FORCE_GPU_PRESENT_CLEAR)
					{
						drawToCurrentTarget(mCpuColorBuffer.surface.width, mCpuColorBuffer.surface.height);
					}
				}
				else if (FORCE_GPU_PRESENT_CLEAR)
				{
					GX2SetColorBuffer(&mCpuColorBuffer, GX2_RENDER_TARGET_0);
					GX2ClearColor(&mCpuColorBuffer, 0.0f, 1.0f, 0.0f, 1.0f);
				}
				if (!cpuPresent)
				{
					GX2Flush();
					GX2DrawDone();
					invalidateSurfaceAfterColorWrite(mCpuColorBuffer.surface);
				}
				if constexpr (PRESENT_LOGS)
				{
					if (mPresentLogCount < PRESENT_LOG_LIMIT)
					{
						RMX_LOG_INFO("GX2Drawer: present frame=" << mPresentCount
							<< " cpuPresent=" << cpuPresent
							<< " windowTarget=" << mWindowTargetBound
							<< " colorBuffer=" << mCpuColorBuffer.surface.width << "x" << mCpuColorBuffer.surface.height
							<< " pitch=" << mCpuColorBuffer.surface.pitch
							<< " tvScan=" << (nullptr != mTvScanBuffer)
							<< " drcScan=" << (nullptr != mDrcScanBuffer));
						++mPresentLogCount;
					}
				}
				GX2SetTVScale(mCpuColorBuffer.surface.width, mCpuColorBuffer.surface.height);
				GX2CopyColorBufferToScanBuffer(&mCpuColorBuffer, GX2_SCAN_TARGET_TV);
				if (nullptr != mDrcScanBuffer)
				{
					GX2SetDRCScale(mCpuColorBuffer.surface.width, mCpuColorBuffer.surface.height);
					GX2CopyColorBufferToScanBuffer(&mCpuColorBuffer, GX2_SCAN_TARGET_DRC);
				}
				GX2Flush();
				GX2SwapScanBuffers();
				GX2SetTVEnable(TRUE);
				GX2DrawDone();
				if constexpr (WAIT_FOR_SCAN_FLIP)
				{
					GX2WaitForFlip();
				}
				++mPresentCount;
				finishFrame();
				return;
			}
		}

		if (nullptr != tvColorBuffer)
		{
			GX2SetTVScale(tvColorBuffer->surface.width, tvColorBuffer->surface.height);
		}
		if (nullptr != drcColorBuffer)
		{
			GX2SetDRCScale(drcColorBuffer->surface.width, drcColorBuffer->surface.height);
		}

		WHBGfxBeginRender();
		if (nullptr != drcColorBuffer)
		{
			WHBGfxBeginRenderDRC();
			WHBGfxClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			drawToCurrentTarget(drcColorBuffer->surface.width, drcColorBuffer->surface.height);
			WHBGfxFinishRenderDRC();
		}
		if (nullptr != tvColorBuffer)
		{
			WHBGfxBeginRenderTV();
			WHBGfxClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			drawToCurrentTarget(tvColorBuffer->surface.width, tvColorBuffer->surface.height);
			WHBGfxFinishRenderTV();
		}
		WHBGfxFinishRender();
		++mPresentCount;
		finishFrame();
	}

public:
	Bitmap mScreenBitmap;
	int mScreenBitmapReservedSize = 0;
	bool mScreenDirty = false;
	bool mSetupSuccessful = false;
	bool mShaderReady = false;
	bool mColorShaderReady = false;
	bool mPlaneShaderReady = false;
	bool mVdpSpriteShaderReady = false;
	bool mVdpSpriteBatchShaderReady = false;
	bool mPaletteSpriteShaderReady = false;
	bool mOwnsGfx = false;
	bool mOwnsProc = false;
	void* mCommandBuffer = nullptr;
	void* mTvScanBuffer = nullptr;
	void* mDrcScanBuffer = nullptr;
	GX2ContextState* mNativeContext = nullptr;
	Vec2i mNativeDrawableSize = Vec2i((int)FALLBACK_TV_WIDTH, (int)FALLBACK_TV_HEIGHT);
	WHBGfxShaderGroup mShaderGroup = {};
	WHBGfxShaderGroup mColorShaderGroup = {};
	WHBGfxShaderGroup mPlaneShaderGroup = {};
	WHBGfxShaderGroup mVdpSpriteShaderGroup = {};
	WHBGfxShaderGroup mVdpSpriteBatchShaderGroup = {};
	WHBGfxShaderGroup mPaletteSpriteShaderGroup = {};
	std::array<GX2Sampler, 4> mSamplers = {};
	uint32 mSamplerLocation = 0;
	std::array<uint32, 1> mPlaneSamplerLocations = {};
	std::array<uint32, 1> mVdpSpriteSamplerLocations = {};
	std::array<uint32, 1> mVdpSpriteBatchSamplerLocations = {};
	std::array<uint32, 2> mPaletteSpriteSamplerLocations = {};
	PixelUniformSlot mTintUniform;
	PixelUniformSlot mAddedUniform;
	PixelUniformSlot mPlaneConfigUniforms[4];
	PixelUniformSlot mVdpSpriteUniforms[4];
	PixelUniformSlot mPaletteSpriteUniforms[4];
	uint8* mPixelUniformBlock = nullptr;
	uint32 mPixelUniformBlockLocation = UINT32_MAX;
	uint32 mPixelUniformBlockSize = 0;
	uint32 mUploadLogCount = 0;
	uint32 mTexturePresentLogCount = 0;
	uint32 mPresentTextureCreateLogCount = 0;
	uint32 mCpuPresentLogCount = 0;
	uint32 mPresentLogCount = 0;
	uint32 mPresentCount = 0;
	GX2ColorBuffer mCpuColorBuffer = {};
	Vec2i mCpuColorBufferSize;
	GX2Texture mPresentTexture = {};
	Vec2i mPresentTextureSize;
	PresentVertex* mVertexBuffer = nullptr;
	uint8* mDynamicVertexBuffer = nullptr;
	size_t mDynamicVertexCapacity = 0;
	size_t mDynamicVertexWriteOffset = 0;
	uint8* mDynamicUniformBuffer = nullptr;
	size_t mDynamicUniformCapacity = 0;
	size_t mDynamicUniformWriteOffset = 0;
	GX2ColorBuffer* mCurrentColorBuffer = nullptr;
	Recti mCurrentViewport;
	Vec4f mPixelToViewSpaceTransform;
	std::vector<Recti> mScissorStack;
	bool mInvalidScissorRegion = false;
	bool mFrameActive = false;
	bool mWindowTargetBound = false;
	bool mWindowTargetCleared = false;
	BlendMode mCurrentBlendMode = BlendMode::OPAQUE;
	SamplingMode mCurrentSamplingMode = SamplingMode::POINT;
	TextureWrapMode mCurrentWrapMode = TextureWrapMode::CLAMP;
	GX2Texture mScratchTexture = {};
	Vec2i mScratchTextureSize;
	bool mScratchUploadPending = false;
	uint32 mScratchDebugLogCount = 0;
	uint32 mSpriteDebugLogCount = 0;
	uint32 mTextDebugLogCount = 0;
	bool mMenuCommandTraceDone = false;
	bool mSoftwarePresentLogged = false;
	std::unordered_map<uint64, CachedSpriteTexture> mComponentSpriteTextures;
	std::unordered_map<uint64, CachedSpriteTexture> mPaletteSpriteTextures;
	Bitmap mScratchBitmap;
	int mScratchBitmapReservedSize = 0;
	Bitmap mSolidBitmap;
	int mSolidBitmapReservedSize = 0;
	Bitmap mTempTextBitmap;
	int mTempTextReservedSize = 0;
};

#else

struct GX2Drawer::Internal
{
	bool mSetupSuccessful = false;
	Bitmap mScreenBitmap;
};

#endif


GX2Drawer::GX2Drawer() :
	mInternal(*new Internal())
{
}

GX2Drawer::~GX2Drawer()
{
	mSoftwareDrawer.clearExternalOutputBitmap();
	delete &mInternal;
}

Drawer::Type GX2Drawer::getType()
{
	return Drawer::Type::GX2;
}

bool GX2Drawer::wasSetupSuccessful()
{
	return mInternal.mSetupSuccessful;
}

void GX2Drawer::createTexture(DrawerTexture& outTexture)
{
#if defined(PLATFORM_WIIU)
	outTexture.setImplementation(new GX2DrawerTextureImplementation(outTexture));
#else
	mSoftwareDrawer.createTexture(outTexture);
#endif
}

void GX2Drawer::refreshTexture(DrawerTexture& texture)
{
#if defined(PLATFORM_WIIU)
	texture.setImplementation(new GX2DrawerTextureImplementation(texture));
#else
	mSoftwareDrawer.refreshTexture(texture);
#endif
}

void GX2Drawer::setupRenderWindow(SDL_Window* window)
{
	(void)window;
#if defined(PLATFORM_WIIU)
	mInternal.ensureScreenBitmap();
	mSoftwareDrawer.setExternalOutputBitmap(&mInternal.mScreenBitmap);
#endif
	mSoftwareDrawer.setupRenderWindow(nullptr);
}

void GX2Drawer::performRendering(const DrawCollection& drawCollection)
{
#if defined(PLATFORM_WIIU)
	const std::vector<DrawCommand*>& drawCommands = drawCollection.getDrawCommands();
	if (drawCommands.empty())
		return;

	if (!mInternal.beginFrame())
		return;
	if constexpr (ENABLE_GX2_DRAWER_DIAGNOSTICS)
	{
		if (mInternal.mPresentCount < 8 || (mInternal.mPresentCount % 300) == 0)
		{
			RMX_LOG_INFO("GX2Drawer: performRendering commands=" << drawCommands.size()
				<< " presentCount=" << mInternal.mPresentCount);
			int commandIndex = 0;
			for (DrawCommand* command : drawCommands)
			{
				switch (command->getType())
				{
					case DrawCommand::Type::SET_WINDOW_RENDER_TARGET:
					{
						SetWindowRenderTargetDrawCommand& dc = command->as<SetWindowRenderTargetDrawCommand>();
						RMX_LOG_INFO("GX2Drawer: cmd[" << commandIndex << "] SET_WINDOW viewport=" << dc.mViewport.x << "," << dc.mViewport.y << " " << dc.mViewport.width << "x" << dc.mViewport.height);
						break;
					}
					case DrawCommand::Type::SET_RENDER_TARGET:
					{
						SetRenderTargetDrawCommand& dc = command->as<SetRenderTargetDrawCommand>();
						RMX_LOG_INFO("GX2Drawer: cmd[" << commandIndex << "] SET_TARGET viewport=" << dc.mViewport.x << "," << dc.mViewport.y << " " << dc.mViewport.width << "x" << dc.mViewport.height
							<< " texSize=" << (nullptr != dc.mTexture ? dc.mTexture->getWidth() : 0) << "x" << (nullptr != dc.mTexture ? dc.mTexture->getHeight() : 0));
						break;
					}
					case DrawCommand::Type::RECT:
					{
						RectDrawCommand& dc = command->as<RectDrawCommand>();
						RMX_LOG_INFO("GX2Drawer: cmd[" << commandIndex << "] RECT rect=" << dc.mRect.x << "," << dc.mRect.y << " " << dc.mRect.width << "x" << dc.mRect.height
							<< " texSize=" << (nullptr != dc.mTexture ? dc.mTexture->getWidth() : 0) << "x" << (nullptr != dc.mTexture ? dc.mTexture->getHeight() : 0)
							<< " colorA=" << dc.mColor.a);
						if (nullptr != dc.mTexture)
						{
							logTextureSample("GX2Drawer:   rect texture", dc.mTexture);
						}
						break;
					}
					case DrawCommand::Type::UPSCALED_RECT:
					{
						UpscaledRectDrawCommand& dc = command->as<UpscaledRectDrawCommand>();
						RMX_LOG_INFO("GX2Drawer: cmd[" << commandIndex << "] UPSCALED rect=" << dc.mRect.x << "," << dc.mRect.y << " " << dc.mRect.width << "x" << dc.mRect.height
							<< " texSize=" << (nullptr != dc.mTexture ? dc.mTexture->getWidth() : 0) << "x" << (nullptr != dc.mTexture ? dc.mTexture->getHeight() : 0));
						logTextureSample("GX2Drawer:   upscaled texture", dc.mTexture);
						break;
					}
					case DrawCommand::Type::SET_BLEND_MODE:
					{
						SetBlendModeDrawCommand& dc = command->as<SetBlendModeDrawCommand>();
						RMX_LOG_INFO("GX2Drawer: cmd[" << commandIndex << "] BLEND mode=" << (int)dc.mBlendMode);
						break;
					}
					default:
					{
						RMX_LOG_INFO("GX2Drawer: cmd[" << commandIndex << "] type=" << (int)command->getType());
						break;
					}
				}
				++commandIndex;
			}
		}
	}
	if constexpr (ENABLE_GX2_MENU_COMMAND_TRACE)
	{
		if (!mInternal.mMenuCommandTraceDone)
		{
			bool hasMenuText = false;
			for (DrawCommand* command : drawCommands)
			{
				if (command->getType() == DrawCommand::Type::PRINT_TEXT || command->getType() == DrawCommand::Type::PRINT_TEXT_W)
				{
					hasMenuText = true;
					break;
				}
			}
			if (hasMenuText)
			{
				mInternal.mMenuCommandTraceDone = true;
				RMX_LOG_INFO("GX2Drawer menu trace: commands=" << drawCommands.size()
					<< " presentCount=" << mInternal.mPresentCount);
				int commandIndex = 0;
				for (DrawCommand* command : drawCommands)
				{
					switch (command->getType())
					{
						case DrawCommand::Type::SET_WINDOW_RENDER_TARGET:
						{
							SetWindowRenderTargetDrawCommand& dc = command->as<SetWindowRenderTargetDrawCommand>();
							RMX_LOG_INFO("GX2Drawer menu trace: [" << commandIndex << "] SET_WINDOW viewport="
								<< dc.mViewport.x << "," << dc.mViewport.y << " " << dc.mViewport.width << "x" << dc.mViewport.height);
							break;
						}
						case DrawCommand::Type::SET_RENDER_TARGET:
						{
							SetRenderTargetDrawCommand& dc = command->as<SetRenderTargetDrawCommand>();
							RMX_LOG_INFO("GX2Drawer menu trace: [" << commandIndex << "] SET_TARGET viewport="
								<< dc.mViewport.x << "," << dc.mViewport.y << " " << dc.mViewport.width << "x" << dc.mViewport.height
								<< " tex=" << (nullptr != dc.mTexture ? dc.mTexture->getWidth() : 0) << "x" << (nullptr != dc.mTexture ? dc.mTexture->getHeight() : 0));
							break;
						}
						case DrawCommand::Type::RECT:
						{
							RectDrawCommand& dc = command->as<RectDrawCommand>();
							RMX_LOG_INFO("GX2Drawer menu trace: [" << commandIndex << "] RECT rect="
								<< dc.mRect.x << "," << dc.mRect.y << " " << dc.mRect.width << "x" << dc.mRect.height
								<< " tex=" << (nullptr != dc.mTexture ? dc.mTexture->getWidth() : 0) << "x" << (nullptr != dc.mTexture ? dc.mTexture->getHeight() : 0)
								<< " color=" << dc.mColor.r << "," << dc.mColor.g << "," << dc.mColor.b << "," << dc.mColor.a
								<< " addA=" << dc.mAddedColor.a);
							break;
						}
						case DrawCommand::Type::SPRITE:
						{
							SpriteDrawCommand& dc = command->as<SpriteDrawCommand>();
							RMX_LOG_INFO("GX2Drawer menu trace: [" << commandIndex << "] SPRITE pos="
								<< dc.mPosition.x << "," << dc.mPosition.y << " scale=" << dc.mScale.x << "," << dc.mScale.y
								<< " tintA=" << dc.mTintColor.a);
							break;
						}
						case DrawCommand::Type::SPRITE_RECT:
						{
							SpriteRectDrawCommand& dc = command->as<SpriteRectDrawCommand>();
							RMX_LOG_INFO("GX2Drawer menu trace: [" << commandIndex << "] SPRITE_RECT rect="
								<< dc.mRect.x << "," << dc.mRect.y << " " << dc.mRect.width << "x" << dc.mRect.height
								<< " tintA=" << dc.mTintColor.a);
							break;
						}
						case DrawCommand::Type::PRINT_TEXT:
						{
							PrintTextDrawCommand& dc = command->as<PrintTextDrawCommand>();
							RMX_LOG_INFO("GX2Drawer menu trace: [" << commandIndex << "] TEXT rect="
								<< dc.mRect.x << "," << dc.mRect.y << " " << dc.mRect.width << "x" << dc.mRect.height
								<< " tintA=" << dc.mPrintOptions.mTintColor.a);
							break;
						}
						case DrawCommand::Type::PRINT_TEXT_W:
						{
							PrintTextWDrawCommand& dc = command->as<PrintTextWDrawCommand>();
							RMX_LOG_INFO("GX2Drawer menu trace: [" << commandIndex << "] TEXT_W rect="
								<< dc.mRect.x << "," << dc.mRect.y << " " << dc.mRect.width << "x" << dc.mRect.height
								<< " tintA=" << dc.mPrintOptions.mTintColor.a);
							break;
						}
						case DrawCommand::Type::SET_BLEND_MODE:
						{
							SetBlendModeDrawCommand& dc = command->as<SetBlendModeDrawCommand>();
							RMX_LOG_INFO("GX2Drawer menu trace: [" << commandIndex << "] BLEND mode=" << (int)dc.mBlendMode);
							break;
						}
						case DrawCommand::Type::SET_SAMPLING_MODE:
						{
							SetSamplingModeDrawCommand& dc = command->as<SetSamplingModeDrawCommand>();
							RMX_LOG_INFO("GX2Drawer menu trace: [" << commandIndex << "] SAMPLING mode=" << (int)dc.mSamplingMode);
							break;
						}
						case DrawCommand::Type::SET_WRAP_MODE:
						{
							SetWrapModeDrawCommand& dc = command->as<SetWrapModeDrawCommand>();
							RMX_LOG_INFO("GX2Drawer menu trace: [" << commandIndex << "] WRAP mode=" << (int)dc.mWrapMode);
							break;
						}
						case DrawCommand::Type::PUSH_SCISSOR:
						{
							PushScissorDrawCommand& dc = command->as<PushScissorDrawCommand>();
							RMX_LOG_INFO("GX2Drawer menu trace: [" << commandIndex << "] PUSH_SCISSOR rect="
								<< dc.mRect.x << "," << dc.mRect.y << " " << dc.mRect.width << "x" << dc.mRect.height);
							break;
						}
						case DrawCommand::Type::POP_SCISSOR:
						{
							RMX_LOG_INFO("GX2Drawer menu trace: [" << commandIndex << "] POP_SCISSOR");
							break;
						}
						default:
						{
							RMX_LOG_INFO("GX2Drawer menu trace: [" << commandIndex << "] type=" << (int)command->getType());
							break;
						}
					}
					++commandIndex;
				}
			}
		}
	}

	if constexpr (USE_SOFTWARE_WINDOW_DRAWER)
	{
		mInternal.ensureScreenBitmap();
		bool targetsWindow = true;
		for (DrawCommand* command : drawCommands)
		{
			if (command->getType() == DrawCommand::Type::SET_RENDER_TARGET)
			{
				targetsWindow = false;
				break;
			}
			if (command->getType() == DrawCommand::Type::SET_WINDOW_RENDER_TARGET)
			{
				targetsWindow = true;
				break;
			}
		}
		if (targetsWindow && !mInternal.mScreenDirty)
		{
			mInternal.mScreenBitmap.clear(0xff000000);
			mInternal.mScreenDirty = true;
		}
		if constexpr (PRESENT_LOGS)
		{
			if (!mInternal.mSoftwarePresentLogged)
			{
				RMX_LOG_INFO("GX2Drawer: using software window present");
				mInternal.mSoftwarePresentLogged = true;
			}
		}
		mSoftwareDrawer.setExternalOutputBitmap(&mInternal.mScreenBitmap);
		mSoftwareDrawer.performRendering(drawCollection);
		return;
	}

	std::vector<const GX2VdpSpriteDrawCommand*> pendingVdpSprites;
	BlendMode pendingVdpBlendMode = BlendMode::OPAQUE;
	GX2RenderResources* pendingVdpResources = nullptr;
	auto flushPendingVdpSprites = [&]()
	{
		if (pendingVdpSprites.empty())
			return;

		const BlendMode previousBlendMode = mInternal.mCurrentBlendMode;
		mInternal.mCurrentBlendMode = pendingVdpBlendMode;
		mInternal.drawGX2VdpSpriteBatch(pendingVdpSprites);
		mInternal.mCurrentBlendMode = previousBlendMode;
		pendingVdpSprites.clear();
		pendingVdpResources = nullptr;
	};

	for (DrawCommand* drawCommand : drawCommands)
	{
		switch (drawCommand->getType())
		{
			case DrawCommand::Type::UNDEFINED:
				flushPendingVdpSprites();
				RMX_ERROR("Got invalid draw command", );
				break;

			case DrawCommand::Type::SET_WINDOW_RENDER_TARGET:
			{
				flushPendingVdpSprites();
				SetWindowRenderTargetDrawCommand& dc = drawCommand->as<SetWindowRenderTargetDrawCommand>();
				mInternal.bindWindowTarget(dc.mViewport);
				break;
			}

			case DrawCommand::Type::SET_RENDER_TARGET:
			{
				flushPendingVdpSprites();
				SetRenderTargetDrawCommand& dc = drawCommand->as<SetRenderTargetDrawCommand>();
				if (nullptr != dc.mTexture)
				{
					mInternal.bindRenderTarget(*dc.mTexture, dc.mViewport);
				}
				break;
			}

			case DrawCommand::Type::RECT:
			{
				flushPendingVdpSprites();
				if (!mInternal.mayRenderAnything())
					break;

				RectDrawCommand& dc = drawCommand->as<RectDrawCommand>();
				if (nullptr == dc.mTexture)
				{
					mInternal.drawColoredRect(dc.mRect, dc.mColor);
				}
				else
				{
					mInternal.drawDrawerTextureRect(dc.mRect, *dc.mTexture, dc.mColor, dc.mAddedColor, dc.mUV0, dc.mUV1);
				}
				break;
			}

			case DrawCommand::Type::UPSCALED_RECT:
			{
				flushPendingVdpSprites();
				if (!mInternal.mayRenderAnything())
					break;

				UpscaledRectDrawCommand& dc = drawCommand->as<UpscaledRectDrawCommand>();
				if (nullptr != dc.mTexture)
				{
					const BlendMode previousBlendMode = mInternal.mCurrentBlendMode;
					const SamplingMode previousSamplingMode = mInternal.mCurrentSamplingMode;
					const TextureWrapMode previousWrapMode = mInternal.mCurrentWrapMode;
					mInternal.mCurrentBlendMode = BlendMode::OPAQUE;
					mInternal.mCurrentSamplingMode = SamplingMode::POINT;
					mInternal.mCurrentWrapMode = TextureWrapMode::CLAMP;
					mInternal.drawDrawerTextureRect(dc.mRect, *dc.mTexture, Color::WHITE, Color::TRANSPARENT, Vec2f(0.0f, 0.0f), Vec2f(1.0f, 1.0f));
					mInternal.mCurrentBlendMode = previousBlendMode;
					mInternal.mCurrentSamplingMode = previousSamplingMode;
					mInternal.mCurrentWrapMode = previousWrapMode;
				}
				break;
			}

			case DrawCommand::Type::SPRITE:
			{
				flushPendingVdpSprites();
				if (!mInternal.mayRenderAnything())
					break;
				mInternal.drawSprite(drawCommand->as<SpriteDrawCommand>());
				break;
			}

			case DrawCommand::Type::SPRITE_RECT:
			{
				flushPendingVdpSprites();
				if (!mInternal.mayRenderAnything())
					break;
				mInternal.drawSpriteRect(drawCommand->as<SpriteRectDrawCommand>());
				break;
			}

			case DrawCommand::Type::MESH:
			{
				flushPendingVdpSprites();
				if (!mInternal.mayRenderAnything())
					break;
				mInternal.drawMesh(drawCommand->as<MeshDrawCommand>());
				break;
			}

			case DrawCommand::Type::MESH_VERTEX_COLOR:
			{
				flushPendingVdpSprites();
				if (!mInternal.mayRenderAnything())
					break;
				mInternal.drawMeshVertexColor(drawCommand->as<MeshVertexColorDrawCommand>());
				break;
			}

			case DrawCommand::Type::GX2_PLANE:
			{
				flushPendingVdpSprites();
				if (!mInternal.mayRenderAnything())
					break;
				mInternal.drawGX2Plane(drawCommand->as<GX2PlaneDrawCommand>());
				break;
			}

			case DrawCommand::Type::GX2_VDP_SPRITE:
			{
				if (!mInternal.mayRenderAnything())
					break;
				GX2VdpSpriteDrawCommand& dc = drawCommand->as<GX2VdpSpriteDrawCommand>();
				if (!pendingVdpSprites.empty()
					&& (pendingVdpBlendMode != mInternal.mCurrentBlendMode || pendingVdpResources != dc.mResources))
				{
					flushPendingVdpSprites();
				}
				if (pendingVdpSprites.empty())
				{
					pendingVdpBlendMode = mInternal.mCurrentBlendMode;
					pendingVdpResources = dc.mResources;
				}
				if (!dc.mRect.empty())
				{
					pendingVdpSprites.push_back(&dc);
				}
				break;
			}

			case DrawCommand::Type::GX2_PALETTE_SPRITE:
			{
				flushPendingVdpSprites();
				if (!mInternal.mayRenderAnything())
					break;
				mInternal.drawGX2PaletteSprite(drawCommand->as<GX2PaletteSpriteDrawCommand>());
				break;
			}

			case DrawCommand::Type::SET_BLEND_MODE:
			{
				const BlendMode blendMode = drawCommand->as<SetBlendModeDrawCommand>().mBlendMode;
				if (!pendingVdpSprites.empty() && pendingVdpBlendMode != blendMode)
				{
					flushPendingVdpSprites();
				}
				mInternal.mCurrentBlendMode = blendMode;
				break;
			}

			case DrawCommand::Type::SET_SAMPLING_MODE:
				mInternal.mCurrentSamplingMode = drawCommand->as<SetSamplingModeDrawCommand>().mSamplingMode;
				break;

			case DrawCommand::Type::SET_WRAP_MODE:
				mInternal.mCurrentWrapMode = drawCommand->as<SetWrapModeDrawCommand>().mWrapMode;
				break;

			case DrawCommand::Type::PRINT_TEXT:
			{
				flushPendingVdpSprites();
				if (!mInternal.mayRenderAnything())
					break;
				PrintTextDrawCommand& dc = drawCommand->as<PrintTextDrawCommand>();
				mInternal.printText(*dc.mFont, dc.mText, dc.mRect, dc.mPrintOptions);
				break;
			}

			case DrawCommand::Type::PRINT_TEXT_W:
			{
				flushPendingVdpSprites();
				if (!mInternal.mayRenderAnything())
					break;
				PrintTextWDrawCommand& dc = drawCommand->as<PrintTextWDrawCommand>();
				mInternal.printText(*dc.mFont, dc.mText, dc.mRect, dc.mPrintOptions);
				break;
			}

			case DrawCommand::Type::PUSH_SCISSOR:
			{
				flushPendingVdpSprites();
				Recti scissorRect = drawCommand->as<PushScissorDrawCommand>().mRect;
				if (!mInternal.mScissorStack.empty())
				{
					scissorRect.intersect(mInternal.mScissorStack.back());
				}
				mInternal.mScissorStack.emplace_back(scissorRect);
				mInternal.applyScissor();
				break;
			}

			case DrawCommand::Type::POP_SCISSOR:
			{
				flushPendingVdpSprites();
				if (!mInternal.mScissorStack.empty())
				{
					mInternal.mScissorStack.pop_back();
				}
				mInternal.applyScissor();
				break;
			}
		}
	}
	flushPendingVdpSprites();
#else
	mSoftwareDrawer.performRendering(drawCollection);
#endif
}

void GX2Drawer::presentScreen()
{
#if defined(PLATFORM_WIIU)
	mInternal.present();
#endif
}
