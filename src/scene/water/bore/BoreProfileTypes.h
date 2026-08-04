#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace water
{
// 涌潮波前剖面的动画模式
enum class BoreProfileAnimationMode : int32_t
{
    OneShot = 0,  // 一次性事件：从形成、抬升、翻卷到破碎的完整生命周期，播放完就结束
    Looping = 1   // 循环模式：剖面动画不断循环播放，用于持续近岸破浪或局部翻卷
};

// 涌潮波前剖面的配置参数，控制潮头的几何形状和动态行为
struct BoreWaveProfileConfig
{
    // ===== 纹理分辨率 =====
    uint32_t distanceResolution = 1024;  // 距离轴(U轴)的采样点数，决定波形剖面的空间精度
    uint32_t phaseResolution = 128;      // 相位轴(V轴)的采样点数，决定动画的帧数/平滑度

    // ===== 空间范围 =====
    float profileHalfWidth = 200.0f;     // 剖面覆盖的半宽度(米)，从波前峰向前后各延伸30米
                                         // 决定了波前影响的总宽度范围

    // ===== 潮头主体形状 =====
    float crestHeight = 4.0f;            // 波峰最大高度(米)，潮头最高点相对于水平面的垂直位移
    float forwardDisplacement = 2.5f;    // 最大前向水平位移(米)，潮头向前推进时的最大推拉量
    float crestWidth = 6.0f;             // 波峰宽度(米)，控制潮头尖峰的横向范围
    float rearTroughDepth = 0.6f;        // 后坡波谷深度(米)，潮头后方的水面下凹程度

    // ===== 尾迹波纹 =====
    float trailAmplitude = 0.25f;        // 尾迹波纹的振幅(米)，潮头后方跟随的次级波纹高度
    float trailWavelength = 12.0f;       // 尾迹波纹的波长(米)，潮后波纹的间距
    float trailDecayLength = 22.0f;      // 尾迹波纹的衰减距离(米)，潮后波纹逐渐消失的距离

    // ===== 流速 =====
    float baseFlowSpeed = 7.0f;          // 基础水流速度(米/秒)，潮头本体之外的背景流速
    float crestFlowBoost = 4.0f;         // 波峰流速增强(米/秒)，潮头顶部因重力作用产生的额外流速

    // ===== 动画时长 =====
    float duration = 24.0f;              // 单次OneShot的总时长(秒)，或Looping模式下一个完整周期的时间
};

// 涌潮波前剖面的预计算数据容器，包含位移纹理和导数纹理
struct BoreWaveProfileData
{
    uint32_t width = 0;                  // 纹理宽度(距离轴分辨率)
    uint32_t height = 0;                 // 纹理高度(相位轴分辨率)

    float profileHalfWidth = 0.0f;       // 剖面半宽度(与配置一致，供着色器采样时使用)
    float duration = 0.0f;               // 动画时长(与配置一致)

    // 位移纹理：每像素存储 (forward位移, upward位移, foamSource, crestMask)
    std::vector<glm::vec4> displacement;

    // 导数纹理：每像素存储 (dForward/ds, dUpward/ds, flowSpeed, breakingWeight)
    std::vector<glm::vec4> derivative;
};
}