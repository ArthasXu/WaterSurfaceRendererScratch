#pragma once

#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

#include "ImageView.h"
#include "PhysicalDevice.h"
#include "Image.h"
#include "scene/water/render/DepthBuffer.h"

struct GLFWwindow;

namespace vkp
{
class SwapChain
{
public:
    SwapChain(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkSurfaceKHR surface,
        GLFWwindow* window,
        const QueueFamilyIndices& queueFamilies
    );
    ~SwapChain();

    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;

    operator VkSwapchainKHR() const;

    VkFormat GetImageFormat() const;
    VkExtent2D GetExtent() const;
    VkFramebuffer GetFramebuffer(uint32_t imageIndex) const;
    size_t GetImageCount() const;

    VkResult AcquireNextImage(VkSemaphore imageAvailable, uint32_t* imageIndex);
    VkResult Present(VkQueue presentQueue, VkSemaphore renderFinished, uint32_t imageIndex);

    static VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);

    void CreateDepthResources(
        VkPhysicalDevice physicalDevice,
        VkFormat depthFormat
    ); // 创建深度资源

    void CreateFramebuffers(VkRenderPass renderPass); // 创建帧缓冲

    VkImageView GetImageView(uint32_t imageIndex) const; // 获取图像视图
    VkImageView GetDepthImageView(uint32_t imageIndex) const; // 获取深度图像视图

private:
    void createSwapChain();
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    SwapChainSupportDetails querySwapChainSupport();
    void createImageViews();
    void createFramebuffers(VkRenderPass renderPass);
    void cleanupSwapChain();

private:
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    GLFWwindow* m_Window = nullptr;
    QueueFamilyIndices m_QueueFamilyIndices;

    VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
    std::vector<VkImage> m_Images;
    std::vector<std::unique_ptr<ImageView>> m_ImageViews;
    std::vector<VkFramebuffer> m_Framebuffers;

    VkFormat m_ImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_Extent{};

    std::vector<std::unique_ptr<water::DepthBuffer>> m_DepthBuffers;
    VkFormat m_DepthFormat = VK_FORMAT_UNDEFINED; // 深度格式
};
} // namespace vkp
