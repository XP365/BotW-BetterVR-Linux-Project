#include "renderer.h"
#include "instance.h"
#include "texture.h"
#include "utils/d3d12_utils.h"


RND_Renderer::RND_Renderer(XrSession xrSession): m_session(xrSession) {
    XrSessionBeginInfo m_sessionCreateInfo = { XR_TYPE_SESSION_BEGIN_INFO };
    m_sessionCreateInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    checkXRResult(xrBeginSession(m_session, &m_sessionCreateInfo), "Failed to begin OpenXR session!");
}

RND_Renderer::~RND_Renderer() {
    xrRequestExitSession(m_session);
    if (m_session != XR_NULL_HANDLE) {
        checkXRResult(xrEndSession(m_session), "Failed to end OpenXR session!");
        m_session = XR_NULL_HANDLE;
    }

    if (m_layer3D) {
        m_layer3D.reset();
    }
    if (m_layer2D) {
        m_layer2D.reset();
    }
}

void RND_Renderer::StartFrame() {
    m_isInitialized = true;

    XrFrameWaitInfo waitFrameInfo = { XR_TYPE_FRAME_WAIT_INFO };
    auto waitStart = std::chrono::high_resolution_clock::now();
    checkXRResult(xrWaitFrame(m_session, &waitFrameInfo, &m_frameState), "Failed to wait for next frame!");
    m_lastWaitTimeMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - waitStart).count();

    // Runtime predicted cadence
    m_predictedDisplayPeriodMs = (double)m_frameState.predictedDisplayPeriod / 1e6;

    // "Frame" as the runtime sees it: delta between predicted display times
    if (m_lastPredictedDisplayTime != 0 && m_frameState.predictedDisplayTime > m_lastPredictedDisplayTime) {
        const XrTime deltaNs = m_frameState.predictedDisplayTime - m_lastPredictedDisplayTime;
        m_lastFrameTimeMs = (double)deltaNs / 1e6;

        // Overhead beyond the runtime cadence (missed interval / late frame, etc.)
        const double overheadMs = m_lastFrameTimeMs - m_predictedDisplayPeriodMs;
        m_lastOverheadMs = overheadMs > 0.0 ? overheadMs : 0.0;
    }
    m_lastPredictedDisplayTime = m_frameState.predictedDisplayTime;

    m_frameStartTime = std::chrono::high_resolution_clock::now();

    XrFrameBeginInfo beginFrameInfo = { XR_TYPE_FRAME_BEGIN_INFO };
    checkXRResult(xrBeginFrame(m_session, &beginFrameInfo), "Couldn't begin OpenXR frame!");

    VRManager::instance().D3D12->StartFrame();
    this->UpdateViews(m_frameState.predictedDisplayTime);

    // todo: update this as late as possible
    //VRManager::instance()->XR->UpdateSpaces(m_frameState.predictedDisplayTime);

    // todo: should we really not update actions if the camera is middle pose is not available?
    auto headsetRotation = VRManager::instance().XR->GetRenderer()->GetMiddlePose();
    if (headsetRotation.has_value()) {
        // todo: update this as late as possible
        VRManager::instance().XR->UpdateActions(m_frameState.predictedDisplayTime, headsetRotation.value(), VRManager::instance().Hooks->IsShowingMenu());
    }
}


