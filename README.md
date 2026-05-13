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

Pre-alpha. Built so far:

- Two-input-bus capture (main + sidechain), output passes the post-effect
  signal through.
- Manual pre-effect delay (sample-accurate) for aligning the two buses.
- Auto-measurement of pre/post latency via FFT-based cross-correlation.
- Real-time pre/post FFT spectrum overlay with log-frequency and dB axes.
- Fully responsive UI - the window is user-resizable and every element
  (controls, text, plot, axis labels) scales uniformly with size.

Not yet built:

- The ten built-in analysis methods: frequency response, THD, IMD, Farina
  deconvolution, multisine flatness, impulse response, step response,
  aliasing detection, transfer function from noise, generic overlay.
- Script picker + sidecar JSON reader (auto-configures from WTSynth's run).
- Sweep capture and 2D position plots.

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
