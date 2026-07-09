#include "scene/water/debug/ScalarFieldWriter.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace water
{
void ScalarFieldWriter::WritePGM(
    const std::string& path,
    const std::vector<float>& values,
    uint32_t width,
    uint32_t height
){
    if(values.size() != static_cast<size_t>(width) * static_cast<size_t>(height)){
        throw std::runtime_error("ScalarFieldWriter::WritePGM size mismatch");
    }

    auto minMax = std::minmax_element(values.begin(), values.end());
    float minValue = *minMax.first;
    float maxValue = *minMax.second;

    float range = maxValue - minValue;

    if(range < 1e-6f){
        range = 1.0f;
    }

    std::ofstream file(path, std::ios::binary);

    if(!file){
        throw std::runtime_error("Failed to open scalar output file: " + path);
    }

    file << "P5\n";
    file << width << " " << height << "\n";
    file << "255\n";

    for(float value : values){
        float normalized = (value - minValue) / range;
        normalized = std::clamp(normalized, 0.0f, 1.0f);

        uint8_t byteValue = static_cast<uint8_t>(normalized * 255.0f);
        file.write(reinterpret_cast<const char*>(&byteValue), 1);
    }
}
}