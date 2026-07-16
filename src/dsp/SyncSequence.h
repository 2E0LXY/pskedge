#pragma once

#include <vector>

namespace psk::dsp {

// Generates a maximal-length pseudorandom binary sequence (m-sequence) via
// a linear feedback shift register - the standard choice for a
// synchronization/acquisition preamble because of its near-ideal
// autocorrelation (a single sharp peak, low sidelobes everywhere else),
// the same property that makes m-sequences and Gold codes the basis for
// GPS and DSSS acquisition. Not a novel design - a well-established,
// verifiable technique (see selftest for the full-period self-check).
class SyncSequence {
public:
    // Generates a period-(2^order - 1) m-sequence using a known primitive
    // polynomial for the given order. Only a small set of orders is
    // supported (only what's needed); asserts/returns empty for others.
    // Output is 0/1 bits, length exactly (2^order - 1).
    static std::vector<int> generate(int order);
};

} // namespace psk::dsp
