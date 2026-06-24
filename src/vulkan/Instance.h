#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace vkp
{
    class Instance{
    public:
        explicit Instance(bool enableValidationLayers); // 禁止编译器进行隐式类型转换
        ~Instance();

        Instance(const Instance&) = delete; // 禁止拷贝构造函数，拷贝构造是浅拷贝，会导致两个对象指向同一块内存
        Instance& operator=(const Instance&) = delete; // 禁止赋值运算符

        operator VkInstance() const; // 隐式类型转换，用于将 Instance 转换为 VkInstance
        VkInstance get() const;
    
    private:
        void createInstance(); // 创建 Vulkan 实例
        void setupDebugMessenger(); // 设置调试回调

        bool checkValidationLayerSupport() const; // 检查是否支持校验层
        std::vector<const char*> getRequiredExtensions() const; // 获取所需的扩展
        void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const; // 填充 VkDebugUtilsMessengerCreateInfoEXT 结构体

    private:
        VkInstance m_Instance = VK_NULL_HANDLE;                     // Vulkan 实例
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE; // 调试消息传递对象
        bool m_EnableValidationLayers = false;                      // 是否启用校验层
    };
}