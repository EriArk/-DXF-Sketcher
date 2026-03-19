#!/usr/bin/env bash

set -euo pipefail
shopt -s extglob

BUILD_DIR="${1:-build-sketcher}"
app_dir="./dist/DXF Sketcher.app"
bin_dir="$app_dir/Contents/MacOS"
res_dir="$app_dir/Contents/Resources"
lib_dir="$res_dir/lib"
brew_prefix="$(brew --prefix)"

mkdir -p "$bin_dir" "$lib_dir" "$res_dir/share/icons" "$res_dir/share/glib-2.0"
cp "$BUILD_DIR/dxfsketcher" "$bin_dir/dxfsketcher-bin"
cp Info.plist "$app_dir/Contents"
cp macos-launcher.sh "$bin_dir/dxfsketcher"
chmod +x "$bin_dir/dxfsketcher"
cp src/icons/dune3d.icns "$res_dir"

echo "APPL????" > "$app_dir/Contents/PkgInfo"

cp -r $brew_prefix/share/icons/Adwaita "$res_dir/share/icons"
cp -r $brew_prefix/share/glib-2.0/schemas "$res_dir/share/glib-2.0"

python3 scripts/bundle_boxes_runtime.py --dest "$res_dir/share/dxfsketcher/pyvendor"

loaders_dir="$lib_dir/gdk-pixbuf-2.0/2.10.0/loaders"
rm -rf "$loaders_dir" "$loaders_dir.cache"
mkdir -p "$loaders_dir"

GDK_PIXBUF_MODULEDIR=$brew_prefix/lib/gdk-pixbuf-2.0/2.10.0/loaders gdk-pixbuf-query-loaders | while read item
do
  unquoted="${item#\"}"
  unquoted="${unquoted%\"}"
  if [[ -f "$unquoted" ]] ;
  then
    cp "$unquoted" "$loaders_dir"
    echo "\"@executable_path/../Resources/lib/gdk-pixbuf-2.0/2.10.0/loaders/$(basename "$unquoted")\"" >> "$loaders_dir.cache"
    dylibbundler -of -b -x "$loaders_dir/$(basename "$unquoted")" -d "$lib_dir" -p @executable_path/../Resources/lib/ -s $brew_prefix/lib
  else
    echo "$item" >> "$loaders_dir.cache"
  fi
done
dylibbundler -of -b -x  "$bin_dir/dxfsketcher-bin" -d "$lib_dir" -p @executable_path/../Resources/lib/
codesign --force --deep --preserve-metadata=entitlements,requirements,flags,runtime --sign - "$app_dir"
