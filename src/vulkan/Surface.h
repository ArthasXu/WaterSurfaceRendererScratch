#pragma once

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace vkp
{
class Surface
{
public:
    Surface(VkInstance instance, GLFWwindow* window);       // 创建窗口表面
    ~Surface();                                             // 销毁窗口表面

    Surface(const Surface&) = delete;                       // 禁止拷贝构造
    Surface& operator=(const Surface&) = delete;            // 禁止拷贝赋值

    operator VkSurfaceKHR() const;                          // 隐式类型转换，用于将 Surface 转换为 VkSurfaceKHR
    VkSurfaceKHR get() const;                               // 获取窗口表面

private:
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;                // 窗口表面
    VkInstance m_Instance = VK_NULL_HANDLE;                 // Vulkan 实例
};
}