# WTAnalyzer

A JUCE-based VST3/AU analyzer plugin for measuring how audio effects respond
to known test signals. Designed as a companion to
[WTSynth](https://github.com/getdunne/WTSynth) (Shane Dunne) - WTSynth
generates the test signal from a Python-rendered wavetable, the effect under
test processes it, and WTAnalyzer sits downstream comparing pre and post.

Together they form a two-plugin DSP test bench that lives in any DAW that
supports sidechain routing, alongside the effect being developed or
characterized.

## Status

Pre-alpha. Working analysis modes:

- **Generic Overlay** - live pre + post FFT spectra overlaid on a log
  frequency / dB plot. Zone-aware mouse zoom (drag in each axis gutter
  zooms that axis only; drag in the plot zooms both).
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
- **Stereo Image** - per-frequency stereo divergence: how much the
  right and left channels differ in level at each frequency, on a
  bipolar centred-on-zero plot (green centre line = balanced, lime
  upward = R louder, mint downward = L louder). Its own Pre / Post /
  Diff selector - Diff (default) isolates the device-added divergence
  `(post_R - post_L) - (pre_R - pre_L)`, so a stereo-transparent
  device reads flat regardless of how stereo the input was. The home
  for stereo-specific analysis; spectral phase correlation and a
  goniometer are planned to join it here.

Both IR modes are shipped but currently bottlenecked by WTSynth as a
source: the wavetable cycling model produces an impulse train at
playback pitch, and WTSynth's script-parameter UI has display bugs
that make Farina parameters hard to set reliably. Full practical
testing of these modes is gated on WTGenerator's render-mode-scripts
arrival.

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
- Display convention: in 1D trace modes (Generic Overlay, FR,
  Aliasing, both IR modes) R draws first, L on top, Diff overlaid
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

- **2D sweep capture for other modes** - currently only Frequency
  Response writes into the SweepCapture buffer; THD / Aliasing /
  IMD / Direct Impulse IR / Farina IR rollouts pending. Each
  mode's heatmap uses its own X-axis category (harmonic, product,
  time, freq) with a mode-specific colormap (bipolar for
  FR/IR, monotonic for THD/IMD/Aliasing).
- **Precision chart types**:
  - *Parameter-sweep curves* (Plugin Doctor style 1D X-Y plots:
    THD% / IMD% / FR-at-freq vs swept parameter).
  - *Phase response + group delay* (companion charts to FR
    magnitude).
  - *Cumulative Spectral Decay (CSD)* waterfall.
  - *Compression / dynamics transfer-function curve*.
  - *Lissajous / goniometer + phase correlation meter* (depends
    on stereo support).
- Multisine flatness, step response, transfer function from noise.
- **Sidecar-driven parameter pre-fill** - the SidecarReader
  infrastructure exists but no analysis consumes its parameters
  yet. Rolling out to all consuming modes (Farina, IMD, THD) in
  one consistent sweep when those modes are feature-complete.
- **WTGenerator companion plugin** (see WTGENERATOR.md design doc)
  - purpose-built test-signal generator that resolves WTSynth's
  structural limitations for IR / Farina / arbitrary-Hz two-tone
  testing.

## How to use it

WTAnalyzer reads two stereo inputs:

- **Main input ("Post-Effect"):** whatever is flowing through the effect
  chain at the point where WTAnalyzer is placed.
- **Sidechain ("Pre-Effect"):** the dry signal before the effect, routed
  in via the host's sidechain UI.

Its output is the post-effect signal, so a track passing through WTAnalyzer
continues to monitor the processed audio as the user expects.

Recommended Ableton Live setup:

1. **Track A** ("Source"): WTSynth (or any signal source). Nothing else.
2. **Track B** ("Effect"): `Audio From -> Track A, Post FX`. Monitor: `In`.
   Place the effect under test, then **WTAnalyzer at the end of the chain**.
3. On WTAnalyzer's device header, expand the device, find the **Sidechain**
   panel, set `Audio From -> Track A, Post FX`. WTAnalyzer's main input
   picks up the post-effect signal automatically from the chain.

This pattern adapts directly to Logic, Reaper, Cubase, Bitwig, Studio One,
Pro Tools, and FL Studio via each DAW's existing sidechain UI.

## Test-signal scripts

The `scripts/` folder contains Python wavetable generators for use with
WTSynth. Each script has a matching `.xml` exposing its parameters to
WTSynth's parameter UI:

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

WTSynth on macOS shells out to `/usr/bin/python3` (the system Python),
not your shell's `python3`. Install script dependencies against the
system interpreter:

    sudo -H /usr/bin/python3 -m pip install numpy soundfile

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
- **WTSynth:** Shane Dunne (the source plugin we pair with).
- Built on [JUCE](https://juce.com).

## License

TBD.

## Contact

beta@fineincrements.com
