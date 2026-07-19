#pragma once

#include <vector>

namespace psk::dsp::ft8 {

// 14-bit CRC for FT8/FT4, generator polynomial 0x6757 per the QEX paper
// (Section 3), computed on the source-encoded message zero-extended from
// 77 to 82 bits, initial value zero. Bit-array form of the polynomial
// (15 bits including the leading 1, matching the reference
// implementation's crc14poly exactly - this project's own CRC16 class
// uses a different, unrelated polynomial for a different mode's FEC, not
// reused here) verified against the reference implementation directly
// rather than independently re-derived from the hex value alone.
class Crc14 {
public:
    // messageBits82: exactly 82 bits (77-bit message + 5 zero-padding
    // bits, per the spec). Returns 14 CRC bits.
    static std::vector<int> compute(const std::vector<int> &messageBits82);
};

} // namespace psk::dsp::ft8
