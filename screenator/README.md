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
  Pen, Text, Eraser, Shapes (Marker, Free Line, Arrow, Angled Line, Angled
  Arrow, Rectangle, Ellipse, Rounded Rectangle), Line Thickness, Line Color,
  Shapes Fill Color, Eraser Size, Opacity, Floating Icon Size, Undo, Redo,
  Change Font, Clear Screen, Toggle Visibility, Show/Hide Floating Icon,
  Reset Settings, About and Quit.
- Marker draws a translucent yellow highlighter stroke at 50% opacity with a
  fixed 32 px width. It has its own settings and switching to it does not
  change the line colour, thickness or opacity used by the other tools.
- Shapes are drawn by dragging: press where the shape should start, move the
  pointer and release. Free Line and Arrow use the drag direction as-is,
  while Angled Line and Angled Arrow snap to 0, 30, 45, 60, 90, 120, 135 and
  150 degrees. Arrow and Angled Arrow add a head at the end of the line.
  Rectangle, ellipse and rounded rectangle can optionally be filled using the
  same colours as the line colour menu; the default is no fill.
- The eraser size can be set to 20, 40 or 60 pixels. Both the erasing area and
  the circular eraser cursor follow the selected size.
- The Text tool shows an I-beam cursor and writes directly at the click
  position: a small inline editor appears where you click, using the pen
  colour and the chosen font. Press `Enter` to place the text or `Esc` to
  cancel. Right-to-left text such as Arabic extends to the left of the click
  position.
- The same menu is shown by the system tray icon.
- Pen, marker, font, eraser and shape settings are saved in
  `~/.config/screenator.ini` and restored on the next start. `Reset Settings`
  restores red, 6 px, 100% opacity, no shape fill, Arial Bold 24, a 40 px
  eraser and a 48x48 icon.
- Hotkeys: `F9` toggle painting, `SHIFT-F9` clear, `CTRL-F9` toggle
  visibility, `ALT-F9` quit, `F8` undo, `SHIFT-F8` redo.

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

- `screenator_1.0_amd64.deb`
- `Screenator-1.0-x86_64.AppImage`

The `.deb` can be installed with `sudo dpkg -i` (or `sudo apt install ./…`).
The AppImage is portable: make it executable and run it.

## License

Screenator is free software under the GNU General Public License version 2 (or
later). It contains code from Gromit-MPX 1.4.2, Copyright 2009-2022 Christian
Beier, and from Gromit, Copyright 2000 Simon Budig. See `COPYING` for the full
license text.
