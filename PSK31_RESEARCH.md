# PSK31 and Weak-Signal Decoder Research

## Goal

Build a cross-platform Windows and Debian application that can monitor and decode up to 16 simultaneous PSK-family signals, show a right-to-left resizable waterfall, show decoded text to the right of the waterfall, and allow clicking decoded text to select a callsign for reply.

Compatibility is a hard requirement: every existing PSK mode we implement must transmit and receive in a way that is fully compatible with existing PSK31-family software and on-air signals. Any stronger weak-signal design must be a separate opt-in mode, not a modified interpretation of BPSK31/QPSK31.

## Baseline PSK31 Facts

- PSK31 is a 31.25 baud keyboard-to-keyboard amateur mode with about 60 Hz occupied bandwidth at -26 dB.
- BPSK31 is the common mode. It uses Varicode and differential 180-degree phase reversals, but it has no forward error correction.
- QPSK31 keeps the same symbol rate, bandwidth, and user text rate as BPSK31, but uses quaternary phase states plus a rate-1/2 convolutional code decoded with Viterbi.
- The PSK31 idle preamble is continuous zeroes, which appear as continuous phase reversals. The postamble is unmodulated carrier. These are useful for squelch, lock detection, and signal discovery.
- Varicode is self-synchronizing because character gaps are two or more zero bits, and valid characters do not contain two consecutive zeroes.

Sources:

- ARRL PSK31 specification: https://www.arrl.org/psk31-spec
- ITU amateur handbook, referencing ITU-R M.2034 for PSK31 alphabet/protocol: https://www.itu.int/dms_pub/itu-r/opb/hdb/R-HDB-52-2026-PDF-E.pdf
- fldigi source mirror and mode list: https://github.com/w1hkj/fldigi
- fldigi beginner documentation, including signal browser behavior: https://sourceforge.net/p/fldigi/wiki/beginners/
- Ham Radio Deluxe DM-780 feature page: https://www.hamradiodeluxe.com/features/dm780/
- HRD DM-780 tour and options docs: https://support.hamradiodeluxe.com/support/solutions/articles/51000052649-dm-780-tour and https://support.hamradiodeluxe.com/support/solutions/articles/51000052647-program-options-overview

## Variants To Support

Minimum useful set:

- BPSK31: required for interoperability; must match normal PSK31 Varicode, symbol timing, differential phase behavior, idle, and postamble behavior.
- QPSK31: required for built-in PSK31 error reduction; must match the standard PSK31 convolutional coding and Viterbi receive behavior.
- BPSK63 / QPSK63: common faster variants; must remain compatible with existing PSK63 software.
- BPSK125 / QPSK125: useful for strong signals and contest-style traffic; must remain compatible with existing PSK125 software.

Weak-signal extension set:

- PSK10 / PSK05 receive-only at first. Slower symbols provide more integration time and improved noise tolerance at the cost of typing speed.
- PSK63F if we want compatibility with existing FEC-bearing PSK63 software.
- A new optional experimental mode named `PSK128FEC` for genuinely below-noise or heavily damaged signal recovery beyond standard PSK31 compatibility.

## Compatibility Rules

- Do not change transmitted waveforms, Varicode mapping, idle patterns, postambles, symbol rates, phase mapping, or FEC behavior for existing modes.
- Receive-side improvements are allowed as long as they do not require nonstandard transmit behavior.
- Transmit-side options for standard modes must produce normal on-air signals that fldigi, MultiPSK, DigiPan, and other PSK-family decoders can decode.
- Experimental modes must be clearly labeled and selected explicitly.
- Experimental mode detection should not cause false mode switching when ordinary PSK31/QPSK31 signals are present.
- If RSID/TXID support is added, use existing mode IDs for standard modes and a new unique identifier for `PSK128FEC`.

## What Can And Cannot Be Fixed In Standard PSK31

Standard BPSK31 can be made more sensitive, but it cannot reliably reconstruct arbitrary destroyed bits because it has no redundancy beyond Varicode syntax and human-language structure. We can:

- improve probability of correct symbol decisions;
- recover through short fades using timing/carrier tracking memory;
- mark uncertain text instead of printing false confidence;
- use language/callsign post-processing to repair likely text;
- combine repeated text/macros when the same phrase is transmitted more than once.

