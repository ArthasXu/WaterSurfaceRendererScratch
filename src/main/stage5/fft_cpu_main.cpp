#include "scene/water/debug/ScalarFieldWriter.h"
#include "scene/water/sources/WSTessendorfCPU.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
struct FieldStats
{
    float minValue = 0.0f;
    float maxValue = 0.0f;
    float mean = 0.0f;
    float rms = 0.0f;
    uint32_t nanCount = 0;
    uint32_t infCount = 0;
};

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

void PrintComplex(const char* name, std::complex<float> value)
{
    std::cout
        << name
        << " = ("
        << value.real()
        << ", "
        << value.imag()
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

FieldStats ComputeStats(const std::vector<float>& values)
{
    FieldStats stats{};

    if(values.empty()){
        return stats;
    }

    stats.minValue = std::numeric_limits<float>::max();
    stats.maxValue = -std::numeric_limits<float>::max();

    double sum = 0.0;
    double sumSquares = 0.0;

    for(float value : values){
        if(std::isnan(value)){
            stats.nanCount++;
            continue;
        }

        if(std::isinf(value)){
            stats.infCount++;
            continue;
        }

        stats.minValue = std::min(stats.minValue, value);
        stats.maxValue = std::max(stats.maxValue, value);

        sum += value;
        sumSquares += static_cast<double>(value) * static_cast<double>(value);
    }

    double validCount =
        static_cast<double>(values.size() - stats.nanCount - stats.infCount);

    if(validCount > 0.0){
        stats.mean = static_cast<float>(sum / validCount);
        stats.rms = static_cast<float>(std::sqrt(sumSquares / validCount));
    }

    return stats;
}

void WriteStats(
    const std::string& path,
    const std::string& name,
    const FieldStats& stats
){
    std::ofstream file(path);

    file << "field = " << name << "\n";
    file << "min = " << stats.minValue << "\n";
    file << "max = " << stats.maxValue << "\n";
    file << "mean = " << stats.mean << "\n";
    file << "rms = " << stats.rms << "\n";
    file << "nanCount = " << stats.nanCount << "\n";
    file << "infCount = " << stats.infCount << "\n";
}

void WriteField(
    const std::string& directory,
    const std::string& name,
    const std::vector<float>& values,
    uint32_t width,
    uint32_t height
){
    std::string pgmPath = directory + "/" + name + ".pgm";
    std::string statsPath = directory + "/" + name + ".txt";

    water::ScalarFieldWriter::WritePGM(
        pgmPath,
        values,
        width,
        height
    );

    WriteStats(
        statsPath,
        name,
        ComputeStats(values)
    );
}

std::vector<float> ExtractHeight(const water::CPUWaterSurfaceFrame& frame)
{
    std::vector<float> values(frame.displacement.size());

    for(size_t i = 0; i < values.size(); i++){
        values[i] = frame.displacement[i].y;
    }

    return values;
}

std::vector<float> ExtractDisplacementX(const water::CPUWaterSurfaceFrame& frame)
{
    std::vector<float> values(frame.displacement.size());

    for(size_t i = 0; i < values.size(); i++){
        values[i] = frame.displacement[i].x;
    }

    return values;
}

std::vector<float> ExtractDisplacementZ(const water::CPUWaterSurfaceFrame& frame)
{
    std::vector<float> values(frame.displacement.size());

    for(size_t i = 0; i < values.size(); i++){
        values[i] = frame.displacement[i].z;
    }

    return values;
}

std::vector<float> ExtractJacobian(const water::CPUWaterSurfaceFrame& frame)
{
    std::vector<float> values(frame.displacement.size());

    for(size_t i = 0; i < values.size(); i++){
        values[i] = frame.displacement[i].w;
    }

    return values;
}

std::vector<float> ExtractSlopeX(const water::CPUWaterSurfaceFrame& frame)
{
    std::vector<float> values(frame.normalAux.size());

    for(size_t i = 0; i < values.size(); i++){
        values[i] = frame.normalAux[i].x;
    }

    return values;
}

std::vector<float> ExtractSlopeZ(const water::CPUWaterSurfaceFrame& frame)
{
    std::vector<float> values(frame.normalAux.size());

    for(size_t i = 0; i < values.size(); i++){
        values[i] = frame.normalAux[i].y;
    }

    return values;
}

void WriteDebugFields(
    water::WSTessendorfCPU& ocean,
    const std::string& directory,
    const std::string& suffix
){
    const water::CPUWaterSurfaceFrame& frame = ocean.GetFrame();

    WriteField(
        directory,
        "height_" + suffix,
        ExtractHeight(frame),
        frame.resolution,
        frame.resolution
    );

    WriteField(
        directory,
        "disp_x_" + suffix,
        ExtractDisplacementX(frame),
        frame.resolution,
        frame.resolution
    );

    WriteField(
        directory,
        "disp_z_" + suffix,
        ExtractDisplacementZ(frame),
        frame.resolution,
        frame.resolution
    );

    WriteField(
        directory,
        "slope_x_" + suffix,
        ExtractSlopeX(frame),
        frame.resolution,
        frame.resolution
    );

    WriteField(
        directory,
        "slope_z_" + suffix,
        ExtractSlopeZ(frame),
        frame.resolution,
        frame.resolution
    );

    WriteField(
        directory,
        "jacobian_" + suffix,
        ExtractJacobian(frame),
        frame.resolution,
        frame.resolution
    );
}

void RunWaveVectorAndPhillipsTest()
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

    PrintComplex("h0(0,0)", ocean.GetH0(0, 0));
    PrintComplex("h0(1,0)", ocean.GetH0(1, 0));
    PrintComplex("h0MinusConj(1,0)", ocean.GetH0MinusConjugate(1, 0));

    ocean.ComputeAtTime(1.25f);

    std::cout << "Hermitian max error at t=1.25 = "
        << ocean.GetLastHermitianMaxError()
        << "\n";

    std::cout << "Height max imaginary residual = "
        << ocean.GetLastMaxImaginaryResidual()
        << "\n";
}