void RND_Renderer::EndFrame() {
    static uint32_t s_endFrameCount = 0;
    s_endFrameCount++;

    std::vector<XrCompositionLayerBaseHeader*> compositionLayers;

    m_presented2DLastFrame = false;

    XrCompositionLayerProjection layer3D = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    std::array<XrCompositionLayerProjectionView, 2> layer3DViews = {};
    std::vector<XrCompositionLayerQuad> layer2DQuads;

    long frameIdx = -1;
    if (m_renderFrames[0].Is3DComplete() && m_renderFrames[0].Is2DComplete()) {
        frameIdx = 0;
    }
    else if (m_renderFrames[1].Is3DComplete() && m_renderFrames[1].Is2DComplete()) {
        frameIdx = 1;
    }
    else if (m_renderFrames[0].Is2DComplete()) {
        frameIdx = 0;
    }
    else if (m_renderFrames[1].Is2DComplete()) {
        frameIdx = 1;
    }

    if (frameIdx != -1) {
        if (m_layer3D) {
            if (m_renderFrames[frameIdx].Is3DComplete()) {
                m_layer3D->StartRendering();
                m_layer3D->Render(OpenXR::EyeSide::LEFT, frameIdx);
                m_layer3D->Render(OpenXR::EyeSide::RIGHT, frameIdx);
                layer3DViews = m_layer3D->FinishRendering(frameIdx);
                layer3D.layerFlags = 0;
                layer3D.space = VRManager::instance().XR->m_stageSpace;
                layer3D.viewCount = (uint32_t)layer3DViews.size();
                layer3D.views = layer3DViews.data();
                if (CemuHooks::IsInGame()) {
                    m_renderFrames[frameIdx].presented3D = true;
                    compositionLayers.emplace_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer3D));
                }
                else {
                    m_renderFrames[frameIdx].presented3D = false;
                }
            } else {
                m_renderFrames[frameIdx].presented3D = false;
            }
        }

        if (m_layer2D) {
            m_layer2D->StartRendering();
            m_layer2D->Render(frameIdx);
            layer2DQuads = m_layer2D->FinishRendering(m_frameState.predictedDisplayTime, frameIdx);
            m_presented2DLastFrame = true;
            for (auto& layer : layer2DQuads) {
                compositionLayers.emplace_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer));
            }
        }

        m_renderFrames[frameIdx].Reset();
    }

    m_lastFrameWorkTimeMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - m_frameStartTime).count();

    XrFrameEndInfo frameEndInfo = { XR_TYPE_FRAME_END_INFO };
    frameEndInfo.displayTime = m_frameState.predictedDisplayTime;
    frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    frameEndInfo.layerCount = (uint32_t)compositionLayers.size();
    frameEndInfo.layers = compositionLayers.data();

    if (s_endFrameCount % 500 == 0) {
        Log::print<INTEROP>("EndFrame #{}: frameIdx={}, layers={}, 3D={}, 2D={}",
            s_endFrameCount, frameIdx, compositionLayers.size(),
            (frameIdx != -1 && m_renderFrames[frameIdx].presented3D) ? "yes" : "no",
            m_presented2DLastFrame ? "yes" : "no");
    }

    XrResult xrResult = xrEndFrame(m_session, &frameEndInfo);
    if (XR_FAILED(xrResult)) {
        Log::print<ERROR>("xrEndFrame #{} FAILED with result {}", s_endFrameCount, (int)xrResult);
    }

    VRManager::instance().D3D12->EndFrame();
}

