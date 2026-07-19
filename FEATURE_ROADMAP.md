# Feature roadmap

Notes on where this project stands versus mature, all-in-one ham radio
digital-mode software generally, and what's planned next. This isn't a
point-by-point comparison against any single competing product - it's a
working list of the capabilities that kind of software typically has, used
to prioritise what to build.

## Mode list: curated, not a copy of any one program's full list

Some established multi-mode suites list 80+ mode/bandwidth variants across
a dozen-plus mode families. The dropdown here was deliberately trimmed to
18 entries covering only modes with real current amateur usage, based on
research (2026-07-16) rather than mirroring any one program's full
historical list:

- **Kept**: PSK/QPSK (31/63/125), RTTY (FSK/AFSK), CW, MFSK (8/16), Olivia
  (the three standard calling-frequency configs: 8/250, 16/500, 32/1000),
  Feld Hell, and the three most common SSTV modes (Martin 1, Scottie 1,
  Robot 36).
- **Cut entirely**: Contestia, RTTYM, MT-63, THROB, DominoEX/THOR. Multiple
  independent sources describe these as having faded to niche/nostalgia
  use - one amateur radio presentation on digital modes put it plainly:
  "only a few of these modes account for most digital usage on HF." A
  ranked comparison of conversational digital modes (K8JTK) orders
  popularity as PSK > RTTY > MFSK > Olivia, with everything else a long
  tail. DominoEX/THOR specifically still get occasional enthusiast
  attention (YouTube "try this if bored of FT8" content) but nowhere near
  the activity level of what was kept.
- Also cut: the many bandwidth/tone-count sub-variants some suites list for
  Contestia/RTTYM/Olivia/SSTV (e.g. Olivia commonly appears with well over
  ten variants; kept only the 3 that are actually used as calling
  frequencies).

If real-world usage patterns shift, or a specific QSO partner needs an
unlisted variant, treat this list as revisable, not fixed - the point is
matching what's actually on the air, not a permanent ruling.

## Capability checklist and where we stand

| Capability | Status here | Notes |
|---|---|---|
| Wide range of digital modes | **Only BPSK31 real** | See "Mode implementation priority" below |
| Waterfall/spectrum display | **Have it** | Real FFT over live audio (fixed 2026-07-16 - was previously simulated noise) |
| Wideband multi-signal scanner (monitor many channels across the passband at once) | **Missing entirely** | `DecodedLinesWidget` shows only the single actively-tracked channel, not a passband sweep. Flagged as "not yet implemented" earlier in this project's history and still unbuilt. |
| PTT via COM port, VOX, TNC, or rig control | **CAT only** | rigctld + OmniRig implemented (2026-07-16); no COM/VOX/TNC PTT path |
| Automatic QSO logging | **No logbook at all** | There is no QSO log, ADIF export/import, or contact recording anywhere in this app - the single biggest gap versus mature digital-mode software. |
| Rig frequency/mode control | **Have it (basic)** | rigctld/OmniRig frequency read + set, PTT. No mode-sync, no split, no rig-driven waterfall centring |
| Macro support for contest exchanges | **Have it** | `MacroEngine` exists |
| Real-time propagation spotting network integration | **Missing** | No spotting network integration of any kind |
| CW decode (Morse-to-text) | **Missing** | Not started |
| CW transmit via hardware keyer | **Missing** | Not started |
| Keyboard CW (soundcard PTT, no hardware keyer) | **Missing** | Not started |

## Mode implementation priority

Ordered by (a) how different the DSP is from what's already built and (b)
real-world usage, not by the order modes appear in the dropdown. Only
covers the 18 modes actually in the trimmed list above:

1. **PSK128FEC** - already in progress (`ConvCode`, `Crc16`, `BlockSyncCodec`
   exist). Finish wiring it into TX/RX and the UI before starting new modes.
2. **QPSK31/QPSK63/QPSK125** - closest to existing `Bpsk31Codec`: same
   symbol rate family, adds a second bit/symbol via quadrature. Smallest
   real jump from what's already built and validated.
3. **BPSK63/BPSK125** - same modulation as BPSK31 at a different baud
   rate; mechanically simple once QPSK's quadrature handling is in place,
   since it reuses the same coherent/differential decision logic.
4. **RTTY (FSK/AFSK)** - very different DSP (frequency-shift, not
   phase-shift; Baudot, not Varicode) but the highest real-world usage
   after PSK (contesting) and no coding-theory-heavy tuning like BPSK31's
   Costas/Gardner work needed - a self-contained, bounded piece of work.
5. **MFSK8/MFSK16** - incremental FSK-family modes once RTTY's
   frequency-domain demodulation groundwork exists.
6. **Olivia (8/250, 16/500, 32/1000)** - MFSK-derived with strong FEC;
   natural follow-on once both the FSK path and a general block-FEC
   framework (`ConvCode`/`BlockSyncCodec`) exist.
7. **CW** - a genuinely distinct signal representation (Morse timing, not
   a symbol-rate digital mode) - treat as its own project, not a
   continuation of the PSK/FSK work above.
8. **Feld Hell** - fax-like pixel mode, distinct DSP again (visual/human-
   readable decoding rather than symbol decisions).
