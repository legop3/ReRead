#!/bin/sh
# Run inside the builder after antix/ui/build.sh. No sample app is installed.
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${1:-/work/build/antix/ui}
export PKG_CONFIG_LIBDIR=/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig
g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror \
    $(pkg-config --cflags Qt5Widgets) -I"$source_dir/../../ui" -I"$source_dir/../.." \
    "$source_dir/device-window.cpp" -L"$build_dir" -lreader-device \
    $(pkg-config --libs Qt5Widgets) -o "$build_dir/device-window-example"
g++ -m32 -std=c++17 -O2 -fPIC -Wall -Wextra -Werror \
    $(pkg-config --cflags Qt5Widgets) -I"$source_dir/../../ui" -I"$source_dir/../.." \
    "$source_dir/screen-window.cpp" -L"$build_dir" -lreader-ui -lreader-device \
    $(pkg-config --libs Qt5Widgets) -o "$build_dir/screen-window-example"
