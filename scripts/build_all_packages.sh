#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-build-sketcher}"

cd "$ROOT_DIR"

echo "Building .deb..."
bash "$ROOT_DIR/scripts/build_deb.sh" "$BUILD_DIR"

echo "Building .rpm..."
bash "$ROOT_DIR/scripts/build_rpm.sh" "$BUILD_DIR"

echo "Building .flatpak bundle..."
bash "$ROOT_DIR/scripts/build_flatpak.sh" "$BUILD_DIR"

echo "Building .AppImage..."
bash "$ROOT_DIR/scripts/build_appimage.sh" "$BUILD_DIR"

echo
echo "All package artifacts:"
find "$ROOT_DIR/dist/deb" "$ROOT_DIR/dist/rpm" "$ROOT_DIR/dist/flatpak" "$ROOT_DIR/dist/appimage" \
    -maxdepth 1 -type f \( -name '*.deb' -o -name '*.rpm' -o -name '*.flatpak' -o -name '*.AppImage' \) \
    | sort
