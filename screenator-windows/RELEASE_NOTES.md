# Screenator for Windows 8.0

A portable 64-bit Windows release of the Screenator screen annotation tool,
based on Screenator 6.0 by Mahmoud Alnaanah.

## Highlights

- Closing lists unsaved changes across all paper types and the PDF. Saving
  exports every modified document with all its pages and lists the output
  files together. Save failures keep the application open for retry.
- Start each application session with fresh Paper pages and no PDF open,
  preserving the saved page position for each previously opened PDF.
- Fixed drawing undo/redo across pages: both operations capture and restore
  the affected page, even when another page is currently selected.
- Screenshots temporarily hide both the floating icon and toolbar, then
  restore their previous visibility.
- Selecting Timer finishes the current edit and switches to Cursor for the
  countdown, then activates Pen when the timer expires.
- In Screen mode, the mouse wheel changes color only while typing text or
  editing text/shape handles. Outside editing it no longer changes tool color
  or size, even with a tool selected.
- Updated pointer behavior to match Linux 8.0: right click and stylus Button 2
  accept text first, finish visible shape editing next, then erase while held.
  Middle click does nothing with Pen selected. With Eraser selected, stylus
  Button 1 erases while held, including during hover. Screen-size paper pages
  now follow both dimensions of a resized Paper window without distorting
  drawings. The Cursor menu item now shows its icon and the **Cursor (Esc)**
  label.
- Added a persistent tool bar (F10) in the Linux 8.0 order, with drag/snap
  (including dragging by the icons), clamping to the screen edges,
  horizontal/vertical, icon size, active-tool highlighting, native tooltips
  and the Linux 8.0 icon set. Page actions appear only in Paper mode; Timer
  and Toggle Visibility appear only in Screen mode. The Save/Screenshot icon
  follows the mode. Shapes opens only the Shapes menu; Paper Type opens the
  paper/PDF menu; option buttons open their own menus; Settings opens the
  Settings dialog. Line Color, Thickness, Fill Color and Eraser Size show
  live samples (circles for the eraser) instead of static icons.
  Previous/Next Page were removed from the menu and toolbar.
  **Settings > Configure Toolbar...** shows, hides and reorders actions, and
  hidden actions keep their row when restored.
- Added a tabbed native **Settings** dialog with General, Paper, Toolbar and
  Floating Icon tabs. Floating-icon size is configured there. Paper types,
  Open PDF, Save Paper, Show Floating Icon and Show Toolbar were removed from
  the dialog (**F10** still toggles the toolbar). **Edit Shortcuts...** is on
  the General tab, and General also has **Open Configuration Folder**.
  **Settings > Configure Toolbar...** lists every action with its icon.
- Added editable shortcuts in **Settings > General > Edit Shortcuts...** for
  Arrow, Text, Pen, Floating Icon, Toolbar, Switch Mode, Screenshot, Timer,
  Undo, Redo, Leave Paper, Toggle Visibility and Quit. Values such as `F8`,
  `Ctrl+S`, `Alt+F4` and `None` are accepted; invalid or duplicate keys are
  rejected and the previous binding is kept. Menu labels follow the configured
  keys. Escape and the `Ctrl+F10` quick tool menu remain fixed.
- Added the Windows **Paper** mode with up to 1024 pages. Each page has its own
  annotation layer and a plain, ruled, graph or PDF background. Page insert
  before/after, delete, Home/End, Go to Page and page-level undo/redo shortcuts
  are available. In Paper mode the right button or stylus Button 2 acts as a
  temporary eraser without exiting the tool.
- **Plain, Ruled, Graph and PDF File are separate documents** with their own
  pages and annotations. Switching type keeps the other documents, so returning
  to a type restores its drawings; the screen annotations are a separate layer.
  The Paper menu no longer shows `Page - / -`; the toolbar still shows the
  current-page icon.
- Added **Settings > Paper > Continuous page scrolling**, enabled by default.
  All pages are shown as one vertically scrollable strip. The mouse wheel and
  Page Up/Page Down scroll it; clicking a page draws on that page and Home/End
  or Go to Page jumps to the first/last page. The page under the pointer is
  selected before a stroke, so annotations always land on the correct page.
- Added **Settings > Paper > Paper size**: **Screen**, **A4 portrait**,
  **A4 landscape** or **Custom** width/height in millimetres. The page width
  always fills the screen/window width; taller pages scroll vertically in the
  continuous strip, and PDF export uses the selected page dimensions.
