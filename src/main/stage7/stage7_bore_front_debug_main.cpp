#include "scene/water/bore/BoreFrontField.h"
#include "scene/water/bore/FrontParameterLUT.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem> 

// 对一块 1024×1024 米的水面区域，逐像素计算涌潮波前场的各种属性，
// 并输出为灰度图（PGM 格式），用于人工检查视觉效果。
namespace
{
void WritePGM(
    const std::string& path,
    const std::vector<float>& values,
    uint32_t width,
    uint32_t height
)
{
    if(values.size() != static_cast<size_t>(width) * static_cast<size_t>(height)){
        throw std::runtime_error("WritePGM size mismatch");
    }

    float minValue = values[0];
    float maxValue = values[0];

    for(float value : values){
        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
    }

    float range = std::max(maxValue - minValue, 1e-6f);

    std::ofstream file(path, std::ios::binary);

    if(!file.is_open()){
        throw std::runtime_error("Failed to open PGM file: " + path);
    }

    file << "P5\n";
    file << width << " " << height << "\n";
    file << "255\n";

    for(float value : values){
        float normalized =
            (value - minValue) / range;

        uint8_t byte =
            static_cast<uint8_t>(
                std::clamp(normalized, 0.0f, 1.0f) * 255.0f
            );

        file.write(
            reinterpret_cast<const char*>(&byte),
            1
        );
    }
}
}

int main()
{
    try{
        std::filesystem::create_directories("output/stage7");

        constexpr uint32_t width = 512;
        constexpr uint32_t height = 512;
        constexpr float worldSize = 1024.0f;

        water::BoreFrontParams params{};
        params.origin = glm::vec2(0.0f);
        params.direction = glm::normalize(glm::vec2(1.0f, 0.15f));
        params.frontLength = 1000.0f;
        params.initialOffset = 0.0f;
        params.speed = 8.0f;
        params.edgeFadeFraction = 0.03f;

        water::BoreFrontField field(params);
        water::FrontLUTData lut =
            water::GenerateDeterministicFrontLUT(1024);

        std::vector<float> signedDistance(width * height);
        std::vector<float> lengthMask(width * height);
        std::vector<float> frontOffset(width * height);
        std::vector<float> normalX(width * height);
        std::vector<float> normalZ(width * height);
        std::vector<float> amplitude(width * height);
        std::vector<float> foam(width * height);
        std::vector<float> phase(width * height);

        for(uint32_t y = 0; y < height; y++){
            for(uint32_t x = 0; x < width; x++){
                float fx =
                    static_cast<float>(x) /
                    static_cast<float>(width - 1);

                float fy =
                    static_cast<float>(y) /
                    static_cast<float>(height - 1);

                glm::vec2 worldXZ{
                    (fx - 0.5f) * worldSize,
                    (fy - 0.5f) * worldSize
                };

                water::BoreFrontSample sample =
                    field.Evaluate(worldXZ, 0.0f, lut);

                size_t index =
                    static_cast<size_t>(y) *
                    static_cast<size_t>(width) +
                    static_cast<size_t>(x);

                glm::vec4 lutValue =
                    water::SampleLUTLinear(
                        lut.parameters,
                        sample.frontUClamped
                    );

                signedDistance[index] = sample.signedDistance;
                lengthMask[index] = sample.lengthMask;
                frontOffset[index] = lutValue.r;
                normalX[index] = sample.localFrontNormal.x;
                normalZ[index] = sample.localFrontNormal.y;
                amplitude[index] = sample.amplitudeMultiplier;
                foam[index] = sample.foamMultiplier;
                phase[index] = sample.profilePhaseOffset;
            }
        }

        WritePGM("output/stage7/bore_signed_distance.pgm", signedDistance, width, height);
        WritePGM("output/stage7/bore_length_mask.pgm", lengthMask, width, height);
        WritePGM("output/stage7/bore_front_offset.pgm", frontOffset, width, height);
        WritePGM("output/stage7/bore_local_normal_x.pgm", normalX, width, height);
        WritePGM("output/stage7/bore_local_normal_z.pgm", normalZ, width, height);
        WritePGM("output/stage7/bore_amplitude_multiplier.pgm", amplitude, width, height);
        WritePGM("output/stage7/bore_foam_multiplier.pgm", foam, width, height);
        WritePGM("output/stage7/bore_phase_offset.pgm", phase, width, height);

        std::cout << "Stage 7 bore front debug PGM files written\n";
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}