RND_Renderer::Layer3D::Layer3D(VkExtent2D inputRes, VkExtent2D outputRes) {
    auto viewConfs = VRManager::instance().XR->GetViewConfigurations();

    this->m_recommendedAspectRatios[OpenXR::EyeSide::LEFT] = (float)viewConfs[0].recommendedImageRectWidth / (float)viewConfs[0].recommendedImageRectHeight;
    this->m_recommendedAspectRatios[OpenXR::EyeSide::RIGHT] = (float)viewConfs[1].recommendedImageRectWidth / (float)viewConfs[1].recommendedImageRectHeight;

    this->m_presentPipelines[OpenXR::EyeSide::LEFT] = std::make_unique<RND_D3D12::PresentPipeline<true>>(VRManager::instance().XR->GetRenderer());
    this->m_presentPipelines[OpenXR::EyeSide::RIGHT] = std::make_unique<RND_D3D12::PresentPipeline<true>>(VRManager::instance().XR->GetRenderer());

    this->m_swapchains[OpenXR::EyeSide::LEFT] = std::make_unique<Swapchain<DXGI_FORMAT_R8G8B8A8_UNORM_SRGB>>(outputRes.width, outputRes.height, viewConfs[0].recommendedSwapchainSampleCount);
    this->m_swapchains[OpenXR::EyeSide::RIGHT] = std::make_unique<Swapchain<DXGI_FORMAT_R8G8B8A8_UNORM_SRGB>>(outputRes.width, outputRes.height, viewConfs[1].recommendedSwapchainSampleCount);
    this->m_depthSwapchains[OpenXR::EyeSide::LEFT] = std::make_unique<Swapchain<DXGI_FORMAT_D32_FLOAT>>(outputRes.width, outputRes.height, viewConfs[0].recommendedSwapchainSampleCount);
    this->m_depthSwapchains[OpenXR::EyeSide::RIGHT] = std::make_unique<Swapchain<DXGI_FORMAT_D32_FLOAT>>(outputRes.width, outputRes.height, viewConfs[1].recommendedSwapchainSampleCount);

    this->m_presentPipelines[OpenXR::EyeSide::LEFT]->BindSettings((float)outputRes.width, (float)outputRes.height);
    this->m_presentPipelines[OpenXR::EyeSide::RIGHT]->BindSettings((float)outputRes.width, (float)outputRes.height);

    // initialize textures
    for (int i = 0; i < 2; ++i) {
        this->m_textures[OpenXR::EyeSide::LEFT][i] = std::make_unique<SharedTexture>(inputRes.width, inputRes.height, VK_FORMAT_B10G11R11_UFLOAT_PACK32, D3D12Utils::ToDXGIFormat(VK_FORMAT_B10G11R11_UFLOAT_PACK32));
        this->m_textures[OpenXR::EyeSide::RIGHT][i] = std::make_unique<SharedTexture>(inputRes.width, inputRes.height, VK_FORMAT_B10G11R11_UFLOAT_PACK32, D3D12Utils::ToDXGIFormat(VK_FORMAT_B10G11R11_UFLOAT_PACK32));
        this->m_depthTextures[OpenXR::EyeSide::LEFT][i] = std::make_unique<SharedTexture>(inputRes.width, inputRes.height, VK_FORMAT_D32_SFLOAT, D3D12Utils::ToDXGIFormat(VK_FORMAT_D32_SFLOAT));
        this->m_depthTextures[OpenXR::EyeSide::RIGHT][i] = std::make_unique<SharedTexture>(inputRes.width, inputRes.height, VK_FORMAT_D32_SFLOAT, D3D12Utils::ToDXGIFormat(VK_FORMAT_D32_SFLOAT));

        this->m_textures[OpenXR::EyeSide::LEFT][i]->d3d12GetTexture()->SetName(L"Layer3D - Left Color Texture");
        this->m_textures[OpenXR::EyeSide::RIGHT][i]->d3d12GetTexture()->SetName(L"Layer3D - Right Color Texture");
        this->m_depthTextures[OpenXR::EyeSide::LEFT][i]->d3d12GetTexture()->SetName(L"Layer3D - Left Depth Texture");
        this->m_depthTextures[OpenXR::EyeSide::RIGHT][i]->d3d12GetTexture()->SetName(L"Layer3D - Right Depth Texture");
    }

    ComPtr<ID3D12CommandAllocator> cmdAllocator;
    {
        ID3D12Device* d3d12Device = VRManager::instance().D3D12->GetDevice();
        ID3D12CommandQueue* d3d12Queue = VRManager::instance().D3D12->GetCommandQueue();
        d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocator));

        RND_D3D12::CommandContext<true> transitionInitialTextures(d3d12Device, d3d12Queue, cmdAllocator.Get(), [this](RND_D3D12::CommandContext<true>* context) {
            context->GetRecordList()->SetName(L"transitionInitialTextures");
            for (int i = 0; i < 2; ++i) {
                this->m_textures[OpenXR::EyeSide::LEFT][i]->d3d12TransitionLayout(context->GetRecordList(), D3D12_RESOURCE_STATE_COMMON);
                this->m_textures[OpenXR::EyeSide::RIGHT][i]->d3d12TransitionLayout(context->GetRecordList(), D3D12_RESOURCE_STATE_COMMON);
                this->m_depthTextures[OpenXR::EyeSide::LEFT][i]->d3d12TransitionLayout(context->GetRecordList(), D3D12_RESOURCE_STATE_COMMON);
                this->m_depthTextures[OpenXR::EyeSide::RIGHT][i]->d3d12TransitionLayout(context->GetRecordList(), D3D12_RESOURCE_STATE_COMMON);
            }
        });
    }
}

RND_Renderer::Layer3D::~Layer3D() {
    for (auto& swapchain : m_swapchains) {
        swapchain.reset();
    }
    for (auto& swapchain : m_depthSwapchains) {
        swapchain.reset();
    }
}

