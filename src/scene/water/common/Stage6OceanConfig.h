#pragma once

#include "scene/water/common/FFTResourceContract.h"
#include "scene/water/sources/WSTessendorfCascadesCPU.h"

#include <array>

namespace water
{
struct Stage6OceanConfig
{
    MultiCascadeParams spectrum{};

    std::array<float, kMaxFFTCascades> amplitudeScales{
        1.0f,
        1.0f,
        0.1f
    };
};

inline Stage6OceanConfig MakeStage6ReferenceOceanConfig()
{
    TessendorfSpectrumParams base{};                                // 用于创建多层级联
    base.resolution = 128;                                          // 频域网格分辨率（N x N），决定波浪细节的丰富程度和 FFT 计算量
    base.windDirection = glm::normalize(glm::vec2(1.0f, 0.25f));    // 主风向（二维单位向量），控制波浪能量的方向性，这里略微偏向 x 轴
    base.windSpeed = 25.0f;                                         // 风速（米/秒），影响 Phillips 谱的强度，风速越大波浪越高
    base.spectrumAmplitude = 2.5f;                                  // 全局频谱振幅缩放因子，用于整体调节波高
    base.shortWaveDamping = 0.001f;                                 // 短波阻尼系数，抑制高频毛细波的强度，使海面更平滑自然
    base.gravity = 9.81f;                                           // 重力加速度（m/s^2），用于色散关系 ω = √(g k)
    base.choppyLambda = 3.0f;                                       // choppy 水平位移强度系数（典型值 0.5~1.5），越大波峰越尖锐
    base.oppositeWindDamping = 0.07f;                               // 逆风方向波浪能量的额外衰减因子，减少背风面的波浪
    base.randomSeed = 1337;                                         // 随机种子，确保同一种子生成完全相同的海面（可重现性）

    Stage6OceanConfig config{};                                     // 多层级联参数
    config.spectrum.baseSpectrum = base;                            // 基础频谱参数
    config.spectrum.resolution = base.resolution;                   // 频域网格分辨率（N x N），决定波浪细节的丰富程度和 FFT 计算量
    config.spectrum.baseSeed = 1337;                                // 随机种子，确保同一种子生成完全相同的海面（可重现性）
    config.spectrum.shortPatchLength = 64.0f;                       // 短波补丁边长（米），决定短波波长和频域采样间距
    config.spectrum.midPatchLength = 256.0f;                        // 中波补丁边长（米），决定中波波长和频域采样间距
    config.spectrum.longPatchLength = 1024.0f;                      // 长波补丁边长（米），决定长波波长和频域采样间距

    return config;
}
}