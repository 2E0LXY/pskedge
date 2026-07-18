#include "dsp/BlockSyncCodec.h"
#include "dsp/Bpsk31Codec.h"
#include "dsp/ConvCode.h"
#include "dsp/Crc16.h"
#include "dsp/CwCodec.h"
#include "dsp/PskVaricode.h"

#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// Golden vectors from the ARRL/G3PLX PSK31 Varicode specification
// (https://www.arrl.org/psk31-spec). A pure round-trip test cannot catch a
// wrong table entry because encode and decode share the same table; these
// vectors are transcribed independently to catch that class of bug.
bool checkGoldenVaricodeVectors(std::string *error)
{
    static const std::unordered_map<unsigned char, std::string> golden = {
        {' ', "1"}, {'e', "11"}, {'t', "101"}, {'a', "1011"},
        {'C', "10101101"}, {'Q', "1111011101"}, {'K', "101111101"},
        {'0', "10110111"}, {'9', "110110111"},
    };

    for (const auto &[ch, expected] : golden) {
        const std::string actual = psk::dsp::PskVaricode::codeForAscii(ch);
        if (actual != expected) {
            if (error) {
                *error = std::string("Varicode mismatch for '") + static_cast<char>(ch)
                    + "': expected " + expected + " got " + actual;
            }
            return false;
        }
    }
    return true;
}

// Simulates a TX/RX sample-clock mismatch (independent soundcard clocks),
// which shows up to the demodulator as gradual symbol timing drift.
std::vector<double> resample(const std::vector<double> &in, double rateFactor)
{
    std::vector<double> out;
    const std::size_t outLen = static_cast<std::size_t>(in.size() / rateFactor);
    out.reserve(outLen);
    for (std::size_t i = 0; i < outLen; ++i) {
        const double srcPos = static_cast<double>(i) * rateFactor;
        const auto i0 = static_cast<std::size_t>(srcPos);
        if (i0 + 1 >= in.size()) {
            break;
        }
        const double frac = srcPos - static_cast<double>(i0);
        out.push_back(in[i0] * (1.0 - frac) + in[i0 + 1] * frac);
    }
    return out;
}

// Regression fixtures for the Costas carrier PLL + Gardner symbol-timing
// loop in Bpsk31Codec::demodulateBits. Validated envelope: carrier offset
// up to +/-7Hz, sample-clock drift up to +/-0.1% - see the gain comment in
// Bpsk31Codec.cpp for why 7-8Hz is a real architectural ceiling for a
// symbol-rate Costas loop on differentially-encoded BPSK, not a bug.
bool checkImpairmentRecovery(std::string *error)
{
    const std::string message = "CQ CQ DE 2E0LXY 2E0LXY K THE QUICK BROWN FOX 73";

    struct FreqCase { double offsetHz; };
    for (const FreqCase c : {FreqCase{2.0}, FreqCase{5.0}, FreqCase{8.0}, FreqCase{10.0}, FreqCase{-10.0}}) {
        psk::dsp::Bpsk31Config txConfig;
        txConfig.carrierHz = 1000.0 + c.offsetHz;
        const psk::dsp::Bpsk31Codec txCodec(txConfig);
        const std::vector<double> samples = txCodec.modulateText(message);

        const psk::dsp::Bpsk31Codec rxCodec; // nominal 1000Hz
        const std::string decoded = rxCodec.demodulateText(samples);
        if (decoded.find(message) == std::string::npos) {
            if (error) {
                *error = "Costas loop + acquisition failed to recover " + std::to_string(c.offsetHz)
                    + "Hz carrier offset (within the validated +/-10Hz envelope)";
            }
            return false;
        }
    }

    struct DriftCase { double rateFactor; };
    for (const DriftCase c : {DriftCase{1.0005}, DriftCase{0.9995}, DriftCase{1.001}, DriftCase{0.999}}) {
        const psk::dsp::Bpsk31Codec txCodec;
        const std::vector<double> samples = txCodec.modulateText(message);
        const std::vector<double> drifted = resample(samples, c.rateFactor);

        const psk::dsp::Bpsk31Codec rxCodec;
        const std::string decoded = rxCodec.demodulateText(drifted);
        if (decoded.find(message) == std::string::npos) {
            if (error) {
                *error = "Gardner loop failed to recover clock drift factor "
                    + std::to_string(c.rateFactor) + " (within the validated +/-0.1% envelope)";
            }
            return false;
        }
    }

    // Combined frequency offset + clock drift, both loops running at once.
    {
        psk::dsp::Bpsk31Config txConfig;
        txConfig.carrierHz = 1004.0;
        const psk::dsp::Bpsk31Codec txCodec(txConfig);
        const std::vector<double> samples = txCodec.modulateText(message);
        const std::vector<double> drifted = resample(samples, 1.0007);

        const psk::dsp::Bpsk31Codec rxCodec;
        const std::string decoded = rxCodec.demodulateText(drifted);
        if (decoded.find(message) == std::string::npos) {
            if (error) {
                *error = "Combined Costas+Gardner recovery failed for 4Hz offset + 0.07% drift";
            }
            return false;
        }
    }

    // Sanity check that there's still a real ceiling (multi-hypothesis
    // acquisition widened it, didn't remove it) and hasn't silently
    // vanished or silently gotten worse: 20Hz is expected to fail - well
    // past the ~12-13Hz boundary measured during development. If this
    // starts passing, the envelope comment needs updating; if something
    // within the validated +/-10Hz range starts failing, that's a
    // regression.
    {
        psk::dsp::Bpsk31Config txConfig;
        txConfig.carrierHz = 1020.0;
        const psk::dsp::Bpsk31Codec txCodec(txConfig);
        const std::vector<double> samples = txCodec.modulateText(message);
        const psk::dsp::Bpsk31Codec rxCodec;
        const std::string decoded = rxCodec.demodulateText(samples);
        if (decoded.find(message) != std::string::npos) {
            if (error) {
                *error = "20Hz offset unexpectedly decoded - envelope comment in "
                    "Bpsk31Codec.cpp is now understating the loop's actual pull-in range";
            }
            return false;
        }
    }

    return true;
}

