#include "Surface.h"

#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept> // std::runtime_error

namespace vkp
{
Surface::Surface(VkInstance instance, GLFWwindow* window)
    : m_Instance(instance) {
    // VkResult glfwCreateWindowSurface(
    //     VkInstance instance,                // Vulkan 实例
    //     GLFWwindow* window,                 // GLFW 窗口
    //     const VkAllocationCallbacks* allocator, // 内存分配器（常为 nullptr）
    //     VkSurfaceKHR* surface               // 输出：创建的 Surface 句柄
    // ); // glfw 做好了封装
    if(glfwCreateWindowSurface(instance, window, nullptr, &m_Surface) != VK_SUCCESS){ // 创建窗口表面
        throw std::runtime_error("Failed to create window surface!");
    }
    std::cout<<"Window surface created: OK\n";
}

Surface::~Surface(){
    if(m_Surface != VK_NULL_HANDLE){
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr); // 销毁窗口表面
    }
}

VkSurfaceKHR Surface::get() const{ // 获取窗口表面
    return m_Surface;
}

Surface::operator VkSurfaceKHR() const{ // 隐式类型转换，用于将 Surface 转换为 VkSurfaceKHR
    return m_Surface;
}

}