We cannot honestly promise deep error correction for one-off BPSK31 text without changing the transmitted waveform or protocol.

QPSK31 can correct many symbol errors because it includes convolutional coding and Viterbi decoding. ARRL notes that QPSK31 typically helps on real HF paths with burst errors and fading, but may not help in pure weak AWGN where its 3 dB split-power penalty can dominate.

## Receiver Sensitivity Improvements

### 1. Front-End DSP

- Convert input audio to complex baseband with a numerically controlled oscillator per tracked signal.
- Use a narrow matched root-raised/cosine receive filter matched to PSK31 pulse shaping.
- Decimate each signal channel after filtering to keep CPU low for 16 decoders.
- Use high dynamic range FFT/waterfall processing separately from decode filtering so display choices do not degrade decoding.
- Add notch filters and adaptive interference suppression for adjacent carriers, hum, birdies, and strong nearby PSK traces.
- Add optional noise blanking for impulse noise before narrowband channel extraction.

### 2. Signal Detection And Tracking

- Detect candidate PSK traces from the waterfall using narrowband energy plus PSK idle/preamble phase reversal signatures.
- Maintain up to 16 independent receiver channels, each with:
  - center frequency;
  - signal strength / SNR estimate;
  - lock quality;
  - carrier phase;
  - symbol timing;
  - mode guess;
  - decoded text buffer.
- Use a PLL or Costas-style carrier tracker for coherent BPSK/QPSK demodulation.
- Use Gardner or Mueller and Muller timing recovery, with a constrained 31.25 baud timing model for faster acquisition.
- Use AFC slowly enough to follow sound-card/radio drift without chasing modulation or adjacent stations.
- Allow "diversity decoders" around a selected signal: run 3-5 slightly different frequency/timing hypotheses and keep the stream with the best confidence.

### 3. Soft Decisions Everywhere

- Do not make early hard 0/1 decisions. Keep log-likelihood or confidence values from symbol demodulation through:
  - differential phase decision;
  - Viterbi branch metrics for QPSK;
  - Varicode framing;
  - confidence display.
- For BPSK31, soft-decode Varicode by scoring likely bit paths rather than immediately splitting on hard zero-zero gaps.
- Use erasures for low-confidence symbols; display uncertain characters with subdued styling or replacement markers.

### 4. QPSK31 And FEC

- Implement the standard PSK31 QPSK convolutional code exactly for interoperability.
- Use soft-decision Viterbi, not hard-decision Viterbi.
- Try both sideband/orientation hypotheses when QPSK lock is ambiguous, then choose the path with valid Varicode and higher text likelihood.
- Keep a traceback delay large enough for robust decisions; ARRL describes practical Viterbi delay for PSK31 reception.

### 5. Burst Damage Handling

- For standard modes:
  - preserve symbol confidence through fades;
  - use Varicode validity to resynchronize quickly after corrupted gaps;
  - identify likely callsigns from partial strings using amateur callsign patterns;
  - avoid printing long noise runs as text.
- For our own extension mode:
  - add a known sync word;
  - add CRC per block;
  - add interleaving over several seconds;
  - add LDPC, convolutional, or polar FEC;
  - use puncturing/rate adaptation for speed versus robustness;
  - optionally use repeated headers and message metadata.

### 6. Below-Noise-Floor Methods

Below-noise decoding requires processing gain. PSK31 already gains from narrow bandwidth, but true "well below noise floor" performance requires one or more of:

- longer coherent integration time;
- strong sync sequences;
- strong FEC;
- interleaving against fading;
- constrained message formats;
- repeated transmission combining;
- known-message or partially known-message correlation;
- accurate time/frequency synchronization.

Modes like WSPR/FT8 achieve extreme weak-signal performance partly because they use structured messages, fixed transmit intervals, synchronization, and strong coding. Free-text interactive PSK31 cannot get the same sensitivity without sacrificing speed, adding structure, or creating a new mode.

## Experimental Mode: PSK128FEC

`PSK128FEC` is the working name for the new mode. It should be treated as a separate mode, not as a replacement for BPSK31/QPSK31.

Design goals:

