#!/usr/bin/env bash
set -euo pipefail

SELF_PATH="${BASH_SOURCE[0]}"
if command -v readlink >/dev/null 2>&1; then
    SELF_PATH="$(readlink -f "$SELF_PATH" 2>/dev/null || printf '%s' "$SELF_PATH")"
fi

APPDIR="${APPDIR:-$(cd "$(dirname "$SELF_PATH")" && pwd)}"
USR_DIR="$APPDIR/usr"
BIN_PATH="$USR_DIR/bin/dxfsketcher"
LIB_DIR="$USR_DIR/lib"
SHARE_DIR="$USR_DIR/share"
PIXBUF_CACHE_TEMPLATE="$LIB_DIR/gdk-pixbuf-loaders.cache.template"
SCHEMA_DIR="$SHARE_DIR/glib-2.0/schemas"

prepend_env_path() {
    local var_name="$1"
    local entry="$2"
    local current_value="${!var_name:-}"

    if [ -z "$current_value" ]; then
        printf -v "$var_name" '%s' "$entry"
    else
        printf -v "$var_name" '%s:%s' "$entry" "$current_value"
    fi
    export "$var_name"
}

if [ ! -x "$BIN_PATH" ]; then
    echo "AppImage launcher could not find executable: $BIN_PATH" >&2
    exit 1
fi

prepend_env_path PATH "$USR_DIR/bin"
prepend_env_path LD_LIBRARY_PATH "$LIB_DIR"
prepend_env_path XDG_DATA_DIRS "$SHARE_DIR"

if [ -f "$SCHEMA_DIR/gschemas.compiled" ]; then
    export GSETTINGS_SCHEMA_DIR="$SCHEMA_DIR"
fi

if [ -f "$PIXBUF_CACHE_TEMPLATE" ]; then
    runtime_root="${XDG_RUNTIME_DIR:-${TMPDIR:-/tmp}}/dxfsketcher-appimage"
    mkdir -p "$runtime_root"

    pixbuf_cache="$runtime_root/gdk-pixbuf-loaders.cache"
    appdir_escaped="$(printf '%s\n' "$APPDIR" | sed 's/[&|]/\\&/g')"
    sed "s|@APPDIR@|$appdir_escaped|g" "$PIXBUF_CACHE_TEMPLATE" >"$pixbuf_cache"

    export GDK_PIXBUF_MODULE_FILE="$pixbuf_cache"
fi

exec "$BIN_PATH" "$@"
