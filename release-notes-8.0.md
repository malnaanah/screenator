# Screenator 8.0

Screenator 8.0 is the Linux release that adds the full Paper Drawing/PDF
feature set and the accumulated tool, toolbar, settings, hints and input
changes.  The native Windows port remains at 7.0 and is not part of this
release.

Generated: 2026-11-09
Scope: Linux application (Screenator 8.0)

1. Paper mode and paper documents
--------------------------------
- Added a full Paper mode alongside Screen mode.
- Paper mode is shown in a normal managed window instead of the fullscreen
  overlay: other applications and dialogs can cover it and Alt+Tab works.
- Ctrl+W switches to Window mode: the Paper window becomes resizable, the
  paper width fits the window automatically, and the desktop panel can appear
  above it. Pressing it again returns to fullscreen.
- Window mode uses ordinary focused-window input: no global pointer grab is
  taken, the drawing tool and cursor stay inside the Paper window, the window
  frame and other applications keep working, and Ctrl/Alt shortcuts are
  received only by the focused Paper window.
- Window-mode drawing invalidations are scaled and translated correctly, and
  Paper mode no longer repaints the whole window on every stroke, so drawing
  stays immediate while the window is resized.
- The paper background (including every visible page in the continuous view)
  is cached instead of being rebuilt for each expose.  Scaled Window-mode
  image drawing uses bilinear filtering instead of Cairo's very slow default
  downscale filter.  This keeps drawing, scrolling and page changes responsive
  with long multi-page documents and PDFs.
- Multi-page PDFs no longer allocate a full annotation layer for every page
  that is only scrolled past, and only a few rendered PDF pages are kept in
  memory at once.  Starting a stroke on any visible page no longer recomposites
  the whole document.
- Opening or replacing a PDF always refreshes the cached paper background, so
  Window mode cannot show a black or stale page.
- Paper mode keeps per-page annotation layers like Xournal++, but the
  continuous view uses one correctly composited drawing layer.  Scrolling
  stores only the pages touched by new ink and skips storage entirely when
  nothing was drawn, while PDF page rendering and background caches remain
  per page.
- PDF pages are rendered by a background worker and cached like Xournal++'s
  render jobs.  A page that is still rendering shows a white "Loading..."
  placeholder instead of freezing the window, and stale jobs are discarded
  when the viewport or document changes.
- Paper backgrounds are drawn directly from each visible page instead of
  rebuilding a screen-sized cache on every scroll.  Scrolling or switching
  pages without drawing no longer saves the whole active page again.
- Continuous Window mode now maps the whole viewport, not the selected page
  origin, so drawing works correctly on pages below the first one.
- Continuous scrolling shifts the existing drawing layer and repaints only the
  newly exposed strip instead of clearing and recompositing the whole screen.
- Ctrl+D leaves fullscreen into a borderless, non-resizable window covering the
  monitor, so the desktop panel stays above the page without entering Window
  mode.
- Plain, Ruled and Graph papers using the screen-sized paper now scale their
  pages, drawings and annotations when the monitor resolution changes, just
  like PDF pages.  Undo buffers are resized with them.
- Recorded vector layers are scaled by replaying the recording under a scale
  transform, not rasterized, so PDF export keeps strokes as vectors and text
  as text after resolution or paper-size changes.
- The cursor is refreshed from the actually active tool after resolution
  changes, mode changes, and paper/PDF loading, including the scaled eraser
  cursor.  When no tool is active, no tool cursor is shown.
- Plain, Ruled and Graph pages now scale their drawing content uniformly and
  centre it when the page dimensions or screen resolution change, preserving
  the aspect ratio of text and graphics like PDF pages do.
- A Delete Page action is available in the app menu and tool bar between the
  insert-page buttons.  It is enabled for Plain, Ruled and Graph papers with
  more than one page and disabled for PDF papers.
- Undo and Redo now also reverse page insertion and deletion, restoring a
  deleted page with its drawings.  Drawing undo takes precedence while drawing
  history exists.
