#include "dsp/Bpsk31Codec.h"
#include "dsp/PskVaricode.h"

#include <iostream>
#include <string>

int main()
{
    std::string error;
    if (!psk::dsp::PskVaricode::validateTable(&error)) {
        std::cerr << "Varicode table invalid: " << error << '\n';
        return 1;
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
