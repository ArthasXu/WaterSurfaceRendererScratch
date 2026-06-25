#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace vkp
{
class ShaderModule
{
public:
    ShaderModule(VkDevice device, const std::string& path);
    ~ShaderModule();

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    operator VkShaderModule() const;
    VkShaderModule GetHandle() const;

private:
    std::vector<char> readFile(const std::string& filename); // 读取文件
    void createShaderModule(const std::vector<char>& code); // 创建着色器模块

private:
    VkDevice m_Device = VK_NULL_HANDLE;                 // 逻辑设备句柄
    VkShaderModule m_ShaderModule = VK_NULL_HANDLE;     // 着色器模块句柄
};
}