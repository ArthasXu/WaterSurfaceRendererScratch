#include "scene/water/foam/FoamDetailGenerator.h"

#include <cmath>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if(!condition){
        throw std::runtime_error(message);
    }
}
}

int main()
{
    try{
        water::FoamDetailTextureData data =
            water::GenerateFoamDetailTexture(256, 256, 1337u);

        water::FoamDetailTextureData repeat =
            water::GenerateFoamDetailTexture(256, 256, 1337u);

        Require(data.width == 256, "Unexpected foam width");
        Require(data.height == 256, "Unexpected foam height");
        Require(data.pixels.size() == 256u * 256u, "Invalid foam pixel count");
        Require(data.pixels.size() == repeat.pixels.size(), "Repeat pixel count mismatch");

        uint64_t coverageSum = 0;
        uint64_t normalXSum = 0;
        uint64_t normalZSum = 0;

        for(size_t i = 0; i < data.pixels.size(); i++){
            const water::FoamDetailPixel& a =
                data.pixels[i];

            const water::FoamDetailPixel& b =
                repeat.pixels[i];

            Require(a.coverage == b.coverage, "Foam generation is not deterministic");
            Require(a.normalX == b.normalX, "Foam normalX is not deterministic");
            Require(a.normalZ == b.normalZ, "Foam normalZ is not deterministic");
            Require(a.breakup == b.breakup, "Foam breakup is not deterministic");

            coverageSum += a.coverage;
            normalXSum += a.normalX;
            normalZSum += a.normalZ;
        }

        for(uint32_t y = 0; y < data.height; y++){
            size_t left =
                static_cast<size_t>(y) *
                data.width;

            size_t right =
                static_cast<size_t>(y) *
                data.width +
                data.width - 1;

            Require(data.pixels[left].coverage == data.pixels[right].coverage, "Foam left/right coverage seam");
            Require(data.pixels[left].normalX == data.pixels[right].normalX, "Foam left/right normalX seam");
            Require(data.pixels[left].normalZ == data.pixels[right].normalZ, "Foam left/right normalZ seam");
            Require(data.pixels[left].breakup == data.pixels[right].breakup, "Foam left/right breakup seam");
        }

        for(uint32_t x = 0; x < data.width; x++){
            size_t top = x;

            size_t bottom =
                static_cast<size_t>(data.height - 1) *
                data.width +
                x;

            Require(data.pixels[top].coverage == data.pixels[bottom].coverage, "Foam top/bottom coverage seam");
            Require(data.pixels[top].normalX == data.pixels[bottom].normalX, "Foam top/bottom normalX seam");
            Require(data.pixels[top].normalZ == data.pixels[bottom].normalZ, "Foam top/bottom normalZ seam");
            Require(data.pixels[top].breakup == data.pixels[bottom].breakup, "Foam top/bottom breakup seam");
        }

        float invCount =
            1.0f /
            static_cast<float>(data.pixels.size());

        float coverageMean =
            static_cast<float>(coverageSum) *
            invCount /
            255.0f;

        float normalXMean =
            static_cast<float>(normalXSum) *
            invCount /
            255.0f;

        float normalZMean =
            static_cast<float>(normalZSum) *
            invCount /
            255.0f;

        Require(coverageMean > 0.05f && coverageMean < 0.95f, "Foam coverage mean is degenerate");
        Require(std::abs(normalXMean - 0.5f) < 0.12f, "Foam normalX mean is biased");
        Require(std::abs(normalZMean - 0.5f) < 0.12f, "Foam normalZ mean is biased");

        std::filesystem::create_directories("output/stage9");

        water::WriteFoamDetailDebugPGMs(
            "output/stage9",
            data
        );

        std::cout << "Stage9 foam detail CPU validation passed\n";
        std::cout << "coverageMean = " << coverageMean << "\n";
        std::cout << "normalXMean = " << normalXMean << "\n";
        std::cout << "normalZMean = " << normalZMean << "\n";

        return 0;
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }
}