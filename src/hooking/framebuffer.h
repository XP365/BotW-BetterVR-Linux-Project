#pragma once

#include "rendering/texture.h"

struct CaptureTexture {
    bool initialized;
    VkExtent2D foundSize;
    VkImage foundImage;
    VkFormat format;
    VkExtent2D minSize;
    std::unique_ptr<SharedTexture> sharedTexture;

    // current frame state
    std::atomic<VkCommandBuffer> captureCmdBuffer = VK_NULL_HANDLE;
};

extern std::array<CaptureTexture, 1> captureTextures;