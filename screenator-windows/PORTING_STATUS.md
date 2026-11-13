# Screenator Windows 8.0 port status

The Windows code has been updated with the applicable behaviour from the Linux
8.0 source tree and is packaged as version 8.0.

| Linux 8.0 change | Windows status | Files / notes |
|---|---|---|
| Editable HTML hints with CSS colours | Implemented | `ui.c` HTML-to-RTF converter; reads `assets/hints.html` directly and never rewrites it. Nested `<ul>`/`<ol>` items are indented below their parent item (RichEdit `\li`/`\fi` paragraph indents), and the whitespace after `<ul>` is suppressed so the first bullet aligns with the rest |
| Hints 700x500, centred once, title bar/border | Implemented | `ui_show_hints`, native caption |
| About button in hints before Close | Implemented | `ID_HINTS_ABOUT` |
| Hints user file, env override and path footer | Removed | no `%APPDATA%` copy, no `SCREENATOR_HINTS_FILE`, no path footer |
| All custom dialogs get title bar and border | Already present | hints, math, About, icon position; in Paper mode an outside click no longer closes a dialog |
| Cursor tool replacing Hand/idle | Implemented | Cursor menu and quick-menu entry; idle tool is Cursor |
| Startup with no drawing tool, Cursor active | Already present | `config_defaults` sets `TOOL_NONE` |
| First Esc/right click closes handles; second exits | Implemented | `tools_right_short`, `tools_exit` |
| Mouse and stylus editing actions | Implemented | middle click / stylus Button 1 changes color and keeps text or shape editing active; after editing closes in Screen mode with Text or a shape tool selected, it does nothing. Paper mode middle click pans after editing closes. Enter or right click / stylus Button 2 accepts text, and right click / Button 2 finishes handles before the staged erase action |
| Text handles stay visible | Already present | text commit leaves `EDIT_FRAME` handles |
| Stylus button 1 recolours while typing or editing with handles | Implemented | `stylus_step` always recolours while `editable_handles_active()` (typing or handles, Screen and Paper mode); the first side button is handled on Windows Ink in-range updates as well as DOWN, the raw HID barrel path, and promoted mouse events through the global `wheel_hook_proc`. Because many pen drivers send button 1 as a plain **middle click with no pen signature**, `WM_MBUTTONDOWN` / `WM_XBUTTONDOWN` XBUTTON1 are turned into a colour step unconditionally while typing or handles are visible (Screen and Paper mode), and the hook swallows the press so it cannot accept the text, open the right-click action, pan, or start an eraser; `WM_KILLFOCUS` no longer commits the text during a colour step and the editor takes the focus back. Outside those states the middle button keeps its eraser/pan action |
| Selecting a Paper tool brings Paper to front | Implemented | `tools_activate` raises/focuses paper overlay |
| Paper -> Screen keeps icon and tool equal | Not applicable | Windows has one overlay, not a separate toolbar |
| Display/DPI change exits tool to Cursor | Implemented | `WM_DISPLAYCHANGE` calls `tools_exit` |
| F4 screenshot in Screen and Paper | Implemented | `app_take_screenshot`, Pictures folder |
| Settings label | Implemented | "Show settings indicator on Floating Icon" |
| Reset removes whole config directory | Implemented | `config_remove_dir`, `%APPDATA%\Screenator` |
| Show Hints menu placement | Implemented | after Settings in the main menu |
| Marker Opacity order and icons | Implemented | directly after Opacity |
| Actions submenu and screenshot icon | Implemented | Switch Mode, Timer, Undo/Redo, Clear, Visibility, Screenshot, Show Icon, Show Hints |
| Editable shortcuts and menu labels | Implemented | `config.c` parse/format and `[Shortcuts]` load/save; **Settings > General > Edit Shortcuts...** in `ui.c`; `app_hotkeys_apply` re-registers and rolls back on conflicts. Escape and Ctrl+F10 stay fixed |
| Timer F12 and tool locking | Implemented | 2-second countdown, then Pen. Windows reserves F12, so the shortcut is handled by a low-level keyboard hook instead of `RegisterHotKey`; an open context menu (ours or another application's) is captured as the drawing background and closed when the countdown ends. Actions > Timer is also available |
| Screen-paper scaling, centring and undo resize | Implemented | `scale_surface_center` in `ui_rebuild_surfaces` |
| Spline preview fixes | Already implemented | stylus hover filtering in `main.c` |
| Cursor/tool mismatch fixes | Implemented | Cursor resets and display-change cleanup |
| Vector-only PDF export | Implemented | `paper.c` keeps a per-page Cairo recording surface plus text frames, renders the annotation layer as a transparent Cairo PDF page, imports original PDF pages with PDFium `FPDF_ImportPages`, and composes each paper page from a background form and an annotation form with `FPDF_ImportNPagesToOne` (the two forms are overlaid by resetting the second form matrix). Selftest checks: PDF backdrop stays a vector form and the annotation page exports with real text and no image. Eraser brushes are recorded and applied as an alpha mask, so drawings/text stay vector; bucket fills add a raster patch |
| Undo/redo vector snapshots | Implemented | 50 steps in all modes. Snapshots carry their paper document/page token, so undo switches back to the page a change belongs to; vector recording surfaces and text frames are swapped alongside the raster buffers. Structural page changes clear the drawing history |
| Paint-bucket screen-only behaviour | Already present | raster bucket; records a small image patch in the vector layer |
| Transparent-stroke vector recording | Implemented | pen strokes are collected and replayed as one path with opacity in the PDF layer |
| Persistent tool bar | Implemented (Windows tool set) | `toolbar.c`: Linux 8.0 action order and icons, mode-dependent visibility (page actions in Paper; Timer and Visibility in Screen), F10 show/hide, horizontal/vertical, icon size slider 16-48 px, zero spacing/border, drag/snap by any icon with a flush screen-edge snap, native tooltips including shortcuts. Save icon follows mode. Shapes/Paper Type/option buttons open dedicated menus; Settings opens the tabbed Settings dialog. Ctrl constrains rectangles/ellipses to 1:1 while drawing or resizing; spline handles draw separate guides from each node; polygon corner handles scale the shape on the screen x and y axes independently, applied after rotation; the rotation guide line starts at the rotated top edge. Line Color, Thickness, Fill Color and Eraser Size are live samples; the eraser sample is circular and the thickness sample is a flat black line spanning 70% of the toolbar height (horizontal bar) or width (vertical bar); the Configure Toolbar list shows every action icon. Previous/Next Page and None were removed; the paper menu has one contextual Reset Page or PDF File item and the toolbar page icon shows only the current page number (tooltip `Page X/Y (Press to change page)`). Application menu: Timer sits after Eraser, and Show Toolbar / Show Floating Icon are in the Actions submenu before Show Hints; the main menu does not repeat submenu items. `toolbar_items`/`toolbar_order` persist order and hidden rows; **Settings > Configure Toolbar...** lists every action with its icon, supports dragging rows by their handle and double-click show/hide |
| PDFium dependency | Integrated | `third_party/pdfium`, `pdfium.dll` packaged; library initialises at startup and is used by `paper.c` for PDF pages |
| Paper pages, continuous view, PDF open/save | Implemented | `paper.c`: up to 1024 pages (screen, A4 or custom size; width always equals the screen width; an opened PDF keeps its own page proportions and ignores the paper-size setting), **separate Plain/Ruled/Graph/PDF documents** (each keeps its own pages and annotations when another type is shown) plus a separate screen annotation layer, per-page annotation surfaces and backgrounds (screen strokes are never written into a page, including when a PDF is opened from Screen mode), insert before/after, delete, Home/End, Go to Page, page-level undo/redo, PDF open via `GetOpenFileNameW`, cached PDFium page rendering, encrypted/broken-PDF messages, per-PDF last page, Cairo PDF export. **Settings > Paper > Continuous page scrolling** is on by default and shows all pages as one scrollable strip (mouse wheel, Page Up/Down, click-to-page) with correct page-local drawing coordinates; the strip shows page edges without a `Page x / y` label or separator line, with no space before the first page and a gap only between pages, and restoring a stored document keeps the configured page size. Closing the app asks Yes/No/Cancel before discarding drawings and always saves the settings. Middle mouse / stylus side button 1 pans the strip in Paper mode; right click/Button 2 is a temporary eraser, but while text or shape handles are being edited the first right click only finishes that edit and keeps the tool. **Ctrl+D** stops the paper at the work area so the taskbar is visible, then returns to full-screen coverage. Paper mode is rendered in a normal window (Screen mode keeps the layered overlay) that is topmost only while active, so Windows Ink delivers live stylus side-button drags and other apps still cover the paper when the user switches away; Ctrl+D stops it at the work area, and Ctrl+W toggles the paper between the full-screen strip and a framed window (painting, toolbar and floating icon work in both). The Paper menu has a single contextual **Reset Page or PDF File** (open for PDF, reset otherwise) with a save prompt and no inline page indicator; the toolbar keeps the current-page icon, which updates while scrolling and opens Go to Page when pressed. Switching type/page keeps the active tool; switching between Screen and Paper returns to the cursor. Paper mode is an Alt+Tab/taskbar application window, F3 brings it forward when another app is in front, and the invisible input window is owned by the paper so it never becomes the foreground window: Alt+Tab goes straight to other applications, and returning to the paper keeps clicks working with or without a tool; choosing a paper type from the toolbar enters Paper first so the screen layer is kept separate. Each opened PDF reopens at the last page used; choosing a paper type or opening a PDF raises the paper above other applications. Stylus and wheel scrolling update only the toolbar indicator per movement and paint directly at about 60 fps during the drag (deferred timer paints were starved by pen pointer messages, so the page previously moved only on release), and panning continues on every pointer update until the pen is released instead of requiring the side-button flag in each update; raw-HID movement reports that omit the button usage no longer end the pan, the freshest pen pointer position is used, promoted pen mouse movement pans too, the 40 ms pen poll advances the pan from the live pen position when no movement messages arrive, a 16 ms paper-pan timer follows the cursor while the middle button is held so panning works even without movement messages, and all pan sources use screen coordinates so mixed updates do not jump. In Paper mode the global wheel hook recolours the selected shape/text while handles are visible instead of scrolling. One button-2 press performs one right-click action: the Windows Ink and raw HID reports are deduplicated and promoted mouse events cannot repeat the press or release. **Settings > Paper > Paper size** supports Screen, A4 portrait, A4 landscape and custom width/height (mm); pages always fit the width and taller pages scroll, with matching export dimensions. Still missing: page thumbnails |
| Linux config path | Adapted | `%APPDATA%\Screenator` |
| Installer and PDF Open with registration | Implemented | `installer/screenator.nsi` (NSIS) builds a per-user installer under `%LOCALAPPDATA%\Programs\Screenator`. The PDF task is selected by default and registers `Applications\Screenator.exe`, a `Screenator.pdf` ProgID, `.pdf\OpenWithProgids`, and `RegisteredApplications`/Capabilities for Settings > Default apps. It never writes `UserChoice`, so the default viewer is unchanged. Install and uninstall remove portable-copy leftovers: the generic `pdf_auto_file` ProgID, `.pdf` default pointing at it, stale `OpenWithList` entries, and the old app registrations; `installer/remove-pdf-association.ps1` does the same on demand. `Screenator.exe "file.pdf"` opens Paper mode and a second launch forwards the path via WM_COPYDATA |
| Linux packaging/CMake/AppImage | Not applicable | MSYS2/static build plus the NSIS installer |

## Full parity blockers

The persistent tool bar, its visibility/order settings, tooltips, editable
shortcut system and the Paper/PDF subsystem are now implemented for the Windows
tool set. The remaining Linux 8.0 diff is dominated by vector paper details:

- Windows now has a continuous scrolling paper strip (Settings > Paper) with
  middle-button/stylus-side-button panning; page thumbnails are still missing.
- Selecting Plain/Ruled/Graph while a PDF page is active starts a fresh
  one-page paper instead of converting the opened PDF page; unsaved PDF
  annotations trigger a confirmation.
- Windows records pen strokes, shapes and text as vector data for PDF export.
  Text stays real PDF text. Eraser brush paths are exported as an alpha mask,
  so drawings/text remain vector; bucket fills add a raster patch.
- Opened PDF pages are preserved as their original PDF form layers
  (`FPDF_ImportPages`) and annotations are composed over them as a second
  vector form, so source vector paths, fonts, selectable text and images stay
  untouched. Source page rotation is not remapped when overlaying annotations.
- A saved PDF containing rendered PDF text no longer goes through PDFium
  rasterisation; only the eraser mask and bucket patches are raster.
- Annotation persistence (`screenator-annotations.png`) still stores the
  current page layer; other pages keep their annotations for the session but are
  not written to that PNG. Paper type, page count, current page, PDF path and
  per-PDF last pages are persisted.

`pdfium.dll` is packaged next to `Screenator.exe`; the rest of the executable is
statically linked.

## Next work

- Persist every page annotation layer, or adopt a page document file format.
- Add page thumbnails.
- Optional: vectorise the fill-bucket region so no raster patch is needed.
- Optional: remap source PDF page rotation when overlying annotations.
- Optional: prompt for an encrypted PDF password instead of only reporting it.

## Manual checks still recommended

- Edit `assets/hints.html` in the installed data folder and reopen Hints; test an unmatched tag. The app must not create or overwrite a copy in `%APPDATA%`.
- Open About from Hints and return.
- Verify F3, F4, F12, Cursor and the Actions menu with a real desktop.
- Hover every toolbar button to check its tooltip; hide, re-show and reorder
  actions from Settings > Toolbar > Configure..., then restart.
- Change a shortcut, reject an invalid/duplicate value, restart, and confirm the
  menu labels and registered keys match.
- Verify text commit with Enter, right-click and stylus Button 2.
- Verify resolution/DPI changes preserve centred annotations and exit to Cursor.
- Verify Reset Settings clears all screen and paper drawings, removes the whole
  config directory, and puts a horizontal toolbar at the top centre, flush with
  the top edge.
- Verify Paper > Insert Before/After, Delete Page, Previous/Next, Home/End and
  Go to Page with both plain and PDF documents.
- Enable Settings > Paper > Continuous page scrolling: scroll with the wheel and
  Page Up/Down, draw on pages above and below the current viewport, then restart
  and confirm the option and annotations persist.
- In Paper mode try Alt+Tab over the fullscreen paper and Ctrl+D (taskbar
  coverage). Draw on a page, erase part of the stroke, save and zoom: the
  remaining stroke/text should stay vector.
- Open an encrypted PDF and a deliberately corrupted PDF; confirm the error
  messages are shown and the current document is left intact.
- Open the same PDF twice after restarting; confirm it returns to the last page
  used for that file.
- Save a multi-page paper from Documents and reopen the exported PDF. Confirm
  the original PDF page text remains selectable and the annotations are vector;
  only eraser/bucket areas are raster.
