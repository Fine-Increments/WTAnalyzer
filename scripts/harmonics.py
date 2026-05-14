# Band-limited classic shapes for aliasing diagnostics.
# Generates sawtooth, square, or triangle waves built from the additive
# Fourier series, with a controllable max harmonic count that grows across
# frames. Comparing a "naive" (full-bandwidth, intentionally aliased) frame
# to a band-limited version makes aliasing artifacts in distortion /
# saturation / filter plugins immediately audible.
#
# Fourier coefficients used:
#   Saw:      a_k = 1/k                         for k = 1..N
#   Square:   a_k = 1/k                         for odd k only
#   Triangle: a_k = (-1)^((k-1)/2) / k^2        for odd k only
#
# Across frames, MaxHarmonic ramps from "Start" up to "End". So early
# frames are mellow band-limited shapes; later frames push toward / past
# Nyquist. When the synth is played at high pitch, late frames will alias
# - that's the point.
#
# Dependencies: numpy, soundfile.
# Install: python3 -m pip install numpy soundfile
#
# Output format: 32-bit float mono WAV at 48 kHz, 2048 samples per frame.
# Each frame is normalized to peak 0.95.
#
# Expected command-line:
# python harmonics.py <Shape> <Start> <End> <num_frames> <output_folder>

import os
import sys
import numpy as np
import soundfile as sf

SAMPLES_PER_FRAME = 2048
SAMPLE_RATE_HZ = 48000
PEAK = 0.95

def harmonic_amplitudes(shape, max_h):
    k = np.arange(1, max_h + 1)
    a = np.zeros(max_h, dtype=np.float64)
    if shape == 'Sawtooth':
        a = 1.0 / k.astype(np.float64)
    elif shape == 'Square':
        odd = (k % 2) == 1
        a[odd] = 1.0 / k[odd].astype(np.float64)
    elif shape == 'Triangle':
        odd = (k % 2) == 1
        odd_k = k[odd]
        signs = np.where(((odd_k - 1) // 2) % 2 == 0, 1.0, -1.0)
        a[odd] = signs / (odd_k.astype(np.float64) ** 2)
    return a

def shape_frame(shape, max_h):
    s = np.arange(SAMPLES_PER_FRAME)
    t = s / SAMPLES_PER_FRAME
    k = np.arange(1, max_h + 1)
    a = harmonic_amplitudes(shape, max_h)
    angles = 2 * np.pi * np.outer(k, t)
    v = (a[:, None] * np.sin(angles)).sum(axis=0)
    peak = np.max(np.abs(v))
    return PEAK * v / peak if peak > 0 else v

shape = sys.argv[1]
start_h = int(sys.argv[2])
end_h = int(sys.argv[3])
num_frames = int(sys.argv[4])
output_path = sys.argv[5]
full_path = os.path.join(output_path, 'wavetable.wav')

frames = []
for i in range(num_frames):
    t = i / max(1, num_frames - 1)
    max_h = max(1, int(round(start_h + t * (end_h - start_h))))
    frames.append(shape_frame(shape, max_h))

data = np.concatenate(frames).astype(np.float32)
sf.write(full_path, data, SAMPLE_RATE_HZ, subtype='FLOAT')
