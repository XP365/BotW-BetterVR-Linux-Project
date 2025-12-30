#include "layer.h"
#include "instance.h"

const std::vector<std::string> additionalInstanceExtensions = {};

VkResult VRLayer::VkInstanceOverrides::CreateInstance(PFN_vkCreateInstance createInstanceFunc, const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
    // Modify VkInstance with needed extensions
    std::vector<const char*> modifiedExtensions;
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        modifiedExtensions.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
    }
    for (const std::string& extension : additionalInstanceExtensions) {
        if (std::find(modifiedExtensions.begin(), modifiedExtensions.end(), extension) == modifiedExtensions.end()) {
            modifiedExtensions.push_back(extension.c_str());
        }
    }

    //SetEnvironmentVariableA("DISABLE_VULKAN_OBS_CAPTURE", "1");

    //VkInstanceCreateInfo modifiedCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    //modifiedCreateInfo.pNext = pCreateInfo->pNext;
    //modifiedCreateInfo.flags = pCreateInfo->flags;
    //modifiedCreateInfo.pApplicationInfo = pCreateInfo->pApplicationInfo;
    //modifiedCreateInfo.enabledLayerCount = pCreateInfo->enabledLayerCount;
    //modifiedCreateInfo.ppEnabledLayerNames = pCreateInfo->ppEnabledLayerNames;
    //modifiedCreateInfo.enabledExtensionCount = (uint32_t)modifiedExtensions.size();
    //modifiedCreateInfo.ppEnabledExtensionNames = modifiedExtensions.data();

    const_cast<VkInstanceCreateInfo*>(pCreateInfo)->enabledExtensionCount = (uint32_t)modifiedExtensions.size();
    const_cast<VkInstanceCreateInfo*>(pCreateInfo)->ppEnabledExtensionNames = modifiedExtensions.data();

    VkResult result = createInstanceFunc(pCreateInfo, pAllocator, pInstance);

    VRManager::instance().vkVersion = pCreateInfo->pApplicationInfo->apiVersion;

    Log::print<INFO>("Created Vulkan instance (using Vulkan {}.{}.{}) successfully!", VK_API_VERSION_MAJOR(pCreateInfo->pApplicationInfo->apiVersion), VK_API_VERSION_MINOR(pCreateInfo->pApplicationInfo->apiVersion), VK_API_VERSION_PATCH(pCreateInfo->pApplicationInfo->apiVersion));
    checkAssert(VK_VERSION_MINOR(pCreateInfo->pApplicationInfo->apiVersion) != 0 || VK_VERSION_MAJOR(pCreateInfo->pApplicationInfo->apiVersion) > 1, "Vulkan version needs to be v1.1 or higher!");
    return result;
}

void VRLayer::VkInstanceOverrides::DestroyInstance(const vkroots::VkInstanceDispatch* pDispatch, VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    return pDispatch->DestroyInstance(instance, pAllocator);
}


VkResult VRLayer::VkInstanceOverrides::EnumeratePhysicalDevices(const vkroots::VkInstanceDispatch* pDispatch, VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) {
    // Proceed to get all devices
    uint32_t internalCount = 0;
    checkVkResult(pDispatch->EnumeratePhysicalDevices(instance, &internalCount, nullptr), "Failed to retrieve number of vulkan physical devices!");
    std::vector<VkPhysicalDevice> internalDevices(internalCount);
    checkVkResult(pDispatch->EnumeratePhysicalDevices(instance, &internalCount, internalDevices.data()), "Failed to retrieve vulkan physical devices!");

    for (const VkPhysicalDevice& device : internalDevices) {
        VkPhysicalDeviceIDProperties deviceId = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
        VkPhysicalDeviceProperties2 properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        properties.pNext = &deviceId;
        pDispatch->GetPhysicalDeviceProperties2(device, &properties);

        if (deviceId.deviceLUIDValid && memcmp(&VRManager::instance().XR->m_capabilities.adapter, deviceId.deviceLUID, VK_LUID_SIZE) == 0) {
            if (pPhysicalDevices != nullptr) {
                if (*pPhysicalDeviceCount < 1) {
                    *pPhysicalDeviceCount = 1;
                    return VK_INCOMPLETE;
                }
                *pPhysicalDeviceCount = 1;
                pPhysicalDevices[0] = device;
                return VK_SUCCESS;
            }
            else {
                *pPhysicalDeviceCount = 1;
                return VK_SUCCESS;
            }
        }
    }
    *pPhysicalDeviceCount = 0;
    return VK_SUCCESS;
}

