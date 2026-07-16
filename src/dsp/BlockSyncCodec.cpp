#include "BlockSyncCodec.h"

#include "ConvCode.h"
#include "Crc16.h"
#include "SimpleFFT.h"
#include "SyncSequence.h"

#include <algorithm>
#include <cmath>

namespace psk::dsp {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

// Raised-cosine (100% roll-off) pulse shaping, matching the technique
// validated for BPSK31 (see Bpsk31Codec.cpp) - shapes a bipolar symbol
// sequence into a continuous-envelope baseband signal instead of
// instantaneous rectangular transitions. Duplicated here rather than
// shared with Bpsk31Codec to keep the two codecs independent (different
// symbol rate, different framing) - a shared PulseShaper utility is a
// reasonable follow-up refactor, not done here to avoid coupling two
// still-evolving codecs together.
std::vector<double> shapeBipolarSymbols(const std::vector<double> &symbolValues, int sps)
{
    const int halfSpan = sps;
    std::vector<double> pulse(static_cast<std::size_t>(2 * halfSpan + 1));
    for (int k = -halfSpan; k <= halfSpan; ++k) {
        const double t = static_cast<double>(k) / static_cast<double>(sps);
        pulse[static_cast<std::size_t>(k + halfSpan)] = 0.5 * (1.0 + std::cos(kPi * t));
    }

    const int totalSamples = static_cast<int>(symbolValues.size()) * sps + 2 * halfSpan;
    std::vector<double> shaped(static_cast<std::size_t>(totalSamples), 0.0);
    for (std::size_t sym = 0; sym < symbolValues.size(); ++sym) {
        const int center = static_cast<int>(sym) * sps + halfSpan;
        for (int k = -halfSpan; k <= halfSpan; ++k) {
            const int idx = center + k;
            if (idx >= 0 && idx < totalSamples) {
                shaped[static_cast<std::size_t>(idx)] += symbolValues[sym] * pulse[static_cast<std::size_t>(k + halfSpan)];
            }
        }
    }
    return shaped;
}

} // namespace

BlockSyncCodec::BlockSyncCodec(BlockSyncConfig config)
    : m_config(config)
{
    const std::vector<int> syncBits = SyncSequence::generate(m_config.syncOrder);
    std::vector<double> syncSymbols(syncBits.size());
    for (std::size_t i = 0; i < syncBits.size(); ++i) {
        syncSymbols[i] = syncBits[i] ? 1.0 : -1.0;
    }
    m_syncBaseband = shapeBipolarSymbols(syncSymbols, samplesPerSymbol());
}

int BlockSyncCodec::samplesPerSymbol() const
{
    return static_cast<int>(std::lround(m_config.sampleRate / m_config.symbolRate));
}

int BlockSyncCodec::syncLength() const
{
    return (1 << m_config.syncOrder) - 1;
}

std::vector<double> BlockSyncCodec::modulateBlock(const std::vector<int> &payloadBits) const
{
    // Crc16 operates on bytes; payloadBits is a bit vector. Pack bits into
    // bytes (MSB-first) for the CRC computation, matching how the payload
    // will eventually be interpreted as bytes at the application layer.
    std::vector<unsigned char> packed((payloadBits.size() + 7) / 8, 0);
    for (std::size_t i = 0; i < payloadBits.size(); ++i) {
        if (payloadBits[i]) {
            packed[i / 8] |= static_cast<unsigned char>(0x80u >> (i % 8));
        }
    }
    const std::uint16_t realCrc = Crc16::compute(packed.data(), packed.size());

    std::vector<int> withCrc = payloadBits;
    for (int i = 15; i >= 0; --i) {
        withCrc.push_back((realCrc >> i) & 1);
    }

    const std::vector<int> encoded = ConvCode::encode(withCrc);

    const int sps = samplesPerSymbol();
    const std::vector<int> syncBits = SyncSequence::generate(m_config.syncOrder);

    std::vector<double> allSymbols;
    allSymbols.reserve(syncBits.size() + encoded.size());
    for (int b : syncBits) {
        allSymbols.push_back(b ? 1.0 : -1.0);
    }
    for (int b : encoded) {
        allSymbols.push_back(b ? 1.0 : -1.0);
    }

    const std::vector<double> shapedBaseband = shapeBipolarSymbols(allSymbols, sps);

    std::vector<double> samples;
    samples.reserve(shapedBaseband.size());
    double carrierPhase = 0.0;
    const double carrierStep = 2.0 * kPi * m_config.carrierHz / m_config.sampleRate;
    for (double b : shapedBaseband) {
        samples.push_back(m_config.amplitude * b * std::cos(carrierPhase));
        carrierPhase += carrierStep;
        if (carrierPhase > 2.0 * kPi) {
            carrierPhase -= 2.0 * kPi;
        }
    }
    return samples;
}

