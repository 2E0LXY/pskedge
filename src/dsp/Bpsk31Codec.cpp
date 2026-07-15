#include "Bpsk31Codec.h"

#include "PskVaricode.h"

#include <cmath>

namespace psk::dsp {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

double wrapPhase(double phase)
{
    while (phase <= -kPi) {
        phase += 2.0 * kPi;
    }
    while (phase > kPi) {
        phase -= 2.0 * kPi;
    }
    return phase;
}

} // namespace

Bpsk31Codec::Bpsk31Codec(Bpsk31Config config)
    : m_config(config)
{
}

std::vector<int> Bpsk31Codec::frameTextBits(const std::string &text) const
{
    std::vector<int> bits;
    bits.reserve(m_config.preambleSymbols + text.size() * 8 + m_config.postambleSymbols);

    for (int i = 0; i < m_config.preambleSymbols; ++i) {
        bits.push_back(0);
    }

    const std::vector<int> payload = PskVaricode::encodeTextBits(text);
    bits.insert(bits.end(), payload.begin(), payload.end());

    for (int i = 0; i < m_config.postambleSymbols; ++i) {
        bits.push_back(1);
    }

    return bits;
}

std::vector<double> Bpsk31Codec::modulateText(const std::string &text) const
{
    const std::vector<int> bits = frameTextBits(text);
    const int sps = samplesPerSymbol();
    std::vector<double> samples;
    samples.reserve(bits.size() * sps);

    double carrierPhase = 0.0;
    double dataPhase = 0.0;
    const double carrierStep = 2.0 * kPi * m_config.carrierHz / m_config.sampleRate;

    for (int bit : bits) {
        if (bit == 0) {
            dataPhase = wrapPhase(dataPhase + kPi);
        }

        for (int i = 0; i < sps; ++i) {
            samples.push_back(m_config.amplitude * std::cos(carrierPhase + dataPhase));
            carrierPhase = wrapPhase(carrierPhase + carrierStep);
        }
    }

    return samples;
}

std::vector<int> Bpsk31Codec::demodulateBits(const std::vector<double> &samples) const
{
    const int sps = samplesPerSymbol();
    const int symbols = static_cast<int>(samples.size()) / sps;
    std::vector<int> bits;
    bits.reserve(symbols);

    const double carrierStep = 2.0 * kPi * m_config.carrierHz / m_config.sampleRate;
    double oscillatorPhase = 0.0;
    double previousPhase = 0.0;
    bool havePrevious = false;

    for (int symbol = 0; symbol < symbols; ++symbol) {
        double iSum = 0.0;
        double qSum = 0.0;
        for (int n = 0; n < sps; ++n) {
            const double sample = samples[static_cast<std::size_t>(symbol * sps + n)];
            iSum += sample * std::cos(oscillatorPhase);
            qSum += -sample * std::sin(oscillatorPhase);
            oscillatorPhase = wrapPhase(oscillatorPhase + carrierStep);
        }

        const double phase = std::atan2(qSum, iSum);
        if (!havePrevious) {
            previousPhase = phase;
            havePrevious = true;
            continue;
        }

        const double delta = std::abs(wrapPhase(phase - previousPhase));
        bits.push_back(delta > (kPi / 2.0) ? 0 : 1);
        previousPhase = phase;
    }

    return bits;
}

std::string Bpsk31Codec::demodulateText(const std::vector<double> &samples) const
{
    return PskVaricode::decodeTextBits(demodulateBits(samples));
}

int Bpsk31Codec::samplesPerSymbol() const
{
    return static_cast<int>(std::lround(m_config.sampleRate / m_config.symbolRate));
}

Bpsk31Config Bpsk31Codec::config() const
{
    return m_config;
}

} // namespace psk::dsp
