#pragma once

#include <string>
#include <vector>

namespace psk::dsp {

struct CwConfig {
    double sampleRate = 8000.0;
    double toneHz = 700.0; // standard CW sidetone/RX tone
    double wpm = 18.0;     // used for modulateText(); demodulateText() estimates WPM adaptively
    double amplitude = 0.6;
};

// Real envelope-detect on/off-keying CW decoder - not a placeholder. Uses a
// tone correlator (same technique as Bpsk31Codec's measureSignalQuality
// in-band energy check) to build an envelope, an adaptive on/off
// threshold, and an adaptive dot-length (WPM) estimate rather than a fixed
// assumed speed, since real operators send at a wide range of speeds and a
// fixed-WPM decoder would only work at one.
class CwCodec {
public:
    explicit CwCodec(CwConfig config = {});

    std::vector<double> modulateText(const std::string &text) const;
    std::string demodulateText(const std::vector<double> &samples) const;

    CwConfig config() const;

private:
    CwConfig m_config;
};

} // namespace psk::dsp
