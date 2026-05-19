cmake_minimum_required(VERSION 3.13)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()


project(Sonic3AIR)

if (CMAKE_SYSTEM_NAME STREQUAL "CafeOS" OR WIIU OR CAFEOS)
	set(PLATFORM_WIIU_BUILD ON)
else()
	set(PLATFORM_WIIU_BUILD OFF)
endif()

set(S3AIR_DEFAULT_BUILD_OXYGEN_ENGINEAPP ON)
set(S3AIR_DEFAULT_BUILD_SDL_STATIC ON)
set(S3AIR_DEFAULT_USE_DISCORD ON)
set(S3AIR_DEFAULT_USE_IMGUI ON)
if (PLATFORM_WIIU_BUILD)
	set(S3AIR_DEFAULT_BUILD_OXYGEN_ENGINEAPP OFF)
	set(S3AIR_DEFAULT_BUILD_SDL_STATIC OFF)
	set(S3AIR_DEFAULT_USE_DISCORD OFF)
	set(S3AIR_DEFAULT_USE_IMGUI OFF)
endif()

# Build options
option(BUILD_OXYGEN_ENGINEAPP "Build the Oxygen App executable" ${S3AIR_DEFAULT_BUILD_OXYGEN_ENGINEAPP})
option(BUILD_OXYGEN_SERVER "Build the Oxygen server executable" OFF)
option(BUILD_SDL_STATIC "Build SDL as a static library instead of a shared / dynamic one" ${S3AIR_DEFAULT_BUILD_SDL_STATIC})
option(USE_GLES "Use OpenGLESv2" OFF)
option(USE_DISCORD "Use Discord API" ${S3AIR_DEFAULT_USE_DISCORD})
option(USE_IMGUI "Use ImGui, required for dev mode and system windows" ${S3AIR_DEFAULT_USE_IMGUI})


if (USE_GLES)
	add_definitions(-DRMX_LINUX_ENFORCE_GLES2)
endif()


# Determine whether to support discord integration
if (USE_DISCORD)
	set(USE_DISCORD ON)
