#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>

struct Vertex
{
    glm::vec3 position; // 顶点位置
    glm::vec3 color; // 顶点颜色
    glm::vec2 uv; // 顶点纹理坐标

    static VkVertexInputBindingDescription GetBindingDescription(); // 静态函数，用于获取顶点输入绑定描述
    static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions(); // 静态函数，用于获取顶点输入属性描述
};