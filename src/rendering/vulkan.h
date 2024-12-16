#pragma once
#include "texture.h"

using ValueVariant = std::variant<uint32_t, float, XrVector3f, std::string>;

struct EntityValue {
    std::string value_name;
    uint32_t value_address;
    std::variant<uint32_t, float, XrVector3f, std::string> value;
};

class RND_Vulkan {
    friend class ImGuiOverlay;

public:
    RND_Vulkan(VkInstance vkInstance, VkPhysicalDevice vkPhysDevice, VkDevice vkDevice);
    ~RND_Vulkan();

    class ImGuiOverlay {
    public:
        explicit ImGuiOverlay(VkCommandBuffer cb, uint32_t width, uint32_t height, VkFormat format);
        ~ImGuiOverlay();


        std::string m_filter;
        std::unordered_map<std::string, std::vector<EntityValue>> m_entities;


        void AddEntity(::std::string entity, ::std::string name, uint32_t address, ValueVariant& value);
        void RemoveEntity(std::string entity);

        void BeginFrame();
        void Draw3DLayerAsBackground(VkCommandBuffer cb, VkImage srcImage, float aspectRatio);
        void DrawHUDLayerAsBackground(VkCommandBuffer cb, VkImage srcImage);
        void UpdateControls();

        void Render();
        void DrawOverlayToImage(VkCommandBuffer cb, VkImage destImage);

    private:
        ImGuiContext* m_context;
        VkDescriptorPool m_descriptorPool;
        VkRenderPass m_renderPass;

        HWND m_cemuTopWindow = nullptr;
        HWND m_cemuRenderWindow = nullptr;

        std::array<std::unique_ptr<VulkanFramebuffer>, 2> m_framebuffers;
        VkSampler m_sampler = VK_NULL_HANDLE;
        std::unique_ptr<VulkanTexture> m_mainFramebuffer;
        VkDescriptorSet m_mainFramebufferDescriptorSet = VK_NULL_HANDLE;
        float m_mainFramebufferAspectRatio = 1.0f;
        std::unique_ptr<VulkanTexture> m_hudFramebuffer;
        VkDescriptorSet m_hudFramebufferDescriptorSet = VK_NULL_HANDLE;
        uint32_t m_framebufferIdx = 0;
    };

    uint32_t FindMemoryType(uint32_t memoryTypeBitsRequirement, VkMemoryPropertyFlags requirementsMask);
    VkInstance GetInstance() { return m_instance; }
    VkDevice GetDevice() { return m_device; }

    const vkroots::VkInstanceDispatch* GetInstanceDispatch() const { return m_instanceDispatch; }
    const vkroots::VkPhysicalDeviceDispatch* GetPhysicalDeviceDispatch() const { return m_physicalDeviceDispatch; }
    const vkroots::VkDeviceDispatch* GetDeviceDispatch() const { return m_deviceDispatch; }

    std::unique_ptr<ImGuiOverlay> m_imguiOverlay;
private:
    VkInstance m_instance;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    VkPhysicalDeviceMemoryProperties2 m_memoryProperties = {};

    // todo: use these with caution
    const vkroots::VkInstanceDispatch* m_instanceDispatch;
    const vkroots::VkPhysicalDeviceDispatch* m_physicalDeviceDispatch;
    const vkroots::VkDeviceDispatch* m_deviceDispatch;
};