void RunDeterminismTest()
{
    water::TessendorfSpectrumParams params{};
    params.resolution = 64;
    params.randomSeed = 1337;

    water::WSTessendorfCPU oceanA(params);
    oceanA.ComputeAtTime(2.0f);

    water::WSTessendorfCPU oceanB(params);
    oceanB.ComputeAtTime(2.0f);

    params.randomSeed = 9999;

    water::WSTessendorfCPU oceanC(params);
    oceanC.ComputeAtTime(2.0f);

    float checksumA = oceanA.ComputeFrameChecksum();
    float checksumB = oceanB.ComputeFrameChecksum();
    float checksumC = oceanC.ComputeFrameChecksum();

    std::cout << "Frame checksum seed 1337 A = " << checksumA << "\n";
    std::cout << "Frame checksum seed 1337 B = " << checksumB << "\n";
    std::cout << "Frame checksum seed 9999   = " << checksumC << "\n";
    std::cout << "Same seed frame delta = " << checksumA - checksumB << "\n";
    std::cout << "Different seed frame delta = " << checksumA - checksumC << "\n";
}

void RunNaiveVsFFTWTest()
{
    water::TessendorfSpectrumParams params{};
    params.resolution = 16;
    params.patchLength = 256.0f;
    params.randomSeed = 1337;

    water::WSTessendorfCPU ocean(params);

    water::FFTValidationStats stats =
        ocean.ValidateNaiveAgainstFFTW(1.25f);

    std::cout << "Naive vs FFTW max abs real error = "
        << stats.maxAbsRealError
        << "\n";

    std::cout << "Naive vs FFTW mean abs real error = "
        << stats.meanAbsRealError
        << "\n";

    std::cout << "Naive vs FFTW relative RMS error = "
        << stats.relativeRmsError
        << "\n";

    std::cout << "Naive vs FFTW max imaginary residual = "
        << stats.maxImaginaryResidual
        << "\n";
}

