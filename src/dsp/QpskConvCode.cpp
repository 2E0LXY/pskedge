#include "QpskConvCode.h"

#include <array>
#include <cstdint>
#include <limits>

namespace psk::dsp {

namespace {

// Indexed by the full 5-bit window value (0-31, MSB = oldest bit in the
// window, LSB = newest). Transcribed directly from the ARRL/QEX and
// original web-page G3PLX articles' "Convolutional Code" table, verified
// against the spec's own worked example (see QpskConvCode.h) before use.
constexpr std::array<int, 32> kPhaseShiftTable = {
    2, 1, 3, 0, 3, 0, 2, 1, 0, 3, 1, 2, 1, 2, 0, 3,
    1, 2, 0, 3, 0, 3, 1, 2, 3, 0, 2, 1, 2, 1, 3, 0,
};

constexpr int kNumStates = 32;

} // namespace

int QpskConvCode::tableLookup(int fiveBitState)
{
    return kPhaseShiftTable[static_cast<std::size_t>(fiveBitState & 0x1F)];
}

std::vector<int> QpskConvCode::encode(const std::vector<int> &bits)
{
    std::vector<int> phaseShifts;
    phaseShifts.reserve(bits.size());
    int state = 0; // all-zero window - matches the idle/preamble state
    for (int bit : bits) {
        state = ((state << 1) | (bit & 1)) & 0x1F;
        phaseShifts.push_back(tableLookup(state));
    }
    return phaseShifts;
}

std::vector<int> QpskConvCode::decode(const std::vector<int> &phaseShifts)
{
    if (phaseShifts.empty()) {
        return {};
    }

    // Standard Viterbi over the 32-state trellis. Each state has exactly
    // 2 possible predecessors (this shift-register structure means a
    // state's own bottom 4 bits equal SOME predecessor's top 4 bits, but
    // the predecessor's own top bit is not recoverable from the state
    // value alone - see below), so the forward pass must record the full
    // predecessor STATE at each step, not just which bit was used, or
    // traceback cannot correctly continue past one step.
    constexpr int kInfeasible = std::numeric_limits<int>::max() / 2;
    std::array<int, kNumStates> pathMetric{};
    pathMetric.fill(kInfeasible);
    pathMetric[0] = 0; // starts in the idle (all-zero) state, same as encode()

    std::vector<std::array<std::int8_t, kNumStates>> predecessorState(phaseShifts.size());

    std::array<int, kNumStates> nextMetric{};
    for (std::size_t t = 0; t < phaseShifts.size(); ++t) {
        nextMetric.fill(kInfeasible);
        for (int state = 0; state < kNumStates; ++state) {
            if (pathMetric[static_cast<std::size_t>(state)] >= kInfeasible) {
                continue;
            }
            for (int bit = 0; bit <= 1; ++bit) {
                const int nextState = ((state << 1) | bit) & 0x1F;
                const int predictedShift = tableLookup(nextState);
                const int cost = pathMetric[static_cast<std::size_t>(state)]
                    + (predictedShift == phaseShifts[t] ? 0 : 1);
                if (cost < nextMetric[static_cast<std::size_t>(nextState)]) {
                    nextMetric[static_cast<std::size_t>(nextState)] = cost;
                    predecessorState[t][static_cast<std::size_t>(nextState)] = static_cast<std::int8_t>(state);
                }
            }
        }
        pathMetric = nextMetric;
    }

    // Trace back from whichever state has the best (lowest-disagreement)
    // final path.
    int bestState = 0;
    int bestMetric = pathMetric[0];
    for (int state = 1; state < kNumStates; ++state) {
        if (pathMetric[static_cast<std::size_t>(state)] < bestMetric) {
            bestMetric = pathMetric[static_cast<std::size_t>(state)];
            bestState = state;
        }
    }

    std::vector<int> bits(phaseShifts.size());
    int state = bestState;
    for (std::size_t t = phaseShifts.size(); t-- > 0;) {
        // The bit consumed on this transition is always state's own LSB
        // (by construction: nextState = ((prevState << 1) | bit) & 0x1F),
        // so it's directly recoverable from the current state - no need
        // to have stored it separately.
        bits[t] = state & 1;
        state = predecessorState[t][static_cast<std::size_t>(state)];
    }
    return bits;
}

} // namespace psk::dsp
