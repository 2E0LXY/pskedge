#pragma once

#include <vector>

namespace psk::dsp {

// The specific 32-state trellis code G3PLX (Peter Martinez) designed for
// QPSK31 - see the ARRL/QEX article "PSK31: A New Radio-Teletype Mode"
// (July/Aug 1999) and the original web-page article, both fetched and
// used to build/verify this table directly, not from memory. This is a
// different code from ConvCode.h's rate-1/2 K=7 code (built for an
// unrelated experimental mode) - QPSK31 needs this specific one for real
// interop with other QPSK31 stations, since the trellis and its
// resulting phase-shift sequence are part of the on-air signal, not an
// implementation detail either side can choose independently.
//
// Table verified against the spec's own worked example (the Varicode
// "space" symbol encoding to phase-shift sequence 2,1,3,3,0,1,2) before
// being used for anything else.
class QpskConvCode {
public:
    // 32 states, one per possible 5-bit history window. Encodes an input
    // bit stream (the same Varicode bit stream Bpsk31Codec's
    // frameTextBits() produces) into one phase-shift value (0-3) per
    // input bit: 0 = no shift, 1 = advance 90 deg, 2 = reversal (180
    // deg), 3 = retard 90 deg - matching the spec's own convention
    // exactly (not an arbitrary internal choice), so a real QPSK31
    // receiver decoding this signal sees the phase shifts the spec
    // defines.
    static std::vector<int> encode(const std::vector<int> &bits);

    // Hard-decision Viterbi decode: takes a sequence of received
    // phase-shift observations (0-3, already demodulated) and returns
    // the most likely original bit sequence. Minimises the count of
    // trellis transitions whose predicted phase-shift disagrees with
    // what was actually observed - a real error-correcting decode (the
    // 32-state trellis carries redundancy even though each input bit
    // maps to exactly one output symbol, because only certain state
    // sequences are reachable), not just an inverse lookup. Processes
    // the whole observation sequence and traces back from the best
    // final state, matching how the rest of this codebase's demodulators
    // (e.g. Bpsk31Codec::demodulateBits()) work on a complete buffer
    // rather than a fixed-depth sliding decision window.
    static std::vector<int> decode(const std::vector<int> &phaseShifts);

private:
    static int tableLookup(int fiveBitState);
};

} // namespace psk::dsp
