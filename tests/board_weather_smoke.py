#!/usr/bin/env python3
"""Verify weather on the real framebuffer with isolated evdev input.

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

try:
    subprocess.run(['systemctl', 'stop', 'hachimi-desktop'], check=True)
    stopped = True
    environment = dict(os.environ, HOME=account.pw_dir, USER=account.pw_name,
                       HACHIMI_EVDEV=str(fifo), HACHIMI_FBDEV='/dev/fb0')
    log = open(str(artifacts / 'desktop.log'), 'w')
    child = subprocess.Popen([args.binary], cwd='/home/debian/hachimi-desktop',
                             env=environment, stdout=log, stderr=log, preexec_fn=demote)
    time.sleep(2)
    capture('weather-desktop')
    tap(556, 425)
    time.sleep(7)
    capture('weather-main')
    tap(182, 66)
    capture('weather-cities')
    tap(212, 116)  # Shanghai quick selection.
    time.sleep(7)
    capture('weather-shanghai')
    tap(182, 66)
    tap(94, 116)  # Return to Beijing.
    time.sleep(7)
    capture('weather-beijing')
    tap(354, 370)
    capture('weather-lifestyle')
    tap(614, 150)
    tap(724, 66)  # Refresh is throttled after the recent city request.
    capture('weather-refresh')
    swipe(400, 469, 400, 300)
    capture('weather-return-home')
    initial = (artifacts / 'weather-desktop.rgb565').read_bytes()[800 * 35 * 2:]
    returned = (artifacts / 'weather-return-home.rgb565').read_bytes()[800 * 35 * 2:]
    if initial != returned:
        raise RuntimeError('Desktop changed after weather return')
    tap(556, 425)
    capture('weather-reopened')
    print('PASS weather framebuffer: launch, city keyboard, Shanghai/Beijing, lifestyle detail, refresh and exact desktop restoration', flush=True)

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
