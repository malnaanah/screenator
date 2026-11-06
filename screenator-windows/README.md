# Screenator for Windows 7.0

A native Windows port of [Screenator 6.0](https://github.com/malnaanah/screenator),
the screen annotation tool for X11. Draw over any running application with a
pen, marker, arrows, text, shapes, math plots or the eraser while the desktop
keeps running underneath.

This port keeps Screenator's cairo-based drawing and tool behaviour and
replaces the X11/GTK platform layer with Win32: a per-pixel layered overlay,
native menus and dialogs, a tray icon, global hotkeys and a floating icon with
the settings ring.

## Features

- Pen, marker, eraser, text, line, arrow, spline, polygon, triangle, ellipse,
  rounded rectangle, paint bucket, math function, parallel lines and random
  signal tools.
- Editable shapes with corner/rotation/extra handles and Ctrl angle snapping.
- Closed shapes (ellipse, rounded rectangle, polygon, triangle) keep their
  stroke entirely outside the shape border.
- Polygon: the first press fixes its centre; dragging sets the radius and
  rotation around that centre, and the handles refine the radius, sides,
  inner radius and angle.
- Triangle placement uses three clicks: the first two fix two vertices, the
  third follows the pointer and the last click fixes it. The vertices remain
  draggable afterwards.
- Spline: the last node can be dragged; dragging a node or its handles hides
  the preview segment after the last point, and clicking the last node without
  moving finishes the curve. Stylus hover updates are filtered so the
  rubber-band preview does not flash stray segments.
- Inline text editing drawn directly over the desktop with a blinking caret;
  a hidden focus window keeps typing, IME, selection and clipboard working.
- Undo (4 steps), redo, clear screen, and optional annotation persistence.
- Line colours, custom colours, shape fill colours, nine line widths, opacity
  (10% to 90% in steps of 10%, applied to strokes and fills), eraser sizes,
  marker opacity and a configurable font.
- Math plots break at real poles such as `1/x` and `tan(x)` instead of drawing
  a false vertical line, and `delta()` is drawn as an arrow at each zero of its
  argument.
- Screen paper (plain, ruled, graph) drawn below the annotations.
- Optional tool activation delay. If the delay expires while any context menu
  is open, including a File Explorer menu or a Qt menu such as Patchy, the menu
  (and an open submenu) is captured just before activation and kept on screen
  as a background image after the native menu closes. Annotations are drawn
  over the captured menu, so it can be marked up and explained. Pressing `Esc`
  closes shape handles first and removes the captured menu only when the tool
  itself is dismissed and Screenator becomes idle; switching directly from one
  tool to another keeps it.
- Floating icon with the settings ring, draggable and docked to any corner.
  Sizes 16, 24, 32, 48, 64, 128 and 256 pixels are available.
- Tray menu, hints dialog with a screen-paper outside-click note, quick tool
  menu (F10), math dialog and About window.
- Settings saved to `%APPDATA%\Screenator\screenator.ini`; drawings saved to
  `%APPDATA%\Screenator\screenator-annotations.png`.

## Defaults

| Setting | Default |
|---|---|
| Line colour | Red |
| Line width | 6 px |
| Opacity | 100% |
| Shape fill | None (first fill is green) |
| Font | Arial Bold 24 pt |
| Eraser | 60 px |
| Floating icon | 48x48, top right |
| Marker opacity | 40% |

## Shortcuts

- `F8` Pen, `F7` Text, `F6` Arrow (global).
- `F9` show or hide the floating icon (global).
- `F10` quick tool menu (global).
- While painting: `Esc` finishes the shape or exits the tool, `Ctrl+Z` undo,
  `Ctrl+Shift+Z` redo.
- Screen paper: `Ctrl+Z` / `Ctrl+Shift+Z` undo and redo, `Ctrl+T` show or hide
  the drawings, `Ctrl+Q` leave the paper, `Alt+F4` quit.

## Build

Requirements:

- [MSYS2](https://www.msys2.org/) with the mingw-w64 toolchain
- `mingw-w64-x86_64-gcc`, `mingw-w64-x86_64-cairo`,
  `mingw-w64-x86_64-pango`, `mingw-w64-x86_64-pkgconf` and `make`

Install the packages once:

```
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cairo \
    mingw-w64-x86_64-pango mingw-w64-x86_64-pkgconf make
```

Then run `build.bat` (or `./build.sh` from an MSYS2 shell). The result is a
portable package in `dist/`:

```
dist/
  Screenator.exe          self-contained, no DLLs
  Screenator Data/
    assets/               icons, cursors and the application icon
    README.md
    RELEASE_NOTES.md
    COPYING
    AUTHORS
```

The executable is statically linked, so it runs on a clean Windows system
without the mingw-w64 runtime. Only the `Screenator Data` folder travels with
it.

## Verification

Developer test modes (they do not touch the normal settings or drawings):

```
Screenator.exe --selftest   # rendering, pango text, math parser and paint bucket
Screenator.exe --uitest     # tools, undo/redo, menus, dialogs and config round trip
Screenator.exe --smoke      # starts the full UI and exits after two seconds
Screenator.exe --pen-test   # 20 seconds with the Pen active; logs pen events
```

## Notes on the Windows port

- The overlay is a layered, always-on-top Win32 window. While a tool is active
  it takes the mouse; otherwise clicks pass through to the applications below.
- Pango/Cairo provide the same anti-aliased drawing and text rendering,
  including right-to-left text.
- Stylus pens use Windows Ink pointer input plus a raw HID fallback: the tip
  draws, the eraser tip erases, the first side button steps colour/thickness
  on a short press and temporarily erases while held after a long press. The
  second side button finishes or exits on a short press and opens the
  **Button 1 Mode** menu on a long press. When the pen exposes its HID barrel
  switch, Screenator reads it directly so the first side button also works
  while the pen is merely hovering; middle-mouse and pointer-second-button
  reports are accepted as well. A pointer second button without the barrel
  flag (and middle mouse) is side button 1; a barrel flag or the third pointer
  button is side button 2. Long-press menus appear at the pen point and are
  selected with the pen tip, and the floating icon stays usable afterwards.
- The **Button 1 Mode** menu selects Color or Thickness for the stylus first
  side button; the Eraser tool uses it to step through eraser sizes.
- **Important pen-driver setting:** set the pen's **button 1 (nearest the
  tip) to Middle Click** and **button 2 to Right Click**. Screenator uses the
  middle-click event for button 1 and the right-click event for button 2. If
  the buttons are assigned to vendor-specific actions, keyboard shortcuts or
  "Disabled", Windows never delivers them and no application can see them.
- Pen side-button assignments are controlled by the pen driver and Windows
  Ink, not by Screenator. Screenator also polls the pen while it is merely in
  range, so drivers that only report side buttons during tip contact still
  work. `--pen-test` writes the exact pointer flags and poll transitions to
  `debug.txt`.
- Settings that do not exist on Windows (X11 pointer grabs, compositors,
  AppIndicator) are implemented with their Win32 equivalents or omitted when
  they have no meaning.
- The screen paper takes the keyboard focus while it is active, so typing does
  not reach the applications below, as in the original.

## License

GNU General Public License version 2 or later. This program is based on
Screenator 6.0 by Mahmoud Alnaanah, which is based on Gromit-MPX 1.4.2
(Copyright 2009-2022 Christian Beier) and Gromit (Copyright 2000 Simon Budig).
See `COPYING` for the full text.
