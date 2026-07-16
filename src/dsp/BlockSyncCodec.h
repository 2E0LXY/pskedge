#pragma once

#include <complex>
#include <optional>
#include <vector>

namespace psk::dsp {

struct BlockSyncConfig {
    double sampleRate = 8000.0;
    double symbolRate = 15.625; // half PSK31's 31.25 baud - reclaims bandwidth spent on rate-1/2 FEC
    double carrierHz = 1000.0;
    double amplitude = 0.65;
    int syncOrder = 7; // 127-symbol m-sequence preamble

    // Time search range for acquisition, in seconds either side of the
    // buffer's nominal expected block start.
    double timeSearchRangeSec = 1.0;
    // Time search step, in samples. Finer = more hypotheses tried = slower
    // but better timing resolution.
    int timeSearchStepSamples = 48;
};

struct AcquisitionResult {
    bool found = false;
    double freqOffsetHz = 0.0;
    double timeOffsetSamples = 0.0;
    double phaseOffsetRadians = 0.0;
    double peakMagnitude = 0.0;
    // Ratio of the best peak to the median peak across all searched
    // hypotheses - a simple, checkable "is this a real detection or noise"
    // confidence proxy, analogous to how correlation-based acquisition
    // schemes (e.g. GPS) distinguish a true lock from a noise peak.
    double peakToMedianRatio = 0.0;
};

struct BlockDecodeResult {
    bool acquired = false;
    bool crcValid = false;
    std::vector<int> payloadBits; // only meaningful if crcValid
    AcquisitionResult acquisition;
};

// Block-based, sync-word-acquired, coherently-demodulated BPSK codec with
// CRC+convolutional FEC - the receiver architecture real weak-signal modes
// (FT8/JT65/WSPR) use instead of continuous per-symbol PLL tracking, which
// becomes unreliable well before the SNR range this targets. See
// ConvCode.h and Crc16.h for the FEC/integrity pieces this wraps.
//
// Known limitation, stated plainly: the coherent phase/frequency reference
// is established ONCE from the sync preamble and held constant (fixed
// frequency, no drift tracking) through the whole payload. Real weak-
// signal modes typically refine sync using tones/patterns spread through
// the entire block, not just an up-front preamble. This is a simpler
// first version - see the Monte Carlo validation for how much clock drift
// degrades it in practice.
class BlockSyncCodec {
public:
    explicit BlockSyncCodec(BlockSyncConfig config = {});

    // payloadBits is the pre-FEC, pre-CRC message. A 16-bit CRC is appended
    // before convolutional encoding.
    std::vector<double> modulateBlock(const std::vector<int> &payloadBits) const;

    // Joint time/frequency/phase acquisition via despread-then-FFT (the
    // sync sequence is known, so correlating it out leaves a residual
    // complex tone at the true frequency/phase offset; FFT finds that
    // tone's frequency and the search position with the strongest peak
    // simultaneously).
    AcquisitionResult acquire(const std::vector<double> &samples) const;

    // Runs acquire() then coherently demodulates and FEC/CRC-decodes the
    // payload using the identified time/frequency/phase reference.
    // messageBitCount is the payload size (pre-CRC, pre-FEC) used to
    // encode - must match what modulateBlock() was called with.
    BlockDecodeResult demodulateBlock(int messageBitCount, const std::vector<double> &samples) const;

    int samplesPerSymbol() const;
    int syncLength() const;

private:
    BlockSyncConfig m_config;
    std::vector<double> m_syncBaseband; // pulse-shaped, +/-amplitude, one symbol's worth of shape per sync bit
};

} // namespace psk::dsp
