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
  the input signal in amber and the added alias residue in green; Pre
  and Post views are channel-only sanity checks. Hold and Clear apply
  to the green trace.
- **IMD Measurement** - intermodulation products from a two-tone input
  (orders 2 through 4, 12 products total). Differential IMD percent plus
  a per-product bar chart with By Order and By Hz layout toggle.
  Pre / Post / Diff view toggle. Hold and Freeze.

Supporting infrastructure:

- Two-input-bus capture (main = post-effect, sidechain = pre-effect).
  Output passes the post-effect signal through.
- Manual pre-effect delay (sample-accurate) and FFT-based
  cross-correlation auto-measurement for alignment.
- Pre / Post level meters with Peak / RMS toggle and a labelled dB scale.
- Cursor hover readout (frequency + dB at the mouse position).
- Per-analysis input-assumption captions below the panel.
- Color semantics are fixed: amber = pre / input, cyan = post / output,
  green = analysis result / "thing to fix."
- Fully responsive UI - the window is user-resizable and every element
  (controls, text, plot, axis labels) scales uniformly.

Not yet built:

- Farina deconvolution, multisine flatness, impulse response, step
  response, transfer function from noise.
- Long-form sweep capture and 2D position plots.
- Script picker / sidecar JSON reader (would auto-configure WTAnalyzer
  from WTSynth's test-signal run).

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
