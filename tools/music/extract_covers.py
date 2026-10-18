#!/usr/bin/env python3
"""Extract a WAV's ID3 APIC cover as a same-stem PNG for the Music app.

python extract_covers.py --output /path/to/covers /path/to/album [...]
Output preserves album directory names; copy those directories into music/.
Requires Pillow. Original music and lyrics are never modified.
"""
import argparse
import io
from pathlib import Path
from PIL import Image, ImageOps


def synchsafe(value):
    number = 0
    for byte in value:
        number = (number << 7) | (byte & 127)
    return number


def extract(path):
    with path.open('rb') as stream:
        header = stream.read(12)
        if header[:4] != b'RIFF' or header[8:] != b'WAVE':
            return None
        while len(chunk := stream.read(8)) == 8:
            size = int.from_bytes(chunk[4:], 'little')
            if chunk[:4].lower() != b'id3 ':
                stream.seek(size + (size & 1), 1)
                continue
            if size > 32 * 1024 * 1024:
                return None
            tag = stream.read(size)
            if tag[:3] != b'ID3' or tag[3] not in (3, 4):
                return None
            pos = 10
            if tag[5] & 64:
                skip = synchsafe(tag[pos:pos+4]) if tag[3] == 4 else int.from_bytes(tag[pos:pos+4], 'big') + 4
                pos += skip
            while pos + 10 <= len(tag) and tag[pos]:
                frame_size = synchsafe(tag[pos+4:pos+8]) if tag[3] == 4 else int.from_bytes(tag[pos+4:pos+8], 'big')
                end = pos + 10 + frame_size
                if end > len(tag) or not frame_size:
                    break
                if tag[pos:pos+4] == b'APIC':
                    frame = tag[pos+10:end]
                    if tag[5] & 128 or (tag[3] == 4 and tag[pos+9] & 2):
                        frame = frame.replace(b'\xff\x00', b'\xff')
                    # Some source files have an empty MIME but valid WebP bytes.
                    for signature in (b'\xff\xd8\xff', b'\x89PNG\r\n\x1a\n', b'RIFF'):
                        start = frame.find(signature)
                        if start >= 0:
                            try:
                                return Image.open(io.BytesIO(frame[start:])).convert('RGB')
                            except OSError:
                                pass
                pos = end
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('albums', nargs='+', type=Path)
    args = parser.parse_args()
    for album in args.albums:
        for wav in sorted(album.glob('*.wav')):
            image = extract(wav)
            if image is None:
                print('No embedded cover:', wav.name)
                continue
            target = args.output / album.name / (wav.stem + '.png')
            target.parent.mkdir(parents=True, exist_ok=True)
            ImageOps.fit(image, (300, 300), method=Image.Resampling.LANCZOS).save(target)
            print('Extracted:', target)


if __name__ == '__main__':
    main()
