#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-build-sketcher}"
PKG_NAME="dxfsketcher"
VERSION="$(python3 -c 'import version; print(version.string)' 2>/dev/null || echo "0.0.0")"
ARCH="${APPIMAGE_ARCH:-$(uname -m)}"

OUT_DIR="$ROOT_DIR/dist/appimage"
APPDIR="$OUT_DIR/AppDir"
BASE_APPDIR="$OUT_DIR/AppDir.base"
ICON_SOURCE="$ROOT_DIR/src/icons/scalable/apps/logo.png"
ICON_FILE="$OUT_DIR/dxfsketcher-512.png"
OUT_FILE="$OUT_DIR/${PKG_NAME}-${VERSION}-${ARCH}.AppImage"
APP_RUN_SOURCE="$ROOT_DIR/scripts/appimage-apprun.sh"

cd "$ROOT_DIR"

THEME_SYMBOLIC_ICON_NAMES=(
    applications-engineering-symbolic
    applications-graphics-symbolic
    dialog-error-symbolic
    dialog-information-symbolic
    dialog-warning-symbolic
    document-edit-symbolic
    document-new-symbolic
    document-open-symbolic
    document-revert-symbolic
    document-save-as-symbolic
    document-save-symbolic
    edit-copy-symbolic
    edit-cut-symbolic
    edit-redo-symbolic
    edit-select-all-symbolic
    edit-undo-symbolic
    face-worried-symbolic
    folder-open-symbolic
    folder-symbolic
    go-down-symbolic
    go-next-symbolic
    go-previous-symbolic
    go-up-symbolic
    help-browser-symbolic
    image-x-generic-symbolic
    insert-link-symbolic
    list-add-symbolic
    list-remove-symbolic
    object-flip-horizontal-symbolic
    object-select-symbolic
    open-menu-symbolic
    package-x-generic-symbolic
    pan-up-symbolic
    sidebar-show-symbolic
    system-lock-screen-symbolic
    text-x-generic-symbolic
    tool-brush-symbolic
    view-column-symbolic
    view-grid-symbolic
    view-list-bullet-symbolic
    view-refresh-symbolic
    window-close-symbolic
)

find_svg_loader() {
    find /usr/lib /lib -path '*/gdk-pixbuf-2.0/*/loaders/libpixbufloader_svg.so' -type f 2>/dev/null | sort | head -n 1
}

