#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>

namespace water
{
struct WaterVertex
{
    glm::vec3 position; // 位置
    glm::vec3 normal;   // 法线
    glm::vec2 uv;       // 纹理坐标

    static VkVertexInputBindingDescription GetBindingDescription(); // 获取绑定描述

    static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions(); // 获取属性描述
};
}