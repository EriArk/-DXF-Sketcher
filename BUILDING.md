# Building DXF Sketcher

[Back to the user guide](README.md)

## Build From Source

```bash
meson setup build-sketcher -Dsketcher_only=true
ninja -C build-sketcher dxfsketcher
./build-sketcher/dxfsketcher
```

To build release packages locally:

```bash
bash scripts/build_deb.sh build-sketcher
bash scripts/build_rpm.sh build-sketcher
bash scripts/build_appimage.sh build-sketcher
```

## AppImage Update Metadata

`scripts/build_appimage.sh` now supports embedding AppImage update information for GitHub Releases / Gear Lever style update flows.

- If the checkout has a GitHub `origin`, the script auto-derives a `gh-releases-zsync` update source.
- You can override it with `APPIMAGE_UPDATE_INFORMATION` or `UPD_INFO`.
- You can also force a specific GitHub target with `APPIMAGE_GITHUB_REPO=owner/repo`.
- By default the release channel is `latest`; override it with `APPIMAGE_GITHUB_RELEASE`.

Example:

```bash
APPIMAGE_GITHUB_REPO=EriArk/-DXF-Sketcher bash scripts/build_appimage.sh build-sketcher
./dist/appimage/dxfsketcher-1.5.3-x86_64.AppImage --appimage-updateinformation
```

If `appimagetool` has zsync support available, it also writes a matching `.zsync` companion file next to the `.AppImage`.
For GitHub releases, upload both files together:

- `dxfsketcher-<version>-<arch>.AppImage`
- `dxfsketcher-<version>-<arch>.AppImage.zsync`
