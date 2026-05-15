# Shared helpers for the WTAnalyzer test-signal scripts.
#
# write_sidecar() emits the wavetable.json file alongside the wavetable.wav
# the script just wrote. The schema is documented in PLANNING.md section
# 2.5; WTAnalyzer reads it via Source/SidecarReader.cpp.
#
# Each script imports this module and calls write_sidecar() at the end of
# its run with the parameter dict it parsed from argv.
#
# Dependencies: stdlib only (json, os, datetime, sys).

import json
import os
import sys
import datetime

SCHEMA_VERSION = 1

def write_sidecar(output_path,
                  script_name,
                  analysis,
                  parameters,
                  sample_rate,
                  samples_per_frame,
                  frame_count,
                  channels=1,
                  audio_format="float32"):
    """
    Write wavetable.json alongside the wavetable.wav produced by the
    calling script.

    output_path:        folder where wavetable.wav lives
    script_name:        e.g. "chirp.py"
    analysis:           analysis-registry identifier; matches the
                        analysis="..." attribute in the script's .xml
    parameters:         dict of typed parameter values used
    sample_rate, ...:   audio metadata (mirrors what's in the WAV header
                        but easier for the analyzer to read)
    """
    payload = {
        "schema_version": SCHEMA_VERSION,
        "script": script_name,
        "analysis": analysis,
        "generated_at": datetime.datetime.utcnow().isoformat() + "Z",
        "audio": {
            "sample_rate": int(sample_rate),
            "samples_per_frame": int(samples_per_frame),
            "frame_count": int(frame_count),
            "format": audio_format,
            "channels": int(channels),
        },
        "parameters": parameters,
        "command_line": list(sys.argv),
    }

    full_path = os.path.join(output_path, "wavetable.json")
    with open(full_path, "w") as f:
        json.dump(payload, f, indent=2, sort_keys=False)
