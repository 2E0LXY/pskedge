#include "dsp/Bpsk31Codec.h"
#include "dsp/PskVaricode.h"

#include <iostream>
#include <string>
#include <unordered_map>

namespace {

// Golden vectors from the ARRL/G3PLX PSK31 Varicode specification
// (https://www.arrl.org/psk31-spec). A pure round-trip test cannot catch a
// wrong table entry because encode and decode share the same table; these
// vectors are transcribed independently to catch that class of bug.
bool checkGoldenVaricodeVectors(std::string *error)
{
    static const std::unordered_map<unsigned char, std::string> golden = {
        {' ', "1"}, {'e', "11"}, {'t', "101"}, {'a', "1011"},
        {'C', "10101101"}, {'Q', "1111011101"}, {'K', "101111101"},
        {'0', "10110111"}, {'9', "110110111"},
    };

    for (const auto &[ch, expected] : golden) {
        const std::string actual = psk::dsp::PskVaricode::codeForAscii(ch);
        if (actual != expected) {
            if (error) {
                *error = std::string("Varicode mismatch for '") + static_cast<char>(ch)
                    + "': expected " + expected + " got " + actual;
            }
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    std::string error;
    if (!psk::dsp::PskVaricode::validateTable(&error)) {
        std::cerr << "Varicode table invalid: " << error << '\n';
        return 1;
    }

    if (!checkGoldenVaricodeVectors(&error)) {
        std::cerr << "Varicode golden vector check failed: " << error << '\n';
        return 4;
    }

    std::string printable;
    for (char ch = 32; ch < 127; ++ch) {
        printable.push_back(ch);
    }

    const auto printableBits = psk::dsp::PskVaricode::encodeTextBits(printable);
    const std::string printableRoundtrip = psk::dsp::PskVaricode::decodeTextBits(printableBits);
    if (printableRoundtrip != printable) {
        std::cerr << "Varicode printable roundtrip failed\n";
        return 2;
    }

    const std::string message = "CQ CQ DE 2E0LXY 2E0LXY K";
    psk::dsp::Bpsk31Codec codec;
    const std::vector<double> samples = codec.modulateText(message);
    const std::string decoded = codec.demodulateText(samples);
    if (decoded.find(message) == std::string::npos) {
        std::cerr << "BPSK31 roundtrip failed\n";
        std::cerr << "Expected fragment: " << message << '\n';
        std::cerr << "Decoded: " << decoded << '\n';
        return 3;
    }

    std::cout << "PSK core self-test passed\n";
    std::cout << "Decoded: " << decoded << '\n';
    return 0;
}
