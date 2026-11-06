# Screenator for Windows 7.0

A portable 64-bit Windows release of the Screenator screen annotation tool,
based on Screenator 6.0 by Mahmoud Alnaanah.

## Highlights

- Native Win32 port with the full Screenator tool set: pen, marker, eraser,
  text, line, arrow, spline, polygon, triangle, ellipse, rounded rectangle,
  paint bucket, math functions, parallel lines and random signal.
- Editable shapes with corner, rotation, arrow-style and extra handles.
  Arrow and parallel-line style handles cycle through all styles while the
  drag continues.
- Closed shapes keep their stroke entirely outside the shape border.
- Polygon placement uses the first click as the fixed centre; dragging sets
  the radius and rotation. Triangle placement uses three clicks with a live
  third vertex.
- Spline editing hides the rubber-band segment while nodes or handles are
  being adjusted. Out-of-range and duplicate stylus hover updates are ignored,
  so the rubber-band preview no longer flashes stray line segments while
  drawing with a pen.
- Global opacity applies to both strokes and fills. The opacity menus offer
  10% through 90% in 10% steps.
- Math plots break at real poles such as `1/x` and `tan(x)`, and `delta()` is
  drawn as an arrow at each zero of its argument. The Math Function dialog now
  places the preset list above the expression field and is 20% taller.
- Stylus support for the first and second side buttons. Set the pen driver's
  **button 1 (nearest the tip) to Middle Click** and **button 2 to Right
  Click** for the expected short-press and long-press actions.
- Defaults: 48x48 floating icon, 60 px eraser, Arial Bold 24 pt font.
- Hints window is 750x550.
- Delayed tool activation no longer dismisses an open context menu. The menu
  is captured once, immediately before the pending tool activates, so the
  captured menu (including a File Explorer menu, a Qt application menu such as
  Patchy, and an open submenu) remains visible as a background image after the
  native menu closes. Annotations are drawn above the captured menu, so it can
  be marked up. Pressing `Esc` closes shape handles first; the image is removed
  only when the tool itself is dismissed and Screenator becomes idle.
  Switching directly from one tool to another keeps it.
- Screen-paper shortcuts work correctly: F6/F7/F8 switch tools immediately,
  F9 shows or hides the floating icon without it reappearing, and Alt+F4
  quits.
- While the screen paper is active, clicking outside Hints, About, Math,
  Floating Icon Position, or the Change Font dialog closes it. The outside
  click is consumed instead of drawing on the paper. The Hints window includes
  an emphasized note in its Screen Paper Mode section explaining this
  behaviour.
- The captured menu stays visible while drawing and does not react to clicks,
  so it can be annotated like the rest of the desktop. `Esc` clears it, and
  normal menu use without a pending delayed tool is unchanged and does not
  leave a menu behind.

## Package

```
Screenator-7.0-win64/
  Screenator.exe
  Screenator Data/
    assets/
    README.md
    RELEASE_NOTES.md
    COPYING
    AUTHORS
```

`Screenator.exe` is statically linked and does not require DLLs. Keep the
`Screenator Data` folder next to it.

## Verification

```
Screenator.exe --selftest
Screenator.exe --uitest
Screenator.exe --smoke
```

## License

GNU General Public License version 2 or later. See `COPYING`.