- Stronger weak-signal performance than BPSK31/QPSK31.
- Free-text operation suitable for keyboard QSOs.
- Familiar PSK-family operating style.
- Same application workflow as PSK31: waterfall trace, decoded text stream, callsign click, reply.
- Clear on-air identity so operators know it is not standard PSK31.

Initial technical direction:

- Symbol rate: 31.25 baud initially, unless simulation proves a different rate is better.
- Occupied bandwidth target: roughly 128 Hz, hence the `128` in `PSK128FEC`.
- Modulation: coherent or differential PSK with pilot/sync assistance.
- Text layer: preserve Varicode-compatible input/output text handling at the application layer, but packetize it before FEC.
- Framing: fixed sync word, frame header, payload length, payload, CRC.
- FEC: soft-decision LDPC or convolutional code for the first prototype; compare against polar coding only after baseline tests exist.
- Interleaver: selectable short/medium/long interleaving depths, with the long setting optimized for fading and burst damage.
- Decoder: soft demodulator, soft FEC decoder, erasure handling, and frame confidence reporting.

Open choices for simulation:

- Whether `PSK128FEC` should use one carrier with wider PSK shaping or multiple narrow PSK subcarriers.
- Whether the best user experience is continuous streaming text or short block delivery.
- Whether Varicode should be FEC-protected directly, or whether text should be converted to UTF-8 bytes before FEC.
- Whether to provide robustness profiles such as `Normal`, `Deep`, and `Extreme`.

Recommended first prototype:

- Keep 31.25 baud.
- Use a 128 Hz nominal channel.
- Encode UTF-8 text blocks with CRC.
- Use rate-1/2 convolutional or short LDPC FEC with soft decisions.
- Interleave over 2-8 seconds.
- Require explicit mode selection for transmit.
- Run automatic detection only when confidence is high.

## Recommended Application Architecture

Language/runtime:

- C++20 core DSP library for performance and portability.
- Qt 6 UI for Windows and Debian, unless a web UI is preferred.
- CMake build system.
- PortAudio or RtAudio for sound devices.
- FFTW, KissFFT, or pocketfft for waterfall and acquisition.
- Hamlib for CAT/PTT rig control on Debian and Windows.
- Optional Windows interoperability backends: OmniRig and FLRig/rigctld network control.

Process layout:

- Audio input thread: captures sound-card or virtual audio stream.
- DSP scheduler: performs FFT/waterfall, signal detection, and per-channel demodulation.
- Decoder workers: up to 16 PSK decoder state machines.
- UI model: receives decoded events, confidence, callsign candidates, and spectrum frames.
- CAT/PTT controller: tracks radio frequency/mode, applies retune commands, and controls transmit switching.
- Signal measurement engine: estimates per-signal SNR, level, frequency offset, drift, decode confidence, and quality metrics for reports/macros.
- QSO manager: tracks selected station, exchange progress, reports, sent/received text, and log state.
- Logbook service: creates, edits, imports, exports, and forwards QSO records.
- Spotting service: sends and receives spots where supported, including PSK Reporter-style reporting.
- Alarm/watchlist service: highlights wanted calls, CQ calls, countries, prefixes, grids, and bands.
- TX path: selected callsign + macro/text entry -> Varicode/FEC encoder -> audio output/PTT.

Data flow:

```text
Audio In
  -> AGC / limiter / optional blanker
  -> waterfall FFT
  -> candidate detector
  -> 16 channel extractors
  -> carrier/timing recovery
  -> BPSK/QPSK soft demod
  -> signal metrics estimator
  -> FEC if applicable
  -> Varicode decoder
  -> callsign detector
  -> UI text panes and reply target
```

## Signal Measurement And Reports

The app must calculate signal details for the station currently transmitting to us, so those details can be sent back in the reply. Measurements should be computed per decoder channel and attached to the decoded stream/callsign.

Required measurements:

- Audio frequency offset within the receiver passband.
- RF frequency estimate: radio VFO plus audio offset.
- Mode detected/selected, for example BPSK31, QPSK31, BPSK63, PSK128FEC.
- Signal level in dB relative to the current waterfall/reference scale.
- Estimated SNR in dB.
- Noise floor estimate around the signal.
- Signal bandwidth estimate.
- Frequency drift in Hz/minute.
- Decode confidence percentage.
- Character error/erasure indication where available.
- Lock quality: unlocked, acquiring, locked, marginal.