// Some layers (OBS vulkan layer) will skip the vkEnumeratePhysicalDevices hook
// Therefor we also override vkGetPhysicalDeviceProperties to make any non-compatible VkPhysicalDevice use Vulkan 1.0 which Cemu won't list due to it being too low
void VRLayer::VkInstanceOverrides::GetPhysicalDeviceProperties(const vkroots::VkInstanceDispatch* pDispatch, VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) {
    // Do original query
    pDispatch->GetPhysicalDeviceProperties(physicalDevice, pProperties);

    // Do a seperate internal query to make sure that we also query the LUID
    {
        VkPhysicalDeviceIDProperties deviceId = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
        VkPhysicalDeviceProperties2 properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        properties.pNext = &deviceId;
        pDispatch->GetPhysicalDeviceProperties2(physicalDevice, &properties);

        if (deviceId.deviceLUIDValid && memcmp(&VRManager::instance().XR->m_capabilities.adapter, deviceId.deviceLUID, VK_LUID_SIZE) != 0) {
            pProperties->apiVersion = VK_API_VERSION_1_0;
        }
    }
}

// Some layers (OBS vulkan layer) will skip the vkEnumeratePhysicalDevices hook
// Therefor we also override vkGetPhysicalDeviceQueueFamilyProperties to make any non-VR-compatible VkPhysicalDevice have 0 queues
void VRLayer::VkInstanceOverrides::GetPhysicalDeviceQueueFamilyProperties(const vkroots::VkInstanceDispatch* pDispatch, VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) {
    // Check whether this VkPhysicalDevice matches the LUID that OpenXR returns
    VkPhysicalDeviceIDProperties deviceId = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
    VkPhysicalDeviceProperties2 properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    properties.pNext = &deviceId;
    pDispatch->GetPhysicalDeviceProperties2(physicalDevice, &properties);

    if (deviceId.deviceLUIDValid && memcmp(&VRManager::instance().XR->m_capabilities.adapter, deviceId.deviceLUID, VK_LUID_SIZE) != 0) {
        *pQueueFamilyPropertyCount = 0;
        return;
    }

    return pDispatch->GetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

const std::vector<std::string> additionalDeviceExtensions = {
    VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
    VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
    VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
#if ENABLE_VK_ROBUSTNESS
    VK_EXT_DEVICE_FAULT_EXTENSION_NAME,
    VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
    VK_EXT_IMAGE_ROBUSTNESS_EXTENSION_NAME
#endif
};

VkResult VRLayer::VkInstanceOverrides::CreateDevice(const vkroots::VkInstanceDispatch* pDispatch, VkPhysicalDevice gpu, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    // Modify VkDevice with needed extensions
    std::vector<const char*> modifiedExtensions;
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        modifiedExtensions.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
    }
    for (const std::string& extension : additionalDeviceExtensions) {
        if (std::find(modifiedExtensions.begin(), modifiedExtensions.end(), extension) == modifiedExtensions.end()) {
            modifiedExtensions.push_back(extension.c_str());
        }
    }

    // Test if timeline semaphores are already enabled
    bool timelineSemaphoresEnabled = false;
    bool imageRobustnessEnabled = false;
    bool robustness2Enabled = false;
    const void* current_pNext = pCreateInfo->pNext;
    while (current_pNext) {
        const VkBaseInStructure* base = static_cast<const VkBaseInStructure*>(current_pNext);
        if (base->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES) {
            timelineSemaphoresEnabled = true;
        }
#if ENABLE_VK_ROBUSTNESS
        if (base->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ROBUSTNESS_FEATURES) {
            imageRobustnessEnabled = true;
        }
        if (base->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT) {
            robustness2Enabled = true;
        }
#endif
        current_pNext = base->pNext;
    }

    VkPhysicalDeviceTimelineSemaphoreFeatures createSemaphoreFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES };
    createSemaphoreFeatures.timelineSemaphore = true;

    VkPhysicalDeviceImageRobustnessFeatures createImageRobustnessFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ROBUSTNESS_FEATURES };
    createImageRobustnessFeatures.robustImageAccess = true;

    VkPhysicalDeviceRobustness2FeaturesEXT createRobustness2Features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
    createRobustness2Features.robustBufferAccess2 = true;
    createRobustness2Features.robustImageAccess2 = true;
    createRobustness2Features.nullDescriptor = true;

    void* nextChain = const_cast<void*>(pCreateInfo->pNext);

