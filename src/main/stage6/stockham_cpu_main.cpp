#include "scene/water/gpu/StockhamFFTCPU.h"

#include <fftw3.h>

#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

namespace
{
struct ErrorStats
{
    float maxAbsError = 0.0f;
    float meanAbsError = 0.0f;
    float relativeRmsError = 0.0f;
    uint32_t nanCount = 0;
    uint32_t infCount = 0;
};

float ComplexAbs(std::complex<float> value)
{
    return std::sqrt(
        value.real() * value.real() +
        value.imag() * value.imag()
    );
}

std::vector<std::complex<float>> MakeRandomComplexField(
    uint32_t count,
    uint32_t seed
)
{
    std::mt19937 generator(seed);
    std::normal_distribution<float> distribution(0.0f, 1.0f);

    std::vector<std::complex<float>> result(count);

    for(std::complex<float>& value : result){
        value = {
            distribution(generator),
            distribution(generator)
        };
    }

    return result;
}

std::vector<std::complex<float>> FFTWIFFT1D(
    const std::vector<std::complex<float>>& input
)
{
    const uint32_t n = static_cast<uint32_t>(input.size());

    fftw_complex* fftwInput = reinterpret_cast<fftw_complex*>(
        fftw_malloc(sizeof(fftw_complex) * input.size())
    );

    fftw_complex* fftwOutput = reinterpret_cast<fftw_complex*>(
        fftw_malloc(sizeof(fftw_complex) * input.size())
    );

    if(fftwInput == nullptr || fftwOutput == nullptr){
        throw std::runtime_error("Failed to allocate FFTW 1D buffers");
    }

    for(size_t i = 0; i < input.size(); i++){
        fftwInput[i][0] = input[i].real();
        fftwInput[i][1] = input[i].imag();
    }

    fftw_plan plan = fftw_plan_dft_1d(
        static_cast<int>(n),
        fftwInput,
        fftwOutput,
        FFTW_BACKWARD,
        FFTW_ESTIMATE
    );

    if(plan == nullptr){
        throw std::runtime_error("Failed to create FFTW 1D plan");
    }

    fftw_execute(plan);

    std::vector<std::complex<float>> output(input.size());

    const double normalization = 1.0 / static_cast<double>(n);

    for(size_t i = 0; i < input.size(); i++){
        output[i] = {
            static_cast<float>(fftwOutput[i][0] * normalization),
            static_cast<float>(fftwOutput[i][1] * normalization)
        };
    }

    fftw_destroy_plan(plan);
    fftw_free(fftwInput);
    fftw_free(fftwOutput);

    return output;
}

std::vector<std::complex<float>> FFTWIFFT2D(
    const std::vector<std::complex<float>>& input,
    uint32_t resolution
)
{
    const uint32_t n = resolution;
    const size_t count = static_cast<size_t>(n) * static_cast<size_t>(n);

    if(input.size() != count){
        throw std::runtime_error("FFTWIFFT2D input size mismatch");
    }

    fftw_complex* fftwInput = reinterpret_cast<fftw_complex*>(
        fftw_malloc(sizeof(fftw_complex) * count)
    );

    fftw_complex* fftwOutput = reinterpret_cast<fftw_complex*>(
        fftw_malloc(sizeof(fftw_complex) * count)
    );

    if(fftwInput == nullptr || fftwOutput == nullptr){
        throw std::runtime_error("Failed to allocate FFTW 2D buffers");
    }

    for(size_t i = 0; i < count; i++){
        fftwInput[i][0] = input[i].real();
        fftwInput[i][1] = input[i].imag();
    }

    fftw_plan plan = fftw_plan_dft_2d(
        static_cast<int>(n),
        static_cast<int>(n),
        fftwInput,
        fftwOutput,
        FFTW_BACKWARD,
        FFTW_ESTIMATE
    );

    if(plan == nullptr){
        throw std::runtime_error("Failed to create FFTW 2D plan");
    }

    fftw_execute(plan);

    std::vector<std::complex<float>> output(count);

    const double normalization = 1.0 / static_cast<double>(count);

    for(size_t i = 0; i < count; i++){
        output[i] = {
            static_cast<float>(fftwOutput[i][0] * normalization),
            static_cast<float>(fftwOutput[i][1] * normalization)
        };
    }

    fftw_destroy_plan(plan);
    fftw_free(fftwInput);
    fftw_free(fftwOutput);

    return output;
}

ErrorStats CompareComplexFields(
    const std::vector<std::complex<float>>& a,
    const std::vector<std::complex<float>>& b
)
{
    if(a.size() != b.size()){
        throw std::runtime_error("CompareComplexFields size mismatch");
    }

    ErrorStats stats{};

    double absErrorSum = 0.0;
    double squaredErrorSum = 0.0;
    double referenceSquaredSum = 0.0;

    for(size_t i = 0; i < a.size(); i++){
        if(std::isnan(a[i].real()) || std::isnan(a[i].imag()) ||
            std::isnan(b[i].real()) || std::isnan(b[i].imag())){
            stats.nanCount++;
            continue;
        }

        if(std::isinf(a[i].real()) || std::isinf(a[i].imag()) ||
            std::isinf(b[i].real()) || std::isinf(b[i].imag())){
            stats.infCount++;
            continue;
        }

        std::complex<float> difference = a[i] - b[i];

        float absError = ComplexAbs(difference);
        float referenceAbs = ComplexAbs(b[i]);

        stats.maxAbsError = std::max(stats.maxAbsError, absError);

        absErrorSum += absError;
        squaredErrorSum +=
            static_cast<double>(absError) *
            static_cast<double>(absError);

        referenceSquaredSum +=
            static_cast<double>(referenceAbs) *
            static_cast<double>(referenceAbs);
    }

    const double validCount =
        static_cast<double>(a.size() - stats.nanCount - stats.infCount);

    if(validCount > 0.0){
        stats.meanAbsError =
            static_cast<float>(absErrorSum / validCount);
    }

    if(referenceSquaredSum > 1e-20){
        stats.relativeRmsError =
            static_cast<float>(
                std::sqrt(squaredErrorSum / referenceSquaredSum)
            );
    }

    return stats;
}

void PrintStats(
    const char* name,
    const ErrorStats& stats
)
{
    std::cout
        << name
        << "\n  maxAbsError = " << stats.maxAbsError
        << "\n  meanAbsError = " << stats.meanAbsError
        << "\n  relativeRmsError = " << stats.relativeRmsError
        << "\n  nanCount = " << stats.nanCount
        << "\n  infCount = " << stats.infCount
        << "\n";
}

void Run1DTest(uint32_t resolution)
{
    std::vector<std::complex<float>> input =
        MakeRandomComplexField(resolution, 1000 + resolution);

    std::vector<std::complex<float>> stockham =
        water::StockhamIFFT1DCPU(input);

    std::vector<std::complex<float>> fftw =
        FFTWIFFT1D(input);

    ErrorStats stats =
        CompareComplexFields(stockham, fftw);

    std::cout << "1D Stockham IFFT test N = " << resolution << "\n";
    PrintStats("Stockham vs FFTW", stats);

    if(stats.nanCount != 0 || stats.infCount != 0){
        throw std::runtime_error("1D Stockham produced NaN or Inf");
    }

    if(stats.relativeRmsError > 1e-5f){
        throw std::runtime_error("1D Stockham relative RMS error too high");
    }
}

void Run2DTest(uint32_t resolution)
{
    const uint32_t count = resolution * resolution;

    std::vector<std::complex<float>> input =
        MakeRandomComplexField(count, 2000 + resolution);

    std::vector<std::complex<float>> stockham =
        water::StockhamIFFT2DCPU(input, resolution);

    std::vector<std::complex<float>> fftw =
        FFTWIFFT2D(input, resolution);

    ErrorStats stats =
        CompareComplexFields(stockham, fftw);

    std::cout << "2D Stockham IFFT test N = " << resolution << "\n";
    PrintStats("Stockham vs FFTW", stats);

    if(stats.nanCount != 0 || stats.infCount != 0){
        throw std::runtime_error("2D Stockham produced NaN or Inf");
    }

    if(stats.relativeRmsError > 1e-5f){
        throw std::runtime_error("2D Stockham relative RMS error too high");
    }
}

}

int main()
{
    try{
        Run1DTest(8);
        Run1DTest(16);

        Run2DTest(8);
        Run2DTest(16);

        std::cout << "CPU Stockham reference validation passed\n";
    }
    catch(const std::exception& e){
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}