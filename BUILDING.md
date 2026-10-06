# Building DXF Sketcher

[Back to the user guide](README.md) · [Contributing](CONTRIBUTING.md)

## Toolchain and prerequisites

The application uses C++20, Meson **1.3 or newer**, Ninja, Python 3, GTK **4.10 or newer**, and gtkmm **4.10 or newer**. The normal product configuration is `-Dsketcher_only=true`; OpenCASCADE is not required in that configuration. Meson defaults to the fuller upstream configuration if you omit the option.

The Linux packaging recipe uses **Ubuntu 24.04**. Install its build and packaging dependencies with:

```bash
sudo apt update
sudo apt install build-essential cmake git meson ninja-build pkg-config \
  libgtkmm-4.0-dev libglm-dev libeigen3-dev libspnav-dev \
  libgdk-pixbuf-2.0-dev libglib2.0-bin libgtk-4-bin \
  python3 python3-gi-cairo gir1.2-rsvg-2.0 \
  desktop-file-utils dpkg-dev imagemagick patchelf rpm
git clone https://github.com/EriArk/-DXF-Sketcher.git
cd -- -DXF-Sketcher
```

Packages for older distributions may not meet the GTK/Meson minimums. These commands document the existing Linux recipe; they do not claim a fresh build on every distribution.

## Compile and launch

```bash
meson setup build-sketcher -Dsketcher_only=true
meson compile -C build-sketcher
./build-sketcher/dxfsketcher
```

For an existing build directory, use `meson setup build-sketcher -Dsketcher_only=true --reconfigure`. Open an [example drawing](examples) after launching.

## Local checks

```bash
git diff --check
meson compile -C build-sketcher
```

The formatter uses **clang-format 18**. `bash scripts/run_clangformat.sh` modifies source files in place; run it deliberately and review its diff. The current source has a known formatting backlog, so a whole-tree style check is not yet a clean baseline.

There is no complete automated DXF round-trip test suite yet. Record manual open/edit/Save As/reopen checks and exercise any generator affected by a change. See [development notes](DEVELOPMENT.md).

## Packaging

Run packaging scripts from the repository root after building:

```bash
bash scripts/build_deb.sh build-sketcher
bash scripts/build_rpm.sh build-sketcher
bash scripts/build_appimage.sh build-sketcher
```

Outputs are written below `dist/`. AppImage packaging also needs `linuxdeploy`, its GTK plugin and `appimagetool` on PATH; the retained [Linux workflow](.github/workflows/linux-packages.yml) records the tooling setup. Review external tool downloads before running them locally.

## Windows and macOS

The Windows recipe uses **MSYS2 MINGW64** with GCC, gtkmm, GLM, Eigen, Meson, Python and Cairo/GObject support. See the dependency and compatibility-package steps in [.github/workflows/win32.yml](.github/workflows/win32.yml); this is a build reference, not an active hosted job. Compile with the same Meson option, then use `make_bindist.sh` to collect runtime files. MSI packaging additionally uses WiX 3 tools through `wix/install.bat`.

macOS packaging remains experimental and has no current downloadable release. Historical recipes are in `scripts/build_macos.sh`, `scripts/bundle_macos.sh` and the macOS workflow.

## Automation policy

GitHub Actions is disabled at repository level by maintainer decision (6 October 2026). Builds, checks and release publication are performed explicitly; pushing a commit or tag does not build or publish installers.

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
