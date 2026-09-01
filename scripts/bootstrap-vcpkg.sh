#!/usr/bin/env bash
# Clone vcpkg next to this repo (or reuse VCPKG_ROOT) and configure PeerDesk.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VCPKG_ROOT="${VCPKG_ROOT:-${ROOT}/.vcpkg}"
if [[ ! -x "${VCPKG_ROOT}/vcpkg" ]]; then
  git clone --depth 1 https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"
  "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
fi
export VCPKG_ROOT
cmake --preset vcpkg
cmake --build --preset vcpkg -j
echo "Built with vcpkg at ${VCPKG_ROOT}"
