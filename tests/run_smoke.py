#!/usr/bin/env python3
"""Run a headless smoke executable with isolated state and optional fixtures."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="hachimi-smoke-run-") as home:
    environment = dict(os.environ, HOME=home, USERPROFILE=home, HACHIMI_WEATHER_DIR=home)
    environment.pop("HACHIMI_WEATHER_KEY", None)
    if binary.stem != "music_smoke":
        environment["HACHIMI_MUSIC_DIR"] = home
    args = [str(binary)]
    if binary.stem in ("clock_smoke", "calendar_smoke"):
        args.append(home)
    if binary.stem == "weather_smoke":
        args.extend(["--offline", str(Path(home) / "weather.ppm")])
    result = subprocess.run(args, env=environment)
    sys.exit(result.returncode)
