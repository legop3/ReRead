#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${1:-/work/build/antix/ui}
mkdir -p "$build_dir"
export PKG_CONFIG_LIBDIR=/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig
device_cflags=$(pkg-config --cflags Qt5Core)
device_libs=$(pkg-config --libs Qt5Core)
qt_cflags=$(pkg-config --cflags Qt5Widgets)
qt_libs=$(pkg-config --libs Qt5Widgets)
g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror $device_cflags -I"$source_dir/.." -shared -Wl,-soname,libreader-device.so.1 "$source_dir/reader-device.cpp" $device_libs -o "$build_dir/libreader-device.so.1"
ln -sf libreader-device.so.1 "$build_dir/libreader-device.so"
g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror $qt_cflags -I"$source_dir/.." -shared -Wl,-soname,libreader-ui.so.3 "$source_dir/reader-ui.cpp" -L"$build_dir" -lreader-device $qt_libs -o "$build_dir/libreader-ui.so.3"
ln -sf libreader-ui.so.3 "$build_dir/libreader-ui.so"
for executable in launcher camera; do
    extra_cflags= extra_libs=
    if [ "$executable" = camera ]; then
        extra_cflags=$(pkg-config --cflags Qt5Network)
        extra_libs=$(pkg-config --libs Qt5Network)
    fi
    g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror $qt_cflags $extra_cflags -I"$source_dir/.." "$source_dir/$executable.cpp" -L"$build_dir" -lreader-ui -lreader-device $qt_libs $extra_libs -o "$build_dir/reader-$executable"
done
