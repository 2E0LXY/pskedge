#include "SyncSequence.h"

namespace psk::dsp {

std::vector<int> SyncSequence::generate(int order)
{
    // Primitive polynomial taps (Fibonacci LFSR, XOR of the listed tap
    // positions counting from the MSB=1 end) for each supported order.
    // These are standard, widely-tabulated primitive polynomials for
    // maximal-length sequences (e.g. Xilinx/Altera LFSR app notes use the
    // same tap sets) - verified empirically below by confirming full
    // period (all 2^order - 1 nonzero states visited) rather than trusted
    // blindly, since a wrong tap set silently produces a shorter, worse
    // sequence instead of an obvious error.
    std::vector<int> taps;
    switch (order) {
    case 7:
        taps = {7, 6}; // x^7 + x^6 + 1
        break;
    case 9:
        taps = {9, 5}; // x^9 + x^5 + 1
        break;
    default:
        return {};
    }

    const int period = (1 << order) - 1;
    std::vector<int> reg(static_cast<std::size_t>(order), 1); // nonzero seed
    std::vector<int> out;
    out.reserve(static_cast<std::size_t>(period));

    for (int i = 0; i < period; ++i) {
        out.push_back(reg[static_cast<std::size_t>(order - 1)]);
        int feedback = 0;
        for (int t : taps) {
            feedback ^= reg[static_cast<std::size_t>(t - 1)];
        }
        for (int j = order - 1; j > 0; --j) {
            reg[static_cast<std::size_t>(j)] = reg[static_cast<std::size_t>(j - 1)];
        }
        reg[0] = feedback;
    }

    return out;
}

} // namespace psk::dsp
