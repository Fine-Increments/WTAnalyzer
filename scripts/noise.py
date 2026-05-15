# Colored-noise diagnostic wavetable.
# Each frame is one period of band-limited noise with a chosen spectral
# slope. Shaping is done in the frequency domain: build a half-spectrum
# with the desired 1/f^alpha magnitude and random phase, then irfft to
# time domain.
#
# Colors:
#   White   - flat spectrum   (alpha = 0)
#   Pink    - -3 dB / oct     (alpha = 0.5 in amplitude / 1 in power)
#   Brown   - -6 dB / oct     (alpha = 1)   integrated white
#   Blue    - +3 dB / oct     (alpha = -0.5)
#   Violet  - +6 dB / oct     (alpha = -1)  differentiated white
#
# Each frame uses a different RNG seed so scanning the wavetable produces
# a continuously evolving noise texture (for stochastic-process testing).
#
# Dependencies: numpy, soundfile.
# Install: python3 -m pip install numpy soundfile
#
# Output format: 32-bit float mono WAV at 48 kHz, 2048 samples per frame.
# Each frame is normalized to peak 0.95. The float container preserves
# the noise's spectral shape exactly - no quantization-induced floor that
# would distort the chosen color.
#
# Expected command-line:
# python noise.py <Color> <SeedOffset> <num_frames> <output_folder>

import os
import sys
import numpy as np
import soundfile as sf
import wt_common

SAMPLES_PER_FRAME = 2048
SAMPLE_RATE_HZ = 48000
PEAK = 0.95

ALPHA = {
    'White':  0.0,
    'Pink':   0.5,
    'Brown':  1.0,
    'Blue':  -0.5,
    'Violet': -1.0,
}

def colored_noise_frame(alpha, seed):
    n = SAMPLES_PER_FRAME
    rng = np.random.default_rng(seed)
    bins = n // 2 + 1
    freqs = np.arange(bins, dtype=np.float64)
    mag = np.ones(bins)
    if alpha != 0.0:
        mag[1:] = freqs[1:] ** (-alpha)
    mag[0] = 0.0  # remove DC
    phases = rng.uniform(0.0, 2 * np.pi, size=bins)
    spectrum = mag * np.exp(1j * phases)
    spectrum[-1] = spectrum[-1].real  # Nyquist bin must be real
    samples = np.fft.irfft(spectrum, n=n)
    peak = np.max(np.abs(samples))
    return PEAK * samples / peak if peak > 0 else samples

color = sys.argv[1]
seed_offset = int(sys.argv[2])
num_frames = int(sys.argv[3])
output_path = sys.argv[4]
full_path = os.path.join(output_path, 'wavetable.wav')

alpha = ALPHA.get(color, 0.0)

frames = [colored_noise_frame(alpha, seed_offset + i + 1) for i in range(num_frames)]
data = np.concatenate(frames).astype(np.float32)
sf.write(full_path, data, SAMPLE_RATE_HZ, subtype='FLOAT')

wt_common.write_sidecar(
    output_path, "noise.py", "TransferFunctionFromNoise",
    {"Color": color, "SeedOffset": seed_offset, "#Frames": num_frames},
    sample_rate=SAMPLE_RATE_HZ, samples_per_frame=SAMPLES_PER_FRAME,
    frame_count=num_frames)
