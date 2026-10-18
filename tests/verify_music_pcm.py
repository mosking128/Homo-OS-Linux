#!/usr/bin/env python3
"""Compare the actual ALSA worker's PCM against source WAV samples, silently.

Run on the board from the desktop's working directory, with the independently
built music_audio_capture executable as the first argument. Uses ALSA's file
and null plugins; never opens a hardware PCM device.
"""
from pathlib import Path
import array
import os
import subprocess
import sys
import tempfile
import wave

tracks = sorted(Path('music').rglob('*.wav'), key=lambda p: str(p).encode('utf-8'))
assert tracks
with tempfile.TemporaryDirectory(prefix='hachimi-pcm-') as temp:
    root = Path(temp)
    raw = root / 'output.raw'
    config = root / 'alsa.conf'
    config.write_text('pcm.null { type null }\npcm.capture { type file slave.pcm "null" '
                      'file "' + str(raw) + '" format "raw" }\n')
    environment = dict(os.environ, ALSA_CONFIG_PATH=str(config), HACHIMI_AUDIO_DEVICE='capture')
    cases = [(0, level) for level in (0, 1, 37, 65, 100)] + [(i, 65) for i in range(1, len(tracks))]
    checked = 0
    for index, volume in cases:
        subprocess.run([sys.argv[1], str(index), str(volume)], env=environment, check=True)
        actual = array.array('h', raw.read_bytes())
        assert len(actual) > 4000
        with wave.open(str(tracks[index])) as source:
            expected = array.array('h', source.readframes(len(actual) // source.getnchannels()))
        assert len(actual) == len(expected)
        for offset, (value, original) in enumerate(zip(actual, expected)):
            scaled = abs(original) * volume // 100
            if original < 0:
                scaled = -scaled
            assert value == scaled, (tracks[index], volume, offset, original, value, scaled)
        checked += len(actual)
        raw.unlink()
    print('PASS: actual ALSA output matches source WAV sample-for-sample:',
          len(cases), 'cases,', checked, 'samples; signed gain, mute, 100%, 44.1/48 kHz')
