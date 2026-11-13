# Screenator for Windows 8.0

A native Windows port of [Screenator 6.0](https://github.com/malnaanah/screenator),
the screen annotation tool for X11. Draw over any running application with a
pen, marker, arrows, text, shapes, math plots or the eraser while the desktop
keeps running underneath.

This port keeps Screenator's cairo-based drawing and tool behaviour and
replaces the X11/GTK platform layer with Win32: a per-pixel layered overlay,
native menus and dialogs, a tray icon, global hotkeys and a floating icon with
the settings ring. It now includes the applicable Linux 8.0 behaviour; the
version 8.0.

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
- Clicking a new spot while a line, arrow or frame still shows its handles
  finishes the old shape and starts the new one with that same click.
- Spline: the last node can be dragged; dragging a node or its handles hides
  the preview segment after the last point, and clicking the last node without
  moving finishes the curve. Stylus hover updates are filtered so the
  rubber-band preview does not flash stray segments.
- Inline text editing drawn directly over the desktop with a blinking caret;
  a hidden focus window keeps typing, IME, selection and clipboard working.
  Clicking the left mouse button again, or pressing the stylus tip, accepts the
  current text and starts a new text at that point. While text or shape handles
  are being edited, middle click changes color and keeps editing active. Use
  Enter or right click / stylus button 2 to accept text or finish handles.
- Cursor tool for leaving the desktop usable, plus two-stage Escape/right-click
  handling: the first press closes shape handles, a further press exits to
  Cursor. Right click or stylus Button 2 accepts inline text like Enter.
- HTML hints are read from `assets/hints.html` only; they are not copied into
  the settings folder and the file is never modified by the app. Nested lists
  are indented and every bullet of a list starts at the same position.
- Dialogs stay open until their Close/Cancel button or Escape is used; clicking
  outside a dialog in Paper mode no longer closes it. Opening a dialog accepts
  active text and returns the current drawing tool to Cursor.
- Actions menu, marker-opacity menu, F3 Screen/Paper switch, F12 two-second
  timer and F4 screenshot saved to Pictures. The floating icon and toolbar
  are hidden during screenshot capture and restored afterward. Selecting Timer finishes the
  current edit and switches to Cursor while waiting, then activates Pen when
  the timer expires. If the timer ends while a context
  menu is open, the menu is captured as a background image and closed, so the
  first drawing attempt draws over the menu image (Screenator 7.0 behaviour).
- Undo (50 steps) and redo follow drawing edits across all paper pages and
  documents, switching to the page each change belongs to when needed.
- Line colours, custom colours, shape fill colours, nine line widths, opacity
  (10% to 100% in steps of 10%, applied to strokes and fills), eraser sizes,
  marker opacity and a configurable font.
- Math plots break at real poles such as `1/x` and `tan(x)` instead of drawing
  a false vertical line, and `delta()` is drawn as an arrow at each zero of its
  argument.
- Paper types are **Plain**, **Ruled**, **Graph** and **PDF File**, each with
  its own separate document, pages and annotations. Switching type keeps the
  other documents and their drawings; the screen annotations are a separate
  layer and are restored when leaving Paper.
- Paper documents with up to 1024 pages. Each page owns its own annotation layer
  and a plain/ruled/graph/PDF background. Insert before/after, delete, page
  navigation (Page Up/Page Down, Home/End), Go to Page and page-level undo/redo
  shortcuts are available. The Paper menu no longer shows `Page - / -` or
  offers None, Undo Page Change or Redo Page Change; the toolbar still shows the
  current-page icon, which updates while scrolling and opens **Go to Page** when
  pressed. The menu's single **Reset Page or PDF File** item opens a PDF when the
  current type is PDF and resets the page otherwise, offering to save first.
- **Settings > Paper > Continuous page scrolling** is enabled by default and
  shows all pages as one scrollable strip. The mouse wheel and Page Up/Page Down
  scroll it; clicking a page makes it current and drawing lands on the page
  under the pointer. The strip shows the page edges but no `Page x / y` label
  or separator line between pages.
- **Clear Screen** clears the annotation on the page occupying the largest
  visible area. This also follows the visible portion of the page in the
  resizable Paper window.
- **Settings > Paper > Paper size** offers **Screen**, **A4 portrait**,
  **A4 landscape** and **Custom** width/height (mm). The page width always
  equals the screen width for every size; taller pages scroll vertically, and
  exported PDFs use the selected dimensions. The configured page size is
  remembered across restarts. Each launch starts with fresh pages and no PDF
  open; the previous Paper session is not restored.
  An opened PDF keeps its own page proportions and is not affected by this
  setting (the width still fills the screen). Each opened PDF reopens at the
  page where it was last left.
- Switching between Screen and Paper leaves the current tool and returns to the
  cursor; switching paper type or PDF file keeps the tool active. In Paper
  mode the window is in Alt+Tab and on the taskbar; `F3` brings the paper
  forward when another application is in front of it. Alt+Tab moves straight
  between the paper and other applications, and clicking the paper after
  switching back keeps it in front and keeps drawing (with or without a tool).
  Selecting a paper type from the toolbar saves the screen layer first, so
  Screen mode is not affected by paper drawings.
- Holding **Ctrl** while drawing or resizing a rectangle or ellipse keeps the
  aspect ratio at 1:1 (square/circle). Spline handles show separate guides from
  each node, and polygon corner handles scale the shape on the x and y axes
  independently, using screen axes even when the shape is rotated.
- The toolbar page icon shows the current page number only. Its tooltip is
  `Page X/Y (Press to change page)` and updates when pages are added or removed.
- Application menu: **Timer** is in the main list after **Eraser**; **Show
  Toolbar** and **Show Floating Icon** live in **Actions** before **Show
  Hints**; the main menu no longer repeats submenu items.
- The continuous strip starts at the first page's top edge; a gap is shown only
  between pages. A single page has no extra space above or below it.
- Closing with **Alt+F4**, the quit button or a window close asks whether to
  save unsaved changes (Yes/No/Cancel), listing every modified paper type and
  the PDF, including their page counts. Yes saves all listed documents with
  all their pages to Documents. One final message lists the saved files and
  any failures. A failed save keeps Screenator open for retry. Documents with
  no changes since their last save are skipped. Settings are saved on exit.
- The paper is not topmost by default, so Alt+Tab lets other applications cover
  it. **Ctrl+D** toggles whether the paper covers the taskbar. **Ctrl+W**
  switches between full-screen Paper and a resizable Paper window.
- Selecting **PDF File** opens a PDF the first time and then reuses that
  document; opening another replaces it. The contextual **Reset/Open PDF** item
  opens a file when the current type is PDF and resets the current page for the
  other types. Both paths offer to save when the page or PDF has annotations.
- The tool bar snaps flush to the screen edge with no gap.
- In Paper mode the middle mouse button or the first stylus side button changes
  color while text or shape handles are being edited. After editing is finished,
  the middle button pans the continuous strip. Right click and stylus button 2
  first accept text,
  then finish shape editing; after those steps they erase while held. The
  first stylus side button changes the color while text or shape handles are
  active. Choosing a paper type or opening a PDF brings the paper to the front.
- **Ctrl+W** toggles Paper mode between the full-screen strip and a framed
  window; drawing, the toolbar and the floating icon all work the same inside
  the window. Screen-size pages follow both the window width and height while
  keeping drawings at a uniform scale.
  Scrolling paints during the drag (about 60 fps) and continues on every
  pointer update while the button is held; raw-HID movement reports without the
  button usage no longer end the pan, the freshest pen position is used, and
  promoted pen mouse movement pans too, so panning follows the pen instead of
  moving only when the button is released. The 40 ms pen poll advances the pan
  from the live pen position, and a 16 ms paper-pan timer follows the cursor
  while the middle button is held, so panning works even if the driver sends no
  movement messages during the drag. All pan sources use screen coordinates, so
  pointer and mouse updates can be mixed without a jump.
- Annotation export is vector: strokes and shapes become PDF paths and text
  stays selectable text. When a PDF page is saved, the original PDF page is
  imported as its original vector form layer and the annotations are composed
  over it as a second vector form, so source fonts/text/vector art are
  preserved. Pages without annotations use an opaque white backdrop behind the
  background, so no transparency-mask images are added. Eraser brushes are
  exported as an alpha mask, keeping the drawings and text vector; bucket fills
  add a small raster patch.
- PDF pages are opened with **Paper > Open PDF...** (Ctrl+O) and rendered
  with PDFium, scaled uniformly and centred on the screen-sized paper. Encrypted
  or broken PDFs are reported with a clear message, and the last page used for
  each PDF is remembered.
- **Save Paper...** (Ctrl+S) writes a multi-page PDF to Documents named
  `Screenator-plain-YYYYMMDD-HHMMSS.pdf`,
  `Screenator-ruled-YYYYMMDD-HHMMSS.pdf`,
  `Screenator-graph-YYYYMMDD-HHMMSS.pdf` or
  `Screenator-pdf-YYYYMMDD-HHMMSS.pdf` according to the current page type.
  Saving with no changes since the last save shows a message and does not
  create another file.
  Plain/ruled/graph backgrounds are exported as Cairo vector graphics. For
  PDF-backed pages the original PDF page is imported unchanged as a vector form
  layer and the annotations are overlaid as a second vector form; page
  rotation is not remapped. See `PORTING_STATUS.md`.
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
- Persistent tool bar (F10) in the Linux 8.0 order, with native tooltips that
  include the configured shortcut, horizontal or vertical layout, icons from 16
  to 48 pixels (Settings slider, any value), no gaps between or around the
  icons, drag/snap positioning by the icons (no separate grip) with a flush
  screen-edge snap, and **Settings > Configure Toolbar...** for showing, hiding
  and reordering actions. Configure lists every action with its icon and lets
  rows be dragged by their handle; hidden actions keep their row and can be
  restored later. Page actions appear only in Paper mode; Timer and Toggle
  Visibility only in Screen mode. Paper Type opens the paper/PDF menu; page
  insert/delete, Save/Screenshot and Go to Page are included. Line Color,
  Thickness, Fill Color and Eraser Size show live samples; the eraser sample is
  circular and the thickness sample is a flat black line that spans 70% of the
  toolbar's height (horizontal bar) or width (vertical bar).
  Changing the floating-icon size in Settings resizes the icon immediately.
- Tray menu, editable hints dialog with an About button, quick tool menu
  (Ctrl+F10), and a tabbed **Settings** dialog (General, Paper, Toolbar,
  Floating Icon), math dialog and About window. Modal dialogs stay above the
  overlay and the toolbar stays visible and usable while they are open.
  Floating-icon size lives in
  Settings; the toolbar is toggled with **F10**. Paper types, Open PDF and
  Save Paper are not in Settings. General includes **Edit Shortcuts...** and
  **Open Configuration Folder**.
- **Settings > General > Edit Shortcuts...** edits the global keys without
  rebuilding: `F6` Arrow, `F7` Text, `F8` Pen, `F9` Floating Icon, `F10`
  Toolbar, `F3` Switch Mode, `F4` Screenshot, `F12` Timer, `Ctrl+Z` Undo,
  `Ctrl+Shift+Z` Redo, `Ctrl+Q` Leave Paper, `Ctrl+T` Toggle Visibility (Screen mode only) and
  `Alt+F4` Quit. The menu labels follow the configured keys. Accepted values
  are combinations such as `F8`, `Ctrl+S`, `Alt+F4`, or `None`; Escape stays
  fixed. Invalid or conflicting keys are rejected and the previous binding is
  kept. `F12` is reserved by Windows, so the Timer key is read by a keyboard
  hook instead of a registered hotkey.
- Settings saved to `%APPDATA%\Screenator\screenator.ini`; drawings saved to
  `%APPDATA%\Screenator\screenator-annotations.png`. The INI stores toolbar
  direction, icon size, position, the ordered `toolbar_items` list (plus a
  `toolbar_order` list so hidden rows keep their place), the
  `[Shortcuts]` section, and the current paper type/page count/page index/PDF
  path. Last pages for opened PDFs are kept in
  `%APPDATA%\Screenator\paper-pdf-pages.txt`. Reset Settings clears the screen
  and paper drawings and the saved annotations, removes the whole
  `%APPDATA%\Screenator` directory, restores every default, and places a
  horizontal toolbar at the top centre of the screen, flush with the top edge.

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
| Toolbar | visible, horizontal, 32 px icons, all 29 actions |
| Marker opacity | 40% |

## Shortcuts

The global shortcuts are editable in **Settings > General > Edit Shortcuts...**. The table
below lists the defaults; menu labels show the currently configured keys.

| Action | Default |
|---|---|
| Arrow tool | `F6` |
| Text tool | `F7` |
| Pen tool | `F8` |
| Floating icon | `F9` |
| Toolbar | `F10` |
| Switch Screen/Paper | `F3` |
| Screenshot | `F4` |
| Timer | `F12` |
| Undo | `Ctrl+Z` |
| Redo | `Ctrl+Shift+Z` |
| Leave Paper | `Ctrl+Q` |
| Toggle Visibility (Screen mode only) | `Ctrl+T` |
| Quit | `Alt+F4` |

- `Ctrl+F10` remains the quick tool menu.
- While painting: `Esc` closes shape handles, a further `Esc` exits to Cursor.
- In Screen mode, `Ctrl+T` shows or hides the drawings. Toggle Visibility and
  `Ctrl+T` are unavailable in Paper mode. In Paper mode, `Ctrl+Q` leaves the
  paper and `Alt+F4` quits.
- Paper pages: `Ctrl+W` toggles the full-screen and resizable Paper window;
  `Ctrl+S` saves the paper to Documents, `Ctrl+O` opens a PDF,
  `Ctrl+B` inserts a page before the current page, `Ctrl+A` inserts a page
  after it, `Page Up`/`Page Down` move one page, and `Home`/`End` jump to the
  first/last page. `Ctrl+Z`/`Ctrl+Shift+Z` undo/redo drawing changes across all
  pages and documents (50 steps); structural page changes use the same keys and
  clear the drawing history.
- `Ctrl+D` in Paper mode shows the Windows taskbar by stopping the paper at the
  work area; pressing it again covers the taskbar again.
- Paper shortcuts also work while the pointer is over the toolbar or floating
  icon, and remain scoped to those controls so other apps keep their shortcuts.
- A shortcut can be set to `None`; Escape itself is fixed.

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
  Screenator.exe
  pdfium.dll              PDF engine (BSD, required for PDF support)
  Screenator Data/
    assets/               icons, cursors and the application icon
    README.md
    RELEASE_NOTES.md
    COPYING
    AUTHORS
    PDFIUM_LICENSE.txt
```

The executable is statically linked against the mingw-w64 runtime. PDF support
uses the BSD-licensed `pdfium.dll`, which must stay next to `Screenator.exe`.
The `Screenator Data` folder travels with it.

## Installer

`installer/screenator.nsi` builds a per-user installer with NSIS:

```
pacman -S --needed mingw-w64-x86_64-nsis
cd installer
./build-installer.sh
```

On Linux or macOS install `nsis` and run the same wrapper; it adds
`-DPOSIX_BUILD` so the POSIX NSIS uses the right path separators. The result is
`release/Screenator-8.0-win64-setup.exe`. The installer places the app in
`%LOCALAPPDATA%\Programs\Screenator`, creates Start menu entries, and includes
the **PDF Open with list** task, selected by default. That task registers
Screenator in the right-click **Open with** menu and in
**Settings > Apps > Default apps** without changing the current default PDF
viewer. Installing or uninstalling also removes PDF associations left behind by
an older portable copy (the generic `pdf_auto_file` ProgID and stale
`OpenWithList` entries), which Windows otherwise gives no way to remove.

If an old association is still present without reinstalling, run
`installer/remove-pdf-association.ps1` (also shipped in `Screenator Data`).
It removes only Screenator entries and never touches the `UserChoice` default.

`Screenator.exe "C:\path\file.pdf"` opens the PDF in Paper mode. Starting it a
second time forwards the file to the already-running instance.

## Verification

Developer test modes (they do not touch the normal settings or drawings):

```
Screenator.exe --selftest   # rendering, paper pages, PDFium load/render/export, math and bucket
Screenator.exe --uitest     # tools, undo/redo, menus, dialogs and config round trip
Screenator.exe --smoke      # starts the full UI and exits after two seconds
Screenator.exe --debug      # writes debug.txt next to the executable (add --pen-test for a tool)
Screenator.exe --pen-test   # 20 seconds with the Pen active; logs pen events
```

`--selftest` creates and removes a small PDF in the executable folder to check
PDFium loading, rendering, Cairo PDF export and the encrypted/broken-file
error path. The normal settings and annotation files are not changed.

## Notes on the Windows port

- In Screen mode, the mouse wheel changes color only while typing text or
  editing text/shape handles. Otherwise it does nothing, even with a tool
  selected. In Paper mode, the wheel scrolls outside editing.
- The Screen overlay is a layered, always-on-top Win32 window. While a tool is
  active it takes the mouse; otherwise clicks pass through to the applications
  below.  **Paper mode uses a normal, opaque window** (painted from the same
  Cairo frame) that is topmost only while it is active, so Windows Ink delivers
  live stylus side-button drags and other applications still cover the paper
  when the user switches away.
- Pango/Cairo provide the same anti-aliased drawing and text rendering,
  including right-to-left text.
- Stylus pens use Windows Ink pointer input plus a raw HID fallback: the tip
  draws and the eraser tip erases. With no text or shape handles active, the
  middle mouse button does nothing with the Pen tool selected. With the Eraser
  tool selected, stylus button 1 erases
  while held, including while the pen hovers. While the text editor is open or a
  shape/text still shows its editing handles, the first side button changes the
  color like the wheel instead; the text editor stays open and the handles stay
  visible. Middle mouse changes the color while editing and keeps the editor or
  handles active. After editing is finished, middle mouse pans the Paper page.
  In Screen mode, middle mouse / Button 1 does nothing with Text or a shape
  tool selected after the editing handles are closed.
  This works while the pen hovers or
  touches, and also when the driver promotes the side button to a middle,
  right or browser mouse event: the global mouse hook turns that press into a
  color step wherever the pen is over Screenator. The second side button
  finishes or exits on a short press and accepts inline text; the right-button
  menu is no longer shown in Screen mode. In Paper mode the first side button /
  middle mouse pans and the second side button / right mouse is the temporary
  eraser. When the pen exposes its HID barrel switch, Screenator reads it
  directly so the first side button also works while the pen is merely
  hovering; pointer-second-button reports are accepted as well. Recent pen
  activity also lets promoted side-button mouse messages be
  recognised as pen input. A real mouse click clears any stale pen state, so
  switching from the stylus to the mouse works immediately.
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
- Paper pages are screen-sized, matching the current virtual screen. Page
  annotation layers scale with the same centred scaling as the main buffer
  when the display or DPI changes. Paper metadata and page annotations for the
  current session are kept in memory. On exit, the existing
  `screenator-annotations.png` persistence stores the current page layer; other
  page layers are not written to that PNG yet.
- **PDF export:** PDFium is used for importing the original PDF pages. A
  PDF-backed paper page is exported by importing its source page as the
  original PDF form layer (vector paths, embedded fonts, text and images
  intact) and composing a Cairo-generated annotation form over it. Annotation
  paths and text therefore stay vector/text on PDF pages. Eraser marks keep the
  annotation overlay raster while the original PDF page remains vector. Page
  rotation inside the source PDF is not remapped when overlaying annotations.

## License

GNU General Public License version 2 or later. This program is based on
Screenator 6.0 by Mahmoud Alnaanah, which is based on Gromit-MPX 1.4.2
(Copyright 2009-2022 Christian Beier) and Gromit (Copyright 2000 Simon Budig).
See `COPYING` for the full text.
