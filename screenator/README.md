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
  Pen, Text, Eraser, Shapes (Marker, Line, Spline, Polygon, Triangle, Arrow,
  Ellipse, Rounded Rectangle, Paint Bucket, Math Function, Parallel Lines),
  Line Thickness, Line Color, Shapes Fill Color, Screen Paper, Eraser Size,
  Opacity, Floating Icon Size, Undo, Redo, Change Font, Clear Screen, Toggle
  Visibility, Show Floating Icon, Show Settings Indicator, Reset Settings,
  About and Quit. The icon
  turns into its active (red) variant while a painting mode is on.
- Marker draws a translucent yellow highlighter stroke at 50% opacity with a
  fixed 32 px width. It has its own settings and switching to it does not
  change the line colour, thickness or opacity used by the other tools.
- Line Thickness offers 1, 2, 3, 6, 12, 24 and 48 pixels; the menu marks the
  entry closest to the current width.
- While painting mode is active, the mouse wheel steps through the line colours;
  with the Paint Bucket tool it steps through the shape fill colours and with
  the Eraser it steps through the eraser sizes instead. The change is the same
  as picking it from the menu, so it is kept when another tool is selected and
  is shown in the settings indicator and the menus.
- Shapes are drawn by dragging: press where the shape should start, move the
  pointer and release. The Line and Arrow follow the drag direction, which
  snaps to 0, 30, 45, 60, 90 and so on while `Ctrl` is held; without it the
  direction follows the pointer. The Arrow adds a head at the end point. A
  placed line or arrow stays editable: each end point has a round handle, and
  dragging the line itself moves the whole shape. Clicking outside,
  double-clicking or right-clicking commits it. The ellipse, rounded rectangle,
  polygon and triangle can optionally be filled using the Shapes Fill Color
  submenu; the default is no fill and green is its first colour.
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
  becomes a regular polygon; that handle snaps to the flat edge when it gets
  close, so a regular polygon is easy to hit. Drag inside the shape to move it.
  Clicking empty space commits it and places the next one; double-click or
  right-click commits it.
- Triangle is placed with a single click and stays editable until it is
  committed. Each of its three vertices has its own round handle, so the shape
  can be stretched and skewed freely; holding `Ctrl` while dragging a vertex
  snaps the angle at that vertex to 30, 45, 60 or 90 degrees. Drag inside the
  triangle to move it. Clicking empty space commits it and places the next one,
  and a double-click or right-click commits it.
- The ellipse and rounded rectangle stay editable after the drag. Every placed
  shape has a square size handle on its bottom-right corner, a round rotation
  handle above it and can be moved by dragging inside it. Rotation is free;
  holding `Ctrl` snaps it to 0, 30, 45, 60, 90 and so on. The rounded
  rectangle keeps a roundness handle on its top edge as well: drag it away from
  the corner to round the corners more, or towards the corner for sharper
  corners. Clicking outside, double-clicking or right-clicking commits the
  shape.
- Math Function opens a dialog for the expression and the range. The
  Rectangular mode plots `y = f(x)` over an x range; type a function of `x`.
  The Polar mode plots `r = f(theta)` over a theta range, where `theta` is
  reserved for the angle and the default range is 0 to 2π (a full circle).
  `pi` and `e` are the constants π and Euler's number, and multiplication can
  be implied (`2pi*x`). A preset drop-down fills the expression with
  `cos(2*pi*x)`, `sin(2*pi*x)`, `unit(x)`, `delta(x)`, `e^(x)`, `log(x)`,
  `1/x`, `sin(2*pi*x)/(2*pi*x)` or `(cos(2*pi*x)+2)*cos(20*pi*x)`; in polar
  mode the same presets use `theta` instead of `x`. `unit(x)` is the unit step,
  and `delta(x)` is the Dirac
  delta, drawn as
  an arrow at the point where its argument is zero. The defaults are
  `cos(2*pi*x)` from x = -2 to x = 2 and `cos(3*theta)` from theta = 0 to 2π;
  each mode keeps its own expression and range. The plot is drawn inside a
  frame that is dragged out like a shape and is scaled to fill it, so the size
  handle also sets its amplitude; polar plots use one scale for both axes, so
  the curve stays round in a non-square frame.
  The plot can be moved, resized and rotated like the other shapes. Selecting
  the tool from the Shapes menu asks for the expression first, and choosing it
  again changes the expression; a plot that is still editable updates
  immediately. A point where the expression is
  undefined is filled in with its limit when both sides agree, so `sin(x)/x`
  is drawn through x = 0; a real pole such as `1/x` or `tan(x)` breaks the
  curve instead.