endif()
if (UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
	link_directories(/opt/vc/lib)	# Needed when building on Raspberry Pi
	set(USE_DISCORD OFF)
endif()



message(STATUS "BUILD_OXYGEN_ENGINEAPP = ${BUILD_OXYGEN_ENGINEAPP}")
message(STATUS "BUILD_OXYGEN_SERVER = ${BUILD_OXYGEN_SERVER}")
message(STATUS "BUILD_SDL_STATIC = ${BUILD_SDL_STATIC}")
message(STATUS "USE_GLES = ${USE_GLES}")
message(STATUS "USE_DISCORD = ${USE_DISCORD}")
message(STATUS "USE_IMGUI = ${USE_IMGUI}")
message(STATUS "PLATFORM_WIIU_BUILD = ${PLATFORM_WIIU_BUILD}")



set(CMAKE_CXX_STANDARD 17)

get_filename_component(WORKSPACE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
get_filename_component(DEFAULT_GX2GL_DIR "${WORKSPACE_DIR}/../gx2gl" ABSOLUTE)
set(GX2GL_DIR "${DEFAULT_GX2GL_DIR}" CACHE PATH "Path to the gx2gl checkout used by the Wii U build")

#add_definitions(-DDEBUG)
if (USE_IMGUI)
	add_definitions(-DUSE_IMGUI)
endif()


if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")

	# GCC only
	#set(CMAKE_CXX_FLAGS "-Wall")
	set(CMAKE_CXX_FLAGS_DEBUG "-g2")		# Include debug information
	set(CMAKE_CXX_FLAGS_RELEASE "-g0")		# Leave out debug information
	set(CMAKE_CXX_FLAGS_RELEASE "-O3")		# Full optimization

	add_compile_options(-Wno-unused-function)	# Silence a warning in SDL2 build (unused function "xinput2_remove_device_info")
	add_compile_options(-Wno-stringop-overflow)	# Silence a warning in rmxbase String class usage
	add_compile_options(-Wno-psabi)

	# Needed under Linux to avoid link error from minizip: "relocation r_x86_64_pc32 against symbol `_length_code' can not be used when making a shared object; recompile with -fpic"
	if (UNIX)
		set(CMAKE_POSITION_INDEPENDENT_CODE ON)
	endif()

elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")

	# Clang only
	#set(CMAKE_C_COMPILER   "/usr/bin/clang")
	#set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
	#set(CMAKE_AR      "/usr/bin/llvm-ar" CACHE PATH "archive")
	#set(CMAKE_LINKER  "/usr/bin/llvm-ld" CACHE PATH "linker")
	#set(CMAKE_NM      "/usr/bin/llvm-nm" CACHE PATH "nm")
	#set(CMAKE_OBJDUMP "/usr/bin/llvm-objdump" CACHE PATH "objdump")
	#set(CMAKE_RANLIB  "/usr/bin/llvm-ranlib" CACHE PATH "ranlib")

endif()


find_package(Threads REQUIRED)
if (NOT PLATFORM_WIIU_BUILD)
	find_package(OpenGL REQUIRED)
endif()


# SDL2 sources as virtual subdirectory "SDL", except on Wii U where devkitPro
# provides the SDL2 port that already knows about CafeOS input/audio/video.
if (PLATFORM_WIIU_BUILD)
	add_library(wiiu_sdl2 INTERFACE)
	set(S3AIR_DEVKITPRO_ROOT "/opt/devkitpro" CACHE PATH "devkitPro root as seen by the Wii U cross compiler")
	target_include_directories(wiiu_sdl2 INTERFACE
		"${S3AIR_DEVKITPRO_ROOT}/portlibs/wiiu/include"
		"${S3AIR_DEVKITPRO_ROOT}/portlibs/wiiu/include/SDL2")
	target_link_directories(wiiu_sdl2 INTERFACE
		"${S3AIR_DEVKITPRO_ROOT}/portlibs/wiiu/lib"
		"${S3AIR_DEVKITPRO_ROOT}/portlibs/ppc/lib")
	target_link_libraries(wiiu_sdl2 INTERFACE SDL2 m wut)
elseif (BUILD_SDL_STATIC)
	set(SDL_STATIC ON)
	set(SDL_SHARED OFF)
	include_directories(SDL/include)
else()
	set(SDL_STATIC OFF)
	set(SDL_SHARED ON)
	if (UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
		include(FindPkgConfig)
		pkg_check_modules(SDL2 sdl2 REQUIRED)
	endif()
endif()

if (NOT PLATFORM_WIIU_BUILD)
	add_subdirectory(${WORKSPACE_DIR}/framework/external/sdl/SDL2 SDL)
endif()


# zlib sources as virtual subdirectory "zlib"
include_directories(${WORKSPACE_DIR}/framework/external/zlib/zlib)		# Included before add_subdirectory so that the examples will build
add_subdirectory(${WORKSPACE_DIR}/framework/external/zlib/zlib zlib)



include_directories(${WORKSPACE_DIR}/framework/external/ogg-vorbis/libogg/include)
include_directories(${WORKSPACE_DIR}/framework/external/ogg-vorbis/libvorbis/include)
include_directories(${WORKSPACE_DIR}/framework/external/ogg-vorbis/libvorbis/lib)
include_directories(build/zlib)		# Needed for zconf.h
include_directories(${WORKSPACE_DIR}/framework/external/zlib/zlib/contrib/minizip)
if (USE_IMGUI)
	include_directories(${WORKSPACE_DIR}/framework/external/imgui/imgui)
endif()
include_directories(${WORKSPACE_DIR}/librmx/source)
include_directories(${WORKSPACE_DIR}/librmx/source/rmxmedia/_glew)
include_directories(${WORKSPACE_DIR}/Oxygen/lemonscript/source)
include_directories(${WORKSPACE_DIR}/Oxygen/oxygenengine/source)
include_directories(${WORKSPACE_DIR}/Oxygen/oxygenserver/source)
include_directories(${WORKSPACE_DIR}/Oxygen/sonic3air/source)
include_directories(${WORKSPACE_DIR}/Oxygen/sonic3air/source/external)
if (PLATFORM_WIIU_BUILD)
	include_directories(${GX2GL_DIR}/include)
	include_directories(${GX2GL_DIR}/src)
endif()



# ogg vorbis

# TODO: I tried creating config_types.h by calling "./configure" by a add_custom_command,
#       but that did not work; instead the solution is to simply add the generated file to versioning

file(GLOB OGGVORBIS_SOURCES ${WORKSPACE_DIR}/framework/external/ogg-vorbis/libogg/src/*.c
							${WORKSPACE_DIR}/framework/external/ogg-vorbis/libvorbis/lib/*.c)

add_library(oggvorbis ${OGGVORBIS_SOURCES})



# minizip

file(GLOB MINIZIP_SOURCES ${WORKSPACE_DIR}/framework/external/zlib/zlib/contrib/minizip/ioapi.c
						  ${WORKSPACE_DIR}/framework/external/zlib/zlib/contrib/minizip/unzip.c
						  ${WORKSPACE_DIR}/framework/external/zlib/zlib/contrib/minizip/zip.c)

add_library(minizip ${MINIZIP_SOURCES})

target_link_libraries(minizip zlibstatic)

if (PLATFORM_WIIU_BUILD)
	target_compile_definitions(minizip PRIVATE USE_FILE32API)
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_compile_options(minizip PRIVATE -Wno-implicit-function-declaration -Wno-int-conversion)	# Silence these warnings in minizip build
endif()



# gx2gl
if (PLATFORM_WIIU_BUILD)
	set(GX2GL_SOURCES
		${GX2GL_DIR}/src/core/gl_context.cpp
		${GX2GL_DIR}/src/core/gl_buffer.cpp
		${GX2GL_DIR}/src/core/gl_texture.cpp
		${GX2GL_DIR}/src/core/gl_shader.cpp
		${GX2GL_DIR}/src/core/gx2gl_cafeglsl.cpp
		${GX2GL_DIR}/src/core/gx2gl_shader_compat.cpp
		${GX2GL_DIR}/src/core/gx2gl_proc.cpp
		${GX2GL_DIR}/src/core/gx2gl_sdl_bridge.cpp
		${GX2GL_DIR}/src/core/gl_vao.cpp
		${GX2GL_DIR}/src/core/gl_framebuffer.cpp
		${GX2GL_DIR}/src/core/gl_draw.cpp
		${GX2GL_DIR}/src/core/gl_get.cpp
		${GX2GL_DIR}/src/state/gl_state.cpp
		${GX2GL_DIR}/src/mem/gl_mem.cpp)

	add_library(gx2gl STATIC ${GX2GL_SOURCES})
	target_include_directories(gx2gl PUBLIC ${GX2GL_DIR}/include ${GX2GL_DIR}/src)
	target_link_libraries(gx2gl PUBLIC wut)

	option(S3AIR_WIIU_BUILD_GX2VK "Build gx2vk alongside gx2gl for Wii U renderer experiments" ON)
	option(S3AIR_WIIU_ENABLE_GX2VK_RENDERER "Enable Sonic 3 A.I.R.'s Vulkan renderer on Wii U through gx2vk" OFF)
	if (S3AIR_WIIU_BUILD_GX2VK)
		set(GX2VK_SOURCES
			${GX2GL_DIR}/src/vk/gx2vk_core.cpp
			${GX2GL_DIR}/src/vk/gx2vk_memory.cpp
			${GX2GL_DIR}/src/vk/gx2vk_cmd.cpp
			${GX2GL_DIR}/src/vk/gx2vk_shader.cpp
			${GX2GL_DIR}/src/vk/gx2vk_descriptor.cpp
			${GX2GL_DIR}/src/vk/gx2vk_pipeline.cpp)

		add_library(gx2vk STATIC ${GX2VK_SOURCES})
		target_include_directories(gx2vk PUBLIC ${GX2GL_DIR}/include ${GX2GL_DIR}/src)
		target_link_libraries(gx2vk PUBLIC wut)
	endif()
endif()



# imgui
if (USE_IMGUI)
	file(GLOB IMGUI_SOURCES ${WORKSPACE_DIR}/framework/external/imgui/imgui/*.cpp
							${WORKSPACE_DIR}/framework/external/imgui/imgui/backends/imgui_impl_opengl3.cpp
							${WORKSPACE_DIR}/framework/external/imgui/imgui/backends/imgui_impl_sdl2.cpp)

	add_library(imgui ${IMGUI_SOURCES})

	if (BUILD_SDL_STATIC)
		target_link_libraries(imgui SDL2-static)
	else()
		target_link_libraries(imgui SDL2)
		if (UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
			target_link_libraries(imgui pthread)
			target_link_libraries(imgui dl)
		endif()
	endif()
	if (USE_GLES)
		target_link_libraries(imgui GLESv2)
	else()
		target_link_libraries(imgui OpenGL::GL)
	endif()
endif()



# rmxbase

file(GLOB_RECURSE RMXBASE_SOURCES ${WORKSPACE_DIR}/librmx/source/rmxbase/*.cpp)

add_library(rmxbase ${RMXBASE_SOURCES})

if (NOT PLATFORM_WIIU_BUILD AND NOT CMAKE_VERSION VERSION_LESS "3.16.0")
	target_precompile_headers(rmxbase PRIVATE ${WORKSPACE_DIR}/librmx/source/rmxbase.h)
endif()

target_link_libraries(rmxbase stdc++fs)
target_link_libraries(rmxbase zlibstatic)



# rmxmedia

file(GLOB_RECURSE RMXMEDIA_SOURCES ${WORKSPACE_DIR}/librmx/source/rmxmedia/*.cpp
								   ${WORKSPACE_DIR}/librmx/source/rmxmedia/_glew/*.c)
if (PLATFORM_WIIU_BUILD)
	list(FILTER RMXMEDIA_SOURCES EXCLUDE REGEX ".*/_glew/.*")
endif()

add_library(rmxmedia ${RMXMEDIA_SOURCES})

target_link_libraries(rmxmedia rmxbase)
if (PLATFORM_WIIU_BUILD)
	target_link_libraries(rmxmedia wiiu_sdl2)
	target_link_libraries(rmxmedia gx2gl)
elseif (BUILD_SDL_STATIC)
	target_link_libraries(rmxmedia SDL2-static)
	target_link_libraries(rmxmedia ${SDL2_STATIC_LIBRARIES})
	target_link_options(rmxmedia PRIVATE ${SDL2_STATIC_LDLIBS_OTHER})
	target_compile_options(rmxmedia PRIVATE ${SDL2_STATIC_CFLAGS})
else()
	target_link_libraries(rmxmedia SDL2)
endif()
if (PLATFORM_WIIU_BUILD)
	# gx2gl provides the OpenGL entry points on top of GX2.
elseif (USE_GLES)
	target_link_libraries(rmxmedia GLESv2)
else()
	target_link_libraries(rmxmedia OpenGL::GL)
endif()



# rmxext_oggvorbis

file(GLOB RMXEXT_OGGVORBIS_SOURCES ${WORKSPACE_DIR}/librmx/source/rmxext_oggvorbis/*.cpp)

add_library(rmxext_oggvorbis ${RMXEXT_OGGVORBIS_SOURCES})

target_link_libraries(rmxext_oggvorbis rmxmedia)
target_link_libraries(rmxext_oggvorbis oggvorbis)



# lemonscript

file(GLOB_RECURSE LEMONSCRIPT_SOURCES ${WORKSPACE_DIR}/Oxygen/lemonscript/source/lemon/*.cpp)

add_library(lemonscript ${LEMONSCRIPT_SOURCES})

if (NOT PLATFORM_WIIU_BUILD AND NOT CMAKE_VERSION VERSION_LESS "3.16.0")
	target_precompile_headers(lemonscript PRIVATE ${WORKSPACE_DIR}/Oxygen/lemonscript/source/lemon/pch.h)
endif()

target_link_libraries(lemonscript rmxbase)



# oxygen_netcore

file(GLOB_RECURSE OXYGEN_NETCORE_SOURCES ${WORKSPACE_DIR}/Oxygen/oxygenengine/source/oxygen_netcore/*.cpp)

add_library(oxygen_netcore ${OXYGEN_NETCORE_SOURCES})

if (NOT PLATFORM_WIIU_BUILD AND NOT CMAKE_VERSION VERSION_LESS "3.16.0")
	target_precompile_headers(oxygen_netcore PRIVATE ${WORKSPACE_DIR}/Oxygen/oxygenengine/source/oxygen_netcore/pch.h)
endif()

target_link_libraries(oxygen_netcore rmxbase)



# oxygenengine

file(GLOB_RECURSE OXYGEN_SOURCES ${WORKSPACE_DIR}/Oxygen/oxygenengine/source/oxygen/*.cpp)

add_library(oxygen ${OXYGEN_SOURCES})

if (NOT CMAKE_VERSION VERSION_LESS "3.16.0")
	# TODO: Precompiled headers lead to build errors with ImGui, there's a conflict between GLEW and ImGui's OpenGL headers
	#  -> A way around this might be to not have <rmxmedia.h> in oxygen's PCH, at least for CMake builds
	#target_precompile_headers(oxygen PRIVATE ${WORKSPACE_DIR}/Oxygen/oxygenengine/source/oxygen/pch.h)
endif()

target_link_libraries(oxygen minizip)
target_link_libraries(oxygen rmxmedia)
target_link_libraries(oxygen rmxext_oggvorbis)
target_link_libraries(oxygen lemonscript)
if (USE_IMGUI)
	target_link_libraries(oxygen imgui)
endif()
if (PLATFORM_WIIU_BUILD AND S3AIR_WIIU_ENABLE_GX2VK_RENDERER AND TARGET gx2vk)
	target_compile_definitions(oxygen PUBLIC OXYGEN_ENABLE_VULKAN_RENDERER S3AIR_WIIU_ENABLE_GX2VK_RENDERER)
	target_link_libraries(oxygen gx2vk)
endif()
target_link_libraries(oxygen oxygen_netcore)

if (UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
	target_link_libraries(oxygen atomic)	# Needed when building on Raspberry Pi
endif()

if (UNIX)
	find_package(CURL REQUIRED)
	target_link_libraries(oxygen CURL::libcurl)
endif()



# OxygenApp
if (BUILD_OXYGEN_ENGINEAPP)

	file(GLOB_RECURSE ENGINEAPP_SOURCES ${WORKSPACE_DIR}/Oxygen/oxygenengine/source/engineapp/*.cpp)

	# TODO: CMake seems to use a different working directory for this, so "Oxygen" is intentionally missing here
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${WORKSPACE_DIR}/oxygenengine")

	add_executable(OxygenApp ${ENGINEAPP_SOURCES})

	if (UNIX AND NOT APPLE)
		# Different executable name on Linux
		set_target_properties(OxygenApp PROPERTIES OUTPUT_NAME "oxygenapp_linux")
	endif ()

	if (NOT PLATFORM_WIIU_BUILD AND NOT CMAKE_VERSION VERSION_LESS "3.16.0")
		target_precompile_headers(OxygenApp PRIVATE ${WORKSPACE_DIR}/Oxygen/oxygenengine/source/engineapp/pch.h)
	endif()

	target_link_libraries(OxygenApp Threads::Threads)
	target_link_libraries(OxygenApp oxygen)

endif()



# Oxygen Server
if (BUILD_OXYGEN_SERVER)

	file(GLOB_RECURSE OXYGENSERVER_SOURCES ${WORKSPACE_DIR}/Oxygen/oxygenserver/source/oxygenserver/*.cpp)

	# TODO: CMake seems to use a different working directory for this, so "Oxygen" is intentionally missing here
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${WORKSPACE_DIR}/oxygenserver")

	add_executable(OxygenServer ${OXYGENSERVER_SOURCES})
	set_target_properties(OxygenServer PROPERTIES OUTPUT_NAME "oxygenserver")

	if (NOT PLATFORM_WIIU_BUILD AND NOT CMAKE_VERSION VERSION_LESS "3.16.0")
		target_precompile_headers(OxygenServer PRIVATE ${WORKSPACE_DIR}/Oxygen/oxygenserver/source/oxygenserver/pch.h)
	endif()

	target_link_libraries(OxygenServer oxygen_netcore)

endif()



# discord_game_sdk

if (USE_DISCORD)
	file(GLOB_RECURSE DISCORD_SOURCES ${WORKSPACE_DIR}/Oxygen/sonic3air/source/external/discord_game_sdk/cpp/*.cpp)

	add_library(discord_game_sdk_source ${DISCORD_SOURCES})

	link_directories("${WORKSPACE_DIR}/Oxygen/sonic3air/source/external/discord_game_sdk/lib/x86_64")

	target_link_libraries(discord_game_sdk_source discord_game_sdk)

	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath='$ORIGIN'")
endif()



# Sonic3AIR

file(GLOB_RECURSE SONIC3AIR_SOURCES ${WORKSPACE_DIR}/Oxygen/sonic3air/source/sonic3air/*.cpp)

# TODO: CMake seems to use a different working directory for this, so "Oxygen" is intentionally missing here
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${WORKSPACE_DIR}/sonic3air")

add_executable(Sonic3AIR ${SONIC3AIR_SOURCES})

if (UNIX AND NOT APPLE)
	# Different executable name on Linux
	set_target_properties(Sonic3AIR PROPERTIES OUTPUT_NAME "sonic3air_linux")
endif()

if (NOT PLATFORM_WIIU_BUILD AND NOT CMAKE_VERSION VERSION_LESS "3.16.0")
	target_precompile_headers(Sonic3AIR PRIVATE ${WORKSPACE_DIR}/Oxygen/sonic3air/source/sonic3air/pch.h)
endif()

target_link_libraries(Sonic3AIR Threads::Threads)
target_link_libraries(Sonic3AIR oxygen)
if (USE_DISCORD)
	target_link_libraries(Sonic3AIR discord_game_sdk_source)
endif()
if (PLATFORM_WIIU_BUILD)
	target_link_libraries(Sonic3AIR wut)
	if (TARGET gx2vk)
		target_link_libraries(Sonic3AIR gx2vk)
	endif()
	set_target_properties(Sonic3AIR PROPERTIES OUTPUT_NAME "sonic3air")
	wut_create_rpx(Sonic3AIR)

	set(S3AIR_WIIU_GLSLCOMPILER_RPL "${GX2GL_DIR}/build/glslcompiler.rpl" CACHE FILEPATH "Cafe GLSL compiler RPL copied into the Wii U WUHB content directory")
	set(S3AIR_WIIU_WUHB_CONTENT_DIR "${CMAKE_CURRENT_BINARY_DIR}/wuhb-content")
	file(MAKE_DIRECTORY "${S3AIR_WIIU_WUHB_CONTENT_DIR}")

	if (EXISTS "${S3AIR_WIIU_GLSLCOMPILER_RPL}")
		set(S3AIR_WIIU_WUHB_GLSLCOMPILER "${S3AIR_WIIU_WUHB_CONTENT_DIR}/glslcompiler.rpl")
		add_custom_command(
			OUTPUT "${S3AIR_WIIU_WUHB_GLSLCOMPILER}"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${S3AIR_WIIU_WUHB_CONTENT_DIR}"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${S3AIR_WIIU_GLSLCOMPILER_RPL}" "${S3AIR_WIIU_WUHB_GLSLCOMPILER}"
			DEPENDS "${S3AIR_WIIU_GLSLCOMPILER_RPL}"
			COMMENT "Copying Cafe GLSL compiler into Wii U WUHB content"
			VERBATIM
		)
		add_custom_target(Sonic3AIRWuhbContent ALL DEPENDS "${S3AIR_WIIU_WUHB_GLSLCOMPILER}")
		wut_create_wuhb(Sonic3AIR_WUHB
			TARGET Sonic3AIR
			OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sonic3air.wuhb"
			CONTENT "${S3AIR_WIIU_WUHB_CONTENT_DIR}"
			NAME "Sonic 3 A.I.R."
			SHORTNAME "Sonic3AIR"
			AUTHOR "Eukaryot / Act-One / gx2gl"
		)
		add_dependencies(Sonic3AIR_WUHB Sonic3AIRWuhbContent)
	else()
		message(WARNING "S3AIR_WIIU_GLSLCOMPILER_RPL was not found; WUHB creation is disabled until glslcompiler.rpl is available")
	endif()
endif()

