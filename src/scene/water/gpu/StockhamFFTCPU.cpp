#include "scene/water/gpu/StockhamFFTCPU.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <stdexcept>

namespace
{
// 判断一个整数是否为 2 的幂（FFT 要求输入长度必须是 2 的幂）
bool IsPowerOfTwo(uint32_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}
}

namespace water
{
// 基于 Stockham 算法的一维逆快速傅里叶变换（IFFT）
// 输入：频域复数序列（长度必须为 2 的幂）
// 输出：空间域复数序列（已归一化）
std::vector<std::complex<float>> StockhamIFFT1DCPU(
    const std::vector<std::complex<float>>& input
)
{
    const uint32_t n = static_cast<uint32_t>(input.size());

    // 输入长度必须是 2 的幂
    if(!IsPowerOfTwo(n)){
        throw std::runtime_error("StockhamIFFT1DCPU input size must be power of two");
    }

    // ping-pong 双缓冲区策略：
    // ping = 当前级输入，pong = 当前级输出，每轮结束后交换，避免重复分配内存
    std::vector<std::complex<float>> ping = input;
    std::vector<std::complex<float>> pong(input.size());

    // 虚数单位 i，用于构造复指数 exp(i * angle)
    const std::complex<float> imaginaryUnit{0.0f, 1.0f};

    // 外层循环：p 是当前合并的子序列半长（1, 2, 4, 8, ...）
    // 对应论文中的“步长”或“间隔”
    for(uint32_t p = 1; p < n; p <<= 1){
        // r = 每个子序列块的数量 = n / (2 * p)
        // 每轮将相邻两个长度为 p 的子序列合并为一个长度为 2p 的子序列
        const uint32_t r = n / (2 * p);

        // j 循环：遍历每一对需要合并的子序列
        for(uint32_t j = 0; j < r; j++){
            // k 循环：遍历子序列内的每个元素（0 ~ p-1）
            for(uint32_t k = 0; k < p; k++){
                // ---------- 输入索引（从 ping 中读取）----------
                // Stockham 的蝶形配对：同一对子序列中相隔 n/2 的两个元素
                const uint32_t input0 = j * p + k;       // 前一个子序列的第 k 个元素
                const uint32_t input1 = input0 + n / 2;  // 后一个子序列的第 k 个元素

                // ---------- 输出索引（写入 pong）----------
                // Stockham 自动顺序输出：合并后的 2p 长度块按顺序写入
                const uint32_t output0 = j * (2 * p) + k;      // 块起始 + k
                const uint32_t output1 = output0 + p;           // 块的后半部分

                // ---------- 旋转因子（Twiddle Factor）----------
                // 对于逆变换，使用 exp(+i * angle)
                // angle = 2π * k * r / n
                const float angle =
                    glm::two_pi<float>() *
                    static_cast<float>(k * r) /
                    static_cast<float>(n);

                const std::complex<float> twiddle =
                    std::exp(imaginaryUnit * angle);

                // ---------- 蝶形运算（Radix-2）----------
                const std::complex<float> a = ping[input0];       // 不加权的值
                const std::complex<float> b = twiddle * ping[input1]; // 旋转后的值

                // 和与差写入连续位置（自动有序输出的关键）
                pong[output0] = a + b;   // 和
                pong[output1] = a - b;   // 差
            }
        }

        // 交换缓冲区：本轮的输出 pong 变为下一轮的输入 ping
        ping.swap(pong);
    }

    // 逆变换需要归一化：所有值除以 N
    const float normalization = 1.0f / static_cast<float>(n);

    for(std::complex<float>& value : ping){
        value *= normalization;
    }

    // 最终结果在 ping 中（因为最后 swap 后 ping 持有最终输出）
    return ping;
}

// 基于行-列分解的二维逆快速傅里叶变换（2D IFFT）
// 算法：先对每一行做1D IFFT，再对每一列做1D IFFT
// 输入：频谱数据（一维数组，按行优先存储，大小为 resolution * resolution）
// resolution：二维网格的宽度/高度（必须是2的幂）
// 输出：空间域复数数组（已归一化，虚部应接近于0）
std::vector<std::complex<float>> StockhamIFFT2DCPU(
    const std::vector<std::complex<float>>& input,
    uint32_t resolution
)
{
    const uint32_t n = resolution;
    const size_t count = static_cast<size_t>(n) * static_cast<size_t>(n);

    // 分辨率必须是2的幂
    if(!IsPowerOfTwo(n)){
        throw std::runtime_error("StockhamIFFT2DCPU resolution must be power of two");
    }

    // 输入数组大小必须匹配
    if(input.size() != count){
        throw std::runtime_error("StockhamIFFT2DCPU input size mismatch");
    }

    // ---------- 第一步：对每一行做1D IFFT ----------
    std::vector<std::complex<float>> rowInput(n);   // 暂存当前行的数据
    std::vector<std::complex<float>> rowOutput(count); // 存储行变换后的中间结果

    for(uint32_t z = 0; z < n; z++){
        // 提取第z行的所有列数据（按行优先存储，行内连续）
        for(uint32_t x = 0; x < n; x++){
            rowInput[x] = input[
                static_cast<size_t>(z) * static_cast<size_t>(n) +
                static_cast<size_t>(x)
            ];
        }

        // 对该行执行一维 IFFT
        std::vector<std::complex<float>> transformedRow =
            StockhamIFFT1DCPU(rowInput);

        // 将变换结果写回中间数组的对应行
        for(uint32_t x = 0; x < n; x++){
            rowOutput[
                static_cast<size_t>(z) * static_cast<size_t>(n) +
                static_cast<size_t>(x)
            ] = transformedRow[x];
        }
    }

    // ---------- 第二步：对每一列做1D IFFT ----------
    std::vector<std::complex<float>> columnInput(n);   // 暂存当前列的数据
    std::vector<std::complex<float>> output(count);    // 最终输出数组

    for(uint32_t x = 0; x < n; x++){
        // 提取第x列的所有行数据（跳跃读取，步长为n）
        for(uint32_t z = 0; z < n; z++){
            columnInput[z] = rowOutput[
                static_cast<size_t>(z) * static_cast<size_t>(n) +
                static_cast<size_t>(x)
            ];
        }

        // 对该列执行一维 IFFT
        std::vector<std::complex<float>> transformedColumn =
            StockhamIFFT1DCPU(columnInput);

        // 将变换结果写回最终输出数组的对应列
        for(uint32_t z = 0; z < n; z++){
            output[
                static_cast<size_t>(z) * static_cast<size_t>(n) +
                static_cast<size_t>(x)
            ] = transformedColumn[z];
        }
    }

    return output;
}

}