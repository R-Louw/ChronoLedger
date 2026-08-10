#!/bin/sh
set -eu
if [ -d build ]; then
    meson setup build --reconfigure --buildtype=release
else
    meson setup build --buildtype=release
fi
meson compile -C build
exec ./build/chronoledger
