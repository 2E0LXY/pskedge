#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace psk::dsp::ft8 {

// Source encoding for FT8/FT4 message type 1 (the standard two-callsign
// QSO message - c28 r1 c28 r1 R1 g15, per QEX paper Table 1), the most
// common real-world message type. Algorithm reconstructed from multiple
// independently-corroborating technical sources (see SourceEncoding.cpp
// for citations) describing WSJT-X's own std_call_to_c28/grid4_to_g15
// routines, not from the WSJT-X source code directly (this project does
// not have access to it) - flagged here plainly as a real risk: getting
// this exactly bit-right matters for genuine interoperability with real
// WSJT-X stations, and has only been verified by this project via
// internal round-trip self-consistency, not against a real WSJT-X
// instance or official test vectors (unlike Crc14 and Ldpc174_91, which
// were checked against real reference test vectors).
class SourceEncoding {
public:
    // Packs a standard amateur callsign (or CQ, DE, QRZ) into its 28-bit
    // integer value per QEX paper Table 7. Returns std::nullopt if the
    // callsign doesn't fit the standard pattern (needs message type 4's
    // nonstandard-call/hash encoding instead, not implemented here).
    static std::optional<std::uint32_t> packCallsign(const std::string &callsign);

    // Packs a 4-character Maidenhead grid locator, or a signal report
    // (-30 to +99 dB), or blank, RRR, RR73, 73, into its 15-bit value.
    static std::optional<int> packGrid(const std::string &gridOrReportOrWord);

    // Assembles a full 77-bit message type 1 payload: two callsigns, the
    // /R flags, the R acknowledgment flag, and the grid/report field.
    // Returns 77 bits (not yet CRC'd or LDPC-encoded).
    static std::optional<std::vector<int>> packType1Message(
        const std::string &callsign1, bool r1Flag,
        const std::string &callsign2, bool r2Flag,
        bool ackFlag, const std::string &gridOrReportOrWord);

    // Inverse of packType1Message - unpacks 77 raw message bits (i3==1)
    // back into a human-readable string, e.g. "K1ABC W9XYZ R EN37".
    // Returns std::nullopt if the bits don't decode to a message type 1
    // this implementation understands.
    static std::optional<std::string> unpackType1Message(const std::vector<int> &bits77);

private:
    static std::optional<std::uint32_t> packStandardCallsignCore(const std::string &sixChar);
    static std::optional<std::string> unpackStandardCallsignCore(std::uint32_t n);
};

} // namespace psk::dsp::ft8
