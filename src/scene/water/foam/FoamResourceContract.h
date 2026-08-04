#pragma once

#include <glm/glm.hpp>

namespace water
{
struct alignas(16) FoamParamsUBO
{
    glm::vec4 animation;       // 动画时间、滚动周期、世界缩放、细节强度
    glm::vec4 sourceStrength;  // 四种泡沫源的权重（Profile/Slope/Jacobian/Breaking）
    glm::vec4 thresholds;      // 斜率泡沫和 Jacobian 泡沫的阈值区间
    glm::vec4 appearance;      // 覆盖阈值、软化度、泡沫法线强度、状态泡沫混合系数
    glm::vec4 state;           // 状态泡沫的增益、衰减、扩散、开关
    glm::vec4 runtime;         // 运行时参数
    glm::vec4 domain;          // 世界大小、分辨率、时间步长
    
    // ===== FF MF_FluidFoam =====
    glm::vec4 foamShallow{0.3f, 1.0f, -0.05f, 0.2f};  // 浅水偏置, 浅水尺度(1/米), 硬度强度, 硬度宽度
    glm::vec4 foamSoft{0.5f, 0.5f, 1.0f, 0.6f};       // 软晕随速度, 软晕基底, 软晕上限, 泡沫总不透明度
};

struct alignas(16) FoamSimulationUBO
{
    glm::vec4 domain;
    glm::vec4 simulation;
    glm::vec4 solver;
};
}