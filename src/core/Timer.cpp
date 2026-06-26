#include "core/Timer.h"

namespace core
{
Timer::Timer()
    : m_LastTime(std::chrono::high_resolution_clock::now()) // 初始化上一次调用的时间点为当前时间
{

}

Timestep Timer::Tick()
{
    auto now = std::chrono::high_resolution_clock::now(); // 获取当前时间
    std::chrono::duration<float> duration = now - m_LastTime; // 计算时间间隔
    m_LastTime = now; // 更新上一次调用的时间点为当前时间

    return Timestep(duration.count()); // 返回时间间隔的秒数
}

}