SharedTexture* RND_Renderer::Layer3D::CopyColorToLayer(OpenXR::EyeSide side, VkCommandBuffer copyCmdBuffer, VkImage image, long frameIdx) {
    static uint32_t s_copyCount = 0;
    static VkImage s_lastSrcImage = VK_NULL_HANDLE;
    s_copyCount++;

    if (s_lastSrcImage != VK_NULL_HANDLE && s_lastSrcImage != image) {
        Log::print<WARNING>("Layer3D srcImage changed: {} -> {} at copy #{}", (void*)s_lastSrcImage, (void*)image, s_copyCount);
    }
    s_lastSrcImage = image;

    if (s_copyCount % 100 == 0) {
        Log::print<INTEROP>("Layer3D::CopyColorToLayer #{} - side={}, frameIdx={}, srcImage={}", s_copyCount, side == OpenXR::EyeSide::LEFT ? "L" : "R", frameIdx, (void*)image);
    }

    m_currentFrameIdx = frameIdx;
    m_textures[side][frameIdx]->CopyFromVkImage(copyCmdBuffer, image);
    return m_textures[side][frameIdx].get();
}

SharedTexture* RND_Renderer::Layer3D::CopyDepthToLayer(OpenXR::EyeSide side, VkCommandBuffer copyCmdBuffer, VkImage image, long frameIdx) {
    m_depthTextures[side][frameIdx]->CopyFromVkImage(copyCmdBuffer, image);
    return m_depthTextures[side][frameIdx].get();
}

void RND_Renderer::Layer3D::PrepareRendering(OpenXR::EyeSide side) {
    // Log::print("Preparing rendering for {} side", side == OpenXR::EyeSide::LEFT ? "left" : "right");
    m_swapchains[side]->PrepareRendering();
    m_depthSwapchains[side]->PrepareRendering();
}

