#include "Qpsk31Codec.h"
#include "PskVaricode.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace psk::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;

double wrapPhase(double phase)
{
    while (phase > kPi) {
        phase -= 2.0 * kPi;
    }
    while (phase < -kPi) {
        phase += 2.0 * kPi;
    }
    return phase;
}

double readInterpolated(const std::vector<double> &samples, double position)
{
    if (position < 0.0 || position >= static_cast<double>(samples.size()) - 1) {
        return 0.0;
    }
    const auto index = static_cast<std::size_t>(position);
    const double frac = position - static_cast<double>(index);
    return samples[index] * (1.0 - frac) + samples[index + 1] * frac;
}
} // namespace

Qpsk31Codec::Qpsk31Codec(Qpsk31Config config)
    : m_config(config)
{
}

Qpsk31Config Qpsk31Codec::config() const
{
    return m_config;
}

std::vector<double> Qpsk31Codec::modulateText(const std::string &text) const
{
    std::vector<int> bits(static_cast<std::size_t>(m_config.preambleSymbols), 0);
    const std::vector<int> textBits = PskVaricode::encodeTextBits(text);
    bits.insert(bits.end(), textBits.begin(), textBits.end());

    const std::vector<int> phaseShifts = QpskConvCode::encode(bits);

    // Absolute phase trajectory: each phase-shift value (0-3) advances
    // the running phase by that many quarter-turns, per the spec's own
    // convention (0=none, 1=+90, 2=180, 3=-90 i.e. +270).
    std::vector<double> symbolPhase(phaseShifts.size());
    double phase = 0.0;
    for (std::size_t i = 0; i < phaseShifts.size(); ++i) {
        phase = wrapPhase(phase + static_cast<double>(phaseShifts[i]) * (kPi / 2.0));
        symbolPhase[i] = phase;
    }

    const double sps = m_config.sampleRate / m_config.symbolRate;
    const auto spsInt = static_cast<int>(std::llround(sps));

    // Raised-cosine pulse shaping applied to the I and Q baseband
    // channels independently, same technique (and same reasoning - see
    // Bpsk31Codec.cpp's TX comment) as BPSK31's own shaping, just doing
    // it twice (once per quadrature channel) since QPSK carries two
    // independent baseband signals.
    std::vector<double> iChannel(symbolPhase.size() * static_cast<std::size_t>(spsInt), 0.0);
    std::vector<double> qChannel(iChannel.size(), 0.0);
    const int halfSpan = spsInt;
    std::vector<double> pulse(static_cast<std::size_t>(2 * halfSpan + 1));
    for (int k = -halfSpan; k <= halfSpan; ++k) {
        const double t = static_cast<double>(k) / sps;
        pulse[static_cast<std::size_t>(k + halfSpan)] = 0.5 * (1.0 + std::cos(kPi * t));
    }
    for (std::size_t sym = 0; sym < symbolPhase.size(); ++sym) {
        const double iVal = std::cos(symbolPhase[sym]);
        const double qVal = std::sin(symbolPhase[sym]);
        const auto center = static_cast<int>(sym) * spsInt;
        for (int k = -halfSpan; k <= halfSpan; ++k) {
            const int idx = center + k;
            if (idx < 0 || idx >= static_cast<int>(iChannel.size())) {
                continue;
            }
            const double w = pulse[static_cast<std::size_t>(k + halfSpan)];
            iChannel[static_cast<std::size_t>(idx)] += iVal * w;
            qChannel[static_cast<std::size_t>(idx)] += qVal * w;
        }
    }

    std::vector<double> samples(iChannel.size());
    for (std::size_t n = 0; n < samples.size(); ++n) {
        const double carrierPhase = 2.0 * kPi * m_config.carrierHz * static_cast<double>(n) / m_config.sampleRate;
        samples[n] = m_config.amplitude
            * (iChannel[n] * std::cos(carrierPhase) - qChannel[n] * std::sin(carrierPhase));
    }
    return samples;
}

