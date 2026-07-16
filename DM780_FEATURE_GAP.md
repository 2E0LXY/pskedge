# DM-780 feature gap analysis

Source: https://www.hamradiodeluxe.com/features/dm780/ (fetched 2026-07-16).
This is the reference used to build the mode dropdown in the top bar - that
dropdown is a **target list of modes to implement**, not a claim that they
already work. Only BPSK31 is real today; PSK128FEC is in development
(core DSP exists, not yet wired into the UI/TX-RX pipeline). Everything
else in the dropdown is a placeholder for future work, same convention as
the modes listed here.

## What DM-780 advertises, and where we stand

| DM-780 feature | Status here | Notes |
|---|---|---|
| Wide range of digital modes (Contestia, DominoEX, Hell, MFSK, MT-63, Olivia, PSK/QPSK, RTTY, RTTYM, SSTV, THOR, THROB, CW) | **Only BPSK31 real** | See "Mode implementation priority" below |
| Waterfall/spectrum display | **Have it** | Real FFT over live audio (fixed 2026-07-16 - was previously simulated noise) |
| SuperSweeper (up to 40 simultaneous signals, sweeps passband, decodes CW/RTTY/PSK) | **Missing entirely** | No multi-signal wideband scanner exists. `DecodedLinesWidget` shows only the single actively-tracked channel, not a passband sweep. This was flagged as "not yet implemented" when it was still a table (see git history) and remains unbuilt after the UI rework. |
| PTT via COM port, VOX, TNC, or rig control | **CAT only** | rigctld + OmniRig implemented (2026-07-16); no COM/VOX/TNC PTT path |
| Direct HRD Logbook integration (auto QSO logging) | **No logbook at all** | There is no QSO log, ADIF export/import, or contact recording anywhere in this app. This is DM-780's headline feature and the biggest gap. |
| Direct HRD Rig Control integration | **Have it (basic)** | rigctld/OmniRig frequency read + set, PTT. No mode-sync, no split, no rig-driven waterfall centring |
| Macro support for contest exchanges | **Have it** | `MacroEngine` exists |
| PSK Reporter integration (real-time spotting) | **Missing** | No spotting network integration of any kind |
| CW decode (Morse-to-text) | **Missing** | Not started |
| CW transmit via WinKeyer | **Missing** | Not started |
| Keyboard CW (soundcard PTT, no hardware keyer) | **Missing** | Not started |

## Mode implementation priority

Ordered by (a) how different the DSP is from what's already built and (b)
real-world usage, not by the order modes appear in the dropdown:

1. **PSK128FEC** - already in progress (`ConvCode`, `Crc16`, `BlockSyncCodec`
   exist). Finish wiring it into TX/RX and the UI before starting new modes.
2. **QPSK31/QPSK63** - closest to existing `Bpsk31Codec`: same symbol rate
   family, adds a second bit/symbol via quadrature. Smallest real jump from
   what's already built and validated.
3. **RTTY (FSK/AFSK)** - very different DSP (frequency-shift, not
   phase-shift; Baudot, not Varicode) but high real-world usage
   (contesting) and no coding-theory-heavy tuning like BPSK31's Costas/
   Gardner work needed - a self-contained, bounded piece of work.
4. **MFSK/DominoEX/THOR** - incremental FSK-family modes once RTTY's
   frequency-domain demodulation groundwork exists.
5. **Contestia/Olivia/RTTYM** - MFSK-derived with FEC; natural follow-on
   once both the FSK path and a general block-FEC framework
   (`ConvCode`/`BlockSyncCodec`) exist.
6. **Hell/THROB/SSTV/CW** - each a genuinely distinct signal
   representation (Hell is a fax-like pixel mode, SSTV is image
   transmission, CW is Morse timing, not a symbol-rate digital mode) -
   treat as separate, later-phase projects, not a continuation of the
   PSK/FSK digital-mode work above.

## Architectural prerequisites that block several DM-780 features at once

- **A logbook** (QSO database, ADIF import/export) is needed before "auto
  QSO logging" can exist in any form - this blocks the single biggest
  named gap regardless of which digital mode is active.
- **A real SuperSweeper** requires wideband (not single-channel) signal
  detection - a genuinely different receiver architecture from the
  single-frequency-hypothesis Costas/BlockSync demodulators built so far.
  Worth scoping as its own design task, not a small addition to the
  existing RX path.
- **PSK Reporter integration** is comparatively small (HTTP/UDP spotting
  protocol, well documented) but depends on decode confidence/callsign
  extraction being reliable enough not to spam false spots.