find_gdk_pixbuf_query_loaders() {
    if command -v gdk-pixbuf-query-loaders >/dev/null 2>&1; then
        command -v gdk-pixbuf-query-loaders
        return 0
    fi
    local loader_source="${1:-}"
    local candidate

    if [ -n "$loader_source" ]; then
        candidate="${loader_source%/loaders/libpixbufloader_svg.so}"
        candidate="${candidate%/*}/gdk-pixbuf-query-loaders"
        if [ -x "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    fi

    find /usr/lib /lib -path "*/$(uname -m)-linux-gnu/gdk-pixbuf-2.0/gdk-pixbuf-query-loaders" -type f 2>/dev/null | sort | head -n 1
}

SVG_LOADER_SOURCE="$(find_svg_loader || true)"
GDK_PIXBUF_QUERY_LOADERS_BIN="$(find_gdk_pixbuf_query_loaders "$SVG_LOADER_SOURCE" || true)"

cleanup_base_appdir() {
    rm -rf "$BASE_APPDIR"
}

find_host_theme_icon() {
    local icon_name="$1"
    local theme_dir
    local found
    for theme_dir in \
        /usr/share/icons/Adwaita \
        /usr/share/icons/Papirus \
        /usr/share/icons/Papirus-Dark \
        /usr/share/icons/Papirus-Light \
        /usr/share/icons/ePapirus \
        /usr/share/icons/ePapirus-Dark
    do
        if [ ! -d "$theme_dir" ]; then
            continue
        fi
        found="$(find "$theme_dir" -type f -name "${icon_name}.svg" 2>/dev/null | sort | head -n 1)"
        if [ -n "$found" ]; then
            printf '%s\n' "$found"
            return 0
        fi
    done
    return 1
}

bundle_theme_icon_fallbacks() {
    local appdir="$1"
    local hicolor_dir="$appdir/usr/share/icons/hicolor"
    local adwaita_dir="$appdir/usr/share/icons/Adwaita"
    local icon_name
    local src
    local rel
    local dst

    mkdir -p "$hicolor_dir" "$adwaita_dir"

    if [ -f /usr/share/icons/hicolor/index.theme ]; then
        cp /usr/share/icons/hicolor/index.theme "$hicolor_dir/index.theme"
    fi
    if [ -f /usr/share/icons/Adwaita/index.theme ]; then
        cp /usr/share/icons/Adwaita/index.theme "$adwaita_dir/index.theme"
    fi

    for icon_name in "${THEME_SYMBOLIC_ICON_NAMES[@]}"; do
        src="$(find_host_theme_icon "$icon_name" || true)"
        if [ -z "$src" ]; then
            echo "Warning: theme icon source not found for $icon_name"
            continue
        fi

        rel="${src#*/symbolic/}"
        if [ "$rel" = "$src" ]; then
            echo "Warning: unsupported theme icon path for $icon_name: $src"
            continue
        fi

        dst="$adwaita_dir/symbolic/$rel"
        mkdir -p "$(dirname "$dst")"
        cp "$src" "$dst"
    done
}

bundle_gtk_runtime_fallback_assets() {
    local appdir="$1"
    local schemas_src="/usr/share/glib-2.0/schemas"
    local schemas_dst="$appdir/usr/share/glib-2.0/schemas"
    local gtk_share_src="/usr/share/gtk-4.0"
    local gtk_share_dst="$appdir/usr/share/gtk-4.0"

    # linuxdeploy's GTK plugin can fail on some hosts; keep essential GTK runtime
    # data available so the fallback AppImage stays closer to the plugin path.
    if [ -f "$schemas_src/gschemas.compiled" ] && [ ! -f "$schemas_dst/gschemas.compiled" ]; then
        mkdir -p "$schemas_dst"
        cp "$schemas_src/gschemas.compiled" "$schemas_dst/gschemas.compiled"
    fi

    if [ -d "$gtk_share_src" ] && [ ! -d "$gtk_share_dst" ]; then
        mkdir -p "$appdir/usr/share"
        cp -a "$gtk_share_src" "$gtk_share_dst"
    fi
}

prepare_base_appdir() {
    rm -rf "$APPDIR" "$BASE_APPDIR"
    mkdir -p "$APPDIR"

    meson configure "$BUILD_DIR" --prefix /usr >/dev/null
    meson compile -C "$BUILD_DIR" >/dev/null
    meson install -C "$BUILD_DIR" --destdir "$APPDIR"
    bash "$ROOT_DIR/scripts/bundle_appimage_python.sh" "$APPDIR"
    bundle_theme_icon_fallbacks "$APPDIR"

    local bin_path="$APPDIR/usr/bin/$PKG_NAME"
    local desktop_file="$APPDIR/usr/share/applications/io.github.eriark.dxfsketcher.desktop"

    if [ ! -x "$bin_path" ]; then
        echo "Expected binary not found: $bin_path"
        exit 1
    fi
    if [ ! -f "$desktop_file" ]; then
        echo "Expected desktop file not found: $desktop_file"
        exit 1
    fi
    if [ ! -f "$ICON_SOURCE" ]; then
        echo "Expected icon source not found: $ICON_SOURCE"
        exit 1
    fi
    if [ ! -f "$APP_RUN_SOURCE" ]; then
        echo "Expected AppRun source not found: $APP_RUN_SOURCE"
        exit 1
    fi

    mv "$APPDIR" "$BASE_APPDIR"
}

restore_appdir_from_base() {
    rm -rf "$APPDIR"
    cp -a "$BASE_APPDIR" "$APPDIR"
}

ensure_appstream_compat_alias() {
    local metainfo_dir="$APPDIR/usr/share/metainfo"
    local canonical_file="$metainfo_dir/io.github.eriark.dxfsketcher.metainfo.xml"
    local appdata_alias="$metainfo_dir/io.github.eriark.dxfsketcher.appdata.xml"

    if [ ! -f "$canonical_file" ]; then
        echo "Warning: AppStream metainfo file not found for AppImage compatibility alias: $canonical_file"
        return 0
    fi

    ln -sfn "$(basename "$canonical_file")" "$appdata_alias"
}

generate_pixbuf_loader_cache_template() {
    local deployed_loader
    local cache_template
    local loader_target_placeholder

    if [ -z "$SVG_LOADER_SOURCE" ]; then
        echo "Warning: SVG pixbuf loader source not found; AppImage icon fallbacks may remain incomplete."
        return 0
    fi
    if [ -z "$GDK_PIXBUF_QUERY_LOADERS_BIN" ] || [ ! -x "$GDK_PIXBUF_QUERY_LOADERS_BIN" ]; then
        echo "Warning: gdk-pixbuf-query-loaders not found; AppImage icon loader cache was not generated."
        return 0
    fi

    deployed_loader="$APPDIR/usr/lib/$(basename "$SVG_LOADER_SOURCE")"
    cache_template="$APPDIR/usr/lib/gdk-pixbuf-loaders.cache.template"

    if [ ! -f "$deployed_loader" ]; then
        echo "Warning: deployed SVG pixbuf loader not found: $deployed_loader"
        return 0
    fi

    loader_target_placeholder="@APPDIR@/usr/lib/$(basename "$SVG_LOADER_SOURCE")"

    "$GDK_PIXBUF_QUERY_LOADERS_BIN" "$SVG_LOADER_SOURCE" >"$cache_template"
    python3 - "$cache_template" "$APPDIR" <<'PY'
from pathlib import Path
import sys

cache_path = Path(sys.argv[1])
appdir = sys.argv[2]
cache_path.write_text(cache_path.read_text().replace(appdir, "@APPDIR@"))
PY
    python3 - "$cache_template" "$SVG_LOADER_SOURCE" "$loader_target_placeholder" <<'PY'
from pathlib import Path
import sys

cache_path = Path(sys.argv[1])
source_loader = sys.argv[2]
target_loader = sys.argv[3]
cache_path.write_text(cache_path.read_text().replace(source_loader, target_loader))
PY
}

trap cleanup_base_appdir EXIT

detect_github_repo() {
    local url
    url="$(git remote get-url origin 2>/dev/null || true)"
    if [ -z "$url" ]; then
        return 1
    fi

    case "$url" in
        https://github.com/*|http://github.com/*)
            url="${url#*github.com/}"
            ;;
        git@github.com:*)
            url="${url#git@github.com:}"
            ;;
        ssh://git@github.com/*)
            url="${url#ssh://git@github.com/}"
            ;;
        *)
            return 1
            ;;
    esac

    url="${url%.git}"
    if [[ "$url" != */* ]]; then
        return 1
    fi

    printf '%s\n' "$url"
}

resolve_update_information() {
    if [ -n "${APPIMAGE_UPDATE_INFORMATION:-}" ]; then
        printf '%s\n' "$APPIMAGE_UPDATE_INFORMATION"
        return 0
    fi
    if [ -n "${UPD_INFO:-}" ]; then
        printf '%s\n' "$UPD_INFO"
        return 0
    fi

    local repo="${APPIMAGE_GITHUB_REPO:-}"
    if [ -z "$repo" ]; then
        repo="$(detect_github_repo || true)"
    fi
    if [ -z "$repo" ]; then
        return 0
    fi

    local owner="${repo%%/*}"
    local name="${repo#*/}"
    local release_channel="${APPIMAGE_GITHUB_RELEASE:-latest}"
    printf 'gh-releases-zsync|%s|%s|%s|%s-*-%s.AppImage.zsync\n' "$owner" "$name" "$release_channel" "$PKG_NAME" "$ARCH"
}