bool checkConvCodeRoundTrip(std::string *error)
{
    // No-noise round trip: encode then decode with hard +/-1 soft values
    // should reproduce the message exactly. Catches basic encoder/decoder
    // structural bugs (trellis indexing, generator polynomial mistakes)
    // before any noise-performance claim is meaningful.
    std::vector<int> msg = {1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 1,
                             1, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 1, 1, 0};
    const std::vector<int> encoded = psk::dsp::ConvCode::encode(msg);
    const std::size_t expectedEncodedSize = (msg.size() + psk::dsp::ConvCode::kConstraintLength - 1) * 2;
    if (encoded.size() != expectedEncodedSize) {
        if (error) {
            *error = "ConvCode::encode produced " + std::to_string(encoded.size())
                + " bits, expected " + std::to_string(expectedEncodedSize);
        }
        return false;
    }

    std::vector<double> soft;
    soft.reserve(encoded.size());
    for (int b : encoded) {
        soft.push_back(b ? 1.0 : -1.0);
    }
    const std::vector<int> decoded = psk::dsp::ConvCode::decodeSoft(soft, static_cast<int>(msg.size()));
    if (decoded != msg) {
        if (error) {
            *error = "ConvCode no-noise round trip did not reproduce the original message";
        }
        return false;
    }
    return true;
}

bool checkConvCodeCodingGain(std::string *error)
{
    // Fixed-seed Monte Carlo regression check: at Eb/N0 = 1.0dB, this code
    // measured ~7.5e-5 BER / ~0.6% frame error rate during development
    // (see the conversation history / commit message for the full
    // BER-vs-EbN0 characterisation this is drawn from). This is a coarse
    // regression guard, not a full re-characterisation on every test run -
    // it catches a broken decoder (e.g. a generator polynomial or trellis
    // bug that silently destroys coding gain) without the cost of a full
    // multi-point sweep in the ordinary test suite.
    std::mt19937 rng(20260716); // fixed seed: reproducible, not tuned to pass
    std::normal_distribution<double> noise(0.0, 1.0);

    const int messageBits = 200;
    const int trials = 500;
    const double ebN0 = std::pow(10.0, 1.0 / 10.0); // 1.0dB
    const double noiseStd = std::sqrt(1.0 / (2.0 * ebN0 * 2.0));

    long errors = 0;
    long total = 0;
    for (int trial = 0; trial < trials; ++trial) {
        std::vector<int> msg(messageBits);
        for (auto &b : msg) {
            b = static_cast<int>(rng() & 1u);
        }
        const std::vector<int> encoded = psk::dsp::ConvCode::encode(msg);
        std::vector<double> soft(encoded.size());
        for (std::size_t i = 0; i < encoded.size(); ++i) {
            const double tx = (encoded[i] ? 1.0 : -1.0) * std::sqrt(0.5);
            soft[i] = tx + noiseStd * noise(rng);
        }
        const std::vector<int> decoded = psk::dsp::ConvCode::decodeSoft(soft, messageBits);
        for (int i = 0; i < messageBits; ++i) {
            if (decoded[static_cast<std::size_t>(i)] != msg[static_cast<std::size_t>(i)]) {
                ++errors;
            }
            ++total;
        }
    }

    const double ber = static_cast<double>(errors) / static_cast<double>(total);
    // Generous margin above the ~7.5e-5 measured value - this is a "did
    // coding gain collapse" check, not a precise re-measurement.
    if (ber > 0.01) {
        if (error) {
            *error = "ConvCode BER at Eb/N0=1.0dB measured " + std::to_string(ber)
                + ", expected well under 0.01 - coding gain may have regressed";
        }
        return false;
    }
    return true;
}