std::optional<std::array<XrView, 2>> RND_Renderer::UpdateViews(XrTime predictedDisplayTime) {
    std::array newViews = { XrView{ XR_TYPE_VIEW }, XrView{ XR_TYPE_VIEW } };
    XrViewLocateInfo viewLocateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
    viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    viewLocateInfo.displayTime = predictedDisplayTime;
    viewLocateInfo.space = VRManager::instance().XR->m_stageSpace; // locate the rendering views relative to the room, not the headset center
    XrViewState viewState = { XR_TYPE_VIEW_STATE };
    uint32_t viewCount = (uint32_t)newViews.size();
    checkXRResult(xrLocateViews(VRManager::instance().XR->m_session, &viewLocateInfo, &viewState, viewCount, &viewCount, newViews.data()), "Failed to get view information!");
    if ((viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0)
        return std::nullopt; // what should occur when the orientation is invalid? keep rendering using old values?

    const float playerHeightOffsetMeters = CemuHooks::GetSettings().playerHeightSetting.getLE();
    for (auto& view : newViews) {
        view.pose.position.y += playerHeightOffsetMeters;
    }

    m_currViews = newViews;
    return m_currViews;
}

void RND_Renderer::Layer3D::StartRendering() {
    // checkAssert((this->m_textures[OpenXR::EyeSide::LEFT] == nullptr && this->m_textures[OpenXR::EyeSide::RIGHT] == nullptr) || (this->m_textures[OpenXR::EyeSide::LEFT] != nullptr && this->m_textures[OpenXR::EyeSide::RIGHT] != nullptr), "Both textures must be either null or not null");
    // checkAssert((this->m_depthTextures[OpenXR::EyeSide::LEFT] == nullptr && this->m_depthTextures[OpenXR::EyeSide::RIGHT] == nullptr) || (this->m_depthTextures[OpenXR::EyeSide::LEFT] != nullptr && this->m_depthTextures[OpenXR::EyeSide::RIGHT] != nullptr), "Both depth textures must be either null or not null");
    // checkAssert((this->m_textures[OpenXR::EyeSide::LEFT][0] == nullptr && this->m_textures[OpenXR::EyeSide::RIGHT][0] == nullptr) || (this->m_textures[OpenXR::EyeSide::LEFT][0] != nullptr && this->m_textures[OpenXR::EyeSide::RIGHT][0] != nullptr), "Both textures must be either null or not null");
    // checkAssert((this->m_depthTextures[OpenXR::EyeSide::LEFT][0] == nullptr && this->m_depthTextures[OpenXR::EyeSide::RIGHT][0] == nullptr) || (this->m_depthTextures[OpenXR::EyeSide::LEFT][0] != nullptr && this->m_depthTextures[OpenXR::EyeSide::RIGHT][0] != nullptr), "Both depth textures must be either null or not null");

    this->m_swapchains[OpenXR::EyeSide::LEFT]->PrepareRendering();
    this->m_swapchains[OpenXR::EyeSide::LEFT]->StartRendering();
    this->m_depthSwapchains[OpenXR::EyeSide::LEFT]->PrepareRendering();
    this->m_depthSwapchains[OpenXR::EyeSide::LEFT]->StartRendering();
    this->m_swapchains[OpenXR::EyeSide::RIGHT]->PrepareRendering();
    this->m_swapchains[OpenXR::EyeSide::RIGHT]->StartRendering();
    this->m_depthSwapchains[OpenXR::EyeSide::RIGHT]->PrepareRendering();
    this->m_depthSwapchains[OpenXR::EyeSide::RIGHT]->StartRendering();
}

void RND_Renderer::Layer3D::Render(OpenXR::EyeSide side, long frameIdx) {
    ID3D12Device* device = VRManager::instance().D3D12->GetDevice();
    ID3D12CommandQueue* queue = VRManager::instance().D3D12->GetCommandQueue();
    ID3D12CommandAllocator* allocator = VRManager::instance().D3D12->GetFrameAllocator();

    RND_D3D12::CommandContext<false> renderSharedTexture(device, queue, allocator, [this, side, frameIdx](RND_D3D12::CommandContext<false>* context) {
        context->GetRecordList()->SetName(L"RenderSharedTexture");
        auto& texture = m_textures[side][frameIdx];
        auto& depthTexture = m_depthTextures[side][frameIdx];

        context->WaitFor(texture.get(), texture->GetD3D12WaitValue());
        context->WaitFor(depthTexture.get(), depthTexture->GetD3D12WaitValue());

        // swapchains are already in D3D12_RESOURCE_STATE_RENDER_TARGET and depth in D3D12_RESOURCE_STATE_DEPTH_WRITE according to OpenXR spec

        m_presentPipelines[side]->BindAttachment(0, texture->d3d12GetTexture());
        m_presentPipelines[side]->BindAttachment(1, depthTexture->d3d12GetTexture(), DXGI_FORMAT_R32_FLOAT);
        m_presentPipelines[side]->BindTarget(0, m_swapchains[side]->GetTexture(), m_swapchains[side]->GetFormat());
        m_presentPipelines[side]->BindDepthTarget(m_depthSwapchains[side]->GetTexture(), m_depthSwapchains[side]->GetFormat());
        m_presentPipelines[side]->Render(context->GetRecordList(), m_swapchains[side]->GetTexture());

        // no transition needed here as OpenXR requires the swapchain to be returned in RENDER_TARGET/DEPTH_WRITE too

        context->Signal(texture.get(), texture->GetD3D12SignalValue());
        context->Signal(depthTexture.get(), depthTexture->GetD3D12SignalValue());
    });
    // Log::print("[D3D12 - 3D Layer] Rendering finished");
}

const std::array<XrCompositionLayerProjectionView, 2>& RND_Renderer::Layer3D::FinishRendering(long frameIdx) {
    this->m_swapchains[OpenXR::EyeSide::LEFT]->FinishRendering();
    this->m_depthSwapchains[OpenXR::EyeSide::LEFT]->FinishRendering();
    this->m_swapchains[OpenXR::EyeSide::RIGHT]->FinishRendering();
    this->m_depthSwapchains[OpenXR::EyeSide::RIGHT]->FinishRendering();

    // clang-format off
    m_projectionViews[OpenXR::EyeSide::LEFT] = {
        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
        .next = &m_projectionViewsDepthInfo[OpenXR::EyeSide::LEFT],
        .pose = VRManager::instance().XR->GetRenderer()->GetPose(OpenXR::EyeSide::LEFT, frameIdx).value(),
        .fov = VRManager::instance().XR->GetRenderer()->GetFOV(OpenXR::EyeSide::LEFT, frameIdx).value(),
        .subImage = {
            .swapchain = this->m_swapchains[OpenXR::EyeSide::LEFT]->GetHandle(),
            .imageRect = {
                .offset = { 0, 0 },
                .extent = {
                    .width = (int32_t)this->m_swapchains[OpenXR::EyeSide::LEFT]->GetWidth(),
                    .height = (int32_t)this->m_swapchains[OpenXR::EyeSide::LEFT]->GetHeight()
                }
            }
        }
    };
    m_projectionViewsDepthInfo[OpenXR::EyeSide::LEFT] = {
        .type = XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR,
        .subImage = {
            .swapchain = this->m_depthSwapchains[OpenXR::EyeSide::LEFT]->GetHandle(),
            .imageRect = {
                .offset = { 0, 0 },
                .extent = {
                    .width = (int32_t)this->m_depthSwapchains[OpenXR::EyeSide::LEFT]->GetWidth(),
                    .height = (int32_t)this->m_depthSwapchains[OpenXR::EyeSide::LEFT]->GetHeight()
                }
            },
        },
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
        .nearZ = CemuHooks::GetSettings().GetZNear(),
        .farZ = CemuHooks::GetSettings().GetZFar(),
    };
    m_projectionViews[OpenXR::EyeSide::RIGHT] = {
        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
        .next = &m_projectionViewsDepthInfo[OpenXR::EyeSide::RIGHT],
        .pose = VRManager::instance().XR->GetRenderer()->GetPose(OpenXR::EyeSide::RIGHT, frameIdx).value(),
        .fov = VRManager::instance().XR->GetRenderer()->GetFOV(OpenXR::EyeSide::RIGHT, frameIdx).value(),
        .subImage = {
            .swapchain = this->m_swapchains[OpenXR::EyeSide::RIGHT]->GetHandle(),
            .imageRect = {
                .offset = { 0, 0 },
                .extent = {
                    .width = (int32_t)this->m_swapchains[OpenXR::EyeSide::RIGHT]->GetWidth(),
                    .height = (int32_t)this->m_swapchains[OpenXR::EyeSide::RIGHT]->GetHeight()
                }
            }
        }
    };
    m_projectionViewsDepthInfo[OpenXR::EyeSide::RIGHT] = {
        .type = XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR,
        .subImage = {
            .swapchain = this->m_depthSwapchains[OpenXR::EyeSide::RIGHT]->GetHandle(),
            .imageRect = {
                .offset = { 0, 0 },
                .extent = {
                    .width = (int32_t)this->m_depthSwapchains[OpenXR::EyeSide::RIGHT]->GetWidth(),
                    .height = (int32_t)this->m_depthSwapchains[OpenXR::EyeSide::RIGHT]->GetHeight()
                }
            },
        },
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
        .nearZ = CemuHooks::GetSettings().GetZNear(),
        .farZ = CemuHooks::GetSettings().GetZFar(),
    };
    // clang-format on
    return m_projectionViews;
}


RND_Renderer::Layer2D::Layer2D(VkExtent2D inputRes, VkExtent2D outputRes) {
    auto viewConfs = VRManager::instance().XR->GetViewConfigurations();

    this->m_presentPipeline = std::make_unique<RND_D3D12::PresentPipeline<false>>(VRManager::instance().XR->GetRenderer());

    this->m_swapchain = std::make_unique<Swapchain<DXGI_FORMAT_R8G8B8A8_UNORM_SRGB>>(inputRes.width, inputRes.height, viewConfs[0].recommendedSwapchainSampleCount);

    this->m_presentPipeline->BindSettings(outputRes.width, outputRes.height);

    // initialize textures
    for (int i = 0; i < 2; ++i) {
        this->m_textures[i] = std::make_unique<SharedTexture>(inputRes.width, inputRes.height, VK_FORMAT_A2B10G10R10_UNORM_PACK32, D3D12Utils::ToDXGIFormat(VK_FORMAT_A2B10G10R10_UNORM_PACK32));
        this->m_textures[i]->d3d12GetTexture()->SetName(L"Layer2D - Color Texture");
    }

    ComPtr<ID3D12CommandAllocator> cmdAllocator;
    {
        ID3D12Device* d3d12Device = VRManager::instance().D3D12->GetDevice();
        ID3D12CommandQueue* d3d12Queue = VRManager::instance().D3D12->GetCommandQueue();
        d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAllocator));

        RND_D3D12::CommandContext<true> transitionInitialTextures(d3d12Device, d3d12Queue, cmdAllocator.Get(), [this](RND_D3D12::CommandContext<true>* context) {
            context->GetRecordList()->SetName(L"transitionInitialTextures");
            for (int i = 0; i < 2; ++i) {
                this->m_textures[i]->d3d12TransitionLayout(context->GetRecordList(), D3D12_RESOURCE_STATE_COMMON);
            }
        });
    }
}

