#!/usr/bin/env bash
# Produce a macOS .app with Qt frameworks bundled (P2b).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"
APP="${BUILD}/peerdesk-client.app"
if [[ ! -d "${APP}" ]]; then
  echo "Build the client first (cmake --build --preset homebrew or vcpkg)."
  exit 1
fi
MACDEPLOYQT="$(command -v macdeployqt || true)"
if [[ -z "${MACDEPLOYQT}" ]]; then
  MACDEPLOYQT="$(find /opt/homebrew/opt/qtbase /opt/homebrew/opt/qt /opt/homebrew/Cellar/qtbase \
    -name macdeployqt -type f 2>/dev/null | head -1 || true)"
fi
if [[ -z "${MACDEPLOYQT}" ]]; then
  echo "macdeployqt not found; the .app exists at ${APP} but Qt dylibs may be unbundled."
  exit 1
fi
"${MACDEPLOYQT}" "${APP}" -always-overwrite
echo "Packed ${APP}"