- Changing the floating-icon size in Settings now resizes the icon immediately.
- Added **100%** to the Opacity menus in the application menu and toolbar.
- Removed **None** and the page undo/redo items from the Paper menu and the
  toolbar Paper Type menu; `Ctrl+Z`/`Ctrl+Shift+Z` still undo/redo page changes.
  The single **Reset Page or PDF File** item opens a PDF when the current type is PDF
  and resets the current page otherwise, offering to save changed annotations
  first.
- **PDF File** is a selectable paper type; it opens a PDF the first time and
  reuses that document afterwards.
- The tool bar snaps flush to the screen edge with no gap, has no spacing
  between or around the icons, and no separate drag grip; drag the bar by any
  icon. Tooltips include the configured shortcut.
- **Settings > Toolbar > Icon Size** is a slider covering every size from 16 to
  48 pixels, with a live value label; changing it resizes the icons immediately.
  **Configure Toolbar** lists every action with its icon and reorders rows by
  dragging the row handle (double-click still shows/hides a row).
- The line-width sample is a flat black line in the orientation opposite to the
  toolbar, spanning 70% of the toolbar's height (horizontal bar) or width
  (vertical bar). The **Configure Toolbar** list now shows the icon of every
  action instead of only the first few.
- Screen and paper drawings are strictly separated: screen strokes never appear
  on a paper page anymore, including when opening a PDF while in Screen mode or
  changing a paper setting from Settings.
- **Settings > Paper** no longer shows the page status line or the explanatory
  text below Continuous page scrolling.
- **Reset Settings** now clears the screen and paper drawings and the saved
  annotations, removes the configuration directory, and puts a horizontal
  toolbar at the top centre of the screen, flush with the top edge.
- In Paper mode a right click while a text editor or a shape's handles are
  active first finishes that edit; erasing starts only with the next right
  click, and the tool itself is never exited.
- The current-page toolbar icon updates while scrolling and opens **Go to
  Page** when pressed; its label is **Current Page (Press to change page)**.
  The inline `Page - / -` menu row was removed.
- **Reset Page or PDF File** replaces Reset/Open PDF in the application and
  toolbar menus. It opens a PDF for PDF pages and resets the page otherwise,
  offering to save changed annotations first.
- Undo/redo now spans paper pages and documents and keeps 50 steps in every
  mode. Undo switches back to the page a change belongs to.
- Switching paper type or page keeps the active tool armed; it no longer falls
  back to the cursor.
- In Screen mode the stylus first side button and the middle mouse button are
  an immediate temporary eraser. While the text editor is open or a shape/text
  still shows its editing handles, the first side button changes the color like
  the wheel instead of erasing. The right-button Scroll Mode menu is not shown
  in Screen mode. The middle-button eraser keeps erasing while the button is
  dragged instead of only marking the first press.
- The floating icon no longer blinks when a tool is selected in Paper mode:
  choosing a tool no longer forces the paper above the icon, and the icon is
  not re-shown on every topmost refresh.
- **Show Toolbar** was removed from **Settings > Toolbar**; `F10` still shows
  and hides the toolbar.
- The **Timer** shortcut (`F12`) works again. Windows reserves `F12`, so the
  shortcut is now handled by a keyboard hook instead of `RegisterHotKey`.
- When the Timer countdown ends while a context menu is open, a snapshot of the
  menu is kept as the drawing background (like Screenator 7.0) and the menu is
  closed, so the first drawing attempt starts a stroke instead of only
  dismissing the menu. This also works for menus owned by other applications.
- Fixed the paper-size handling: restoring a stored paper/PDF document used to
  resize its pages to the screen size, which squashed A4/custom pages and broke
  page scrolling. Restored pages now keep the configured paper size, and the
  page width always equals the screen width for every size.
- The `Page x / y` label and the separator line between pages were removed from
  the continuous paper view; the page edge remains.
- An opened PDF keeps its own page proportions: the page height follows the PDF
  page, and the paper-size setting (Screen/A4/Custom) no longer affects it. The
  page width still fills the screen, and saving the paper preserves the PDF page
  size.
- The continuous strip has no space before the first page, and a gap only
  appears between pages. A single page has no space above or below it, and tall
  pages (A4, custom, PDF) scroll to their real bottom.
- The toolbar's page icon shows only the current page number; its tooltip reads
  `Page X/Y (Press to change page)` and follows page scrolling, page inserts and
  deletes.
- Switching between Screen and Paper now exits the active tool and returns to
  the cursor. Changing the paper type or the PDF file keeps the tool active.
