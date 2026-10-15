#!/usr/bin/env bash
# Build RUMOR Standalone (and optional VST3/AU) on macOS.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
  -DRUMOR_WITH_PAULXSTRETCH=ON \
  -DRUMOR_BUILD_PLUGIN=ON \
  -DRUMOR_BUILD_TESTS=ON

cmake --build "$BUILD_DIR" --target Rumor_Standalone -j"$JOBS"

APP="$BUILD_DIR/Rumor_artefacts/Release/Standalone/RUMOR.app"
if [[ -d "$APP" ]]; then
  echo "OK: $APP"
else
  # Xcode/multi-config layouts sometimes omit Release/
  APP="$(find "$BUILD_DIR/Rumor_artefacts" -name 'RUMOR.app' -type d | head -1 || true)"
  echo "OK: ${APP:-built (path varies)}"
fi
