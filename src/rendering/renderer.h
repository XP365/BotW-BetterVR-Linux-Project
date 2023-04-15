#pragma once

#include "d3d12.h"


class RND_Renderer {
public:
    explicit RND_Renderer(XrSession xrSession);
    ~RND_Renderer();

    void StartFrame();
    void Render(class SharedTexture* texture);
    void EndFrame();

protected:
    XrSession m_session;

    std::unique_ptr<RND_D3D12::PresentPipeline> m_presentPipeline;
    std::vector<class SharedTexture*> m_textures;
    std::vector<XrCompositionLayerBaseHeader*> m_layers;
    std::array<XrCompositionLayerProjectionView, 2> m_frameProjectionViews{};
    XrFrameState m_frameState = { XR_TYPE_FRAME_STATE };
};