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
    glm::vec4 absorptionCoeff{0.234f, 0.153f, 0.116f, 0.0f}; // 深水档 RGB 每米吸收（FF _SurfaceAbsorption0）
    glm::vec4 shallowParams{0.85f, 6.0f, 0.0f, 0.0f};     // x=河床反照率强度 y=最大可见水深(米)
    
    // ===== FF MF_WaterTransition：深水档 / 岸线档 =====
    glm::vec4 absorptionShore{0.163f, 0.092f, 0.084f, 0.0f};  // 岸线档吸收(1/m)
    glm::vec4 scatteringDeep{0.006f, 0.009f, 0.012f, 8.0f};   // xyz=深水档散射(1/m), w=散射增益(入射光强)
    glm::vec4 scatteringShore{0.020f, 0.020f, 0.020f, 4.0f};  // xyz=岸线档散射(1/m), w=FoamScatteringScale
    glm::vec4 shoreBlend{17.0f, 200.0f, 0.0f, 0.0f};          // x=深度归一(米) y=离岸归一(米) z=PhaseG w=水面基准高度
    glm::vec4 colorBehind{0.63f, 0.54f, 0.45f, 1.0f};         // FF ColorBehind：水下背景色调
};
}