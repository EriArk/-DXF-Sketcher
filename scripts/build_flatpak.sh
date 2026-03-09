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

for cmd in flatpak flatpak-builder meson ldconfig python3; do
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
python3 "$ROOT_DIR/scripts/bundle_boxes_runtime.py" --dest "$STAGE_DIR/app/share/dxfsketcher/pyvendor"

if [ ! -x "$STAGE_DIR/app/bin/dxfsketcher" ]; then
    echo "Expected flatpak payload binary not found: $STAGE_DIR/app/bin/dxfsketcher"
    exit 1
fi

VENDORED_LIB_DIR="$STAGE_DIR/app/lib"
mkdir -p "$VENDORED_LIB_DIR"

resolve_library_path() {
    local soname="$1"
    ldconfig -p | awk -v name="$soname" '
        $1 == name && !found { print $NF; found=1 }
        END { if (!found) exit 1 }
    '
}

copy_library_with_soname() {
    local soname="$1"
    local lib_path
    local real_path
    local soname_file
    local real_file

    lib_path="$(resolve_library_path "$soname")"
    if [ -z "$lib_path" ]; then
        echo "Unable to resolve shared library: $soname"
        exit 1
    fi

    real_path="$(readlink -f "$lib_path")"
    soname_file="$(basename "$lib_path")"
    real_file="$(basename "$real_path")"

    cp -f "$real_path" "$VENDORED_LIB_DIR/$real_file"
    if [ "$soname_file" != "$real_file" ]; then
        ln -sf "$real_file" "$VENDORED_LIB_DIR/$soname_file"
    fi
}

# GNOME runtime does not ship gtkmm/cairomm/sigc++ stack required by dxfsketcher.
for lib in \
    libgtkmm-4.0.so.0 \
    libgiomm-2.68.so.1 \
    libglibmm-2.68.so.1 \
    libcairomm-1.16.so.1 \
    libpangomm-2.48.so.1 \
    libsigc-3.0.so.0 \
    libspnav.so.0 \
    libstdc++.so.6 \
    libgcc_s.so.1
do
    copy_library_with_soname "$lib"
done

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