AcquisitionResult BlockSyncCodec::acquire(const std::vector<double> &samples) const
{
    const int sps = samplesPerSymbol();
    const int syncSpanSamples = sps * syncLength();

    // Mix the real audio down to complex baseband at the nominal
    // (uncorrected) carrier frequency.
    std::vector<std::complex<double>> iq(samples.size());
    double phase = 0.0;
    const double step = 2.0 * kPi * m_config.carrierHz / m_config.sampleRate;
    for (std::size_t n = 0; n < samples.size(); ++n) {
        iq[n] = samples[n] * std::complex<double>(std::cos(phase), -std::sin(phase));
        phase += step;
        if (phase > 2.0 * kPi) {
            phase -= 2.0 * kPi;
        }
    }

    // Real-to-complex mixing produces both the wanted near-DC baseband
    // term AND an unwanted image at -2*carrierHz (multiplying a real
    // cosine by a complex exponential always does this - it's not
    // specific to this signal). Bpsk31Codec's per-symbol integrate-and-
    // dump correlator implicitly averages this out over many cycles per
    // symbol; this FFT-based path doesn't do that implicitly, so it needs
    // an explicit low-pass filter. A boxcar of length
    // round(sampleRate / (2*carrierHz)) has a null exactly at 2*carrierHz
    // by construction, cancelling the image while leaving the narrow band
    // of interest near DC essentially flat.
    {
        const int boxcarLen = std::max(1, static_cast<int>(std::lround(m_config.sampleRate / (2.0 * m_config.carrierHz))));
        std::vector<std::complex<double>> filtered(iq.size(), std::complex<double>(0.0, 0.0));
        std::complex<double> sum(0.0, 0.0);
        for (std::size_t n = 0; n < iq.size(); ++n) {
            sum += iq[n];
            if (n >= static_cast<std::size_t>(boxcarLen)) {
                sum -= iq[n - static_cast<std::size_t>(boxcarLen)];
            }
            filtered[n] = sum / static_cast<double>(boxcarLen);
        }
        iq.swap(filtered);
    }

    const std::size_t fftSize = SimpleFFT::nextPowerOfTwo(static_cast<std::size_t>(syncSpanSamples));
    const int searchRangeSamples = static_cast<int>(m_config.timeSearchRangeSec * m_config.sampleRate);
    const int step_ = std::max(1, m_config.timeSearchStepSamples);

    AcquisitionResult best;
    std::vector<double> allPeaks;

    for (int t = 0; t + syncSpanSamples <= static_cast<int>(iq.size()) && t <= searchRangeSamples; t += step_) {
        if (t < 0) {
            continue;
        }
        std::vector<std::complex<double>> despread(fftSize, std::complex<double>(0.0, 0.0));
        for (int n = 0; n < syncSpanSamples; ++n) {
            // The known sync baseband is real-valued (+/-amplitude-ish),
            // so "despreading" (correlating out the known pattern) is just
            // multiplying by it directly - conjugate of a real number is
            // itself.
            despread[static_cast<std::size_t>(n)] =
                iq[static_cast<std::size_t>(t + n)] * m_syncBaseband[static_cast<std::size_t>(n)];
        }
        SimpleFFT::forward(despread);

        std::size_t peakBin = 0;
        double peakMag = 0.0;
        for (std::size_t k = 0; k < fftSize; ++k) {
            const double mag = std::abs(despread[k]);
            if (mag > peakMag) {
                peakMag = mag;
                peakBin = k;
            }
        }
        allPeaks.push_back(peakMag);

        if (peakMag > best.peakMagnitude) {
            best.peakMagnitude = peakMag;
            best.timeOffsetSamples = t;

            // Parabolic interpolation around the peak bin for sub-bin
            // frequency resolution. The raw bin spacing here (~0.12Hz for
            // this FFT size) is far too coarse on its own: even a 0.04Hz
            // residual error accumulates ~3.8 radians of phase drift over
            // a ~15s payload with no per-symbol tracking (see the
            // class-level comment on that limitation), which is enough to
            // break coherent demodulation outright. This interpolation
            // was added after discovering exactly that failure empirically
            // - not a precaution taken in advance.
            const std::size_t prevBin = (peakBin + fftSize - 1) % fftSize;
            const std::size_t nextBin = (peakBin + 1) % fftSize;
            const double yPrev = std::abs(despread[prevBin]);
            const double yPeak = std::abs(despread[peakBin]);
            const double yNext = std::abs(despread[nextBin]);
            const double denom = (yPrev - 2.0 * yPeak + yNext);
            const double binOffset = (std::abs(denom) > 1e-12)
                ? 0.5 * (yPrev - yNext) / denom
                : 0.0;
            const double interpolatedBin = static_cast<double>(peakBin) + binOffset;

            double binFreq = interpolatedBin * m_config.sampleRate / static_cast<double>(fftSize);
            if (interpolatedBin > static_cast<double>(fftSize) / 2.0) {
                binFreq -= m_config.sampleRate;
            }
            best.freqOffsetHz = binFreq;
            best.phaseOffsetRadians = std::arg(despread[peakBin]);
            best.found = true;
        }
    }

    if (!allPeaks.empty()) {
        std::vector<double> sorted = allPeaks;
        std::sort(sorted.begin(), sorted.end());
        const double median = sorted[sorted.size() / 2];
        best.peakToMedianRatio = median > 0.0 ? best.peakMagnitude / median : 0.0;
    }

    return best;
}

