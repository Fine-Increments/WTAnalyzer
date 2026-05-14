# Impulse / sparse-pulse diagnostic wavetable.
# Each frame is mostly silent with one or a few non-zero samples. When the
# synth plays a frame through an effect, the output cycle approximates the
# effect's impulse response (truncated to one wavetable period). With
# float WAV the IR captures real dynamic range - reverb tails, filter
# pre-/post-ringing, etc. all decay properly to silence.
#
# Modes:
#   Single Impulse    - one impulse per frame at the chosen position
#   Sweep Position    - position moves from 0 to 2047 across frames
#   Pair              - two impulses per frame (start and chosen position)
#   Comb              - N evenly spaced impulses; N grows across frames
#
# Dependencies: numpy, soundfile.
# Install: python3 -m pip install numpy soundfile
#
# Output format: 32-bit float mono WAV at 48 kHz, 2048 samples per frame.
# Impulse amplitude is 0.95 (about -0.45 dBFS) with the rest of the frame
# at exact zero - no quantization noise floor.
#
# Expected command-line:
# python impulse.py <Mode> <Position> <CombMax> <num_frames> <output_folder>

import os
import sys
import numpy as np
import soundfile as sf

SAMPLES_PER_FRAME = 2048
SAMPLE_RATE_HZ = 48000
PEAK = 0.95

def write_frame(positions):
    buf = np.zeros(SAMPLES_PER_FRAME, dtype=np.float32)
    for p in positions:
        buf[p % SAMPLES_PER_FRAME] = PEAK
    return buf

mode = sys.argv[1]
position = int(sys.argv[2])
comb_max = int(sys.argv[3])
num_frames = int(sys.argv[4])
output_path = sys.argv[5]
full_path = os.path.join(output_path, 'wavetable.wav')

frames = []
for i in range(num_frames):
    t = i / max(1, num_frames - 1)
    if mode == 'Single Impulse':
        frames.append(write_frame([position]))
    elif mode == 'Sweep Position':
        p = int(t * (SAMPLES_PER_FRAME - 1))
        frames.append(write_frame([p]))
    elif mode == 'Pair':
        frames.append(write_frame([0, position]))
    else:  # Comb
        n = max(1, int(round(1 + t * (comb_max - 1))))
        spacing = SAMPLES_PER_FRAME // n
        frames.append(write_frame([k * spacing for k in range(n)]))

data = np.concatenate(frames).astype(np.float32)
sf.write(full_path, data, SAMPLE_RATE_HZ, subtype='FLOAT')
