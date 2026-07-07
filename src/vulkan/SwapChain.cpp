#include "SwapChain.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace vkp
{
SwapChain::SwapChain(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkSurfaceKHR surface,
    GLFWwindow* window,
    const QueueFamilyIndices& queueFamilies
)
    : m_PhysicalDevice(physicalDevice),
      m_Device(device),
      m_Surface(surface),
      m_Window(window),
      m_QueueFamilyIndices(queueFamilies)
{
    createSwapChain();
    createImageViews();
}

SwapChain::~SwapChain()
{
    cleanupSwapChain();
}

SwapChain::operator VkSwapchainKHR() const
{
    return m_SwapChain;
}

VkFormat SwapChain::GetImageFormat() const
{
    return m_ImageFormat;
}

VkExtent2D SwapChain::GetExtent() const
{
    return m_Extent;
}

VkFramebuffer SwapChain::GetFramebuffer(uint32_t imageIndex) const
{
    return m_Framebuffers[imageIndex];
}

size_t SwapChain::GetImageCount() const
{
    return m_Images.size();
}

VkResult SwapChain::AcquireNextImage(VkSemaphore imageAvailable, uint32_t* imageIndex)
{
    return vkAcquireNextImageKHR(
        m_Device,
        m_SwapChain,
        UINT64_MAX,
        imageAvailable,
        VK_NULL_HANDLE,
        imageIndex
    );
}

VkResult SwapChain::Present(VkQueue presentQueue, VkSemaphore renderFinished, uint32_t imageIndex)
{
    VkSemaphore waitSemaphores[] = { renderFinished };
    VkSwapchainKHR swapChains[] = { m_SwapChain };

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = waitSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    return vkQueuePresentKHR(presentQueue, &presentInfo);
}

SwapChainSupportDetails SwapChain::querySwapChainSupport()
{
    SwapChainSupportDetails details{};

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        m_PhysicalDevice,
        m_Surface,
        &details.capabilities
    );

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_PhysicalDevice,
        m_Surface,
        &formatCount,
        nullptr
    );

    if(formatCount != 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            m_PhysicalDevice,
            m_Surface,
            &formatCount,
            details.formats.data()
        );
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_PhysicalDevice,
        m_Surface,
        &presentModeCount,
        nullptr
    );

    if(presentModeCount != 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            m_PhysicalDevice,
            m_Surface,
            &presentModeCount,
            details.presentModes.data()
        );
    }

    return details;
}

void SwapChain::createSwapChain()
{
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport();

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;

    if(swapChainSupport.capabilities.maxImageCount > 0 &&
       imageCount > swapChainSupport.capabilities.maxImageCount)
    {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_Surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndices[] = {
        m_QueueFamilyIndices.graphicsFamily.value(),
        m_QueueFamilyIndices.presentFamily.value()
    };

    if(m_QueueFamilyIndices.graphicsFamily != m_QueueFamilyIndices.presentFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if(vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_SwapChain) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, nullptr);
    m_Images.resize(imageCount);
    vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, m_Images.data());

    m_ImageFormat = surfaceFormat.format;
    m_Extent = extent;

    std::cout << "Swapchain image count: " << m_Images.size() << "\n";
    std::cout << "Swapchain format: " << m_ImageFormat << "\n";
    std::cout << "Swapchain extent: " << m_Extent.width << " x " << m_Extent.height << "\n";
    std::cout << "Swapchain created\n";
}

VkSurfaceFormatKHR SwapChain::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
    for(const auto& availableFormat : availableFormats)
    {
        if(availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
           availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR SwapChain::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
{
    for(const auto& availablePresentMode : availablePresentModes)
    {
        if(availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities)
{
    if(capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_Window, &width, &height);

    VkExtent2D actualExtent{
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };

    actualExtent.width = std::clamp(
        actualExtent.width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width
    );

    actualExtent.height = std::clamp(
        actualExtent.height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height
    );

    return actualExtent;
}

void SwapChain::CreateDepthResources(
    VkPhysicalDevice physicalDevice,
    VkFormat depthFormat
){
    m_DepthFormat = depthFormat;

    m_DepthImages.clear();
    m_DepthImageViews.clear();

    m_DepthImages.reserve(m_Images.size());
    m_DepthImageViews.reserve(m_Images.size());

    for(size_t i = 0; i < m_Images.size(); i++){
        auto depthImage = std::make_unique<Image>(
            physicalDevice,
            m_Device,
            m_Extent.width,
            m_Extent.height,
            depthFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        auto depthImageView = std::make_unique<ImageView>(
            m_Device,
            *depthImage,
            depthFormat,
            VK_IMAGE_ASPECT_DEPTH_BIT
        );

        m_DepthImages.push_back(std::move(depthImage));
        m_DepthImageViews.push_back(std::move(depthImageView));
    }

    std::cout << "Depth resources created: " << m_DepthImages.size() << "\n";
}

void SwapChain::CreateFramebuffers(VkRenderPass renderPass)
{
    createFramebuffers(renderPass);
}

void SwapChain::createImageViews()
{
    m_ImageViews.clear();
    m_ImageViews.reserve(m_Images.size());

    for(size_t i = 0; i < m_Images.size(); i++)
    {
        m_ImageViews.push_back(
            std::make_unique<ImageView>(m_Device, m_Images[i], m_ImageFormat)
        );
    }

    std::cout << "Image views created: " << m_ImageViews.size() << "\n";
}

void SwapChain::createFramebuffers(VkRenderPass renderPass)
{
    m_Framebuffers.resize(m_ImageViews.size());

    for(size_t i = 0; i < m_ImageViews.size(); i++)
    {
        VkImageView attachments[] = {
            *m_ImageViews[i],
            *m_DepthImageViews[i]
        }; // 加入深度图像视图

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_Extent.width;
        framebufferInfo.height = m_Extent.height;
        framebufferInfo.layers = 1;

        if(vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_Framebuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create framebuffer!");
        }
    }

    std::cout << "Framebuffers created: " << m_Framebuffers.size() << "\n";
}

void SwapChain::cleanupSwapChain()
{
    for(auto framebuffer : m_Framebuffers)
    {
        vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
    }
    m_Framebuffers.clear();

    m_DepthImageViews.clear(); // 清理深度图像视图
    m_DepthImages.clear(); // 清理深度图像

    m_ImageViews.clear(); // 清理图像视图

    if(m_SwapChain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
        m_SwapChain = VK_NULL_HANDLE;
    }
}
} // namespace vkp
