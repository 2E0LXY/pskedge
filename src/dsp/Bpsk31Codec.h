#pragma once

#include <string>
#include <vector>

namespace psk::dsp {

struct Bpsk31Config {
    double sampleRate = 8000.0;
    double symbolRate = 31.25;
    double carrierHz = 1000.0;
    double amplitude = 0.65;
    int preambleSymbols = 64;
    int postambleSymbols = 64;
};

class Bpsk31Codec {
public:
    explicit Bpsk31Codec(Bpsk31Config config = {});

    std::vector<int> frameTextBits(const std::string &text) const;
    std::vector<double> modulateText(const std::string &text) const;
    std::vector<int> demodulateBits(const std::vector<double> &samples) const;
    std::string demodulateText(const std::vector<double> &samples) const;

    int samplesPerSymbol() const;
    Bpsk31Config config() const;

private:
    Bpsk31Config m_config;
};

} // namespace psk::dsp
