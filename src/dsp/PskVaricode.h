#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace psk::dsp {

class PskVaricode {
public:
    static const char *codeForAscii(unsigned char ch);
    static std::vector<int> encodeTextBits(const std::string &text);
    static std::string decodeTextBits(const std::vector<int> &bits);
    static bool validateTable(std::string *error = nullptr);
};

} // namespace psk::dsp
