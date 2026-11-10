#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${1:-/work/build/antix/ui}
export PKG_CONFIG_LIBDIR=/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig
moc "$source_dir/main.cpp" -o "$build_dir/audio.moc"
g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror \
    $(pkg-config --cflags Qt5Widgets Qt5DBus gstreamer-1.0) -I"$source_dir/../../ui" -I"$source_dir/../.." -I"$build_dir" \
    "$source_dir/main.cpp" -L"$build_dir" -lreader-ui -lreader-device \
    $(pkg-config --libs Qt5Widgets Qt5DBus gstreamer-1.0) -o "$build_dir/reader-audio-app"