9. **SSTV (Martin 1, Scottie 1, Robot 36)** - image transmission, the
   most different from everything else in this list - lowest priority
   despite being a real, still-used mode, simply because it shares the
   least code/architecture with what already exists.

## Architectural prerequisites that block several capabilities at once

- **A logbook** (QSO database, ADIF import/export) is needed before
  automatic QSO logging can exist in any form - this blocks the single
  biggest gap regardless of which digital mode is active.
- **A real wideband scanner** requires multi-channel (not single-channel)
  signal detection - a genuinely different receiver architecture from the
  single-frequency-hypothesis Costas/BlockSync demodulators built so far.
  Worth scoping as its own design task, not a small addition to the
  existing RX path.
- **Spotting network integration** (e.g. PSK Reporter-style) is
  comparatively small (a documented HTTP/UDP protocol) but depends on
  decode confidence/callsign extraction being reliable enough not to spam
  false spots.

## Real-world BPSK31 reception status

Tested against real recordings, not just synthetic signals - five
provided during development (a Wikipedia reference sample, a BARTG
sample, two photobyte.org/similar samples, and psk31.wav; see git
history for the specific commits and URLs). Three real, separate bugs
were found and fixed this way, none of which any synthetic test had
caught:

1. **Matched filter pulse-shape mismatch** (fixed) - the correlator
   window reached a full symbol period into each neighbouring symbol,
   which was harmless for this codec's own raised-cosine-shaped
   signal but caused complete decode failure on any signal shaped
   differently (confirmed with a clean, noise-free rectangular-pulse
   test, not just the real recordings). See the `halfSpan` comment in
   `Bpsk31Codec::trackWithOffset()`.
2. **No continuity across calls** (fixed) - the demodulator re-ran its
   full acquisition search from scratch on every call, requiring a
   fresh preamble every time. A real transmission only sends its
   preamble once, so decoding fell apart as soon as it scrolled out of
   whatever window was being processed, regardless of signal quality.
   See `Bpsk31StreamDecoder`.
3. **Varicode table error for 'Q'** (fixed) - the table entry for 'Q'
   was 10 bits (`1111011101`); the actual G3PLX specification (checked
   directly against two independent primary sources: the ARRL/QEX
   article and the original PSK31 web page article, both fetched and
   diffed programmatically against the full 128-entry table, not
   spot-checked) says 9 bits (`111011101`). A pure round-trip test
   never caught this because encode and decode shared the same wrong
   table; even the project's own "golden vector" regression test had
   independently made the identical transcription error, so it had
   been protecting the bug rather than catching it. Fixed, and
   replaced the 9-character spot-check with a full 128-entry table
   check (`checkFullVaricodeTable` in `psk_core_selftest.cpp`) so this
   class of error can't recur undetected in the untested 119 entries
   again. 'Q' is common in real ham traffic (CQ, QSO, QTH, QRZ, QRP,
   QRM) and Varicode's self-synchronisation means one wrong-length
   code corrupts everything decoded after it in the same message - this
   is likely to measurably improve real-world decode quality generally,
   though it did not change the outcome on either of the two
   still-unexplained files below (their content likely doesn't happen
   to contain much text containing Q).

After all three fixes, three of the five real recordings tested decode
completely and correctly (two cleanly and continuously with zero
corruption; see git history). The other two still do not decode
cleanly:

- The Wikipedia sample fails across the entire audio passband even at
  its exact measured carrier frequency and symbol rate, with the
  cause not yet identified - ruled out so far: carrier frequency,
  symbol rate, Vorbis compression (a known-good signal survives the
  same compression round-trip perfectly), matched-filter pulse shape,
  and low SNR (this file measures ~30dB, not marginal).
- A second sample (psk31.wav, 8-bit PCM) shows the same
  unexplained-failure pattern: symbol rate confirmed correct (31.25
  baud, verified via phase-transition timing), 8-bit quantization
  ruled out directly (a known-good signal decodes correctly after the
  same quantization), DC offset checked and found minimal. Two
  genuinely unexplained real-world failures now, out of five real
  files tested this session - the other three (BARTG, and two others
  that decoded cleanly: a photobyte.org sample and a second MP3
  sample) are all understood.
- The BARTG sample shows recognisable but incomplete fragments ("RST
  599", "Dipole" recur consistently rather than being pure noise).
  Confirmed, not just measured indirectly: fixed a real reporting bug
  where `Bpsk31StreamDecoder::lockedCarrierHz()` returned a value
  frozen at initial acquisition rather than the Costas loop's actual,
  continuously-updating tracking estimate. With that fixed, the
  loop's real-time frequency estimate visibly moves from ~1547Hz to
  ~1568Hz over the recording's first 8 seconds - a genuine ~20Hz
  in-message drift, not a static offset. The loop does track it
  (doesn't diverge or get stuck), but decode quality suffers while
  actively correcting for it. Whether this is worth chasing further
  with loop-gain changes (real engineering risk against the validated
  static-offset envelope) versus accepting as a genuinely difficult
  case - this much drift within one recording may reflect real
  oscillator instability in whatever produced it - is an open
  question, not resolved either way.

Neither is claimed fixed. Both are open, with the specific diagnostic
evidence recorded here rather than left as a vague "sometimes doesn't
work".
