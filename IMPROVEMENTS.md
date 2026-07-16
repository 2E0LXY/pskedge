# Improvements Review

This is the next-pass review of receiver methods and UI layout after the first `v1.0.0-beta` scaffold.

## Highest-Value Method Improvements

### 1. Split Detection From Full Decoding

Do not allocate a full PSK decoder to every visible trace immediately.

Recommended pipeline:

```text
Wideband audio
  -> waterfall FFT
  -> candidate detector
  -> lightweight signal classifier
  -> ranked candidate list
  -> full decoder assignment, max 16
```

Benefits:

- More activity can be watched than the 16 full decoder limit.
- CPU is reserved for signals that matter.
- wideband-scanner-style monitoring becomes natural.
- Automatic decoder assignment can prioritize CQ calls, watched callsigns, strong locks, or clicked traces.

### 2. Add A Per-Signal State Machine

Each decoder channel should have explicit states:

```text
Empty -> Candidate -> Acquiring -> Locked -> Marginal -> Lost -> Released
```

The UI should show this state, and DSP decisions should use it. For example, AFC should be more aggressive in `Acquiring`, slower in `Locked`, and frozen or damped in `Marginal`.

### 3. Store Soft Confidence, Not Just Text

Decoded output should carry confidence at three levels:

- symbol confidence;
- character confidence;
- line/callsign confidence.

This lets the UI dim uncertain characters, avoid false callsign selection, and decide when a signal report is reliable.

### 4. Use Multi-Hypothesis Tracking

For weak PSK31/QPSK31 signals, run limited parallel hypotheses for:

- small frequency offsets;
- symbol timing offsets;
- sideband/orientation for QPSK;
- BPSK versus QPSK mode guess.

Keep the path with the best score using Varicode validity, lock quality, and text likelihood.

### 5. Add Real Measurement Windows

Signal reports need a defined rolling measurement window:

- short window: 1-2 seconds for meters;
- report window: 5-15 seconds for sent reports;
- long window: whole QSO/session summary.

This avoids reports jumping around while the operator is replying.

### 6. Build A Fixture-Driven DSP Test Harness Early

Before real-time audio work gets complicated, create generated WAV fixtures:

- clean BPSK31;
- clean QPSK31;
- AWGN at known SNR;
- frequency offset;
- timing offset;
- adjacent PSK signals;
- QSB/fading;
- impulse noise.

Every decoder improvement should be measured against these fixtures.

### 7. PSK128FEC Should Be Simulation-First

Do not lock the final waveform yet. Simulate these variants:

- single-carrier PSK with stronger FEC;
- pilot-assisted coherent PSK;
- 2-4 narrow PSK subcarriers inside 128 Hz;
- short versus long interleaving;
- convolutional code versus short LDPC.

Pick the first public `PSK128FEC` format only after measured performance curves exist.

## Highest-Value UI Layout Improvements

### 1. Make The Screen Task-Oriented

The main screen should be arranged around the operator's real loop:

```text
Find signal -> Identify station -> Reply -> Monitor TX -> Log QSO
```

Recommended persistent zones:

- top: radio safety and frequency;
- left: waterfall and signal selection;
- right: active decoders and wideband-scanner candidates;
- bottom: QSO/reply/log workflow.

### 2. Separate Active Decoders From Sweeper Candidates

The current table should become two related views:

- `Active Decoders`: up to 16 full decoders with lock, SNR, text, and reply/log controls.
- `Band Sweeper`: many lightweight candidates with CQ/callsign snippets and one-click assign.

This avoids overloading one table with two jobs.

### 3. Add A Selected-QSO Panel

When a decoded line is clicked, the app should populate a selected-QSO panel:

- callsign;
- name/QTH/locator if known;
- mode;
- RF frequency;
- audio offset;
- SNR/quality/IMD;
- sent report;
- received report;
- log state.

This panel should sit directly above or beside the TX composer so the user sees exactly who the reply is going to.

### 4. Make TX Safety Visually Dominant

Add a clear TX strip:

- reply target;
- TX mode;
- TX audio offset;
- RF TX frequency;
- PTT method;
- transmit inhibit state;
- split TX/RX warning.

The `Send` button should be disabled until:

- user callsign is configured;
- target/mode/frequency are valid, unless CQ/manual mode;
- audio output is configured;
- PTT mode is valid or manual transmit is selected.

### 5. Move Setup Out Of The Operating Flow

Setup should be a proper settings dialog with searchable sections. The operating screen should only show active profile selectors:

- station profile;
- radio profile;
- antenna profile;
- macro set;
- mode preset.

### 6. Add Mode And Decoder Presets

Operators should not tune dozens of DSP settings during a QSO. Provide presets:

- `Normal`;
- `Weak`;
- `Crowded`;
- `Contest/Fast`;
- `Deep/FEC`.

Advanced settings can exist, but the main UI should expose presets first.

### 7. Improve Text Legibility

Decoded PSK text is dense. Use:

- monospace font for received/transmitted text;
- color only for meaning, not decoration;
- dim uncertain characters;
- highlight callsigns, `CQ`, `DE`, and watched terms;
- avoid wrapping important fields like callsign, SNR, mode, and audio frequency.

### 8. Add Keyboard Workflow

The app needs keyboard-first operation:

- `Enter` or configured key to send line/macro;
- `Esc` abort TX or clear current selection depending on TX state;
- function keys for macros;
- arrow keys to move between active decoders;
- shortcut to focus TX composer;
- shortcut to log QSO.

## Current Scaffold Gaps To Fix In Code

- `QTableWidget` rows only store displayed text; replace this with a real model that stores full `DecodeLine` data.
- Add active decoder slot objects instead of appending all text to one table.
- Add selected-QSO state separate from selected table row.
- Add a bottom workflow area with tabs or panels: `Reply`, `Log`, `Signal`, `Macros`.
- Add a right-side split between `Active Decoders` and `Sweeper`.
- Add TX/RX marker state in `WaterfallWidget`.
- Add settings persistence with `QSettings`.
- Add macro editor instead of hard-coded macro buttons.
- Add disabled/guarded send behavior until station and audio/PTT settings are valid.
- Add a service boundary for CAT, audio, decoder, logbook, and spotting so UI code does not own application logic.

## Recommended Next Implementation Order

1. Replace the decode table with a `QAbstractTableModel` backed by decoder slot data.
2. Add selected-QSO panel and move reply target details out of the plain label.
3. Add TX safety strip and disable `Send` until basic settings are valid.
4. Add `QSettings` persistence for station/CAT/radio/antenna setup.
5. Add macro editor and load macro buttons from settings.
6. Add waterfall RX/TX markers.
7. Add a sweeper candidate panel separate from active decoders.
8. Add ADIF log record model.
9. Add Hamlib backend interface stub.
10. Add offline DSP fixture generator and first BPSK31 Varicode unit tests.
