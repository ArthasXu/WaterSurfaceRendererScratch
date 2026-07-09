#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace water
{
class ScalarFieldWriter
{
public:
    static void WritePGM(
        const std::string& path,
        const std::vector<float>& values,
        uint32_t width,
        uint32_t height
    );
};
}