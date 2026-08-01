#pragma once

#include <glm/glm.hpp>

namespace water
{
// opticalParams.x = fresnelPower
// opticalParams.y = reflectionStrength
// opticalParams.z = absorptionStrength
// opticalParams.w = sedimentAmount

// lightParams.xyz = sunDirection
// lightParams.w = specularStrength

// fogParams.x = fogStart
// fogParams.y = fogEnd
// fogParams.z = horizonFade
// fogParams.w = reserved
struct alignas(16) WaterMaterialUBO
{
    glm::vec4 shallowColor;     // 浅水区基础颜色（RGB），控制近岸/浅滩的水色
    glm::vec4 deepColor;        // 深水区基础颜色（RGB），控制远海/深水的水色
    glm::vec4 sedimentColor;    // 泥沙颜色（RGB），与浅水色混合模拟浑浊水体
    glm::vec4 opticalParams;    // 光学参数（fresnelPower, reflectionStrength, absorptionStrength, sedimentAmount）
    glm::vec4 lightParams;      // 光照参数（sunDirection.xyz, specularStrength）
    glm::vec4 fogParams;        // 雾与远景参数（fogStart, fogEnd, horizonFade, reserved）
    glm::vec4 absorptionCoeff{0.35f, 0.06f, 0.03f, 0.0f}; // RGB 每米吸收，红最快
    glm::vec4 shallowParams{0.85f, 6.0f, 0.0f, 0.0f};     // x=河床反照率强度 y=最大可见水深(米)
};
}