- The Delete Page button becomes sensitive immediately after a page is added.
- A Go to Page action is available in the Paper menu and tool bar for PDF and
  multi-page Plain, Ruled and Graph documents, using goto-page.png.  A
  current-page indicator sits directly before it, uses the same square size
  as the other icons, and inactive tool-bar buttons now show their tooltips.
- The tool-bar tooltip is anchored next to the hovered icon, so it stays
  aligned in both horizontal and vertical tool bars.
- The Timer tool is hidden and disabled in Paper Drawing; it remains available
  in Screen Annotation mode.  The Toggle Visibility tool-bar button is also
  hidden in Paper Drawing (Ctrl+T and the menu action still work).
- Each PDF file remembers the page it was last closed on when reopened from
  Screenator or from the desktop.
- Saving a paper shows a progress overlay until the PDF is written; repeated
  save actions are ignored until it finishes, then the saved path is shown.
- PDF export now serializes Poppler access with the background renderer and
  does not schedule/cache page backgrounds while saving.  This fixes
  segmentation faults and excessive memory use when saving large PDFs.
- Saving a screenshot now shows a confirmation with the saved path.
- The inline text editor is now an override-redirect popup, and the Paper
  window is kept above the desktop panel only while the text editor or a
  dialog is open.  Normal fullscreen Paper mode is no longer always above
  other applications.
- Activating Paper mode with F3 now deiconifies, presents and raises the Paper
  window after the mode switch, so it comes to the front instead of remaining
  minimized/in the taskbar.
- Vector annotations on pages after the first are saved at the correct
  vertical position in exported PDFs.
- The Paper menu's Open PDF... action is now Reset/Open PDF.: it resets the
  current Plain/Ruled/Graph paper to one empty page, or opens another PDF when
  the current paper is PDF, asking to save unsaved changes first.
- All Paper presentations (fullscreen, Ctrl+D and Window mode) now use the same
  local input model: the tool and cursor only act over the Paper window, there
  is no global pointer grab, and Ctrl/Alt shortcuts are only received while the
  Paper window is focused.
- The eraser cursor scales with the paper in Window mode, so its ring matches
  the area actually erased after resizing the window.
- Window mode now extends the paper viewport to the real window size.  A tall
  or side-tiled window shows more of the continuous document instead of a
  fixed screen-aspect area, and drawing/scrolling work across the whole
  window.  The drawing buffer, undo buffers and paint contexts are resized
  with the viewport and restored when leaving Window mode.
- Plain, Ruled, Graph and PDF each keep their own document, pages, drawings,
  current page, view position and PDF file.
- Switching between paper types no longer clears the other paper's drawings.
- Selecting a paper type, or opening a PDF, switches to Paper mode.
- Returning to PDF reuses the already opened PDF instead of asking for the file
  again.
- Added "Open PDF..." to the Paper menu and to the tool bar's Paper Type menu.
- Opening a PDF through Open With, the desktop entry, or screenator file.pdf
  starts in Paper mode.
- Opening a PDF from the file browser while Screenator is already running now
  forwards the file to the running instance.
- Registered Screenator as a PDF handler in the desktop entry and AppData.
- Added continuous scrolling for Plain, Ruled, Graph and PDF pages, with a grey
  gap between pages.
- Added page insertion before/after for non-PDF papers.
- Added page navigation with Page Up/Page Down and first/last navigation with
  Home/End.
- Drawings that cross a page boundary remain on both pages after scrolling.
- Added paper size settings: Screen, A4 portrait, A4 landscape and Custom.
- Added a separate Settings > Paper tab for continuous scrolling and paper size.

2. Tool bar
-----------
- Added the Paper/Screen tool bar, shown with F10.
- The tool bar and floating icon are now visible by default.
- The default tool bar direction is Horizontal, along the top edge.
- Added a Vertical direction option, placed along the right edge.
- Tool bar direction and icon size are configured in Settings > General.
- Added Settings > Toolbar to choose which icons are visible and their order.
- The Toolbar list shows the actual icons, including generated colour,
  thickness and eraser-size samples.