- Parallel Lines draws between 2 and 40 evenly spaced vertical lines with the
  current line thickness. The square handle outside the left edge sets their
  number: pressing it resets to two lines and moving away adds more. The lines
  can be moved, resized and rotated with the usual handles.
- Paint Bucket fills the closed area under the click with the current Shapes
  Fill Color. The area must be enclosed by drawn strokes and must not reach the
  screen edge; otherwise nothing is filled, so clicking outside a shape cannot
  flood the whole screen. With None selected the bucket erases the enclosed
  fill instead, leaving the strokes around it. The tool shows a bucket cursor. Green is the first
  shape fill colour and the default, so a first fill is green; Red is the last
  entry in the submenu.
- The eraser size can be set to 20, 40, 60 or 120 pixels. Both the erasing area
  and the circular eraser cursor follow the selected size.
- The Text tool shows an I-beam cursor and writes directly at the click
  position: a small inline editor appears where you click, using the pen
  colour and the chosen font; the text is centred vertically on the click
  point. Press `Enter` to place the text or `Esc` to cancel. While the editor
  is open the mouse buttons are disabled, so a click cannot leave painting mode
  or reach the application below; `Enter` or `Esc` enables them again. The text
  and its blinking caret are drawn above the annotations and the screen paper,
  so they stay visible even over a drawing. The middle mouse button still
  erases while it is held, also with the Text tool selected. Right-to-left text
  such as Arabic extends to the left of the click position. A placed text stays editable: drag it to move it, drag
  its square handle to change the font size and its round handle to rotate it.
- The same menu is shown by the system tray icon.
- A small settings indicator sits to the left of the floating icon. It is a
  rectangle as tall as the icon, divided into three equal-height pieces that
  are centred horizontally: the upper piece is the line thickness in the line
  colour, the middle piece is 24 pixels of the line colour and the lower piece
  is 24 pixels of the shape fill colour (a black and white checkerboard when
  shapes are not filled). Each piece is surrounded by a one pixel black
  outline. "Show Settings Indicator" in the menu turns it off and on; it is on
  by default and is disabled while the floating icon is hidden.
- Dialogs and message boxes, such as the Math Function dialog and the tool
  hints, are shown with the drawings temporarily hidden, so they are never
  covered by them; the drawings come back when the dialog is closed. This also
  works in virtual machines without a compositor.
- A Screen Paper submenu turns the screen into paper for drawing and writing.
  None shows the normal desktop, Plain a white page, Ruled a white page with
  blue writing lines and Graph a white page with a grey square grid. The paper
  is drawn below the annotations and above the other windows (the floating
  icon and its menu stay above it), takes the mouse and keyboard so clicks and
  typing do not reach the desktop or the applications below, stays when Clear
  Screen is selected and is remembered across restarts.
- Selecting a tool shows a short hint that describes that tool: its mouse
  buttons, what the mouse wheel changes and how its handles work. Each tool's
  message has its own "Never show this message again" option, so turning one
  off leaves the other tools' messages alone. The Spline hint additionally
  explains how to move its handles, and the tools whose handles snap show a
  hint about holding `Ctrl` to snap to 0, 30, 45, 60, 90 degrees and so on.
  Selecting the Text tool explains that the mouse buttons are disabled while
  typing. Each hint has its own option, and Reset Settings keeps all the
  choices, so a hint you turned off stays off.
- Pen, marker, font, eraser, shape and screen paper settings are saved in
  `~/.config/screenator.ini` and restored on the next start, together with the
  floating icon and settings indicator visibility. `Reset Settings` restores red, 6 px, 100%
  opacity, no shape fill, Arial Bold 24, a 40 px eraser, a 48x48 icon and no
  screen paper.
- Hotkeys: `F9` toggle painting, `SHIFT-F9` clear, `CTRL-F9` toggle
  visibility, `ALT-F9` quit, `F8` undo, `SHIFT-F8` redo. While painting mode
  is active, `Ctrl+Z` also undoes and `Ctrl+Shift+Z` redoes, even with Caps
  Lock or Num Lock on.

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

- `screenator_3.0_amd64.deb`
- `Screenator-3.0-x86_64.AppImage`

The `.deb` can be installed with `sudo dpkg -i` (or `sudo apt install ./…`).
The AppImage is portable: make it executable and run it.

## License

Screenator is free software under the GNU General Public License version 2 (or
later). It contains code from Gromit-MPX 1.4.2, Copyright 2009-2022 Christian
Beier, and from Gromit, Copyright 2000 Simon Budig. See `COPYING` for the full
license text.
