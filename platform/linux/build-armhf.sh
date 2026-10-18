#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR=${BUILD_DIR:-"$HERE/build-armhf"}
if [ -z "${CC:-}" ]; then
    if [ -n "${CROSS_COMPILE:-}" ]; then
        CC="${CROSS_COMPILE}gcc"
    elif [ -x "$HOME/.arm-linux-gnueabihf-current/bin/arm-linux-gnueabihf-gcc" ]; then
        CC="$HOME/.arm-linux-gnueabihf-current/bin/arm-linux-gnueabihf-gcc"
    else
        CC=arm-linux-gnueabihf-gcc
    fi
fi

make -C "$HERE" BUILD_DIR="$BUILD_DIR" CC="$CC" -j"${JOBS:-2}"
