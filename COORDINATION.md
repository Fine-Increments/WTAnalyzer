# WTGenerator <-> WTAnalyzer Coordination Contract

> The interface between the two plugins. WTGenerator (source) and WTAnalyzer
> (analyzer) are a standalone pair; this document is the shared contract that
> keeps them in sync. It is mirrored verbatim in both repos - edit both copies
> in the same change.
>
> Last updated: 2026-07-11.

## Principle: host-mediated, no IPC

The two plugins never talk directly. There is no shared memory, no named pipe,
no host-API parameter introspection. All coordination flows through the DAW:

- Audio is routed generator -> effect-under-test -> analyzer by the host.
- Sweep position is shared by driving both plugins' parameters from one DAW
  automation lane.
- Signal metadata is handed off as a file on disk (the sidecar), read by the
  analyzer on its own schedule.

If a future feature seems to need a direct link, it is almost certainly wrong -
re-express it as audio routing, shared automation, or a file. (Recorded
decision; see WTGENERATOR.md "Open design questions".)

## Contract 1: Farina log-sweep phase (LIVE - keep in lockstep)

This is the one contract where the two plugins share an exact mathematical
form, and where a change on one side silently breaks the other.

The generator's Chirp emits an analytic log sweep:

    phase(t) = K * (exp(t * L / T) - 1)
    L = ln(f1 / f0),  K = 2*pi*f0*T / L,  T = sweep duration

    s(t) = sin(phase(t))

The analyzer builds its inverse-sweep deconvolution filter from the SAME closed
form (time-reversed, with the +6 dB/oct Farina amplitude envelope):

    f(t) = s(T - t) * exp(-t * L / T)

so that `s * f` is a delta and the deconvolution recovers a clean impulse.

- Generator side: `WTGenerator/Source/BuiltIn/SweptGenerators.cpp`
  (`ChirpGenerator::render`).
- Analyzer side: `WTAnalyzer/Source/Analyses/FarinaIR.cpp`
  (`generateInverseSweep`).

**Rule:** the generator must produce phase from this closed form, NOT by numeric
integration of the instantaneous frequency - a per-sample integral drifts from
the analytic phase and smears the recovered IR (worst at high frequencies).
If either side's sweep definition changes (phase law, envelope, f0/f1 guard),
change the other in the same commit.

Known minor mismatch (acceptable): the generator applies a 15 ms raised-cosine
end taper the analyzer does not model, and clamps `f1 > f0` (analyzer requires
`f1 >= 1.1*f0`). Effect is confined to the sweep endpoints.

## Contract 2: Sweep position and the X axis (LIVE)

WTAnalyzer exposes one 0..1 automatable parameter, `Sweep Position`. The user
drives both it and the generator's chosen swept parameter (a built-in generator
parameter, or a declared expression parameter) from a single DAW automation
source. The analyzer buckets its metric by `Sweep Position`, so its X axis is
"where in the sweep," not elapsed time.

The analyzer never reads the generator's parameter values - it only knows the
0..1 position the DAW handed it. Real-world units for the axis come from
Contract 3 (sidecar) or manual entry in the analyzer UI.

## Contract 3: Sidecar JSON handoff (PENDING - v3)

Planned, not yet built. WTGenerator will write a `signal.json` next to its
loaded definition describing the current signal's parameters in REAL units
(absolute Hz, dB, seconds). WTAnalyzer already has the consumer side: a
`SidecarReader` and a "Load Sidecar..." button that polls the file's
modification time and pre-fills mode parameters / labels the X axis.

- Analyzer consumer: `WTAnalyzer/Source/SidecarReader.{h,cpp}` (shipped, waiting
  for a producer).
- Generator producer: not yet implemented (WTGENERATOR.md section 7).

**Open item:** the exact `signal.json` schema must be defined jointly when the
producer is built, matching what `SidecarReader` expects. Define it in this
document at that time.

## Contract 4: Signal cleanliness guarantees (LIVE)

The analyzer trusts that the generator's signal is clean, because it cannot
distinguish generator artifacts from device-under-test artifacts (it even has an
Aliasing Detection mode). The generator therefore guarantees:

- **Neutral path** - no filter, LFO, or envelope colouring the signal.
- **Session-rate, no resampling** - built-in generators render at the host rate;
  expression mode oversamples then decimates.
- **Band-limited** - expression output is oversampled (8x) and decimated;
  built-in generators are band-limited by construction. Residual aliasing sits
  far below any device under test.
- **Free-run transport** - signal follows the host transport; the standalone
  build free-runs.

## Test-signal -> analysis mapping

Which generator feeds which analysis (the intended pairings):

| WTGenerator signal | WTAnalyzer analysis |
|---|---|
| Sine (swept freq) | Frequency Response, THD |
| Sine sweep / Multisine | Frequency Response (flatness) |
| Two-tone | IMD |
| Farina chirp | Farina IR (Contract 1), CSD |
| MLS | MLS IR, CSD |
| Impulse (one-shot / train) | Direct Impulse IR, CSD |
| Step | Step Response |
| White / pink noise | Frequency Response, noise-floor / SNR |
| Amplitude ramp (any signal) | Dynamics (transfer curve) |
| Any, per-channel (future stereo) | Stereo Image (divergence / correlation) |

## Current coordination state (2026-07-11)

| Piece | WTGenerator | WTAnalyzer |
|---|---|---|
| Expression engine + built-in generators | shipped (v1, v2) | n/a |
| Farina chirp analytic phase (Contract 1) | shipped | shipped |
| Sweep-position sharing (Contract 2) | via any automatable param | `Sweep Position` shipped |
| Sidecar producer (Contract 3) | pending (v3) | consumer shipped |
| Internal auto-sweep (standalone) | pending (v3) | its own sweep capture shipped |
| Stereo test signals | pending (v4) | stereo analysis shipped |
