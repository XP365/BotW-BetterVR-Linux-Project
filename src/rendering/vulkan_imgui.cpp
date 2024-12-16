#include "instance.h"
#include "vulkan.h"

RND_Vulkan::ImGuiOverlay::ImGuiOverlay(VkCommandBuffer cb, uint32_t width, uint32_t height, VkFormat format) {
    m_context = ImGui::CreateContext();

    // get queue
    VkQueue queue = nullptr;
    VRManager::instance().VK->GetDeviceDispatch()->GetDeviceQueue(VRManager::instance().VK->GetDevice(), 0, 0, &queue);

    // create descriptor pool
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 1000,
        .poolSizeCount = std::size(poolSizes),
        .pPoolSizes = poolSizes
    };
    checkVkResult(VRManager::instance().VK->GetDeviceDispatch()->CreateDescriptorPool(VRManager::instance().VK->GetDevice(), &poolInfo, nullptr, &m_descriptorPool), "Failed to create descriptor pool for ImGui");

    // create render pass to render imgui to a texture which we'll copy to the swapchain
    VkAttachmentDescription colorAttachment = {
        .format = format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkAttachmentReference colorAttachmentRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef
    };

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
        .dependencyFlags = 0
    };

    VkRenderPassCreateInfo renderPassInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency
    };
    checkVkResult(VRManager::instance().VK->GetDeviceDispatch()->CreateRenderPass(VRManager::instance().VK->GetDevice(), &renderPassInfo, nullptr, &m_renderPass), "Failed to create render pass for ImGui");

    // // initialize imgui for vulkan
    ImGui::GetIO().DisplaySize = ImVec2((float)width, (float)height);

    // load vulkan functions
    checkAssert(ImGui_ImplVulkan_LoadFunctions([](const char* funcName, void* data_queue) {
        VkDevice device = VRManager::instance().VK->GetDevice();
        PFN_vkVoidFunction addr = VRManager::instance().VK->GetDeviceDispatch()->GetDeviceProcAddr(device, funcName);

        if (addr == nullptr) {
            // Log::print("Failed to load function {}", funcName);
            addr = VRManager::instance().VK->GetInstanceDispatch()->GetInstanceProcAddr(VRManager::instance().VK->GetInstance(), funcName);
            Log::print("Loaded function {} at {} using instance", funcName, (void*)addr);
        }
        else {
            Log::print("Loaded function {} at {}", funcName, (void*)addr);
        }

        return addr;
    }), "Failed to load vulkan functions for ImGui");

    ImGui_ImplVulkan_InitInfo init_info = {
        .Instance = VRManager::instance().VK->m_instance,
        .PhysicalDevice = VRManager::instance().VK->m_physicalDevice,
        .Device = VRManager::instance().VK->m_device,
        .QueueFamily = 0,
        .Queue = queue,
        .DescriptorPool = m_descriptorPool,
        .RenderPass = m_renderPass,
        .MinImageCount = (uint32_t)m_framebuffers.size(),
        .ImageCount = (uint32_t)m_framebuffers.size(),
        .MSAASamples = VK_SAMPLE_COUNT_1_BIT,

        .UseDynamicRendering = false,

        .Allocator = nullptr,
        .CheckVkResultFn = nullptr,
        .MinAllocationSize = 1024 * 1024
    };
    checkAssert(ImGui_ImplVulkan_Init(&init_info), "Failed to initialize ImGui");

    for (auto& framebuffer : m_framebuffers) {
        framebuffer = std::make_unique<VulkanFramebuffer>(width, height, format, m_renderPass);
    }

    Log::print("Initializing fonts for ImGui...");
    ImGui_ImplVulkan_CreateFontsTexture(cb);
    Log::print("Fonts initialized for ImGui");

    // find HWND that starts with Cemu in its title
    struct EnumWindowsData {
        DWORD cemuPid;
        HWND outHwnd;
    } enumData = { GetCurrentProcessId(), NULL };

    EnumWindows([](HWND iteratedHwnd, LPARAM data) -> BOOL {
        EnumWindowsData* enumData = (EnumWindowsData*)data;
        DWORD currPid;
        GetWindowThreadProcessId(iteratedHwnd, &currPid);
        if (currPid == enumData->cemuPid) {
            constexpr size_t bufSize = 256;
            wchar_t buf[bufSize];
            GetWindowTextW(iteratedHwnd, buf, bufSize);
            if (wcsstr(buf, L"Cemu") != nullptr) {
                enumData->outHwnd = iteratedHwnd;
                return FALSE;
            }
        }
        return TRUE;
    }, (LPARAM)&enumData);
    m_cemuTopWindow = enumData.outHwnd;

    // find the most nested child window since that's the rendering window
    HWND iteratedHwnd = m_cemuTopWindow;
    while (true) {
        HWND nextIteratedHwnd = FindWindowExW(iteratedHwnd, NULL, NULL, NULL);
        if (nextIteratedHwnd == NULL) {
            break;
        }
        iteratedHwnd = nextIteratedHwnd;
    }
    m_cemuRenderWindow = iteratedHwnd;


    m_mainFramebuffer = std::make_unique<VulkanTexture>(width, height, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    m_hudFramebuffer = std::make_unique<VulkanTexture>(width, height, VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    // create sampler
    VkSamplerCreateInfo samplerInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = -1000.0f;
    samplerInfo.maxLod = 1000.0f;
    checkVkResult(VRManager::instance().VK->GetDeviceDispatch()->CreateSampler(VRManager::instance().VK->GetDevice(), &samplerInfo, nullptr, &m_sampler), "Failed to create sampler for ImGui");
}

RND_Vulkan::ImGuiOverlay::~ImGuiOverlay() {
    if (m_mainFramebufferDescriptorSet != VK_NULL_HANDLE)
        ImGui_ImplVulkan_RemoveTexture(m_mainFramebufferDescriptorSet);
    if (m_mainFramebuffer != nullptr)
        m_mainFramebuffer.reset();
    if (m_hudFramebufferDescriptorSet != VK_NULL_HANDLE)
        ImGui_ImplVulkan_RemoveTexture(m_hudFramebufferDescriptorSet);
    if (m_hudFramebuffer != nullptr)
        m_hudFramebuffer.reset();

    if (m_sampler != VK_NULL_HANDLE)
        VRManager::instance().VK->GetDeviceDispatch()->DestroySampler(VRManager::instance().VK->GetDevice(), m_sampler, nullptr);
    if (m_renderPass != VK_NULL_HANDLE)
        VRManager::instance().VK->GetDeviceDispatch()->DestroyRenderPass(VRManager::instance().VK->GetDevice(), m_renderPass, nullptr);
    if (m_descriptorPool != VK_NULL_HANDLE)
        VRManager::instance().VK->GetDeviceDispatch()->DestroyDescriptorPool(VRManager::instance().VK->GetDevice(), m_descriptorPool, nullptr);

    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext(m_context);
}

constexpr ImGuiWindowFlags FULLSCREEN_WINDOW_FLAGS = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;

void RND_Vulkan::ImGuiOverlay::BeginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    if (m_mainFramebufferDescriptorSet == VK_NULL_HANDLE) {
        m_mainFramebufferDescriptorSet = ImGui_ImplVulkan_AddTexture(m_sampler, m_mainFramebuffer->GetImageView(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    if (m_hudFramebufferDescriptorSet == VK_NULL_HANDLE) {
        m_hudFramebufferDescriptorSet = ImGui_ImplVulkan_AddTexture(m_sampler, m_hudFramebuffer->GetImageView(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }

    const bool shouldCrop3DTo16_9 = VRManager::instance().Hooks->GetSettings().cropFlatTo16x9Setting == 1;

    // calculate width minus the retina scaling
    ImVec2 windowSize = ImGui::GetIO().DisplaySize;
    windowSize.x = windowSize.x / ImGui::GetIO().DisplayFramebufferScale.x;
    windowSize.y = windowSize.y / ImGui::GetIO().DisplayFramebufferScale.y;

    // center position using aspect ratio
    ImVec2 centerPos = ImVec2((windowSize.x - windowSize.y * m_mainFramebufferAspectRatio) / 2, 0);
    ImVec2 squishedWindowSize = ImVec2(windowSize.y * m_mainFramebufferAspectRatio, windowSize.y);

    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize);
        ImGui::Begin("HUD Background", nullptr, FULLSCREEN_WINDOW_FLAGS);
        ImGui::Image((ImTextureID)m_hudFramebufferDescriptorSet, windowSize);
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleVar();
    }

    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(shouldCrop3DTo16_9 ? ImVec2(0, 0) : centerPos);
        ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize);
        ImGui::Begin("3D Background", nullptr, FULLSCREEN_WINDOW_FLAGS);

        ImVec2 croppedUv0 = ImVec2(0.0f, 0.0f);
        ImVec2 croppedUv1 = ImVec2(1.0f, 1.0f);
        if (shouldCrop3DTo16_9) {
            ImVec2 displaySize = ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y / 2 / m_mainFramebufferAspectRatio);
            ImVec2 displayOffset = ImVec2(ImGui::GetIO().DisplaySize.x / 2 - (displaySize.x / 2), ImGui::GetIO().DisplaySize.y / 2 - (displaySize.y / 2));
            ImVec2 textureSize = ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);

            croppedUv0 = ImVec2(displayOffset.x / textureSize.x, displayOffset.y / textureSize.y);
            croppedUv1 = ImVec2((displayOffset.x + displaySize.x) / textureSize.x, (displayOffset.y + displaySize.y) / textureSize.y);
        }

        ImGui::Image((ImTextureID)m_mainFramebufferDescriptorSet, shouldCrop3DTo16_9 ? windowSize : squishedWindowSize, croppedUv0, croppedUv1);
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleVar();
    }

    // ImGui::ShowDemoWindow();

    ImGui::Begin("Entity Inspector");

    // ImGui::BeginListBox("Entities");
    for (auto& [entity, values] : m_entities) {
        ImGui::Text(entity.c_str());
        ImGui::PushID(entity.c_str());

        // ImGui::BeginGroup();
        for (auto& value : values) {
            // ImGui::InputInt("Address", (int*)&value.value_address);
            std::visit([&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, uint32_t>) {
                    ImGui::InputInt(value.value_name.c_str(), (int*)&arg);
                }
                else if constexpr (std::is_same_v<T, float>) {
                    ImGui::InputFloat(value.value_name.c_str(), &arg);
                }
                else if constexpr (std::is_same_v<T, XrVector3f>) {
                    ImGui::InputFloat3(value.value_name.c_str(), &arg.x);
                }
                else if constexpr (std::is_same_v<T, std::string>) {
                    ImGui::Text(value.value_name.c_str());
                }
            }, value.value);
        }
        // ImGui::EndGroup();
        ImGui::PopID();

        ImGui::Separator();
    }
    // ImGui::EndListBox();

    ImGui::End();
}

