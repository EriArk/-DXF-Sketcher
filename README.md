# DXF Sketcher

DXF Sketcher is a 2D DXF editor built for fast, practical sketch work.

It is based on [dune3d](https://github.com/dune3d/dune3d), with the workflow and UI adapted for simpler day-to-day DXF editing.

Repository: <https://github.com/EriArk/-DXF-Sketcher>

## What Users Care About

- Open one or multiple DXF files
- Open a whole folder of DXF files
- Edit the active sketch quickly
- Use `Save` for the linked file, or `Save As` for `DXF` / `SVG`
- Use practical tools for daily work: contour, rectangle, circle/arc, polygon, text, image import

## Two Workflows, One App

DXF Sketcher keeps the **parametric workflow**:
- constraints, dimensions, and precise geometry are still fully supported.

DXF Sketcher also adds a more **direct, object-style workflow** (closer to graphic editors):
- faster select/move/edit behavior on canvas;
- fewer steps for common operations.

You choose the approach per task: strict parametric modeling, direct object-style editing, or a mix of both.

## Quick Flow

1. Click `Open file` or `Open folder`.
2. Select the sketch you want to edit.
3. Modify geometry.
4. Save with `Save` or `Save As`.

## Current Limits

- The main editable input format is `DXF`.
- Opening SVG as a standalone editable document via `Open file` is not implemented yet.
- SVG can still be brought into a sketch via `Import Picture`.

## Acknowledgements

DXF Sketcher is built on top of ideas and work from:
- [dune3d](https://github.com/dune3d/dune3d)
- [SolveSpace](https://github.com/solvespace/solvespace)
- [dxflib](https://www.ribbonsoft.com/dxflib.html)
- [Clipper2](http://www.angusj.com/clipper2/Docs/Overview.htm)
- [nlohmann/json](https://github.com/nlohmann/json)
- [NanoSVG](https://github.com/memononen/nanosvg)

Thanks to the authors and contributors of these upstream projects.

## License

- DXF Sketcher: **GPL-3.0** (see [LICENSE](LICENSE)).
- Third-party components use their own licenses (see `3rd_party/` and license files inside).
