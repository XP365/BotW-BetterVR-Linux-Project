#include "framebuffer.h"
#include "instance.h"
#include "layer.h"
#include "utils/vulkan_utils.h"


std::mutex lockImageResolutions;
std::unordered_map<VkImage, std::pair<VkExtent2D, VkFormat>> imageResolutions;

std::mutex s_activeCopyMutex;
std::vector<std::pair<VkCommandBuffer, SharedTexture*>> s_activeCopyOperations;

VkImage s_curr3DColorImage = VK_NULL_HANDLE;
VkImage s_curr3DDepthImage = VK_NULL_HANDLE;

using namespace VRLayer;

VkResult VkDeviceOverrides::CreateImage(const vkroots::VkDeviceDispatch& pDispatch, VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) {
    VkResult res = pDispatch.CreateImage(device, pCreateInfo, pAllocator, pImage);

    if (pCreateInfo->extent.width >= 1280 && pCreateInfo->extent.height >= 720) {
        lockImageResolutions.lock();
        checkAssert(imageResolutions.try_emplace(*pImage, std::make_pair(VkExtent2D{ pCreateInfo->extent.width, pCreateInfo->extent.height }, pCreateInfo->format)).second, "Couldn't insert image resolution into map!");
        lockImageResolutions.unlock();
    }
    return res;
}

void VkDeviceOverrides::DestroyImage(const vkroots::VkDeviceDispatch& pDispatch, VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) {
    lockImageResolutions.lock();
    imageResolutions.erase(image);
    if (s_curr3DColorImage == image) {
        s_curr3DColorImage = VK_NULL_HANDLE;
    }
    else if (s_curr3DDepthImage == image) {
        s_curr3DDepthImage = VK_NULL_HANDLE;
    }
    lockImageResolutions.unlock();

    pDispatch.DestroyImage(device, image, pAllocator);
}

