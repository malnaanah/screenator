# Screenator 7.0

Screenator 7.0 brings the Linux application to version 7.0 and introduces the
native Windows port.

## Linux

- Added 128x128 and 256x256 floating-icon sizes. The settings-indicator rings
  are twice as wide at 128x128 and three times as wide at 256x256.
- Made 48x48 the default floating-icon size and 60 px the default eraser size.
- Polygon, ellipse, rounded rectangle and triangle outlines are now drawn
  entirely outside the shape border.
- Marker opacity and the application Opacity menu now offer 10%, 20%, 30%,
  40%, 50%, 60%, 70%, 80% and 90%.
- Polygon placement now uses the first click as its centre; dragging sets the
  radius and rotation around that centre.
- Triangle placement now uses three clicks, with the preview lines following
  the pointer between clicks. Dragging does not place a point.
- The hints dialog is a single scrollable 750x550 column. The scrollbar is
  always visible and styled for contrast, starts at the top, and the
  "Never show hints again" option sits beside the Close button.
- Added the stylus button configuration note to the hints dialog and
  documentation.
- Improved pen detection under XWayland and corrected middle-button erasing.

## Windows

- Native Win32 port with the same cairo-based drawing tools.
- Pen, marker, eraser, text, line, arrow, spline, polygon, triangle, ellipse,
  rounded rectangle, paint bucket, math function, parallel lines and random
  signal tools.
- Editable shapes, outside strokes for closed shapes, Ctrl angle snapping,
  opacity controls, screen paper, undo/redo, saved annotations, floating icon,
  tray menu and quick tool menu.
- Native Windows Ink and HID pen support, including stylus side buttons.

## Assets

- `screenator_7.0_amd64.deb`
- `screenator-7.0-1.x86_64.rpm`
- `Screenator-7.0-x86_64.AppImage`
- `Screenator-7.0-win64.zip`