Useful optional measurements:

- IMD estimate for PSK31 transmit quality where enough signal structure is visible.
- Phase error / constellation quality.
- Timing error.
- Adjacent-channel interference warning.
- Overload/clipping warning.
- Fading/QSB estimate.

Report calculation notes:

- SNR should be measured over a defined bandwidth and displayed with that context, for example `SNR 12 dB / 60 Hz`.
- Noise floor should be estimated from bins near but outside the signal passband, excluding strong adjacent traces.
- Signal level should be averaged over a short window and also expose peak/recent values.
- Use a rolling measurement window, for example 5-15 seconds, so reports do not jump around too quickly.
- Mark measurements as approximate when lock is weak or noise/interference makes the estimate unreliable.
- Keep raw internal metrics separate from user-facing rounded reports.

Suggested user-facing signal report fields:

```text
RST: 599
SNR: +12 dB
LEVEL: -64 dB
NOISE: -76 dB
AUDIO: 1420 Hz
RF: 14.071420 MHz
MODE: BPSK31
DRIFT: +1.5 Hz/min
QUALITY: 92%
IMD: -24 dB
```

For amateur-style quick replies, support both traditional and measured reports:

```text
UR RST 599 SNR +12DB AUDIO 1420HZ IMD -24DB
```

If a metric is unavailable, the macro engine should omit it or show a configurable fallback rather than inserting bad data.

## CAT/PTT Radio Control

CAT control is required for the application. Use Hamlib as the primary implementation on both Debian and Windows. Hamlib provides a consistent API for amateur-radio rig control, while `rigctl` and `rigctld` provide command-line and TCP daemon interfaces. The `rigctld` model is useful because multiple programs can talk to a radio through a local TCP service.

Sources:

- Hamlib project: https://hamlib.github.io/
- Hamlib `rigctld` manual: https://hamlib.sourceforge.net/html/rigctld.1.html
- Debian `rigctl` manual: https://manpages.debian.org/testing/libhamlib-utils/rigctl.1.en.html
- OmniRig overview: https://ve3nea.github.io/HamCockpit/users_guide/omnirig.html

Required CAT features:

- Read VFO frequency.
- Set VFO frequency.
- Read radio mode and passband.
- Set radio mode to USB/data mode where supported.
- PTT on/off.
- Poll radio state at a configurable interval.
- Detect disconnects and reconnect cleanly.
- Support serial ports, USB CAT devices, and network CAT endpoints.
- Keep waterfall/audio frequency and RF frequency synchronized.

Backend priority:

1. Native Hamlib library backend.
2. Hamlib `rigctld` TCP backend for users already running a daemon.
3. OmniRig backend on Windows for users whose station is already centered around OmniRig.
4. FLRig XML-RPC or `rigctld` bridge as an optional compatibility path.

Debian implementation notes:

- Package dependency should include Hamlib runtime utilities where available.
- Support direct Hamlib model/serial configuration in the app.
- Support connecting to `localhost:4532` or another `rigctld` host/port.
- Provide a "Test CAT" button that reads frequency/mode and toggles PTT only after explicit confirmation.

Windows implementation notes:

- Hamlib should still be supported as the primary backend.
- Bundle or document the required Hamlib DLL/runtime approach during packaging.
- Add OmniRig support as a secondary backend because many Windows ham applications share rigs through OmniRig.
- Serial port selection must handle normal COM ports and virtual USB CAT ports.
- PTT options should include CAT PTT, RTS, DTR, and no-PTT/manual.

UI requirements for CAT:

- Show current RF frequency in the large top-center VFO readout.
- Show selected audio offset under the waterfall cursor.
- Show transmit RF frequency as `RF VFO + audio offset`.
- Provide lock controls to prevent accidental retuning.
- Provide clear status: `CAT Connected`, `CAT Offline`, `PTT Active`, `TX Inhibit`.
- During transmit, disable unsafe retune actions unless the user explicitly unlocks them.

## Setup And Station Configuration