- Application menu: **Timer** moved next to **Eraser** in the main menu,
  **Show Toolbar** and **Show Floating Icon** are in the **Actions** submenu
  before **Show Hints**, and the action items are no longer duplicated in the
  main menu.
- The page of an opened PDF is remembered on exit and when another PDF is
  opened, so reopening that file returns to the page where it was left.
- Holding **Ctrl** while drawing (or resizing) a rounded rectangle or ellipse
  constrains it to a square or circle.
- Spline tangent handles are drawn as two separate guides from the node, so the
  second handle no longer appears to start where the first one ends. Ctrl still
  moves a handle without mirroring the other side.
- Polygons have corner handles that scale the shape on the x and y axes
  independently; the scaling is applied along the screen axes *after*
  rotation, so a rotated shape stretches horizontally or vertically instead of
  along its own axes. The radius, rotation, inner radius, point count and
  centre handles are unchanged.
- The rotation guide line of a rotated shape starts at its rotated top edge
  rather than at the unrotated top-centre position.
- The text tool opens instantly on first use: the font caches are warmed up at
  startup instead of on the first text frame.
- Paper mode is a real application window again: it appears in **Alt+Tab** and
  on the taskbar, so it can be switched back to after using another
  application. If the paper is behind another window, `F3` brings it forward
  instead of leaving Paper mode.
- Coming back to the paper (Alt+Tab, taskbar or `F3`) now raises the
  invisible input window above the paper, with or without a tool selected.
  Clicking the paper no longer makes that invisible window the foreground
  window, so it does not become a second Alt+Tab stop: Alt+Tab leaves the
  paper directly and returning to it keeps clicks and drawing working.
- Choosing Plain, Ruled or Graph from the toolbar Paper Type menu no longer
  copies the screen drawings into the page: entering Paper that way now saves
  the screen layer first.
- Choosing a paper type or opening a PDF now brings that paper to the front of
  the screen, even when another application was covering it.
- Nested lists in the Hints dialog are indented below their parent item instead
  of appearing as another top-level bullet.
- **Paper mode renders in a normal (non-layered) window** (Screen mode keeps the
  layered overlay).  Windows Ink delivers live pen side-button input to ordinary
  topmost windows but not to layered or non-topmost ones, so the paper is drawn
  in a normal window that is topmost **only while it is the active window**.
  This makes stylus button 1 pan the paper during the drag, while still letting
  other applications cover the paper when the user switches away (Alt+Tab /
  taskbar).  Ctrl+D still stops the paper at the work area.
- **Ctrl+W in Paper mode toggles the paper between the full-screen strip and a
  framed window.**  Painting, shapes, text, erasing, the toolbar and the floating
  icon all work the same in the window; the pages fit the window width and the
  strip scrolls inside it.
- In the continuous Paper strip, shapes and lines that cross a page boundary are
  now split onto the pages they overlap (each page shows its own portion), just
  like a pen stroke.  Text keeps its single anchor page.
- Stylus and wheel scrolling through the paper no longer updates the tray icon
  on every movement, and the repaint is limited to about 60 fps and painted
  directly during the drag. Deferred timer paints were starved while pen
  pointer messages were streaming in, so the page used to move only after the
  button was released. Panning also continues on every pointer update until the
  pen is released instead of waiting for the side-button flag in each update.
  Raw-HID movement reports that omit the button usage no longer end the pan,
  the freshest pen pointer position is used instead of the system cursor, and
  promoted pen mouse movement pans too.  Every pan source now uses screen
  coordinates so pointer and mouse updates can be mixed without a jump.  The
  40 ms pen poll also advances the pan from the live pen position, and a 16 ms
  paper-pan timer samples the cursor position while the middle button is held,
  so the page follows the pen even when the driver sends no movement or pointer
  updates between the press and the release.
- Stylus button 2 accepts inline text exactly like a right click, and side
  buttons mapped to the browser Back/Forward buttons are handled too.
- One button-2 press performs one right-click action: the Windows Ink pointer
  report and the raw HID report are deduplicated, and a promoted mouse event
  can no longer repeat the press or its release.
- In Screen mode the first side button keeps erasing while it is held and moved;
  the eraser is no longer ended early when a pointer update omits the button
  flag.
