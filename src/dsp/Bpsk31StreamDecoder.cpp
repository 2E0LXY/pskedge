#include "Bpsk31StreamDecoder.h"
#include "PskVaricode.h"

namespace psk::dsp {

namespace {
// Narrower loop gains for steady-state tracking after lock - see the
// class comment in Bpsk31StreamDecoder.h for the gear-shifting rationale.
// Roughly half the acquisition gains: a conservative first step that
// meaningfully reduces loop noise bandwidth (better BER on a weak signal
// once locked) while still able to track real in-message drift - a real
// recording tested this session showed ~20Hz drift over 8s, and going
// too narrow would lose the ability to follow that at all. Not swept
// against a validation set; a reasoned starting point, flagged as such
// rather than presented as tuned.
Bpsk31Config narrowedTrackingConfig(Bpsk31Config config)
{
    config.costasKp *= 0.5;
    config.costasKi *= 0.5;
    config.gardnerKp *= 0.5;
    config.gardnerKi *= 0.5;
    return config;
}
} // namespace

Bpsk31StreamDecoder::Bpsk31StreamDecoder(Bpsk31Config config)
    : m_acquisitionCodec(config)
    , m_trackingCodec(narrowedTrackingConfig(config))
{
}

std::string Bpsk31StreamDecoder::pushSamples(const std::vector<double> &newSamples)
{
    m_buffer.insert(m_buffer.end(), newSamples.begin(), newSamples.end());

    if (!m_acquired) {
        tryAcquire();
        return m_decodedText;
    }

    // Already locked: continue tracking from the stored state using ONLY
    // the newly-arrived samples appended to the buffer - trackStreaming()
    // picks up exactly where it left off (state->symbolStart already
    // points past everything already decoded), so this does not
    // re-process old audio. Uses the narrower tracking-gain codec, not
    // the wide acquisition one - see the gear-shifting comment in the
    // header.
    const std::vector<int> newBits = m_trackingCodec.trackStreaming(m_buffer, m_lockedOffsetHz, m_state);
    m_allBits.insert(m_allBits.end(), newBits.begin(), newBits.end());
    m_decodedText = PskVaricode::decodeTextBits(m_allBits);
    return m_decodedText;
}

void Bpsk31StreamDecoder::tryAcquire()
{
    const double bufferSeconds = static_cast<double>(m_buffer.size()) / m_acquisitionCodec.config().sampleRate;
    const Bpsk31DemodResult result = m_acquisitionCodec.demodulateTextWithLock(m_buffer);
    if (result.hasLock) {
        m_acquired = true;
        m_lockedOffsetHz = result.lockedOffsetHz;
        // One-time re-run of the (now-known-good) accumulated buffer
        // through the streaming tracker, seeded at the winning offset,
        // starting from a fresh Bpsk31TrackState. This is deliberately
        // not an attempt to transplant demodulateTextWithLock()'s
        // internal loop state directly (it doesn't expose one - it's a
        // batch function) - re-tracking the same, already-accumulated
        // audio once at the moment of acquisition is a small, one-off
        // cost, and it leaves m_state positioned correctly at the end of
        // the current buffer, ready to continue with whatever new
        // samples arrive next. Uses the acquisition (wide) codec still,
        // matching the same gains that found this lock in the first
        // place - the switch to the narrower tracking codec happens on
        // the NEXT pushSamples() call (in the "already locked" branch
        // above), not mid-way through this one-time re-track.
        m_allBits = m_acquisitionCodec.trackStreaming(m_buffer, m_lockedOffsetHz, m_state);
        m_decodedText = PskVaricode::decodeTextBits(m_allBits);
        return;
    }

    if (bufferSeconds > kMaxUnacquiredBufferSeconds) {
        // Nothing has locked in a generous window - keep only the most
        // recent portion rather than growing the buffer (and therefore
        // the cost of every future failed acquisition attempt, which
        // re-scans the whole thing) without bound while nothing is being
        // received.
        const auto sampleRate = m_acquisitionCodec.config().sampleRate;
        const auto keepSamples = static_cast<std::size_t>(kMaxUnacquiredBufferSeconds * sampleRate * 0.5);
        if (m_buffer.size() > keepSamples) {
            m_buffer.erase(m_buffer.begin(), m_buffer.end() - static_cast<std::ptrdiff_t>(keepSamples));
        }
    }
}

bool Bpsk31StreamDecoder::isAcquired() const
{
    return m_acquired;
}

double Bpsk31StreamDecoder::lockedCarrierHz() const
{
    if (!m_acquired || !m_state.initialized) {
        return m_acquisitionCodec.config().carrierHz + m_lockedOffsetHz;
    }
    // effectiveStep is the Costas loop's current per-sample phase
    // increment (nominal carrier step plus its ongoing carrierFreqIntegral
    // correction) - converting back to Hz gives the loop's real, current
    // tracking estimate, not the value frozen at initial acquisition.
    constexpr double kTwoPi = 6.28318530717958647692;
    return m_state.effectiveStep * m_trackingCodec.config().sampleRate / kTwoPi;
}

void Bpsk31StreamDecoder::reset()
{
    m_buffer.clear();
    m_state = Bpsk31TrackState{};
    m_acquired = false;
    m_lockedOffsetHz = 0.0;
    m_allBits.clear();
    m_decodedText.clear();
}

} // namespace psk::dsp
