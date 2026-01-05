#include "instance.h"
#include "vulkan.h"
#include "hooking/entity_debugger.h"
#include "utils/vulkan_utils.h"

RND_Renderer::ImGuiOverlay::ImGuiOverlay(VkCommandBuffer cb, uint32_t width, uint32_t height, VkFormat format) {
    ImGui::CreateContext();
    ImPlot3D::CreateContext();
    ImPlot::CreateContext();

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
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
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
    checkAssert(ImGui_ImplVulkan_LoadFunctions(VRManager::instance().vkVersion, [](const char* funcName, void* data_queue) {
        VkInstance instance = VRManager::instance().VK->GetInstance();
        VkDevice device = VRManager::instance().VK->GetDevice();
        PFN_vkVoidFunction addr = VRManager::instance().VK->GetDeviceDispatch()->GetDeviceProcAddr(device, funcName);

        if (addr == nullptr) {
            addr = VRManager::instance().VK->GetInstanceDispatch()->GetInstanceProcAddr(instance, funcName);
            Log::print<VERBOSE>("Loaded function {} at {} using instance", funcName, (void*)addr);
        }
        else {
            Log::print<VERBOSE>("Loaded function {} at {}", funcName, (void*)addr);
        }

        return addr;
    }), "Failed to load vulkan functions for ImGui");

    ImGui_ImplVulkan_InitInfo init_info = {
        .Instance = VRManager::instance().VK->GetInstance(),
        .PhysicalDevice = VRManager::instance().VK->GetPhysicalDevice(),
        .Device = VRManager::instance().VK->GetDevice(),
        .QueueFamily = 0,
        .Queue = queue,
        .DescriptorPool = m_descriptorPool,
        .RenderPass = m_renderPass,
        .MinImageCount = 6,
        .ImageCount = 6,
        .MSAASamples = VK_SAMPLE_COUNT_1_BIT,

        .UseDynamicRendering = false,

        .Allocator = nullptr,
        .CheckVkResultFn = nullptr,
        .MinAllocationSize = 1024 * 1024
    };
    checkAssert(ImGui_ImplVulkan_Init(&init_info), "Failed to initialize ImGui");

    auto* renderer = VRManager::instance().XR->GetRenderer();
    for (int i = 0; i < 2; ++i) {
        renderer->GetFrame(i).imguiFramebuffer = std::make_unique<VulkanFramebuffer>(width, height, format, m_renderPass);
    }

    Log::print<VERBOSE>("Initializing font textures for ImGui...");
    ImGui_ImplVulkan_CreateFontsTexture();

    // find HWND that starts with Cemu in its title
    struct EnumWindowsData {
        DWORD cemuPid;
        HWND outHwnd;
    } enumData = { .cemuPid = GetCurrentProcessId(), .outHwnd = NULL };

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

    for (int i = 0; i < 2; ++i) {
        auto& frame = renderer->GetFrame(i);
        frame.mainFramebuffer = std::make_unique<VulkanTexture>(width, height, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false);
        frame.hudFramebuffer = std::make_unique<VulkanTexture>(width, height, VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false);
        frame.hudWithoutAlphaFramebuffer = std::make_unique<VulkanTexture>(width, height, VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, true);

        frame.mainFramebuffer->vkTransitionLayout(cb, VK_IMAGE_LAYOUT_GENERAL);
        frame.mainFramebuffer->vkClear(cb, { 0.0f, 0.0f, 0.0f, 0.0f });

        frame.hudFramebuffer->vkTransitionLayout(cb, VK_IMAGE_LAYOUT_GENERAL);
        frame.hudFramebuffer->vkClear(cb, { 0.0f, 0.0f, 0.0f, 0.0f });

        frame.hudWithoutAlphaFramebuffer->vkTransitionLayout(cb, VK_IMAGE_LAYOUT_GENERAL);
        frame.hudWithoutAlphaFramebuffer->vkClear(cb, { 0.0f, 0.0f, 0.0f, 0.0f });
    }

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

RND_Renderer::ImGuiOverlay::~ImGuiOverlay() {
    auto* renderer = VRManager::instance().XR->GetRenderer();
    for (int i = 0; i < 2; ++i) {
        auto& frame = renderer->GetFrame(i);
        if (frame.mainFramebufferDS != VK_NULL_HANDLE)
            ImGui_ImplVulkan_RemoveTexture(frame.mainFramebufferDS);
        if (frame.mainFramebuffer != nullptr)
            frame.mainFramebuffer.reset();
        if (frame.hudFramebufferDS != VK_NULL_HANDLE)
            ImGui_ImplVulkan_RemoveTexture(frame.hudFramebufferDS);
        if (frame.hudFramebuffer != nullptr)
            frame.hudFramebuffer.reset();
        if (frame.hudWithoutAlphaFramebufferDS != VK_NULL_HANDLE)
            ImGui_ImplVulkan_RemoveTexture(frame.hudWithoutAlphaFramebufferDS);
        if (frame.hudWithoutAlphaFramebuffer != nullptr)
            frame.hudWithoutAlphaFramebuffer.reset();
        if (frame.imguiFramebuffer != nullptr)
            frame.imguiFramebuffer.reset();
    }

    if (m_sampler != VK_NULL_HANDLE)
        VRManager::instance().VK->GetDeviceDispatch()->DestroySampler(VRManager::instance().VK->GetDevice(), m_sampler, nullptr);
    if (m_renderPass != VK_NULL_HANDLE)
        VRManager::instance().VK->GetDeviceDispatch()->DestroyRenderPass(VRManager::instance().VK->GetDevice(), m_renderPass, nullptr);
    if (m_descriptorPool != VK_NULL_HANDLE)
        VRManager::instance().VK->GetDeviceDispatch()->DestroyDescriptorPool(VRManager::instance().VK->GetDevice(), m_descriptorPool, nullptr);

    ImGui_ImplVulkan_Shutdown();
    ImPlot::DestroyContext();
    ImPlot3D::DestroyContext();
    ImGui::DestroyContext();
}

constexpr ImGuiWindowFlags FULLSCREEN_WINDOW_FLAGS = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;

void DrawFPSOverlay(RND_Renderer* renderer) {
    ImGui::SetNextWindowBgAlpha(0.6f);

    // Use DisplaySize/FramebufferScale so positioning matches the same coordinate space as the overlay.
    ImVec2 windowSize = ImGui::GetIO().DisplaySize;
    windowSize.x = windowSize.x / ImGui::GetIO().DisplayFramebufferScale.x;
    windowSize.y = windowSize.y / ImGui::GetIO().DisplayFramebufferScale.y;

    const ImVec2 pad(10.0f, 10.0f);
    ImGui::SetNextWindowPos(ImVec2(windowSize.x - pad.x, pad.y), ImGuiCond_Always, ImVec2(1.0f, 0.0f));

    if (ImGui::Begin("AppMS Overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove)) {
        const float predictedDisplayPeriodMs = (float)renderer->GetPredictedDisplayPeriodMs();
        const float predictedHz = predictedDisplayPeriodMs > 0.0f ? (1000.0f / predictedDisplayPeriodMs) : 0.0f;

        const float appMs = (float)renderer->GetLastFrameTimeMs(); // Total frame time (includes wait)
        const float workMs = (float)renderer->GetLastFrameWorkTimeMs(); // GPU Work time only (excludes wait)
        const float waitMs = (float)renderer->GetLastWaitTimeMs();
        const float overheadMs = (float)renderer->GetLastOverheadMs();

        // --- 2. Convert to FPS ---
        const float appFps = appMs > 0.0000001f ? (1000.0f / appMs) : 0.0f;

        // "Theoretical FPS": How fast you COULD run if you didn't have to wait for V-Sync/OpenXR
        const float workFps = workMs >= 0.0000001f ? (1000.0f / workMs) : 0.0f;

        // Calculate percentage of the frame budget used (still useful in % terms)
        const float workPct = predictedDisplayPeriodMs > 0.0f ? (workMs / predictedDisplayPeriodMs) * 100.0f : 0.0f;

        // --- 3. Text Summary ---
        ImGui::Text("Your headset is %.0f Hz", predictedHz);
        ImGui::Text("Currently Running At %.1f FPS", appFps);
        ImGui::Text("");
        ImGui::Text("OpenXR waited %.1f ms so that it can interpolate/have low latency.", waitMs);
        ImGui::Text("Theoretically, it'd run at %.1f FPS if that didn't matter", workFps);

        if (predictedHz > 0.0f && workFps > 0.0f) {
            auto rateForDivisor = [predictedHz](int divisor) -> double {
                return divisor > 0 ? (predictedHz / (double)divisor) : 0.0;
            };

            auto chooseBestDivisor = [&](double fps) -> int {
                // Pick the closest *supported* refresh divisor (1x, 1/2x, 1/3x, 1/4x).
                int bestDiv = 1;
                double bestErr = std::abs(fps - rateForDivisor(1));
                for (int div = 2; div <= 4; ++div) {
                    const double err = std::abs(fps - rateForDivisor(div));
                    if (err < bestErr) {
                        bestErr = err;
                        bestDiv = div;
                    }
                }
                return bestDiv;
            };

            // Use theoretical FPS (GPU work time) as the basis for which step we're closest to.
            const int currentDiv = chooseBestDivisor(workFps);
            const double currentTarget = rateForDivisor(currentDiv);
            const int nextDiv = std::max(1, currentDiv - 1);
            const double nextTarget = rateForDivisor(nextDiv);
            const double missingNext = std::max(0.0, nextTarget - (double)workFps);

            if (currentDiv == 1) {
                ImGui::Text("Its reaching the full refresh rate you've set (%.0f hz)", currentTarget);
                ImGui::Text("You've got ~%.1f FPS of headroom to spare", (float)std::max(0.0, (double)workFps - nextTarget));
            }
            else {
                ImGui::Text("It is however able to reliably reach %.0f FPS (%.0f Hz / %d)", currentTarget, predictedHz, currentDiv);
                ImGui::Text("You'd need to get %.1f FPS more to get it to switch to %.0f FPS", missingNext, nextTarget);
            }
        }

        // --- 4. History Buffers (Storing FPS now) ---
        static float history_app_fps[120] = {};
        static float history_work_fps[120] = {};
        static int offset = 0;

        history_app_fps[offset] = appFps;
        history_work_fps[offset] = workFps;
        offset = (offset + 1) % 120;

        // --- 5. Plotting ---
        const double targetFps = predictedHz;
        const double halfFps = predictedHz / 2.0;
        const double thirdFps = predictedHz / 3.0;
        const double fourthFps = predictedHz / 4.0;
        if (ImPlot::BeginPlot("##Frametime", ImVec2(420, 150), ImPlotFlags_NoFrame | ImPlotFlags_NoTitle | ImPlotFlags_NoMouseText | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoInputs)) {
            ImPlot::SetupAxes(nullptr, "##FPS", ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoInitialFit);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0, 120, ImPlotCond_Always);

            if (targetFps >= 0.000000001f) {
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, predictedHz * 1.5f, ImPlotCond_Always);

                // 1. Target Refresh Rate (Green)
                ImPlot::SetNextLineStyle(ImVec4(0, 1, 0, 0.5f));
                ImPlot::PlotInfLines("##Target", &targetFps, 1, ImPlotInfLinesFlags_Horizontal);
                ImPlot::TagY(targetFps, ImVec4(0, 1, 0, 0.5f), "%.0f Hz", targetFps);

                // 2. Half Rate (ASW/Reprojection threshold) (Yellow)
                ImPlot::SetNextLineStyle(ImVec4(1, 1, 0, 0.5f));
                ImPlot::PlotInfLines("##1/2 Rate", &halfFps, 1, ImPlotInfLinesFlags_Horizontal);
                ImPlot::TagY(halfFps, ImVec4(1, 1, 0, 0.5f), "%.0f Hz", halfFps);

                // 3. Third Rate (Red)
                ImPlot::SetNextLineStyle(ImVec4(1, 0, 0, 0.5f));
                ImPlot::PlotInfLines("##1/3 Rate", &thirdFps, 1, ImPlotInfLinesFlags_Horizontal);
            }
            else {
                ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 144.0, ImPlotCond_Always);
            }

            // --- Draw Graphs ---
            // 1. Theoretical Max FPS (Work Time) - Purple/Pink
            ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.4f, 1.0f, 1.0f));
            ImPlot::PlotLine("Theoretical Max", history_work_fps, 120, 1.0, 0.0, 0, offset);

            // 2. Actual FPS (App Time) - Blue
            // This represents what is actually hitting the screen (capped by Wait).
            ImPlot::SetNextFillStyle(ImVec4(0.4f, 0.4f, 1.0f, 0.50f));
            ImPlot::SetNextLineStyle(ImVec4(0.4f, 0.4f, 1.0f, 1.0f));
            ImPlot::PlotShaded("Actual", history_app_fps, 120, 0.0, 1.0, 0.0, 0, offset);
            ImPlot::PlotLine("##TotalLine", history_app_fps, 120, 1.0, 0.0, 0, offset);

            // Current FPS Tag
            if (appFps > 0.0f) {
                const double currentFps = appFps;
                ImPlot::SetNextLineStyle(ImVec4(1, 1, 1, 0.65f));
                ImPlot::PlotInfLines("##Current", &currentFps, 1, ImPlotInfLinesFlags_Horizontal);
                ImPlot::TagY(currentFps, ImVec4(1, 1, 1, 0.8f), "%.1f FPS", currentFps);
            }

            ImPlot::EndPlot();
        }
    }
    ImGui::End();
}

