#!/usr/bin/env bash
set -euo pipefail

DISTDIR="${1:-dist/dxfsketcher}"
LDD_OUTPUT="$(ldd "$DISTDIR/dxfsketcher.exe" 2>&1 || true)"
MISSING="$(printf '%s\n' "$LDD_OUTPUT" | grep -i "not found" || true)"

if [ -z "$MISSING" ]
then
  echo "No missing DLLs"
else
  echo "Missing DLLs"
  echo "$MISSING"
  exit 1
fi
