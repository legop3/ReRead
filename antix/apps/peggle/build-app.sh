#!/bin/sh
# One-command build for the Peggle installer only. Run from any directory.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
image=reread-app-builder
if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is needed to cross-build the i386 app." >&2
  exit 1
fi
if ! docker image inspect "$image" >/dev/null 2>&1; then
  echo "Preparing the ReRead app builder image (first build only)..."
  docker build -t "$image" "$root/image-builder"
fi
docker run --rm --user "$(id -u):$(id -g)" -v "$root:/work" -w /work "$image" sh -ec '
  antix/apps/peggle/build.sh build/antix/ui
  python3 antix/apps/peggle/package.py
'
echo "Built: $root/out/antix/reread-app-peggle-installer_0.1.0_i386.deb"
