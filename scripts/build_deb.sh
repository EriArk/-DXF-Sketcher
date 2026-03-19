#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-build-sketcher}"
PKG_NAME="dxfsketcher"
VERSION="$(python3 -c 'import version; print(version.string)' 2>/dev/null || echo "0.0.0")"
ARCH="$(dpkg --print-architecture)"
OUT_DIR="$ROOT_DIR/dist/deb"
STAGE_DIR="$OUT_DIR/pkgroot"

cd "$ROOT_DIR"

if ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "dpkg-deb is required but not found"
    exit 1
fi

if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory '$BUILD_DIR' not found"
    exit 1
fi

mkdir -p "$OUT_DIR"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"

# Debian packages should install under /usr, not /usr/local.
meson configure "$BUILD_DIR" --prefix /usr >/dev/null
meson compile -C "$BUILD_DIR" >/dev/null
meson install -C "$BUILD_DIR" --destdir "$STAGE_DIR"

BIN_PATH="$STAGE_DIR/usr/bin/$PKG_NAME"
if [ ! -x "$BIN_PATH" ]; then
    echo "Expected binary not found: $BIN_PATH"
    exit 1
fi

mkdir -p "$STAGE_DIR/DEBIAN"

DEPENDS="$(
    ldd "$BIN_PATH" \
    | awk '/=> \//{print $3}' \
    | sort -u \
    | while read -r so; do
        dpkg-query -S "$so" 2>/dev/null \
        | awk -F: '{print $1}' \
        | tr ',' '\n'
    done \
    | sed '/^$/d' \
    | sed 's/^[[:space:]]*//; s/[[:space:]]*$//' \
    | sed 's/:[[:alnum:]_+-]\+$//' \
    | awk '/^[a-z0-9][a-z0-9+.-]+$/' \
    | sort -u \
    | paste -sd',' - \
    | sed 's/,/, /g'
)"

INSTALLED_SIZE="$(du -sk "$STAGE_DIR" | awk '{print $1}')"

cat > "$STAGE_DIR/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $VERSION
Section: graphics
Priority: optional
Architecture: $ARCH
Maintainer: DXF Sketcher contributors
Installed-Size: $INSTALLED_SIZE
Depends: ${DEPENDS:-libc6}
Homepage: https://github.com/EriArk/-DXF-Sketcher
Description: DXF Sketcher
 Practical 2D DXF sketch editor with parametric constraints and direct editing workflow.
EOF

cat > "$STAGE_DIR/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q /usr/share/icons/hicolor || true
fi
exit 0
EOF

cat > "$STAGE_DIR/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q /usr/share/icons/hicolor || true
fi
exit 0
EOF

chmod 0755 "$STAGE_DIR/DEBIAN/postinst" "$STAGE_DIR/DEBIAN/postrm"

OUT_FILE="$OUT_DIR/${PKG_NAME}_${VERSION}_${ARCH}.deb"
rm -f "$OUT_FILE"
dpkg-deb --build "$STAGE_DIR" "$OUT_FILE" >/dev/null

echo "Built package: $OUT_FILE"