The app needs a dedicated setup area. It should be reachable from the main operating screen but not consume operating space during normal use.

Setup sections:

- Station details.
- CAT/PTT control.
- Audio devices.
- Radio equipment.
- Antenna details.
- Macro buttons.
- Mode defaults.
- Logbook and QSO workflow.
- Spotting/reporting.
- Waterfall and wideband-scanner-style monitor.
- Alarms/watchlists.
- Favorites and band presets.
- Storage/backup.

### Station Details

Store user/operator details for logging, replies, and macro expansion:

- Callsign.
- Operator name.
- QTH/location.
- Locator/grid square, for example Maidenhead locator. This covers the requested `licat8` field if that means locator.
- Country.
- Email/website, optional.
- Default signal report.
- Default sign-off text.

The configured callsign is required before transmit is enabled. Receive-only operation may run without it.

### CAT/PTT Setup

CAT setup must be user-configurable rather than hard-coded.

Fields:

- CAT backend: Hamlib native, Hamlib `rigctld`, OmniRig, FLRig, manual/no CAT.
- Radio model.
- Serial/USB port.
- Baud rate.
- Data bits, stop bits, parity.
- RTS/DTR behavior.
- CI-V address where applicable.
- Network host/port for `rigctld` or FLRig.
- VFO selection: A, B, current/default.
- PTT method: CAT, RTS, DTR, VOX, manual.
- Poll interval.
- Startup behavior: connect automatically or manual connect.

Controls:

- `Test Connection`: read frequency and mode.
- `Test PTT`: guarded action requiring confirmation.
- `Read From Radio`: pull current frequency/mode into the UI.
- `Apply To Radio`: set selected mode/frequency.
- `Save Profile`: store the setup under a named station/radio profile.

### Radio Equipment

Store radio profile details for display, macros, and logging:

- Radio manufacturer.
- Radio model.
- Interface type.
- Maximum power.
- Normal PSK power.
- Data mode preference, for example USB-D, DATA-U, or USB.
- Notes.

### Antenna Details

Store one or more antenna profiles:

- Antenna name.
- Antenna type.
- Bands supported.
- Height.
- Direction/azimuth if fixed or beam.
- Tuner used.
- Notes.

The active antenna profile should be selectable from the operating screen so macros and logs use the correct details.

### Macro Buttons

Macro buttons should be fully user-configurable and able to insert station/radio fields into the TX composer.

Macro behavior:

- Insert text at cursor.
- Replace current composer text.
- Send immediately after insert, optional and disabled by default.
- Support per-mode macro sets.
- Support import/export of macro profiles.
- Support radio-control actions before or after text insertion, for example set filter width, set data mode, enable/disable noise reduction, or set power.
- Support file insertion for dynamic text such as weather, station notes, or contest messages.
- Support conditional fields, so unavailable signal metrics do not produce ugly text.

Suggested default macros:

- `CQ`: `CQ CQ CQ DE {MYCALL} {MYCALL} K`
- `Answer`: `{THEIRCALL} DE {MYCALL} `
- `Report`: `{THEIRCALL} DE {MYCALL} UR {RST} {RST} IN {QTH} {LOCATOR}`
- `Name/QTH`: `NAME {NAME} QTH {QTH} LOC {LOCATOR}`
- `Rig`: `RIG {RADIO_MODEL} PWR {PSK_POWER}W`
- `Antenna`: `ANT {ANTENNA_NAME} {ANTENNA_TYPE}`
- `73`: `{THEIRCALL} DE {MYCALL} 73 SK`

Supported macro fields:

```text
{MYCALL}
{THEIRCALL}
{NAME}
{QTH}
{LOCATOR}
{COUNTRY}
{RST}
{RADIO_MAKE}
{RADIO_MODEL}
{PSK_POWER}
{ANTENNA_NAME}
{ANTENNA_TYPE}
{BAND}
{FREQUENCY}
{AUDIO_FREQ}
{RF_FREQ}
{MODE}
{SNR}
{SIGNAL_LEVEL}
{NOISE_FLOOR}
{SIGNAL_BANDWIDTH}
{DRIFT}
{QUALITY}
{LOCK_QUALITY}
{IMD}
{PHASE_ERROR}
{TIMING_ERROR}
{TIME_UTC}
{DATE_UTC}
```