void VkDeviceOverrides::CmdClearColorImage(const vkroots::VkCommandBufferDispatch& pDispatch, VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) {
    // check whether the magic values are there, and which order they are in to determine which eye
    OpenXR::EyeSide side = (OpenXR::EyeSide)-1;
    if (pColor->float32[1] >= 0.12 && pColor->float32[1] <= 0.13 && pColor->float32[2] >= 0.97 && pColor->float32[2] <= 0.99) {
        side = OpenXR::EyeSide::LEFT;
    }
    else if (pColor->float32[2] >= 0.12 && pColor->float32[2] <= 0.13 && pColor->float32[1] >= 0.97 && pColor->float32[1] <= 0.99) {
        side = OpenXR::EyeSide::RIGHT;
    }

    if (!VRManager::instance().VK) {
        auto* dispatch = pDispatch.pDeviceDispatch;
        VRManager::instance().Init(dispatch->pPhysicalDeviceDispatch->pInstanceDispatch->Instance, dispatch->PhysicalDevice, dispatch->Device);
        VRManager::instance().InitSession();
    }

    if (side != (OpenXR::EyeSide)-1) {
        // r value in magical clear value is the capture idx after rounding down
        const long captureIdx = std::lroundf(pColor->float32[0] * 32.0f);
        const long frameIdx = pColor->float32[3] < 0.5f ? 0 : 1;
        checkAssert(captureIdx == 0 || captureIdx == 2, "Invalid capture index!");

        Log::print<RENDERING>("[{}] Clearing color image for {} layer for {} side", frameIdx, captureIdx == 0 ? "3D" : "2D", side == OpenXR::EyeSide::LEFT ? "left" : "right");

        auto* renderer = VRManager::instance().XR->GetRenderer();
        if (!renderer) {
            Log::print<RENDERING>("Renderer is not initialized yet!");
            return pDispatch.CmdClearColorImage(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
        }
        auto& layer3D = renderer->m_layer3D;
        auto& layer2D = renderer->m_layer2D;
        auto& imguiOverlay = renderer->m_imguiOverlay;

        // initialize the textures of both 2D and 3D layer if either is found since they share the same VkImage and resolution
        if (captureIdx == 0 || captureIdx == 2) {
            if (!layer2D) {
                lockImageResolutions.lock();
                if (const auto it = imageResolutions.find(image); it != imageResolutions.end()) {
                    auto viewConfs = VRManager::instance().XR->GetViewConfigurations();

                    VkExtent2D swapchainRes = it->second.first;
                    if (VRManager::instance().XR->m_capabilities.isMetaSimulator) {
                        swapchainRes = VkExtent2D{ viewConfs[0].recommendedImageRectWidth, viewConfs[0].recommendedImageRectHeight };
                    }

                    layer3D = std::make_unique<RND_Renderer::Layer3D>(it->second.first, swapchainRes);
                    layer2D = std::make_unique<RND_Renderer::Layer2D>(it->second.first, swapchainRes);

                    Log::print<INFO>("Found rendering resolution {}x{} @ {} using capture #{}", it->second.first.width, it->second.first.height, it->second.second, captureIdx);
                    imguiOverlay = std::make_unique<RND_Renderer::ImGuiOverlay>(commandBuffer, it->second.first.width, it->second.first.height, VK_FORMAT_A2B10G10R10_UNORM_PACK32);
                    if (CemuHooks::GetSettings().ShowDebugOverlay()) {
                        VRManager::instance().Hooks->m_entityDebugger = std::make_unique<EntityDebugger>();
                    }
                }
                else {
                    checkAssert(false, "Couldn't find image resolution in map!");
                }
                lockImageResolutions.unlock();
            }
        }

        if (!VRManager::instance().XR->GetRenderer()->IsInitialized()) {
            return;
        }

        checkAssert(layer3D && layer2D, "Couldn't find 3D or 2D layer!");

        // change source image to GENERAL layout
        VulkanUtils::TransitionLayout(commandBuffer, image, imageLayout, VK_IMAGE_LAYOUT_GENERAL);
        VulkanUtils::DebugPipelineBarrier(commandBuffer);

        auto returnToLayout = [&]() {
            VulkanUtils::TransitionLayout(commandBuffer, image, VK_IMAGE_LAYOUT_GENERAL, imageLayout);
            VulkanUtils::DebugPipelineBarrier(commandBuffer);
        };

        // 3D layer - color texture for 3D rendering
        if (captureIdx == 0) {
            // check if the color texture has the appropriate texture format
            if (s_curr3DColorImage == VK_NULL_HANDLE) {
                lockImageResolutions.lock();
                if (const auto it = imageResolutions.find(image); it != imageResolutions.end()) {
                    if (it->second.second == VK_FORMAT_B10G11R11_UFLOAT_PACK32) {
                        s_curr3DColorImage = it->first;
                    }
                }
                lockImageResolutions.unlock();
            }

            // don't clear the image if we're in the faux 2D mode
            if (CemuHooks::UseBlackBarsDuringEvents()) {
                return;
            }

            if (image != s_curr3DColorImage) {
                Log::print<RENDERING>("Color image is not the same as the current 3D color image! ({} != {})", (void*)image, (void*)s_curr3DColorImage);
                VkClearColorValue clearColor;
                if (VRManager::instance().XR->GetRenderer()->IsRendering3D(frameIdx)) {
                    clearColor = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
                }
                else {
                    clearColor = {{ 0.0f, 0.0f, 0.0f, 1.0f }};
                }
                returnToLayout();
                return pDispatch.CmdClearColorImage(commandBuffer, image, imageLayout, &clearColor, rangeCount, pRanges);
            }

            if (renderer->GetFrame(frameIdx).copiedColor[side]) {
                // the color texture has already been copied to the layer
                Log::print<RENDERING>("A 3D color texture is already been copied for the current frame!");

                VkClearColorValue clearColor = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
                returnToLayout();
                return pDispatch.CmdClearColorImage(commandBuffer, image, imageLayout, &clearColor, rangeCount, pRanges);
            }

            // note: This uses vkCmdCopyImage to copy the image to the D3D12-created interop texture. s_activeCopyOperations queues a semaphore for the D3D12 side to wait on.
            SharedTexture* texture = layer3D->CopyColorToLayer(side, commandBuffer, image, frameIdx);
            renderer->On3DColorCopied(side, frameIdx);

            {
                std::lock_guard lk(s_activeCopyMutex);
                s_activeCopyOperations.emplace_back(commandBuffer, texture);
            }

            // imgui needs only one eye to render Cemu's 2D output, so use right side since it looks better
            if (side == EyeSide::RIGHT) {
                // note: Uses vkCmdCopyImage to copy the (right-eye-only) image to the imgui overlay's texture
                float aspectRatio = layer3D->GetAspectRatio(side);
                imguiOverlay->Draw3DLayerAsBackground(commandBuffer, image, aspectRatio, frameIdx);
            }

            // clear the image to be transparent to allow for the HUD to be rendered on top of it which results in a transparent HUD layer
            VkClearColorValue clearColor = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
            returnToLayout();
            return pDispatch.CmdClearColorImage(commandBuffer, image, imageLayout, &clearColor, rangeCount, pRanges);
        }

        // 2D layer - color texture for HUD rendering
        if (captureIdx == 2) {
            bool hudCopied = renderer->GetFrame(frameIdx).copied2D;

            if (side == OpenXR::EyeSide::LEFT) {
                if (hudCopied) {
                    // the 2D texture has already been copied to the layer
                    Log::print<RENDERING>("A 2D texture has already been copied for the current frame!");

                    VkClearColorValue clearColor = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
                    returnToLayout();
                    return pDispatch.CmdClearColorImage(commandBuffer, image, imageLayout, &clearColor, rangeCount, pRanges);
                }
                else {
                    // provide the HUD texture to the imgui overlay we'll use to recomposite Cemu's original flatscreen rendering
                    if (imguiOverlay && !hudCopied) {
                        imguiOverlay->DrawHUDLayerAsBackground(commandBuffer, image, frameIdx);
                        VulkanUtils::DebugPipelineBarrier(commandBuffer);
                    }

                    if (imguiOverlay && !hudCopied) {
                        // render imgui, and then copy the framebuffer to the 2D layer
                        imguiOverlay->BeginFrame(frameIdx, false);
                        imguiOverlay->Update();
                        imguiOverlay->Render();
                        imguiOverlay->DrawAndCopyToImage(commandBuffer, image, frameIdx);
                        VulkanUtils::DebugPipelineBarrier(commandBuffer);
                    }

                    // copy the HUD texture to D3D12 to be presented
                    // only copy the first attempt at capturing when GX2ClearColor is called with this capture index since the game/Cemu clears the 2D layer twice
                    SharedTexture* texture = layer2D->CopyColorToLayer(commandBuffer, image, frameIdx);
                    renderer->On2DCopied(frameIdx);

                    {
                        std::lock_guard lk(s_activeCopyMutex);
                        s_activeCopyOperations.emplace_back(commandBuffer, texture);
                    }
                }
            }
            if (side == OpenXR::EyeSide::RIGHT) {
                // render the imgui overlay on the right side
                if (imguiOverlay) {
                    // render imgui, and then copy the framebuffer to the 2D layer
                    imguiOverlay->BeginFrame(frameIdx, true);
                    imguiOverlay->Update();
                    imguiOverlay->Render();
                    imguiOverlay->DrawAndCopyToImage(commandBuffer, image, frameIdx);
                    return;
                }

                if (hudCopied) {
                    // AMD GPU FIX: Use local VkClearColorValue instead of const_cast to avoid UB
                    VkClearColorValue clearColor = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
                    return pDispatch.CmdClearColorImage(commandBuffer, image, imageLayout, &clearColor, rangeCount, pRanges);
                }
            }
        }
        return;
    }
    else {
        return pDispatch.CmdClearColorImage(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
    }
}

void VkDeviceOverrides::CmdClearDepthStencilImage(const vkroots::VkCommandBufferDispatch& pDispatch, VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) {
    // check for magical clear values
    // check order and whether there's a match with the magical clear value
    OpenXR::EyeSide side = (OpenXR::EyeSide)-1;
    if (pDepthStencil->depth >= 0.011456789 && pDepthStencil->depth <= 0.013456789) { // 0.0123456789
        side = OpenXR::EyeSide::LEFT;
    }
    else if (pDepthStencil->depth >= 0.153987654 && pDepthStencil->depth <= 0.173987654) { // 0.163987654
        side = OpenXR::EyeSide::RIGHT;
    }

    if (rangeCount == 1 && side != (OpenXR::EyeSide)-1) {
        // stencil value is the frame counter
        const uint32_t frameCounter = pDepthStencil->stencil;
        checkAssert(frameCounter == 0 || frameCounter == 1, "Invalid frame counter for depth clear!");

        auto& layer3D = VRManager::instance().XR->GetRenderer()->m_layer3D;
        auto& layer2D = VRManager::instance().XR->GetRenderer()->m_layer2D;

        if (!VRManager::instance().XR->GetRenderer()->IsInitialized()) {
            return;
        }

        Log::print<RENDERING>("[{}] Clearing depth image for 3D layer for {} side", frameCounter, side == OpenXR::EyeSide::LEFT ? "left" : "right");

        // change source image to GENERAL layout
        VulkanUtils::TransitionLayout(commandBuffer, image, imageLayout, VK_IMAGE_LAYOUT_GENERAL);
        VulkanUtils::DebugPipelineBarrier(commandBuffer);

        auto returnToLayout = [&]() {
            VulkanUtils::TransitionLayout(commandBuffer, image, VK_IMAGE_LAYOUT_GENERAL, imageLayout);
            VulkanUtils::DebugPipelineBarrier(commandBuffer);
        };

        if (side == OpenXR::EyeSide::LEFT || side == OpenXR::EyeSide::RIGHT) {
            // 3D layer - depth texture for 3D rendering
            if (s_curr3DDepthImage == VK_NULL_HANDLE) {
                lockImageResolutions.lock();
                if (const auto it = imageResolutions.find(image); it != imageResolutions.end()) {
                    if (it->second.second == VK_FORMAT_D32_SFLOAT) {
                        s_curr3DDepthImage = it->first;
                    }
                }
                lockImageResolutions.unlock();
            }

            if (image != s_curr3DDepthImage) {
                Log::print<RENDERING>("Depth image is not the same as the current 3D depth image! ({} != {})", (void*)image, (void*)s_curr3DDepthImage);
                returnToLayout();
                return;
            }

            if (VRManager::instance().XR->GetRenderer()->GetFrame(frameCounter).copiedDepth[side]) {
                // the depth texture has already been copied to the layer
                Log::print<RENDERING>("A depth texture is already bound for the current frame!");
                returnToLayout();
                return;
            }

            // if (layer3D.GetStatus() == Status3D::LEFT_BINDING_DEPTH || layer3D.GetStatus() == Status3D::RIGHT_BINDING_DEPTH) {
            //     // seems to always be the case whenever closing the (inventory) menu
            //     Log::print("A depth texture is already bound for the current frame!");
            //     return;
            // }
            //
            // checkAssert(layer3D.GetStatus() == Status3D::LEFT_BINDING_COLOR || layer3D.GetStatus() == Status3D::RIGHT_BINDING_COLOR, "3D layer is not in the correct state for capturing depth images!");



            SharedTexture* texture = layer3D->CopyDepthToLayer(side, commandBuffer, image, frameCounter);
            VRManager::instance().XR->GetRenderer()->On3DDepthCopied(side, frameCounter);

            {
                std::lock_guard lk(s_activeCopyMutex);
                s_activeCopyOperations.emplace_back(commandBuffer, texture);
            }
            returnToLayout();
            return;
        }
    }
    else {
        return pDispatch.CmdClearDepthStencilImage(commandBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges);
    }
}

VkResult VkDeviceOverrides::QueueSubmit(const vkroots::VkQueueDispatch& pDispatch, VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) {
    VkResult result = VK_SUCCESS;
    
    size_t activeCopyCount;
    {
        std::lock_guard lk(s_activeCopyMutex);
        activeCopyCount = s_activeCopyOperations.size();
    }

    if (activeCopyCount == 0) {
        result = pDispatch.QueueSubmit(queue, submitCount, pSubmits, fence);
    }
    else {
        struct ModifiedSubmitInfo_t {
            VkSubmitInfo submitInfoCopy; // Shadow copy of VkSubmitInfo
            std::vector<VkSemaphore> waitSemaphores;
            std::vector<uint64_t> timelineWaitValues;
            std::vector<VkPipelineStageFlags> waitDstStageMasks;
            std::vector<VkSemaphore> signalSemaphores;
            std::vector<uint64_t> timelineSignalValues;

            VkTimelineSemaphoreSubmitInfo timelineSemaphoreSubmitInfo = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
        };

        // insert (possible) pipeline barriers for any active copy operations
        std::vector<ModifiedSubmitInfo_t> modifiedSubmitInfos{ submitCount };
        std::vector<VkSubmitInfo> shadowSubmits{ submitCount };

        std::lock_guard lk(s_activeCopyMutex);

        for (uint32_t i = 0; i < submitCount; i++) {
            const VkSubmitInfo& submitInfo = pSubmits[i];
            ModifiedSubmitInfo_t& modifiedSubmitInfo = modifiedSubmitInfos[i];

            // AMD GPU FIX: Create shadow copy of original VkSubmitInfo
            modifiedSubmitInfo.submitInfoCopy = submitInfo;

            // copy old semaphores into new vectors
            modifiedSubmitInfo.waitSemaphores.assign(submitInfo.pWaitSemaphores, submitInfo.pWaitSemaphores + submitInfo.waitSemaphoreCount);
            modifiedSubmitInfo.waitDstStageMasks.assign(submitInfo.pWaitDstStageMask, submitInfo.pWaitDstStageMask + submitInfo.waitSemaphoreCount);
            modifiedSubmitInfo.timelineWaitValues.resize(submitInfo.waitSemaphoreCount, 0);

            modifiedSubmitInfo.signalSemaphores.assign(submitInfo.pSignalSemaphores, submitInfo.pSignalSemaphores + submitInfo.signalSemaphoreCount);
            modifiedSubmitInfo.timelineSignalValues.resize(submitInfo.signalSemaphoreCount, 0);

            // find timeline semaphore submit info if already present
            const VkTimelineSemaphoreSubmitInfo* existingTimelineInfo = nullptr;

            const VkBaseInStructure* pNextIt = static_cast<const VkBaseInStructure*>(submitInfo.pNext);
            while (pNextIt) {
                if (pNextIt->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
                    existingTimelineInfo = reinterpret_cast<const VkTimelineSemaphoreSubmitInfo*>(pNextIt);
                    break;
                }
                pNextIt = pNextIt->pNext;
            }

            // copy any existing timeline values into new vectors
            if (existingTimelineInfo) {
                for (uint32_t j = 0; j < existingTimelineInfo->waitSemaphoreValueCount; j++) {
                    modifiedSubmitInfo.timelineWaitValues[j] = existingTimelineInfo->pWaitSemaphoreValues[j];
                }
                for (uint32_t j = 0; j < existingTimelineInfo->signalSemaphoreValueCount; j++) {
                    modifiedSubmitInfo.timelineSignalValues[j] = existingTimelineInfo->pSignalSemaphoreValues[j];
                }
            }

            // Insert timeline semaphores for active copy operations
            for (uint32_t j = 0; j < submitInfo.commandBufferCount; j++) {
                for (auto it = s_activeCopyOperations.begin(); it != s_activeCopyOperations.end();) {
                    if (submitInfo.pCommandBuffers[j] == it->first) {
                        // Wait for D3D12/XR to finish with the previous shared texture render
                        uint64_t waitValue = it->second->GetVulkanWaitValue();
                        modifiedSubmitInfo.waitSemaphores.emplace_back(it->second->GetSemaphoreForWait(waitValue));
                        modifiedSubmitInfo.waitDstStageMasks.emplace_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
                        modifiedSubmitInfo.timelineWaitValues.emplace_back(waitValue);

                        // Signal to D3D12/XR rendering that the shared texture can be rendered to VR headset
                        uint64_t signalValue = it->second->GetVulkanSignalValue();
                        modifiedSubmitInfo.signalSemaphores.emplace_back(it->second->GetSemaphoreForSignal(signalValue));
                        modifiedSubmitInfo.timelineSignalValues.emplace_back(signalValue);
                        it = s_activeCopyOperations.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
            }

            // Update timeline semaphore submit info
            modifiedSubmitInfo.timelineSemaphoreSubmitInfo.waitSemaphoreValueCount = (uint32_t)modifiedSubmitInfo.timelineWaitValues.size();
            modifiedSubmitInfo.timelineSemaphoreSubmitInfo.pWaitSemaphoreValues = modifiedSubmitInfo.timelineWaitValues.data();
            modifiedSubmitInfo.timelineSemaphoreSubmitInfo.signalSemaphoreValueCount = (uint32_t)modifiedSubmitInfo.timelineSignalValues.size();
            modifiedSubmitInfo.timelineSemaphoreSubmitInfo.pSignalSemaphoreValues = modifiedSubmitInfo.timelineSignalValues.data();

            // AMD GPU FIX: Preserve existing pNext chain - prepend our timeline struct
            modifiedSubmitInfo.timelineSemaphoreSubmitInfo.pNext = submitInfo.pNext;

            modifiedSubmitInfo.submitInfoCopy.pNext = &modifiedSubmitInfo.timelineSemaphoreSubmitInfo;
            modifiedSubmitInfo.submitInfoCopy.waitSemaphoreCount = (uint32_t)modifiedSubmitInfo.waitSemaphores.size();
            modifiedSubmitInfo.submitInfoCopy.pWaitSemaphores = modifiedSubmitInfo.waitSemaphores.data();
            modifiedSubmitInfo.submitInfoCopy.pWaitDstStageMask = modifiedSubmitInfo.waitDstStageMasks.data();
            modifiedSubmitInfo.submitInfoCopy.signalSemaphoreCount = (uint32_t)modifiedSubmitInfo.signalSemaphores.size();
            modifiedSubmitInfo.submitInfoCopy.pSignalSemaphores = modifiedSubmitInfo.signalSemaphores.data();

            shadowSubmits[i] = modifiedSubmitInfo.submitInfoCopy;
        }
        result = pDispatch.QueueSubmit(queue, submitCount, shadowSubmits.data(), fence);
    }

    if (result != VK_SUCCESS) {
        Log::print<ERROR>("QueueSubmit failed with error {}", result);
    }

    return result;
}

VkResult VkDeviceOverrides::QueuePresentKHR(const vkroots::VkQueueDispatch& pDispatch, VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    VRManager::instance().XR->ProcessEvents();

    auto* renderer = VRManager::instance().XR->GetRenderer();
    if (renderer && renderer->m_layer3D && renderer->m_layer2D && renderer->m_imguiOverlay) {
        if (renderer->IsInitialized()) {
            renderer->EndFrame();
        }
        renderer->StartFrame();
    }

    return pDispatch.QueuePresentKHR(queue, pPresentInfo);
}