void RND_Vulkan::ImGuiOverlay::Draw3DLayerAsBackground(VkCommandBuffer cb, VkImage srcImage, float aspectRatio) {
    m_mainFramebuffer->vkPipelineBarrier(cb);
    m_mainFramebuffer->vkTransitionLayout(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    m_mainFramebuffer->vkCopyFromImage(cb, srcImage);
    m_mainFramebuffer->vkPipelineBarrier(cb);
    m_mainFramebuffer->vkTransitionLayout(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_mainFramebufferAspectRatio = aspectRatio;
}

void RND_Vulkan::ImGuiOverlay::DrawHUDLayerAsBackground(VkCommandBuffer cb, VkImage srcImage) {
    m_hudFramebuffer->vkPipelineBarrier(cb);
    m_hudFramebuffer->vkTransitionLayout(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    m_hudFramebuffer->vkCopyFromImage(cb, srcImage);
    m_hudFramebuffer->vkPipelineBarrier(cb);
    m_hudFramebuffer->vkTransitionLayout(cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void RND_Vulkan::ImGuiOverlay::Render() {
    ImGui::Render();
}

void RND_Vulkan::ImGuiOverlay::DrawOverlayToImage(VkCommandBuffer cb, VkImage destImage) {
    auto* dispatch = VRManager::instance().VK->GetDeviceDispatch();

    // transition framebuffer to color attachment
    m_framebuffers[m_framebufferIdx]->vkTransitionLayout(cb, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    m_framebuffers[m_framebufferIdx]->vkPipelineBarrier(cb);

    // start render pass
    VkClearValue clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } };
    VkRenderPassBeginInfo renderPassInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = m_renderPass,
        .framebuffer = m_framebuffers[m_framebufferIdx]->GetFramebuffer(),
        .renderArea = {
            .offset = { 0, 0 },
            .extent = { (uint32_t)ImGui::GetIO().DisplaySize.x, (uint32_t)ImGui::GetIO().DisplaySize.y }
        },
        .clearValueCount = 1,
        .pClearValues = &clearValue
    };
    dispatch->CmdBeginRenderPass(cb, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // render imgui
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);

    // end render pass
    dispatch->CmdEndRenderPass(cb);

    // transition framebuffer to now be a transfer source
    m_framebuffers[m_framebufferIdx]->vkPipelineBarrier(cb);
    m_framebuffers[m_framebufferIdx]->vkTransitionLayout(cb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    m_framebuffers[m_framebufferIdx]->vkCopyToImage(cb, destImage);
    m_framebuffers[m_framebufferIdx]->vkPipelineBarrier(cb);

    m_framebufferIdx++;
    if (m_framebufferIdx >= m_framebuffers.size())
        m_framebufferIdx = 0;
}


void RND_Vulkan::ImGuiOverlay::UpdateControls() {
    POINT p;
    GetCursorPos(&p);

    ScreenToClient(m_cemuRenderWindow, &p);

    // scale mouse position with the texture size
    uint32_t framebufferWidth = (uint32_t)ImGui::GetIO().DisplaySize.x;
    uint32_t framebufferHeight = (uint32_t)ImGui::GetIO().DisplaySize.y;

    // calculate how many client side pixels are used on the border since its not a 16:9 aspect ratio
    RECT rect;
    GetClientRect(m_cemuRenderWindow, &rect);
    uint32_t nonCenteredWindowWidth = rect.right - rect.left;
    uint32_t nonCenteredWindowHeight = rect.bottom - rect.top;

    // calculate window size without black bars due to a non-16:9 aspect ratio
    uint32_t windowWidth = nonCenteredWindowWidth;
    uint32_t windowHeight = nonCenteredWindowHeight;
    if (nonCenteredWindowWidth * 9 > nonCenteredWindowHeight * 16) {
        windowWidth = nonCenteredWindowHeight * 16 / 9;
    }
    else {
        windowHeight = nonCenteredWindowWidth * 9 / 16;
    }

    // calculate the black bars
    uint32_t blackBarWidth = (nonCenteredWindowWidth - windowWidth) / 2;
    uint32_t blackBarHeight = (nonCenteredWindowHeight - windowHeight) / 2;

    // the actual window is centered, so add offsets to both x and y on both sides
    p.x = p.x - blackBarWidth;
    p.y = p.y - blackBarHeight;

    ImGui::GetIO().DisplayFramebufferScale = ImVec2((float)framebufferWidth / (float)windowWidth, (float)framebufferHeight / (float)windowHeight);

    // update mouse state depending on if the window is focused
    if (GetForegroundWindow() != m_cemuTopWindow) {
        ImGui::GetIO().MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
        ImGui::GetIO().MouseDown[0] = false;
        ImGui::GetIO().MouseDown[1] = false;
        ImGui::GetIO().MouseDown[2] = false;
    }
    else {
        ImGui::GetIO().MousePos = ImVec2((float)p.x, (float)p.y);
        ImGui::GetIO().MouseDown[0] = GetAsyncKeyState(VK_LBUTTON) & 0x8000;
        ImGui::GetIO().MouseDown[1] = GetAsyncKeyState(VK_RBUTTON) & 0x8000;
        ImGui::GetIO().MouseDown[2] = GetAsyncKeyState(VK_MBUTTON) & 0x8000;
    }
}

// Memory Viewer/Editor

void RND_Vulkan::ImGuiOverlay::AddEntity(std::string entity, std::string name, uint32_t address, ValueVariant& value) {
    if (m_entities.find(entity) == m_entities.end()) {
        m_entities[entity] = {};
    }

    m_entities[entity].emplace_back(name, address, value);
}

void RND_Vulkan::ImGuiOverlay::RemoveEntity(std::string entity) {
    m_entities.erase(entity);
}