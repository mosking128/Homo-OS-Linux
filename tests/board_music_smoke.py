#!/usr/bin/env python3
"""Verify Music on the real framebuffer with isolated evdev input.

Run as root. The systemd desktop is stopped for the check and always restarted.
The app runs as debian, draws to the real framebuffer, and uses its normal
evdev decoder. A FIFO avoids Goodix hardware releases interrupting injected
events; no input configuration or kernel modules are changed.
"""
import argparse
import os
from pathlib import Path
import pwd
import struct
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', default='/home/debian/hachimi-desktop/hachimi_desktop')
parser.add_argument('--artifacts', default='/tmp/hachimi-modular-live')
args = parser.parse_args()
if os.geteuid() != 0:
    raise SystemExit('Run this board-only check as root.')
account = pwd.getpwnam('debian')
artifacts = Path(args.artifacts)
artifacts.mkdir(exist_ok=True)
input_dir = Path(tempfile.mkdtemp(prefix='hachimi-evdev-smoke-'))
os.chown(str(input_dir), account.pw_uid, account.pw_gid)
fifo = input_dir / 'input.fifo'
os.mkfifo(str(fifo), 0o600)
os.chown(str(fifo), account.pw_uid, account.pw_gid)
writer = os.open(str(fifo), os.O_RDWR | os.O_NONBLOCK)
child = None
stopped = False
log = None

def event(kind, code, value):
    os.write(writer, struct.pack('llHHi', 0, 0, kind, code, value))

def point(x, y):
    event(3, 0, x)
    event(3, 1, y)
    event(0, 0, 0)

def press(x, y):
    point(x, y)
    event(1, 330, 1)
    event(0, 0, 0)
    time.sleep(.08)

def release():
    event(1, 330, 0)
    event(0, 0, 0)
    time.sleep(1)

def tap(x, y):
    press(x, y)
    time.sleep(.15)
    release()

def swipe(x, y, end_x, end_y):
    press(x, y)
    for step in range(1, 25):
        point(x + (end_x - x) * step // 24, y + (end_y - y) * step // 24)
        time.sleep(.006)
    release()

def capture(name):
    if child.poll() is not None:
        raise RuntimeError('Board desktop exited: {}'.format(child.returncode))
    with open('/dev/fb0', 'rb', buffering=0) as framebuffer:
        pixels = framebuffer.read(800 * 480 * 2)
    if len(pixels) != 800 * 480 * 2:
        raise RuntimeError('Unexpected framebuffer size')
    (artifacts / (name + '.rgb565')).write_bytes(pixels)
    print('Captured ' + name, flush=True)

def demote():
    os.initgroups(account.pw_name, account.pw_gid)
    os.setgid(account.pw_gid)
    os.setuid(account.pw_uid)

def assert_pcm(running):
    state = Path('/proc/asound/card0/pcm0p/sub0/status').read_text()
    if ('RUNNING' in state) != running:
        raise RuntimeError('Unexpected PCM state: ' + state)

def assert_desktop(name):
    capture(name)
    baseline = (artifacts / 'music-desktop.rgb565').read_bytes()[800 * 35 * 2:]
    actual = (artifacts / (name + '.rgb565')).read_bytes()[800 * 35 * 2:]
    if actual != baseline:
        raise RuntimeError('Desktop pixels retained app content after return: ' + name)

try:
    subprocess.run(['systemctl', 'stop', 'hachimi-desktop'], check=True)
    stopped = True
    environment = dict(os.environ, HOME=account.pw_dir, USER=account.pw_name,
                       HACHIMI_EVDEV=str(fifo), HACHIMI_FBDEV='/dev/fb0')
    log = open(str(artifacts / 'desktop.log'), 'w')
    child = subprocess.Popen([args.binary], cwd='/home/debian/hachimi-desktop',
                             env=environment, stdout=log, stderr=log, preexec_fn=demote)
    time.sleep(2)
    capture('music-desktop')
    tap(400, 425)
    time.sleep(1)
    capture('music-open')
    tap(206, 427)
    assert_pcm(True)
    capture('music-playing')
    tap(206, 427)
    assert_pcm(False)
    capture('music-paused')
    tap(205, 395)
    assert_pcm(False)
    capture('music-seek-paused')
    tap(206, 427)
    assert_pcm(True)
    tap(256, 427)
    assert_pcm(True)
    tap(366, 427)
    capture('music-queue-live')
    tap(366, 427)
    capture('music-lyrics-live')
    first = (artifacts / 'music-open.rgb565').read_bytes()
    second = (artifacts / 'music-lyrics-live.rgb565').read_bytes()
    pixel_offset = (80 * 800 + 410) * 2
    if first[pixel_offset:pixel_offset+2] == second[pixel_offset:pixel_offset+2]:
        raise RuntimeError('Album change did not recolor the real framebuffer')
    print('PASS cover-based background recoloring on the real framebuffer', flush=True)
    tap(96, 427)
    capture('music-repeat')
    tap(96, 427)
    capture('music-shuffle')
    swipe(400, 469, 400, 300)
    assert_pcm(True)
    assert_desktop('music-home-background')
    tap(400, 425)
    assert_pcm(True)
    capture('music-reopen-live')
    tap(206, 427)
    assert_pcm(False)
    swipe(400, 469, 400, 300)
    assert_desktop('music-home-paused')
    for index, (x, y) in enumerate(((337, 90), (212, 90), (87, 200), (462, 90))):
        tap(x, y)
        swipe(400, 469, 400, 300)
        assert_desktop('music-other-app-return-' + str(index))
    for index in range(4):
        tap(400, 425)
        tap(256, 427)
        swipe(400, 469, 400, 300)
        assert_desktop('music-cover-return-' + str(index))
    print('PASS pixel-exact desktop restoration after playback, other apps and repeated cover changes', flush=True)
    print('PASS Music framebuffer/evdev: Home/Dock, playback, pause, seek, next, lyrics, repeat/shuffle, background playback and reopen', flush=True)

finally:
    if child is not None:
        child.terminate()
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait(timeout=5)
    if log is not None:
        log.close()
    os.close(writer)
    fifo.unlink()
    input_dir.rmdir()
    if stopped:
        subprocess.run(['systemctl', 'start', 'hachimi-desktop'], check=True)
        print('Restored systemd desktop and physical Goodix input.', flush=True)
