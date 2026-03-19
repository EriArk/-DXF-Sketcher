#!/usr/bin/env bash
set -euo pipefail

DISTDIR="${1:-dist/dxfsketcher}"
MISSING=$(ldd "$DISTDIR/dxfsketcher.exe" | grep -vi windows | grep -vi "$DISTDIR" | grep -v "???")
if [ -z "$MISSING" ]
then
  echo "No missing DLLs"
else
  echo "Missing DLLs"
  echo "$MISSING"
  exit 1
fi
