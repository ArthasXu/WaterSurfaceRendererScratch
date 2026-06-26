#pragma once

#include <spdlog/spdlog.h>

namespace core
{
class Log
{
public:
    static void Init();
};
}

#define VKP_INFO(...)  spdlog::info(__VA_ARGS__) // 用来输出信息
#define VKP_WARN(...)  spdlog::warn(__VA_ARGS__) // 用来输出警告
#define VKP_ERROR(...) spdlog::error(__VA_ARGS__) // 用来输出错误