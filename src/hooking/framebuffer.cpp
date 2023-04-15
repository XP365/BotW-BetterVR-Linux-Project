#include "framebuffer.h"
#include "instance.h"
#include "layer.h"
#include "utils/d3d12_utils.h"

std::array<CaptureTexture, 1> captureTextures = {
    { false, { 0, 0 }, VK_NULL_HANDLE, VK_FORMAT_A2B10G10R10_UNORM_PACK32, { 1280, 720 }, nullptr, VK_NULL_HANDLE }
};
std::atomic_size_t foundResolutions = std::size(captureTextures);

std::mutex lockImageResolutions;
std::unordered_map<VkImage, std::pair<VkExtent2D, VkFormat>> imageResolutions;

using namespace VRLayer;

VkResult VkDeviceOverrides::CreateImage(const vkroots::VkDeviceDispatch* pDispatch, VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) {
    VkResult res = pDispatch->CreateImage(device, pCreateInfo, pAllocator, pImage);
    if (foundResolutions > 0) {
        lockImageResolutions.lock();
        imageResolutions.try_emplace(*pImage, std::make_pair(VkExtent2D{ pCreateInfo->extent.width, pCreateInfo->extent.height }, pCreateInfo->format));
        lockImageResolutions.unlock();
    }
    return res;
}

void VkDeviceOverrides::DestroyImage(const vkroots::VkDeviceDispatch* pDispatch, VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) {
    pDispatch->DestroyImage(device, image, pAllocator);
    if (foundResolutions > 0) {
        lockImageResolutions.lock();
        imageResolutions.erase(image);
        lockImageResolutions.unlock();
    }
}

void VkDeviceOverrides::CmdClearColorImage(const vkroots::VkDeviceDispatch* pDispatch, VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) {
    // check for magical clear values
    if (pColor->float32[1] >= 0.12 && pColor->float32[1] <= 0.13 && pColor->float32[2] >= 0.97 && pColor->float32[2] <= 0.99) {
        // r value in magical clear value is the capture idx after rounding down
        const long captureIdx = std::lroundf(pColor->float32[0]);
        auto& capture = captureTextures[captureIdx];

        capture.foundImage = image;
        checkAssert(capture.captureCmdBuffer == VK_NULL_HANDLE, "This texture already got captured in a previous command buffer, but never got submitted!");

        // initialize textures if not already done
        if (!capture.initialized) {
            lockImageResolutions.lock();

            auto it = imageResolutions.find(image);
            checkAssert(it != imageResolutions.end(), "Couldn't find the resolution for an image. Is the graphic pack not active?");

            capture.initialized = true;
            capture.foundSize = it->second.first;
            checkAssert(capture.format == it->second.second, std::format("[WARNING] Got {} as VkFormat instead of the expected {}", it->second.second, capture.format).c_str());

            capture.sharedTexture = std::make_unique<SharedTexture>(capture.foundSize.width, capture.foundSize.height, capture.format, D3D12Utils::ToDXGIFormat(capture.format));
            //capture.d3d12Texture = std::make_unique<Texture>(capture.foundSize.width, capture.foundSize.height, D3D12Utils::ToDXGIFormat(capture.format));
            imageResolutions.erase(it);
            foundResolutions--;
            Log::print("Found capture texture {}: res={}x{}, format={}", captureIdx, capture.foundSize.width, capture.foundSize.height, capture.format);

            ComPtr<ID3D12CommandAllocator> cmdAllocator;
            {
                ID3D12Device* d3d12Device = VRManager::instance().D3D12->GetDevice();
                ID3D12CommandQueue* d3d12Queue = VRManager::instance().D3D12->GetCommandQueue();
                d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocator));

                RND_D3D12::CommandContext<true> transitionInitialTextures(d3d12Device, d3d12Queue, cmdAllocator.Get(), [&capture](ID3D12GraphicsCommandList* cmdList) {
                    cmdList->SetName(L"transitionInitialTextures");
                    capture.sharedTexture->d3d12TransitionLayout(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
                    capture.sharedTexture->d3d12SignalFence(2);
                });
            }

            // fixme: this could just as well be a blocking CommandContext
            uint64_t fenceValue = 2;

            VkSemaphoreWaitInfo waitInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
            waitInfo.semaphoreCount = 1;
            waitInfo.pSemaphores = &capture.sharedTexture->GetSemaphore();
            waitInfo.pValues = &fenceValue;
            pDispatch->WaitSemaphores(VRManager::instance().VK->GetDevice(), &waitInfo, UINT64_MAX);

            VkSemaphoreSignalInfo signalInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO };
            signalInfo.semaphore = capture.sharedTexture->GetSemaphore();
            signalInfo.value = 0;
            pDispatch->SignalSemaphore(VRManager::instance().VK->GetDevice(), &signalInfo);

            lockImageResolutions.unlock();
        }

        capture.sharedTexture->CopyFromVkImage(commandBuffer, image);
        capture.captureCmdBuffer = commandBuffer;
        VRManager::instance().XR->GetRenderer()->Render(capture.sharedTexture.get());
        return;
    }
    else {
        return pDispatch->CmdClearColorImage(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
    }
}

