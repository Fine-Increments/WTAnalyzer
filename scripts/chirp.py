# Chirp diagnostic wavetable.
# Each frame contains a chirp that sweeps from harmonic START to harmonic
# END of the playback fundamental, packed into one wavetable cycle.
#
# Linear chirp:    instantaneous frequency rises linearly with time.
# Log chirp (Farina): instantaneous frequency rises exponentially with
#                     time. Standard for measuring loudspeakers / rooms /
#                     nonlinear systems. Distortion harmonics appear as
#                     separate impulses in time after deconvolution - which
#                     is genuinely measurable now that the format is float
#                     instead of 8-bit.
#
# Across frames, the END harmonic ramps from START up to the max END you
# choose, so scanning the wavetable progressively widens the swept range.
#
# Dependencies: numpy, soundfile.
# Install: python3 -m pip install numpy soundfile
#
# Output format: 32-bit float mono WAV at 48 kHz, 2048 samples per frame.
# Each frame is normalized to peak 0.95 (about -0.45 dBFS).
#
# Expected command-line:
# python chirp.py <Type> <Start> <End> <num_frames> <output_folder>

import os
import sys
import numpy as np
import soundfile as sf

SAMPLES_PER_FRAME = 2048
SAMPLE_RATE_HZ = 48000
PEAK = 0.95

def linear_chirp(start_h, end_h):
    s = np.arange(SAMPLES_PER_FRAME)
    t = s / SAMPLES_PER_FRAME
    # phase = integral(2*pi*f(t) dt), f(t) = start + (end-start)*t
    phase = 2 * np.pi * (start_h * t + (end_h - start_h) * t * t * 0.5)
    v = np.sin(phase)
    peak = np.max(np.abs(v))
    return PEAK * v / peak if peak > 0 else v

def log_chirp(start_h, end_h):
    if start_h <= 0:
        start_h = 1
    if end_h <= start_h:
        end_h = start_h + 1
    s = np.arange(SAMPLES_PER_FRAME)
    t = s / SAMPLES_PER_FRAME
    K = end_h / start_h
    L = np.log(K)
    phase = 2 * np.pi * start_h * (np.exp(L * t) - 1.0) / L
    v = np.sin(phase)
    peak = np.max(np.abs(v))
    return PEAK * v / peak if peak > 0 else v

chirp_type = sys.argv[1]
start_h = int(sys.argv[2])
max_end_h = int(sys.argv[3])
num_frames = int(sys.argv[4])
output_path = sys.argv[5]
full_path = os.path.join(output_path, 'wavetable.wav')

gen = log_chirp if chirp_type == 'Log (Farina)' else linear_chirp

frames = []
for i in range(num_frames):
    t = i / max(1, num_frames - 1)
    end_h = max(start_h + 1, int(round(start_h + 1 + t * (max_end_h - start_h - 1))))
    frames.append(gen(start_h, end_h))

data = np.concatenate(frames).astype(np.float32)
sf.write(full_path, data, SAMPLE_RATE_HZ, subtype='FLOAT')