Macro validation:

- Highlight unknown fields.
- Warn if `{MYCALL}` is missing from transmit macros.
- Warn before immediate-send macros are enabled.
- Preview expanded macro text using the current selected callsign and station profile.
- Preview measured signal fields from the currently selected decoder channel.
- Omit unavailable measurement fields using a configurable rule: blank, `N/A`, or remove the whole phrase.

## Integrated Feature Targets

The application should feel like a polished, integrated ham radio digital-mode suite rather than a bare decoder - joining digital modes, rig control, macros, logging, panoramic monitoring, and station workflow. We should implement those categories from the start instead of treating them as add-ons.

Feature areas to match or exceed:

- Soundcard digital-mode receive/transmit.
- Waterfall-centered tuning.
- Multi-signal panoramic monitor (wideband scanner) covering many channels across the passband at once.
- Macro manager with unlimited or user-defined macro sets.
- Mode-specific macro sets, including PSK, QPSK, PSK128FEC, and contest sets.
- Radio-control macros.
- Station details available as macro tags.
- QSO/logbook integration.
- Callsign lookup hooks.
- PSK Reporter or equivalent spot/report upload.
- RSID/TXID-style mode identification where compatible.
- Waterfall customization.
- Favorite frequencies and mode presets.
- Alarms/watchlists for wanted callsigns, CQ calls, prefixes, countries, grids, or text.
- Audio device setup and calibration.
- PTT setup.
- Storage, backup, and profile import/export.

## QSO Workflow And Logbook

The current brief needs a QSO-centric layer, not just decode panes. Clicking a decoded line should start or update a QSO session.

QSO session fields:

- Other station callsign.
- User callsign.
- Start/end time UTC.
- RF frequency.
- Audio offset.
- Band.
- Mode.
- Sent report.
- Received report.
- Name.
- QTH.
- Locator/grid.
- Country/prefix.
- Radio profile.
- Antenna profile.
- Power.
- Free notes.
- Full RX/TX transcript link or embedded transcript.

Required logbook features:

- Add QSO from active decoder.
- Auto-fill frequency/mode from CAT.
- Auto-fill callsign from clicked text.
- Auto-fill signal metrics from selected decoder.
- Edit before saving.
- Save to internal logbook.
- Export ADIF.
- Import ADIF.
- Duplicate detection by callsign, band, mode, and date/time.
- QSO state indicator: `New`, `In Progress`, `Ready To Log`, `Logged`.

Nice-to-have integrations:

- QRZ/HamQTH/Club Log lookup hooks if the user configures credentials.
- LoTW/eQSL/Club Log export or upload later.
- Contest exchange mode later, with serial numbers and fixed exchange fields.

## Panoramic Multi-Signal Monitor

Add a panoramic monitor view (a wideband scanner showing many candidate signals across the passband at once), focused initially on PSK-family modes.

Requirements:

- Show many decoded candidates across the passband, not only the 16 active full decoders.
- Use lightweight scanning decoders or pre-decoders to identify CQ, DE, callsigns, and likely modes.
- Highlight CQ calls.
- Highlight watchlist calls/prefixes/grids.
- Allow one-click assignment of a candidate to a full decoder slot.
- Show audio frequency, mode guess, callsign, SNR, confidence, and last decoded fragment.
- Support alarms when watched text/callsigns appear.

This is separate from the 16 full decoder streams: the sweeper can detect more candidates, while full decoders are reserved for signals the user wants to follow continuously.

## Mode Identification

The application should support compatible mode IDs where possible.

Requirements:

- Receive RSID/TXID if implemented for standard modes.
- Transmit standard IDs only where compatible and user-enabled.
- Assign a unique ID for `PSK128FEC` before public use.
- Never label `PSK128FEC` as BPSK31/QPSK31.
- Show mode ID detection confidence in the UI.

Also consider a visual ID/text ID option for human readability, but keep it optional because it consumes airtime.

## RX/TX Frequency Handling

Mature integrated digital-mode software commonly supports waterfall RX/TX markers and split audio operation. We need equivalent behavior.

Requirements:

