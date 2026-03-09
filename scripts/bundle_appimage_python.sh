#!/usr/bin/env bash
set -euo pipefail

APPDIR="${1:-dist/appimage/AppDir}"

python3 scripts/bundle_boxes_runtime.py --dest "$APPDIR/usr/share/dxfsketcher/pyvendor"
