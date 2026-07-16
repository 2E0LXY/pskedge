# PSKedge

A PSK31-family digital mode transceiver for HF amateur radio, built around a
real DSP core rather than a UI shell over a stub decoder. Standard BPSK31 is
fully interoperable with existing PSK31 software and hardware; PSKedge adds a
proper carrier/timing recovery receiver, a matched pulse-shaping transmitter,
real signal measurement, CAT rig control, and — in development — a
weak-signal FEC mode (PSK128FEC) that is a genuinely new, non-backward-
compatible protocol, not a modification of standard PSK31.

## What's actually different from standard PSK31 software

Every claim below is backed by a measurement or test in this repository's
history, not asserted. Where a number is given, it was measured against
this codebase, not taken from a spec sheet.

- **Coherent carrier tracking (Costas loop) + symbol-timing recovery
  (Gardner loop).** Many lightweight PSK31 implementations use a simple
  free-running local oscillator with no frequency correction, which only
  decodes cleanly when TX and RX are tuned to within a fraction of a Hz of
  each other. PSKedge's receiver actively tracks and corrects both carrier
  frequency and symbol timing during reception. Validated envelope: up to
  ±10Hz carrier offset and ±0.1% TX/RX sample-clock drift (both figures are
  well beyond typical soundcard clock error, which is usually under 0.01%).
- **Wideband multi-hypothesis acquisition.** A single carrier-tracking loop
  has a hard pull-in ceiling around ±7-8Hz for differentially-encoded BPSK
  (a consequence of the ±90°-per-symbol decision ambiguity, not a tuning
  choice). PSKedge runs several tracking hypotheses at different starting
  offsets in parallel and keeps whichever locks cleanly, extending real
  pull-in range to roughly ±12-13Hz measured.
- **Matched raised-cosine pulse shaping**, on both TX and RX. The
  transmitter shapes each symbol with a 100%-roll-off raised-cosine pulse
  (the same technique described in the ARRL/G3PLX PSK31 specification),
  keeping occupied bandwidth close to the ~60Hz spec target rather than the
  much wider sidebands an unshaped, instantaneous-phase-flip transmitter
  produces. The receiver's correlator is matched to that same pulse shape,
  which is what maximises received SNR for a known pulse (matched filter
  theorem) — measured occupied bandwidth for this transmitter: 55.9Hz at
  -30dB.
- **Real signal measurement**, not placeholder numbers. SNR and signal/
  noise-floor readings are computed from actual in-band vs out-of-band
  correlator energy on the live audio, not a hardcoded or simulated value.
- **Real-time FFT waterfall.** The spectrum display is a Hann-windowed FFT
  over the actual captured audio, not a simulated animation.
- **CAT/rig control**, via Hamlib `rigctld` (TCP) or OmniRig (Windows),
  with dual-rig support and CAT-based PTT, running on its own thread so
  rig I/O can't stall the UI.
- **PSK128FEC (in development, experimental).** A convolutional-coded
  (rate 1/2, K=7, soft-decision Viterbi decoded), CRC-checked, block-
  synchronised weak-signal mode aimed at extending usable range well below
  standard PSK31's ~-10dB SNR threshold. Measured (Monte Carlo, real
  acquisition-and-decode pipeline, not just the FEC code in isolation):
  reliable operation around -21 to -23dB SNR (2500Hz reference bandwidth) —
  roughly FT8's territory, a genuine ~11-13dB improvement over PSK31. **This
  is a separate protocol from standard PSK31 and does not interoperate with
  it** — see Compatibility below. It is not yet wired into the main
  TX/RX UI path.

## Compatibility with standard PSK31

Standard BPSK31 mode in PSKedge uses the same Varicode alphabet and the same
differential BPSK encoding at 31.25 baud as the original G3PLX PSK31
specification. A PSKedge transmission is decodable by fldigi, DM-780, MixW,
DigiPan, or any other spec-compliant PSK31 software or hardware decoder, and
PSKedge can decode transmissions from any of them in return. The raised-
cosine pulse shaping described above is not a deviation from the PSK31
spec — it is what the spec itself describes and what most real-world PSK31
transmitters already do.

One honest caveat: PSKedge's receive correlator is matched specifically to
its own transmitted pulse shape. Decoding a station using a differently-
shaped (or entirely unshaped/rectangular) transmission may see a small
reduction in matched-filter gain compared to decoding another PSKedge
station — this affects margin at the weakest signals, not basic
compatibility, and PSKedge's coherent tracking still applies regardless of
the far station's pulse shape.

PSK128FEC is explicitly not part of this compatibility story: it is a
different framing, coding, and synchronisation scheme entirely, intended as
a separate selectable mode once fully wired in, not a variant of BPSK31.

## Status and roadmap

This is still an early-stage project. See
[`DM780_FEATURE_GAP.md`](DM780_FEATURE_GAP.md) for a feature-by-feature
comparison against Ham Radio Deluxe's DM-780 (the long-term reference point
for where this project is heading) and a prioritised list of what's missing —
notably, there is no QSO logbook yet and no wideband multi-signal scanner.
See [`PSK31_RESEARCH.md`](PSK31_RESEARCH.md) for the PSK128FEC design
research and rationale, including the Shannon-capacity analysis behind why
"weak-signal" and "keyboard-speed free text" are fundamentally in tension at
very low SNR.

The mode selector in the top bar lists the modes DM-780 supports as a
development roadmap — selecting anything other than BPSK31 there does not
currently change what PSKedge can send or receive.

## Build

Requirements:

- CMake 3.21 or newer
- C++20 compiler
- Qt 6 (Widgets, Multimedia, Network) development packages

Debian example:

```sh
sudo apt install build-essential cmake qt6-base-dev qt6-multimedia-dev
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/pskedge
```

Windows example, from a Qt developer shell:

```bat
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
build\Release\pskedge.exe
```

## GitHub Builds

This repository includes a GitHub Actions workflow at
`.github/workflows/build.yml`, building:

- Ubuntu latest, as the Debian-compatible Linux build.
- Windows latest, using Visual Studio 2022.

The workflow runs on pushes, pull requests, version tags, and manual
`workflow_dispatch`. Build artifacts are uploaded as:

- `pskedge-deb`, containing the Debian-compatible `.deb` package.
- `pskedge-windows`, containing the Windows `.exe` plus deployed Qt runtime
  files.
