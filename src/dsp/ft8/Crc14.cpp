#include "Crc14.h"

namespace psk::dsp::ft8 {

std::vector<int> Crc14::compute(const std::vector<int> &messageBits82)
{
    // Generator polynomial as a 15-bit array (leading 1 + the 14 bits of
    // 0x2757), matching the reference implementation exactly - standard
    // CRC long-division: append 14 zero bits (the CRC's own width) to
    // the message, then repeatedly XOR the divisor into the message
    // wherever the leading bit is 1, shifting through.
    static const std::vector<int> divisor = {1, 1, 0, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1};
    const int divisorLen = static_cast<int>(divisor.size());

    std::vector<int> work = messageBits82;
    work.insert(work.end(), 14, 0);

    const int messageLen = static_cast<int>(messageBits82.size());
    for (int i = 0; i < messageLen; ++i) {
        if (work[static_cast<std::size_t>(i)] == 1) {
            for (int j = 0; j < divisorLen; ++j) {
                work[static_cast<std::size_t>(i + j)] ^= divisor[static_cast<std::size_t>(j)];
            }
        }
    }

    return std::vector<int>(work.end() - 14, work.end());
}

} // namespace psk::dsp::ft8
