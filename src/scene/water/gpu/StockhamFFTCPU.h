#pragma once

#include <complex>
#include <cstdint>
#include <vector>

namespace water
{
std::vector<std::complex<float>> StockhamIFFT1DCPU(
    const std::vector<std::complex<float>>& input
);

std::vector<std::complex<float>> StockhamIFFT2DCPU(
    const std::vector<std::complex<float>>& input,
    uint32_t resolution
);
}