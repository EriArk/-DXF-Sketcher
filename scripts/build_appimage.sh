#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-build-sketcher}"
PKG_NAME="dxfsketcher"
VERSION="$(python3 -c 'import version; print(version.string)' 2>/dev/null || echo "0.0.0")"
ARCH="${APPIMAGE_ARCH:-$(uname -m)}"

OUT_DIR="$ROOT_DIR/dist/appimage"
APPDIR="$OUT_DIR/AppDir"
ICON_SOURCE="$ROOT_DIR/src/icons/scalable/apps/logo.png"
ICON_FILE="$OUT_DIR/dxfsketcher-512.png"
OUT_FILE="$OUT_DIR/${PKG_NAME}-${VERSION}-${ARCH}.AppImage"

cd "$ROOT_DIR"

for cmd in meson linuxdeploy appimagetool convert; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Required command not found: $cmd"
        exit 1
    fi
done

if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory '$BUILD_DIR' not found"
    exit 1
fi

mkdir -p "$OUT_DIR"
rm -rf "$APPDIR"
mkdir -p "$APPDIR"

meson configure "$BUILD_DIR" --prefix /usr >/dev/null
meson compile -C "$BUILD_DIR" >/dev/null
meson install -C "$BUILD_DIR" --destdir "$APPDIR"

BIN_PATH="$APPDIR/usr/bin/$PKG_NAME"
DESKTOP_FILE="$APPDIR/usr/share/applications/io.github.eriark.dxfsketcher.desktop"

if [ ! -x "$BIN_PATH" ]; then
    echo "Expected binary not found: $BIN_PATH"
    exit 1
fi
if [ ! -f "$DESKTOP_FILE" ]; then
    echo "Expected desktop file not found: $DESKTOP_FILE"
    exit 1
fi
if [ ! -f "$ICON_SOURCE" ]; then
    echo "Expected icon source not found: $ICON_SOURCE"
    exit 1
fi

export ARCH
export VERSION
export APPIMAGE_EXTRACT_AND_RUN=1

pushd "$OUT_DIR" >/dev/null
rm -f ./*.AppImage

convert "$ICON_SOURCE" -resize 512x512 "$ICON_FILE"

run_linuxdeploy() {
    local use_gtk_plugin="$1"
    local args=(
        --appdir "$APPDIR"
        -e "$BIN_PATH"
        -d "$DESKTOP_FILE"
        -i "$ICON_FILE"
        --output appimage
    )
    if [ "$use_gtk_plugin" = "yes" ]; then
        args+=(--plugin gtk)
    fi
    linuxdeploy "${args[@]}"
}

if ! run_linuxdeploy "yes"; then
    echo "linuxdeploy gtk plugin failed, retrying without gtk plugin..."
    rm -f ./*.AppImage
    run_linuxdeploy "no"
fi

GENERATED_FILE="$(ls -1t ./*.AppImage 2>/dev/null | head -n1 || true)"
if [ -z "$GENERATED_FILE" ]; then
    echo "linuxdeploy did not generate an AppImage"
    exit 1
fi
mv -f "$GENERATED_FILE" "$OUT_FILE"
popd >/dev/null

echo "Built package: $OUT_FILE"
