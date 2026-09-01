#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"
CLIENT="${BUILD}/peerdesk-client"
if [[ -d "${BUILD}/peerdesk-client.app" ]]; then
  CLIENT="${BUILD}/peerdesk-client.app/Contents/MacOS/peerdesk-client"
fi
if [[ ! -x "${BUILD}/peerdesk-server" || ! -x "${CLIENT}" ]]; then
  echo "Build first: cmake --preset homebrew && cmake --build --preset homebrew -j"
  echo "  or: ./scripts/bootstrap-vcpkg.sh"
  exit 1
fi
DATA="${PEERDESK_DATA:-${ROOT}/.peerdesk-demo}"
PORT="${PEERDESK_PORT:-4473}"
MODE="${1:-synthetic}"

if [[ "${MODE}" == "x11" ]]; then
  EXTRA=()
else
  EXTRA=(--synthetic --no-inject)
fi

echo "Starting peerdesk-server on port ${PORT} (${MODE})"
"${BUILD}/peerdesk-server" --port "${PORT}" --data-dir "${DATA}" --user jordan --password peerdesk "${EXTRA[@]}" &
SID=$!
trap 'kill "${SID}" 2>/dev/null || true' EXIT
sleep 0.5
export PEERDESK_CA_FILE="${DATA}/server.crt"
echo "Starting peerdesk-client (jordan / peerdesk) with PEERDESK_CA_FILE=${PEERDESK_CA_FILE}"
exec "${CLIENT}" --ca-file "${PEERDESK_CA_FILE}"
