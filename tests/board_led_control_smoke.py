#!/usr/bin/env python3
"""Exercise the RGB app and verify live updates on the board's PWM RGB LED.

Run as root after installing platform/linux/install-led.sh. The systemd desktop
is stopped temporarily; its state and the prior RGB values are restored at exit.
"""
import os
from pathlib import Path
import pwd
import struct
import subprocess
import tempfile
import time

if os.geteuid() != 0:
    raise SystemExit('Run this board-only check as root.')

account = pwd.getpwnam('debian')
binary = '/home/debian/hachimi-desktop/hachimi_desktop'
helper = '/usr/local/libexec/hachimi-led'
artifacts = Path('/tmp/hachimi-led-control-smoke')
artifacts.mkdir(exist_ok=True)
input_dir = Path(tempfile.mkdtemp(prefix='hachimi-led-control-'))
os.chown(str(input_dir), account.pw_uid, account.pw_gid)
fifo = input_dir / 'input.fifo'
os.mkfifo(str(fifo), 0o600)
os.chown(str(fifo), account.pw_uid, account.pw_gid)
writer = os.open(str(fifo), os.O_RDWR | os.O_NONBLOCK)
child = None
stopped = False
log = None


def led_request(request):
    result = subprocess.run([helper], input=request, text=True, check=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return result.stdout.strip()


def read_rgb():
    output = led_request('read\n')
    fields = output.split('\t')
    if len(fields) != 4 or fields[0] != 'RGB':
        raise RuntimeError('Unexpected PWM readback: ' + repr(output))
    return tuple(int(value) for value in fields[1:])


def set_rgb(values):
    output = led_request('set\t{}\t{}\t{}\n'.format(*values))
    if output != 'OK':
        raise RuntimeError('PWM update failed: ' + repr(output))


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
    time.sleep(.12)


def release():
    event(1, 330, 0)
    event(0, 0, 0)
    time.sleep(.65)


def tap(x, y):
    press(x, y)
    release()


def capture(name):
    if child.poll() is not None:
        raise RuntimeError('Board desktop exited: {}'.format(child.returncode))
    with open('/dev/fb0', 'rb', buffering=0) as framebuffer:
        pixels = framebuffer.read(800 * 480 * 2)
    if len(pixels) != 800 * 480 * 2:
        raise RuntimeError('Unexpected framebuffer size')
    (artifacts / (name + '.rgb565')).write_bytes(pixels)


def demote():
    os.initgroups(account.pw_name, account.pw_gid)
    os.setgid(account.pw_gid)
    os.setuid(account.pw_uid)


previous_rgb = read_rgb()
try:
    subprocess.run(['systemctl', 'stop', 'hachimi-desktop'], check=True)
    stopped = True
    environment = dict(os.environ, HOME=account.pw_dir, USER=account.pw_name,
                       HACHIMI_EVDEV=str(fifo), HACHIMI_FBDEV='/dev/fb0')
    log = open(str(artifacts / 'desktop.log'), 'w')
    child = subprocess.Popen([binary], cwd='/home/debian/hachimi-desktop',
                             env=environment, stdout=log, stderr=log, preexec_fn=demote)
    time.sleep(1.5)
    tap(87, 200)  # Home icon: LED Control (first column, second row).
    time.sleep(.7)
    capture('led-control')

    # Drag red while held and confirm the board updates before release.
    press(400, 217)
    point(560, 217)
    time.sleep(.05)
    mid_drag = read_rgb()
    if not 90 <= mid_drag[0] <= 175:
        raise RuntimeError('RGB output did not update during drag: {}'.format(mid_drag))
    release()

    # Continue with precise red, green, and blue values after the live-drag check.
    tap(720, 217)
    tap(559, 293)
    tap(478, 369)
    values = read_rgb()
    if not (values[0] >= 245 and 120 <= values[1] <= 136 and 56 <= values[2] <= 72):
        raise RuntimeError('RGB sliders did not reach PWM outputs: {}'.format(values))
    capture('mixed-rgb')
    print('PASS LED app UI -> PWM RGB {}'.format(values), flush=True)
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
    try:
        set_rgb(previous_rgb)
    finally:
        os.close(writer)
        fifo.unlink()
        input_dir.rmdir()
        if stopped:
            subprocess.run(['systemctl', 'start', 'hachimi-desktop'], check=True)
            print('Restored prior RGB values and systemd desktop.', flush=True)
