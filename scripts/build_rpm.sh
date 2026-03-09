#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-build-sketcher}"
PKG_NAME="dxfsketcher"
VERSION="$(python3 -c 'import version; print(version.string)' 2>/dev/null || echo "0.0.0")"
ARCH="$(rpm --eval '%{_arch}')"

OUT_DIR="$ROOT_DIR/dist/rpm"
STAGE_DIR="$OUT_DIR/pkgroot"
SOURCE_TARBALL="${PKG_NAME}-${VERSION}-root.tar.gz"

cd "$ROOT_DIR"

for cmd in rpmbuild rpm meson tar; do
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
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"

RPMROOT="$(mktemp -d /tmp/${PKG_NAME}-rpmbuild.XXXXXX)"
SOURCES_DIR="$RPMROOT/SOURCES"
SPECS_DIR="$RPMROOT/SPECS"
SPEC_FILE="$SPECS_DIR/${PKG_NAME}.spec"
trap 'rm -rf "$RPMROOT"' EXIT
mkdir -p "$SOURCES_DIR" "$SPECS_DIR" "$RPMROOT/BUILD" "$RPMROOT/RPMS" "$RPMROOT/SRPMS" "$RPMROOT/BUILDROOT"

meson configure "$BUILD_DIR" --prefix /usr >/dev/null
meson compile -C "$BUILD_DIR" >/dev/null
meson install -C "$BUILD_DIR" --destdir "$STAGE_DIR"

BIN_PATH="$STAGE_DIR/usr/bin/$PKG_NAME"
if [ ! -x "$BIN_PATH" ]; then
    echo "Expected binary not found: $BIN_PATH"
    exit 1
fi

tar -C "$STAGE_DIR" -czf "$SOURCES_DIR/$SOURCE_TARBALL" usr

cat > "$SPEC_FILE" <<EOF
%global debug_package %{nil}

Name:           $PKG_NAME
Version:        $VERSION
Release:        1%{?dist}
Summary:        DXF Sketcher
License:        GPL-3.0-or-later
URL:            https://github.com/EriArk/-DXF-Sketcher
Source0:        $SOURCE_TARBALL
BuildArch:      $ARCH

%description
Practical 2D DXF sketch editor with parametric constraints and direct editing workflow.

%prep
%setup -q -c -T
tar -xzf "%{SOURCE0}"

%build

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}
cp -a usr %{buildroot}/

%post
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q /usr/share/icons/hicolor || true
fi

%postun
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q /usr/share/icons/hicolor || true
fi

%files
/usr/bin/$PKG_NAME
/usr/share/applications/io.github.eriark.dxfsketcher.desktop
/usr/share/dxfsketcher
/usr/share/metainfo/io.github.eriark.dxfsketcher.metainfo.xml
/usr/share/icons/hicolor/scalable/apps/dxfsketcher.png
EOF

rpmbuild -bb "$SPEC_FILE" --define "_topdir $RPMROOT" >/dev/null

find "$RPMROOT/RPMS" -type f -name '*.rpm' -exec cp -f {} "$OUT_DIR/" \;

echo "Built packages:"
find "$OUT_DIR" -maxdepth 1 -type f -name '*.rpm' -print | sort