void RND_Renderer::ImGuiOverlay::BeginFrame(long frameIdx, bool renderBackground) {
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    auto* renderer = VRManager::instance().XR->GetRenderer();
    auto& frame = renderer->GetFrame(frameIdx);

    if (frame.mainFramebufferDS == VK_NULL_HANDLE) {
        frame.mainFramebufferDS = ImGui_ImplVulkan_AddTexture(m_sampler, frame.mainFramebuffer->GetImageView(), VK_IMAGE_LAYOUT_GENERAL);
    }
    if (frame.hudFramebufferDS == VK_NULL_HANDLE) {
        frame.hudFramebufferDS = ImGui_ImplVulkan_AddTexture(m_sampler, frame.hudFramebuffer->GetImageView(), VK_IMAGE_LAYOUT_GENERAL);
    }
    if (frame.hudWithoutAlphaFramebufferDS == VK_NULL_HANDLE) {
        frame.hudWithoutAlphaFramebufferDS = ImGui_ImplVulkan_AddTexture(m_sampler, frame.hudWithoutAlphaFramebuffer->GetImageView(), VK_IMAGE_LAYOUT_GENERAL);
    }

    if (renderBackground || CemuHooks::UseBlackBarsDuringEvents()) {
        const bool shouldCrop3DTo16_9 = CemuHooks::GetSettings().cropFlatTo16x9Setting == 1;

        // calculate width minus the retina scaling
        ImVec2 windowSize = ImGui::GetIO().DisplaySize;
        windowSize.x = windowSize.x / ImGui::GetIO().DisplayFramebufferScale.x;
        windowSize.y = windowSize.y / ImGui::GetIO().DisplayFramebufferScale.y;

        // center position using aspect ratio
        ImVec2 centerPos = ImVec2((windowSize.x - windowSize.y * frame.mainFramebufferAspectRatio) / 2, 0);
        ImVec2 squishedWindowSize = ImVec2(windowSize.y * frame.mainFramebufferAspectRatio, windowSize.y);

        bool shouldRender3DBackground = VRManager::instance().XR->GetRenderer()->IsRendering3D(frameIdx) || CemuHooks::UseBlackBarsDuringEvents();

        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize);
            ImGui::Begin("HUD Background", nullptr, FULLSCREEN_WINDOW_FLAGS);
            ImGui::Image((ImTextureID)(shouldRender3DBackground && !CemuHooks::UseBlackBarsDuringEvents() ? frame.hudFramebufferDS : frame.hudWithoutAlphaFramebufferDS), windowSize);
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleVar();
        }

        if (shouldRender3DBackground) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::SetNextWindowPos(shouldCrop3DTo16_9 ? ImVec2(0, 0) : centerPos);
            ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize);
            ImGui::Begin("3D Background", nullptr, FULLSCREEN_WINDOW_FLAGS);

            ImVec2 croppedUv0 = ImVec2(0.0f, 0.0f);
            ImVec2 croppedUv1 = ImVec2(1.0f, 1.0f);
            if (shouldCrop3DTo16_9) {
                ImVec2 displaySize = ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y / 2 / frame.mainFramebufferAspectRatio);
                ImVec2 displayOffset = ImVec2(ImGui::GetIO().DisplaySize.x / 2 - (displaySize.x / 2), ImGui::GetIO().DisplaySize.y / 2 - (displaySize.y / 2));
                ImVec2 textureSize = ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);

                croppedUv0 = ImVec2(displayOffset.x / textureSize.x, displayOffset.y / textureSize.y);
                croppedUv1 = ImVec2((displayOffset.x + displaySize.x) / textureSize.x, (displayOffset.y + displaySize.y) / textureSize.y);
            }

            ImGui::Image((ImTextureID)frame.mainFramebufferDS, shouldCrop3DTo16_9 ? windowSize : squishedWindowSize, croppedUv0, croppedUv1);

            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleVar();
        }
    }
    else {
        // calculate width minus the retina scaling
        ImVec2 windowSize = ImGui::GetIO().DisplaySize;
        windowSize.x = windowSize.x / ImGui::GetIO().DisplayFramebufferScale.x;
        windowSize.y = windowSize.y / ImGui::GetIO().DisplayFramebufferScale.y;

        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize);

            ImGui::Begin("HUD Background", nullptr, FULLSCREEN_WINDOW_FLAGS);
            ImGui::Image((ImTextureID)(VRManager::instance().XR->GetRenderer()->IsRendering3D(frameIdx) ? frame.hudFramebufferDS : frame.hudWithoutAlphaFramebufferDS), windowSize);
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleVar();
        }
    }

    {
        //// calculate width minus the retina scaling
        //ImVec2 windowSize = ImGui::GetIO().DisplaySize;
        //windowSize.x = windowSize.x / ImGui::GetIO().DisplayFramebufferScale.x;
        //windowSize.y = windowSize.y / ImGui::GetIO().DisplayFramebufferScale.y;

        //{
        //    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        //    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        //    ImGui::SetNextWindowPos(ImVec2(0, 0));
        //    ImGui::SetNextWindowSize(ImGui::GetMainViewport()->WorkSize);

        //    ImGui::Begin("Weapon Selector", nullptr, FULLSCREEN_WINDOW_FLAGS);
        //    ImGui::Text("Hello World!");
        //    ImGui::End();
        //    ImGui::PopStyleVar();
        //    ImGui::PopStyleVar();
        //}
    }

    if (VRManager::instance().Hooks->m_entityDebugger) {
        VRManager::instance().Hooks->m_entityDebugger->DrawEntityInspector();
        VRManager::instance().Hooks->DrawDebugOverlays();
    }

    if (m_showAppMS) {
        DrawFPSOverlay(renderer);
    }
}

