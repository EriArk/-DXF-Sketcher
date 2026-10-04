# October 2026 screenshots

Captured on 4 October 2026 from the installed **DXF Sketcher 1.5.3** Windows application. Version confirmed against its installed package registration. The public source baseline was commit `2016c01`.

| Screenshot | Document | View |
| --- | --- | --- |
| [control-panel-dark.png](control-panel-dark.png) | [01-control-panel.dxf](../../examples/01-control-panel.dxf) | Dark theme; geometry markers hidden. |
| [control-panel-light.png](control-panel-light.png) | Same document | Light theme; geometry markers hidden. |
| [gear-pair-preview.png](gear-pair-preview.png) | Gear Generator preview | Two gears enabled, ratio 2.000, 24 teeth on the primary gear, 5 mm circular bore. |
| [gear-pair-preview.gif](gear-pair-preview.gif) | Same generator preview | Interactive rotation captured in 15 successive positions using the rotation slider's Up key, encoded at 12 frames per second and looped. |
| [boxes-finger-joints.png](boxes-finger-joints.png) | Boxes / ABox preview | Default 100 × 100 × 100 mm dimensions, 3 mm thickness and 0.1 mm burn allowance. |
| [boxes-flex-preview.png](boxes-flex-preview.png) | Boxes with flex / FlexBox preview | Default 100 mm dimensions, 15 mm radius and 3 mm thickness. |

The PNGs are direct application-window captures, saved without retouching or compositing: 1920 × 1030 for the editor, 1048 × 669 for Gear Generator and 1348 × 849 for Boxes. The GIF encodes actual 1048 × 669 window captures with FFmpeg palette generation; no geometry was redrawn or animated outside the app. Playback timing is set for the demonstration, not a recording of interaction speed.

The control-panel images show an original demonstration drawing imported from DXF, not a claim that it was drawn manually in the application. Labels are line geometry. The exact sample bytes are included in the repository. The gear and box images show geometry produced in the application's own generator previews; these were not imported into or saved over the control-panel sample.

No application source changes were required for the captures. The March 2026 screenshots in the parent folder retain their original files and dates.
