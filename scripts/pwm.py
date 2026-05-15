# Pulse-width-modulation wavetable.
# Each frame is one pulse cycle whose duty cycle is determined by the
# frame index and a chosen mapping curve (Linear, Reciprocal, Exponential).
# Scanning the table sweeps PWM character without manual modulation.
#
# See https://www.youtube.com/watch?v=0rMUuULc5DE for the original demo.
#
# Dependencies: numpy, soundfile.
# Install: python3 -m pip install numpy soundfile
#
# Output format: 32-bit float mono WAV at 48 kHz, 2048 samples per frame.
# Pulses are bipolar (+/- 0.95) and DC-balanced around the chosen duty so
# the signal has zero mean - cleaner than the legacy unipolar 8-bit form.
#
# Expected command-line:
# python pwm.py linear|reciprocal|exponential <num_frames> <output_folder>

import os
import sys
import numpy as np
import soundfile as sf
import wt_common

SAMPLES_PER_FRAME = 2048
SAMPLE_RATE_HZ = 48000
PEAK = 0.95

def pulse_wave(duty_fraction):
    s = np.arange(SAMPLES_PER_FRAME)
    phi = s / SAMPLES_PER_FRAME
    return np.where(phi < duty_fraction, PEAK, -PEAK)

def get_duty_fraction(method, index, num_frames):
    fraction = index / float(num_frames)
    if method == 'linear':
        return 0.5 - fraction * (1.0 / 2 - 1.0 / 32)
    if method == 'reciprocal':
        return 1.0 / (2.0 + fraction * 30.0)
    if method == 'exponential':
        return pow(0.5, 1 + fraction * 4.0)
    return 0.5

method = sys.argv[1].lower()
num_frames = int(sys.argv[2])
output_path = sys.argv[3]
full_path = os.path.join(output_path, 'wavetable.wav')

frames = [pulse_wave(get_duty_fraction(method, i, num_frames)) for i in range(num_frames)]
data = np.concatenate(frames).astype(np.float32)
sf.write(full_path, data, SAMPLE_RATE_HZ, subtype='FLOAT')

wt_common.write_sidecar(
    output_path, "pwm.py", "GenericOverlay",
    {"Method": method, "#Frames": num_frames},
    sample_rate=SAMPLE_RATE_HZ, samples_per_frame=SAMPLES_PER_FRAME,
    frame_count=num_frames)
