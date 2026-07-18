#include "Bpsk31Codec.h"

#include "PskVaricode.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace psk::dsp {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

double wrapPhase(double phase)
{
    // fmod-based reduction is O(1) regardless of how far out of range
    // `phase` is. The previous while-loop implementation was fine when
    // only ever called on small per-sample increments, but phaseAt() below
    // evaluates phase from a potentially large (p - epochPos) * step
    // product - if step is ever large (e.g. an unlocked, winded-up
    // frequency integrator, see the clamps in demodulateBits), a
    // while-loop takes time proportional to the phase magnitude, which on
    // pure noise input (the ordinary case of "nothing on this frequency")
    // turned into a multi-second stall per hypothesis. That combination
    // was found and fixed together, not this alone - the clamps below are
    // the actual fix for unbounded windup; this makes wrapPhase robust
    // regardless.
    double wrapped = std::fmod(phase + kPi, 2.0 * kPi);
    if (wrapped < 0.0) {
        wrapped += 2.0 * kPi;
    }
    return wrapped - kPi;
}

// Linear-interpolated read so correlator windows can start at a fractional
// sample position (required for symbol-timing correction). Out-of-range
// reads return 0 rather than throwing - the demodulator runs to the end of
// whatever buffer it's given, so edge padding is expected behaviour, not
// an error condition.
double readInterpolated(const std::vector<double> &samples, double pos)
{
    if (pos < 0.0 || pos >= static_cast<double>(samples.size()) - 1.0) {
        return 0.0;
    }
    const auto i0 = static_cast<std::size_t>(pos);
    const double frac = pos - static_cast<double>(i0);
    return samples[i0] * (1.0 - frac) + samples[i0 + 1] * frac;
}

// Empirically tuned and validated against the impairment fixtures in
// tools/psk_core_selftest.cpp, not derived from a target loop
// noise-bandwidth/damping-factor design - a properly designed loop filter
// (via the standard zeta/omega_n mapping) is a documented follow-up if a
// wider or narrower pull-in range is needed than what is tested here.
//
// Validated operating envelope (see selftest for the exact fixtures), with
// the multi-hypothesis acquisition in demodulateBits() below:
//   - carrier frequency offset: up to +/-10 Hz
//   - sample-clock drift (TX/RX clock mismatch): up to +/-0.1%
// A single Costas hypothesis alone is limited to about +/-7-8Hz: above
// that, per-symbol carrier rotation exceeds the +/-90 degree differential
// decision boundary (rotation = 360 * f / symbolRate degrees/symbol),
// which is a fundamental ambiguity limit for a symbol-rate Costas loop on
// differentially-encoded BPSK, not something these gains alone can be
// tuned around. Running several hypotheses at different starting offsets
// (see demodulateBits) and keeping whichever locks cleanly extends the
// combined range to about +/-12-13Hz measured, +/-10Hz claimed with
// margin - see the acquisition comment for why a proper non-ambiguous
// wideband search would be a more principled way to widen this further.

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

    // Differentially-encoded bipolar baseband symbol values (+1/-1), one
    // per bit, tracking the same phase-reversal-on-zero rule as before but
    // as a value to be pulse-shaped rather than an instantaneous phase.
    std::vector<double> symbolValues;
    symbolValues.reserve(bits.size());
    double dataPhase = 0.0;
    for (int bit : bits) {
        if (bit == 0) {
            dataPhase = wrapPhase(dataPhase + kPi);
        }
        symbolValues.push_back(std::cos(dataPhase)); // +1 or -1
    }

    // Raised-cosine (100% roll-off) pulse shaping filter spanning one
    // symbol period either side of center. This is the standard technique
    // real PSK31 modems use: an instantaneous rectangular phase flip has a
    // sinc-shaped spectrum with slow sidelobe rolloff (splatter well
    // outside the nominal ~60Hz PSK31 bandwidth); convolving the bipolar
    // symbol sequence with this pulse before upconversion produces a
    // continuous, smoothly-varying envelope with a far narrower spectrum.
    // See ARRL PSK31 spec and G3PLX's reference implementation notes.
    const int halfSpan = sps;
    std::vector<double> pulse(2 * halfSpan + 1);
    for (int k = -halfSpan; k <= halfSpan; ++k) {
        const double t = static_cast<double>(k) / static_cast<double>(sps);
        pulse[k + halfSpan] = 0.5 * (1.0 + std::cos(kPi * t));
    }

    const int totalSamples = static_cast<int>(symbolValues.size()) * sps + 2 * halfSpan;
    std::vector<double> shapedBaseband(static_cast<std::size_t>(totalSamples), 0.0);
    for (std::size_t sym = 0; sym < symbolValues.size(); ++sym) {
        const int center = static_cast<int>(sym) * sps + halfSpan;
        for (int k = -halfSpan; k <= halfSpan; ++k) {
            const int idx = center + k;
            if (idx >= 0 && idx < totalSamples) {
                shapedBaseband[static_cast<std::size_t>(idx)] += symbolValues[sym] * pulse[k + halfSpan];
            }
        }
    }

    std::vector<double> samples;
    samples.reserve(shapedBaseband.size());
    double carrierPhase = 0.0;
    const double carrierStep = 2.0 * kPi * m_config.carrierHz / m_config.sampleRate;
    for (double b : shapedBaseband) {
        samples.push_back(m_config.amplitude * b * std::cos(carrierPhase));
        carrierPhase = wrapPhase(carrierPhase + carrierStep);
    }

    return samples;
}

