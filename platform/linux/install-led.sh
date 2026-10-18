#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
command -v visudo >/dev/null
install -d -o root -g root -m 755 /usr/local/libexec
install -d -o root -g root -m 755 /var/lib/hachimi
install -o root -g root -m 755 "$HERE/hachimi-led" /usr/local/libexec/hachimi-led
install -o root -g root -m 644 "$HERE/70-hachimi-rgb-led.rules" /etc/udev/rules.d/70-hachimi-rgb-led.rules
install -o root -g root -m 644 "$HERE/70-hachimi-backlight.rules" /etc/udev/rules.d/70-hachimi-backlight.rules
install -o root -g root -m 644 "$HERE/hachimi-desktop.service" /etc/systemd/system/hachimi-desktop.service
RULE=$(mktemp)
trap 'rm -f "$RULE"' EXIT
printf '%s\n' 'debian ALL=(root) NOPASSWD: /usr/local/libexec/hachimi-led ""' > "$RULE"
visudo -cf "$RULE"
install -o root -g root -m 440 "$RULE" /etc/sudoers.d/hachimi-led
udevadm control --reload-rules
udevadm trigger --action=add --subsystem-match=leds
udevadm trigger --action=add --subsystem-match=backlight
systemctl daemon-reload
systemctl restart hachimi-desktop
systemctl is-active --quiet hachimi-desktop
[ "$(systemctl show -p User --value hachimi-desktop)" = root ]