- While the text editor is open or a shape/text shows its editing handles,
  stylus button 1 changes the colour (line colour, shape fill for the Paint
  Bucket, or the live text) like the wheel, in Screen and Paper mode, instead
  of erasing, panning, or accepting the text. The colour change no longer closes
  the text editor or commits the text; it stays open with the keyboard focus.
  This works through every delivery path a pen driver uses: Windows Ink pointer
  updates while the pen hovers or already touches the screen, the raw HID side
  button, and pen side buttons promoted to middle/right/browser mouse events.
  Many pen drivers (including drivers that send no pen signature at all) send
  button 1 as an ordinary middle click, so a middle/browser-1 press is taken as
  button 1 unconditionally whenever the text editor is open or handles are
  visible: it recolours and never accepts the text, pans the paper, or starts
  the temporary eraser. The global mouse hook applies this wherever the pen is
  over Screenator, even when the press lands on the toolbar or the floating
  icon. The wheel also recolours while typing now, not only while handles are
  visible. Outside those states the first side button keeps its immediate
  temporary eraser in Screen mode.
- Pressing the left mouse button while typing accepts the current text and
  immediately starts a new text at the clicked point.  The same works when the
  text is accepted with the stylus tip.
- A real middle click accepts typed text, then finishes visible text or shape
  handles on the next click. With no editable text or handles active, middle
  mouse keeps its normal temporary-eraser (Screen) or paper-pan (Paper) action.
  A recognized stylus first side button still changes color while editing.
- In Paper mode the wheel recolours the selected shape or text while its
  handles are visible instead of scrolling the page.  The global wheel hook now
  honours this too, so it works in Paper mode as well as Screen mode.
- In Paper mode a click outside a dialog no longer closes it; the dialog stays
  open until its Close/Cancel button or Escape is used.
- The first bullet of a Hints list is no longer indented slightly further than
  the following bullets.
- Clicking on a new spot while a line, arrow or frame still shows its handles
  both finishes the old shape and starts the new one with the same click.
- A real mouse message clears stale pen state (paper panning, side buttons, an
  unfinished pen-tip stroke), so switching from the stylus to the mouse works
  immediately.
- Closing Screenator now asks before discarding drawings: **Yes** saves (the
  paper is saved via the file dialog), **No** discards, **Cancel** keeps the
  app running. Alt+F4, a window close and the quit button all go through this
  prompt and save the settings, so the selected paper size is remembered.
- Text input works again in Screen mode: the full-screen input window could sit
  above the toolbar, so clicking a tool icon drew a dot instead of selecting the
  tool. The toolbar and floating icon are raised above the input window again.
- The toolbar is no longer hidden while a modal dialog (Settings, Shortcuts,
  Hints, ...) is open.
- `Ctrl+D` in Paper mode shows the Windows taskbar by stopping the paper at
  the work area; the next press covers the taskbar again.
- Removed the separate Paper window mode and its `Ctrl+W` shortcut. Paper mode
  is always the fullscreen overlay; drawing, erasing and wheel scrolling work
  there directly.
- Hints are read from the installed `assets/hints.html` only. The app no longer
  copies or rewrites a user hints file or reads `SCREENATOR_HINTS_FILE`, so the
  source file can be edited without being overwritten.
- The paper is not topmost by default, so Alt+Tab lets other applications cover
  it. **Ctrl+D** toggles whether the paper covers the taskbar.
- Fixed returning from Paper to Screen: with continuous scrolling enabled the
  screen annotations were not repainted, and the overlay stayed below other
  windows. Both are corrected.
- Fixed an intermittent crash after saving or self-testing a PDF-backed paper;
  extracted PDF text was written one element past its buffer.
- Paper panning now matches Linux: hold the middle mouse button, or the first
  stylus side button, and drag to move through the continuous strip. In Paper
  mode the right mouse button or the second stylus side button is still the
  temporary eraser. The mouse wheel keeps scrolling the paper rather than
  changing the brush size.
- **Vector PDF export:** pen/marker strokes, lines, arrows, splines, polygons,
  triangles, ellipses, rounded rectangles, math plots, bar/random frames and
  text are recorded as Cairo vector operations. Exported PDFs contain stroked/
  filled paths and real selectable text instead of a raster annotation image.
  Pages with no annotations use an opaque white backdrop placed behind the
  background so they no longer introduce transparency-mask images.
  Multi-page papers and PDF-backed pages are covered by the selftest, which
  scans the saved PDF for image objects and extracts the annotation text.
- **Original PDF pages are preserved.** When a paper contains an opened PDF,
  the export imports the source page as its original PDF form layer, so its
  vector paths, embedded fonts, selectable text and images stay exactly as they
  were. The annotations are composed over that page as a second vector form
  built by Cairo. PDF pages are no longer rendered to an image for saving.
  Pages that use the eraser keep their strokes and text vector: eraser brush
  paths are exported as an alpha mask image instead of rasterising the drawing
  layer. The fill bucket contributes a small raster patch. Plain/ruled/graph
  backgrounds are vector.