for cmd in meson linuxdeploy appimagetool convert python3; do
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
prepare_base_appdir
restore_appdir_from_base

BIN_PATH="$APPDIR/usr/bin/$PKG_NAME"
DESKTOP_FILE="$APPDIR/usr/share/applications/io.github.eriark.dxfsketcher.desktop"

export ARCH
export VERSION
export APPIMAGE_EXTRACT_AND_RUN=1

pushd "$OUT_DIR" >/dev/null
rm -f ./*.AppImage ./*.zsync

convert "$ICON_SOURCE" -resize 512x512 "$ICON_FILE"

run_linuxdeploy() {
    local use_gtk_plugin="$1"
    local args=(
        --appdir "$APPDIR"
        -e "$BIN_PATH"
        -d "$DESKTOP_FILE"
        -i "$ICON_FILE"
        --custom-apprun "$APP_RUN_SOURCE"
    )
    if [ -n "$SVG_LOADER_SOURCE" ] && [ -f "$SVG_LOADER_SOURCE" ]; then
        args+=(-l "$SVG_LOADER_SOURCE")
    fi
    if [ "$use_gtk_plugin" = "yes" ]; then
        args+=(--plugin gtk)
    fi
    linuxdeploy "${args[@]}"
}

if ! run_linuxdeploy "yes"; then
    echo "linuxdeploy gtk plugin failed, retrying without gtk plugin..."
    rm -f ./*.AppImage ./*.zsync
    restore_appdir_from_base
    run_linuxdeploy "no"
fi

bundle_gtk_runtime_fallback_assets "$APPDIR"
generate_pixbuf_loader_cache_template
ensure_appstream_compat_alias

UPDATE_INFORMATION="$(resolve_update_information)"
APPIMAGETOOL_ARGS=()
if [ -n "$UPDATE_INFORMATION" ]; then
    echo "Embedding AppImage update information: $UPDATE_INFORMATION"
    APPIMAGETOOL_ARGS=(-u "$UPDATE_INFORMATION")
else
    echo "Building AppImage without embedded update information."
fi

appimagetool "${APPIMAGETOOL_ARGS[@]}" "$APPDIR" "$OUT_FILE"
popd >/dev/null

echo "Built package: $OUT_FILE"
if [ -f "${OUT_FILE}.zsync" ]; then
    echo "Built update companion: ${OUT_FILE}.zsync"
elif [ -n "$UPDATE_INFORMATION" ]; then
    echo "Note: no .zsync companion was generated; ensure appimagetool has zsync support available for release uploads."
fi
