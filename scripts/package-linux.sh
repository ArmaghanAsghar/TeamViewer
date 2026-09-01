#!/usr/bin/env bash
# Ubuntu .deb via CPack. Optional AppImage if linuxdeploy is on PATH.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"
if [[ ! -x "${BUILD}/peerdesk-server" ]]; then
  echo "Build first."
  exit 1
fi
cmake --build "${BUILD}" --target package
echo "CPack packages are under ${BUILD}."
if command -v linuxdeploy >/dev/null 2>&1 && command -v linuxdeploy-plugin-qt >/dev/null 2>&1; then
  linuxdeploy --appdir "${BUILD}/AppDir" -e "${BUILD}/peerdesk-client" -e "${BUILD}/peerdesk-server" \
    --plugin qt --output appimage
fi