- Tool bar rows can be reordered by dragging the ☰ handle.
- Hiding and showing a tool bar item keeps its row in place instead of moving
  it to the end of the list.
- A 1 pixel border is drawn around the tool bar itself; individual icons have
  no border or gap between them.
- The tool bar keeps its screen-edge or centre alignment when mode-specific
  buttons change its width.
- The thickness icon uses horizontal lines in a vertical bar and vertical
  lines in a horizontal bar, so its thickness always expands across the bar.
- The tool bar can be dragged with the pointer and snaps/clamps to screen edges.
- Tool bar visibility and icon order are remembered across restarts.
- Removed the special extra spacing around Switch Mode and Quit; all buttons now
  use the same spacing.
- Fixed the tool bar staying hidden after opening a PDF with the native chooser.

Tool bar actions added or updated:
- Switch Mode
- Cursor
- Pen
- Text
- Eraser
- Arrow
- Timer
- Shapes
- Undo and Redo
- Save / Screenshot
- Paper Type
- Insert Page Before
- Delete Page
- Insert Page After
- Current Page
- Go to Page
- Clear Screen
- Toggle Visibility
- Eraser Size
- Line Color
- Thickness
- Fill Color
- Opacity
- Marker Opacity
- Settings
- Show Hints
- Quit

3. Menus and icons
------------------
- Renamed the "Paper Drawing" submenu to "Paper".
- Removed New paper, Previous Page and Next Page from the Paper submenu.
- Unified the insert-page items as Insert Page Before / Insert Page After in
  both the menu and tool bar, using add_before.png and add_after.png.
- Added the Actions submenu below Shapes, using actions.png.
- Actions contains Switch Mode, Undo, Redo, Clear Screen, Toggle Visibility,
  Screenshot, Show toolbar, Show Floating Icon and Show Hints.
- Added Screenshot to the Actions menu using screenshot.png.
- The Hints text is loaded from the installed HTML file each time the dialog
  opens. It is not edited after installation, so there is no user override or
  environment-selected hints file.
- All dialogs now show a title bar with a close button and a visible border.
- All configuration, saved annotations and user tool configuration now live in
  ~/.config/Screenator.  Old ~/.config/screenator.* files are migrated
  automatically.
- F4 saves a screenshot in both Screen and Paper modes.
- The first Escape or right click now closes the current shape/text handles and
  keeps the selected tool.  A further Escape exits the tool, selects the Cursor
  tool and highlights its toolbar icon.  Changing the screen resolution exits
  the current tool and returns to Cursor immediately.
- Switching between Screen and Paper modes exits the current tool and leaves
  Cursor selected.
- Switching from one paper type to another, or opening another PDF while
  already in Paper mode, keeps the active tool and brings the Paper window to
  the front.  Resetting a paper still exits to Cursor.
- Changing pages, PDFs or paper types reapplies the selected tool after the
  switch, so the tool cursor and the actual drawing tool stay in sync.
- The startup Hints window is fixed at 700x500 and stays centred instead of
  moving and resizing repeatedly while it opens.
- Added a Show Hints toolbar button directly after Settings.  Toolbar
  visibility settings now also control it correctly; upgrading old settings
  adds the new buttons only once, so hiding them is respected on later starts.
- Added Save current paper to the Paper menu using save.png.
- Added Marker Opacity directly after Opacity in the app menu.
- The Opacity menu now uses opacity.png and Marker Opacity uses
  marker_opacity.png.
- The tool bar Marker Opacity button now uses marker_opacity.png.
- Added quit.png to the Quit menu item and the rightmost tool bar button.
- Added/updated icons for actions, save, screenshot, cursor, switch mode,
  timer, settings and add-page actions.
- Refreshed several existing icons: arrow, bars, eraser, font settings, hide
  and reset settings.