- Separate RX and TX audio markers.
- Lock TX to RX by default.
- Allow split audio TX/RX when explicitly unlocked.
- Show resulting RF TX frequency from CAT VFO plus audio TX offset.
- Provide a one-click reset to return TX marker to RX marker.
- Prevent accidental transmit on the wrong trace with visible TX marker and confirmation when split is active.

## Favorites, Presets, And Profiles

Add station operating presets:

- Band/frequency favorites.
- Mode favorites.
- Radio filter presets.
- Waterfall palette/preset.
- Decoder sensitivity preset.
- Macro set selection per mode.
- Antenna selection per band.
- Power level per mode.

Profiles:

- Home station.
- Portable station.
- Receive-only station.
- Radio-specific profiles.
- Contest/profile variants later.

## Alerts And Operator Assistance

The program should help the operator notice useful activity without making bad automatic decisions.

Alerts:

- CQ detected.
- Callsign heard.
- Watched callsign heard.
- Watched prefix/country/grid heard.
- Reply target callsign appears again.
- Signal quality changed sharply.
- CAT disconnected.
- Audio clipping/overload.
- TX attempted without callsign configured.
- TX attempted while CAT/PTT offline, depending on settings.

Operator assistance:

- Highlight likely callsigns inside decoded text.
- Show confidence on callsign extraction.
- Keep a per-station recent history in the session.
- Offer "reply", "log", "ignore", and "watch" actions from decoded lines.

## UI Requirements

- Main screen is the operating screen, not a landing page.
- Visual direction follows the supplied mockup: dark SDR-style instrument panel, cyan/blue text, compact controls, large tuned-frequency readout, waterfall-heavy layout, and dense decode monitoring.
- Top header:
  - frequency/profile selector on the left;
  - large primary VFO/frequency readout in the center;
  - mode, bandwidth, SNR, RX/TX, and tuning controls on the right.
- Left/main area: resizable waterfall that scrolls right to left.
- Waterfall should use a high-contrast radio palette: deep blue noise floor, cyan/green/yellow signal energy, red/white for strong peaks.
- Waterfall should include:
  - frequency ruler;
  - 16 decoder markers/lanes where applicable;
  - click-to-tune behavior;
  - drag-to-select bandwidth or signal region;
  - right-to-left time flow.
- Right side: decoded text streams, one dense table/panel for active signals.
- Each decoded stream shows:
  - callsign candidate;
  - audio frequency;
  - mode;
  - SNR/quality;
  - lock state;
  - decoded text;
  - uncertainty styling.
- Clicking decoded text selects the best callsign in that stream as the reply target.
- Clicking a decoded line also prepares the reply composer automatically:
  - extract the other station callsign from the clicked line/stream;
  - set that callsign as the active reply target;
  - insert a reply prefix at the start of the outgoing message using `THEIRCALL DE MYCALL`;
  - place the cursor after the prefix so the user can immediately type the reply.
- Clicking a waterfall trace opens or retunes one of the 16 decoder slots.
- Provide manual lock, unlock, mute, and "reply here" controls per stream.
- Provide macro buttons for CQ, answer, signal report, name/QTH, 73, and custom text.
- Bottom strip:
  - per-channel controls;
  - small spectrum/graph panel;
  - RX quality meters;
  - selected decoder details;
  - reply composer for user-typed outgoing text.
- Reply composer:
  - always visible in the main operating screen;
  - multi-line text entry;
  - shows active reply target callsign;
  - shows user's configured callsign;
  - can receive text from configurable macro buttons;
  - supports `Send`, `Abort`, `Clear`, and macro insert buttons;
  - supports manual editing of the auto-inserted prefix before transmit.
- Avoid oversized decorative panels. This is an operating tool, so density and readability matter more than marketing-style whitespace.

## UI Layout Draft

```text
+--------------------------------------------------------------------------------+
| Band/Frequency controls       MAIN RF VFO / Audio Freq        Mode/BW/SNR/PTT   |
+-------------------------------------------+------------------------------------+
| RF WATERFALL                               | REAL-TIME DECODE                  |
| right-to-left scrolling                    | Ch  Call   Freq  Mode  Text       |
| frequency ruler + decoder markers          | 01  K7ABC  1407  B31   CQ DX ...  |
| click/drag tune                            | 02  W1AW   1515  Q31   DE W1AW... |
|                                            | ... up to 16 active streams       |
+----------------------+--------------------+------------------------------------+
| Decoder controls     | Spectrum/graphs    | Reply target + macros + TX composer|
+----------------------+--------------------+------------------------------------+
```

