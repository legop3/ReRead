#!/bin/sh
# Flash a downloaded or locally built ReRead installer on Linux.
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

case "${1:-}" in
    --build)
        shift
        "$base/build.sh"
        ;;
    --build-only)
        shift
        [ "$#" -eq 0 ] || { echo 'Usage: ./build.sh' >&2; exit 2; }
        exec "$base/build.sh"
        ;;
esac

command -v python3 >/dev/null || { echo 'Python 3 is required.' >&2; exit 1; }
# Help and drive selection work without Docker or a privileged build process.
if [ "${1:-}" = --help ] || [ "${1:-}" = -h ]; then
    exec python3 "$base/antix/write-usb.py" --help
fi
if [ "$(id -u)" -eq 0 ]; then
    exec python3 "$base/antix/write-usb.py" "$@"
fi
exec sudo python3 "$base/antix/write-usb.py" "$@"
