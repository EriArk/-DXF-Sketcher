# DXF Sketcher

DXF Sketcher is a practical 2D CAD sketch editor focused on fast DXF workflows.

This project is a customized fork of [dune3d](https://github.com/dune3d/dune3d), with most 3D/solid-model complexity removed from daily UI in favor of a cleaner sketching experience.

Repository: <https://github.com/EriArk/-DXF-Sketcher>

## What Changed In This Fork

### 1) Sketcher-only mode and simplified UX

- Added build option `-Dsketcher_only=true`.
- In sketcher mode, 3D-oriented tools and OpenCASCADE/STEP paths are disabled/hidden.
- Header/actions are redesigned around quick 2D operations.
- Compact sidebar + popover interactions for common controls.

### 2) DXF-first file workflow

- `Open file` supports opening multiple files in one action.
- `Open folder` imports all `*.dxf` files in the selected folder.
- Each imported DXF becomes its own sketch group.
- Duplicate imports are detected and redirected to the existing sketch.
- In folder mode, imported sketches are mapped to files in that folder.

### 3) Save/export behavior aligned to sketch workflow

- `Save` writes the current sketch directly to its mapped file path.
- `Save As` supports both `DXF` and `SVG` for the current sketch.
- Group names are synced with file names after save/export.

### 4) Expanded drawing and editing tools

- Rectangle tool: center/corner mode, square mode, rounded corners, corner radius controls.
- Circle tool: circle/oval mode, slice/sector mode, arc span controls.
- Regular polygon tool: quick side count editing, rounded corners, corner radius controls.
- Text tool: quick bold/italic toggles and font controls.
- Picture import upgraded: supports raster images and SVG path conversion for sketch insertion.

### 5) In-tool keyboard actions (new defaults + customization)

- Added dedicated in-tool action keymap (for tool-local actions like mode/radius/span toggles).
- Configurable from Preferences.
- Help window now shows both:
  - Main keys
  - In-tool keys

Examples of default in-tool keys:

- Rectangle: `m` mode, `s` square, `r` rounded, `[` / `]` corner radius
- Circle: `o` oval, `a` slice, `[` / `]` span
- Polygon: `+` / `-` sides, `r` rounded, `[` / `]` corner radius
- Text: `b` bold, `i` italic

### 6) Sketcher UX additions

- Header toolbar includes direct sketch tools (contour, rectangle, circle, polygon, text, picture import).
- Grid quick controls (toggle, snap, spacing).
- Symmetry quick controls (horizontal/vertical/radial with live settings).
- Sidebar visibility toggle (`Tab`).
- Updated branding, logo, icon, About/Help text.

### 7) Workspace metadata handling

In sketcher mode, workspace helper files are stored in user cache instead of project folders:

- Cache path: `~/.cache/dune3d-sketcher/workspaces/`
- Helper workspace files are cleaned on app shutdown (best effort).

## Quick Start

1. Start DXF Sketcher.
2. Use `Open file` to import one or more DXF files, or `Open folder` to load a full folder.
3. Edit the active sketch.
4. Use `Save` to write back to the mapped file, or `Save As` to export as DXF/SVG.

## Build

### Dependencies

Minimum toolchain:

- `meson`
- `ninja`
- `gtk4` / `gtkmm-4.0`
- standard C++20 compiler toolchain

### Build sketcher-only (recommended for this fork)

```bash
meson setup build-sketcher -Dsketcher_only=true
meson compile -C build-sketcher
./build-sketcher/dune3d
```

### Build full upstream-like mode

```bash
meson setup build
meson compile -C build
./build/dune3d
```

Note: full mode requires OpenCASCADE/STEP-related dependencies.

## Current Limits

- `Open file` currently accepts DXF as editable sketch input.
- Direct SVG document opening is not implemented in sketch-open flow.
- SVG content can still be brought into a sketch via **Import Picture**.

## Credits

- Based on [dune3d/dune3d](https://github.com/dune3d/dune3d)
- DXF Sketcher contributors

## License

GPL-3.0 (same as upstream, see [LICENSE](LICENSE)).
