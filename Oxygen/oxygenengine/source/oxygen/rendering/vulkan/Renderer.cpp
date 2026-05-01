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

#include "oxygen/rendering/vulkan/Renderer.h"

#include "oxygen/application/Configuration.h"
#include "oxygen/rendering/vulkan/VulkanDevice.h"
#include "oxygen/helper/Logging.h"


namespace
{
	void recordImageBarrier(vulkan::VulkanDevice& device, VkCommandBuffer commandBuffer, VkImage image, VkImageAspectFlags aspectMask, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags2 srcStageMask, VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask)
	{
		VkImageMemoryBarrier2 imageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		imageBarrier.srcStageMask = srcStageMask;
		imageBarrier.srcAccessMask = srcAccessMask;
		imageBarrier.dstStageMask = dstStageMask;
		imageBarrier.dstAccessMask = dstAccessMask;
		imageBarrier.oldLayout = oldLayout;
		imageBarrier.newLayout = newLayout;
		imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.image = image;
		imageBarrier.subresourceRange.aspectMask = aspectMask;
		imageBarrier.subresourceRange.baseMipLevel = 0;
		imageBarrier.subresourceRange.levelCount = 1;
		imageBarrier.subresourceRange.baseArrayLayer = 0;
		imageBarrier.subresourceRange.layerCount = 1;

		VkDependencyInfo dependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		dependencyInfo.imageMemoryBarrierCount = 1;
		dependencyInfo.pImageMemoryBarriers = &imageBarrier;
		device.vk().CmdPipelineBarrier2(commandBuffer, &dependencyInfo);
	}

	void writeIdentityProjection(vulkan::RendererBackend::FrameGlobals& frameGlobals)
	{
		std::memset(frameGlobals.mProjection, 0, sizeof(frameGlobals.mProjection));
		frameGlobals.mProjection[0] = 1.0f;
		frameGlobals.mProjection[5] = 1.0f;
		frameGlobals.mProjection[10] = 1.0f;
		frameGlobals.mProjection[15] = 1.0f;
	}

