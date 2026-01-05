#pragma once

#include <pch.h>

#include "hooking/cemu_hooks.h"
#include "rendering/d3d12.h"
#include "rendering/openxr.h"
#include "rendering/renderer.h"
#include "rendering/vulkan.h"
#include "utils/update_checker.h"

class VRManager {
public:
    static VRManager& instance() {
        static VRManager singletonInstance;
        return singletonInstance;
    }

    VRManager(VRManager const&) = delete;
    void operator=(VRManager const&) = delete;

    void Init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) {
        D3D12 = std::make_unique<RND_D3D12>();
        VK = std::make_unique<RND_Vulkan>(instance, physicalDevice, device);
        Log::print<INFO>("Initialized VRManager instance...");
        UpdateChecker::CheckForUpdates();
    }

    void InitSession() {
        XrGraphicsBindingD3D12KHR d3d12Binding = { XR_TYPE_GRAPHICS_BINDING_D3D12_KHR };
        d3d12Binding.device = D3D12->GetDevice();
        d3d12Binding.queue = D3D12->GetCommandQueue();
        XR->CreateSession(d3d12Binding);
        XR->CreateActions();
        Hooks = std::make_unique<CemuHooks>();
    }

    std::unique_ptr<OpenXR> XR;
    std::unique_ptr<RND_D3D12> D3D12;
    std::unique_ptr<RND_Vulkan> VK;
    std::unique_ptr<CemuHooks> Hooks;

    uint32_t vkVersion = 0;

private:
    VRManager() {
        m_logger = std::make_unique<Log>();
        XR = std::make_unique<OpenXR>();
    };

    ~VRManager() {
        // note: OpenXR gets to remove its swapchains first before D3D12 gets destroyed, so reverse that order
        VK.reset();
        XR.reset();
        D3D12.reset();

        m_logger.reset();
    };

    std::unique_ptr<Log> m_logger;
};