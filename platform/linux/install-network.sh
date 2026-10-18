#!/bin/sh
set -eu

# Run as root. Only this fixed adapter is granted passwordless execution.
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
command -v python3 >/dev/null
install -d -o root -g root -m 755 /usr/local/libexec
install -o root -g root -m 755 "$HERE/hachimi-network" /usr/local/libexec/hachimi-network
RULE=$(mktemp)
trap 'rm -f "$RULE"' EXIT
printf '%s\n' 'debian ALL=(root) NOPASSWD: /usr/local/libexec/hachimi-network ""' > "$RULE"
visudo -cf "$RULE"
install -o root -g root -m 440 "$RULE" /etc/sudoers.d/hachimi-network
