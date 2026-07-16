#pragma once

#include <complex>
#include <vector>

namespace psk::dsp {

// Self-contained radix-2 Cooley-Tukey FFT. No external DSP library is
// vendored in this project (PSK31_RESEARCH.md lists FFTW/KissFFT/pocketfft
// as options, none currently integrated), and pulling one in for a single
// FFT call in a prototype block-sync codec isn't warranted yet - this is
// a deliberately small, self-contained implementation, verified against a
// direct O(n^2) DFT for correctness (see selftest) rather than trusted
// blindly. Input size must be a power of 2.
class SimpleFFT {
public:
    // In-place forward FFT. data.size() must be a power of 2.
    static void forward(std::vector<std::complex<double>> &data);

    // Returns the next power of 2 >= n.
    static std::size_t nextPowerOfTwo(std::size_t n);
};

} // namespace psk::dsp
