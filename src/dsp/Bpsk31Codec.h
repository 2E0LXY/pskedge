#pragma once

#include <string>
#include <utility>
#include <vector>

namespace psk::dsp {

struct Bpsk31Config {
    double sampleRate = 8000.0;
    double symbolRate = 31.25;
    double carrierHz = 1000.0;
    double amplitude = 0.65;
    int preambleSymbols = 64;
    int postambleSymbols = 64;

    // RX Costas (carrier) and Gardner (timing) loop filter gains. See the
    // validated-envelope comment in Bpsk31Codec.cpp for what these were
    // tuned and tested against. Exposed here (rather than hardcoded) so
    // they can be swept/tuned without editing the codec, and so a future
    // properly-designed loop filter (zeta/omega_n mapping) can replace
    // these empirical defaults without an API change.
    double costasKp = 0.06;
    double costasKi = 0.004;
    double gardnerKp = 1.50;
    double gardnerKi = 0.01875;
};

struct Bpsk31SignalQuality {
    double signalLevelDb = 0.0;  // relative to full-scale audio input, not an absolute RF reference
    double noiseFloorDb = 0.0;   // measured out-of-band (carrierHz + 250Hz), not the true RF noise floor
    double snrDb = 0.0;
};

struct Bpsk31DemodResult {
    std::string text;
    // Which of the acquisition hypotheses (see demodulateBits()) actually
    // won - i.e. config.carrierHz + lockedOffsetHz is what the signal's
    // real carrier frequency was measured to be this call, not just what
    // it was assumed to be going in. Only meaningful when a hypothesis
    // clearly won (hasLock); with no signal present, whichever hypothesis
    // scores marginally highest on pure noise is not a real lock and
    // lockedOffsetHz should not be trusted.
    double lockedOffsetHz = 0.0;
    bool hasLock = false;
};

class Bpsk31Codec {
public:
    explicit Bpsk31Codec(Bpsk31Config config = {});

    std::vector<int> frameTextBits(const std::string &text) const;
    std::vector<double> modulateText(const std::string &text) const;
    std::vector<int> demodulateBits(const std::vector<double> &samples) const;
    std::string demodulateText(const std::vector<double> &samples) const;

    // Same acquisition/tracking as demodulateText(), but also reports
    // which hypothesis offset actually won - for AFC, so the caller can
    // nudge its target frequency toward where the signal actually is
    // rather than where it was assumed to be, instead of relying solely
    // on each call's independent +/-17.5Hz acquisition range to
    // compensate for an increasingly stale target.
    Bpsk31DemodResult demodulateTextWithLock(const std::vector<double> &samples) const;

    // Independent of the Costas/Gardner tracking state - a simple in-band
    // vs out-of-band correlator energy comparison, not tied to whether a
    // hypothesis actually locked. Reasonably accurate for a quick reading
    // (an offset within the acquisition range still falls mostly within
    // the fixed correlator's passband) but not a calibrated RF
    // measurement - see the field comments on Bpsk31SignalQuality.
    Bpsk31SignalQuality measureSignalQuality(const std::vector<double> &samples) const;

    int samplesPerSymbol() const;
    Bpsk31Config config() const;

private:
    Bpsk31Config m_config;
    std::vector<int> trackWithOffset(const std::vector<double> &samples, double offsetHz) const;
    double scoreDecodedBits(const std::vector<int> &bits) const;
    // Shared implementation for demodulateBits() and
    // demodulateTextWithLock() - runs the multi-hypothesis acquisition
    // loop once and returns both the winning bits and which offset
    // produced them, so the two public methods don't duplicate this
    // logic (and can't drift out of sync with each other).
    std::pair<std::vector<int>, double> demodulateBitsAndOffset(const std::vector<double> &samples) const;
};

} // namespace psk::dsp
