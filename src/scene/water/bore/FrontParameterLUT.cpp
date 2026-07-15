#include "scene/water/bore/FrontParameterLUT.h"

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

// Front LUT（波前查找表）的解析生成与线性采样。它为 BoreFrontField 提供可复现的波前形状数据，
// 让涌潮的弯曲锋线、振幅分布、泡沫强度和相位偏移等特性能够被预先计算并高效查询
namespace water
{
// 给定归一化的波前线坐标 u ∈ [0,1]，返回该点的波前偏移量（米）
// 后续可以用美术手工绘制的曲线替换它，但现阶段用解析函数保证可复现性
float ComputeAnalyticOffset(float u)
{
    float envelope =
        std::sin(glm::pi<float>() * u);

    envelope *= envelope;

    float value =
        8.0f * std::sin(2.0f * glm::pi<float>() * u + 0.3f) +
        3.0f * std::sin(4.0f * glm::pi<float>() * u + 1.1f) +
        1.5f * std::sin(6.0f * glm::pi<float>() * u + 2.2f);

    return envelope * value;
}

// 计算 ComputeAnalyticOffset(u) 对 u 的导数
// 这个导数会在 BoreFrontField::Evaluate 中被换算成世界空间导数，用于修正局部波前法线。
// 如果只有偏移而没有导数，弯曲的波前就会用错误的方向推动水面，导致坡度异常。
float ComputeAnalyticOffsetDerivative(float u)
{
    float pi = glm::pi<float>();

    float sinPiU = std::sin(pi * u);
    float envelope = sinPiU * sinPiU;

    float envelopeDerivative =
        2.0f * sinPiU * std::cos(pi * u) * pi;

    float value =
        8.0f * std::sin(2.0f * pi * u + 0.3f) +
        3.0f * std::sin(4.0f * pi * u + 1.1f) +
        1.5f * std::sin(6.0f * pi * u + 2.2f);

    float valueDerivative =
        8.0f * 2.0f * pi * std::cos(2.0f * pi * u + 0.3f) +
        3.0f * 4.0f * pi * std::cos(4.0f * pi * u + 1.1f) +
        1.5f * 6.0f * pi * std::cos(6.0f * pi * u + 2.2f);

    return envelopeDerivative * value +
        envelope * valueDerivative;
}

// 给定一个浮点数数组 values（vec4[]）和纹理坐标 u ∈ [0,1]，执行线性插值并返回采样结果
// 这是 CPU 端 LUT 采样的基础函数，BoreFrontField::Evaluate 用它来查询 parameters 和 derivatives
glm::vec4 SampleLUTLinear(
    const std::vector<glm::vec4>& values,
    float u
)
{
    if(values.empty()){
        throw std::runtime_error("SampleLUTLinear values is empty");
    }

    float clampedU =
        glm::clamp(u, 0.0f, 1.0f);

    float x =
        clampedU *
        static_cast<float>(values.size() - 1);

    uint32_t i0 =
        static_cast<uint32_t>(std::floor(x));

    uint32_t i1 =
        std::min<uint32_t>(
            i0 + 1,
            static_cast<uint32_t>(values.size() - 1)
        );

    float t =
        x - static_cast<float>(i0);

    return values[i0] * (1.0f - t) +
        values[i1] * t;
}

// 生成一个确定性的 FrontLUTData 对象，包含 parameters 和 derivatives 两个 vec4 数组
// 输出：一个固定的 FrontLUTData，可以在程序启动时生成一次，然后在每帧的 Evaluate 中反复采样。由于没有随机数参与，每次运行结果一致，便于调试
// 后续当你需要更复杂的艺术效果时，只需替换 GenerateDeterministicFrontLUT 中的生成逻辑，
// 或者直接加载预先烘焙好的 LUT 纹理，而整个管线接口保持不变
FrontLUTData GenerateDeterministicFrontLUT(uint32_t resolution)
{
    if(resolution < 2){
        throw std::runtime_error("Front LUT resolution must be at least 2");
    }

    FrontLUTData result{};
    result.resolution = resolution;
    result.parameters.resize(resolution);
    result.derivatives.resize(resolution);

    for(uint32_t i = 0; i < resolution; i++){
        float u =
            static_cast<float>(i) /
            static_cast<float>(resolution - 1);

        float offset =
            ComputeAnalyticOffset(u);

        float amplitude =
            1.0f +
            0.15f * std::sin(2.0f * glm::pi<float>() * u + 0.7f);

        float foam =
            1.0f +
            0.2f * std::sin(4.0f * glm::pi<float>() * u + 1.4f);

        float phase =
            0.5f +
            0.5f * std::sin(2.0f * glm::pi<float>() * u + 2.1f);

        float dOffsetDu =
            ComputeAnalyticOffsetDerivative(u);

        result.parameters[i] =
            glm::vec4(offset, amplitude, foam, phase);

        result.derivatives[i] =
            glm::vec4(dOffsetDu, 0.0f, 0.0f, 0.0f);
    }

    return result;
}
}