#if ENABLE_VK_ROBUSTNESS
    if (!robustness2Enabled) {
        createRobustness2Features.pNext = nextChain;
        nextChain = &createRobustness2Features;
    }

    if (!imageRobustnessEnabled) {
        createImageRobustnessFeatures.pNext = nextChain;
        nextChain = &createImageRobustnessFeatures;
    }
#endif

    if (!timelineSemaphoresEnabled) {
        createSemaphoreFeatures.pNext = nextChain;
        nextChain = &createSemaphoreFeatures;
    }

    VkDeviceCreateInfo modifiedCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    modifiedCreateInfo.pNext = nextChain;
    modifiedCreateInfo.flags = pCreateInfo->flags;
    modifiedCreateInfo.queueCreateInfoCount = pCreateInfo->queueCreateInfoCount;
    modifiedCreateInfo.pQueueCreateInfos = pCreateInfo->pQueueCreateInfos;
    modifiedCreateInfo.enabledLayerCount = pCreateInfo->enabledLayerCount;
    modifiedCreateInfo.ppEnabledLayerNames = pCreateInfo->ppEnabledLayerNames;
    modifiedCreateInfo.enabledExtensionCount = (uint32_t)modifiedExtensions.size();
    modifiedCreateInfo.ppEnabledExtensionNames = modifiedExtensions.data();

    VkResult result = pDispatch->CreateDevice(gpu, &modifiedCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS) {
        Log::print<ERROR>("Failed to create Vulkan device! Error {}", result);
        return result;
    }

    // Initialize VRManager late if neither vkEnumeratePhysicalDevices and vkGetPhysicalDeviceProperties were called and used to filter the device
    if (!VRManager::instance().VK) {
        Log::print<WARNING>("Wasn't able to filter OpenXR-compatible devices for this instance!");
        Log::print<WARNING>("You might encounter an error if you've selected a GPU that's not connected to the VR headset in Cemu's settings. Usually this error is fine as long as this is the case.");
        Log::print<WARNING>("This issue appears due to OBS's Vulkan layer being installed which skips some calls used to hide GPUs that aren't compatible with your VR headset.");
    }

    return result;
}

void VRLayer::VkDeviceOverrides::DestroyDevice(const vkroots::VkDeviceDispatch* pDispatch, VkDevice device, const VkAllocationCallbacks* pAllocator) {
    return pDispatch->DestroyDevice(device, pAllocator);
}

VKROOTS_DEFINE_LAYER_INTERFACES(VRLayer::VkInstanceOverrides, VRLayer::VkPhysicalDeviceOverrides, VRLayer::VkDeviceOverrides);

// todo: These methods aren't required since we already use the negotiatelayerinterface function, thus can be removed for simplicity
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL VRLayer_GetInstanceProcAddr(VkInstance instance, const char* pName) {
    return vkroots::GetInstanceProcAddr<VRLayer::VkInstanceOverrides, VRLayer::VkPhysicalDeviceOverrides, VRLayer::VkDeviceOverrides>(instance, pName);
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL VRLayer_GetDeviceProcAddr(VkDevice device, const char* pName) {
    return vkroots::GetDeviceProcAddr<VRLayer::VkInstanceOverrides, VRLayer::VkPhysicalDeviceOverrides, VRLayer::VkDeviceOverrides>(device, pName);
}