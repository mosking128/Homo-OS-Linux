#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
command -v python3 >/dev/null
command -v timedatectl >/dev/null
install -d -o root -g root -m 755 /usr/local/libexec
install -o root -g root -m 755 "$HERE/hachimi-time" /usr/local/libexec/hachimi-time
RULE=$(mktemp)
trap 'rm -f "$RULE"' EXIT
printf '%s\n' 'debian ALL=(root) NOPASSWD: /usr/local/libexec/hachimi-time ""' > "$RULE"
visudo -cf "$RULE"
install -o root -g root -m 440 "$RULE" /etc/sudoers.d/hachimi-time
