# WTAnalyzer

A JUCE-based VST3/AU analyzer plugin for measuring how audio effects respond
to known test signals. WTAnalyzer sits downstream of the effect under test
and compares the pre-effect and post-effect signals to characterize what the
device did.

Its companion is **WTGenerator** - a purpose-built test-signal generator that
emits the clean, reproducible reference signal WTAnalyzer measures against.
Together they form a self-contained two-plugin DSP test bench that lives in
any DAW supporting sidechain routing, alongside the effect being developed or
characterized. See [COORDINATION.md](COORDINATION.md) for the interface
contract between the two plugins.

WTAnalyzer and WTGenerator are inspired by Shane Dunne's WTSynth - the
wavetable synth that sparked the idea - but they are a standalone pair and
are not designed to be used with WTSynth.

## Status

Pre-alpha. Working analysis modes:

- **Frequency Response** - per-bin transfer function `post_dB - pre_dB`
  drawn as a green trace on top of the spectrum. Bins where pre is too
  quiet are flagged as no-measurement and break the path.
- **THD Measurement** - differential THD percent (added energy at each
  harmonic, `post^2 - pre^2`), big readout plus a 16-harmonic bar chart.
  Pre / Post / Diff view toggle. Hold (peak-keep across frames) and
  Freeze capture controls.
- **Aliasing Detection** - differential alias residue (off-grid added
  energy from the effect) peak-held across a sweep. Composite view shows
  the input signal in the pre-effect colour and the added alias residue
  in the analysis colour; Pre and Post views are channel-only sanity
  checks. Hold and Clear apply to the analysis trace.
- **IMD Measurement** - intermodulation products from a two-tone input
  (orders 2 through 4, 12 products total). Differential IMD percent plus
  a per-product bar chart with By Order and By Hz layout toggle.
  Pre / Post / Diff view toggle. Hold and Freeze.
- **Direct Impulse IR** - time-domain capture of the device's impulse
  response by feeding actual discrete impulses. Threshold-triggered,
  multi-capture averaging, user-settable window length up to 120 seconds
  (long-tail reverbs). The first time-domain analysis in the suite.
  Export... button saves the averaged IR as a 32-bit float stereo WAV
  for drop-in use in any convolution reverb.
- **Farina IR** - time-domain impulse response via log-sweep
  deconvolution. Sister to Direct Impulse IR - same display, different
  acquisition. User configures f0 / f1 / sweep / tail; clicks Capture;
  the algorithm auto-triggers on the sweep onset, records, deconvolves
  against the mathematically-generated inverse-sweep filter, and
  displays the resulting IR. Buffer allocation and FFT setup are lazy
  (first Capture click pays the cost, subsequent captures with the
  same params are instant). Same Export... button as Direct Impulse IR
  - the deconvolved IR saves as a 32-bit float stereo WAV ready for
  any convolution reverb.
- **CSD (Cumulative Spectral Decay)** - a sub-view of both IR modes,
  reached by a Waveform / CSD Heatmap / CSD 3D selector above the
  plot. The CSD shows how each frequency of the captured IR decays
  over time: a resonance lingers as a ridge while the rest drops
  away. A short window is slid along the IR and FFT'd at each
  position to build a time/frequency/level grid, drawn either as a
  log-frequency-vs-time heatmap or as the classic 3D receding
  waterfall of spectra. The L/R toggle picks the channel.
- **Stereo Image** - the home for stereo-specific analysis, with a
  Divergence / Correlation / Goniometer view selector. **Divergence**
  (shipped) plots the per-frequency device-added stereo divergence on
  a bipolar centred-on-zero plot: green centre line = the device left
  the stereo image untouched, the trace lifts up (lime) where the
  device acted on the right channel and down (mint) where it acted on
  the left. The plotted value is `|FR_R - FR_L|` signed toward the
  channel modified more, so a stereo-transparent device reads flat
  regardless of how stereo the input was. **Correlation** (shipped)
  plots the post signal's per-frequency L/R phase correlation from
  their cross-spectrum, +1 (in phase) through 0 (decorrelated) to -1
  (anti-phase / mono-fold cancellation), plus a Broadband corner
  readout aggregating the whole band into one energy-weighted figure.
  **Goniometer** (shipped) is a time-domain L-vs-R XY scope
  (Lissajous): M axis vertical, S axis horizontal, L and R on the
  45-degree diagonals. A header toggle picks Pre / Post (overlaid
  input and output clouds, so the device's effect is the visible
  delta) or Difference (post minus aligned pre - the stereo image
  the device added).
