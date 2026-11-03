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
  Ellipse, Rounded Rectangle, Paint Bucket, Math Function, Parallel Lines,
  Random Signal), Tool Activation Delay,
  Line Thickness, Line Color, Shapes Fill Color, Screen Paper, Eraser Size,
  Opacity, Floating Icon Size, Undo, Redo, Clear Screen, Toggle Visibility,
  Show Floating Icon (F9), Show Hints, Settings (Save Annotations on Exit,
  Change Font, Show Settings Indicator, Floating Icon Position..., Reset
  Settings), About and Quit. The icon turns into its active (red) variant
  while a painting mode is on.
- **Floating Icon Position...** in Settings opens a dialog with Top Right
  (default), Top Left, Bottom Right and Bottom Left. The chosen corner is
  saved and restored on the next start.
- Marker draws a translucent highlighter stroke with a fixed 32 px width,
  using the current line colour. Its opacity is 40% by default and is
  independent of the global pen opacity. Long right click, or a long press of
  the stylus second side button, opens the **Marker Opacity** menu with 30%,
  40%, 50%, 60% and 70%; the choice is remembered across restarts. Selecting
  the Marker switches the line colour to yellow; leaving the Marker or
  exiting painting restores the colour that was active before. The mouse wheel
  or the stylus first side button changes its colour.
- Tool Activation Delay can delay a newly selected tool by 0, 1, 2, 3, 4, 5,
  7, 9 or 11 seconds. The delay is always 0 when the application starts; it is
  not remembered across restarts. During the delay the previously selected
  tool stays active.
  If the pointer grab is blocked when the delay ends, for example by another
  application's context menu, Screenator freezes the current screen as a
  temporary background so the tool can draw over it; exiting the tool removes
  the frozen background.
- Line Thickness offers 1, 2, 3, 4, 5, 6, 12, 24 and 48 pixels; the menu marks the
  entry closest to the current width.
- While painting mode is active, the mouse wheel changes the setting selected
  in the Scroll Mode menu: **Color** steps the line colours (including the
  custom colour), or the shape fill colours over the Paint Bucket; **Thickness**
  steps through 1, 2, 3, 4, 5, 6, 12, 24 and 48 pixels. With the Eraser tool
  selected, the wheel changes the eraser size instead. Long-press the right
  mouse button to choose Color or Thickness; a short right click finishes the
  current shape or exits painting like Escape. The change is the same as
  picking it from the menu, so it is kept when another tool is selected and is
  shown in the settings indicator and the menus.
- Shapes are drawn by dragging: press where the shape should start, move the
  pointer and release. The Line and Arrow follow the drag direction, which
  snaps to 0, 30, 45, 60, 90 and so on while `Ctrl` is held; without it the
  direction follows the pointer. The Arrow adds a head at the end point by
  default; its square handle, offset from the middle of the line, changes the
  heads: pressing it resets to none and dragging away cycles start, end and
  both. A placed line or arrow stays editable: each end point has a round
  handle, and dragging the line itself moves the whole shape. Clicking outside,
  double-clicking or right-clicking commits it. The ellipse, rounded rectangle,
  polygon and triangle can optionally be filled using the Shapes Fill Color
  submenu; the default is no fill and green is its first colour. Line Color
  and Shapes Fill Color also have a Custom... entry. It opens a colour
  chooser; the custom colour starts as magenta and the last choice is
  remembered.
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
  shape has a square size handle on each of its four corners, a round rotation
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
  `cos(2*pi*x)`, `sin(2*pi*x)`, `unit(x)`, `delta(x)`, `rect(x)`, `e^(x)`,
  `log(x)`, `1/x`, `sin(2*pi*x)/(2*pi*x)` or
  `(cos(2*pi*x)+2)*cos(20*pi*x)`; in polar mode the same presets use `theta`
  instead of `x`. `unit(x)` is the unit step, `delta(x)` is the Dirac delta,
  drawn as an arrow at the point where its argument is zero, and `rect(x)` is
  a rectangle pulse that is 1 for |x| <= 1/2 and 0 elsewhere. The defaults are
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
  number: pressing it resets to two lines and moving away adds more. The square
  handle outside the right edge changes the arrowheads on the lines: pressing
  it resets to none, and moving away adds them in the order top, bottom and
  both; the handle shows the current mode. The lines can be moved, resized and
  rotated with the usual handles.
- Random Signal draws a random waveform. A single click places a default-size
  signal centred on the click; dragging out a frame sets its size. The square
  handle outside the left edge resets the smoothness to 1 (sharpest); dragging
  it away in any direction raises it up to 16 (smoothest). The default is 5.
  Clicking the signal without dragging generates a new signal with a new seed.
  The frame can be moved, resized and rotated with the usual handles.
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
  a corner handle to change the font size and its round handle to rotate it.
