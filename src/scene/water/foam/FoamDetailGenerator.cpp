#include "scene/water/foam/FoamDetailGenerator.h"

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

// 用纯数学算法生成一张可无缝平铺的、256×256 的 RGBA8 泡沫细节纹理，并导出各个通道为 PGM 灰度图供人工验证
namespace
{
float Hash01(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;

    return static_cast<float>(x & 0x00ffffff) /
        static_cast<float>(0x01000000);
}

// 生成在 u, v ∈ [0, 1] 范围内无缝平铺的噪声值
// 为 coverage、normal、breakup 三个通道提供基础噪声，决定泡沫细胞的形状和分布
float TileableNoise(float u, float v, uint32_t seed)
{
    float value = 0.0f;
    float amplitude = 0.5f;
    float amplitudeSum = 0.0f;

    for(uint32_t octave = 0; octave < 5; octave++){
        float frequency =
            static_cast<float>(1u << octave);

        float phase0 =
            Hash01(seed + octave * 17u) *
            glm::two_pi<float>();

        float phase1 =
            Hash01(seed + octave * 29u) *
            glm::two_pi<float>();

        float wave =
            std::sin(glm::two_pi<float>() * frequency * u + phase0) *
            std::sin(glm::two_pi<float>() * frequency * v + phase1);

        value += wave * amplitude;
        amplitudeSum += amplitude;
        amplitude *= 0.5f;
    }

    value =
        value / amplitudeSum *
        0.5f +
        0.5f;

    return glm::clamp(value, 0.0f, 1.0f);
}

// 将 [0, 1] 范围的浮点数钳位并映射为 [0, 255] 的 uint8_t，用于填充 FoamDetailPixel 的各通道
uint8_t ToByte(float value)
{
    return static_cast<uint8_t>(
        glm::clamp(value, 0.0f, 1.0f) *
        255.0f
    );
}

// 将一个 uint8_t 数组以二进制 PGM（P5 格式）写入文件
// 用于调试时输出 coverage、normalX、normalZ、breakup 四个通道的独立灰度图
void WriteChannelPGM(
    const std::string& path,
    uint32_t width,
    uint32_t height,
    const std::vector<uint8_t>& values
)
{
    std::ofstream file(path, std::ios::binary);

    if(!file){
        throw std::runtime_error("Failed to open foam PGM output");
    }

    file
        << "P5\n"
        << width << " " << height << "\n"
        << "255\n";

    file.write(
        reinterpret_cast<const char*>(values.data()),
        static_cast<std::streamsize>(values.size())
    );
}
}

namespace water
{
FoamDetailTextureData GenerateFoamDetailTexture(
    uint32_t width,
    uint32_t height,
    uint32_t seed
)
{
    if(width < 2 || height < 2){
        throw std::runtime_error("Foam detail texture size must be at least 2");
    }

    FoamDetailTextureData data{};
    data.width = width;
    data.height = height;
    data.pixels.resize(static_cast<size_t>(width) * height);

    for(uint32_t y = 0; y < height; y++){
        for(uint32_t x = 0; x < width; x++){
            float u =
                static_cast<float>(x) /
                static_cast<float>(width - 1);

            float v =
                static_cast<float>(y) /
                static_cast<float>(height - 1);

            float base =
                TileableNoise(u, v, seed);

            float fine =
                TileableNoise(u * 3.0f, v * 3.0f, seed + 101u);

            // 决定哪里有泡沫细胞、哪里没有。通过混合两层噪声（base + fine），用 smoothstep 压缩对比度，
            // 使泡沫细胞不全是纯黑纯白，而是有稀疏有密集的自然分布
            float coverage =
                glm::smoothstep(
                    0.42f,
                    0.82f,
                    base * 0.7f + fine * 0.3f
                );

            float du = 1.0f / static_cast<float>(width - 1);
            float dv = 1.0f / static_cast<float>(height - 1);

            // 泡沫细胞的微观法线扰动。通过对噪声做中心有限差分近似梯度，得到一个方向向量，然后映射到 [0, 1]。
            // 这会让泡沫表面在光照下有凹凸感，而不是一个平面白块
            float nx =
                TileableNoise(u + du, v, seed) -
                TileableNoise(u - du, v, seed);

            float nz =
                TileableNoise(u, v + dv, seed) -
                TileableNoise(u, v - dv, seed);

            // 额外的高频噪声，用于后续在着色器中破坏泡沫的规律性，让边缘更自然、不呆板
            float breakup =
                TileableNoise(u * 5.0f, v * 5.0f, seed + 211u);

            FoamDetailPixel pixel{};
            pixel.coverage = ToByte(coverage);
            pixel.normalX = ToByte(nx * 2.0f + 0.5f);
            pixel.normalZ = ToByte(nz * 2.0f + 0.5f);
            pixel.breakup = ToByte(breakup);

            data.pixels[
                static_cast<size_t>(y) *
                width +
                x
            ] = pixel;
        }
    }

    return data;
}

void WriteFoamDetailDebugPGMs(
    const std::string& directory,
    const FoamDetailTextureData& data
)
{
    size_t pixelCount =
        static_cast<size_t>(data.width) *
        data.height;

    if(data.pixels.size() != pixelCount){
        throw std::runtime_error("Invalid foam detail pixel count");
    }

    std::vector<uint8_t> coverage(pixelCount);
    std::vector<uint8_t> normalX(pixelCount);
    std::vector<uint8_t> normalZ(pixelCount);
    std::vector<uint8_t> breakup(pixelCount);

    for(size_t i = 0; i < pixelCount; i++){
        coverage[i] = data.pixels[i].coverage;
        normalX[i] = data.pixels[i].normalX;
        normalZ[i] = data.pixels[i].normalZ;
        breakup[i] = data.pixels[i].breakup;
    }

    WriteChannelPGM(directory + "/foam_coverage.pgm", data.width, data.height, coverage);
    WriteChannelPGM(directory + "/foam_normal_x.pgm", data.width, data.height, normalX);
    WriteChannelPGM(directory + "/foam_normal_z.pgm", data.width, data.height, normalZ);
    WriteChannelPGM(directory + "/foam_breakup.pgm", data.width, data.height, breakup);
}
}