std::vector<int> Bpsk31Codec::trackWithOffset(const std::vector<double> &samples, double offsetHz) const
{
    const double sps = static_cast<double>(samplesPerSymbol());
    const double carrierStepNominal = 2.0 * kPi * (m_config.carrierHz + offsetHz) / m_config.sampleRate;

    std::vector<int> bits;
    bits.reserve(static_cast<std::size_t>(samples.size() / std::max(1.0, sps)));

    // Matched filter for the raised-cosine (100% roll-off) pulse shape
    // used by modulateText(). `start` denotes the position of a symbol's
    // pulse PEAK (matching where modulateText() places it), and the
    // correlator integrates a window of +/-halfSpan around that peak,
    // weighted by the same pulse shape - this is the receiver-side match
    // for the transmitter's shaping filter, which is what "matched filter"
    // means: correlating against the exact waveform you expect to receive
    // maximises SNR (matched filter theorem), rather than a filter shaped
    // for a signal we no longer transmit.
    const int halfSpan = static_cast<int>(sps);
    std::vector<double> pulse(static_cast<std::size_t>(2 * halfSpan + 1));
    for (int k = -halfSpan; k <= halfSpan; ++k) {
        const double t = static_cast<double>(k) / sps;
        pulse[static_cast<std::size_t>(k + halfSpan)] = 0.5 * (1.0 + std::cos(kPi * t));
    }

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
    double effectiveStep = carrierStepNominal;
    double carrierFreqIntegral = 0.0;
    double timingFreqIntegral = 0.0;
    double symbolStart = sps; // first symbol's pulse peak, matching modulateText()

    double previousPhase = 0.0;
    bool havePreviousPhase = false;
    std::complex<double> previousOnTime(0.0, 0.0);
    bool havePreviousOnTime = false;

    // Stop when the on-time window (+/-halfSpan around the peak, plus one
    // interpolation sample) would run off either end of the buffer.
    while (symbolStart + halfSpan + 1.0 <= static_cast<double>(samples.size())
           && symbolStart - halfSpan >= 0.0) {
        const std::complex<double> onTime = integrate(symbolStart, epochPos, phaseEpoch, effectiveStep);

        double gardnerError = 0.0;
        if (havePreviousOnTime) {
            const double midStart = symbolStart - sps / 2.0;
            const std::complex<double> midOnTime = integrate(midStart, epochPos, phaseEpoch, effectiveStep);
            // e = Re{ conj(mid) * (onTime - previous) } - the complex
            // generalisation of Gardner's BPSK/QPSK timing-error detector
            // (Gardner, IEEE Trans. Commun., May 1986), normalised by the
            // current symbol's energy so the loop gain doesn't depend on
            // signal amplitude.
            const double energy = std::norm(onTime) + 1e-9;
            gardnerError = (std::conj(midOnTime) * (onTime - previousOnTime)).real() / energy;
        }
        timingFreqIntegral += m_config.gardnerKi * gardnerError;
        // Clamp: on pure noise (no lock ever achieved - the ordinary case
        // of nothing being on this frequency), this integral has no
        // steady-state error to converge to and otherwise winds up
        // without bound over a long buffer. Symptom before this fix:
        // measured 2.9s to process one 3-second no-signal hypothesis
        // (should be low milliseconds) - the runaway timing/frequency
        // state made phaseAt() evaluate wrapPhase() on enormous values.
        // +/-0.35*sps is generous headroom above any correction the
        // validated +/-0.1% clock-drift envelope actually needs.
        timingFreqIntegral = std::clamp(timingFreqIntegral, -0.35 * sps, 0.35 * sps);
        const double timingCorrection = -(m_config.gardnerKp * gardnerError + timingFreqIntegral);

        // Costas decision-directed phase detector for BPSK: e = sign(I)*Q.
        // Drives Q toward zero (all energy on the in-phase axis) regardless
        // of what the recovered bit values mean - it locks the constellation
        // orientation, not the data.
        const double energy = std::norm(onTime) + 1e-9;
        const double costasError = (onTime.real() >= 0.0 ? 1.0 : -1.0) * onTime.imag() / energy;
        carrierFreqIntegral += m_config.costasKi * costasError;
        // Clamp to +/-30Hz equivalent: comfortably beyond the +/-10-14Hz
        // acquisition range this codec actually locks within, but bounded
        // rather than unbounded - same runaway-prevention reasoning as the
        // timing clamp above.
        const double maxFreqIntegral = 2.0 * kPi * 30.0 / m_config.sampleRate;
        carrierFreqIntegral = std::clamp(carrierFreqIntegral, -maxFreqIntegral, maxFreqIntegral);
        const double phaseKick = m_config.costasKp * costasError;

        const double phase = std::atan2(onTime.imag(), onTime.real());
        if (!havePreviousPhase) {
            previousPhase = phase;
            havePreviousPhase = true;
        } else {
            const double delta = std::abs(wrapPhase(phase - previousPhase));
            bits.push_back(delta > (kPi / 2.0) ? 0 : 1);
            previousPhase = phase;
        }

        previousOnTime = onTime;
        havePreviousOnTime = true;

        // Advance both loops' state to the next symbol. Frequency
        // corrections (the integral paths) persist and compound; phase
        // and timing corrections (proportional paths) are applied once as
        // an offset to the next symbol's reference point.
        const double nextStart = symbolStart + sps + timingCorrection;
        const double naturalPhaseAtNext = phaseAt(epochPos, phaseEpoch, effectiveStep, nextStart);
        epochPos = nextStart;
        phaseEpoch = wrapPhase(naturalPhaseAtNext + phaseKick);
        effectiveStep = carrierStepNominal + carrierFreqIntegral;
        symbolStart = nextStart;
    }

    return bits;
}

