#!/bin/sh
# Build ReRead release files without accessing host disks.
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

case "${1:-}" in
    -h|--help) echo "Usage: ./build.sh (requires Docker, the antiX ISO and camera firmware)"; exit 0 ;;
    '') ;;
    *) echo "Usage: ./build.sh" >&2; exit 2 ;;
esac
[ "$#" -le 1 ] || { echo "Too many arguments." >&2; exit 2; }

command -v docker >/dev/null || { echo 'Docker is required.' >&2; exit 1; }
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

echo "ReRead release ready in out/antix/."
