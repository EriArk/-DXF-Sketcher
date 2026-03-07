#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-build-sketcher}"
APP_ID="io.github.eriark.dxfsketcher"
VERSION="$(python3 -c 'import version; print(version.string)' 2>/dev/null || echo "0.0.0")"
RUNTIME_VERSION="${FLATPAK_RUNTIME_VERSION:-49}"
BRANCH="${FLATPAK_BRANCH:-stable}"

OUT_DIR="$ROOT_DIR/dist/flatpak"
STAGE_DIR="$OUT_DIR/pkgroot"
BUILD_STATE_DIR="$OUT_DIR/build-state"
REPO_DIR="$OUT_DIR/repo"
MANIFEST="$OUT_DIR/${APP_ID}.json"
BUNDLE_FILE="$OUT_DIR/${APP_ID}-${VERSION}-${BRANCH}.flatpak"

cd "$ROOT_DIR"

for cmd in flatpak flatpak-builder meson; do
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
rm -rf "$STAGE_DIR" "$BUILD_STATE_DIR" "$REPO_DIR"
mkdir -p "$STAGE_DIR" "$BUILD_STATE_DIR" "$REPO_DIR"

meson configure "$BUILD_DIR" --prefix /app >/dev/null
meson compile -C "$BUILD_DIR" >/dev/null
meson install -C "$BUILD_DIR" --destdir "$STAGE_DIR"

if [ ! -x "$STAGE_DIR/app/bin/dxfsketcher" ]; then
    echo "Expected flatpak payload binary not found: $STAGE_DIR/app/bin/dxfsketcher"
    exit 1
fi

cat > "$MANIFEST" <<EOF
{
  "app-id": "$APP_ID",
  "branch": "$BRANCH",
  "runtime": "org.gnome.Platform",
  "runtime-version": "$RUNTIME_VERSION",
  "sdk": "org.gnome.Sdk",
  "command": "dxfsketcher",
  "finish-args": [
    "--share=ipc",
    "--socket=fallback-x11",
    "--socket=wayland",
    "--device=dri",
    "--filesystem=home",
    "--talk-name=org.freedesktop.FileManager1",
    "--env=GDK_BACKEND=x11,wayland"
  ],
  "modules": [
    {
      "name": "dxfsketcher",
      "buildsystem": "simple",
      "build-commands": [
        "cp -a . /app"
      ],
      "sources": [
        {
          "type": "dir",
          "path": "$STAGE_DIR/app"
        }
      ]
    }
  ]
}
EOF

flatpak-builder --force-clean --repo="$REPO_DIR" "$BUILD_STATE_DIR" "$MANIFEST" >/dev/null
flatpak build-bundle "$REPO_DIR" "$BUNDLE_FILE" "$APP_ID" "$BRANCH" >/dev/null

echo "Built package: $BUNDLE_FILE"