	void beginColorRendering(vulkan::VulkanDevice& device, VkCommandBuffer commandBuffer, vulkan::Texture& targetTexture, VkAttachmentLoadOp loadOp, vulkan::Texture* depthTexture)
	{
		recordImageBarrier(device, commandBuffer, targetTexture.getImage(), VK_IMAGE_ASPECT_COLOR_BIT, targetTexture.getLayout(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			(targetTexture.getLayout() == VK_IMAGE_LAYOUT_UNDEFINED) ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			0,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
		targetTexture.setLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		VkClearValue clearValue = {};
		clearValue.color.float32[0] = 0.0f;
		clearValue.color.float32[1] = 0.0f;
		clearValue.color.float32[2] = 0.0f;
		clearValue.color.float32[3] = 1.0f;

		VkRenderingAttachmentInfo colorAttachmentInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		colorAttachmentInfo.imageView = targetTexture.getImageView();
		colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachmentInfo.loadOp = loadOp;
		colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachmentInfo.clearValue = clearValue;

		VkRenderingAttachmentInfo depthAttachmentInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		if (nullptr != depthTexture)
		{
			recordImageBarrier(device, commandBuffer, depthTexture->getImage(), VK_IMAGE_ASPECT_DEPTH_BIT, depthTexture->getLayout(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				(depthTexture->getLayout() == VK_IMAGE_LAYOUT_UNDEFINED) ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
				0,
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
			depthTexture->setLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

			VkClearValue depthClearValue = {};
			depthClearValue.depthStencil.depth = 0.0f;
			depthClearValue.depthStencil.stencil = 0;
			depthAttachmentInfo.imageView = depthTexture->getImageView();
			depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			depthAttachmentInfo.loadOp = loadOp;
			depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			depthAttachmentInfo.clearValue = depthClearValue;
		}

		VkRenderingInfo renderingInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
		renderingInfo.renderArea.offset = { 0, 0 };
		renderingInfo.renderArea.extent = { (uint32_t)targetTexture.getSize().x, (uint32_t)targetTexture.getSize().y };
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachmentInfo;
		renderingInfo.pDepthAttachment = (nullptr != depthTexture) ? &depthAttachmentInfo : nullptr;
		device.vk().CmdBeginRendering(commandBuffer, &renderingInfo);

		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (float)targetTexture.getSize().x;
		viewport.height = (float)targetTexture.getSize().y;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		device.vk().CmdSetViewport(commandBuffer, 0, 1, &viewport);

		VkRect2D scissor = {};
		scissor.extent = { (uint32_t)targetTexture.getSize().x, (uint32_t)targetTexture.getSize().y };
		device.vk().CmdSetScissor(commandBuffer, 0, 1, &scissor);
	}

	void getWindowSizeForRendering(SDL_Window& window, int& outWidth, int& outHeight)
	{
		outWidth = 0;
		outHeight = 0;
		SDL_GetWindowSizeInPixels(&window, &outWidth, &outHeight);
		if (outWidth <= 0 || outHeight <= 0)
		{
			SDL_GetWindowSize(&window, &outWidth, &outHeight);
		}
	}
}


vulkan::RendererBackend::RendererBackend()
{
}

vulkan::RendererBackend::~RendererBackend()
{
	shutdown();
}

bool vulkan::RendererBackend::startup(SDL_Window& window)
{
	shutdown();

	mWindow = &window;
	mPresentBackend = std::make_unique<SwapchainPresentBackend>();
	RMX_LOG_INFO("Vulkan: startup begin");

	std::vector<const char*> instanceExtensions;
	std::vector<const char*> deviceExtensions;
	mPresentBackend->collectInstanceExtensions(window, instanceExtensions);
	mPresentBackend->collectDeviceExtensions(deviceExtensions);

	mDevice = std::make_unique<VulkanDevice>();
	RMX_LOG_INFO("Vulkan: creating device");
	if (!mDevice->startup(instanceExtensions, deviceExtensions, window, *mPresentBackend))
		return false;

	mAllocator = std::make_unique<VmaAllocatorImpl>();
	RMX_LOG_INFO("Vulkan: creating allocator");
	if (!mAllocator->startup(*mDevice))
		return false;

	RMX_LOG_INFO("Vulkan: creating frames");
	for (Frame& frame : mFrames)
	{
		if (!frame.startup(*mDevice, *mAllocator, 1024 * 1024))
			return false;
	}

	mDescriptors = std::make_unique<Descriptors>();
	RMX_LOG_INFO("Vulkan: creating descriptors");
	if (!mDescriptors->startup(*mDevice, mFrames))
		return false;

	mWindowTexture = std::make_unique<Texture>();
	mProcessingTexture = std::make_unique<Texture>();
	RMX_LOG_INFO("Vulkan: initializing present backend");
	if (!mPresentBackend->init(*mDevice, window, *mWindowTexture, true))
		return false;

	const Vec2i presentExtent = mPresentBackend->getExtent();
	RMX_LOG_INFO("Vulkan: creating offscreen textures " << presentExtent.x << " x " << presentExtent.y);
	if (!mWindowTexture->create(*mDevice, *mAllocator, OFFSCREEN_FORMAT, presentExtent, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true, true))
		return false;
	if (!mProcessingTexture->create(*mDevice, *mAllocator, OFFSCREEN_FORMAT, presentExtent, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true, true))
		return false;

	mPipelines = std::make_unique<PipelineLibrary>();
	const std::wstring pipelineCachePath = Configuration::instance().mAppDataPath + L"pipeline_cache_vulkan.bin";
	RMX_LOG_INFO("Vulkan: creating pipelines");
	if (!mPipelines->startup(*mDevice, *mDescriptors, OFFSCREEN_FORMAT, VK_FORMAT_D32_SFLOAT, pipelineCachePath))
		return false;

	mCurrentFrameIndex = 0;
	mCurrentSwapchainImageIndex = 0;
	mCurrentSwapchainImage = VK_NULL_HANDLE;
	mFrameStarted = false;
	mFrameReadyToPresent = false;
	mRenderingActive = false;
	RMX_LOG_INFO("Vulkan: startup complete");
	return true;
}

void vulkan::RendererBackend::shutdown()
{
	if (mDevice != nullptr && mDevice->getDevice() != VK_NULL_HANDLE)
	{
		mDevice->vk().DeviceWaitIdle(mDevice->getDevice());
	}

	if (mWindowTexture != nullptr && mAllocator != nullptr && mDevice != nullptr)
		mWindowTexture->destroy(*mDevice, *mAllocator);
	if (mProcessingTexture != nullptr && mAllocator != nullptr && mDevice != nullptr)
		mProcessingTexture->destroy(*mDevice, *mAllocator);
	if (mPipelines != nullptr && mDevice != nullptr)
		mPipelines->shutdown(*mDevice);
	if (mDescriptors != nullptr && mDevice != nullptr)
		mDescriptors->shutdown(*mDevice);
	if (mAllocator != nullptr && mDevice != nullptr)
	{
		for (Frame& frame : mFrames)
			frame.shutdown(*mDevice, *mAllocator);
		mAllocator->shutdown(*mDevice);
	}
	if (mPresentBackend != nullptr && mDevice != nullptr)
		mPresentBackend->shutdown(*mDevice);
	if (mDevice != nullptr)
		mDevice->shutdown();

	mPipelines.reset();
	mDescriptors.reset();
	mWindowTexture.reset();
	mProcessingTexture.reset();
	mAllocator.reset();
	mPresentBackend.reset();
	mDevice.reset();
	mWindow = nullptr;
	mFrameStarted = false;
	mFrameReadyToPresent = false;
	mRenderingActive = false;
}

void vulkan::RendererBackend::reset()
{
	mCurrentFrameIndex = 0;
	mCurrentSwapchainImageIndex = 0;
	mCurrentSwapchainImage = VK_NULL_HANDLE;
	mFrameStarted = false;
	mFrameReadyToPresent = false;
	mRenderingActive = false;
}

void vulkan::RendererBackend::setGameResolution(const Vec2i& gameResolution)
{
	mGameResolution = gameResolution;
}

bool vulkan::RendererBackend::beginOffscreenFrame(SDL_Window& window)
{
	if (mDevice == nullptr || mAllocator == nullptr || mDescriptors == nullptr)
		return false;

	mWindow = &window;

	Frame& frame = mFrames[mCurrentFrameIndex];
	if (!frame.begin(*mDevice, *mAllocator))
		return false;

	int windowWidth = 0;
	int windowHeight = 0;
	getWindowSizeForRendering(window, windowWidth, windowHeight);

	FrameGlobals frameGlobals = {};
	writeIdentityProjection(frameGlobals);
	frameGlobals.mTimeSeconds = (float)SDL_GetTicks() / 1000.0f;
	frameGlobals.mWindowWidth = (float)std::max(windowWidth, 1);
	frameGlobals.mWindowHeight = (float)std::max(windowHeight, 1);
	frameGlobals.mGameWidth = (float)mGameResolution.x;
	frameGlobals.mGameHeight = (float)mGameResolution.y;
	if (frame.getGlobalsMappedData() != nullptr)
	{
		std::memcpy(frame.getGlobalsMappedData(), &frameGlobals, sizeof(frameGlobals));
	}
	mDescriptors->updateFrameSet(*mDevice, mCurrentFrameIndex);

	mCurrentSwapchainImageIndex = 0;
	mCurrentSwapchainImage = VK_NULL_HANDLE;
	mFrameStarted = true;
	mFrameReadyToPresent = false;
	mRenderingActive = false;
	return true;
}

void vulkan::RendererBackend::submitOffscreenFrame(bool waitForCompletion)
{
	if (!mFrameStarted || mDevice == nullptr)
		return;

	Frame& frame = mFrames[mCurrentFrameIndex];
	if (mRenderingActive && frame.isRecording())
	{
		mDevice->vk().CmdEndRendering(frame.getCommandBuffer());
		mRenderingActive = false;
	}
	frame.end(*mDevice);

	VkCommandBufferSubmitInfo commandBufferSubmitInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
	commandBufferSubmitInfo.commandBuffer = frame.getCommandBuffer();

	VkSubmitInfo2 submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	submitInfo.commandBufferInfoCount = 1;
	submitInfo.pCommandBufferInfos = &commandBufferSubmitInfo;

	const VkResult submitResult = mDevice->vk().QueueSubmit2(mDevice->getGraphicsQueue(), 1, &submitInfo, frame.getFence());
	if (submitResult != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkQueueSubmit2 failed with result " << (int)submitResult);
	}
	else if (waitForCompletion)
	{
		const VkFence fence = frame.getFence();
		mDevice->vk().WaitForFences(mDevice->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
	}

	mCurrentFrameIndex = (mCurrentFrameIndex + 1) % VULKAN_FRAMES_IN_FLIGHT;
	mCurrentSwapchainImage = VK_NULL_HANDLE;
	mFrameStarted = false;
	mFrameReadyToPresent = false;
	mRenderingActive = false;
}

bool vulkan::RendererBackend::beginFrame(SDL_Window& window, bool useVSync)
{
	if (mDevice == nullptr || mAllocator == nullptr || mPresentBackend == nullptr || mWindowTexture == nullptr)
		return false;

	mWindow = &window;
	if (mPresentBackend->isVSyncEnabled() != useVSync)
	{
		mPresentBackend->resize(*mDevice, window, *mWindowTexture, useVSync);
	}

	const Vec2i currentExtent = mPresentBackend->getExtent();
	if (!mWindowTexture->isValid() || mWindowTexture->getSize() != currentExtent)
	{
		if (mWindowTexture->isValid())
			mWindowTexture->destroy(*mDevice, *mAllocator);
		if (mProcessingTexture->isValid())
			mProcessingTexture->destroy(*mDevice, *mAllocator);
		mWindowTexture->create(*mDevice, *mAllocator, OFFSCREEN_FORMAT, currentExtent, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true, true);
		mProcessingTexture->create(*mDevice, *mAllocator, OFFSCREEN_FORMAT, currentExtent, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true, true);
	}

	Frame& frame = mFrames[mCurrentFrameIndex];
	if (!frame.begin(*mDevice, *mAllocator))
		return false;

	if (!mPresentBackend->acquireImage(*mDevice, mCurrentFrameIndex, frame.getImageAvailableSemaphore(), mCurrentSwapchainImageIndex, mCurrentSwapchainImage))
	{
		frame.end(*mDevice);
		return false;
	}

	beginColorRendering(*mDevice, frame.getCommandBuffer(), *mWindowTexture, VK_ATTACHMENT_LOAD_OP_CLEAR, nullptr);

	FrameGlobals frameGlobals = {};
	writeIdentityProjection(frameGlobals);
	frameGlobals.mTimeSeconds = (float)SDL_GetTicks() / 1000.0f;
	frameGlobals.mWindowWidth = (float)currentExtent.x;
	frameGlobals.mWindowHeight = (float)currentExtent.y;
	frameGlobals.mGameWidth = (float)mGameResolution.x;
	frameGlobals.mGameHeight = (float)mGameResolution.y;
	if (frame.getGlobalsMappedData() != nullptr)
	{
		std::memcpy(frame.getGlobalsMappedData(), &frameGlobals, sizeof(frameGlobals));
	}
	mDescriptors->updateFrameSet(*mDevice, mCurrentFrameIndex);

	mFrameStarted = true;
	mFrameReadyToPresent = false;
	mRenderingActive = true;
	return true;
}

void vulkan::RendererBackend::beginColorPass(Texture& targetTexture, VkAttachmentLoadOp loadOp, Texture* depthTexture)
{
	if (!mFrameStarted || mDevice == nullptr)
		return;

	Frame& frame = mFrames[mCurrentFrameIndex];
	beginColorRendering(*mDevice, frame.getCommandBuffer(), targetTexture, loadOp, depthTexture);
	mRenderingActive = true;
}

void vulkan::RendererBackend::endRenderingPass()
{
	if (!mFrameStarted || !mRenderingActive || mDevice == nullptr)
		return;

	Frame& frame = mFrames[mCurrentFrameIndex];
	mDevice->vk().CmdEndRendering(frame.getCommandBuffer());
	mRenderingActive = false;
}

void vulkan::RendererBackend::endFrame()
{
	if (!mFrameStarted || mDevice == nullptr)
		return;

	Frame& frame = mFrames[mCurrentFrameIndex];
	if (mRenderingActive && frame.isRecording())
	{
		mDevice->vk().CmdEndRendering(frame.getCommandBuffer());
		mRenderingActive = false;
	}
	mFrameStarted = false;
	mFrameReadyToPresent = true;
}

void vulkan::RendererBackend::present()
{
	if (!mFrameReadyToPresent || mDevice == nullptr || mPresentBackend == nullptr || mWindowTexture == nullptr)
		return;

	Frame& frame = mFrames[mCurrentFrameIndex];
	if (!mPresentBackend->recordPresent(*mDevice, frame.getCommandBuffer(), mWindowTexture->getImage(), mWindowTexture->getLayout(), mWindowTexture->getSize(), mCurrentSwapchainImageIndex))
		return;

	mWindowTexture->setLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	frame.end(*mDevice);

	VkSemaphoreSubmitInfo waitSemaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	waitSemaphoreInfo.semaphore = frame.getImageAvailableSemaphore();
	waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	waitSemaphoreInfo.deviceIndex = 0;

	VkSemaphoreSubmitInfo signalSemaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	signalSemaphoreInfo.semaphore = frame.getRenderCompleteSemaphore();
	signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	signalSemaphoreInfo.deviceIndex = 0;

	VkCommandBufferSubmitInfo commandBufferSubmitInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
	commandBufferSubmitInfo.commandBuffer = frame.getCommandBuffer();

	VkSubmitInfo2 submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	submitInfo.waitSemaphoreInfoCount = 1;
	submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
	submitInfo.commandBufferInfoCount = 1;
	submitInfo.pCommandBufferInfos = &commandBufferSubmitInfo;
	submitInfo.signalSemaphoreInfoCount = 1;
	submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;

	const VkResult submitResult = mDevice->vk().QueueSubmit2(mDevice->getGraphicsQueue(), 1, &submitInfo, frame.getFence());
	if (submitResult != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkQueueSubmit2 failed with result " << (int)submitResult);
		return;
	}

	if (!mPresentBackend->queuePresent(*mDevice, mCurrentFrameIndex, mCurrentSwapchainImageIndex, frame.getRenderCompleteSemaphore()))
	{
		RMX_LOG_WARNING("Vulkan present requested a swapchain refresh");
	}

	mCurrentFrameIndex = (mCurrentFrameIndex + 1) % VULKAN_FRAMES_IN_FLIGHT;
	mCurrentSwapchainImage = VK_NULL_HANDLE;
	mFrameReadyToPresent = false;
	mRenderingActive = false;
}

bool vulkan::RendererBackend::presentTexture(Texture& sourceTexture, SDL_Window& window, bool useVSync)
{
	if (mDevice == nullptr || mAllocator == nullptr || mPresentBackend == nullptr || !sourceTexture.isValid())
		return false;

	mWindow = &window;
	if (mPresentBackend->isVSyncEnabled() != useVSync)
	{
		mPresentBackend->resize(*mDevice, window, sourceTexture, useVSync);
	}

	const Vec2i currentExtent = mPresentBackend->getExtent();
	int windowWidth = 0;
	int windowHeight = 0;
	getWindowSizeForRendering(window, windowWidth, windowHeight);
	if (currentExtent.x != std::max(windowWidth, 1) || currentExtent.y != std::max(windowHeight, 1))
	{
		mPresentBackend->resize(*mDevice, window, sourceTexture, useVSync);
	}

	Frame& frame = mFrames[mCurrentFrameIndex];
	if (!frame.begin(*mDevice, *mAllocator))
		return false;

	if (!mPresentBackend->acquireImage(*mDevice, mCurrentFrameIndex, frame.getImageAvailableSemaphore(), mCurrentSwapchainImageIndex, mCurrentSwapchainImage))
	{
		frame.end(*mDevice);
		return false;
	}

	if (!mPresentBackend->recordPresent(*mDevice, frame.getCommandBuffer(), sourceTexture.getImage(), sourceTexture.getLayout(), sourceTexture.getSize(), mCurrentSwapchainImageIndex))
	{
		frame.end(*mDevice);
		return false;
	}
	sourceTexture.setLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	frame.end(*mDevice);

	VkSemaphoreSubmitInfo waitSemaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	waitSemaphoreInfo.semaphore = frame.getImageAvailableSemaphore();
	waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	waitSemaphoreInfo.deviceIndex = 0;

	VkSemaphoreSubmitInfo signalSemaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	signalSemaphoreInfo.semaphore = frame.getRenderCompleteSemaphore();
	signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	signalSemaphoreInfo.deviceIndex = 0;

	VkCommandBufferSubmitInfo commandBufferSubmitInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
	commandBufferSubmitInfo.commandBuffer = frame.getCommandBuffer();

	VkSubmitInfo2 submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	submitInfo.waitSemaphoreInfoCount = 1;
	submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
	submitInfo.commandBufferInfoCount = 1;
	submitInfo.pCommandBufferInfos = &commandBufferSubmitInfo;
	submitInfo.signalSemaphoreInfoCount = 1;
	submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;

	const VkResult submitResult = mDevice->vk().QueueSubmit2(mDevice->getGraphicsQueue(), 1, &submitInfo, frame.getFence());
	if (submitResult != VK_SUCCESS)
	{
		RMX_LOG_ERROR("vkQueueSubmit2 failed with result " << (int)submitResult);
		return false;
	}

	if (!mPresentBackend->queuePresent(*mDevice, mCurrentFrameIndex, mCurrentSwapchainImageIndex, frame.getRenderCompleteSemaphore()))
	{
		RMX_LOG_WARNING("Vulkan present requested a swapchain refresh");
	}

	mCurrentFrameIndex = (mCurrentFrameIndex + 1) % VULKAN_FRAMES_IN_FLIGHT;
	mCurrentSwapchainImage = VK_NULL_HANDLE;
	mFrameReadyToPresent = false;
	mRenderingActive = false;
	return true;
}

void vulkan::RendererBackend::clearGameScreen()
{
	// The main offscreen target is cleared at the start of every frame via dynamic rendering loadOp = clear.
}

void vulkan::RendererBackend::renderGameScreen(const std::vector<Geometry*>& geometries)
{
	(void)geometries;
	// The real Vulkan game renderer is wired in through the Oxygen-facing VulkanRenderer/Drawer layers.
}

#endif