VkResult VkDeviceOverrides::EndCommandBuffer(const vkroots::VkDeviceDispatch* pDispatch, VkCommandBuffer commandBuffer) {
    return pDispatch->EndCommandBuffer(commandBuffer);
}

VkResult VkDeviceOverrides::QueueSubmit(const vkroots::VkDeviceDispatch* pDispatch, VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) {
    // insert pipeline barriers if an active capture is ongoing
    bool activeCapture = std::any_of(captureTextures.begin(), captureTextures.end(), [](const CaptureTexture& capture) { return capture.captureCmdBuffer != VK_NULL_HANDLE; });

    struct ModifiedSubmitInfo_t {
        std::vector<VkSemaphore> waitSemaphores;
        std::vector<uint64_t> timelineWaitValues;
        std::vector<VkPipelineStageFlags> waitDstStageMasks;
        std::vector<VkSemaphore> signalSemaphores;
        std::vector<uint64_t> timelineSignalValues;

        VkTimelineSemaphoreSubmitInfo timelineSemaphoreSubmitInfo = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    };

    if (activeCapture) {
        std::vector<ModifiedSubmitInfo_t> modifiedSubmitInfos(submitCount);
        for (uint32_t i = 0; i < submitCount; i++) {
            const VkSubmitInfo& submitInfo = pSubmits[i];
            ModifiedSubmitInfo_t& modifiedSubmitInfo = modifiedSubmitInfos[i];

            // copy old semaphores into new vectors
            for (uint32_t j = 0; j < submitInfo.waitSemaphoreCount; j++) {
                modifiedSubmitInfo.waitSemaphores.emplace_back(submitInfo.pWaitSemaphores[j]);
                modifiedSubmitInfo.waitDstStageMasks.emplace_back(submitInfo.pWaitDstStageMask[j]);
                modifiedSubmitInfo.timelineWaitValues.emplace_back(0);
            }

            for (uint32_t j = 0; j < submitInfo.signalSemaphoreCount; j++) {
                modifiedSubmitInfo.signalSemaphores.emplace_back(submitInfo.pSignalSemaphores[j]);
                modifiedSubmitInfo.timelineSignalValues.emplace_back(0);
            }

            // find timeline semaphore submit info if already present
            const VkTimelineSemaphoreSubmitInfo* timelineSemaphoreSubmitInfoPtr = &modifiedSubmitInfo.timelineSemaphoreSubmitInfo;

            const VkBaseInStructure* pNextIt = static_cast<const VkBaseInStructure*>(submitInfo.pNext);
            while (pNextIt) {
                if (pNextIt->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
                    timelineSemaphoreSubmitInfoPtr = reinterpret_cast<const VkTimelineSemaphoreSubmitInfo*>(pNextIt);
                    break;
                }
                pNextIt = pNextIt->pNext;
            }
            if (pNextIt == nullptr)
                const_cast<VkSubmitInfo&>(submitInfo).pNext = &modifiedSubmitInfo.timelineSemaphoreSubmitInfo;

            // copy any existing timeline values into new vectors
            for (uint32_t j = 0; j < timelineSemaphoreSubmitInfoPtr->waitSemaphoreValueCount; j++) {
                modifiedSubmitInfo.timelineWaitValues.emplace_back(timelineSemaphoreSubmitInfoPtr->pWaitSemaphoreValues[j]);
            }
            for (uint32_t j = 0; j < timelineSemaphoreSubmitInfoPtr->signalSemaphoreValueCount; j++) {
                modifiedSubmitInfo.timelineSignalValues.emplace_back(timelineSemaphoreSubmitInfoPtr->pSignalSemaphoreValues[j]);
            }

            // insert timeline semaphores
            for (uint32_t j = 0; j < submitInfo.commandBufferCount; j++) {
                for (auto& capture : captureTextures) {
                    if (submitInfo.pCommandBuffers[j] == capture.captureCmdBuffer) {
                        // wait for D3D12/XR to finish with previous shared texture render
                        modifiedSubmitInfo.waitSemaphores.emplace_back(capture.sharedTexture->GetSemaphore());
                        modifiedSubmitInfo.waitDstStageMasks.emplace_back(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
                        modifiedSubmitInfo.timelineWaitValues.emplace_back(0);

                        // signal to D3D12/XR rendering that the shared texture can be rendered to VR headset
                        modifiedSubmitInfo.signalSemaphores.emplace_back(capture.sharedTexture->GetSemaphore());
                        modifiedSubmitInfo.timelineSignalValues.emplace_back(1);
                        capture.captureCmdBuffer = VK_NULL_HANDLE;
                    }
                }
            }

            // update the VkTimelineSemaphoreSubmitInfo struct
            const_cast<VkTimelineSemaphoreSubmitInfo*>(timelineSemaphoreSubmitInfoPtr)->waitSemaphoreValueCount = (uint32_t)modifiedSubmitInfo.timelineWaitValues.size();
            const_cast<VkTimelineSemaphoreSubmitInfo*>(timelineSemaphoreSubmitInfoPtr)->pWaitSemaphoreValues = modifiedSubmitInfo.timelineWaitValues.data();
            const_cast<VkTimelineSemaphoreSubmitInfo*>(timelineSemaphoreSubmitInfoPtr)->signalSemaphoreValueCount = (uint32_t)modifiedSubmitInfo.timelineSignalValues.size();
            const_cast<VkTimelineSemaphoreSubmitInfo*>(timelineSemaphoreSubmitInfoPtr)->pSignalSemaphoreValues = modifiedSubmitInfo.timelineSignalValues.data();

            // add wait and signal semaphores to the submit info
            const_cast<VkSubmitInfo&>(submitInfo).waitSemaphoreCount = modifiedSubmitInfo.waitSemaphores.size();
            const_cast<VkSubmitInfo&>(submitInfo).pWaitSemaphores = modifiedSubmitInfo.waitSemaphores.data();
            const_cast<VkSubmitInfo&>(submitInfo).pWaitDstStageMask = modifiedSubmitInfo.waitDstStageMasks.data();
            const_cast<VkSubmitInfo&>(submitInfo).signalSemaphoreCount = modifiedSubmitInfo.signalSemaphores.size();
            const_cast<VkSubmitInfo&>(submitInfo).pSignalSemaphores = modifiedSubmitInfo.signalSemaphores.data();
        }

        return pDispatch->QueueSubmit(queue, submitCount, pSubmits, fence);
    }
    else {
        return pDispatch->QueueSubmit(queue, submitCount, pSubmits, fence);
    }
}

VkResult VkDeviceOverrides::QueuePresentKHR(const vkroots::VkDeviceDispatch* pDispatch, VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    VRManager::instance().XR->ProcessEvents();
    if (VRManager::instance().XR->GetRenderer()) {
        VRManager::instance().XR->GetRenderer()->EndFrame();
        VRManager::instance().XR->GetRenderer()->StartFrame();
    }

    return pDispatch->QueuePresentKHR(queue, pPresentInfo);
}