double Bpsk31Codec::scoreDecodedBits(const std::vector<int> &bits) const
{
    // Scores a candidate hypothesis by how well its leading bits match the
    // PSK31 idle preamble (continuous phase reversals = continuous zero
    // bits, per the ARRL/G3PLX spec - this codec transmits
    // preambleSymbols of exactly this pattern before every message, and
    // real on-air PSK31 stations begin transmissions the same way).
    //
    // An earlier version scored preamble-zero-fraction alone. That looked
    // right but had a failure mode caught only by checking actual decoded
    // output during validation: a marginally-mistuned hypothesis sitting
    // right at the Costas ambiguity boundary can get "stuck" producing a
    // long run of zero bits that isn't a real lock, just a beat-frequency
    // coincidence between the residual carrier error and the symbol rate -
    // it never actually demodulates the payload afterward. Requiring the
    // section past where the preamble should end to show real bit
    // variation (not also stuck at all-zero or all-one) filters that out:
    // a genuine lock transitions from preamble to varied payload data: a
    // stuck one doesn't.
    if (bits.empty()) {
        return 0.0;
    }
    const std::size_t preambleCheckLen = std::min<std::size_t>(bits.size(), 40);
    int zeroCount = 0;
    for (std::size_t i = 0; i < preambleCheckLen; ++i) {
        if (bits[i] == 0) {
            ++zeroCount;
        }
    }
    const double preambleScore = static_cast<double>(zeroCount) / static_cast<double>(preambleCheckLen);

    const std::size_t payloadStart = static_cast<std::size_t>(std::max(0, m_config.preambleSymbols - 4));
    const std::size_t payloadEnd = std::min(bits.size(), payloadStart + 40);
    if (payloadEnd <= payloadStart + 10) {
        // Not enough buffer past the expected preamble to check for a
        // real transition; fall back to the preamble score alone.
        return preambleScore;
    }
    int payloadOnes = 0;
    for (std::size_t i = payloadStart; i < payloadEnd; ++i) {
        if (bits[i] == 1) {
            ++payloadOnes;
        }
    }
    const double payloadOneFraction = static_cast<double>(payloadOnes) / static_cast<double>(payloadEnd - payloadStart);
    // Real Varicode-encoded text is not close to all-zero or all-one in
    // any 40-bit window; a stuck/degenerate lock is.
    const bool payloadLooksReal = payloadOneFraction > 0.1 && payloadOneFraction < 0.9;
    return payloadLooksReal ? preambleScore : preambleScore * 0.1;
}

std::vector<int> Bpsk31Codec::demodulateBits(const std::vector<double> &samples) const
{
    return demodulateBitsAndOffset(samples).first;
}

