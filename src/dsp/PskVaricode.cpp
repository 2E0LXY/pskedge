#include "PskVaricode.h"

#include <array>
#include <unordered_map>

namespace psk::dsp {
namespace {

// PSK31 Varicode table, ASCII 0-127. Bits are transmitted left first.
// 0 is a BPSK phase reversal; 1 is steady carrier.
constexpr std::array<const char *, 128> kVaricode = {
    "1010101011", "1011011011", "1011101101", "1101110111",
    "1011101011", "1101011111", "1011101111", "1011111101",
    "1011111111", "11101111",   "11101",      "1101101111",
    "1011011101", "11111",      "1101110101", "1110101011",
    "1011110111", "1011110101", "1110101101", "1110101111",
    "1101011011", "1101101011", "1101101101", "1101010111",
    "1101111011", "1101111101", "1110110111", "1101010101",
    "1101011101", "1110111011", "1011111011", "1101111111",
    "1",          "111111111",  "101011111",  "111110101",
    "111011011",  "1011010101", "1010111011", "101111111",
    "11111011",   "11110111",   "101101111",  "111011111",
    "1110101",    "110101",     "1010111",    "110101111",
    "10110111",   "10111101",   "11101101",   "11111111",
    "101110111",  "101011011",  "101101011",  "110101101",
    "110101011",  "110110111",  "11110101",   "110111101",
    "111101101",  "1010101",    "111010111",  "1010101111",
    "1010111101", "1111101",    "11101011",   "10101101",
    "10110101",   "1110111",    "11011011",   "11111101",
    "101010101",  "1111111",    "111111101",  "101111101",
    "11010111",   "10111011",   "11011101",   "10101011",
    "11010101",   "111011101",  "10101111",   "1101111",
    "1101101",    "101010111",  "110110101",  "101011101",
    "101110101",  "101111011",  "1010101101", "111110111",
    "111101111",  "111111011",  "1010111111", "101101101",
    "1011011111", "1011",       "1011111",    "101111",
    "101101",     "11",         "111101",     "1011011",
    "101011",     "1101",       "111101011",  "10111111",
    "11011",      "111011",     "1111",       "111",
    "111111",     "110111111",  "10101",      "10111",
    "101",        "110111",     "1111011",    "1101011",
    "11011111",   "1011101",    "111010101",  "1010110111",
    "110111011",  "1010110101", "1011010111", "1110110101"
};

const std::unordered_map<std::string, char> &decodeMap()
{
    static const std::unordered_map<std::string, char> map = [] {
        std::unordered_map<std::string, char> out;
        for (std::size_t i = 0; i < kVaricode.size(); ++i) {
            out.emplace(kVaricode[i], static_cast<char>(i));
        }
        return out;
    }();
    return map;
}

} // namespace

const char *PskVaricode::codeForAscii(unsigned char ch)
{
    return kVaricode[ch & 0x7f];
}

std::vector<int> PskVaricode::encodeTextBits(const std::string &text)
{
    std::vector<int> bits;
    bits.reserve(text.size() * 8);

    for (unsigned char ch : text) {
        const char *code = codeForAscii(ch);
        for (const char *cursor = code; *cursor; ++cursor) {
            bits.push_back(*cursor == '1' ? 1 : 0);
        }
        bits.push_back(0);
        bits.push_back(0);
    }

    return bits;
}

std::string PskVaricode::decodeTextBits(const std::vector<int> &bits)
{
    std::string out;
    std::string current;
    int zeroRun = 0;

    for (int rawBit : bits) {
        const int bit = rawBit ? 1 : 0;
        if (bit == 1) {
            if (zeroRun == 1 && !current.empty()) {
                current.push_back('0');
            }
            zeroRun = 0;
            current.push_back('1');
            if (current.size() > 10) {
                current.clear();
            }
            continue;
        }

        ++zeroRun;
        if (zeroRun >= 2) {
            if (!current.empty()) {
                const auto found = decodeMap().find(current);
                if (found != decodeMap().end()) {
                    out.push_back(found->second);
                } else {
                    out.push_back('?');
                }
                current.clear();
            }
            zeroRun = 2;
        }
    }

    return out;
}

bool PskVaricode::validateTable(std::string *error)
{
    std::unordered_map<std::string, int> seen;
    for (std::size_t i = 0; i < kVaricode.size(); ++i) {
        const std::string code = kVaricode[i];
        if (code.empty() || code.front() != '1' || code.back() != '1') {
            if (error) {
                *error = "Varicode entry must start and end with 1";
            }
            return false;
        }
        if (code.find("00") != std::string::npos) {
            if (error) {
                *error = "Varicode entry contains 00";
            }
            return false;
        }
        if (seen.contains(code)) {
            if (error) {
                *error = "Duplicate Varicode entry";
            }
            return false;
        }
        seen.emplace(code, static_cast<int>(i));
    }
    return true;
}

} // namespace psk::dsp
