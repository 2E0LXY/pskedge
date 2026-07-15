# PSK Analyzer

Initial beta scaffold for a Windows and Debian PSK-family digital-mode program.

The current build is a runnable Qt UI prototype with:

- a real PSK31 Varicode encoder/decoder core;
- a first offline BPSK31 TX/RX roundtrip path covered by CI;
- right-to-left waterfall simulation;
- 16-channel active decoder model;
- separate SuperSweeper-style candidate monitor;
- click decoded text to select callsign and prepare reply;
- selected-QSO panel with callsign, frequency, audio offset, SNR, quality, and IMD;
- TX safety strip that inhibits sending until callsign, target, and text are valid;
- RX/TX waterfall markers;
- TX composer and macro buttons;
- station, CAT/PTT, radio, antenna, and macro settings dialog with persisted settings;
- signal metric placeholders for reports;
- architecture ready for Hamlib, DSP, logbook, and PSK128FEC implementation.

## Build

Requirements:

- CMake 3.21 or newer
- C++20 compiler
- Qt 6 Widgets development package

Debian example:

```sh
sudo apt install build-essential cmake qt6-base-dev
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/psk-analyzer
```

Windows example from a Qt developer shell:

```bat
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
build\Release\psk-analyzer.exe
```

## GitHub Builds

This repository includes a GitHub Actions workflow at `.github/workflows/build.yml`.

It builds:

- Ubuntu latest, as the Debian-compatible Linux build.
- Windows latest, using Visual Studio 2022.

The workflow runs on pushes, pull requests, version tags, and manual `workflow_dispatch`.

Build artifacts are uploaded as:

- `psk-analyzer-deb`, containing the Debian-compatible `.deb` package.
- `psk-analyzer-windows`, containing the Windows `.exe` plus deployed Qt runtime files.

## Status

Tag `v0.01-beta` is the first packaged UI/workflow baseline. The current `master` branch has started the real radio core with Varicode and offline BPSK31 TX/RX roundtrip coverage. Live audio DSP, Hamlib control, ADIF logging, PSK Reporter, and PSK128FEC modulation are next implementation steps.