void RND_Renderer::ImGuiOverlay::Draw3DLayerAsBackground(VkCommandBuffer cb, VkImage srcImage, float aspectRatio, long frameIdx) {
    auto& frame = VRManager::instance().XR->GetRenderer()->GetFrame(frameIdx);

    frame.mainFramebuffer->vkCopyFromImage(cb, srcImage);
    frame.mainFramebufferAspectRatio = aspectRatio;
}

void RND_Renderer::ImGuiOverlay::DrawHUDLayerAsBackground(VkCommandBuffer cb, VkImage srcImage, long frameIdx) {
    auto& frame = VRManager::instance().XR->GetRenderer()->GetFrame(frameIdx);

    frame.hudFramebuffer->vkCopyFromImage(cb, srcImage);
    frame.hudWithoutAlphaFramebuffer->vkCopyFromImage(cb, srcImage);
}

void RND_Renderer::ImGuiOverlay::Render() {
    ImGui::Render();
}

void RND_Renderer::ImGuiOverlay::Update() {
    POINT p;
    GetCursorPos(&p);

    ScreenToClient(m_cemuRenderWindow, &p);

    // scale mouse position with the texture size
    uint32_t framebufferWidth = (uint32_t)ImGui::GetIO().DisplaySize.x;
    uint32_t framebufferHeight = (uint32_t)ImGui::GetIO().DisplaySize.y;

    ImGui::GetIO().FontGlobalScale = 0.9f;

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

    // update mouse controls and keyboard input
    bool isWindowFocused = m_cemuTopWindow == GetForegroundWindow();

    bool isF3Pressed = GetKeyState(VK_F3) & 0x8000;
    if (isF3Pressed && !m_wasF3Pressed) {
        m_showAppMS = !m_showAppMS;
    }
    m_wasF3Pressed = isF3Pressed;

    ImGui::GetIO().AddFocusEvent(isWindowFocused);
    ImGui::GetIO().AddMousePosEvent((float)p.x, (float)p.y);

    if (VRManager::instance().Hooks->m_entityDebugger && isWindowFocused) {
        VRManager::instance().Hooks->m_entityDebugger->UpdateKeyboardControls();
    }
}

