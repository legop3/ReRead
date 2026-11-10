#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${1:-/work/build/antix/ui}
export PKG_CONFIG_LIBDIR=/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig
g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror \
    $(pkg-config --cflags Qt5Widgets Qt5Network gio-2.0) -I"$source_dir/../../ui" -I"$source_dir/../.." \
    "$source_dir/main.cpp" -L"$build_dir" -lreader-ui -lreader-device \
    $(pkg-config --libs Qt5Widgets Qt5Network gio-2.0) -o "$build_dir/reader-camera-app"
