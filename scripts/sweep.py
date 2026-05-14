# Sine-harmonic sweep wavetable.
# Frame N contains N cycles of a pure sine wave packed into one wavetable
# period - so when the synth plays a note, frame 1 is the fundamental,
# frame 2 is the 2nd harmonic, and so on. Useful as a basic harmonic-by-
# harmonic frequency-response probe and for hearing how an effect treats
# individual harmonics.
#
# Dependencies: numpy, soundfile.
# Install: python3 -m pip install numpy soundfile
#
# Output format: 32-bit float mono WAV at 48 kHz, 2048 samples per frame.
# Range is +/- 0.95 (about -0.45 dBFS) to leave a small margin for synth-
# side interpolation overshoot.
#
# Expected command-line:
# python sweep.py <num_frames> <output_folder>

import os
import sys
import numpy as np
import soundfile as sf

SAMPLES_PER_FRAME = 2048
SAMPLE_RATE_HZ = 48000
PEAK = 0.95

def sine_wave(cycles):
    s = np.arange(SAMPLES_PER_FRAME)
    phi = cycles * s / SAMPLES_PER_FRAME
    return PEAK * np.sin(2 * np.pi * phi)

num_frames = int(sys.argv[1])
output_path = sys.argv[2]
full_path = os.path.join(output_path, 'wavetable.wav')

frames = [sine_wave(i + 1) for i in range(num_frames)]
data = np.concatenate(frames).astype(np.float32)
sf.write(full_path, data, SAMPLE_RATE_HZ, subtype='FLOAT')