RND_Renderer::Layer2D::~Layer2D() {
    m_swapchain.reset();
}

SharedTexture* RND_Renderer::Layer2D::CopyColorToLayer(VkCommandBuffer copyCmdBuffer, VkImage image, long frameIdx) {
    static uint32_t s_copyCount = 0;
    s_copyCount++;
    if (s_copyCount % 100 == 0) {
        Log::print<INTEROP>("Layer2D::CopyColorToLayer #{} - frameIdx={}, srcImage={}", s_copyCount, frameIdx, (void*)image);
    }

    m_currentFrameIdx = frameIdx;
    m_textures[frameIdx]->CopyFromVkImage(copyCmdBuffer, image);
    return m_textures[frameIdx].get();
}

void RND_Renderer::Layer2D::StartRendering() const {
    m_swapchain->PrepareRendering();
    m_swapchain->StartRendering();
}

void RND_Renderer::Layer2D::Render(long frameIdx) {
    ID3D12Device* device = VRManager::instance().D3D12->GetDevice();
    ID3D12CommandQueue* queue = VRManager::instance().D3D12->GetCommandQueue();
    ID3D12CommandAllocator* allocator = VRManager::instance().D3D12->GetFrameAllocator();

    RND_D3D12::CommandContext<false> renderSharedTexture(device, queue, allocator, [this, frameIdx](RND_D3D12::CommandContext<false>* context) {
        context->GetRecordList()->SetName(L"RenderSharedTexture");

        // wait for both since we only have one 2D swap buffer to render to
        // fixme: Why do we signal to the global command list instead of the local one?!
        auto& texture = m_textures[frameIdx];
        context->WaitFor(texture.get(), texture->GetD3D12WaitValue());

        m_presentPipeline->BindAttachment(0, texture->d3d12GetTexture());
        m_presentPipeline->BindTarget(0, m_swapchain->GetTexture(), m_swapchain->GetFormat());
        m_presentPipeline->Render(context->GetRecordList(), m_swapchain->GetTexture());

        context->Signal(texture.get(), texture->GetD3D12SignalValue());
    });
}

