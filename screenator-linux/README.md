# Screenator

Screenator is a vibe-coded screen annotation tool for X11, developed by
**Mahmoud Alnaanah** <malnaanah@gmail.com>. It is based on
[Gromit-MPX](https://github.com/bk138/gromit-mpx) 1.4.2.

Draw over any running application with a pen, arrow, text, shapes or eraser,
change the line colour, thickness and opacity, choose the text font, and
resize the floating icon. The drawing stays on screen as long as you need it,
and you can keep using your applications while it is visible.

Screenator starts in Screen mode with Plain selected for the next time Paper
mode is opened. Remembered PDF files, their pages and paper-size settings are
kept.

## Features

- A small draggable floating icon. Left or right click opens the menu with:
  Cursor, Pen, Text, Eraser, Arrow, Timer, Shapes (Marker, Line, Spline, Polygon,
  Triangle, Ellipse, Rounded Rectangle, Paint Bucket, Math Function, Parallel
  Lines, Random Signal), Actions (Switch Mode, Undo, Redo, Clear Screen,
  Toggle Visibility (Screen mode only), Screenshot, Show toolbar, Show Floating Icon, Show
  Hints), Line Thickness, Line Color, Shapes Fill Color, Paper, Eraser Size,
  Opacity, Marker Opacity, Settings..., About and Quit. Quit uses `quit.png` and is also the
  last button in the toolbar. The icon turns into its active (red)
  variant while a painting mode is on. The floating icon and tool bar stay
  clickable while a tool is selected; hovering them shows the normal pointer
  and suspends drawing.
  **Settings**
  opens a tabbed dialog with a **General** tab (save annotations, font,
  settings indicator, timer delay, reset settings, open configuration folder),
  a **Paper** tab
  (continuous scrolling and paper size), a **Toolbar** tab (direction, icon
  size, and each icon's visibility and order, changed by dragging), a
  **Floating Icon** tab (icon size and position) and a
  **Shortcuts** tab (every editable shortcut). The tool bar
  and floating icon are visible by default.
- Opening a PDF file with Screenator, either with **Open With** or with
  `screenator file.pdf`, starts it directly in **Paper** mode with that PDF.
  If Screenator is already running, the file is forwarded to the running
  instance. The installed desktop entry registers Screenator as a PDF handler.
- **Settings > Floating Icon** sets the floating icon size and position.
  Position choices are Top Right (default), Top Left, Bottom Right and Bottom
  Left. Size choices are 16x16, 32x32, 48x48, 64x64, 128x128 and 256x256; the
  default is 48x48. The settings ring doubles in width at 128x128 and triples
  at 256x256.
- The **Opacity** menu offers 10% to 100% in 10% steps. A separate **Marker
  Opacity** menu offers 10% to 90%.
- Marker draws a translucent highlighter stroke with a fixed 32 px width,
  using the current line colour. Its opacity is 40% by default and is
  independent of the global pen opacity. The **Marker Opacity** menu with 10%,
  20%, 30%, 40%, 50%, 60%, 70%, 80% and 90% is available from the app menu and
  tool bar; the choice is remembered across restarts. Selecting the Marker switches the line colour to yellow; leaving
  the Marker or exiting painting restores the colour that was active before.
  The mouse wheel or the stylus first side button changes its colour.
- **Timer** (F12) is available in Screen Annotation mode only. It releases the
  input for the **Timer Delay** set in Settings >
  General, giving you time to switch to another application. During the
  countdown other tools cannot be selected. When it ends, the Pen tool becomes
  active and normal tool selection resumes.
  If the pointer grab is blocked when the timer ends, for example by another
  application's context menu, Screenator freezes the current screen as a
  temporary background so the tool can draw over it; exiting the tool removes
  the frozen background.
- Line Thickness offers 1, 2, 3, 4, 5, 6, 12, 24 and 48 pixels; the menu marks the
  entry closest to the current width.
- While painting mode is active, the mouse wheel steps the line colours,
  including the custom colour, or the shape fill colours over the Paint Bucket.
  In Screen mode, with the Eraser tool selected, it changes the eraser size instead. Middle
  clicks never erase. Right click accepts typed text first, then commits
  visible handles on another click; a later right click starts erasing without
  exiting the selected tool.
  In Screen mode, with Eraser selected, right click or stylus button 2 changes its size.
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
  submenu; the default is no fill and green is its first colour. Their outlines
  are drawn outside the shape border, so a thick stroke never covers the
  inside of the shape. Line Color
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
- Polygon: the first click sets its centre, and dragging sets the radius and
  rotation around that centre; a plain click keeps a default size. The shape
  stays editable until it is committed. Six handles shape it: the square
  handle controls the number of sides (3 to 16); pressing it resets the shape
  to 3 sides. Moving away from the handle in any direction adds sides, and
  moving back towards it removes them again, down to 3 at the handle. One
  round handle sets the radius, one sets the rotation, one moves the inner
  vertices outwards until the shape becomes a regular polygon, and four corner
  handles scale it in X and Y after rotation; the inner handle snaps to the
  flat edge when it gets close, so a regular polygon is easy to hit. Drag inside the shape to move it. Clicking empty space commits it and
  places the next one; double-click or right-click commits it.
- Triangle: each click places the next vertex. A line follows the pointer after
  the first click, and the remaining sides follow after the second click.
  Dragging does not place a point; the line keeps following the pointer until
  the next click. The third click fixes the last vertex and closes the
  triangle. It stays editable until it is committed: each of
  its three vertices has its own round handle, so the shape can be stretched
  and skewed freely; holding `Ctrl` while dragging a vertex snaps the angle at
  that vertex to 30, 45, 60 or 90 degrees. Drag inside the triangle to move
  it. Clicking empty space commits it and places the next one, and a
  double-click or right-click commits it.
- The ellipse and rounded rectangle stay editable after the drag. Every placed
  shape has a square size handle on each of its four corners, a round rotation
  handle above it and can be moved by dragging inside it. Rotation is free;
  holding `Ctrl` snaps it to 0, 30, 45, 60, 90 and so on. The rounded
  rectangle keeps a roundness handle on its top edge as well: drag it away from
  the corner to round the corners more, or towards the corner for sharper
  corners. While dragging an ellipse or rounded rectangle, hold `Ctrl` to make
  a circle or square. Clicking outside, double-clicking or right-clicking commits the
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
  Fill Color. In Screen and Paper modes, the mouse wheel or stylus Button 1
  cycles the fill color, regardless of the wheel or stylus setting.
  In Paper mode, short-click stylus Button 1 or middle mouse to change color;
  hold and drag to pan after a brief 200 ms delay and a small movement, including while hovering.
  The area must be enclosed by drawn strokes and must not reach the
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
  point. Press `Enter` to place the text or `Esc` to cancel. In Screen mode, a
  left click or a stylus tip while the editor is open accepts the current text,
  commits its handles and starts a new one at the click position; in Paper mode
  a click does not place it. No click reaches the application below, and
  `Enter` or `Esc` enables the mouse again. The text
  and its blinking caret are drawn above the annotations and the paper page,
  so they stay visible even over a drawing. The middle mouse button does not
  erase in Screen mode and pans in Paper mode; it accepts typed text first.
  Right-to-left text
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
  settings indicator" in Settings turns it off and on; it is on by default and
  is disabled while the floating icon is hidden.
- Dialogs and message boxes, such as the Math Function dialog and the hints
  dialog, are shown above the drawings and the paper page, so both stay
  visible behind them. Every dialog has a title bar with a close button and a
  visible border, and each dialog is raised as it opens, which also works in
  virtual machines without a compositor.
- A **Paper** submenu holds the paper tools, and **Actions** holds Switch
  Mode (F3), Undo, Redo, Clear Screen, Screenshot, Show
  toolbar (F10), Show Floating Icon (F9) and Show Hints. **Switch Mode**
  turns **Paper** mode on or off and is also the first item in the toolbar.
  Plain, Ruled and Graph
  are white pages (with blue writing lines for Ruled and a grey square grid for
  Graph), and selecting **PDF File** opens the desktop's own file chooser (KDE,
  GNOME, etc.). **Reset Page or PDF File** in the Paper menu or the tool bar's
  Paper Type menu resets the current Plain, Ruled or Graph paper to one empty page,
  or opens another PDF when the current paper is PDF. If the current paper has
  unsaved changes, Screenator asks whether to save it in the Documents folder
  first. Selecting a paper type, or opening a PDF, switches to
  **Paper** mode automatically and brings the Paper window to the front.
  Switching between **Screen** and **Paper** always exits the current tool and
  leaves **Cursor** selected. The same applies when selecting a paper type,
  resetting a paper or opening a PDF. Every type keeps its own pages, drawings and
  loaded PDF while another type is shown, so switching back restores them
  without a save prompt. Switching paper types keeps the active tool. The selected paper type and PDF file are remembered
  across restarts, and each PDF file remembers the page it was last closed on,
  both when reopened from Screenator and when opened from the desktop. Paper
  saves write a PDF and show a progress overlay until the file is complete.
  Saving a screenshot shows a confirmation with the saved path. While
  **Paper** mode is active, the
  page is shown in a normal managed window, not a fullscreen overlay: other
  applications and dialogs can cover it, and Alt+Tab switches away normally.
  It takes the mouse and keyboard while focused, and stays when Clear Screen is
  selected. **Ctrl+W** switches to **Window mode**: the Paper window becomes a
  normal resizable window, the paper page width fits the window automatically,
  and the desktop panel can appear above it. The drawing tool and its cursor
  are active only inside the Paper window, and no global pointer grab is taken,
  so resizing, the window frame and other applications keep working normally.
  Ctrl/Alt shortcuts are received by the focused Paper window as ordinary key
  events instead of being grabbed globally. Pressing Ctrl+W again returns to
  fullscreen. **Ctrl+D** leaves fullscreen into a borderless, non-resizable
  window covering the monitor, so the desktop panel stays above the page;
  press it again to return to fullscreen. In every Paper presentation
  (fullscreen, Ctrl+D and Window mode), the drawing tool and cursor are
  limited to the Paper window, and Ctrl/Alt shortcuts are received only while
  that window is focused. **Ctrl+S**, or the **Save** toolbar button
  directly before **Paper Type**, saves only the current paper if it has
  changed. The file is written automatically to the **Documents** folder as
  `Screenator-plain-YYYYMMDD-HHMMSS.pdf`, `Screenator-ruled-...`,
  `Screenator-graph-...`, or `Screenator-file-...` for opened PDF files. If
  that name already exists, a numeric suffix is added. A message shows the
  saved path. Drawings are written as vector paths, text as text, and the
  original PDF pages and paper backgrounds stay vector. Undo and redo keep
  drawings and text vector as well. Eraser strokes over empty space leave the
  vector layer untouched; when ink is erased, the remaining drawings stay
  vector and text stays text. In **Screen** mode
  that button becomes **Screenshot**. When
  Screenator quits, it offers once to save every changed paper; no save
  prompt appears while switching paper types.
  **Ctrl+B** and **Ctrl+A** insert a page before or after the current page in
  Plain, Ruled and Graph. With **Continuous scrolling** on (the default), the
  selected paper is shown as one continuous vertical document with a grey gap
  between its pages, and **Up**/**Down** move smoothly through all of them.
  **Page Up**/**Page Down** move to the previous or next page and
  **Home**/**End** jump to the first or last page. With it off, pages are shown
  one at a time; scrolling moves within the
  current page and moves to the next or previous page at the top or bottom
  edge. In **Paper** mode, holding the mouse middle
  button or stylus side button 1 pans the paper and shows a hand cursor;
  holding the right mouse button or stylus side button 2 selects the Eraser
  and shows the eraser cursor. Eraser and Paint Bucket panning use a 200 ms delay; other tools pan immediately, and both work whether
  a tool is active or not; releasing the right button or stylus button 2
  restores the previous tool. A middle click accepts typed text or commits
  shape/text handles before panning. Right click accepts text first, commits
  its handles on the next click, then starts erasing on a later click. The mouse wheel
  scrolls the pages, except while a placed shape or text shows its handles:
  then the wheel and stylus button 1 change its colour. While text is being
  typed, the wheel and stylus side button 1 also change the text colour.
  Right click, stylus side button 2 or mouse button 2 accepts the text like
  Enter. **F10** shows
  or hides the tool bar in both **Screen** and **Paper** modes.
  By default the bar is horizontal along the top edge; **Settings > General**
  can make it vertical along the right edge, and **Settings > Toolbar** chooses
  which icons are shown and their order.
  It contains **Switch Mode** first, then **Cursor**, **Pen**, **Text**,
  **Eraser**, **Arrow**, **Timer**, **Shapes**, **Undo**, **Redo**, **Save**,
  **Paper Type**, **Insert Page Before**, **Delete Page**, **Insert Page
  After**, **Current Page**, **Go to Page**, **Clear Screen**,
  **Eraser Size**, **Line Color**,
  **Thickness**, **Fill Color**, **Opacity**, **Marker Opacity**, **Settings**,
  **Show Hints** and **Quit** last.
  **Paper Type**
  selects Plain, Ruled, Graph or
  PDF, and the insert-page buttons insert a page in an active Plain, Ruled or
  Graph paper; both are hidden in **Screen** mode. The **Timer** icon appears
  only in **Screen** mode. Toggle Visibility and Ctrl+T are available only in
  **Screen** mode; the menu item and toolbar icon are absent in **Paper** mode.
  **Ctrl+O** opens a PDF in **Paper** mode. **Save** saves the paper as
  a PDF, or takes a screenshot in **Screen** mode. **Cursor** exits the
  current tool and is available in both modes. In **Paper** mode, when no tool
  is active, the middle mouse button or stylus side button 1 pans the page,
  the wheel scrolls it and the right button or stylus side button 2 erases.
  Middle click never erases; with Eraser selected, stylus side button 1 erases.
  Drag any icon
  or the gaps between them to move the bar; a short click still selects the
  tool. The bar stays open when a tool or a menu entry is chosen, the selected
  tool is highlighted, its icons show tooltips, its direction and icon size
  (16 to 64 px) are set in **Settings > General**, its visible icons and
  order in **Settings > Toolbar**, and its shortcuts in **Settings >
  Shortcuts**. Its open/closed state is
  remembered. **Settings > Paper** also sets continuous scrolling and the
  paper size: Screen, A4 portrait, A4 landscape, or Custom width and height in
  millimetres. A4 and Custom pages always take the current screen or Paper
  window width, with their height following the paper ratio. The size applies
  to Plain, Ruled and Graph pages and scales their existing drawings; PDF pages
  keep the PDF's own size. Screen-size pages follow the Paper window's width
  and height in Window mode, and return to the display size in fullscreen.
  Window mode scales the canvas uniformly to the window, preserving shape
  proportions. The drawing stays anchored to the top when the window gets
  taller and to the left when it gets wider.
  Continuous PDF
  pages are separated by a grey gap so their edges stay visible.
  Screenator always starts on the normal desktop with Plain selected for the
  next Paper session; remembered PDF files, pages and paper-size settings stay
  available.
- A single, colourful **Hints** dialog appears when Screenator starts. It is a
  700x500 scrollable column that summarises the common mouse and stylus
  controls plus Shape Editing tips, the global **F9** shortcut and the Screen
  and Paper mode shortcuts in one place instead of repeating them for
  every tool. A "Never show hints again" option beside the Close button is
  remembered across restarts.
  **Show Hints** in the menu opens the dialog again, and Reset app settings
  and drawings keeps the choice. The hints text is loaded from the installed
  HTML file each time the dialog opens; it is not edited after installation.
  Headings, paragraphs, lists, bold/italic/underline text, links and coloured
  spans are supported. Simple `<style>` rules such as
  `h2 { color: #008800; }` and `span { color: #0000aa; }` also work.
- Pen, marker, font, eraser, shape and **Paper** mode settings are saved in
  `~/.config/Screenator/screenator.ini` and restored on the next start, together with the
  floating icon and settings indicator visibility, the selected paper type,
  and the opened PDF file and page. `Reset app settings and drawings` deletes
  the configuration file and saved drawings, clears every paper page, and
  restores red, 6 px, 100% opacity, no shape fill, Arial Bold 24, a 60 px
  eraser, a 48x48 icon, a 2 s Timer Delay, a visible 32 px horizontal tool bar
  with all icons, and **Paper** mode off. Beside it, `Open Configuration
  Folder` opens `~/.config/Screenator`, and the folder path is shown below the
  buttons.
- `F9` is a global shortcut that shows or hides the floating icon at any
  time. `F8` selects the Pen tool, `F6` the Arrow tool, `F7` the Text
  tool and `F12` starts the Timer. `F10` shows or hides the tool bar in
  both modes (horizontal on the top by default; vertical on the right when
  chosen in **Settings > General**). `F3` activates the selected paper
  or turns it off. `F4` saves a screenshot of the current screen, including
  the annotations or paper page, to the Pictures directory as
  `Screenator-YYYYMMDD-HHMMSS.png` in both **Screen** and **Paper** modes. The old Gromit-MPX keys (`Shift+F9`, `Ctrl+F9`,
  `Alt+F9`, `Shift+F8` and the old F8 undo) are no longer grabbed. While
  painting mode is active, `Ctrl+Z` undoes the last drawing stroke or page
  insertion/deletion and `Ctrl+Shift+Z` redoes it, even with Caps Lock or
  Num Lock on. `Esc` first finishes the current shape or closes its handles
  and keeps the selected tool. Press it again to exit the tool and select the
  **Cursor** tool, so its icon becomes active. Drawing undo is used first
  while drawing history exists. Undo history keeps 50 steps in every mode and
  covers the whole document: switching pages does not clear it, and undo
  returns to the page and view where the step was made.
- Stylus: **IMPORTANT:** In your stylus configuration, set button 1 (near the
  tip) to **Middle Click** and button 2 to **Right Click**. The tip draws like
  the left mouse button, using the selected line
  width; pen pressure is ignored so it behaves exactly like the mouse. The
  first side button and the middle mouse button do not erase. With Pen
  selected, middle mouse does nothing. While a text or shape still shows its
  handles, the first side button changes its colour instead. The second side
  button acts like right click: it accepts text, then commits visible handles
  on another click before a later click erases, without exiting the tool. In Screen mode, with Eraser selected it
  changes eraser size; the first side button erases in Screen mode when Eraser is selected.
  There is no side-button popup menu. In **Paper Drawing** the buttons
  change roles: hold side button 1 or the middle mouse button to pan, and hold
  side button 2 or the right mouse button to erase. Middle click never erases.
  No long press is needed;
  releasing the right button or side button 2 brings the previous tool back.
  A middle click accepts typed text or commits handles before panning. The
  mouse wheel scrolls the pages except while text is being typed or handles
  are edited, when it changes colour. The hints dialog covers the
  stylus controls at startup and can be reopened with **Show Hints**.

In **Paper** mode, **Clear Screen** clears the whole page with the largest
visible area and preserves neighboring pages. With **Eraser** selected, the
wheel scrolls pages; a short Button 1 (middle-click) press changes eraser size,
while holding and dragging pans the page after a brief 200 ms delay and a
small movement, including while hovering. Holding Button 2 (right-click) erases while hovering.

## Build

Dependencies: GTK+ 3, Ayatana AppIndicator 3, XInput 2, X11, Poppler GLib,
CMake and a C compiler. On Debian/Ubuntu:

```
sudo apt install build-essential cmake libgtk-3-dev \
    libayatana-appindicator3-dev libpoppler-dev libpoppler-glib-dev \
    libxi-dev libx11-dev librsvg2-bin
```

Then run:

```
./build.sh
```

The script builds the project and produces both packages in `../build/`:

- `screenator_8.0_amd64.deb`
- `screenator-8.0-1.x86_64.rpm`
- `Screenator-8.0-x86_64.AppImage`

The `.deb` can be installed with `sudo dpkg -i` (or `sudo apt install ./…`).
The AppImage is portable: make it executable and run it.

## License

Screenator is free software under the GNU General Public License version 2 (or
later). It contains code from Gromit-MPX 1.4.2, Copyright 2009-2022 Christian
Beier, and from Gromit, Copyright 2000 Simon Budig. See `COPYING` for the full
license text.
