# Hachimi i.MX6ULL desktop

This target runs Hachimi's LVGL 9.4 desktop on the board's 800x480 RGB565
framebuffer. It uses LVGL's Linux framebuffer and evdev drivers. The Goodix
touchscreen is opened for input. Settings includes device information, live
network management, display brightness, and date/time.

The desktop uses the user's iOS 27 icons and `wp_3.png` wallpaper. The prepared
800x480 wallpaper and small icon PNGs are in `custom/assets/ios`.

## Build for ARM hard-float

The i.MX6ULL target uses the ARM hard-float ABI and NEON. The build script
prefers the VM's configured `~/.arm-linux-gnueabihf-current` toolchain when it
exists, which keeps the binary compatible with the board's Debian 10 glibc.
Otherwise, make a compatible compiler available on `PATH`, or set `CC` / `CROSS_COMPILE` explicitly. Then run:

```sh
cd platform/linux
./build-armhf.sh
```

The executable and copied image assets are written to `build-armhf/bin/`.

## Run on the board

Copy the contents of `build-armhf/bin/` to a directory such as
`/home/debian/hachimi-desktop`, then run:

```sh
cd /home/debian/hachimi-desktop
./hachimi_desktop
```

The default devices are `/dev/fb0` and `/dev/input/event2`. Override them with
`HACHIMI_FBDEV` or `HACHIMI_EVDEV` if the board enumerates devices differently.
At startup, the Linux app detects the board's Goodix touchscreen and uses
`EVIOCSABS` to align its advertised axis range with the framebuffer before
LVGL opens it. The board reports screen-space touch coordinates but its missing
Goodix configuration blob leaves an incorrect ABS range; the existing four-
corner calibration confirms the coordinates are already landscape 800x480, so
the axes are not swapped. If the kernel rejects that ioctl, LVGL uses the same
measured range directly. Other input devices keep LVGL's automatic range.

## Start at boot with systemd

Install `hachimi-desktop.service` as `/etc/systemd/system/hachimi-desktop.service`
on the board, then enable it:

```sh
sudo systemctl daemon-reload
sudo systemctl disable --now tablet-home.service
sudo systemctl enable --now hachimi-desktop.service
```

The current board unit runs as `root`; the helper and udev rules also support
manual runs as `debian`. The unit conflicts with
the Qt tablet home and the other framebuffer demos so only one desktop owns the
screen at a time.

## PXP framebuffer acceleration

The Linux target can offload large RGB565 framebuffer copies to the BSP's
`/dev/pxp_device`. Install the device rule once; the desktop continues to run as
`debian`, using its existing `video` group:

```sh
sudo install -m 644 platform/linux/70-hachimi-pxp.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --action=change --subsystem-match=misc --sysname-match=pxp_device
sudo udevadm settle
```

PXP is attempted by default. `HACHIMI_PXP=0` selects the original software path;
`HACHIMI_PXP_DEVICE` overrides the device for diagnostics. Unsupported displays
and missing permissions fall back automatically. The adapter supports this
board's 800x480 RGB565, single-buffer DIRECT configuration with zero rotation.
Updates below 65,536 pixels still use software copying.

CPU/NEON drawing uses a cached, contiguous DMA buffer. A separate worker cleans
the cache and submits the PXP copy. Its response has a 100 ms UI-side deadline;
an error disables further hardware jobs until restart. The worker owns the
final device release, so the old driver's potentially blocking cleanup stays
outside the desktop thread. A real kernel DMA fault may still require a board
restart to recover the hardware or a worker stuck in kernel D state.

The BSP's native `fill_en` operation timed out during testing and is not used.
The white mask animation remains a white rectangle; App contents are shown only
after expansion. This acceleration changes framebuffer transfers, not that
animation's appearance or easing.

Build the hardware-only check using the same ARM compiler as the desktop:

```sh
make -C platform/linux CC="$HOME/.arm-linux-gnueabihf-current/bin/arm-linux-gnueabihf-gcc" pxp-smoke
# Copy build-armhf/bin/pxp_smoke to the board, then run as debian:
./pxp_smoke
./pxp_smoke --unavailable
```

The test checks every pixel for full copies and odd crops, destination guards,
cache synchronization, scaling, repeated teardown, and a stopped worker's
deadline. It uses offscreen DMA memory, without touching the framebuffer.
The vendored UAPI headers in `pxp/include/linux` come from the board's 4.19.35
BSP and retain their original notices. They are isolated from LVGL because that
UAPI declares its own `bool` type.

Measured on 2026-10-16, a full-frame cached-to-WC copy took about 3.7 ms through
PXP versus 2.2 ms through CPU memcpy. UI-thread CPU time was about 0.14 ms versus
1.72 ms. These are copy microbenchmarks, not end-to-end animation FPS; this path
reduces UI-thread CPU work but does not promise a shorter transfer time.

## Network settings

The network page uses the board's existing `autowifi.service`, `wpa_supplicant`
and `udhcpc`. It supports Wi-Fi power, scanning, WPA/WPA2 personal or open
networks, disconnecting, saved credentials after a successful connection, and
live Ethernet, IPv4, route and DNS information. Failed joins restore the
previous saved connection. Network commands run in a child process so the UI
stays responsive. Ethernet configuration is owned by the system's ifupdown.

Install Python 3 and the fixed-operation adapter once on the board:

```sh
sudo apt-get install --no-install-recommends python3
sudo ./install-network.sh
```

The root-owned `/usr/local/libexec/hachimi-network` accepts bounded operations
on stdin. The sudo rule grants the `debian` user access only to this adapter,
with no command-line arguments. Passwords travel on stdin and are converted to
PSKs before being saved by wpa_supplicant. No arbitrary shell commands or file
paths are accepted. Bluetooth and hotspot pages explain when the board lacks
an adapter or configured system service; pairing and AP mode are not provided
by this version. The Windows simulator identifies itself as a preview and
does not change the computer's network.

## Date and time settings

The date and time page uses `systemd-timesyncd` through `timedatectl` for network
time synchronization and reads supported time zones from the installed system
database. Install the fixed-operation helper once as root:

```sh
sudo ./install-time.sh
```

The helper supports reading time status and zones, toggling NTP, and selecting a
zone reported by `timedatectl list-timezones`. The simulator displays a preview
and does not change the host clock or time zone.

For a board integration check, copy `check-network.py` to the board and run
`sudo python3 check-network.py` over the wired connection. It briefly toggles
Wi-Fi, checks scanning and joining, verifies recovery after a failed join,
and restores the original saved configuration in a `finally` block.

## Maps assets

The Maps app reads `assets/maps/beijing` relative to the desktop working directory,
or the directory set by `HACHIMI_MAPS_DIR`. Deploy the generated map assets together
with the executable; existing books and other assets should be retained. See
[maps documentation](../../docs/maps.md) for coverage and asset generation.

## RGB LED control

The LED Control app drives the board's three PWM LED outputs with independent
0–255 red, green, and blue sliders. The preview updates immediately; the latest
hardware brightness is written directly by the desktop process on each slider
change, without launching a helper for every touch event. The board's RGB LED is
common anode, so its PWM outputs are active-low. Enable the existing
`imx-fire-pwm-led.dtbo` overlay in `/boot/uEnv.txt` in place of
`imx-fire-led.dtbo`, then reboot. This overlay configures PWM3 (red), PWM8
(green), and PWM7 (blue), and exposes them as active-low entries under
`/sys/class/leds`. Install the one-time permission rule and service group:

```sh
sudo ./install-led.sh
```

The desktop service runs as root so its local controls can access board devices
without spawning a privileged helper for each change. The kernel LED interface
applies the active-low polarity. The simulator shows the color preview without
touching host hardware.
