#include "scene/water/sources/WSTessendorfCPU.h"

#include <glm/glm.hpp>

#include <iomanip>
#include <iostream>

namespace
{
void PrintVector(const char* name, glm::vec2 value)
{
    std::cout
        << name
        << " = ("
        << value.x
        << ", "
        << value.y
        << ")\n";
}

void PrintPhillips(const char* name, float value)
{
    std::cout
        << name
        << " = "
        << value
        << "\n";
}
}

int main()
{
    water::TessendorfSpectrumParams params{};
    params.resolution = 64;
    params.patchLength = 256.0f;
    params.windDirection = glm::vec2(1.0f, 0.0f);
    params.windSpeed = 25.0f;
    params.spectrumAmplitude = 0.0005f;
    params.shortWaveDamping = 0.001f;
    params.gravity = 9.81f;
    params.choppyLambda = 1.0f;
    params.oppositeWindDamping = 0.07f;
    params.randomSeed = 1337;

    water::WSTessendorfCPU ocean(params);

    std::cout << std::fixed << std::setprecision(8);

    std::cout << "Stage 5 FFT CPU Tessendorf test\n";
    std::cout << "Resolution: " << ocean.GetResolution() << "\n";
    std::cout << "Patch length: " << ocean.GetPatchLength() << "\n";

    PrintVector("k(0,0)", ocean.GetWaveVector(0, 0));
    PrintVector("k(1,0)", ocean.GetWaveVector(1, 0));
    PrintVector("k(N-1,0)", ocean.GetWaveVector(params.resolution - 1, 0));
    PrintVector("k(0,1)", ocean.GetWaveVector(0, 1));
    PrintVector("k(0,N-1)", ocean.GetWaveVector(0, params.resolution - 1));

    PrintPhillips("P(0,0)", ocean.GetPhillipsValue(0, 0));
    PrintPhillips("P(1,0) along wind", ocean.GetPhillipsValue(1, 0));
    PrintPhillips("P(0,1) perpendicular", ocean.GetPhillipsValue(0, 1));
    PrintPhillips("P(N/2,0) high k", ocean.GetPhillipsValue(params.resolution / 2, 0));

    float checksumA = ocean.ComputeH0Checksum();

    water::WSTessendorfCPU oceanSameSeed(params);
    float checksumB = oceanSameSeed.ComputeH0Checksum();

    params.randomSeed = 9999;
    water::WSTessendorfCPU oceanDifferentSeed(params);
    float checksumC = oceanDifferentSeed.ComputeH0Checksum();

    std::cout << "H0 checksum seed 1337 A = " << checksumA << "\n";
    std::cout << "H0 checksum seed 1337 B = " << checksumB << "\n";
    std::cout << "H0 checksum seed 9999   = " << checksumC << "\n";

    std::cout << "Same seed checksum delta = " << checksumA - checksumB << "\n";
    std::cout << "Different seed checksum delta = " << checksumA - checksumC << "\n";

    return 0;
}