BlockDecodeResult BlockSyncCodec::demodulateBlock(int messageBitCount, const std::vector<double> &samples) const
{
    BlockDecodeResult result;
    result.acquisition = acquire(samples);
    if (!result.acquisition.found) {
        return result;
    }
    result.acquired = true;

    const int sps = samplesPerSymbol();
    const int halfSpan = sps;
    const int syncSpanSamples = sps * syncLength();
    const double payloadStartSample = result.acquisition.timeOffsetSamples + syncSpanSamples + halfSpan;
    const double effectiveCarrierHz = m_config.carrierHz + result.acquisition.freqOffsetHz;
    const double step = 2.0 * kPi * effectiveCarrierHz / m_config.sampleRate;

    // Pulse-matched correlator, same shape as TX, evaluated with the
    // coherent phase/frequency reference from acquisition and held
    // constant (no per-symbol tracking) - see the class-level comment on
    // this limitation.
    std::vector<double> pulse(static_cast<std::size_t>(2 * halfSpan + 1));
    for (int k = -halfSpan; k <= halfSpan; ++k) {
        const double t = static_cast<double>(k) / static_cast<double>(sps);
        pulse[static_cast<std::size_t>(k + halfSpan)] = 0.5 * (1.0 + std::cos(kPi * t));
    }

    const int encodedLength = (messageBitCount + 16 /* CRC */ + ConvCode::kConstraintLength - 1) * 2;
    std::vector<double> soft;
    soft.reserve(static_cast<std::size_t>(encodedLength));

    // Absolute phase reference: at payloadStartSample, the signal's phase
    // is phaseOffsetRadians (from the sync FFT peak) plus whatever the
    // carrier has advanced by since the sync segment's start (the FFT
    // phase is referenced to the START of the sync span, i.e.
    // timeOffsetSamples).
    for (int symIdx = 0; symIdx < encodedLength; ++symIdx) {
        const double center = payloadStartSample + symIdx * sps;
        double iSum = 0.0;
        for (int n = -halfSpan; n <= halfSpan; ++n) {
            const double p = center + n;
            const auto idx = static_cast<std::size_t>(p);
            if (p < 0.0 || idx + 1 >= samples.size()) {
                continue;
            }
            const double frac = p - static_cast<double>(idx);
            const double s = samples[idx] * (1.0 - frac) + samples[idx + 1] * frac;
            const double ph = result.acquisition.phaseOffsetRadians
                + (p - result.acquisition.timeOffsetSamples) * step;
            const double w = pulse[static_cast<std::size_t>(n + halfSpan)];
            iSum += s * w * std::cos(ph);
        }
        soft.push_back(iSum);
    }

    const std::vector<int> decoded = ConvCode::decodeSoft(soft, messageBitCount + 16);
    if (static_cast<int>(decoded.size()) < messageBitCount + 16) {
        return result;
    }

    std::vector<int> payload(decoded.begin(), decoded.begin() + messageBitCount);
    std::vector<int> crcBits(decoded.begin() + messageBitCount, decoded.begin() + messageBitCount + 16);
    std::uint16_t receivedCrc = 0;
    for (int b : crcBits) {
        receivedCrc = static_cast<std::uint16_t>((receivedCrc << 1) | b);
    }

    std::vector<unsigned char> packed((static_cast<std::size_t>(messageBitCount) + 7) / 8, 0);
    for (int i = 0; i < messageBitCount; ++i) {
        if (payload[static_cast<std::size_t>(i)]) {
            packed[static_cast<std::size_t>(i) / 8] |= static_cast<unsigned char>(0x80u >> (i % 8));
        }
    }
    const std::uint16_t computedCrc = Crc16::compute(packed.data(), packed.size());

    result.crcValid = (computedCrc == receivedCrc);
    if (result.crcValid) {
        result.payloadBits = payload;
    }
    return result;
}

} // namespace psk::dsp