bool checkCrc16(std::string *error)
{
    const std::uint16_t crc1 = psk::dsp::Crc16::compute(std::string("CQ CQ DE 2E0LXY"));
    const std::uint16_t crc2 = psk::dsp::Crc16::compute(std::string("CQ CQ DE 2E0LXZ"));
    if (crc1 == crc2) {
        if (error) {
            *error = "Crc16 produced identical CRCs for different input";
        }
        return false;
    }
    const std::uint16_t crc1Again = psk::dsp::Crc16::compute(std::string("CQ CQ DE 2E0LXY"));
    if (crc1 != crc1Again) {
        if (error) {
            *error = "Crc16 is not deterministic for identical input";
        }
        return false;
    }
    return true;
}

bool checkBlockSyncRoundTrip(std::string *error)
{
    // No-noise, no-offset round trip - fast structural correctness check
    // (acquisition search, symbol alignment, coherent phase reference,
    // FEC/CRC framing). This alone would NOT have caught two real bugs
    // found during development (a 2x symbol-count-per-bit indexing error,
    // and insufficient frequency resolution causing payload-length phase
    // drift) - both only showed up under a timing/frequency offset, which
    // the second check below exercises.
    const std::vector<int> payload = {1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0,
                                       0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0};
    const psk::dsp::BlockSyncCodec codec;
    const std::vector<double> samples = codec.modulateBlock(payload);
    const psk::dsp::BlockDecodeResult result = codec.demodulateBlock(static_cast<int>(payload.size()), samples);

    if (!result.acquired || !result.crcValid || result.payloadBits != payload) {
        if (error) {
            *error = "BlockSyncCodec no-noise round trip failed: acquired=" + std::to_string(result.acquired)
                + " crcValid=" + std::to_string(result.crcValid);
        }
        return false;
    }
    return true;
}

bool checkBlockSyncOffsetAcquisition(std::string *error)
{
    // Fixed-seed regression check with a realistic frequency offset and
    // unknown timing (block starts mid-buffer), at Eb/N0 = 6dB (SNR2500
    // ~ -19dB, measured ~68% single-shot success rate during development -
    // see the conversation/commit history for the full Monte Carlo
    // characterisation). A single fixed-seed trial isn't a statistical
    // re-validation of that rate; it catches the two structural bugs
    // mentioned above (both of which broke every offset trial, not just
    // some), without the multi-minute runtime of a full sweep in the
    // ordinary test suite.
    const std::vector<int> payload = {1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0,
                                       0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0};
    std::mt19937 rng(7);
    std::normal_distribution<double> gauss(0.0, 1.0);

    psk::dsp::BlockSyncConfig txConfig;
    txConfig.carrierHz = 1003.5;
    const psk::dsp::BlockSyncCodec txCodec(txConfig);
    std::vector<double> samples = txCodec.modulateBlock(payload);

    std::vector<double> padded(1500, 0.0);
    padded.insert(padded.end(), samples.begin(), samples.end());
    padded.resize(padded.size() + 2000, 0.0);

    // sigma chosen for Eb/N0 ~ 6dB against this payload's measured
    // per-symbol energy (~0.01 continuous-time units at 8kHz/15.625baud
    // with this config) - see the Monte Carlo script this was validated
    // against for the derivation; hardcoded here since recomputing it
    // from scratch would just reproduce that derivation inline.
    const double sigma = 0.09;
    for (double &s : padded) {
        s += sigma * gauss(rng);
    }

    const psk::dsp::BlockSyncCodec rxCodec; // nominal 1000Hz - doesn't know the true 1003.5Hz offset
    const psk::dsp::BlockDecodeResult result = rxCodec.demodulateBlock(static_cast<int>(payload.size()), padded);

    if (!result.acquired || !result.crcValid || result.payloadBits != payload) {
        if (error) {
            *error = "BlockSyncCodec offset+noise regression check failed (this specific fixed-seed "
                "case is expected to succeed - if it doesn't, something regressed, though note the "
                "underlying success rate at this SNR is ~68%, not 100%, so a real hardware/production "
                "change to timing could occasionally flip this without indicating a bug)";
        }
        return false;
    }
    return true;
}

