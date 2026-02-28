# DXF Sketcher

DXF Sketcher is a lightweight 2D sketch editor forked from Dune3D.

This fork is focused on a simple DXF workflow:
- open one or multiple `*.dxf` files as sketches
- open a folder and load all DXF files from it
- edit active sketches in 2D
- save/export per sketch

3D CAD features (solid modeling, STEP workflow, etc.) are intentionally not the focus here.

## Current workflow

- `Open file`: import one or more DXF files
- `Open folder`: import all DXF files from selected folder
- Each imported file is represented as its own sketch
- `Save` writes the current sketch to its assigned file path
- `Save As` lets you choose `DXF` or `SVG`

## Build

Requirements:
- `meson`
- `ninja`
- GTK4 / gtkmm4 toolchain used by the original project

Configure and build sketcher-only mode:

```bash
meson setup build-sketcher -Dsketcher_only=true
meson compile -C build-sketcher
```

Run:

```bash
./build-sketcher/dune3d
```

## Notes

- Workspace helper files (`.wrk`) are redirected to cache in sketcher mode, not into your project folders.
- This is an actively customized fork. Behavior may differ significantly from upstream Dune3D.

## Upstream credits

This project is based on [dune3d/dune3d](https://github.com/dune3d/dune3d). Big thanks to the original authors.

## License

Same as upstream repository license (see [LICENSE](LICENSE)).
