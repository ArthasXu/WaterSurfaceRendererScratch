#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace water
{
struct FrontLUTData
{
    uint32_t resolution = 0; // LUT 纹理的分辨率（一维，沿波前线的采样点数）

    // 参数纹理（即之前说的 Front LUT 纹理）。每个 vec4 的通道含义：
        // • R = frontOffset（波前偏移量，米）
        // • G = amplitudeMultiplier（波峰振幅缩放系数）
        // • B = foamMultiplier（泡沫强度系数）
        // • A = profilePhaseOffset（Wave Profile 相位偏移）
    std::vector<glm::vec4> parameters;
    
    // 导数纹理。每个 vec4 的通道含义：
        // • R = dfrontOffset/du（偏移量对纹理坐标 U 的导数）
        // • G, B, A 目前为预留通道，可存储其他参数的导数
    std::vector<glm::vec4> derivatives;
};

FrontLUTData GenerateDeterministicFrontLUT(uint32_t resolution);

glm::vec4 SampleLUTLinear(
    const std::vector<glm::vec4>& values,
    float u
);

float ComputeAnalyticOffset(float u);
float ComputeAnalyticOffsetDerivative(float u);
}