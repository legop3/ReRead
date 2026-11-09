#!/bin/sh
# Build ReRead and write its automatic installer to a registered USB stick.
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

case "${1:-}" in
    '') write_usb=yes ;;
    --build-only) write_usb=no ;;
    -h|--help)
        echo 'Usage: ./make-usb.sh [--build-only]'
        echo 'Build ReRead, then overwrite one registered, unmounted USB stick.'
        echo '--build-only builds release files without writing a USB stick.'
        exit 0 ;;
    *) echo 'Usage: ./make-usb.sh [--build-only]' >&2; exit 2 ;;
esac
[ "$#" -le 1 ] || { echo 'Too many arguments.' >&2; exit 2; }

command -v docker >/dev/null || { echo 'Docker is required.' >&2; exit 1; }
command -v python3 >/dev/null || { echo 'Python 3 is required.' >&2; exit 1; }
[ -f downloads/antiX-26.1_386-core.iso ] || {
    echo 'Missing downloads/antiX-26.1_386-core.iso; see antix/README.md.' >&2
    exit 1
}

echo 'Preparing the ReRead builder...'
# Keep this project's cache separate from Docker's embedded builder.
# Its containerd exporter can stall on the host used for Reader development.
if ! docker buildx inspect reread >/dev/null 2>&1; then
    docker buildx create --name reread --driver docker-container >/dev/null
fi
docker buildx build --builder reread --load -t reread-builder image-builder
# Shared SELinux labeling permits use alongside the optional development container.
echo 'Building the SSD image, installer and update package...'
docker run --rm -v "$PWD:/work:z" reread-builder python3 /work/antix/build.py

if [ "$write_usb" = yes ]; then
    echo 'Writing the installer to the registered USB stick...'
    if [ "$(id -u)" -eq 0 ]; then
        python3 antix/write-usb.py
    else
        sudo python3 antix/write-usb.py
    fi
else
    echo 'ReRead release ready in out/antix/. No USB drive was written.'
fi
