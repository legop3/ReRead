#!/bin/sh
# Build only the Peggle app and the two shared SDK libraries it links.
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ui_dir="$source_dir/../../ui"
build_dir=${1:-/work/build/antix/ui}
mkdir -p "$build_dir"
export PKG_CONFIG_LIBDIR=/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig

# Build the shared libraries only if missing or older than their sources.
if [ ! -f "$build_dir/libreader-device.so.1" ] || \
   [ "$ui_dir/reader-device.cpp" -nt "$build_dir/libreader-device.so.1" ] || \
   [ "$ui_dir/reader-device.hpp" -nt "$build_dir/libreader-device.so.1" ]; then
  moc "$ui_dir/reader-device.hpp" -o "$build_dir/moc-reader-device.cpp"
  g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror \
    $(pkg-config --cflags Qt5Core Qt5DBus) -I"$source_dir/../.." \
    -shared -Wl,-soname,libreader-device.so.1 \
    "$ui_dir/reader-device.cpp" "$build_dir/moc-reader-device.cpp" \
    $(pkg-config --libs Qt5Core Qt5DBus) -o "$build_dir/libreader-device.so.1"
fi
ln -sfn libreader-device.so.1 "$build_dir/libreader-device.so"

if [ ! -f "$build_dir/libreader-ui.so.5" ] || \
   [ "$ui_dir/reader-ui.cpp" -nt "$build_dir/libreader-ui.so.5" ] || \
   [ "$ui_dir/reader-ui.hpp" -nt "$build_dir/libreader-ui.so.5" ] || \
   [ "$build_dir/libreader-device.so.1" -nt "$build_dir/libreader-ui.so.5" ]; then
  g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror \
    $(pkg-config --cflags Qt5Widgets) -I"$source_dir/../.." \
    -shared -Wl,-soname,libreader-ui.so.5 \
    "$ui_dir/reader-ui.cpp" -L"$build_dir" -lreader-device \
    $(pkg-config --libs Qt5Widgets) -o "$build_dir/libreader-ui.so.5"
fi
ln -sfn libreader-ui.so.5 "$build_dir/libreader-ui.so"

g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror \
  $(pkg-config --cflags Qt5Widgets) -I"$ui_dir" -I"$source_dir/../.." \
  "$source_dir/main.cpp" -L"$build_dir" -lreader-ui -lreader-device \
  $(pkg-config --libs Qt5Widgets) -o "$build_dir/reader-peggle-installer"
