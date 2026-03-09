diff --git a//home/abysstail/apps/DXF Maker/dune3d/README.md b//home/abysstail/apps/DXF Maker/dune3d/README.md
new file mode 100644
--- /dev/null
+++ b//home/abysstail/apps/DXF Maker/dune3d/README.md
@@ -0,0 +1,129 @@
+# DXF Sketcher
+
+DXF Sketcher is a practical 2D DXF editor for fast sketching, cleanup, and fabrication prep.
+
+It keeps the parametric core from [dune3d](https://github.com/dune3d/dune3d), but reshapes the workflow for day-to-day DXF work: quicker drawing, quicker selection, and built-in helper tools for boxes, gears, tracing, and edge features.
+
+Repository: <https://github.com/EriArk/-DXF-Sketcher>
+

+
+## What It Does
+
+- Open a single DXF or a whole folder of DXF files.
+- Edit sketches directly on canvas with a simpler workflow than full CAD.
+- Keep using constraints, dimensions, and precise parametric geometry when needed.
+- Save back to the linked DXF, or export with `Save As` to `DXF` or `SVG`.
+- Generate fabrication-ready geometry with built-in helpers instead of external tools.
+
+## Highlights
+
+- Fast 2D drawing tools: contour, rectangle, rounded rectangle, circle, arc, polygon, text.
+- Direct editing tools for selection, move, copy/paste, and quick object-style changes.
+- Constraint-aware workflow with dimensions, symmetry, and precise geometry editing.
+- Built-in **Boxes** catalog powered by bundled `boxes.py`, with gallery, preview, and sample photos.
+- Built-in **Gears** generator for common involute gear shapes.
+- Built-in **Import Raster** and **Trace Image** workflows for turning images into editable vector geometry.
+- Built-in **Edge Features** tool for joinery, hinges, grooves, flex cuts, handles, and related edge operations.
+- Theme variants for light, dark, dark blue, and soft light sketch work.
+
+## 1.5.0 Prerelease Highlights
+
+- **Boxes is now self-contained**: the app bundles `boxes.py`, uses its real parameters, and no longer depends on the user installing an external boxes tool.
+- **Boxes catalog got much bigger**: categories, favorites, gallery view, sample-photo popups, and native previews for the bundled library.
+- **Edge Features** replaces the old joints workflow with family-based operations such as finger joints, dovetails, grooves, hinges, slide-on lid edges, mounting cuts, and more.
+- **Raster-to-vector flow is now first-class**: import a bitmap, preprocess it, then trace and apply it directly into the sketch.
+- **Packaging improved**: bundled Python runtime support for release builds and scripted packaging for AppImage and Flatpak.
+
+## Main Workflows
+
+### Edit DXF quickly
+
+1. Open a single file or a whole folder.
+2. Pick the sketch you want to edit.
+3. Draw, move, constrain, mirror, trace, or generate helper geometry.
+4. Save to the original DXF or export to DXF / SVG.
+
+### Prepare fabrication geometry
+
+- Use **Boxes** to generate box layouts with native `boxes.py` parameters.
+- Use **Gears** to create involute gears from selected profiles.
+- Use **Edge Features** to apply joints, grooves, hinges, lid rails, mounting slots, flex patterns, and handles to selected edges.
+- Use **Trace Image** to turn black-and-white or processed raster art into vector curves.
+
+## Feature Overview
+
+### Drawing and editing
+
+- Contour drawing
+- Rectangle and rounded rectangle drawing
+- Circles and arcs
+- Regular polygons
+- Text placement
+- Move and direct selection workflows
+- Open-folder batch workflow for DXF collections
+
+### Precision tools
+
+- Parametric constraints
+- Dimensions and measurements
+- Symmetry tools
+- Layers for sketch groups
+- Constraint and marker rendering controls
+
+### Generators and helpers
+
+- Boxes catalog with gallery and sample photos
+- Gear generator
+- Edge Features tool
+- Cup template helper
+- Raster import and image tracing
+
+### Appearance and workflow
+
+- Light and dark sketch themes
+- Workspace browser with recent files and folders
+- Save, Save As, DXF export, SVG export
+
+## Install
+
+Prebuilt packages are intended to be distributed from GitHub Releases.
+
+Planned release artifacts for `1.5.0`:
+
+- `AppImage`
+- `Flatpak bundle`
+
+### Build from source
+
+```bash
+meson setup build-sketcher
+ninja -C build-sketcher dxfsketcher
+./build-sketcher/dxfsketcher
+```
+
+## Current Notes
+
+- `DXF` is the main editable document format.
+- `SVG` export is supported with `Save As`.
+- Opening standalone SVG as a primary editable document is still not the main workflow.
+- **Edge Features** is actively evolving and some advanced families will keep improving after this prerelease.
+
+## Acknowledgements
+
+DXF Sketcher builds on work and ideas from:
+
+- [dune3d](https://github.com/dune3d/dune3d)
+- [SolveSpace](https://github.com/solvespace/solvespace)
+- [boxes.py](https://boxes.hackerspace-bamberg.de/)
+- [dxflib](https://www.ribbonsoft.com/dxflib.html)
+- [Clipper2](http://www.angusj.com/clipper2/Docs/Overview.htm)
+- [nlohmann/json](https://github.com/nlohmann/json)
+- [NanoSVG](https://github.com/memononen/nanosvg)
+
+## License
+
+- DXF Sketcher: **GPL-3.0**
+- Third-party components keep their own licenses inside `3rd_party/`
