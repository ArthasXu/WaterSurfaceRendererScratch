#include "ShaderModule.h"

#include <fstream>
#include <stdexcept>

namespace vkp
{
ShaderModule::ShaderModule(VkDevice device, const std::string& path)
    : m_Device(device)
{
    auto code = readFile(path); // 读取着色器代码
    createShaderModule(code); // 创建着色器模块
}

ShaderModule::~ShaderModule()
{
    if(m_ShaderModule != VK_NULL_HANDLE){
        vkDestroyShaderModule(m_Device, m_ShaderModule, nullptr); // 销毁着色器模块
    }
}

ShaderModule::operator VkShaderModule() const
{
    return m_ShaderModule;
}

VkShaderModule ShaderModule::GetHandle() const
{
    return m_ShaderModule;
}

std::vector<char> ShaderModule::readFile(const std::string& filename){ // 读取文件
    std::ifstream file(filename, std::ios::ate | std::ios::binary); // 以二进制模式打开文件，从末尾开始读取
    if(!file.is_open()){ // 如果文件未打开
        throw std::runtime_error("Failed to open file: " + filename); // 抛出异常
    }

    size_t fileSize = (size_t)file.tellg(); // 获取文件大小
    std::vector<char> buffer(fileSize); // 创建缓冲区

    file.seekg(0); // 从文件开头开始读取
    file.read(buffer.data(), fileSize); // 读取文件
    file.close(); // 关闭文件

    return buffer; // 返回缓冲区
}

void ShaderModule::createShaderModule(const std::vector<char>& code){ // 创建着色器模块
    VkShaderModuleCreateInfo createInfo{}; // 着色器模块创建信息
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO; // 结构体类型
    createInfo.codeSize = code.size(); // 着色器代码大小
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data()); // 着色器代码指针, pCode 要 uint32_t*，因为 SPIR-V 按 32-bit word 存储

    // VkResult vkCreateShaderModule(
    //     VkDevice                                    device,          // 逻辑设备
    //     const VkShaderModuleCreateInfo*             pCreateInfo,     // 着色器模块创建信息
    //     const VkAllocationCallbacks*                pAllocator,      // 自定义内存分配器，常为 nullptr
    //     VkShaderModule*                             pShaderModule    // 输出：着色器模块句柄
    // );
    if(vkCreateShaderModule(m_Device, &createInfo, nullptr, &m_ShaderModule) != VK_SUCCESS){ // 创建着色器模块
        throw std::runtime_error("Failed to create shader module!"); // 失败
    }
}
}