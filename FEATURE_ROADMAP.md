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
