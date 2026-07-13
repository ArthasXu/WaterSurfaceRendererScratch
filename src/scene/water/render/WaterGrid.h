#pragma once

#include "scene/water/render/WaterVertex.h"

#include "vulkan/Buffer.h"
#include "vulkan/CommandPool.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace water
{
enum class WaterGridUploadMode
{
    DynamicHostVisible,
    StaticDeviceLocal
}; // 用于指定水网格上传模式的枚举类
struct WaterGridConfig
{
    uint32_t cellCountX = 128; // 单元格数量 X
    uint32_t cellCountZ = 128; // 单元格数量 Z

    float sizeX = 256.0f; // 网格大小 X
    float sizeZ = 256.0f; // 网格大小 Z

    glm::vec3 origin{0.0f}; // 网格原点
}; // 用于配置水网格的结构体

class WaterGrid
{
public:
    WaterGrid(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        vkp::CommandPool& commandPool,
        VkQueue graphicsQueue,
        const WaterGridConfig& config,
        WaterGridUploadMode uploadMode = WaterGridUploadMode::DynamicHostVisible
    ); // 构造函数

    void Bind(VkCommandBuffer commandBuffer) const; // 绑定顶点和索引缓冲区到命令缓冲区
    void Draw(VkCommandBuffer commandBuffer) const; // 绘制水网格

    const std::vector<WaterVertex>& GetBaseVertices() const; // 获取基础顶点数据

    void UpdateVertices(const std::vector<WaterVertex>& vertices); // 更新顶点数据

private:
    void GenerateGrid(); // 生成水网格
    void CreateVertexBuffer(
        VkPhysicalDevice physicalDevice, 
        VkDevice device,
        vkp::CommandPool& commandPool,
        VkQueue graphicsQueue
    ); // 创建顶点缓冲区
    void CreateIndexBuffer(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        vkp::CommandPool& commandPool,
        VkQueue graphicsQueue
    ); // 创建索引缓冲区

private:
    WaterGridConfig m_Config; // 水网格配置
    WaterGridUploadMode m_UploadMode = WaterGridUploadMode::DynamicHostVisible; // 水网格上传模式

    std::vector<WaterVertex> m_BaseVertices; // 基础顶点数据
    std::vector<uint32_t> m_Indices; // 索引数据

    std::unique_ptr<vkp::Buffer> m_VertexBuffer; // 顶点缓冲区
    std::unique_ptr<vkp::Buffer> m_IndexBuffer; // 索引缓冲区

    uint32_t m_IndexCount = 0; // 索引数量
};
}