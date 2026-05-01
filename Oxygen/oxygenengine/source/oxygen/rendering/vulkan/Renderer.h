/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2026 by siahisaforker
*	Based on original Oxygen Engine work by Eukaryot.
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen/rendering/vulkan/Pipeline.h"
#include "oxygen/rendering/vulkan/SwapchainPresentBackend.h"
#include "oxygen/rendering/vulkan/VmaAllocatorImpl.h"

#if defined(OXYGEN_ENABLE_VULKAN_RENDERER)

class Geometry;

namespace vulkan
{
	class RendererBackend
	{
	public:
		struct FrameGlobals
		{
			float mProjection[16] = {};
			float mTimeSeconds = 0.0f;
			float mWindowWidth = 0.0f;
			float mWindowHeight = 0.0f;
			float mGameWidth = 0.0f;
			float mGameHeight = 0.0f;
			float mPadding[2] = {};
		};

	public:
		RendererBackend();
		~RendererBackend();

		bool startup(SDL_Window& window);
		void shutdown();
		void reset();
		void setGameResolution(const Vec2i& gameResolution);
		bool beginOffscreenFrame(SDL_Window& window);
		void submitOffscreenFrame(bool waitForCompletion);
		bool beginFrame(SDL_Window& window, bool useVSync);
		void beginColorPass(Texture& targetTexture, VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, Texture* depthTexture = nullptr);
		void endRenderingPass();
		void endFrame();
		void present();
		bool presentTexture(Texture& sourceTexture, SDL_Window& window, bool useVSync);
		void clearGameScreen();
		void renderGameScreen(const std::vector<Geometry*>& geometries);

		class VulkanDevice& getDevice() const			{ return *mDevice; }
		class IAllocator& getAllocator() const			{ return *mAllocator; }
		class Descriptors& getDescriptors() const		{ return *mDescriptors; }
		class PipelineLibrary& getPipelines() const		{ return *mPipelines; }
		class IPresentBackend& getPresentBackend() const{ return *mPresentBackend; }
		class Frame& getCurrentFrame()				{ return mFrames[mCurrentFrameIndex]; }
		const class Frame& getCurrentFrame() const	{ return mFrames[mCurrentFrameIndex]; }
		inline uint32_t getCurrentFrameIndex() const		{ return mCurrentFrameIndex; }
		Texture& getWindowTexture() const				{ return *mWindowTexture; }
		Texture& getProcessingTexture() const			{ return *mProcessingTexture; }

	private:
		std::unique_ptr<VulkanDevice> mDevice;
		std::unique_ptr<IAllocator> mAllocator;
		std::unique_ptr<Descriptors> mDescriptors;
		std::unique_ptr<PipelineLibrary> mPipelines;
		std::unique_ptr<IPresentBackend> mPresentBackend;
		std::unique_ptr<Texture> mWindowTexture;
		std::unique_ptr<Texture> mProcessingTexture;
		std::array<Frame, VULKAN_FRAMES_IN_FLIGHT> mFrames;
		SDL_Window* mWindow = nullptr;
		Vec2i mGameResolution;
		uint32_t mCurrentFrameIndex = 0;
		uint32_t mCurrentSwapchainImageIndex = 0;
		VkImage mCurrentSwapchainImage = VK_NULL_HANDLE;
		bool mFrameStarted = false;
		bool mFrameReadyToPresent = false;
		bool mRenderingActive = false;
	};
}

#endif