Reply prefix behavior:

```text
Clicked decoded line includes: CQ DX DE K7ABC
Configured user callsign:      W1AW
Composer becomes:              K7ABC DE W1AW 
```

If a clicked line contains multiple callsigns, prefer the station after `DE`. If no reliable callsign is found, keep the composer unchanged and ask the user to select or type the callsign manually.

## Callsign Extraction

Use layered extraction:

- regex for common amateur callsigns;
- confidence score based on repeated occurrence;
- context keywords: CQ, DE, K, KN, QRZ;
- country/prefix validity table;
- user override by selecting text manually.

Click behavior:

- If one callsign is detected in the clicked text span, select it.
- If multiple are detected, prefer the station after "DE" or before "K/KN".
- If ambiguous, show a small chooser.

## Development Phases

### Phase 1: Research Prototype

- Build offline decoder test harness.
- Generate clean BPSK31/QPSK31 WAV fixtures from known text.
- Decode fixtures under AWGN, frequency offset, timing offset, fading, and adjacent carrier interference.
- Produce BER/CER curves for baseline hard decoder versus improved soft/matched-filter decoder.

### Phase 2: Interoperable Decoder

- Implement BPSK31 and QPSK31 receive.
- Implement 16-channel receive scheduler.
- Implement waterfall and click-to-tune.
- Implement decoded text panes and callsign detection.

### Phase 3: Transmit And Reply Workflow

- Implement BPSK31 transmit.
- Add QPSK31 transmit after receive is stable.
- Add audio output device selection and PTT/CAT integration.
- Add macros and selected-callsign reply insertion.

### Phase 4: Weak-Signal Enhancements

- Add diversity demod hypotheses.
- Add adaptive notch/noise blanking.
- Add soft Varicode and confidence rendering.
- Add repeated-message combining.
- Add PSK05/PSK10 receive.

### Phase 5: New Deep Mode

- Define `PSK128FEC` as a backward-incompatible extension mode only after the standard decoder works.
- Target: free-text but slower than PSK31, with block sync, CRC, FEC, interleaving, and optional structured QSO fields.
- Evaluate LDPC or convolutional coding with soft decisions.
- Make it explicitly opt-in so normal PSK31 users are not affected.

## Test Plan

- Unit tests for Varicode encode/decode.
- Unit tests for BPSK/QPSK symbol mapping and differential phase logic.
- Golden WAV decode tests for BPSK31, QPSK31, BPSK63, QPSK63.
- Monte Carlo SNR tests to quantify sensitivity gains.
- HF impairment tests:
  - slow fading;
  - selective fading;
  - impulse noise;
  - adjacent PSK carriers;
  - frequency drift;
  - sample-rate error.
- UI tests for:
  - 16 active streams;
  - waterfall resizing;
  - right-to-left scroll direction;
  - click-to-select callsign;
  - click-to-tune trace.

## Key Recommendation

Build the first release as an excellent interoperable PSK31/QPSK31 multi-decoder. Put aggressive weak-signal DSP behind normal PSK31 reception, but be explicit that true below-noise damaged-signal correction requires either QPSK31 FEC or a new opt-in mode with stronger coding and longer integration.

## Current Improvement Backlog

See `IMPROVEMENTS.md` for the current next-pass review of receiver methods and UI layout. The highest-priority changes are:

- split wideband signal detection from the 16 full decoder slots;
- add explicit per-signal decoder states;
- preserve soft confidence through symbol, character, line, and callsign decisions;
- separate `Active Decoders` from a wideband-scanner-style candidate monitor;
- add a selected-QSO panel directly tied to the reply composer;
- make TX safety, target callsign, TX frequency, and PTT state visually dominant;
- replace the prototype table with real Qt models backed by full decoder/QSO data;
- add settings persistence, macro editing, waterfall RX/TX markers, ADIF logging, and Hamlib service boundaries.
