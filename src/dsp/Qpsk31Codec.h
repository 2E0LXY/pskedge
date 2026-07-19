#pragma once

#include "QpskConvCode.h"

#include <string>
#include <vector>

namespace psk::dsp {

struct Qpsk31Config {
    double sampleRate = 8000.0;
    double symbolRate = 31.25; // same baud rate as BPSK31 - see the G3PLX spec
    double carrierHz = 1000.0;
    double amplitude = 0.55;
    int preambleSymbols = 64;

    // Same tuned defaults as Bpsk31Config's Costas/Gardner gains,
    // reused as a starting point since it's the same symbol rate and
    // pulse shaping - not independently re-tuned for QPSK's different
    // phase-detector shape yet.
    double costasKp = 0.06;
    double costasKi = 0.004;
    double gardnerKp = 1.50;
    double gardnerKi = 0.01875;
};

// QPSK31: the G3PLX-specified quadrature variant of PSK31, adding the
// 32-state trellis code (QpskConvCode) for real error correction at the
// cost of needing tuning accuracy within ~4Hz (twice as critical as
// BPSK31) and a small transmission delay from the trellis decoder - both
// documented tradeoffs in the spec itself, not something this
// implementation adds.
//
// A separate class from Bpsk31Codec, not a mode flag on it: the
// modulation (4 phases, not 2), the bit-to-symbol mapping (via the
// trellis code, not a direct 1:1 differential mapping), and the
// demodulator's phase detector are all genuinely different, and keeping
// them separate means nothing here can put the already-extensively-
// validated BPSK31 path at risk.
class Qpsk31Codec {
public:
    explicit Qpsk31Codec(Qpsk31Config config = {});

    std::vector<double> modulateText(const std::string &text) const;
    std::string demodulateText(const std::vector<double> &samples) const;

    Qpsk31Config config() const;

private:
    Qpsk31Config m_config;
};

} // namespace psk::dsp
