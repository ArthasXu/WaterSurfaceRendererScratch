#include "core/Log.h"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace core
{
void Log::Init()
{
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v"); // 设置日志格式,格式为[时间] [日志级别] 日志内容
    spdlog::set_level(spdlog::level::trace); // 设置日志级别,这里设置为trace,即输出所有级别的日志
}
}