#include "SimpleFFT.h"

#include <cmath>

namespace psk::dsp {

std::size_t SimpleFFT::nextPowerOfTwo(std::size_t n)
{
    std::size_t p = 1;
    while (p < n) {
        p <<= 1;
    }
    return p;
}

void SimpleFFT::forward(std::vector<std::complex<double>> &data)
{
    const std::size_t n = data.size();
    if (n <= 1) {
        return;
    }

    // Bit-reversal permutation.
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * M_PI / static_cast<double>(len);
        const std::complex<double> wLen(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = data[i + k];
                const std::complex<double> v = data[i + k + len / 2] * w;
                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;
                w *= wLen;
            }
        }
    }
}

} // namespace psk::dsp
