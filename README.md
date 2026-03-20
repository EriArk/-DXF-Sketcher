# DXF Sketcher

DXF Sketcher is a practical 2D editor for people who just want to open drawings, clean them up, build missing geometry, and get back to making things.

It keeps the precise sketching core from [dune3d](https://github.com/dune3d/dune3d), but reshapes the workflow around real DXF work: open one file, open a whole folder, save a multi-file project, edit directly on canvas, and export without dragging a full mechanical CAD workflow into every small job.

It is especially useful for laser cutting, CNC prep, templates, signs, enclosures, box layouts, gears, traced artwork, and everyday workshop drawing edits.

<p align="center">
  <img src="screenshots/Screenshot%20From%202026-03-20%2012-29-09.png" alt="DXF Sketcher editor" width="950">
</p>

## Why People Use It

- Open a single `DXF`, a whole folder of `DXF` files, or a saved project
- Edit geometry directly on the canvas instead of fighting a heavy CAD workflow
- Keep constraints, measurements, snapping, and precise sketch tools when you need them
- Generate fabrication helpers inside the app instead of jumping between separate tools
- Save back to `DXF` or export through `Save As` to `DXF` or `SVG`

## What You Can Do

### Fast DXF editing

- Draw contours, rectangles, rounded rectangles, circles, arcs, polygons, and text
- Move, duplicate, trim, clean up, and refine geometry directly on the sketch
- Use grid, snapping, markers, and symmetry tools while drawing
- Organize sketch content with layers and visibility controls
- Convert imported cluster geometry into editable geometry in place

### Work with real file sets

- Open one file when you just need a quick edit
- Open full folders when a job is split into many `DXF` files
- Color-mark folders and files, rename them, and toggle visibility from the side panel
- Save the entire job as a project file (`.dxsp`)

When you save a project, DXF Sketcher creates a project folder, copies in the relevant files and folders, and remembers:

- file and folder visibility
- file and folder color markers
- the currently active file
- the overall project structure

That makes it easy to stop and continue later exactly where you left off.

### Built-in fabrication helpers

- **Edge Tools** for finger joints, grooves, hinges, lids, flex cuts, handles, and related edge operations
- **Boxes** integration with bundled `boxes.py`, including catalog, preview, settings, gallery, and import
- **Gears** generator for involute gears directly in the sketch
- **Cup Template** helper
- **Import Raster** for cleanup before vectorizing
- **Trace Image** for turning artwork into editable vector geometry

### Easier to learn

- Short built-in tool hints for new users
- Quick popovers on the main toolbar instead of digging through deep menus
- Five built-in interface themes: `Light`, `Mix`, `Dark`, `Blue`, `Light-Blue`
- Accent colors and line-thickness tuning from the in-app menu

## Typical Workflows

### 1. Open and fix a DXF

1. Open one file, a folder, or an existing project.
2. Pick the sketch you want to work on.
3. Edit, redraw, constrain, or clean up geometry on canvas.
4. Save back to `DXF` or export to `SVG`.

### 2. Prepare parts for cutting

1. Draw or import the base outline.
2. Apply **Edge Tools** for joints, grooves, lids, or flex features.
3. Add text, markers, helper geometry, or traced graphics.
4. Export the final file for fabrication.

### 3. Build a full job package

1. Open several folders and files.
2. Color-mark and show/hide what matters.
3. Save everything into one `.dxsp` project.
4. Reopen later with the same structure and visibility state restored.

## Screenshots

<table>
  <tr>
    <td align="center" width="50%">
      <img src="screenshots/Screenshot%20From%202026-03-20%2012-28-43.png" alt="Welcome screen" width="100%"><br>
      <strong>Quick start</strong><br>
      Open recent files, folders, or full projects from the start screen.
    </td>
    <td align="center" width="50%">
      <img src="screenshots/Screenshot%20From%202026-03-20%2012-29-09.png" alt="Main editor" width="100%"><br>
      <strong>Main editor</strong><br>
      Direct sketch editing with markers, clean canvas tools, and fabrication-friendly workflow.
    </td>
  </tr>
  <tr>
    <td align="center" width="50%">
      <img src="screenshots/Screenshot%20From%202026-03-20%2012-34-51.png" alt="Edge Tools" width="100%"><br>
      <strong>Edge Tools</strong><br>
      Apply joints and edge treatments from a compact popover.
    </td>
    <td align="center" width="50%">
      <img src="screenshots/Screenshot%20From%202026-03-20%2012-36-37.png" alt="Gears generator" width="100%"><br>
      <strong>Gears generator</strong><br>
      Build involute gears without leaving the sketch.
    </td>
  </tr>
  <tr>
    <td align="center" width="50%">
      <img src="screenshots/Screenshot%20From%202026-03-20%2012-39-06.png" alt="Boxes integration" width="100%"><br>
      <strong>Boxes integration</strong><br>
      Browse templates, change parameters, preview the result, and import it straight into the project.
    </td>
    <td align="center" width="50%">
      <img src="screenshots/Screenshot%20From%202026-03-20%2012-49-21.png" alt="Import Raster" width="100%"><br>
      <strong>Import Raster</strong><br>
      Adjust an image before tracing or using it as a sketch reference.
    </td>
  </tr>
  <tr>
    <td align="center" width="50%">
      <img src="screenshots/Screenshot%20From%202026-03-20%2012-49-46.png" alt="Trace Image" width="100%"><br>
      <strong>Trace Image</strong><br>
      Turn bitmap artwork into editable vector geometry.
    </td>
    <td align="center" width="50%">
      <img src="screenshots/Screenshot%20From%202026-03-20%2012-51-27.png" alt="Light Blue theme" width="100%"><br>
      <strong>Theme options</strong><br>
      Switch between multiple built-in themes and accent colors.
    </td>
  </tr>
</table>

## Download

Current release: **1.5.1**

GitHub releases:

- https://github.com/EriArk/-DXF-Sketcher/releases

Linux release files include:

- `.deb`
- `.rpm`
- `.AppImage`

Example install commands:

```bash
sudo apt install ./dxfsketcher_1.5.1_amd64.deb
```

```bash
sudo rpm -i dxfsketcher-1.5.1-1.x86_64.rpm
```

```bash
chmod +x dxfsketcher-1.5.1-x86_64.AppImage
./dxfsketcher-1.5.1-x86_64.AppImage
```

## Platform Status

Linux is the main release target right now.

Windows and macOS versions will arrive a little later. They are planned, but there are still a few platform-specific issues we want to sort out before shipping builds there.

## Support

If DXF Sketcher is useful to you and you'd like to support its development, you can do that here:

- Patreon: https://patreon.com/AbyssTail
- Ko-fi: https://ko-fi.com/abysstail

Feedback, bug reports, and feature ideas also help a lot.

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

## Project Notes

- The main editable workflow is `DXF`
- `SVG` export is available through `Save As`
- DXF Sketcher is focused on 2D sketching and fabrication prep, not full 3D solid-model CAD

## Acknowledgements

DXF Sketcher builds on work and ideas from:

- [dune3d](https://github.com/dune3d/dune3d)
- [SolveSpace](https://github.com/solvespace/solvespace)
- [boxes.py](https://boxes.hackerspace-bamberg.de/)
- [dxflib](https://www.ribbonsoft.com/dxflib.html)
- [Clipper2](http://www.angusj.com/clipper2/Docs/Overview.htm)
- [nlohmann/json](https://github.com/nlohmann/json)
- [NanoSVG](https://github.com/memononen/nanosvg)

## License

- DXF Sketcher: **GPL-3.0**
- Third-party components keep their own licenses inside `3rd_party/`
