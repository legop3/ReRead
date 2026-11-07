#!/bin/sh
set -eu
cd /work
mkdir -p build out
python3 /work/image-builder/prepare-stock.py
if [ ! -f build/debian-root/.reader-configured ]; then
    if [ ! -x build/debian-root/bin/sh ]; then
        debootstrap --arch=i386 --variant=minbase --include=ca-certificates \
            bookworm /work/build/debian-root https://deb.debian.org/debian
    fi
    sh /work/image-builder/configure-root.sh
    touch build/debian-root/.reader-configured
fi
python3 /work/image-builder/build-system.py
python3 /work/image-builder/test-desktop.py
python3 /work/image-builder/build-installer.py
python3 /work/image-builder/test-installer.py
