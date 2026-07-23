#pragma once

#include <vulkan/vulkan.h>

#include <vector>

struct GLFWwindow;

namespace gui
{
std::vector<VkDescriptorPoolSize> GetDescriptorPoolSizes();

void Init(
    VkInstance instance,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    uint32_t queueFamily,
    VkQueue queue,
    VkDescriptorPool descriptorPool,
    uint32_t minImageCount,
    uint32_t imageCount,
    GLFWwindow* window,
    VkRenderPass renderPass
);

void UploadFonts(VkDevice device, VkQueue queue, VkCommandPool commandPool);
void Shutdown();
void NewFrame();
void Render(VkCommandBuffer commandBuffer);
void SetMinImageCount(uint32_t minImageCount);
}
