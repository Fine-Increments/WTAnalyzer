# Step-function diagnostic wavetable.
# A unit step (or band-limited approximation) sitting inside a single
# cycle. Driving an effect with a step exposes:
#   - filter ringing and pre-/post-ringing in linear-phase EQs
#   - transient-shaper attack/release behavior
#   - saturator memory effects (asymmetric on rising vs falling edge)
#
# With float WAV the small ripple lobes around a transition - typically
# 40 to 80 dB below the step itself - are now actually visible and not
# buried in quantization noise.
#
# Modes:
#   Naive             - hard transition from low to high at chosen position.
#                       Full bandwidth, will alias when played high.
#   Bandlimited Sweep - sum the first N odd harmonics of a square wave;
#                       N grows across frames. Smooth -> sharp.
#   Symmetric Step    - both up- and down-steps in one cycle (it IS a
#                       square). Position controls the duty.
#
# Dependencies: numpy, soundfile.
# Install: python3 -m pip install numpy soundfile
#
# Output format: 32-bit float mono WAV at 48 kHz, 2048 samples per frame.
# Step amplitudes are +/- 0.95; band-limited frames are normalized to 0.95.
#
# Expected command-line:
# python step.py <Mode> <Position> <MaxHarmonic> <num_frames> <output_folder>

import os
import sys
import numpy as np
import soundfile as sf
import wt_common

SAMPLES_PER_FRAME = 2048
SAMPLE_RATE_HZ = 48000
PEAK = 0.95

def naive_step(position):
    buf = np.full(SAMPLES_PER_FRAME, -PEAK, dtype=np.float64)
    buf[position:] = PEAK
    return buf

def symmetric_step(position):
    buf = np.full(SAMPLES_PER_FRAME, -PEAK, dtype=np.float64)
    buf[:position] = PEAK
    return buf

def bandlimited_square(max_h):
    s = np.arange(SAMPLES_PER_FRAME)
    t = s / SAMPLES_PER_FRAME
    k = np.arange(1, max_h + 1, 2)  # odd harmonics only
    angles = 2 * np.pi * np.outer(k, t)
    v = (np.sin(angles) / k[:, None].astype(np.float64)).sum(axis=0)
    peak = np.max(np.abs(v))
    return PEAK * v / peak if peak > 0 else v

mode = sys.argv[1]
position = int(sys.argv[2])
max_h = int(sys.argv[3])
num_frames = int(sys.argv[4])
output_path = sys.argv[5]
full_path = os.path.join(output_path, 'wavetable.wav')

frames = []
for i in range(num_frames):
    t = i / max(1, num_frames - 1)
    if mode == 'Naive':
        frames.append(naive_step(position))
    elif mode == 'Symmetric Step':
        p = position if position > 0 else int(SAMPLES_PER_FRAME * (0.1 + 0.8 * t))
        frames.append(symmetric_step(p))
    else:  # Bandlimited Sweep
        n = max(1, int(round(1 + t * (max_h - 1))))
        if (n % 2) == 0:
            n -= 1
        if n < 1:
            n = 1
        frames.append(bandlimited_square(n))

data = np.concatenate(frames).astype(np.float32)
sf.write(full_path, data, SAMPLE_RATE_HZ, subtype='FLOAT')

wt_common.write_sidecar(
    output_path, "step.py", "StepResponse",
    {"Mode": mode, "Position": position, "MaxHarmonic": max_h, "#Frames": num_frames},
    sample_rate=SAMPLE_RATE_HZ, samples_per_frame=SAMPLES_PER_FRAME,
    frame_count=num_frames)