- **Parameter Sweep** - Plugin Doctor style parameter testing: a
  headline metric (THD% or IMD%) plotted against a swept parameter,
  with a Line / Heatmap view selector. The user automates WTAnalyzer's
  Sweep Position parameter from the DAW and routes the same automation
  to whatever they want to characterise (the source plugin's WT Pos,
  an effect's drive or cutoff); while Capture is armed the metric is
  recorded per sweep-position bucket. **Line** draws the metric scalar
  as a 1D X-Y curve per L/R channel with a live position dot; **Heatmap**
  draws the metric's full distribution - column per harmonic (THD) or
  product (IMD), row per sweep position, colour the level. One capture
  feeds both views.
- **Phase Response** - the phase side of the transfer function,
  companion to Frequency Response's magnitude. A Phase / Group Delay
  view selector picks the sub-view. **Phase** plots the per-frequency
  pre-to-post phase difference as a wrapped +/-180 degree curve, with
  the bulk linear-phase (latency) component removed automatically by a
  best-fit line - so it shows the device's real phase distortion
  regardless of path latency, no manual nulling required. **Group
  Delay** plots the negated slope of the unwrapped phase in
  milliseconds (not detrended - true delay). Per-channel L and R
  traces; reveals linear-phase vs minimum-phase character, all-pass
  behaviour, and frequency-dependent time smearing.
- **Dynamics** - the device's static input-vs-output transfer curve.
  Each processed block contributes one point: its pre-effect RMS level
  (X, dB) paired with its post-effect RMS level (Y, dB). Feed a slow
  amplitude ramp through the device and the curve fills in across the
  input range, each input-level bin averaging every block that lands
  in it. A 45-degree unity diagonal is the reference - curve below it
  at high input is compression / limiting, below it at low input is
  expansion / gating. Per-channel L and R traces; accumulates
  continuously with a Clear button to start a fresh trace.

Both IR modes are shipped. Full practical testing of them is gated on
WTGenerator, which delivers the clean discrete impulses and precisely
parameterized log sweeps these modes depend on.

Supporting infrastructure:

- Two-input-bus capture (main = post-effect, sidechain = pre-effect).
  Output passes the post-effect signal through.
- Manual pre-effect delay (sample-accurate) and FFT-based
  cross-correlation auto-measurement for alignment.
- Pre / Post level meters with Peak / RMS toggle and a labelled dB scale.
- Cursor hover readout (frequency + dB at the mouse position).
- Per-analysis input-assumption captions below the panel.
- Sidecar JSON reader: every test-signal script in `scripts/` now emits
  a `wavetable.json` next to its `wavetable.wav` capturing the exact
  parameter values used. WTAnalyzer's "Load Sidecar..." button picks
  that JSON up and exposes its parameters to whichever analysis needs
  them (the analyzer also polls the file's modification time so
  re-running the script picks up automatically). Caption text
  switches from generic mode advice to the actual loaded test-signal
  parameters when a sidecar is present.
- Color semantics are fixed: warm red-orange = pre / input, periwinkle
  violet = post / output, chartreuse = analysis result / "thing to
  fix." Each master colour has an R-channel sibling in the same family
  (amber for pre, cyan for post, green for analysis); the master (L)
  is the bolder of the pair so it stays readable when R is layered on
  top of it.
- Fully responsive UI - the window is user-resizable and every element
  (controls, text, plot, axis labels) scales uniformly.

Stereo support (shipped):

- Every analysis mode runs per-channel. Pre and post buses are both
  read as stereo; each analysis class (FrequencyResponse,
  THDMeasurement, AliasingDetection, IMDMeasurement, ImpulseResponse,
  FarinaIR) maintains independent L and R state and produces L, R,
  and R-L (Diff) outputs.
- Color scheme: L (master) is the bolder of each pair (warm red-orange
  pre, periwinkle violet post, chartreuse analysis) so the master
  identity stays readable when R is layered on top. R is the lighter
  sibling in the same family (amber pre, cyan post, green analysis).
- Display convention: in 1D trace modes (FR, Aliasing, Phase,
  Dynamics, both IR modes) R draws first, L on top, Diff overlaid
  additively in whitesmoke. In bar-chart modes (THD, IMD) each
  category slot subdivides into paired L / R sub-bars with an
  optional Diff sub-bar. Mono signals overlap pixel-for-pixel.
- L / R / Diff toggle row at the right end of the cursor-readout
  strip below the spectrum / panel. L and R are independent on/off
  (at least one stays on); Diff is a separate additive toggle.
- Inline stereo-imbalance readout in the same row, between cursor
  readout and the toggles. Live per-mode summary - e.g.
  "FR diff: +0.3 dB at 234 Hz", "THD diff: +0.03 pp",
  "IR diff: max +0.0012" - so the user can see channel asymmetry
  numerically without leaving the current mode.
- Per-channel level meters: each existing pre and post bar splits
  in half (L top, R bottom), with tiny L / R glyphs in the gutter
  and a base-colour-to-white gradient.

Not yet built:

- **2D sweep capture for other modes** - the Parameter Sweep mode
  now carries the THD and IMD parameter heatmaps (harmonic / product
  distribution vs sweep position), alongside Frequency Response's
  own freq-vs-position heatmap. Aliasing peak-holds across a sweep
  by its own design; the IR modes are one-shot, so neither buckets
  cleanly per sweep position - no further heatmap rollout planned.
- **Precision chart types**:
  - *Parameter-sweep curves* - shipped as the Parameter Sweep mode
    (THD% / IMD% metrics); FR-at-frequency and SNR/SINAD metrics
    still pending.
  - *Compression / dynamics transfer-function curve* - shipped as the
    Dynamics mode.
- Multisine flatness, step response, transfer function from noise.
- **Sidecar-driven parameter pre-fill** - the SidecarReader
  infrastructure exists but no analysis consumes its parameters
  yet. Deferred to WTGenerator: pre-fill is built once WTGenerator
  emits a sidecar carrying real-units parameters (absolute Hz,
  durations) that the Farina / THD / IMD modes consume directly.
- **WTGenerator companion plugin** (see WTGENERATOR.md design doc)
  - the purpose-built test-signal generator: an expression-driven
  source covering the classical measurement signals and the
  signal-character sweeps WTAnalyzer is built to measure.

## How to use it

WTAnalyzer reads two stereo inputs:

- **Main input ("Post-Effect"):** whatever is flowing through the effect
  chain at the point where WTAnalyzer is placed.
- **Sidechain ("Pre-Effect"):** the dry signal before the effect, routed
  in via the host's sidechain UI.

Its output is the post-effect signal, so a track passing through WTAnalyzer
continues to monitor the processed audio as the user expects.

Recommended Ableton Live setup:

1. **Track A** ("Source"): WTGenerator (or any signal source). Nothing else.
2. **Track B** ("Effect"): `Audio From -> Track A, Post FX`. Monitor: `In`.
   Place the effect under test, then **WTAnalyzer at the end of the chain**.
3. On WTAnalyzer's device header, expand the device, find the **Sidechain**
   panel, set `Audio From -> Track A, Post FX`. WTAnalyzer's main input
   picks up the post-effect signal automatically from the chain.

This pattern adapts directly to Logic, Reaper, Cubase, Bitwig, Studio One,
Pro Tools, and FL Studio via each DAW's existing sidechain UI.

## Test-signal scripts (transitional)

Until WTGenerator ships, the `scripts/` folder holds a placeholder set of
Python wavetable generators - plain wavetable WAVs that any wavetable host
can play as a stand-in signal source during this pre-alpha phase.
WTGenerator supersedes them entirely; its built-in generators and
expression engine replace the script-and-wavetable workflow. Each script
has a matching `.xml` exposing its parameters:

| Script | Purpose |
|---|---|
| `harmonics.py` | Additive synthesis of arbitrary harmonic series |
| `multisine.py` | Flat-spectrum multitone for transfer-function tests |
| `noise.py` | White, pink, and brown noise |
| `chirp.py` | Linear and logarithmic sweeps |
| `sweep.py` | Simple sine sweep |
| `step.py` | Step / DC offset signals |
| `impulse.py` | Single impulses and impulse trains |
| `pwm.py` | Pulse-width modulated waveforms |
| `two_tone.py` | Two-sine test signals for IMD measurement |

These scripts need `numpy` and `soundfile`. Run them with whatever
`python3` is on your path:

    python3 -m pip install numpy soundfile

## Building

Requirements:

- [JUCE](https://juce.com) 7+ at `~/Documents/JUCE/` (referenced via global
  path, not vendored).
- Xcode 14+ on macOS for the included project.
- C++20.

Open `Builds/MacOSX/WTAnalyzer.xcodeproj`, select the **WTAnalyzer - All**
scheme, and build. VST3 and AU bundles are copied to
`~/Library/Audio/Plug-Ins/VST3/` and `~/Library/Audio/Plug-Ins/Components/`
as a post-build step.

Currently macOS only. Windows and Linux builds can be regenerated from
`WTAnalyzer.jucer` via [Projucer](https://juce.com/projucer); not yet tested.

## Credits

- **WTAnalyzer:** Fine Increments.
- **WTGenerator:** Fine Increments.
- **WTSynth:** Shane Dunne - the wavetable synth that inspired this project.
  WTAnalyzer and WTGenerator are a standalone pair and are not affiliated
  with or dependent on WTSynth.
- Built on [JUCE](https://juce.com).

## License

TBD.

## Contact

beta@fineincrements.com