std::pair<std::vector<int>, double> Bpsk31Codec::demodulateBitsAndOffset(const std::vector<double> &samples) const
{
    // Wideband acquisition: a single Costas hypothesis has a hard pull-in
    // ceiling (~+/-7Hz here - see the envelope comment above) because
    // per-symbol carrier rotation beyond ~90 degrees is ambiguous to a
    // symbol-rate decision-directed phase detector. Rather than widen that
    // ceiling by chasing an inherently ambiguity-limited detector (it
    // can't be tuned past the ambiguity, only a differently-structured
    // frequency-only discriminator escapes it - see the TODO below), this
    // runs several parallel hypotheses at different starting offsets, each
    // within its own +/-7Hz pull-in range, and keeps whichever one
    // produces the cleanest decode. Five hypotheses spaced 7Hz apart cover
    // a combined ~+/-17.5Hz acquisition range - see
    // tools/psk_core_selftest.cpp for the validated coverage.
    //
    // TODO: a proper non-ambiguous wideband search (e.g. FFT peak search
    // on the squared/frequency-doubled signal, which removes the +/-180
    // degree BPSK modulation and reveals carrier frequency unambiguously
    // over a much wider range) would be more principled and cheaper than
    // running N full tracking passes. Not implemented here - flagged as a
    // follow-up, not attempted blind.
    static constexpr double kHypothesisOffsetsHz[] = {0.0, -7.0, 7.0, -14.0, 14.0};

    std::vector<int> best;
    double bestScore = -1.0;
    double bestOffsetHz = 0.0;
    for (const double offsetHz : kHypothesisOffsetsHz) {
        std::vector<int> candidate = trackWithOffset(samples, offsetHz);
        const double score = scoreDecodedBits(candidate);
        if (score > bestScore) {
            bestScore = score;
            best = std::move(candidate);
            bestOffsetHz = offsetHz;
        }
    }
    return {std::move(best), bestOffsetHz};
}

std::string Bpsk31Codec::demodulateText(const std::vector<double> &samples) const
{
    return PskVaricode::decodeTextBits(demodulateBits(samples));
}

Bpsk31DemodResult Bpsk31Codec::demodulateTextWithLock(const std::vector<double> &samples) const
{
    const auto [bits, offsetHz] = demodulateBitsAndOffset(samples);
    Bpsk31DemodResult result;
    result.text = PskVaricode::decodeTextBits(bits);
    result.lockedOffsetHz = offsetHz;
    // scoreDecodedBits() returns close to 1.0 for a genuine lock
    // (preamble nearly all zero bits AND payload shows real variation,
    // not a degenerate stuck pattern - see that function's comment) and
    // is penalised by 10x if the payload looks fake, which reliably pulls
    // even a high preamble-only score well under this threshold. 0.8 is
    // a conservative cut some way below a clean lock's typical score and
    // well above what a penalised fake lock can reach, not a value swept
    // against a validation set - flagged as a reasoned choice, not a
    // calibrated one.
    result.hasLock = scoreDecodedBits(bits) > 0.8;
    return result;
}

Bpsk31SignalQuality Bpsk31Codec::measureSignalQuality(const std::vector<double> &samples) const
{
    const int sps = samplesPerSymbol();
    auto blockRms = [&](double freqHz) {
        if (samples.size() < static_cast<std::size_t>(sps)) {
            return 0.0;
        }
        const double step = 2.0 * kPi * freqHz / m_config.sampleRate;
        double phase = 0.0;
        double sumSquares = 0.0;
        int blocks = 0;
        for (std::size_t start = 0; start + static_cast<std::size_t>(sps) <= samples.size();
             start += static_cast<std::size_t>(sps)) {
            double iSum = 0.0;
            double qSum = 0.0;
            for (int n = 0; n < sps; ++n) {
                const double s = samples[start + static_cast<std::size_t>(n)];
                iSum += s * std::cos(phase);
                qSum += -s * std::sin(phase);
                phase = wrapPhase(phase + step);
            }
            sumSquares += iSum * iSum + qSum * qSum;
            ++blocks;
        }
        if (blocks == 0) {
            return 0.0;
        }
        return std::sqrt(sumSquares / blocks) / sps;
    };

    // Out-of-band reference: 250Hz away from the tracked carrier is well
    // outside the ~55-80Hz occupied bandwidth of a raised-cosine-shaped
    // PSK31 signal (measured in tools/bandwidth checks during
    // development), while still comfortably within a typical SSB audio
    // passband, so it reads local noise/adjacent-signal energy rather than
    // the wanted signal's own sidelobes.
    const double signalRms = blockRms(m_config.carrierHz);
    const double noiseRms = blockRms(m_config.carrierHz + 250.0);

    Bpsk31SignalQuality quality;
    quality.signalLevelDb = 20.0 * std::log10(std::max(signalRms, 1e-9));
    quality.noiseFloorDb = 20.0 * std::log10(std::max(noiseRms, 1e-9));
    quality.snrDb = quality.signalLevelDb - quality.noiseFloorDb;
    return quality;
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