bool checkCwCodecRoundTrip(std::string *error)
{
    const std::string message = "CQ CQ DE 2E0LXY 2E0LXY K THE QUICK BROWN FOX 73";

    // Multiple WPM speeds with added noise - the decoder estimates WPM
    // adaptively from the signal itself (see CwCodec::demodulateText), so
    // this genuinely exercises that estimation across a real speed range,
    // not just a single hardcoded rate.
    for (double wpm : {12.0, 18.0, 25.0, 35.0}) {
        psk::dsp::CwConfig txConfig;
        txConfig.wpm = wpm;
        const psk::dsp::CwCodec txCodec(txConfig);
        std::vector<double> samples = txCodec.modulateText(message);

        std::mt19937 rng(static_cast<unsigned>(wpm * 100));
        std::normal_distribution<double> noise(0.0, 0.15);
        for (double &s : samples) {
            s += noise(rng);
        }

        const psk::dsp::CwCodec rxCodec;
        const std::string decoded = rxCodec.demodulateText(samples);
        if (decoded.find(message) == std::string::npos) {
            if (error) {
                *error = "CwCodec round-trip failed at " + std::to_string(wpm) + " WPM (decoded: \"" + decoded + "\")";
            }
            return false;
        }
    }

    // Noise rejection: pure noise must decode to nothing, not a
    // confident-looking fabrication - see the contrast-gate and
    // short-word sanity-check comments in CwCodec.cpp for what this
    // guards against (both were added after this specific failure mode
    // was found during development: 3s of Gaussian noise alone decoding
    // to 140+ characters of plausible-looking garbage).
    std::mt19937 noiseRng(99);
    std::normal_distribution<double> pureNoise(0.0, 0.3);
    std::vector<double> noiseOnly(8000 * 3);
    for (double &s : noiseOnly) {
        s = pureNoise(noiseRng);
    }
    const psk::dsp::CwCodec rxCodec;
    const std::string noiseDecoded = rxCodec.demodulateText(noiseOnly);
    if (!noiseDecoded.empty()) {
        if (error) {
            *error = "CwCodec decoded pure noise as text instead of returning empty: \"" + noiseDecoded + "\"";
        }
        return false;
    }

    return true;
}

} // namespace

int main()
{
    std::string error;
    if (!psk::dsp::PskVaricode::validateTable(&error)) {
        std::cerr << "Varicode table invalid: " << error << '\n';
        return 1;
    }

    if (!checkGoldenVaricodeVectors(&error)) {
        std::cerr << "Varicode golden vector check failed: " << error << '\n';
        return 4;
    }

    std::string printable;
    for (char ch = 32; ch < 127; ++ch) {
        printable.push_back(ch);
    }

    const auto printableBits = psk::dsp::PskVaricode::encodeTextBits(printable);
    const std::string printableRoundtrip = psk::dsp::PskVaricode::decodeTextBits(printableBits);
    if (printableRoundtrip != printable) {
        std::cerr << "Varicode printable roundtrip failed\n";
        return 2;
    }

    const std::string message = "CQ CQ DE 2E0LXY 2E0LXY K";
    psk::dsp::Bpsk31Codec codec;
    const std::vector<double> samples = codec.modulateText(message);
    const std::string decoded = codec.demodulateText(samples);
    if (decoded.find(message) == std::string::npos) {
        std::cerr << "BPSK31 roundtrip failed\n";
        std::cerr << "Expected fragment: " << message << '\n';
        std::cerr << "Decoded: " << decoded << '\n';
        return 3;
    }

    if (!checkImpairmentRecovery(&error)) {
        std::cerr << "Impairment recovery check failed: " << error << '\n';
        return 5;
    }

    if (!checkConvCodeRoundTrip(&error)) {
        std::cerr << "ConvCode round-trip check failed: " << error << '\n';
        return 6;
    }

    if (!checkConvCodeCodingGain(&error)) {
        std::cerr << "ConvCode coding gain regression check failed: " << error << '\n';
        return 7;
    }

    if (!checkCrc16(&error)) {
        std::cerr << "Crc16 check failed: " << error << '\n';
        return 8;
    }

    if (!checkBlockSyncRoundTrip(&error)) {
        std::cerr << "BlockSyncCodec round-trip check failed: " << error << '\n';
        return 9;
    }

    if (!checkBlockSyncOffsetAcquisition(&error)) {
        std::cerr << "BlockSyncCodec offset acquisition check failed: " << error << '\n';
        return 10;
    }

    if (!checkCwCodecRoundTrip(&error)) {
        std::cerr << "CwCodec check failed: " << error << '\n';
        return 11;
    }

    std::cout << "PSK core self-test passed\n";
    std::cout << "Decoded: " << decoded << '\n';
    return 0;
}