4. Settings
-----------
- Added a new Toolbar settings tab for direction, icon size, visibility and
  order.
- Added a new Floating Icon settings tab for icon size and position.
- Added a new Shortcuts settings tab for editable keyboard shortcuts.
- Tool bar direction and icon size live in the Toolbar tab.
- The Text Font control is in the General tab and opens the font chooser.
- Renamed the old Paper Drawing settings tab to Paper.
- Shortcuts are persisted in the [Shortcuts] section of screenator.ini and are
  re-grabbed immediately when the dialog closes.
- Menu shortcut labels now reflect the configured shortcuts.
- Menu entries support typing a shortcut such as F8, Ctrl+S or Alt+F4; "None"
  disables a shortcut.
- Default shortcuts include: F9 floating icon, F8 Pen, F7 Text, F6 Arrow,
  F12 Timer, F10 tool bar, F3 Switch Mode, F4 Screenshot, Ctrl+Z undo,
  Ctrl+Shift+Z redo, Ctrl+S save, Ctrl+B/Ctrl+A insert page,
  Ctrl+T visibility, Ctrl+Q exit Paper mode, Alt+F4 quit and Escape cancel.
- Expanded "Reset app settings and drawings" to delete configuration and saved
  drawings, clear every paper page, restore defaults, and show/hide the
  controls appropriately.
- Reset now restores a visible horizontal tool bar with all icons.
- The default Timer delay is 2 seconds.
- Tool bar visibility and floating-icon visibility default to visible.

5. Timer, cursor and input
--------------------------
- Changed the Timer shortcut from F2 to F12.
- Fixed the F12 Timer shortcut so it works globally.
- Ctrl/Alt letter shortcuts now work with non-Latin keyboard layouts such as
  Arabic by matching the physical keycode instead of the active layout
  character. This includes Ctrl+D and Ctrl+W.
- The Timer now locks tool selection during the countdown and activates Pen at
  the end.
- Replaced the Hand tool with the Cursor tool using cursor.png.
- Cursor is available in both Screen and Paper modes.
- Cursor exits the current tool and restores the normal pointer.
- In Paper mode with no active tool, the middle mouse button or stylus side
  button 1 still pans, the wheel scrolls, and the right button or stylus side
  button 2 erases.
- On startup, no drawing tool is active; Cursor is the active tool even if a
  tool was selected when the app last exited.
- Fixed cases where the normal pointer appeared while a tool was active.
- In Screen mode, the right button no longer opens colour, thickness, marker
  opacity or similar popup menus.  It acts as Escape and closes handles.
- In Screen mode, the stylus first side button and the middle mouse button
  activate the temporary eraser on a normal press; no long press is required.
- While a text or shape still shows its handles, the stylus first side button
  changes its colour in both Screen and Paper modes.
- In Paper mode, right click/stylus side button 2 still becomes the temporary
  eraser after the current object is committed.
- Undo history now keeps 40 steps in every mode.
- While typing text, the mouse wheel and stylus side button 1 change the text
  colour.  Right click, stylus side button 2 or mouse button 2 accepts the text
  like Enter; Escape still cancels it.
- While a placed shape or text shows its handles, the wheel and stylus side
  button 1 change its colour instead of scrolling.
- Added Page Up/Page Down/Home/End page navigation in Paper mode.
- Spline node and handle editing no longer shows a stray preview line beyond
  the last point.
- The spline rubber-band preview remains hidden after editing a node or handle
  until a new node is placed.
- The app menu “Show Floating Icon” and “Show toolbar” labels use the configured
  shortcuts.

6. Saving and PDF export
------------------------
- Saving now writes automatically to the user's Documents folder.
- File names use these patterns:
  - Screenator-plain-YYYYMMDD-HHMMSS.pdf
  - Screenator-ruled-YYYYMMDD-HHMMSS.pdf
  - Screenator-graph-YYYYMMDD-HHMMSS.pdf
  - Screenator-file-YYYYMMDD-HHMMSS.pdf for opened PDF files
  - A numeric suffix is added if that name already exists.
