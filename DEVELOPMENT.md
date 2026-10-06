# Development guide

DXF Sketcher is a 2D workshop drawing editor derived from dune3d. Preserve the upstream attribution and third-party notices while maintaining the DXF-focused interface.

## Source map

| Area | Location |
| --- | --- |
| Application/editor coordination, generators and toolbar | src/editor/editor.cpp |
| File/folder/project browser | src/editor/workspace_browser.cpp |
| Document, entities and constraints | src/document and src/core |
| DXF import | src/import_dxf/dxf_importer.cpp |
| DXF export | src/document/export_dxf.cpp |
| Drawing and rendering | src/canvas and src/render |
| Preferences and shared widgets | src/preferences and src/widgets |
| Bundled components, including Boxes | 3rd_party |
| Build and packaging | meson.build, scripts and wix |

## Product and review principles

- Keep ordinary open/edit/Save As workflows direct and preserve user data.
- Keep theme, spacing, units and interaction conventions consistent across generators and the editor.
- Check zoom, DPI, rulers and overlays for affected rendering changes.
- Check packaged runtime behavior separately from source compilation, especially Boxes, raster tracing, icons and AppImage update metadata.
- Prefer small extractions from the large editor over a wholesale rewrite. Establish behavior checks before moving responsibility between components.

## Current work and known limits

[GitHub Issues](https://github.com/EriArk/-DXF-Sketcher/issues) is the current backlog. In particular, import can discard unsupported DXF entities and layer information ([#2](https://github.com/EriArk/-DXF-Sketcher/issues/2)); Bezier export uses a bounded segment heuristic rather than a documented error tolerance ([#5](https://github.com/EriArk/-DXF-Sketcher/issues/5)). Work on these limitations is not complete.

Local product-build checks and DXF regression fixtures are tracked in [#3](https://github.com/EriArk/-DXF-Sketcher/issues/3) and [#4](https://github.com/EriArk/-DXF-Sketcher/issues/4). GitHub Actions is disabled. See [BUILDING.md](BUILDING.md) for commands that exist today; do not treat a proposed test target as implemented.

The [local formatting baseline](https://github.com/EriArk/-DXF-Sketcher/issues/6) and [focused editor extractions](https://github.com/EriArk/-DXF-Sketcher/issues/7) are separate maintenance tasks, behind drawing correctness in priority.

## Historical working notes

The former codex/ prompt pack mixed session instructions, estimates and superseded plans. It was removed from the current tree on 6 October 2026. Its original content remains in [Git history at 0d0c70f](https://github.com/EriArk/-DXF-Sketcher/tree/0d0c70f40ff584622fbc3afce720cbc15ba6e4ac/codex); it is historical context, not the current work queue. The useful product and verification principles are summarized above. Release notes, changelog, screenshots and capture provenance remain intact.
