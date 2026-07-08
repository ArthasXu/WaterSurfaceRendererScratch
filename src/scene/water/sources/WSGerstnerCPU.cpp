#include "scene/water/sources/WSGerstnerCPU.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace water
{
WSGerstnerCPU::WSGerstnerCPU(std::vector<GerstnerWave> waves)
    : m_Waves(std::move(waves))
{
    for(GerstnerWave& wave : m_Waves){ // 归一化波的方向
        if(glm::length(wave.direction) > 0.0001f){ // 如果波的方向不为0
            wave.direction = glm::normalize(wave.direction);
        }
        else{
            wave.direction = glm::vec2(1.0f, 0.0f); // 如果波的方向为0，设置为默认值
        }

        wave.wavelength = std::max(wave.wavelength, 0.0001f); // 波的波长不能为0
    }
}

void WSGerstnerCPU::Update(float deltaTime)
{
    m_Time += deltaTime; // 时间累加 驱动波浪随时间演化
}

WaterSurfaceSample WSGerstnerCPU::Sample(glm::vec2 worldXZ) const
{   // 逐个波叠加 计算波浪高度和法线
    WaterSurfaceSample sample{};

    for(const GerstnerWave& wave : m_Waves){
        glm::vec2 direction = glm::normalize(wave.direction); // 波的方向

        float k = glm::two_pi<float>() / wave.wavelength; // 波数k = 2π / 波长λ
        float omega = k * wave.phaseSpeed; // 角频率ω = 波数k * 速度c

        float theta =
            k * glm::dot(direction, worldXZ) -
            omega * m_Time +
            wave.phaseOffset; // 波的相位=波数*方向*位置-角频率*时间+相位偏移

        float sinTheta = std::sin(theta); // 波的正弦值 
        float cosTheta = std::cos(theta); // 波的余弦值

        float denominator = std::max(
            k * wave.amplitude * static_cast<float>(m_Waves.size()),
            1e-5f
        ); 

        float q = glm::clamp(
            wave.steepness / denominator,
            0.0f,
            1.0f
        ); // 波的陡峭度q 防止多个波叠加时出现“波峰自交”

        sample.height += wave.amplitude * sinTheta; // 波浪高度=振幅*正弦值

        sample.horizontalDisplacement +=
            q * wave.amplitude * direction * cosTheta; // 水平位移=陡峭度*振幅*方向*余弦值

        sample.slope +=
            wave.amplitude * k * direction * cosTheta; // 斜率=振幅*波数*方向*余弦值
    }

    return sample;
}

void WSGerstnerCPU::ResetTime()
{
    m_Time = 0.0f;
}

float WSGerstnerCPU::GetTime() const
{
    return m_Time;
}



}