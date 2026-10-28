# Screenator

Screenator is a vibe-coded screen annotation tool for X11, developed by
**Mahmoud Alnaanah** <malnaanah@gmail.com>. It is based on
[Gromit-MPX](https://github.com/bk138/gromit-mpx) 1.4.2.

Draw over any running application with a pen, arrow, text, shapes or eraser,
change the line colour, thickness and opacity, choose the text font, and
resize the floating icon. The drawing stays on screen as long as you need it,
and you can keep using your applications while it is visible.

## Features

- A small draggable floating icon. Left or right click opens the menu with:
  Pen, Text, Eraser, Shapes (Marker, Free Line, Spline, Polygon, Triangle,
  Arrow, Angled Line, Angled Arrow, Rectangle, Ellipse, Rounded Rectangle,
  Paint Bucket),
  Line Thickness, Line Color, Shapes Fill Color, Eraser Size, Opacity,
  Floating Icon Size, Undo, Redo, Change Font, Clear Screen, Toggle
  Visibility, Show/Hide Floating Icon, Reset Settings, About and Quit.
- Marker draws a translucent yellow highlighter stroke at 50% opacity with a
  fixed 32 px width. It has its own settings and switching to it does not
  change the line colour, thickness or opacity used by the other tools.
- Shapes are drawn by dragging: press where the shape should start, move the
  pointer and release. Free Line and Arrow use the drag direction as-is, while
  Angled Line and Angled Arrow snap to 0, 30, 45, 60, 90, 120, 135 and 150
  degrees. Arrow and Angled Arrow add a head at the end of the line.
  Rectangle, ellipse, rounded rectangle, polygon and triangle can optionally be
  filled using the Shapes Fill Color submenu; the default is no fill and green
  is its first colour.
- Spline is placed click by click: each click adds a node and the nodes are
  joined by a smooth Bezier curve. Every node has two round Bezier handles:
  drag one and the opposite handle mirrors it so the curve stays smooth; hold
  `Ctrl` while dragging to move only that side and give the node a corner.
  Drag a square node to move it. Click the last node again, double-click or
  right-click to finish the curve. A single node does not create an
  annotation.
- Polygon is placed with a single click and stays editable until it is
  committed. Four handles shape it: the square handle controls the number of
  sides (3 to 16); pressing it resets the shape to 3 sides. Moving away from
  the handle in any direction adds sides, and moving back towards it removes
  them again, down to 3 at the handle. One round handle sets the radius, one
  sets the rotation, and one moves the inner vertices outwards until the shape
  becomes a regular polygon. Drag inside the shape to move it. Clicking empty
  space commits it and places the next one; double-click or right-click commits
  it.
- Triangle is placed with a single click and stays editable until it is
  committed. Each of its three vertices has its own round handle, so the shape
  can be stretched and skewed freely; drag inside the triangle to move it.
  Clicking empty space commits it and places the next one, and a double-click
  or right-click commits it.
- The Rounded Rectangle keeps a roundness handle on its top edge after the
  drag. Drag the handle away from the corner to round the corners more, or
  towards the corner for sharper corners; the shape stays editable until you
  click outside it, double-click or right-click.
- Paint Bucket fills the closed area under the click with the current Shapes
  Fill Color. The area must be enclosed by drawn strokes and must not reach the
  screen edge; otherwise nothing is filled, so clicking outside a shape cannot
  flood the whole screen. The tool shows a bucket cursor. Green is the first
  shape fill colour and the default, so a first fill is green; Red is the last
  entry in the submenu.
- The eraser size can be set to 20, 40, 60 or 120 pixels. Both the erasing area
  and the circular eraser cursor follow the selected size.
- The Text tool shows an I-beam cursor and writes directly at the click
  position: a small inline editor appears where you click, using the pen
  colour and the chosen font. Press `Enter` to place the text or `Esc` to
  cancel. Right-to-left text such as Arabic extends to the left of the click
  position.
- The same menu is shown by the system tray icon.
- Selecting a tool shows a short hint about the mouse buttons, with a
  "Never show this message again" option. The Spline hint additionally
  explains how to move its handles. Reset Settings keeps
  both choices, so a hint you turned off stays off.
- Pen, marker, font, eraser and shape settings are saved in
  `~/.config/screenator.ini` and restored on the next start. `Reset Settings`
  restores red, 6 px, 100% opacity, no shape fill, Arial Bold 24, a 40 px
  eraser and a 48x48 icon.
- Hotkeys: `F9` toggle painting, `SHIFT-F9` clear, `CTRL-F9` toggle
  visibility, `ALT-F9` quit, `F8` undo, `SHIFT-F8` redo. While painting mode
  is active, `Ctrl+Z` also undoes and `Ctrl+Shift+Z` redoes.

## Build

Dependencies: GTK+ 3, Ayatana AppIndicator 3, XInput 2, X11, CMake and a C
compiler. On Debian/Ubuntu:

```
sudo apt install build-essential cmake libgtk-3-dev \
    libayatana-appindicator3-dev libxi-dev libx11-dev librsvg2-bin
```

Then run:

```
./build.sh
```

The script builds the project and produces both packages in `../build/`:

- `screenator_2.0_amd64.deb`
- `Screenator-2.0-x86_64.AppImage`

The `.deb` can be installed with `sudo dpkg -i` (or `sudo apt install ./…`).
The AppImage is portable: make it executable and run it.

## License

Screenator is free software under the GNU General Public License version 2 (or
later). It contains code from Gromit-MPX 1.4.2, Copyright 2009-2022 Christian
Beier, and from Gromit, Copyright 2000 Simon Budig. See `COPYING` for the full
license text.
