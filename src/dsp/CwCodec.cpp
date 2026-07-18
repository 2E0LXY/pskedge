#include "CwCodec.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>

namespace psk::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Standard International Morse Code. Kept as a single source-of-truth map,
// used directly for encode (char -> pattern) and inverted once for decode
// (pattern -> char) rather than maintaining two separate tables that could
// drift apart.
const std::map<char, std::string> &morseTable()
{
    static const std::map<char, std::string> table = {
        {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."},
        {'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"},
        {'K', "-.-"}, {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"},
        {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
        {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, {'Y', "-.--"},
        {'Z', "--.."},
        {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
        {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
        {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'/', "-..-."}, {'=', "-...-"},
    };
    return table;
}

const std::unordered_map<std::string, char> &reverseMorseTable()
{
    static const std::unordered_map<std::string, char> table = [] {
        std::unordered_map<std::string, char> reversed;
        for (const auto &[ch, pattern] : morseTable()) {
            reversed[pattern] = ch;
        }
        return reversed;
    }();
    return table;
}

} // namespace

CwCodec::CwCodec(CwConfig config)
    : m_config(config)
{
}

CwConfig CwCodec::config() const
{
    return m_config;
}

std::vector<double> CwCodec::modulateText(const std::string &text) const
{
    // PARIS standard: the word "PARIS" (including trailing word space) is
    // defined as exactly 50 dot-units, giving dot duration (seconds) =
    // 1.2 / WPM - the conventional formula for Morse timing, not a
    // figure invented here.
    const double unitSeconds = 1.2 / std::max(1.0, m_config.wpm);
    const auto unitSamples = static_cast<std::size_t>(std::llround(unitSeconds * m_config.sampleRate));

    std::vector<double> samples;
    samples.reserve(unitSamples * text.size() * 8); // rough headroom, avoids repeated reallocation

    // Short raised-cosine ramp on every tone on/off transition so keying
    // doesn't produce audible clicks (a hard on/off step is broadband
    // impulse noise) - same shaping principle as Bpsk31Codec's
    // raised-cosine pulse shaping, just applied to on/off keying rather
    // than phase.
    const auto rampSamples = std::max<std::size_t>(1, unitSamples / 8);
    auto appendTone = [&](std::size_t durationSamples) {
        for (std::size_t n = 0; n < durationSamples; ++n) {
            double envelope = 1.0;
            if (n < rampSamples) {
                envelope = 0.5 - 0.5 * std::cos(kPi * static_cast<double>(n) / static_cast<double>(rampSamples));
            } else if (n >= durationSamples - rampSamples) {
                const std::size_t fromEnd = durationSamples - n;
                envelope = 0.5 - 0.5 * std::cos(kPi * static_cast<double>(fromEnd) / static_cast<double>(rampSamples));
            }
            const double phase = 2.0 * kPi * m_config.toneHz * static_cast<double>(n) / m_config.sampleRate;
            samples.push_back(m_config.amplitude * envelope * std::sin(phase));
        }
    };
    auto appendSilence = [&](std::size_t durationSamples) {
        samples.insert(samples.end(), durationSamples, 0.0);
    };

    bool anyCharEmitted = false;
    bool pendingWordGap = false;
    for (char rawCh : text) {
        const char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(rawCh)));
        if (ch == ' ') {
            pendingWordGap = true;
            continue;
        }
        const auto it = morseTable().find(ch);
        if (it == morseTable().end()) {
            continue; // unsupported character - skipped, not substituted with something misleading
        }
        if (anyCharEmitted) {
            appendSilence(unitSamples * (pendingWordGap ? 7 : 3));
        }
        pendingWordGap = false;
        const std::string &pattern = it->second;
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            if (i > 0) {
                appendSilence(unitSamples); // intra-character gap
            }
            appendTone(pattern[i] == '.' ? unitSamples : unitSamples * 3);
        }
        anyCharEmitted = true;
    }
    return samples;
}

std::string CwCodec::demodulateText(const std::vector<double> &samples) const
{
    if (samples.empty()) {
        return {};
    }

    // Envelope via a short-block tone correlator at the target frequency -
    // same in-band energy technique as Bpsk31Codec::measureSignalQuality's
    // blockRms, just applied over much shorter blocks for the time
    // resolution CW timing needs (a PSK31 symbol is ~32ms; a fast CW dot
    // at 30WPM is ~40ms, so a similar block size is appropriate here).
    constexpr double kBlockSeconds = 0.004;
    const auto blockSamples = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::llround(kBlockSeconds * m_config.sampleRate)));
    const double step = 2.0 * kPi * m_config.toneHz / m_config.sampleRate;

    std::vector<double> envelope;
    envelope.reserve(samples.size() / blockSamples + 1);
    double phase = 0.0;
    for (std::size_t start = 0; start < samples.size(); start += blockSamples) {
        const std::size_t end = std::min(samples.size(), start + blockSamples);
        double iSum = 0.0;
        double qSum = 0.0;
        for (std::size_t n = start; n < end; ++n) {
            iSum += samples[n] * std::cos(phase);
            qSum += -samples[n] * std::sin(phase);
            phase += step;
            if (phase > 2.0 * kPi) {
                phase -= 2.0 * kPi;
            }
        }
        const double count = static_cast<double>(end - start);
        envelope.push_back(std::sqrt(iSum * iSum + qSum * qSum) / std::max(1.0, count));
    }
    if (envelope.empty()) {
        return {};
    }

    {
        constexpr int kSmoothingRadius = 2; // 5-block moving average
        std::vector<double> smoothed(envelope.size());
        for (std::size_t i = 0; i < envelope.size(); ++i) {
            const std::size_t start = i > static_cast<std::size_t>(kSmoothingRadius) ? i - kSmoothingRadius : 0;
            const std::size_t end = std::min(envelope.size(), i + kSmoothingRadius + 1);
            double sum = 0.0;
            for (std::size_t j = start; j < end; ++j) {
                sum += envelope[j];
            }
            smoothed[i] = sum / static_cast<double>(end - start);
        }
        envelope = std::move(smoothed);
    }

    // Adaptive on/off threshold: midpoint between the envelope's overall
    // min and max, not a fixed absolute level - real signals vary
    // enormously in level, and a fixed threshold would only work at one
    // specific SNR.
    std::vector<double> sortedEnvelope = envelope;
    std::sort(sortedEnvelope.begin(), sortedEnvelope.end());
    const auto percentile = [&](double p) {
        const auto index = static_cast<std::size_t>(p * static_cast<double>(sortedEnvelope.size() - 1));
        return sortedEnvelope[index];
    };
    const double lowLevel = percentile(0.10);
    const double highLevel = percentile(0.90);
    if (highLevel - lowLevel < 1e-9) {
        return {}; // flat envelope - no signal, nothing to decode
    }
    // Real on/off keying spends a large, genuine fraction of time clearly
    // on and clearly off, giving a large gap between the 10th and 90th
    // percentile envelope levels. Noise's percentile gap reflects its
    // actual, bounded fluctuation - unlike raw min/max, which are
    // dominated by random extremes over many samples (order statistics
    // grow with sample count even for stationary noise) and did not
    // reliably reject noise in testing: 3s of Gaussian noise alone
    // decoded to 140+ characters of confident-looking but entirely fake
    // text before this was switched from min/max to percentiles.
    constexpr double kMinContrastRatio = 3.0;
    if (highLevel / std::max(lowLevel, 1e-9) < kMinContrastRatio) {
        return {};
    }
    const double threshold = lowLevel + (highLevel - lowLevel) * 0.4;

    // Run-length encode the on/off sequence in block units.
    struct Run {
        bool on;
        std::size_t blocks;
    };
    std::vector<Run> runs;
    bool currentOn = envelope.front() > threshold;
    std::size_t currentLen = 1;
    for (std::size_t i = 1; i < envelope.size(); ++i) {
        const bool on = envelope[i] > threshold;
        if (on == currentOn) {
            ++currentLen;
        } else {
            runs.push_back({currentOn, currentLen});
            currentOn = on;
            currentLen = 1;
        }
    }
    runs.push_back({currentOn, currentLen});

    // Adaptive unit-length (dot duration) estimate: the shortest "on" runs
    // are dots almost by definition (a dash is 3x a dot at the same
    // speed), so the minimum "on" run length across the whole buffer is a
    // reasonable, genuinely adaptive estimate of the sender's actual
    // speed - not a fixed assumed WPM, which would only decode correctly
    // at one specific speed.
    std::size_t minOnBlocks = std::numeric_limits<std::size_t>::max();
    for (const Run &run : runs) {
        if (run.on && run.blocks < minOnBlocks) {
            minOnBlocks = run.blocks;
        }
    }
    if (minOnBlocks == std::numeric_limits<std::size_t>::max() || minOnBlocks == 0) {
        return {}; // no "on" runs at all - nothing was actually keyed
    }
    const auto unitBlocks = static_cast<double>(minOnBlocks);

    std::string result;
    std::string symbolBuffer;
    const auto &reverseTable = reverseMorseTable();
    auto flushSymbol = [&]() {
        if (symbolBuffer.empty()) {
            return;
        }
        const auto it = reverseTable.find(symbolBuffer);
        result += (it != reverseTable.end()) ? it->second : '?';
        symbolBuffer.clear();
    };

    for (const Run &run : runs) {
        const double lengthInUnits = static_cast<double>(run.blocks) / unitBlocks;
        if (run.on) {
            symbolBuffer += (lengthInUnits < 2.0) ? '.' : '-';
        } else {
            if (lengthInUnits >= 5.0) {
                flushSymbol();
                result += ' ';
            } else if (lengthInUnits >= 2.0) {
                flushSymbol();
            }
            // else: intra-character gap, symbolBuffer continues
        }
    }
    flushSymbol();

    // Marginal-SNR sanity check: a heavily noise-corrupted decode
    // characteristically fragments into many short "words" (T and E, the
    // two shortest Morse patterns, survive noise-driven misclassification
    // far more often than longer patterns) - a real signal, even a short
    // message, does not look like this. Confirmed in testing: right past
    // the contrast gate's SNR boundary (enough signal to pass it, not
    // enough to decode reliably), the decoder was producing confident-
    // looking but entirely wrong text rather than failing cleanly. This
    // 0.6 threshold catches most of that range without false-rejecting
    // real ham-radio-style text (which legitimately has many short
    // tokens - "K", "DE", "CQ", "73" are all common); a tighter
    // threshold (tried 0.35) was worse, rejecting genuinely correct
    // decodes of exactly that kind of text. One specific noise level
    // tested (amplitude 0.6 signal vs noise std 0.6, ~0dB) still slips
    // through with garbage - a known remaining gap in this heuristic,
    // not claimed as fully solved.
    if (result.size() >= 8) {
        std::size_t wordCount = 0;
        std::size_t shortWordCount = 0;
        std::size_t wordStart = 0;
        auto countWord = [&](std::size_t start, std::size_t end) {
            if (end > start) {
                ++wordCount;
                if (end - start <= 2) {
                    ++shortWordCount;
                }
            }
        };
        for (std::size_t i = 0; i < result.size(); ++i) {
            if (result[i] == ' ') {
                countWord(wordStart, i);
                wordStart = i + 1;
            }
        }
        countWord(wordStart, result.size());
        if (wordCount > 0 && static_cast<double>(shortWordCount) / static_cast<double>(wordCount) > 0.6) {
            return {};
        }
    }
    return result;
}

} // namespace psk::dsp