void RND_Renderer::ImGuiOverlay::DrawAndCopyToImage(VkCommandBuffer cb, VkImage destImage, long frameIdx) {
    auto* dispatch = VRManager::instance().VK->GetDeviceDispatch();
    auto* renderer = VRManager::instance().XR->GetRenderer();
    auto& frame = renderer->GetFrame(frameIdx);

    // transition framebuffer to color attachment
    frame.imguiFramebuffer->vkClear(cb, { 0.0f, 0.0f, 0.0f, 0.0f });

    // start render pass
    VkClearValue clearValue = { .color = { 0.0f, 0.0f, 0.0f, 0.0f } };
    VkRenderPassBeginInfo renderPassInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = m_renderPass,
        .framebuffer = frame.imguiFramebuffer->GetFramebuffer(),
        .renderArea = {
            .offset = { 0, 0 },
            .extent = { (uint32_t)ImGui::GetIO().DisplaySize.x, (uint32_t)ImGui::GetIO().DisplaySize.y } },
        .clearValueCount = 0,
        .pClearValues = &clearValue
    };
    dispatch->CmdBeginRenderPass(cb, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // render imgui
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);

    // end render pass
    dispatch->CmdEndRenderPass(cb);

    // copy rendered imgui to destination image
    frame.imguiFramebuffer->vkPipelineBarrier(cb);
    frame.imguiFramebuffer->vkCopyToImage(cb, destImage);
    frame.imguiFramebuffer->vkPipelineBarrier(cb);
}