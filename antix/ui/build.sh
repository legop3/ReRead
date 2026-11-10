#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${1:-/work/build/antix/ui}
mkdir -p "$build_dir"
export PKG_CONFIG_LIBDIR=/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig
device_cflags=$(pkg-config --cflags Qt5Core Qt5DBus)
device_libs=$(pkg-config --libs Qt5Core Qt5DBus)
qt_cflags=$(pkg-config --cflags Qt5Widgets)
qt_libs=$(pkg-config --libs Qt5Widgets)
moc "$source_dir/reader-device.hpp" -o "$build_dir/moc-reader-device.cpp"
moc "$source_dir/session.cpp" -o "$build_dir/session.moc"
g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror $device_cflags -I"$source_dir/.." -shared -Wl,-soname,libreader-device.so.1 "$source_dir/reader-device.cpp" "$build_dir/moc-reader-device.cpp" $device_libs -o "$build_dir/libreader-device.so.1"
ln -sf libreader-device.so.1 "$build_dir/libreader-device.so"
g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror $device_cflags -I"$source_dir/.." -I"$build_dir" "$source_dir/session.cpp" -L"$build_dir" -lreader-device $device_libs -o "$build_dir/reader-session"
g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror $qt_cflags -I"$source_dir/.." -shared -Wl,-soname,libreader-ui.so.3 "$source_dir/reader-ui.cpp" -L"$build_dir" -lreader-device $qt_libs -o "$build_dir/libreader-ui.so.3"
ln -sf libreader-ui.so.3 "$build_dir/libreader-ui.so"
gio_cflags=$(pkg-config --cflags gio-unix-2.0)
gio_libs=$(pkg-config --libs gio-unix-2.0)
g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror $qt_cflags $gio_cflags -I"$source_dir/.." "$source_dir/launcher.cpp" -L"$build_dir" -lreader-ui -lreader-device $qt_libs $gio_libs -o "$build_dir/reader-launcher"
