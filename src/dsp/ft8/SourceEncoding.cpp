#include "SourceEncoding.h"

#include <algorithm>
#include <cctype>

namespace psk::dsp::ft8 {

namespace {

int charValue37(char c)
{
    if (c == ' ') {
        return 0;
    }
    if (c >= '0' && c <= '9') {
        return 1 + (c - '0');
    }
    if (c >= 'A' && c <= 'Z') {
        return 11 + (c - 'A');
    }
    return -1;
}

char valueToChar37(int v)
{
    if (v == 0) {
        return ' ';
    }
    if (v >= 1 && v <= 10) {
        return static_cast<char>('0' + (v - 1));
    }
    if (v >= 11 && v <= 36) {
        return static_cast<char>('A' + (v - 11));
    }
    return '?';
}

std::vector<int> uintToBits(std::uint32_t value, int bitCount)
{
    std::vector<int> bits(static_cast<std::size_t>(bitCount));
    for (int i = 0; i < bitCount; ++i) {
        bits[static_cast<std::size_t>(bitCount - 1 - i)] = static_cast<int>((value >> i) & 1u);
    }
    return bits;
}

std::uint32_t bitsToUint(const std::vector<int> &bits, int start, int count)
{
    std::uint32_t value = 0;
    for (int i = 0; i < count; ++i) {
        value = (value << 1) | static_cast<std::uint32_t>(bits[static_cast<std::size_t>(start + i)]);
    }
    return value;
}

} // namespace

std::optional<std::uint32_t> SourceEncoding::packStandardCallsignCore(const std::string &sixChar)
{
    if (sixChar.size() != 6) {
        return std::nullopt;
    }
    const int c1 = charValue37(sixChar[0]);
    const int c2raw = charValue37(sixChar[1]);
    const int c3raw = charValue37(sixChar[2]);
    if (c1 < 0 || c2raw <= 0 || c3raw < 1 || c3raw > 10) {
        return std::nullopt;
    }
    auto suffixValue = [](char c) -> int {
        if (c == ' ') {
            return 0;
        }
        if (c >= 'A' && c <= 'Z') {
            return 1 + (c - 'A');
        }
        return -1;
    };
    const int c4 = suffixValue(sixChar[3]);
    const int c5 = suffixValue(sixChar[4]);
    const int c6 = suffixValue(sixChar[5]);
    if (c4 < 0 || c5 < 0 || c6 < 0) {
        return std::nullopt;
    }

    std::uint64_t n = static_cast<std::uint64_t>(c1);
    n = n * 36 + static_cast<std::uint64_t>(c2raw - 1);
    n = n * 10 + static_cast<std::uint64_t>(c3raw - 1);
    n = n * 27 + static_cast<std::uint64_t>(c4);
    n = n * 27 + static_cast<std::uint64_t>(c5);
    n = n * 27 + static_cast<std::uint64_t>(c6);
    return static_cast<std::uint32_t>(n);
}

std::optional<std::string> SourceEncoding::unpackStandardCallsignCore(std::uint32_t n)
{
    std::uint64_t v = n;
    const int c6 = static_cast<int>(v % 27);
    v /= 27;
    const int c5 = static_cast<int>(v % 27);
    v /= 27;
    const int c4 = static_cast<int>(v % 27);
    v /= 27;
    const int c3 = static_cast<int>(v % 10) + 1;
    v /= 10;
    const int c2 = static_cast<int>(v % 36) + 1;
    v /= 36;
    const int c1 = static_cast<int>(v);

    auto suffixChar = [](int val) -> char {
        if (val == 0) {
            return ' ';
        }
        if (val >= 1 && val <= 26) {
            return static_cast<char>('A' + (val - 1));
        }
        return '?';
    };

    std::string result;
    result += valueToChar37(c1);
    result += valueToChar37(c2);
    result += valueToChar37(c3);
    result += suffixChar(c4);
    result += suffixChar(c5);
    result += suffixChar(c6);

    while (!result.empty() && result.front() == ' ') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    if (result.empty()) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::uint32_t> SourceEncoding::packCallsign(const std::string &callsignIn)
{
    std::string callsign = callsignIn;
    std::transform(callsign.begin(), callsign.end(), callsign.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (callsign == "DE") {
        return 0;
    }
    if (callsign == "QRZ") {
        return 1;
    }
    if (callsign == "CQ") {
        return 2;
    }

    if (callsign.size() < 3 || callsign.size() > 6) {
        return std::nullopt;
    }

    std::string sixChar;
    if (callsign.size() >= 3 && std::isdigit(static_cast<unsigned char>(callsign[2]))) {
        // 2-character prefix (letters and/or digits, e.g. "GD4JNT",
        // "2E0LXY") - the digit is already in the field's 3rd position,
        // no padding needed.
        sixChar = callsign;
    } else if (callsign.size() >= 2 && std::isdigit(static_cast<unsigned char>(callsign[1]))) {
        // 1-character prefix (e.g. "K1ABC") - the digit is in the
        // field's 2nd position, needs a leading space to shift it into
        // the 3rd.
        sixChar = " " + callsign;
    } else {
        return std::nullopt;
    }
    if (sixChar.size() < 6) {
        sixChar.append(6 - sixChar.size(), ' ');
    }
    if (sixChar.size() != 6) {
        return std::nullopt;
    }

    const auto core = packStandardCallsignCore(sixChar);
    if (!core) {
        return std::nullopt;
    }
    return *core + 6257896u;
}

std::optional<int> SourceEncoding::packGrid(const std::string &wordIn)
{
    std::string word = wordIn;
    std::transform(word.begin(), word.end(), word.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (word.empty()) {
        return 32400;
    }
    if (word == "RRR") {
        return 32401;
    }
    if (word == "RR73") {
        return 32402;
    }
    if (word == "73") {
        return 32403;
    }
    if (word.size() == 4 && word[0] >= 'A' && word[0] <= 'R' && word[1] >= 'A' && word[1] <= 'R'
        && std::isdigit(static_cast<unsigned char>(word[2])) && std::isdigit(static_cast<unsigned char>(word[3]))) {
        const int ch1 = word[0] - 'A';
        const int ch2 = word[1] - 'A';
        const int ch3 = word[2] - '0';
        const int ch4 = word[3] - '0';
        return ch1 * 18 * 10 * 10 + ch2 * 10 * 10 + ch3 * 10 + ch4;
    }
    if ((word.size() == 2 || word.size() == 3) && (word[0] == '-' || word[0] == '+' || std::isdigit(static_cast<unsigned char>(word[0])))) {
        try {
            const int report = std::stoi(word);
            if (report >= -30 && report <= 99) {
                return report + 35;
            }
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<int>> SourceEncoding::packType1Message(
    const std::string &callsign1, bool r1Flag, const std::string &callsign2, bool r2Flag,
    bool ackFlag, const std::string &gridOrReportOrWord)
{
    const auto c1 = packCallsign(callsign1);
    const auto c2 = packCallsign(callsign2);
    const auto g = packGrid(gridOrReportOrWord);
    if (!c1 || !c2 || !g) {
        return std::nullopt;
    }

    std::vector<int> bits;
    bits.reserve(77);
    auto append = [&](std::uint32_t value, int bitCount) {
        const auto valueBits = uintToBits(value, bitCount);
        bits.insert(bits.end(), valueBits.begin(), valueBits.end());
    };
    append(*c1, 28);
    bits.push_back(r1Flag ? 1 : 0);
    append(*c2, 28);
    bits.push_back(r2Flag ? 1 : 0);
    bits.push_back(ackFlag ? 1 : 0);
    append(static_cast<std::uint32_t>(*g), 15);
    append(1, 3);

    if (bits.size() != 77) {
        return std::nullopt;
    }
    return bits;
}

std::optional<std::string> SourceEncoding::unpackType1Message(const std::vector<int> &bits77)
{
    if (bits77.size() != 77) {
        return std::nullopt;
    }
    const int i3 = static_cast<int>(bitsToUint(bits77, 74, 3));
    if (i3 != 1) {
        return std::nullopt;
    }

    const std::uint32_t c1 = bitsToUint(bits77, 0, 28);
    const bool r1 = bits77[28] != 0;
    const std::uint32_t c2 = bitsToUint(bits77, 29, 28);
    const bool r2 = bits77[57] != 0;
    const bool ack = bits77[58] != 0;
    const int g = static_cast<int>(bitsToUint(bits77, 59, 15));

    std::string call1;
    if (c1 == 0) {
        call1 = "DE";
    } else if (c1 == 1) {
        call1 = "QRZ";
    } else if (c1 == 2) {
        call1 = "CQ";
    } else if (c1 >= 6257896) {
        const auto core = unpackStandardCallsignCore(c1 - 6257896);
        if (!core) {
            return std::nullopt;
        }
        call1 = *core;
    } else {
        return std::nullopt;
    }
    if (r1) {
        call1 += "/R";
    }

    std::string call2;
    if (c2 >= 6257896) {
        const auto core = unpackStandardCallsignCore(c2 - 6257896);
        if (!core) {
            return std::nullopt;
        }
        call2 = *core;
    } else {
        return std::nullopt;
    }
    if (r2) {
        call2 += "/R";
    }

    std::string gridWord;
    if (g == 32400) {
        gridWord.clear();
    } else if (g == 32401) {
        gridWord = "RRR";
    } else if (g == 32402) {
        gridWord = "RR73";
    } else if (g == 32403) {
        gridWord = "73";
    } else if (g >= 0 && g < 32400) {
        const int ch1 = g / (18 * 10 * 10);
        const int rem1 = g % (18 * 10 * 10);
        const int ch2 = rem1 / (10 * 10);
        const int rem2 = rem1 % (10 * 10);
        const int ch3 = rem2 / 10;
        const int ch4 = rem2 % 10;
        gridWord = std::string(1, static_cast<char>('A' + ch1)) + static_cast<char>('A' + ch2)
            + static_cast<char>('0' + ch3) + static_cast<char>('0' + ch4);
    } else {
        const int report = g - 35;
        gridWord = (report >= 0 ? "+" : "") + std::to_string(report);
    }

    std::string result = call1 + " " + call2;
    if (ack) {
        result += " R";
    }
    if (!gridWord.empty()) {
        result += " " + gridWord;
    }
    return result;
}

} // namespace psk::dsp::ft8
