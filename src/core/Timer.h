#pragma once

#include "core/Timestep.h"

#include <chrono> // 用于时间测量

namespace core
{
class Timer
{
public:
    Timer(); // 构造函数，初始化计时器

    Timestep Tick(); // 计算自上次调用以来的时间间隔

private:
    std::chrono::high_resolution_clock::time_point m_LastTime; // 记录上一次调用的时间点
};
}