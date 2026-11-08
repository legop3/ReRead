#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${1:-/work/build/antix/ui}
mkdir -p "$build_dir"
export PKG_CONFIG_LIBDIR=/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig
qt_cflags=$(pkg-config --cflags Qt5Widgets Qt5Network)
qt_libs=$(pkg-config --libs Qt5Widgets Qt5Network)
g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror $qt_cflags -I"$source_dir/.." -shared -Wl,-soname,libreader-ui.so.1 "$source_dir/reader-ui.cpp" $qt_libs -o "$build_dir/libreader-ui.so.1"
ln -sf libreader-ui.so.1 "$build_dir/libreader-ui.so"
for executable in launcher camera; do
    g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror $qt_cflags -I"$source_dir/.." "$source_dir/$executable.cpp" -L"$build_dir" -lreader-ui $qt_libs -o "$build_dir/reader-$executable"
done