void RunPeriodicityTest()
{
    water::TessendorfSpectrumParams params{};
    params.resolution = 64;
    params.patchLength = 256.0f;
    params.randomSeed = 1337;

    water::WSTessendorfCPU ocean(params);
    ocean.ComputeAtTime(2.0f);

    water::WaterSurfaceSample a =
        ocean.Sample(glm::vec2(17.3f, -41.8f));

    water::WaterSurfaceSample b =
        ocean.Sample(glm::vec2(17.3f + params.patchLength, -41.8f));

    water::WaterSurfaceSample c =
        ocean.Sample(glm::vec2(17.3f, -41.8f + params.patchLength));

    std::cout << "Period X height delta = "
        << a.height - b.height
        << "\n";

    std::cout << "Period Z height delta = "
        << a.height - c.height
        << "\n";
}

void RunWindDirectionTest()
{
    water::TessendorfSpectrumParams paramsX{};
    paramsX.resolution = 64;
    paramsX.windDirection = glm::vec2(1.0f, 0.0f);

    water::TessendorfSpectrumParams paramsZ = paramsX;
    paramsZ.windDirection = glm::vec2(0.0f, 1.0f);

    water::WSTessendorfCPU oceanX(paramsX);
    water::WSTessendorfCPU oceanZ(paramsZ);

    std::cout << "Wind X P(1,0) = "
        << oceanX.GetPhillipsValue(1, 0)
        << "\n";

    std::cout << "Wind X P(0,1) = "
        << oceanX.GetPhillipsValue(0, 1)
        << "\n";

    std::cout << "Wind Z P(1,0) = "
        << oceanZ.GetPhillipsValue(1, 0)
        << "\n";

    std::cout << "Wind Z P(0,1) = "
        << oceanZ.GetPhillipsValue(0, 1)
        << "\n";
}

void RunWindSpeedTest()
{
    water::TessendorfSpectrumParams params10{};
    params10.windSpeed = 10.0f;

    water::TessendorfSpectrumParams params25 = params10;
    params25.windSpeed = 25.0f;

    water::TessendorfSpectrumParams params40 = params10;
    params40.windSpeed = 40.0f;

    water::WSTessendorfCPU ocean10(params10);
    water::WSTessendorfCPU ocean25(params25);
    water::WSTessendorfCPU ocean40(params40);

    std::cout << "Wind 10 P(1,0) = "
        << ocean10.GetPhillipsValue(1, 0)
        << "\n";

    std::cout << "Wind 25 P(1,0) = "
        << ocean25.GetPhillipsValue(1, 0)
        << "\n";

    std::cout << "Wind 40 P(1,0) = "
        << ocean40.GetPhillipsValue(1, 0)
        << "\n";
}

void RunDebugOutput()
{
    std::filesystem::create_directories("debug/stage5_fft_cpu");

    water::TessendorfSpectrumParams params{};
    params.resolution = 64;
    params.patchLength = 256.0f;
    params.windDirection = glm::vec2(1.0f, 0.0f);
    params.windSpeed = 25.0f;
    params.randomSeed = 1337;

    water::WSTessendorfCPU ocean(params);

    ocean.ComputeAtTime(0.0f);
    WriteDebugFields(ocean, "debug/stage5_fft_cpu", "t000");

    ocean.ComputeAtTime(1.0f);
    WriteDebugFields(ocean, "debug/stage5_fft_cpu", "t100");

    std::cout << "Debug fields written to debug/stage5_fft_cpu\n";
}
}

int main()
{
    std::cout << std::fixed << std::setprecision(8);

    RunWaveVectorAndPhillipsTest(); // 验证波矢量生成是否正确
    RunDeterminismTest(); // 检查相同随机种子输入是否产生完全相同的输出
    RunNaiveVsFFTWTest(); // 将朴素 IDFT 结果与 FFTW 结果对比
    RunPeriodicityTest(); // 测试 Sample 函数的周期边界条件
    RunWindDirectionTest(); // 验证方向性因子
    RunWindSpeedTest(); // 验证风速越高，波浪能量越大
    RunDebugOutput(); // 输出两个时刻（t=0 和 t=1）的可视化数据

    return 0;
}