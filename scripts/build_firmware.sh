#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FW="$ROOT/firmware/esp32c3"

if ! command -v idf.py >/dev/null 2>&1; then
  if [[ -n "${IDF_PATH:-}" && -f "$IDF_PATH/export.sh" ]]; then
    # shellcheck source=/dev/null
    . "$IDF_PATH/export.sh" >/dev/null
  elif [[ -f "$HOME/esp/esp-idf-v5.3.5/export.sh" ]]; then
    # shellcheck source=/dev/null
    . "$HOME/esp/esp-idf-v5.3.5/export.sh" >/dev/null
  elif [[ -f "$HOME/esp/esp-idf/export.sh" ]]; then
    # shellcheck source=/dev/null
    . "$HOME/esp/esp-idf/export.sh" >/dev/null
  else
    echo "ESP-IDF is not available. Install ESP-IDF or set IDF_PATH; this script only builds and never flashes." >&2
    exit 127
  fi
fi

cd "$FW"
idf.py set-target esp32c3
idf.py build