std::string Qpsk31Codec::demodulateText(const std::vector<double> &samples) const
{
    const double sps = m_config.sampleRate / m_config.symbolRate;
    const int halfSpan = static_cast<int>(sps / 2.0);
    std::vector<double> pulse(static_cast<std::size_t>(2 * halfSpan + 1));
    for (int k = -halfSpan; k <= halfSpan; ++k) {
        const double t = static_cast<double>(k) / static_cast<double>(halfSpan);
        pulse[static_cast<std::size_t>(k + halfSpan)] = 0.5 * (1.0 + std::cos(kPi * t));
    }

    // QPSK31 needs tuning accuracy within ~4Hz per the spec (twice as
    // critical as BPSK31's ~7Hz single-hypothesis pull-in), so the
    // acquisition hypotheses are spaced correspondingly tighter -
    // covering a smaller total range than BPSK31's +/-17.5Hz, which
    // matches the spec's own description of QPSK being harder to tune,
    // not an oversight.
    static constexpr double kHypothesisOffsetsHz[] = {0.0, -4.0, 4.0, -8.0, 8.0};

    std::string best;
    double bestScore = -1.0;

    for (const double offsetHz : kHypothesisOffsetsHz) {
        const double carrierStep = 2.0 * kPi * (m_config.carrierHz + offsetHz) / m_config.sampleRate;

        auto phaseAt = [&](double epochPos, double phaseEpoch, double step, double p) {
            return wrapPhase(phaseEpoch + (p - epochPos) * step);
        };
        auto integrate = [&](double start, double epochPos, double phaseEpoch, double step) {
            double iSum = 0.0;
            double qSum = 0.0;
            for (int n = -halfSpan; n <= halfSpan; ++n) {
                const double p = start + n;
                const double ph = phaseAt(epochPos, phaseEpoch, step, p);
                const double s = readInterpolated(samples, p);
                const double w = pulse[static_cast<std::size_t>(n + halfSpan)];
                iSum += s * w * std::cos(ph);
                qSum += -s * w * std::sin(ph);
            }
            return std::complex<double>(iSum, qSum);
        };

        double epochPos = sps;
        double phaseEpoch = 0.0;
        double effectiveStep = carrierStep;
        double carrierFreqIntegral = 0.0;
        double timingFreqIntegral = 0.0;
        double symbolStart = sps;

        double previousPhase = 0.0;
        bool havePreviousPhase = false;
        std::complex<double> previousOnTime(0.0, 0.0);
        bool havePreviousOnTime = false;

        std::vector<int> phaseShifts;
        phaseShifts.reserve(samples.size() / std::max(1.0, sps));

        while (symbolStart + halfSpan + 1.0 <= static_cast<double>(samples.size())
               && symbolStart - halfSpan >= 0.0) {
            const std::complex<double> onTime = integrate(symbolStart, epochPos, phaseEpoch, effectiveStep);

            double gardnerError = 0.0;
            if (havePreviousOnTime) {
                const double midStart = symbolStart - sps / 2.0;
                const std::complex<double> midOnTime = integrate(midStart, epochPos, phaseEpoch, effectiveStep);
                const double energy = std::norm(onTime) + 1e-9;
                gardnerError = (std::conj(midOnTime) * (onTime - previousOnTime)).real() / energy;
            }
            timingFreqIntegral += m_config.gardnerKi * gardnerError;
            timingFreqIntegral = std::clamp(timingFreqIntegral, -0.35 * sps, 0.35 * sps);
            const double timingCorrection = -(m_config.gardnerKp * gardnerError + timingFreqIntegral);

            // Standard decision-directed QPSK Costas detector (4-quadrant
            // generalisation of BPSK's sign(I)*Q): drives both I and Q
            // toward the nearest axis, locking the constellation
            // orientation to a multiple of 90 degrees - which one exactly
            // is resolved by the differential phase-shift decode below,
            // same principle as BPSK's 180-degree ambiguity being
            // resolved differentially rather than by the Costas loop
            // itself.
            const double energy = std::norm(onTime) + 1e-9;
            const double iSign = onTime.real() >= 0.0 ? 1.0 : -1.0;
            const double qSign = onTime.imag() >= 0.0 ? 1.0 : -1.0;
            const double costasError = (iSign * onTime.imag() - qSign * onTime.real()) / energy;
            carrierFreqIntegral += m_config.costasKi * costasError;
            const double maxFreqIntegral = 2.0 * kPi * 15.0 / m_config.sampleRate;
            carrierFreqIntegral = std::clamp(carrierFreqIntegral, -maxFreqIntegral, maxFreqIntegral);
            const double phaseKick = m_config.costasKp * costasError;

            const double phase = std::atan2(onTime.imag(), onTime.real());
            if (!havePreviousPhase) {
                previousPhase = phase;
                havePreviousPhase = true;
            } else {
                // Differential phase change, quantised to the nearest
                // quarter-turn - the receiver's estimate of which of the
                // 4 phase-shift symbols (0/1/2/3) was actually sent.
                const double delta = wrapPhase(phase - previousPhase);
                int shift = static_cast<int>(std::lround(delta / (kPi / 2.0))) & 0x3;
                phaseShifts.push_back(shift);
                previousPhase = phase;
            }

            previousOnTime = onTime;
            havePreviousOnTime = true;

            const double nextStart = symbolStart + sps + timingCorrection;
            const double naturalPhaseAtNext = phaseAt(epochPos, phaseEpoch, effectiveStep, nextStart);
            epochPos = nextStart;
            phaseEpoch = wrapPhase(naturalPhaseAtNext + phaseKick);
            effectiveStep = carrierStep + carrierFreqIntegral;
            symbolStart = nextStart;
        }

        const std::vector<int> bits = QpskConvCode::decode(phaseShifts);

        // Same scoring principle as Bpsk31Codec::scoreDecodedBits(): the
        // leading bits should look like the idle preamble (continuous
        // zero bits - QPSK31 uses the identical Varicode framing and
        // idle convention as BPSK31, only the modulation/trellis differ),
        // and the payload past that should show real variation rather
        // than a stuck degenerate pattern.
        if (bits.empty()) {
            continue;
        }
        const std::size_t preambleCheckLen = std::min<std::size_t>(bits.size(), 40);
        int zeroCount = 0;
        for (std::size_t i = 0; i < preambleCheckLen; ++i) {
            if (bits[i] == 0) {
                ++zeroCount;
            }
        }
        double score = static_cast<double>(zeroCount) / static_cast<double>(preambleCheckLen);
        if (bits.size() > preambleCheckLen) {
            const std::size_t payloadStart = preambleCheckLen;
            const std::size_t payloadLen = bits.size() - payloadStart;
            int payloadOnes = 0;
            for (std::size_t i = payloadStart; i < bits.size(); ++i) {
                payloadOnes += bits[i];
            }
            const double onesFraction = static_cast<double>(payloadOnes) / static_cast<double>(payloadLen);
            const bool payloadLooksReal = onesFraction > 0.15 && onesFraction < 0.85;
            if (!payloadLooksReal) {
                score *= 0.1;
            }
        }

        if (score > bestScore) {
            bestScore = score;
            best = PskVaricode::decodeTextBits(bits);
        }
    }

    return best;
}

} // namespace psk::dsp
