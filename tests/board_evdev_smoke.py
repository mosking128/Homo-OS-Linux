#!/usr/bin/env python3
"""Exercise the real board entry point using an isolated evdev-format FIFO.

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
parser.add_argument('--transition-cycles', type=int, default=0,
                    help='Additional calculator open/return cycles after the UI checks')
parser.add_argument('--books-cycles', type=int, default=0,
                    help='Book list, reader and focused-search returns (requires demo TXT books)')
parser.add_argument('--maps-cycles', type=int, default=0,
                    help='CarPlay map, place selection, zoom, pan and return cycles')
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
    capture('desktop')
    tap(587, 200)  # Settings: fifth column, second row.
    time.sleep(1)
    tap(80, 328)
    time.sleep(2)
    capture('settings-time')
    swipe(400, 469, 400, 300)
    capture('home-after-bottom-swipe')
    tap(712, 90)  # Files: sixth column, first row.
    capture('files')
    swipe(12, 240, 230, 240)
    tap(337, 90)
    tap(295, 347)  # 1
    tap(505, 347)  # +
    tap(365, 347)  # 2
    tap(505, 411)  # =
    capture('calculator-basic')
    tap(72, 94)  # science mode
    capture('calculator-scientific')
    swipe(788, 240, 570, 240)
    capture('home-after-right-swipe')
    for cycle in range(args.transition_cycles):
        tap(337, 90)
        swipe(400, 469, 400, 300)
        if child.poll() is not None:
            raise RuntimeError('Desktop exited during transition stress')
        print('Transition stress cycle {}/{}'.format(cycle + 1, args.transition_cycles), flush=True)
    for cycle in range(args.books_cycles):
        tap(212, 90)
        tap(245, 72)  # Return a retained reader view to the library first.
        mode = cycle % 3
        if mode == 1:
            tap(400, 130)  # First demo book in the library.
        elif mode == 2:
            tap(90, 70)  # Focus search before returning home.
        capture('books-mode-{}'.format(mode))
        swipe(400, 469, 400, 300)
        print('Books transition cycle {}/{}'.format(cycle + 1, args.books_cycles), flush=True)
    for cycle in range(args.maps_cycles):
        tap(212, 200)  # Maps: second column, second row.
        if cycle == 0:
            capture('maps-overview')
            tap(302, 149)  # Parks filter.
            tap(190, 232)  # First park card.
            capture('maps-selected')
            tap(180, 100)
            capture('maps-keyboard')
            tap(324, 100)  # Clear search and close keyboard.
        tap(760, 100)
        if cycle == 0:
            capture('maps-zoom')
        log.write('MAP-PAN-BEGIN\n')
        log.flush()
        swipe(675, 300, 490, 345)
        log.write('MAP-PAN-END\n')
        log.flush()
        if cycle == 0:
            capture('maps-pan')
        tap(32, 432)  # CarPlay rail's Home button.
        print('Maps transition cycle {}/{}'.format(cycle + 1, args.maps_cycles), flush=True)
    if args.transition_cycles or args.books_cycles or args.maps_cycles:
        capture('home-after-transition-stress')
        # The clock strip may change while stress runs; the desktop underneath
        # must survive the framebuffer handoffs without stale card pixels.
        initial = (artifacts / 'desktop.rgb565').read_bytes()[800 * 35 * 2:]
        returned = (artifacts / 'home-after-transition-stress.rgb565').read_bytes()[800 * 35 * 2:]
        if initial != returned:
            raise RuntimeError('Desktop pixels changed after transition stress')
        print('PASS desktop pixels match after transition stress', flush=True)
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
