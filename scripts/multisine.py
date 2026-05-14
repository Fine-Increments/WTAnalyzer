# Multisine diagnostic wavetable.
# A sum of N equal-amplitude harmonics with carefully chosen phases.
# Schroeder phasing minimizes the peak/RMS (crest factor), so you can
# drive the signal much louder without clipping while still having a flat
# magnitude spectrum across all harmonics 1..N. With float WAV, the low-
# crest-factor benefit translates into real measurement headroom: quiet
# distortion products are visible all the way down to ~-150 dBFS.
#
# Use cases:
#   - Frequency-response measurement that loads the system uniformly.
#   - Nonlinearity testing without high crest-factor confounds.
#   - A/B between Schroeder vs Random vs Zero phase reveals phase-dependent
#     behavior in a plugin (e.g. saturation, nonlinear EQ).
#
# Phase modes:
#   Schroeder    - phi_k = -pi * k * (k-1) / N      (low crest factor)
#   Random       - uniformly random in [0, 2*pi)
#   Zero         - all phases zero (high crest, looks like an impulse)
#   Sweep        - frame index morphs Schroeder -> Random
#
# Dependencies: numpy, soundfile.
# Install: python3 -m pip install numpy soundfile
#
# Output format: 32-bit float mono WAV at 48 kHz, 2048 samples per frame.
# Each frame is normalized to peak 0.95.
#
# Expected command-line:
# python multisine.py <Phase> <MaxHarmonic> <num_frames> <output_folder>

import os
import sys
import numpy as np
import soundfile as sf

SAMPLES_PER_FRAME = 2048
SAMPLE_RATE_HZ = 48000
PEAK = 0.95

def make_phases(mode, n, seed):
    if mode == 'Schroeder':
        k = np.arange(1, n + 1)
        return -np.pi * k * (k - 1) / n
    if mode == 'Zero':
        return np.zeros(n)
    rng = np.random.default_rng(seed)
    return rng.uniform(0.0, 2 * np.pi, n)

def multisine_frame(n, phases):
    s = np.arange(SAMPLES_PER_FRAME)
    t = s / SAMPLES_PER_FRAME
    k = np.arange(1, n + 1)
    angles = 2 * np.pi * np.outer(k, t) + phases[:, None]
    v = np.sin(angles).sum(axis=0)
    peak = np.max(np.abs(v))
    return PEAK * v / peak if peak > 0 else v

phase_mode = sys.argv[1]
max_harmonic = int(sys.argv[2])
num_frames = int(sys.argv[3])
output_path = sys.argv[4]
full_path = os.path.join(output_path, 'wavetable.wav')

frames = []
for i in range(num_frames):
    if phase_mode == 'Sweep':
        t = i / max(1, num_frames - 1)
        schro = make_phases('Schroeder', max_harmonic, 0)
        rand  = make_phases('Random', max_harmonic, i + 1)
        phases = (1 - t) * schro + t * rand
    else:
        phases = make_phases(phase_mode, max_harmonic, i + 1)
    frames.append(multisine_frame(max_harmonic, phases))

data = np.concatenate(frames).astype(np.float32)
sf.write(full_path, data, SAMPLE_RATE_HZ, subtype='FLOAT')
