/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/simulation/EmulatorInterface.h"


#if defined(PLATFORM_WIIU)
namespace
{
	constexpr bool ENABLE_WIIU_RENDER_PART_TIMING_LOGS = false;

	double getElapsedMilliseconds(uint64 start, uint64 end)
	{
		return (double)(end - start) * 1000.0 / (double)SDL_GetPerformanceFrequency();
	}
}
#endif


RenderParts::RenderParts() :
	mPlaneManager(mPatternManager),
	mScrollOffsetsManager(mPlaneManager),
	mSpriteManager(mPatternManager, mSpacesManager)
{
	for (int i = 0; i < 8; ++i)
		mLayerRendering[i] = true;

	reset();
}

void RenderParts::reset()
{
	mActiveDisplay = true;
	mPaletteManager.setShadowHighlightMode(false);

	mPlaneManager.reset();
	mSpriteManager.clear();
	mScrollOffsetsManager.reset();
}

void RenderParts::preFrameUpdate()
{
	// TODO: It could make sense to require an explicit script call for these as well, see "Renderer.resetCustomPlaneConfigurations()"
	mPaletteManager.preFrameUpdate();
	mSpriteManager.preFrameUpdate();
	mScrollOffsetsManager.preFrameUpdate();
}

void RenderParts::postFrameUpdate()
{
	mSpriteManager.postFrameUpdate();
	mScrollOffsetsManager.postFrameUpdate();
}

void RenderParts::refresh(const RefreshParameters& refreshParameters)
{
	if (!refreshParameters.mSkipThisFrame)
	{
	#if defined(PLATFORM_WIIU)
		const uint64 t0 = ENABLE_WIIU_RENDER_PART_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
	#endif
		mPatternManager.refresh();
	#if defined(PLATFORM_WIIU)
		const uint64 t1 = ENABLE_WIIU_RENDER_PART_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
	#endif
		mPlaneManager.refresh();
	#if defined(PLATFORM_WIIU)
		const uint64 t2 = ENABLE_WIIU_RENDER_PART_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
	#endif
		mScrollOffsetsManager.refresh(refreshParameters);
	#if defined(PLATFORM_WIIU)
		const uint64 t3 = ENABLE_WIIU_RENDER_PART_TIMING_LOGS ? SDL_GetPerformanceCounter() : 0;
	#endif
		EmulatorInterface::instance().getVRamChangeBits().clearAllBits();
	#if defined(PLATFORM_WIIU)
		if constexpr (ENABLE_WIIU_RENDER_PART_TIMING_LOGS)
		{
			static uint32 sSampleCount = 0;
			static double sPatternMs = 0.0;
			static double sPlaneMs = 0.0;
			static double sScrollMs = 0.0;
			++sSampleCount;
			sPatternMs += getElapsedMilliseconds(t0, t1);
			sPlaneMs += getElapsedMilliseconds(t1, t2);
			sScrollMs += getElapsedMilliseconds(t2, t3);
			if ((sSampleCount % 180) == 0)
			{
				const double inv = 1.0 / 180.0;
				RMX_LOG_INFO("RenderParts timing avg pattern=" << (float)(sPatternMs * inv)
					<< "ms plane=" << (float)(sPlaneMs * inv)
					<< "ms scroll=" << (float)(sScrollMs * inv)
					<< "ms total=" << (float)((sPatternMs + sPlaneMs + sScrollMs) * inv)
					<< "ms");
				sPatternMs = 0.0;
				sPlaneMs = 0.0;
				sScrollMs = 0.0;
			}
		}
	#endif
	}
}

void RenderParts::dumpPatternsContent()
{
	PaletteBitmap bmp;
	mPatternManager.dumpAsPaletteBitmap(bmp);

	std::vector<uint8> content;
	bmp.saveBMP(content, mPaletteManager.getMainPalette(0).getRawColors());
	FTX::FileSystem->saveFile("dump.bmp", content.data(), (uint32)content.size());
}

void RenderParts::dumpPlaneContent(int planeIndex)
{
	PaletteBitmap bmp;
	mPlaneManager.dumpAsPaletteBitmap(bmp, planeIndex);

	std::vector<uint8> content;
	bmp.saveBMP(content, mPaletteManager.getMainPalette(0).getRawColors());
	FTX::FileSystem->saveFile("dump.bmp", content.data(), (uint32)content.size());
}
