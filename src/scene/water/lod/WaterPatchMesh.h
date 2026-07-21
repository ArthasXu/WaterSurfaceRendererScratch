#pragma once

#include "vulkan/Buffer.h"
#include "vulkan/CommandPool.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace water
{
struct WaterPatchVertex
{
    glm::vec2 localXZ{0.0f};    // 顶点在补丁局部空间中的标准化二维坐标，范围是 [0, 1] × [0, 1]。实际世界空间位置会通过 UBO 传入的缩放和偏移量在着色器中动态计算
    float skirt = 0.0f;         // 一个标记变量，用于指示该顶点是否为“裙边”顶点。值为 0.0 表示普通水面顶点，值为 1.0 表示是裙边顶点。在顶点着色器中，当检测到是裙边顶点时，会将其沿垂直方向（Y 轴）向下偏移，形成一个垂直的“边缘墙”，以填补 LOD 层级之间的接缝
    float padding = 0.0f;

    static VkVertexInputBindingDescription GetBindingDescription();

    static std::array<VkVertexInputAttributeDescription, 2>
        GetAttributeDescriptions();
};

class WaterPatchMesh
{
public:
    WaterPatchMesh(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        vkp::CommandPool& commandPool,
        VkQueue graphicsQueue,
        uint32_t cellCount
    );

    void Bind(VkCommandBuffer commandBuffer) const;

    void Draw(VkCommandBuffer commandBuffer) const;

    uint32_t GetIndexCount() const;

private:
    void GeneratePatch(uint32_t cellCount);

    void CreateVertexBuffer(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        vkp::CommandPool& commandPool,
        VkQueue graphicsQueue
    );

    void CreateIndexBuffer(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        vkp::CommandPool& commandPool,
        VkQueue graphicsQueue
    );

private:
    std::vector<WaterPatchVertex> m_Vertices;
    std::vector<uint32_t> m_Indices;

    std::unique_ptr<vkp::Buffer> m_VertexBuffer;
    std::unique_ptr<vkp::Buffer> m_IndexBuffer;

    uint32_t m_IndexCount = 0;
};
}