#pragma once

#include <stdexcept>

#define VKP_ASSERT(condition, message) \
    do { if (!(condition)) { throw std::runtime_error(message); } } while(false)
    // 计算 condition，若为 false，则抛出异常中断，异常信息为 message