- Selecting **Plain**, **Ruled** or **Graph** while a PDF page is displayed
  switches to that type's own document; the PDF and its annotations are kept
  and restored when **PDF File** is selected again.
- Added PDFium PDF import: **PDF File**, **Reset Page or PDF File** or Ctrl+O. PDF
  pages are rendered at the current screen size, scaled uniformly and centred.
  Rendered page images are cached, encrypted and broken PDFs show a clear
  message, and the last page for each opened PDF is remembered.
- Added **Save Paper...** (Ctrl+S) to Documents. Files are named exactly
  `Screenator-plain-YYYYMMDD-HHMMSS.pdf`,
  `Screenator-ruled-YYYYMMDD-HHMMSS.pdf`,
  `Screenator-graph-YYYYMMDD-HHMMSS.pdf` or
  `Screenator-pdf-YYYYMMDD-HHMMSS.pdf` according to the current page type.
  The old wide-string filename bug that produced garbled names is fixed.
- **Limit:** the eraser is exported as an alpha-mask image (the strokes/text
  remain vector); a fill-bucket action is stored as a small raster patch. Page
  rotation inside the source PDF is not remapped when annotations are overlaid.
- Paper type, page count, current page and PDF path are persisted in the INI.
  Last pages for opened PDFs are stored in `paper-pdf-pages.txt`.
- Added Paper toolbar actions for insert before/after, delete page, Go to
  Page, Paper Type and Save Paper. Previous/Next Page are not shown.
- Added selftest coverage for the page model, per-page annotations, page
  undo/redo, continuous scrolling and page selection, PDFium
  loading/rendering, multi-page and PDF-page vector/text export (image-object
  scan), Cairo PDF export and missing/encrypted PDF error handling.
- Added the PDFium dependency and packaged `pdfium.dll`. PDF page functionality
  is implemented for the Windows port.
- Updated with the applicable Linux 8.0 behaviour: Cursor tool, two-stage
  Escape/right-click handling, right-click text commit, Actions and Marker
  Opacity menus, F3 Screen/Paper switch, F12 timer, F4 screenshot, editable
  HTML hints, 700x500 hints with About, and display/DPI annotation scaling.
- Reset Settings now removes the whole `%APPDATA%\Screenator` directory,
  including user hints and saved annotations.
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
  10% through 100% in 10% steps.
- Math plots break at real poles such as `1/x` and `tan(x)`, and `delta()` is
  drawn as an arrow at each zero of its argument. The Math Function dialog now
  places the preset list above the expression field and is 20% taller.
- Stylus support for the first and second side buttons. Set the pen driver's
  **button 1 (nearest the tip) to Middle Click** and **button 2 to Right
  Click** for the expected short-press and long-press actions.
- Defaults: 48x48 floating icon, 60 px eraser, Arial Bold 24 pt font.
- Hints window is 700x500.
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
Screenator-8.0-win64/
  Screenator.exe
  pdfium.dll
  Screenator Data/
    assets/
    README.md
    RELEASE_NOTES.md
    COPYING
    AUTHORS
    PDFIUM_LICENSE.txt
```

A per-user installer is also produced:

```
Screenator-8.0-win64-setup.exe
```

It installs the same files under `%LOCALAPPDATA%\Programs\Screenator`, adds
Start menu entries, and includes the **PDF Open with list** task, selected by
default. It registers Screenator as one of the applications that can open PDF
files (right-click **Open with** and **Settings > Apps > Default apps**) and
never sets Screenator as the default PDF viewer. Installing or uninstalling
also removes PDF associations left by an older portable copy, including the
generic `pdf_auto_file` ProgID and stale `OpenWithList` entries that Windows
does not expose for removal. `installer/remove-pdf-association.ps1` performs
the same cleanup on demand. `Screenator.exe "file.pdf"` opens a PDF in Paper
mode, and a second launch forwards the file to the running instance.

`Screenator.exe` is statically linked and does not require the mingw runtime.
Keep `pdfium.dll` and the `Screenator Data` folder next to it. PDF support uses
the BSD-licensed PDFium build.

## Verification

```
Screenator.exe --selftest
Screenator.exe --uitest
Screenator.exe --smoke
```

## License

GNU General Public License version 2 or later. See `COPYING`.