- The same menu is shown by the system tray icon.
- The settings indicator is a ring around the floating icon's circle. The
  inner white ring is divided into nine equal sectors, one for each line
  thickness (1, 2, 3, 4, 5, 6, 12, 24 and 48 pixels), each with a one pixel black
  stroke. Starting at angle 0 and going counter clockwise, the sectors up to
  and including the selected thickness are filled magenta. The outer six
  pixel ring has the line colour in its upper half and the shape fill colour
  in its lower half; when shapes are not filled, the lower half shows twelve black
  and white radial stripes. Both halves have a one pixel black stroke. Left or
  right click on the ring opens the same menu as the floating icon. "Show
  Settings Indicator" in the menu turns it off and on; it is on by default and
  is disabled while the floating icon is hidden.
- Dialogs and message boxes, such as the Math Function dialog and the hints
  dialog, are shown above the drawings and the screen paper, so both stay
  visible behind them; each dialog is raised as it opens, which also works in
  virtual machines without a compositor.
- A Screen Paper submenu turns the screen into paper for drawing and writing.
  None shows the normal desktop, Plain a white page, Ruled a white page with
  blue writing lines and Graph a white page with a grey square grid. The paper
  is drawn below the annotations and above the other windows (the floating
  icon and its menu stay above it), takes the mouse and keyboard so clicks and
  typing do not reach the desktop or the applications below and stays when
  Clear Screen is selected. Screenator always starts on the normal desktop; a
  saved paper is not restored. While a paper is active, Ctrl+Z and
  Ctrl+Shift+Z undo and redo, Ctrl+Q exits the paper,
  Ctrl+T shows or hides the drawings and Alt+F4 quits.
- A single, colourful **Hints** dialog appears when Screenator starts. It
  summarises the common mouse and stylus controls plus Shape Editing tips, the
  global **F9** shortcut and the Drawing Mode and Screen Paper shortcuts in
  one place instead of repeating them for every tool, and it emphasises that
  a long right click — or a long press of the stylus second side button —
  opens the **Scroll Mode** menu to choose **Color** or **Thickness** for
  scrolling. It has a "Never show hints again" option which is remembered
  across restarts.
  **Show Hints** in the menu opens the dialog again, and Reset Settings keeps
  the choice.
- Pen, marker, font, eraser, shape and screen paper settings are saved in
  `~/.config/screenator.ini` and restored on the next start, together with the
  floating icon and settings indicator visibility. `Reset Settings` restores red, 6 px, 100%
  opacity, no shape fill, Arial Bold 24, a 40 px eraser, a 32x32 icon and no
  screen paper.
- `F9` is a global shortcut that shows or hides the floating icon at any
  time. `F8` selects the Pen tool, `F6` the Arrow tool and `F7` the Text
  tool. While a tool is active these switch immediately; otherwise they honor
  the Tool Activation Delay. The old Gromit-MPX keys (`Shift+F9`, `Ctrl+F9`,
  `Alt+F9`, `Shift+F8` and the old F8 undo) are no longer grabbed. While
  painting mode is active, `Ctrl+Z` undoes and `Ctrl+Shift+Z` redoes, even
  with Caps Lock or Num Lock on, and `Esc` exits the current tool like a
  right click outside a shape.
- Stylus: the tip draws like the left mouse button, using the selected line
  width; pen pressure is ignored so it behaves exactly like the mouse. The
  first side button has two short-press settings, chosen in the stylus menu:
  **Color** steps the line colour (or the fill colour over the Paint Bucket)
  and **Thickness** steps through the line thicknesses. A long press of the
  first side button activates the eraser while it is held, even when the pen
  is hovering above the tablet. The second side button acts like Escape/right
  click on a short click, finishing the shape being edited and keeping the tool
  active, or leaving painting mode when no shape is being edited. A long press
  opens the **Button 1 Mode** menu with Color and Thickness. With the Eraser
  tool, a short press of the first side button steps through the eraser sizes
  and a long press of the second side button does not open the menu. The hints
  dialog covers the stylus controls at startup and can be reopened with
  **Show Hints**.

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

- `screenator_5.5_amd64.deb`
- `Screenator-5.5-x86_64.AppImage`

The `.deb` can be installed with `sudo dpkg -i` (or `sudo apt install ./…`).
The AppImage is portable: make it executable and run it.

## License

Screenator is free software under the GNU General Public License version 2 (or
later). It contains code from Gromit-MPX 1.4.2, Copyright 2009-2022 Christian
Beier, and from Gromit, Copyright 2000 Simon Budig. See `COPYING` for the full
license text.
