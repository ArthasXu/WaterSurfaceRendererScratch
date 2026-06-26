#pragma once

namespace core
{
class Timestep
{
public:
    explicit Timestep(float seconds = 0.0f)
        : m_Seconds(seconds) // 构造函数，初始化秒数
    {
        
    }

    float GetSeconds() const { return m_Seconds; } // 获取秒数

    float GetMilliseconds() const { return m_Seconds * 1000.0f; } // 获取毫秒数

    operator float() const { return m_Seconds; } // 隐式转换为float类型

private:
    float m_Seconds = 0.0f; // 秒数
};
}