- The Save button and Ctrl+S save only the current paper.
- Only papers changed since their last save are written.
- A success dialog shows the saved file path or paths.
- Saving with no changes shows a "no changes to save" message.
- Save confirmation appears only when exiting the application.
- On exit, one dialog offers to save every changed paper, ignore the changes, or
  cancel the exit.
- After an exit save, the dialog lists all created files.
- Per-paper modified tracking was added so unchanged papers are not rewritten.
- PDF export now embeds the original PDF page as vector content instead of a
  flattened bitmap.
- Plain, Ruled and Graph page backgrounds are exported as vector graphics.
- Annotation strokes, lines, arrows, frames, stars, triangles and splines are
  recorded as vector drawing operations for PDF export.
- Text is recorded through Pango and exported as text rather than bitmap.
- PDF export is vector-only: annotations are never written from the raster
  annotation layer.  Paint-bucket fills remain a screen-layer operation and
  are not included in the exported vector page; they no longer force the rest
  of the page to export as a bitmap.
- Undo and redo keep a parallel snapshot of the page's vector layer, so
  strokes stay vector paths and text stays text after undoing or redoing
  them.
- A newly opened PDF page now starts vector recording even when drawing
  begins before its first background render; the recording is scaled to the
  final page size when rendering completes.
- Transparent pen and marker strokes are recorded as one continuous vector path,
  preventing darker dots where individual segment caps used to overlap.
- Pen strokes are recorded with the same on-screen smoothing, including the
  final short segment, so saved strokes no longer look angular.
- Using the eraser over empty space no longer rasterizes the page: the vector
  layer is left untouched when nothing was erased.
- When the eraser removes ink, the affected drawing stays vector and text stays
  text; an alpha mask is baked into the vector layer at that point so later
  drawings are not affected by the old erasure.
- Saving now preserves the original PDF's vector page content and overlays the
  annotation layer separately.

7. Bug fixes
------------
- Fixed continuous scrolling for non-PDF papers.
- Fixed the Actions menu appearing empty.
- Fixed non-PDF wheel page switching.
- Fixed page scrolling after the last page of non-PDF papers.
- Fixed painting disappearing on the neighbouring page after scrolling in
  continuous mode.
- Fixed the tool bar disappearing after opening a PDF with a native chooser.
- Fixed the timer shortcut not working globally.
- Fixed the normal cursor appearing while a tool was active.
- Fixed PDFs opened from the file browser not reaching a running instance.
- Fixed stale tool selection after restarting the application.
- Fixed Alt+F4 in Paper mode only leaving Paper mode instead of quitting
  Screenator.  Ctrl+Q still returns to Screen mode.
- Fixed spline preview segments after editing points or handles.
- Fixed transparent vector strokes showing overlapping-cap dots in saved PDFs.
- Fixed PDF export using rasterized page backgrounds.

8. Packaging, build and documentation
-------------------------------------
- Added new icons to CMake, Debian, RPM and AppImage packaging lists.
- Removed hand.png and added cursor.png.
- Added marker_opacity.png to all package icon lists.
- Updated desktop MIME association to application/pdf.
- Updated appdata metadata for PDF support.
- Updated README.md, info.txt, screenator.1 and the in-app hints for:
  - Screen and Paper mode terminology
  - Paper persistence and switching
  - Automatic Documents saving and file names
  - Vector PDF export
  - Tool bar settings and draggable ordering
  - Shortcuts settings
  - Cursor tool
  - Page navigation keys
  - Text/shape right-click and scrolling behaviour
- The Linux version is 8.0.

9. Not included in the Windows port yet
---------------------------------------
- All items above are Linux changes.
- screenator-windows/ is still a separate native Win32 implementation at 7.0
  and has not been updated since the v7.0 release.
- Porting requires a manual feature-by-feature update in the Windows sources or
  a future shared-core refactor.