std::vector<XrCompositionLayerQuad> RND_Renderer::Layer2D::FinishRendering(XrTime predictedDisplayTime, long frameIdx) {
    this->m_swapchain->FinishRendering();

    XrSpaceLocation spaceLocation = { XR_TYPE_SPACE_LOCATION };
    xrLocateSpace(VRManager::instance().XR->m_headSpace, VRManager::instance().XR->m_stageSpace, predictedDisplayTime, &spaceLocation);
    glm::quat headOrientation = ToGLM(spaceLocation.pose.orientation);
    glm::vec3 headPosition = ToGLM(spaceLocation.pose.position);

    constexpr float DISTANCE = 1.5f;
    constexpr float LERP_SPEED = 0.05f;

    if (CemuHooks::GetSettings().UIFollowsLookingDirection()) {
        m_currentOrientation = glm::slerp(m_currentOrientation, headOrientation, LERP_SPEED);
        glm::vec3 forwardDirection = headOrientation * glm::vec3(0.0f, 0.0f, -1.0f);

        // calculate new position forwards
        glm::vec3 targetPosition = headPosition + (DISTANCE * forwardDirection);

        // calculate rightward direction
        glm::vec3 rightDirection = glm::normalize(glm::cross(forwardDirection, glm::vec3(0.0f, 1.0f, 0.0f)));

        // recalculate up direction using right and forward direction
        glm::vec3 upDirection = glm::cross(rightDirection, forwardDirection);
        glm::quat userFacingOrientation = glm::quatLookAt(forwardDirection, upDirection);

        spaceLocation.pose.orientation = ToXR(userFacingOrientation);
        spaceLocation.pose.position = ToXR(targetPosition);
    }
    else {
        spaceLocation.pose.position.z -= DISTANCE;
        spaceLocation.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
    }

    const float aspectRatio = (float)this->m_textures[frameIdx]->d3d12GetTexture()->GetDesc().Width / (float)this->m_textures[frameIdx]->d3d12GetTexture()->GetDesc().Height;

    const float width = aspectRatio > 1.0f ? aspectRatio : 1.0f;
    const float height = aspectRatio <= 1.0f ? 1.0f / aspectRatio : 1.0f;

    // todo: change space to head space if we want to follow the head
    constexpr float MENU_SIZE = 0.8f;

    std::vector<XrCompositionLayerQuad> layers;

    // clang-format off
    layers.push_back({
        .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
        .space = VRManager::instance().XR->m_stageSpace,
        .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
        .subImage = {
            .swapchain = this->m_swapchain->GetHandle(),
            .imageRect = {
                .offset = { 0, 0 },
                .extent = {
                    .width = (int32_t)this->m_swapchain->GetWidth(),
                    .height = (int32_t)this->m_swapchain->GetHeight()
                }
            }
        },
        .pose = spaceLocation.pose,
        .size = { width * MENU_SIZE, height * MENU_SIZE }
    });
    // clang-format on

    // render layer twice to visualize the controller positions in debug mode
    auto inputs = VRManager::instance().XR->m_input.load();

    if (!(inputs.inGame.in_game && inputs.inGame.pose[OpenXR::EyeSide::LEFT].isActive && inputs.inGame.pose[OpenXR::EyeSide::RIGHT].isActive)) {
        return layers;
    }

    return layers;

    auto movePoseToHandPosition = [](XrPosef& inputPose) {
        glm::fquat modifiedRotation = ToGLM(inputPose.orientation);
        glm::fvec3 modifiedPosition = ToGLM(inputPose.position);

        modifiedRotation *= glm::angleAxis(glm::radians(-45.0f), glm::fvec3(1, 0, 0));


        inputPose.orientation = ToXR(modifiedRotation);
        inputPose.position = ToXR(modifiedPosition);
    };

    if ((inputs.inGame.poseLocation[OpenXR::EyeSide::LEFT].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 1 || (inputs.inGame.poseLocation[OpenXR::EyeSide::LEFT].locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 1) {
        movePoseToHandPosition(inputs.inGame.poseLocation[OpenXR::EyeSide::LEFT].pose);
        // clang-format off
        layers.push_back({
            .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
            .layerFlags = 0,
            .space = VRManager::instance().XR->m_stageSpace,
            .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
            .subImage = {
                .swapchain = this->m_swapchain->GetHandle(),
                .imageRect = {
                    .offset = { 0, 0 },
                    .extent = {
                        .width = (int32_t)this->m_swapchain->GetWidth(),
                        .height = (int32_t)this->m_swapchain->GetHeight()
                    }
                }
            },
            .pose = inputs.inGame.poseLocation[OpenXR::EyeSide::LEFT].pose,
            .size = { 0.15f, 0.15f }
        });
        // clang-format on
    }

    if ((inputs.inGame.poseLocation[OpenXR::EyeSide::RIGHT].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 1 || (inputs.inGame.poseLocation[OpenXR::EyeSide::RIGHT].locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 1) {
        movePoseToHandPosition(inputs.inGame.poseLocation[OpenXR::EyeSide::RIGHT].pose);

        // clang-format off
        layers.push_back({
            .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
            .layerFlags = 0,
            .space = VRManager::instance().XR->m_stageSpace,
            .eyeVisibility = XR_EYE_VISIBILITY_BOTH,
            .subImage = {
                .swapchain = this->m_swapchain->GetHandle(),
                .imageRect = {
                    .offset = { 0, 0 },
                    .extent = {
                        .width = (int32_t)this->m_swapchain->GetWidth(),
                        .height = (int32_t)this->m_swapchain->GetHeight()
                    }
                }
            },
            .pose = inputs.inGame.poseLocation[OpenXR::EyeSide::RIGHT].pose,
            .size = { 0.15f, 0.15f }
        });
        // clang-format on
    }

    return layers;
}