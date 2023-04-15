#include "swapchain.h"
#include "instance.h"

Swapchain::Swapchain(uint32_t width, uint32_t height, uint32_t sampleCount): m_width(width), m_height(height) {
    auto getBestSwapchainFormat = [](const std::vector<DXGI_FORMAT>& applicationSupportedFormats) -> DXGI_FORMAT {
        // Finds the first matching DXGI_FORMAT (int) that matches the int64 from OpenXR
        uint32_t swapchainCount = 0;
        xrEnumerateSwapchainFormats(VRManager::instance().XR->GetSession(), 0, &swapchainCount, nullptr);
        std::vector<int64_t> xrPreferredFormats(swapchainCount);
        xrEnumerateSwapchainFormats(VRManager::instance().XR->GetSession(), swapchainCount, &swapchainCount, xrPreferredFormats.data());

        auto found = std::find_first_of(std::begin(xrPreferredFormats), std::end(xrPreferredFormats), std::begin(applicationSupportedFormats), std::end(applicationSupportedFormats));
        if (found == std::end(xrPreferredFormats)) {
            throw std::runtime_error("OpenXR runtime doesn't support any of the presenting modes that the GPU drivers support.");
        }
        return (DXGI_FORMAT)*found;
    };

    const std::vector<DXGI_FORMAT> preferredColorFormats = {
        // fixme: check if OpenXR prefers sRGB or not
        //DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
    };
    m_format = getBestSwapchainFormat(preferredColorFormats);

    XrSwapchainCreateInfo swapchainCreateInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    swapchainCreateInfo.width = width;
    swapchainCreateInfo.height = height;
    swapchainCreateInfo.arraySize = 1;
    swapchainCreateInfo.sampleCount = sampleCount;
    swapchainCreateInfo.format = m_format;
    swapchainCreateInfo.mipCount = 1;
    swapchainCreateInfo.faceCount = 1;
    swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCreateInfo.createFlags = 0;
    checkXRResult(xrCreateSwapchain(VRManager::instance().XR->GetSession(), &swapchainCreateInfo, &m_swapchain), "Failed to create OpenXR swapchain images!");

    uint32_t swapchainImagesCount = 0;
    checkXRResult(xrEnumerateSwapchainImages(m_swapchain, 0, &swapchainImagesCount, NULL), "Failed to enumerate swapchain images!");
    std::vector<XrSwapchainImageD3D12KHR> swapchainImages(swapchainImagesCount, { XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR });
    checkXRResult(xrEnumerateSwapchainImages(m_swapchain, swapchainImagesCount, &swapchainImagesCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchainImages.data())), "Failed to enumerate swapchain images!");

    ID3D12Device* device = VRManager::instance().D3D12->GetDevice();
    ID3D12CommandQueue* queue = VRManager::instance().D3D12->GetCommandQueue();
    ComPtr<ID3D12CommandAllocator> allocator;
    checkHResult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Failed to create command allocator!");
    {
        RND_D3D12::CommandContext<true> formatSwapchains(device, queue, allocator.Get(), [this, &swapchainImages](ID3D12GraphicsCommandList* cmdList) {
            for (XrSwapchainImageD3D12KHR& image : swapchainImages) {
                // D3D12Utils::CreateConstantBuffer(D3D12_HEAP_TYPE_DEFAULT);
                m_swapchainTextures.emplace_back(image.texture);
            }
        });
    }
}

void Swapchain::PrepareRendering() {
    checkXRResult(xrAcquireSwapchainImage(m_swapchain, NULL, &m_swapchainImageIdx), "Can't acquire OpenXR swapchain image!");
}

ID3D12Resource* Swapchain::StartRendering() {
    XrSwapchainImageWaitInfo waitSwapchainInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    waitSwapchainInfo.timeout = XR_INFINITE_DURATION;
    if (XrResult waitResult = xrWaitSwapchainImage(m_swapchain, &waitSwapchainInfo); waitResult == XR_TIMEOUT_EXPIRED || XR_FAILED(waitResult)) {
        checkXRResult(waitResult, "Failed to wait for swapchain image!");
    }
    return m_swapchainTextures[m_swapchainImageIdx].Get();
}

void Swapchain::FinishRendering() {
    XrSwapchainImageReleaseInfo releaseSwapchainInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    checkXRResult(xrReleaseSwapchainImage(m_swapchain, &releaseSwapchainInfo), "Failed to release swapchain image!");
}


Swapchain::~Swapchain() {
    if (m_swapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(m_swapchain);
    }
}