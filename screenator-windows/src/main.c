/*
 * Screenator for Windows -- Win32 platform layer.
 * GPL-2.0-or-later.  See app.h.
 */
#include "app.h"
#include <stdint.h>
#include <stdarg.h>
#include <wchar.h>
#include <hidusage.h>
#include <hidsdi.h>
#include <hidpi.h>
#include "bucket_cursor.xpm"
App g;
#define WM_TRAY          (WM_APP + 1)
#define WM_START_HINTS   (WM_APP + 2)
#define WM_COMMIT_TEXT   (WM_APP + 3)
#define WM_START_TEXT    (WM_APP + 4)
#define WM_START_ERASE   (WM_APP + 5)
#define WM_REFOCUS_TEXT  (WM_APP + 6)
#define WM_PAPER_SHORTCUT (WM_APP + 7)
#define TIMER_HINTS      4
#define TIMER_PAPER_PAN  11
#define ICON_RING_EXTRA  14
static const wchar_t *OVERLAY_CLASS = L"ScreenatorOverlay";
static const wchar_t *ICON_CLASS = L"ScreenatorIcon";
static const wchar_t *MSG_CLASS = L"ScreenatorMessage";
static cairo_surface_t *asset_icon_normal;
static cairo_surface_t *asset_icon_active;
static int old_vx, old_vy, old_vw, old_vh;
static WNDPROC edit_old_proc;
static HHOOK g_wheel_hook;
static HHOOK g_key_hook;
static int paper_control_shortcuts_pending;
static int paper_control_shortcuts_suspended;
static ULONGLONG paper_control_shortcuts_at;
static void app_paper_toggle_topmost(void);
static void app_set_window_topmost(HWND h, int topmost);
static void app_paper_zorder(void);
static Pt paper_map_move_point(Pt p);
static void right_button_up(Pt p);
static int editable_handles_active(void);
static void stylus_step(int dir, int colors_only);
static void pen_scroll_tap(Pt p, int source);
static void pen_barrel_end(void);
static int is_pen_mouse(void);
static void pen_classify(DWORD pointer_flags, DWORD pen_flags,
                         int *first, int *second);
static int pen_first_scroll_only;
static int pen_second_owner;      /* 1 = Windows Ink pointer, 2 = raw HID */
static ULONGLONG pen_second_owner_at;
static ULONGLONG pen_activity_at;
static int text_closing;
static int text_restart_pending;
static Pt text_restart_point;
static int erase_start_pending;
static Pt erase_start_point;
static int paper_pan_middle_prev;
static int paper_pan_timer_started;
static ULONGLONG paper_pan_started_at;
static Pt paper_pan_origin;
static int paper_pan_moved;
static int paper_left_pending;
static int paper_left_panning;
static Pt paper_left_view;
static Pt paper_left_origin;
static int paper_middle_pending;
static Pt paper_middle_view;
static Pt paper_middle_origin;
static int paper_normal_mode;
static int app_paper_on_top;
static void text_accept_and_restart(Pt p);
static void text_accept_and_erase(Pt p);
static int raw_pen_available;
static int pen_second_recent(void);
static int pen_input_recent(void);
static int pen_now_first_side(void);
static int paper_control_at_point(POINT p);
static void unregister_managed_hotkeys(void);

/* ------------------------------------------------------------------ */
/* paths                                                              */
/* ------------------------------------------------------------------ */
void app_exe_dir(wchar_t *buf, int cap)
{
  GetModuleFileNameW(NULL, buf, (DWORD) cap);
  wchar_t *p = wcsrchr(buf, L'\\');
  if (p) *p = 0;
}

void app_asset_path(wchar_t *buf, int cap, const wchar_t *name)
{
  wchar_t dir[MAX_PATH];
  app_exe_dir(dir, MAX_PATH);
  _snwprintf(buf, (size_t) cap, L"%s\\Screenator Data\\assets\\%s", dir, name);
}

void dlog(const char *fmt, ...)
{
  if (!g.debug) return;
  wchar_t path[MAX_PATH];
  char utf8[MAX_PATH * 3];
  app_exe_dir(path, MAX_PATH);
  wcscat(path, L"\\debug.txt");
  WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);
  FILE *f = fopen(utf8, "a");
  if (!f) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
}

void app_force_foreground(HWND hwnd)
{
  if (!hwnd) return;
  HWND fg = GetForegroundWindow();
  DWORD fg_thread = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
  DWORD my_thread = GetCurrentThreadId();
  BOOL attached = FALSE;
  if (fg_thread && fg_thread != my_thread)
    attached = AttachThreadInput(fg_thread, my_thread, TRUE);
  BOOL ok = SetForegroundWindow(hwnd);
  SetFocus(hwnd);
  if (attached)
    AttachThreadInput(fg_thread, my_thread, FALSE);
  (void) ok;
}

static cairo_surface_t *load_png_asset(const wchar_t *name)
{
  wchar_t path[MAX_PATH];
  char utf8[MAX_PATH * 3];
  app_asset_path(path, MAX_PATH, name);
  WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);
  cairo_surface_t *s = cairo_image_surface_create_from_png(utf8);
  if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy(s);
      return NULL;
    }
  return s;
}

/* ------------------------------------------------------------------ */
/* surfaces                                                           */
/* ------------------------------------------------------------------ */
static void get_virtual_screen(void)
{
  g.vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
  g.vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
  g.vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  g.vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (g.vw < 1) g.vw = 1;
  if (g.vh < 1) g.vh = 1;
}

static void destroy_buffers(void)
{
  if (g.backbuf) { cairo_surface_destroy(g.backbuf); g.backbuf = NULL; }
  if (g.present) { cairo_surface_destroy(g.present); g.present = NULL; }
  if (g.strokebuf) { cairo_surface_destroy(g.strokebuf); g.strokebuf = NULL; }
  for (int i = 0; i < MAX_UNDO; i++)
    {
      if (g.undo[i]) { cairo_surface_destroy(g.undo[i]); g.undo[i] = NULL; }
      if (g.redo[i]) { cairo_surface_destroy(g.redo[i]); g.redo[i] = NULL; }
    }
  g.undo_head = g.undo_depth = 0;
  g.redo_head = g.redo_depth = 0;
  if (g.menu_capture)
    { cairo_surface_destroy(g.menu_capture); g.menu_capture = NULL; }
  if (g.menu_ghost)
    { cairo_surface_destroy(g.menu_ghost); g.menu_ghost = NULL; }
  g.menu_ghost_active = 0;
  if (g.dibbits) { g.dibbits = NULL; }
  if (g.dibbmp) { DeleteObject(g.dibbmp); g.dibbmp = NULL; }
  if (g.dibdc) { DeleteDC(g.dibdc); g.dibdc = NULL; }
  g.dibw = g.dibh = 0;
}

static int make_dib(void)
{
  BITMAPINFO bi;
  HDC screen = GetDC(NULL);
  void *bits = NULL;
  int vw = app_view_w(), vh = app_view_h();
  if (g.dibw == vw && g.dibh == vh && g.dibbits)
    return 1;
  if (g.dibbmp) { DeleteObject(g.dibbmp); g.dibbmp = NULL; }
  if (g.dibdc) { DeleteDC(g.dibdc); g.dibdc = NULL; }
  if (g.present) { cairo_surface_destroy(g.present); g.present = NULL; }
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = vw;
  bi.bmiHeader.biHeight = -vh;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  g.dibdc = CreateCompatibleDC(screen);
  g.dibbmp = CreateDIBSection(g.dibdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  ReleaseDC(NULL, screen);
  if (!g.dibbmp || !bits) return 0;
  SelectObject(g.dibdc, g.dibbmp);
  g.dibbits = bits;
  g.dibw = vw;
  g.dibh = vh;
  g.present = cairo_image_surface_create_for_data((unsigned char *) bits,
                                                  CAIRO_FORMAT_ARGB32,
                                                  vw, vh, vw * 4);
  return g.present != NULL;
}

static cairo_surface_t *scale_surface_center(cairo_surface_t *src,
                                             int dw, int dh)
{
  if (!src || dw <= 0 || dh <= 0) return NULL;
  int sw = cairo_image_surface_get_width(src);
  int sh = cairo_image_surface_get_height(src);
  if (sw <= 0 || sh <= 0) return NULL;
  cairo_surface_t *dst =
    cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
  double s = (double) dw / (double) sw;
  double sy = (double) dh / (double) sh;
  if (sy < s) s = sy;
  double ox = (dw - sw * s) / 2.0;
  double oy = (dh - sh * s) / 2.0;
  if (g.paper_windowed && g.paper_size_kind == PAPER_SIZE_SCREEN)
    ox = oy = 0.0;
  cairo_t *cr = cairo_create(dst);
  cairo_translate(cr, ox, oy);
  cairo_scale(cr, s, s);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  return dst;
}

void ui_rebuild_surfaces(void)
{
  cairo_surface_t *old_back = g.backbuf;
  cairo_surface_t *old_undo[MAX_UNDO];
  cairo_surface_t *old_redo[MAX_UNDO];
  int old_undo_head = g.undo_head, old_undo_depth = g.undo_depth;
  int old_redo_head = g.redo_head, old_redo_depth = g.redo_depth;
  for (int i = 0; i < MAX_UNDO; i++)
    { old_undo[i] = g.undo[i]; old_redo[i] = g.redo[i]; }
  /* Keep the live page in sync before the screen dimensions change. */
  if (g.paper != PAPER_NONE) paper_sync_current();
  get_virtual_screen();
  {
    int cw = paper_page_width();
    int ch = paper_page_height();
    paper_resize_all(cw, ch);
    cairo_surface_t *newbuf = scale_surface_center(old_back, cw, ch);
    if (!newbuf)
      newbuf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cw, ch);
    cairo_surface_t *new_undo[MAX_UNDO], *new_redo[MAX_UNDO];
    for (int i = 0; i < MAX_UNDO; i++)
      {
        new_undo[i] = scale_surface_center(old_undo[i], cw, ch);
        new_redo[i] = scale_surface_center(old_redo[i], cw, ch);
      }
    destroy_buffers();
    g.backbuf = newbuf;
    for (int i = 0; i < MAX_UNDO; i++)
      { g.undo[i] = new_undo[i]; g.redo[i] = new_redo[i]; }
    g.undo_head = old_undo_head;
    g.undo_depth = old_undo_depth;
    g.redo_head = old_redo_head;
    g.redo_depth = old_redo_depth;
    if (g.paper != PAPER_NONE) paper_replace_current_surface(g.backbuf);
  }
  if (!make_dib())
    {
      /* Fall back to a plain cairo surface if DIB creation failed. */
      g.present = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, g.vw, g.vh);
    }
  if (g.overlay)
    {
      if (!g.paper_windowed)
        SetWindowPos(g.overlay, HWND_NOTOPMOST, g.vx, g.vy, g.vw, g.vh,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
      if (g.paper != PAPER_NONE) app_paper_zorder();
      else app_set_window_topmost(g.overlay, 1);
      ShowWindow(g.overlay, SW_SHOWNA);
    }
  if (g.inputwin)
    {
      SetWindowPos(g.inputwin, HWND_NOTOPMOST, g.vx, g.vy, g.vw, g.vh,
                   SWP_NOACTIVATE);
      app_set_window_topmost(g.inputwin,
                             !(g.paper != PAPER_NONE && !g.paper_topmost));
      app_raise_icon();
    }
  old_vx = g.vx; old_vy = g.vy; old_vw = g.vw; old_vh = g.vh;
  ui_position_icon();
  ui_layout();
  app_render_all();
}

/* Visible paper height.  When Ctrl+D asks the taskbar to stay visible, the
 * overlay stops at the work area instead of covering the taskbar. */
static int app_paper_view_height(void)
{
  RECT work;
  int h;
  if (g.paper == PAPER_NONE || !g.paper_topmost)
    return app_view_h();
  if (g.paper_windowed) return app_view_h();
  if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) return g.vh;
  h = work.bottom - g.vy;
  if (h < 1) h = g.vh;
  if (h > g.vh) h = g.vh;
  return h;
}

int app_paper_viewport_h(void)
{
  return app_paper_view_height();
}

static void app_size_overlay(void)
{
  if (!g.overlay) return;
  if (g.paper_windowed) return;   /* the frame controls the window size */
  SetWindowPos(g.overlay, NULL, g.vx, g.vy, g.vw,
               paper_normal_mode ? app_paper_view_height() : g.vh,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

/* Width/height of the surface the paper is drawn into: the window client area
 * in windowed mode, otherwise the whole screen. */
int app_view_w(void)
{
  if (g.paper != PAPER_NONE && g.paper_windowed && g.overlay)
    {
      RECT r;
      if (GetClientRect(g.overlay, &r) && r.right > r.left)
        return r.right - r.left;
    }
  return g.vw;
}

int app_view_h(void)
{
  if (g.paper != PAPER_NONE && g.paper_windowed && g.overlay)
    {
      RECT r;
      if (GetClientRect(g.overlay, &r) && r.bottom > r.top)
        return r.bottom - r.top;
    }
  return g.vh;
}

/* Toggle the paper between the full-screen strip and a framed window. */
static void app_set_paper_window_mode(int windowed)
{
  RECT wa;
  LONG_PTR style;
  if (g.paper == PAPER_NONE) return;
  if (g.paper_windowed == windowed) return;
  tools_commit(0);
  paper_sync_current();
  g.paper_windowed = windowed;
  style = GetWindowLongPtrW(g.overlay, GWL_STYLE);
  if (windowed)
    {
      style |= WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME |
               WS_MAXIMIZEBOX;
      SetWindowLongPtrW(g.overlay, GWL_STYLE, style);
      SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
      {
        int w = (int) ((wa.right - wa.left) * 0.8);
        int h = (int) ((wa.bottom - wa.top) * 0.8);
        if (w < 640) w = 640;
        if (h < 480) h = 480;
        SetWindowPos(g.overlay, NULL,
                     wa.left + ((wa.right - wa.left) - w) / 2,
                     wa.top + ((wa.bottom - wa.top) - h) / 2, w, h,
                     SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
      }
    }
  else
    {
      style &= ~(WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME |
                 WS_MAXIMIZEBOX);
      SetWindowLongPtrW(g.overlay, GWL_STYLE, style);
      SetWindowPos(g.overlay, NULL, g.vx, g.vy, g.vw, g.vh,
                   SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
  ui_resize_canvas_for_doc();
  app_paper_zorder();
  app_render_all();
}

/* Copy the top-left part of a canvas into a new one without scaling.  Used
 * for the screen annotation layer, which always starts at the top-left and
 * must not be centred (and thus shifted) when the paper size changes. */
static cairo_surface_t *copy_surface_top_left(cairo_surface_t *src, int w,
                                              int h)
{
  cairo_surface_t *dst =
    cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy(dst);
      return NULL;
    }
  if (src)
    {
      cairo_t *cr = cairo_create(dst);
      cairo_set_source_surface(cr, src, 0, 0);
      cairo_paint(cr);
      cairo_destroy(cr);
    }
  return dst;
}

static void resize_canvas_common(int sync_live, int reload_page)
{
  cairo_surface_t *old_back = g.backbuf;
  cairo_surface_t *old_undo[MAX_UNDO];
  cairo_surface_t *old_redo[MAX_UNDO];
  int old_undo_head = g.undo_head, old_undo_depth = g.undo_depth;
  int old_redo_head = g.redo_head, old_redo_depth = g.redo_depth;
  int cw, ch, screen_mode;
  if (!g.backbuf) return;
  for (int i = 0; i < MAX_UNDO; i++)
    { old_undo[i] = g.undo[i]; old_redo[i] = g.redo[i]; }
  screen_mode = g.paper == PAPER_NONE;
  if (!screen_mode && sync_live) paper_sync_current();
  cw = paper_page_width();
  ch = paper_page_height();
  paper_resize_all(cw, ch);
  {
    cairo_surface_t *newbuf = screen_mode
      ? copy_surface_top_left(old_back, cw, ch)
      : scale_surface_center(old_back, cw, ch);
    cairo_surface_t *new_undo[MAX_UNDO], *new_redo[MAX_UNDO];
    if (!newbuf)
      newbuf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cw, ch);
    for (int i = 0; i < MAX_UNDO; i++)
      {
        new_undo[i] = screen_mode ? copy_surface_top_left(old_undo[i], cw, ch)
                                  : scale_surface_center(old_undo[i], cw, ch);
        new_redo[i] = screen_mode ? copy_surface_top_left(old_redo[i], cw, ch)
                                  : scale_surface_center(old_redo[i], cw, ch);
      }
    destroy_buffers();
    g.backbuf = newbuf;
    for (int i = 0; i < MAX_UNDO; i++)
      { g.undo[i] = new_undo[i]; g.redo[i] = new_redo[i]; }
    g.undo_head = old_undo_head;
    g.undo_depth = old_undo_depth;
    g.redo_head = old_redo_head;
    g.redo_depth = old_redo_depth;
    if (!screen_mode)
      {
        if (sync_live) paper_replace_current_surface(g.backbuf);
        else if (reload_page) paper_reload_current();
      }
  }
  if (!make_dib())
    g.present = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, g.vw, g.vh);
  app_render_all();
}

void ui_resize_canvas(void)
{
  resize_canvas_common(1, 0);
}

/* Resize the canvas to the page size of the document that was just activated
 * (an opened PDF uses its own proportions).  The old live buffer belongs to the
 * previous document, so it is not stored back into a page. */
void ui_resize_canvas_for_doc(void)
{
  resize_canvas_common(0, 1);
}

void app_render(int x, int y, int w, int h)
{
  if (!g.present || !g.overlay || g.shutting_down) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (w <= 0 || h <= 0) return;
  {
    int vw = app_view_w(), vh = app_view_h();
    if (x + w > vw) w = vw - x;
    if (y + h > vh) h = vh - y;
  }
  cairo_t *cr = cairo_create(g.present);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  if (g.paper != PAPER_NONE)
    {
      if (paper_continuous_active())
        paper_draw_continuous(cr, app_view_w(), app_view_h());
      else
        {
          draw_paper(cr, g.paper, app_view_w(), app_view_h());
          paper_draw_page_margin(cr, app_view_w(), app_view_h());
        }
    }
  /* The captured menu is a background image: annotations are drawn on top
   * of it, so it can be marked up and explained while the tool is active. */
  if (g.menu_ghost_active && g.menu_ghost)
    {
      cairo_save(cr);
      cairo_set_source_surface(cr, g.menu_ghost,
                               g.menu_ghost_rect.left - g.vx,
                               g.menu_ghost_rect.top - g.vy);
      cairo_paint(cr);
      cairo_restore(cr);
    }
  if (!g.hide_drawings)
    {
      cairo_save(cr);
      if (paper_continuous_active())
        cairo_translate(cr, 0, paper_current_page_view_top());
      if (!paper_continuous_active())
        draw_backbuffer(cr);
      if (g.pen_drawing && g.stroke_buffer_active && g.strokebuf)
        {
          double alpha = (g.tool == TOOL_MARKER) ? g.marker_opacity : g.opacity;
          cairo_save(cr);
          cairo_set_source_surface(cr, g.strokebuf, 0, 0);
          cairo_paint_with_alpha(cr, alpha);
          cairo_restore(cr);
        }
      draw_preview(cr);
      cairo_restore(cr);
    }
  if (g.text_editor_active)
    {
      cairo_save(cr);
      if (paper_continuous_active())
        cairo_translate(cr, 0, paper_current_page_view_top());
      draw_text_editor(cr);
      cairo_restore(cr);
    }
  cairo_destroy(cr);
  if (!g.dibbits)
    return;
  if (paper_normal_mode)
    {
      HDC dc = GetDC(g.overlay);
      cairo_surface_flush(g.present);
      if (dc)
        {
          BitBlt(dc, 0, 0, app_view_w(), app_view_h(), g.dibdc, 0, 0,
                 SRCCOPY);
          ReleaseDC(g.overlay, dc);
        }
      return;
    }
  cairo_surface_flush(g.present);
  POINT dst = { g.vx, g.vy };
  SIZE size = { g.vw, app_paper_view_height() };
  POINT src = { 0, 0 };
  BLENDFUNCTION blend;
  blend.BlendOp = AC_SRC_OVER;
  blend.BlendFlags = 0;
  blend.SourceConstantAlpha = 255;
  blend.AlphaFormat = AC_SRC_ALPHA;
  RECT dirty = { x, y, x + w, y + h };
  UPDATELAYEREDWINDOWINFO info;
  memset(&info, 0, sizeof(info));
  info.cbSize = sizeof(info);
  info.pptDst = &dst;
  info.psize = &size;
  info.hdcSrc = g.dibdc;
  info.pptSrc = &src;
  info.crKey = 0;
  info.pblend = &blend;
  info.dwFlags = ULW_ALPHA;
  info.prcDirty = (x == 0 && y == 0 && w == g.vw && h == size.cy)
                    ? NULL : &dirty;
  if (!UpdateLayeredWindowIndirect(g.overlay, &info))
    {
      /* Older systems may reject the indirect form; do a full update. */
      if (!UpdateLayeredWindow(g.overlay, NULL, &dst, &size, g.dibdc, &src, 0,
                               &blend, ULW_ALPHA))
        g.render_failures++;
    }
}

void app_render_all(void)
{
  app_render(0, 0, app_view_w(), app_view_h());
}

/* ------------------------------------------------------------------ */
/* menu snapshot (ghost menu)                                         */
/* ------------------------------------------------------------------ */
typedef struct
{
  RECT r[8];
  int count;
} MenuSnapRects;
/* Win32 menus use the #32768 class.  Qt applications such as Patchy draw
 * their menus in a QWindowPopup popup window instead. */
static int is_menu_window_class(const wchar_t *cls)
{
  return wcscmp(cls, L"#32768") == 0 ||
         wcsstr(cls, L"QWindowPopup") != NULL;
}

static BOOL CALLBACK menu_snap_enum(HWND h, LPARAM lp)
{
  MenuSnapRects *m = (MenuSnapRects *) lp;
  if (m->count >= 8) return FALSE;
  if (!IsWindowVisible(h)) return TRUE;
  wchar_t cls[64];
  if (!GetClassNameW(h, cls, 64) || !is_menu_window_class(cls))
    return TRUE;
  RECT r;
  if (!GetWindowRect(h, &r)) return TRUE;
  if (r.right <= r.left || r.bottom <= r.top) return TRUE;
  m->r[m->count++] = r;
  return TRUE;
}

/* Close menus owned by other applications without injecting global input.
 * Posting Esc to the menu window ends its modal loop. */
static BOOL CALLBACK menu_cancel_enum(HWND h, LPARAM lp)
{
  if (!IsWindowVisible(h)) return TRUE;
  wchar_t cls[64];
  if (!GetClassNameW(h, cls, 64) || !is_menu_window_class(cls))
    return TRUE;
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  if (pid == GetCurrentProcessId()) return TRUE;
  PostMessageW(h, WM_KEYDOWN, VK_ESCAPE, 0);
  PostMessageW(h, WM_KEYUP, VK_ESCAPE, 0);
  *(int *) lp = 1;
  return TRUE;
}

static int dismiss_external_menus(void)
{
  int found = 0;
  EnumWindows(menu_cancel_enum, (LPARAM) &found);
  return found;
}

static void menu_snapshot_rect(HDC screen, const RECT *r,
                               cairo_surface_t *dst, int ox, int oy)
{
  int w = r->right - r->left;
  int h = r->bottom - r->top;
  if (w <= 0 || h <= 0) return;
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = NULL;
  HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits,
                                 NULL, 0);
  if (!bmp || !bits) return;
  HDC mem = CreateCompatibleDC(screen);
  HGDIOBJ old = SelectObject(mem, bmp);
  BitBlt(mem, 0, 0, w, h, screen, r->left, r->top, SRCCOPY);
  SelectObject(mem, old);
  DeleteDC(mem);
  /* Screen-copied DIBs have an undefined alpha channel. */
  uint32_t *px = (uint32_t *) bits;
  for (int i = 0; i < w * h; i++) px[i] |= 0xFF000000u;
  cairo_surface_t *src =
    cairo_image_surface_create_for_data((unsigned char *) bits,
                                        CAIRO_FORMAT_ARGB32, w, h, w * 4);
  cairo_surface_mark_dirty(src);
  cairo_t *cr = cairo_create(dst);
  cairo_set_source_surface(cr, src, ox, oy);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(src);
  DeleteObject(bmp);
}

static void menu_capture_screen(void)
{
  MenuSnapRects m;
  memset(&m, 0, sizeof(m));
  /* Context menus from other applications (for example File Explorer) do
   * not belong to this thread, so enumerate every top-level menu window. */
  EnumWindows(menu_snap_enum, (LPARAM) &m);
  if (m.count == 0) return;
  RECT vs = { g.vx, g.vy, g.vx + g.vw, g.vy + g.vh };
  RECT u = m.r[0];
  for (int i = 0; i < m.count; i++)
    {
      if (m.r[i].left < u.left) u.left = m.r[i].left;
      if (m.r[i].top < u.top) u.top = m.r[i].top;
      if (m.r[i].right > u.right) u.right = m.r[i].right;
      if (m.r[i].bottom > u.bottom) u.bottom = m.r[i].bottom;
    }
  if (u.left < vs.left) u.left = vs.left;
  if (u.top < vs.top) u.top = vs.top;
  if (u.right > vs.right) u.right = vs.right;
  if (u.bottom > vs.bottom) u.bottom = vs.bottom;
  if (u.right <= u.left || u.bottom <= u.top) return;
  cairo_surface_t *surf =
    cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                               u.right - u.left, u.bottom - u.top);
  cairo_t *cr = cairo_create(surf);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_destroy(cr);
  HDC screen = GetDC(NULL);
  if (!screen)
    {
      cairo_surface_destroy(surf);
      return;
    }
  for (int i = 0; i < m.count; i++)
    {
      RECT r = m.r[i];
      if (r.left < vs.left) r.left = vs.left;
      if (r.top < vs.top) r.top = vs.top;
      if (r.right > vs.right) r.right = vs.right;
      if (r.bottom > vs.bottom) r.bottom = vs.bottom;
      if (r.right <= r.left || r.bottom <= r.top) continue;
      menu_snapshot_rect(screen, &r, surf, r.left - u.left, r.top - u.top);
    }
  ReleaseDC(NULL, screen);
  if (g.menu_capture) cairo_surface_destroy(g.menu_capture);
  g.menu_capture = surf;
  g.menu_capture_rect = u;
}

void app_menu_capture_start(int kind, int marker)
{
  app_clear_menu_ghost();
  if (g.menu_capture)
    {
      cairo_surface_destroy(g.menu_capture);
      g.menu_capture = NULL;
    }
  g.menu_capture_kind = kind;
  g.menu_capture_marker = marker;
}

void app_menu_capture_stop(int keep)
{
  if (keep && g.menu_capture)
    {
      if (g.menu_ghost) cairo_surface_destroy(g.menu_ghost);
      g.menu_ghost = g.menu_capture;
      g.menu_capture = NULL;
      g.menu_ghost_rect = g.menu_capture_rect;
      g.menu_ghost_kind = g.menu_capture_kind;
      g.menu_ghost_marker = g.menu_capture_marker;
      g.menu_ghost_active = 1;
      dlog("menu ghost kept %ldx%ld at %ld,%ld kind=%d",
           (long) (g.menu_ghost_rect.right - g.menu_ghost_rect.left),
           (long) (g.menu_ghost_rect.bottom - g.menu_ghost_rect.top),
           (long) g.menu_ghost_rect.left, (long) g.menu_ghost_rect.top,
           g.menu_ghost_kind);
    }
  else if (g.menu_capture)
    {
      dlog("menu ghost discarded");
      cairo_surface_destroy(g.menu_capture);
      g.menu_capture = NULL;
    }
  else
    dlog("menu ghost unavailable keep=%d", keep);
  app_request_render();
}

void app_clear_menu_ghost(void)
{
  if (g.menu_ghost)
    {
      cairo_surface_destroy(g.menu_ghost);
      g.menu_ghost = NULL;
    }
  if (g.menu_ghost_active)
    {
      g.menu_ghost_active = 0;
      app_request_render();
    }
}

/* Coalesce the very high-rate pen/mouse move events into at most one full
 * layered-window update every 8 ms. */
void app_request_render(void)
{
  ULONGLONG now = GetTickCount64();
  if (!g.msgwin)
    {
      app_render_all();
      return;
    }
  if (now - g.last_present >= 8)
    {
      if (g.render_pending)
        {
          KillTimer(g.msgwin, TIMER_PRESENT);
          g.render_pending = 0;
        }
      g.last_present = now;
      app_render_all();
    }
  else if (!g.render_pending)
    {
      g.render_pending = 1;
      SetTimer(g.msgwin, TIMER_PRESENT,
               (UINT) (8 - (now - g.last_present)), NULL);
    }
}

/* ------------------------------------------------------------------ */
/* overlay input                                                      */
/* ------------------------------------------------------------------ */
static void app_set_window_topmost(HWND h, int topmost)
{
  if (!h || !IsWindow(h)) return;
  /* Clear first: some layered windows ignore HWND_TOPMOST while they are
   * still in the non-topmost band. */
  if (topmost)
    SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  /* HWND_NOTOPMOST does not reorder a window that is already non-topmost, so
   * use HWND_TOP to raise it above other applications' windows. */
  SetWindowPos(h, topmost ? HWND_TOPMOST : HWND_TOP,
               0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

/* Paper z-order: topmost only while the paper is the active window, so stylus
 * side-button input arrives live, but other applications still cover it when
 * the user switches away. */
static void app_paper_zorder(void)
{
  if (g.paper == PAPER_NONE) return;
  SetWindowPos(g.overlay,
               app_paper_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
               0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void app_set_click_through(int on)
{
  if (!g.inputwin) return;
  if (paper_normal_mode)
    {
      ShowWindow(g.inputwin, SW_HIDE);
      return;
    }
  if (on)
    {
      ShowWindow(g.inputwin, SW_HIDE);
    }
  else
    {
      SetWindowPos(g.inputwin, HWND_NOTOPMOST, g.vx, g.vy, g.vw,
                   app_paper_view_height(),
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
      app_set_window_topmost(g.inputwin,
                             !(g.paper != PAPER_NONE && !g.paper_topmost));
      app_raise_icon();
    }
}

void app_activate_overlay_input(void)
{
  if (!g.overlay) return;
  if (g.menu_open)
    {
      g.overlay_activate_pending = 1;
      return;
    }
  g.overlay_activate_pending = 0;
  dlog("overlay input activated");
  app_set_click_through(0);
  ShowWindow(g.overlay, SW_SHOWNA);
  SetWindowPos(g.overlay, HWND_NOTOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  app_set_window_topmost(g.overlay,
                         !(g.paper != PAPER_NONE && !g.paper_topmost));
  /* The overlay is transparent to mouse input. Put its owned input surface
   * above it after raising the owner, or clicks can fall through to another
   * foreground application. */
  if (g.inputwin && IsWindowVisible(g.inputwin) && !paper_normal_mode)
    SetWindowPos(g.inputwin, HWND_TOPMOST, g.vx, g.vy, g.vw, g.vh,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
  app_raise_icon();
}

void app_deactivate_overlay_input(void)
{
  if (!g.overlay) return;
  if (g.paper != PAPER_NONE) return;  /* the paper keeps the pointer */
  app_set_click_through(1);
}

/* ------------------------------------------------------------------ */
/* cursors                                                            */
/* ------------------------------------------------------------------ */
static unsigned xpm_color(const char *spec)
{
  if (!spec || !spec[0] || !strcmp(spec, "None")) return 0;
  if (spec[0] == '#')
    {
      unsigned r = 0, g = 0, b = 0;
      size_t n = strlen(spec + 1);
      if (n >= 12)
        { sscanf(spec + 1, "%4x%4x%4x", &r, &g, &b); r >>= 8; g >>= 8; b >>= 8; }
      else if (n >= 9)
        { sscanf(spec + 1, "%3x%3x%3x", &r, &g, &b); r >>= 4; g >>= 4; b >>= 4; }
      else if (n >= 6)
        { sscanf(spec + 1, "%2x%2x%2x", &r, &g, &b); }
      else
        {
          unsigned rr = 0, gg = 0, bb = 0;
          sscanf(spec + 1, "%1x%1x%1x", &rr, &gg, &bb);
          r = rr * 17; g = gg * 17; b = bb * 17;
        }
      return 0xFF000000u | (r << 16) | (g << 8) | b;
    }
  if (!_stricmp(spec, "black")) return 0xFF000000u;
  if (!_stricmp(spec, "white")) return 0xFFFFFFFFu;
  return 0xFF000000u;
}

/* Convert an XPM cursor (as embedded by the original Screenator sources) into
 * an HCURSOR, preserving its hotspot. */
static HCURSOR build_xpm_cursor(const char **xpm, int hot_x, int hot_y)
{
  int w = 0, h = 0, n = 0, cpp = 0;
  if (!xpm || sscanf(xpm[0], "%d %d %d %d", &w, &h, &n, &cpp) != 4)
    return NULL;
  if (w < 1 || w > 256 || h < 1 || h > 256 || n < 1 || cpp < 1 || cpp > 4)
    return NULL;
  unsigned *keys = (unsigned *) calloc((size_t) n, sizeof(unsigned));
  unsigned *cols = (unsigned *) calloc((size_t) n, sizeof(unsigned));
  unsigned *pix = (unsigned *) calloc((size_t) w * h, sizeof(unsigned));
  if (!keys || !cols || !pix)
    { free(keys); free(cols); free(pix); return NULL; }
  for (int i = 0; i < n; i++)
    {
      const char *line = xpm[1 + i];
      unsigned key = 0;
      for (int j = 0; j < cpp; j++)
        key = (key << 8) | (unsigned char) line[j];
      keys[i] = key;
      const char *p = line + cpp;
      while (*p == ' ' || *p == '\t') p++;
      if (*p == 'c')
        {
          p++;
          while (*p == ' ' || *p == '\t') p++;
        }
      char spec[64];
      size_t sl = 0;
      while (*p && *p != ' ' && *p != '\t' && sl < sizeof(spec) - 1)
        spec[sl++] = *p++;
      spec[sl] = 0;
      cols[i] = xpm_color(spec);
    }
  for (int y = 0; y < h; y++)
    {
      const char *row = xpm[1 + n + y];
      for (int x = 0; x < w; x++)
        {
          unsigned key = 0;
          for (int j = 0; j < cpp; j++)
            key = (key << 8) | (unsigned char) row[x * cpp + j];
          unsigned c = 0;
          for (int i = 0; i < n; i++)
            if (keys[i] == key) { c = cols[i]; break; }
          pix[y * w + x] = c;
        }
    }
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  HDC screen = GetDC(NULL);
  HDC dc = CreateCompatibleDC(screen);
  void *bits = NULL;
  HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  HCURSOR cur = NULL;
  if (bmp && bits)
    {
      SelectObject(dc, bmp);
      memcpy(bits, pix, (size_t) w * h * 4);
      HBITMAP mask = CreateBitmap(w, h, 1, 1, NULL);
      ICONINFO ii;
      memset(&ii, 0, sizeof(ii));
      ii.fIcon = FALSE;
      ii.xHotspot = hot_x;
      ii.yHotspot = hot_y;
      ii.hbmMask = mask;
      ii.hbmColor = bmp;
      cur = CreateIconIndirect(&ii);
      DeleteObject(mask);
    }
  if (bmp) DeleteObject(bmp);
  DeleteDC(dc);
  ReleaseDC(NULL, screen);
  free(keys);
  free(cols);
  free(pix);
  return cur;
}

static HCURSOR build_bucket_cursor(void)
{
  return build_xpm_cursor((const char **) bucket_cursor_xpm,
                          bucket_cursor_x_hot, bucket_cursor_y_hot);
}

static HCURSOR build_eraser_cursor(int width)
{
  int dim = width + 2;
  if (dim < 8) dim = 8;
  if (dim > 256) dim = 256;
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = dim;
  bi.bmiHeader.biHeight = -dim;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  HDC screen = GetDC(NULL);
  HDC dc = CreateCompatibleDC(screen);
  void *bits = NULL;
  HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!bmp)
    {
      ReleaseDC(NULL, screen);
      DeleteDC(dc);
      return LoadCursorW(NULL, IDC_CROSS);
    }
  SelectObject(dc, bmp);
  cairo_surface_t *surf = cairo_image_surface_create_for_data(
    (unsigned char *) bits, CAIRO_FORMAT_ARGB32, dim, dim, dim * 4);
  cairo_t *cr = cairo_create(surf);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_set_line_width(cr, 2.0);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
  cairo_arc(cr, dim / 2.0, dim / 2.0, width / 2.0, 0, 2 * M_PI);
  cairo_stroke(cr);
  cairo_set_line_width(cr, 1.0);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.9);
  cairo_arc(cr, dim / 2.0, dim / 2.0, width / 2.0, 0, 2 * M_PI);
  cairo_stroke(cr);
  cairo_surface_flush(surf);
  HBITMAP mask = CreateBitmap(dim, dim, 1, 1, NULL);
  ICONINFO ii;
  memset(&ii, 0, sizeof(ii));
  ii.fIcon = FALSE;
  ii.xHotspot = dim / 2;
  ii.yHotspot = dim / 2;
  ii.hbmMask = mask;
  ii.hbmColor = bmp;
  HCURSOR cur = CreateIconIndirect(&ii);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  DeleteObject(mask);
  DeleteObject(bmp);
  DeleteDC(dc);
  ReleaseDC(NULL, screen);
  return cur ? cur : LoadCursorW(NULL, IDC_CROSS);
}

static HCURSOR tool_cursor(void)
{
  if (g.tool == TOOL_NONE) return LoadCursorW(NULL, IDC_ARROW);
  if (g.middle_erasing || g.tool == TOOL_ERASER) return g.cur_erase;
  if (g.tool == TOOL_TEXT) return g.cur_text;
  if (g.tool == TOOL_BUCKET) return g.cur_bucket;
  return g.cur_pen;
}

/* TRUE when the foreground window belongs to this process.  The low-level
 * hook is global, so pen side buttons must only be consumed while Screenator
 * (or its text editor) is the window the user is working in. */
static int our_foreground(void)
{
  HWND fg = GetForegroundWindow();
  DWORD pid = 0;
  if (fg) GetWindowThreadProcessId(fg, &pid);
  return pid == GetCurrentProcessId();
}

static LRESULT CALLBACK wheel_hook_proc(int code, WPARAM wp, LPARAM lp)
{
  if (code == HC_ACTION)
    {
      MSLLHOOKSTRUCT *m = (MSLLHOOKSTRUCT *) lp;
      if (g.paper != PAPER_NONE &&
          (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN))
        {
          if (paper_control_at_point(m->pt))
            app_note_paper_control_action();
          else if (paper_control_shortcuts_pending)
            {
              paper_control_shortcuts_pending = 0;
              paper_control_shortcuts_suspended = 1;
            }
        }
      if (wp == WM_MBUTTONDOWN || wp == WM_MBUTTONUP ||
          wp == WM_RBUTTONDOWN || wp == WM_RBUTTONUP ||
          wp == WM_XBUTTONDOWN || wp == WM_XBUTTONUP)
        dlog("t=%llu hook mouse msg=%u at %ld,%ld data=%08lx extra=%llx",
             (unsigned long long) GetTickCount64(), (unsigned) wp,
             (long) m->pt.x, (long) m->pt.y,
             (unsigned long) m->mouseData,
             (unsigned long long) m->dwExtraInfo);
      /* A colour step started here must always finish on its own release,
       * even if the text editor or handles disappeared in between (the up
       * would otherwise be delivered somewhere that does not clear it and
       * the next press would be ignored). */
      if (g.pen_barrel_down && pen_first_scroll_only)
        {
          int x1up = wp == WM_XBUTTONUP &&
                     GET_XBUTTON_WPARAM(m->mouseData) == XBUTTON1;
          if (wp == WM_MBUTTONUP || x1up ||
              (wp == WM_RBUTTONUP && g.ignore_next_right_up))
            {
              pen_barrel_end();
              if (wp == WM_RBUTTONUP) g.ignore_next_right_up = 0;
              return 1;
            }
        }
      if (wp == WM_MOUSEWHEEL && !ui_is_modal())
        {
          int dir = GET_WHEEL_DELTA_WPARAM(m->mouseData) > 0 ? 1 : -1;
          /* While text is being typed or shape/text handles are visible the
           * wheel recolours the object, in Screen and Paper mode, instead of
           * scrolling or changing the thickness. */
          if (editable_handles_active())
            {
              stylus_step(dir, 1);
              return 1;
            }
          if (g.tool == TOOL_BUCKET && our_foreground())
            {
              step_fill_color(dir);
              return 1;
            }
          if (g.paper != PAPER_NONE && paper_continuous_active())
            {
              paper_scroll_step(-dir);
              return 1;
            }
          if (g.paper != PAPER_NONE && g.tool != TOOL_NONE)
            {
              tools_wheel(GET_WHEEL_DELTA_WPARAM(m->mouseData));
              return 1;          /* the wheel only changes the tool setting */
            }
        }
      /* A stylus first side button is sometimes promoted to an ordinary
       * mouse button instead of an extra pointer button.  Browser button 1
       * still changes the colour while editing; a real middle click is passed
       * through so it can accept text or finish shape editing. */
      if ((wp == WM_MBUTTONDOWN || wp == WM_RBUTTONDOWN ||
           wp == WM_XBUTTONDOWN || wp == WM_MBUTTONUP ||
           wp == WM_RBUTTONUP || wp == WM_XBUTTONUP) &&
          !ui_is_modal() && editable_handles_active() &&
          (g.text_editor_active || our_foreground()))
        {
          /* Consume middle clicks before Windows can move focus away from the
           * inline edit control and make WM_KILLFOCUS commit the text. */
          if (wp == WM_MBUTTONDOWN)
            {
              if (!g.pen_barrel_down)
                {
                  Pt p = { (double) m->pt.x, (double) m->pt.y };
                  pen_scroll_tap(p, 1);
                }
              return 1;
            }
          int penish = ((m->dwExtraInfo & 0xFFFFFF00u) == 0xFF515700u) ||
                       (pen_activity_at != 0 &&
                        GetTickCount64() - pen_activity_at < 1500) ||
                       (wp == WM_MBUTTONDOWN && pen_now_first_side());
          int x1down = wp == WM_XBUTTONDOWN &&
                       GET_XBUTTON_WPARAM(m->mouseData) == XBUTTON1;
          int colour_down = x1down ||
                            (wp == WM_MBUTTONDOWN && penish) ||
                            (wp == WM_RBUTTONDOWN && penish &&
                             pen_now_first_side());
          if (colour_down && !g.pen_second_down && !pen_second_recent())
            {
              Pt p = { (double) m->pt.x, (double) m->pt.y };
              /* Same accounting as a pointer side-button press, so the
               * matching release and any pointer duplicate see a held
               * colour step instead of starting an erase or pan. */
              pen_scroll_tap(p, 1);
              if (wp == WM_RBUTTONDOWN) g.ignore_next_right_up = 1;
              return 1;
            }
        }
    }
  return CallNextHookEx(g_wheel_hook, code, wp, lp);
}

static int paper_shortcut_matches(UINT vk, UINT mods)
{
  if (vk == VK_PRIOR || vk == VK_NEXT || vk == VK_HOME || vk == VK_END)
    return (mods & (MOD_ALT | MOD_WIN)) == 0;
  if (mods == MOD_CONTROL)
    return vk == 'Q' || vk == 'W' || vk == 'S' ||
           vk == 'O' || vk == 'D' || vk == 'B' || vk == 'A' || vk == 'Z';
  if (mods == (MOD_CONTROL | MOD_SHIFT)) return vk == 'Z';
  return mods == MOD_ALT && vk == VK_F4;
}

static int paper_shortcut_dispatch(UINT vk, UINT mods)
{
  int ctrl = (mods & MOD_CONTROL) != 0;
  int shift = (mods & MOD_SHIFT) != 0;

  if (g.paper == PAPER_NONE || !paper_shortcut_matches(vk, mods)) return 0;
  if (ctrl && vk == 'Q') app_exit_paper();
  else if (ctrl && vk == 'W')
    app_set_paper_window_mode(!g.paper_windowed);
  else if (ctrl && vk == 'S')
    { paper_save_dialog(); config_save(); }
  else if (ctrl && vk == 'O') paper_open_pdf_dialog();
  else if (ctrl && vk == 'D') app_paper_toggle_topmost();
  else if (ctrl && vk == 'B')
    { paper_insert_page(1); config_save(); }
  else if (ctrl && vk == 'A')
    { paper_insert_page(0); config_save(); }
  else if (ctrl && vk == 'Z')
    {
      if (shift)
        {
          if (paper_page_change_is_last() && paper_page_can_redo())
            { paper_page_redo(); config_save(); }
          else tools_redo();
        }
      else
        {
          if (paper_page_change_is_last() && paper_page_can_undo())
            { paper_page_undo(); config_save(); }
          else tools_undo();
        }
    }
  else if (vk == VK_PRIOR)
    {
      if (paper_continuous_active()) paper_scroll_step(-1);
      else { paper_prev_page(); config_save(); }
    }
  else if (vk == VK_NEXT)
    {
      if (paper_continuous_active()) paper_scroll_step(1);
      else { paper_next_page(); config_save(); }
    }
  else if (vk == VK_HOME)
    { if (paper_page_has_document()) paper_goto_page(0); }
  else if (vk == VK_END)
    { if (paper_page_has_document())
        paper_goto_page(paper_page_count() - 1); }
  else if (vk == VK_F4 && (mods & MOD_ALT)) app_save_and_quit();
  return 1;
}

static int paper_control_at_point(POINT p)
{
  HWND hit;
  hit = WindowFromPoint(p);
  if (hit == g.iconwin || GetAncestor(hit, GA_ROOT) == g.iconwin) return 1;
  return toolbar_pointer_hit(p);
}

static int paper_control_at_pointer(void)
{
  POINT p;
  return GetCursorPos(&p) && paper_control_at_point(p);
}

void app_note_paper_control_action(void)
{
  if (g.paper == PAPER_NONE) return;
  paper_control_shortcuts_pending = 1;
  paper_control_shortcuts_suspended = 0;
  paper_control_shortcuts_at = GetTickCount64();
}

static LRESULT CALLBACK key_hook_proc(int code, WPARAM wp, LPARAM lp)
{
  if (code == HC_ACTION)
    {
      KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *) lp;
      dlog("t=%llu hook key vk=%02lx scan=%02lx flags=%08lx extra=%llx",
           (unsigned long long) GetTickCount64(),
           (unsigned long) k->vkCode, (unsigned long) k->scanCode,
           (unsigned long) k->flags,
           (unsigned long long) k->dwExtraInfo);
      /* Windows reserves F12 for the debugger, so RegisterHotKey() always
       * fails for it.  The Timer shortcut is handled here instead of being
       * registered as a global hotkey. */
      if ((wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) &&
          !(k->flags & LLKHF_UP))
        {
          UINT have = 0;
          if (GetAsyncKeyState(VK_CONTROL) & 0x8000) have |= MOD_CONTROL;
          if (GetAsyncKeyState(VK_SHIFT) & 0x8000) have |= MOD_SHIFT;
          if (GetAsyncKeyState(VK_MENU) & 0x8000) have |= MOD_ALT;
          int win_down = (GetAsyncKeyState(VK_LWIN) & 0x8000) ||
                         (GetAsyncKeyState(VK_RWIN) & 0x8000);

          /* Alt+Tab and Win+Tab always belong to Windows.  Drop the
           * one-shot UI target so subsequent keys reach the other app. */
          if (g.paper != PAPER_NONE && k->vkCode == VK_TAB &&
              ((have & MOD_ALT) || win_down))
            {
              paper_control_shortcuts_pending = 0;
              paper_control_shortcuts_suspended = 1;
            }
          if (paper_control_shortcuts_pending &&
              GetTickCount64() - paper_control_shortcuts_at > 15000)
            paper_control_shortcuts_pending = 0;

          int modifier_key = k->vkCode == VK_CONTROL ||
                             k->vkCode == VK_LCONTROL ||
                             k->vkCode == VK_RCONTROL ||
                             k->vkCode == VK_SHIFT ||
                             k->vkCode == VK_LSHIFT ||
                             k->vkCode == VK_RSHIFT ||
                             k->vkCode == VK_MENU ||
                             k->vkCode == VK_LMENU ||
                             k->vkCode == VK_RMENU ||
                             k->vkCode == VK_LWIN ||
                             k->vkCode == VK_RWIN;
          if (g.paper != PAPER_NONE && !g.text_editor_active &&
              !ui_is_modal() && !g.menu_open &&
              !paper_control_shortcuts_suspended &&
              (paper_control_shortcuts_pending || paper_control_at_pointer()))
            {
              if (paper_shortcut_matches((UINT) k->vkCode, have))
                {
                  PostMessageW(g.msgwin, WM_PAPER_SHORTCUT, k->vkCode,
                               (LPARAM) have);
                  return 1;
                }
              if (paper_control_shortcuts_pending && !modifier_key)
                paper_control_shortcuts_pending = 0;
            }
          UINT mods = 0, vk = 0;
          if (shortcut_parse(g.shortcuts[SK_TIMER], &mods, &vk) && vk &&
              (UINT) k->vkCode == vk)
            {
              UINT have = 0;
              if (GetKeyState(VK_CONTROL) & 0x8000) have |= MOD_CONTROL;
              if (GetKeyState(VK_SHIFT) & 0x8000) have |= MOD_SHIFT;
              if (GetKeyState(VK_MENU) & 0x8000) have |= MOD_ALT;
              if ((GetKeyState(VK_LWIN) & 0x8000) ||
                  (GetKeyState(VK_RWIN) & 0x8000)) have |= MOD_WIN;
              if (have == mods)
                {
                  PostMessageW(g.msgwin, WM_HOTKEY, HK_TIMER, 0);
                  return 1;
                }
            }
        }
    }
  return CallNextHookEx(g_key_hook, code, wp, lp);
}

void app_install_wheel_hook(void)
{
  if (!g_wheel_hook)
    g_wheel_hook = SetWindowsHookExW(WH_MOUSE_LL, wheel_hook_proc, g.hinst, 0);
  /* The Timer shortcut is handled by this hook (F12 cannot be registered),
   * so it stays installed for the whole session. */
  if (!g_key_hook)
    g_key_hook = SetWindowsHookExW(WH_KEYBOARD_LL, key_hook_proc, g.hinst, 0);
}

void app_remove_wheel_hook(void)
{
  if (g_wheel_hook)
    {
      UnhookWindowsHookEx(g_wheel_hook);
      g_wheel_hook = NULL;
    }
  /* g_key_hook stays installed: it services the Timer shortcut when no tool
   * is active. */
}

void app_update_cursor(void)
{
  if (!g.cur_pen) g.cur_pen = LoadCursorW(NULL, IDC_CROSS);
  if (!g.cur_text) g.cur_text = LoadCursorW(NULL, IDC_IBEAM);
  if (!g.cur_bucket)
    {
      g.cur_bucket = build_bucket_cursor();
      if (!g.cur_bucket) g.cur_bucket = LoadCursorW(NULL, IDC_CROSS);
    }
  if (!g.cur_erase || g.cur_erase_size != (int) g.eraser_width)
    {
      if (g.cur_erase) DestroyCursor(g.cur_erase);
      g.cur_erase = build_eraser_cursor((int) g.eraser_width);
      g.cur_erase_size = (int) g.eraser_width;
    }
  SetCursor(tool_cursor());
}

/* ------------------------------------------------------------------ */
/* floating icon                                                      */
/* ------------------------------------------------------------------ */
static double icon_ring_scale(void)
{
  if (g.icon_size >= 256) return 3.0;
  if (g.icon_size >= 128) return 2.0;
  return 1.0;
}

static void icon_geometry(int *size)
{
  double s = icon_ring_scale();
  *size = (int) g.icon_size + (int) (ICON_RING_EXTRA * 2 * s + 0.5);
}

void app_refresh_icon_image(void)
{
  if (!g.iconwin) return;
  int size;
  icon_geometry(&size);
  if (size < 16) size = 16;
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = size;
  bi.bmiHeader.biHeight = -size;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  HDC screen = GetDC(NULL);
  HDC dc = CreateCompatibleDC(screen);
  void *bits = NULL;
  HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!bmp) { ReleaseDC(NULL, screen); DeleteDC(dc); return; }
  SelectObject(dc, bmp);
  cairo_surface_t *surf = cairo_image_surface_create_for_data(
    (unsigned char *) bits, CAIRO_FORMAT_ARGB32, size, size, size * 4);
  cairo_t *cr = cairo_create(surf);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  double cx = size / 2.0, cy = size / 2.0;
  double r = g.icon_size / 2.0;
  int active = g.tool != TOOL_NONE;
  cairo_surface_t *img = active ? asset_icon_active : asset_icon_normal;
  if (img)
    {
      double iw = cairo_image_surface_get_width(img);
      double ih = cairo_image_surface_get_height(img);
      cairo_save(cr);
      cairo_translate(cr, cx - g.icon_size / 2.0, cy - g.icon_size / 2.0);
      cairo_scale(cr, g.icon_size / iw, g.icon_size / ih);
      cairo_set_source_surface(cr, img, 0, 0);
      cairo_paint(cr);
      cairo_restore(cr);
    }
  else
    {
      cairo_set_source_rgb(cr, 1, 1, 1);
      cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
      cairo_fill(cr);
      cairo_set_source_rgb(cr, 0, 0, 0);
      cairo_set_line_width(cr, 2);
      cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
      cairo_stroke(cr);
    }
  if (g.show_indicator)
    {
      double s = icon_ring_scale();
      /* Nine thickness sectors, counter-clockwise from angle 0. */
      double rr = r + 4.5 * s;
      double halfw = 3.0 * s;
      int width_idx = 0, best = 10000;
      for (int i = 0; i < 9; i++)
        {
          int d = abs((int) g.pen_width - (int) g_widths[i]);
          if (d < best) { best = d; width_idx = i; }
        }
      for (int i = 0; i < 9; i++)
        {
          double a0 = -(double) i * 2 * M_PI / 9.0;
          double a1 = -(double) (i + 1) * 2 * M_PI / 9.0;
          cairo_new_path(cr);
          cairo_arc(cr, cx, cy, rr + halfw, a1, a0);
          cairo_arc_negative(cr, cx, cy, rr - halfw, a0, a1);
          cairo_close_path(cr);
          if (i <= width_idx)
            cairo_set_source_rgb(cr, 1, 0, 1);
          else
            cairo_set_source_rgb(cr, 1, 1, 1);
          cairo_fill_preserve(cr);
          cairo_set_source_rgb(cr, 0, 0, 0);
          cairo_set_line_width(cr, s);
          cairo_stroke(cr);
        }
      /* Outer six-pixel ring: line colour above, fill colour below. */
      double ro = r + 11.0 * s;
      cairo_set_line_width(cr, 6.0 * s);
      cairo_set_source_rgb(cr, g.pen_color.r, g.pen_color.g, g.pen_color.b);
      cairo_arc(cr, cx, cy, ro, M_PI, 2 * M_PI);
      cairo_stroke(cr);
      if (g.fill_enabled)
        {
          cairo_set_source_rgb(cr, g.fill_color.r, g.fill_color.g,
                               g.fill_color.b);
          cairo_arc(cr, cx, cy, ro, 0, M_PI);
          cairo_stroke(cr);
        }
      else
        {
          cairo_save(cr);
          cairo_new_path(cr);
          cairo_arc(cr, cx, cy, ro + 3.0 * s, 0, M_PI);
          cairo_arc_negative(cr, cx, cy, ro - 3.0 * s, M_PI, 0);
          cairo_close_path(cr);
          cairo_clip(cr);
          for (int i = 0; i < 12; i++)
            {
              double a0 = (double) i * M_PI / 12.0;
              double a1 = (double) (i + 1) * M_PI / 12.0;
              cairo_new_path(cr);
              cairo_move_to(cr, cx, cy);
              cairo_arc(cr, cx, cy, ro + 4.0 * s, a0, a1);
              cairo_close_path(cr);
              cairo_set_source_rgb(cr, (i & 1) ? 1 : 0, (i & 1) ? 1 : 0,
                                   (i & 1) ? 1 : 0);
              cairo_fill(cr);
            }
          cairo_restore(cr);
        }
      cairo_set_line_width(cr, s);
      cairo_set_source_rgb(cr, 0, 0, 0);
      cairo_arc(cr, cx, cy, ro + 3.0 * s, 0, 2 * M_PI);
      cairo_stroke(cr);
      cairo_arc(cr, cx, cy, ro - 3.0 * s, 0, 2 * M_PI);
      cairo_stroke(cr);
    }
  cairo_surface_flush(surf);
  RECT wr;
  GetWindowRect(g.iconwin, &wr);
  POINT dst = { wr.left, wr.top };
  SIZE sz = { size, size };
  POINT src = { 0, 0 };
  BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
  UpdateLayeredWindow(g.iconwin, NULL, &dst, &sz, dc, &src, 0, &blend,
                      ULW_ALPHA);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  DeleteObject(bmp);
  DeleteDC(dc);
  ReleaseDC(NULL, screen);
}

void ui_update_icon(void)
{
  app_refresh_icon_image();
  app_update_tray_icon();
  toolbar_refresh();
}

void app_update_tray_icon(void)
{
  if (!g.tray_added) return;
  g.nid.uFlags = NIF_ICON | NIF_TIP;
  g.nid.hIcon = (g.tool != TOOL_NONE) ? g.hicon_active : g.hicon_normal;
  wcscpy(g.nid.szTip, L"Screenator 8.0");
  Shell_NotifyIconW(NIM_MODIFY, &g.nid);
}

void app_raise_icon(void)
{
  if (g.paper == PAPER_NONE || app_paper_on_top) toolbar_raise();
  if (!g.iconwin) return;
  if (!g.show_icon)
    {
      ShowWindow(g.iconwin, SW_HIDE);
      return;
    }
  /* In Paper mode the icon is owned by the paper window, so it follows the
   * paper into and out of the topmost band and stays above the page without
   * remaining above other applications after Alt+Tab. */
  if (g.paper != PAPER_NONE)
    {
      SetWindowPos(g.iconwin, NULL, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                   SWP_SHOWWINDOW);
      return;
    }
  SetWindowPos(g.iconwin, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void ui_position_icon(void)
{
  if (!g.iconwin) return;
  int size;
  icon_geometry(&size);
  RECT wa;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  int margin = 8;
  int x = wa.right - size - margin;
  int y = wa.top + margin;
  switch (g.icon_position)
    {
    case ICON_TL: x = wa.left + margin; y = wa.top + margin; break;
    case ICON_BR: x = wa.right - size - margin; y = wa.bottom - size - margin; break;
    case ICON_BL: x = wa.left + margin; y = wa.bottom - size - margin; break;
    default: break;
    }
  SetWindowPos(g.iconwin, g.paper == PAPER_NONE ? HWND_TOPMOST : NULL,
               x, y, size, size,
               SWP_NOACTIVATE |
               (g.paper != PAPER_NONE ? SWP_NOZORDER : 0) |
               (g.show_icon ? SWP_SHOWWINDOW : 0));
  app_refresh_icon_image();
}

/* ------------------------------------------------------------------ */
/* paper                                                              */
/* ------------------------------------------------------------------ */
/* Defined with the overlay input helpers below. */
static void paper_pan_begin(Pt p);
static void paper_pan_move(Pt p);
static void paper_pan_end(void);
static void paper_pan_finish(void);
void app_set_paper(Paper p, int grab_focus)
{
  toolbar_hide_temporary(0);
  if (g.paper_panning) paper_pan_end();
  dlog("set_paper %d", (int) p);
  int was_active = g.paper != PAPER_NONE;
  if ((p != PAPER_NONE) != was_active)
    {
      paper_control_shortcuts_pending = 0;
      paper_control_shortcuts_suspended = 0;
    }
  /* Switching between Screen and Paper returns to the cursor; switching
   * paper type or PDF file keeps the active tool. */
  if (((p != PAPER_NONE) != was_active))
    {
      if (g.text_editor_active) { g.ignore_next_right_up = 1; app_end_text(1); }
      if (g.tool != TOOL_NONE) tools_exit();
    }
  if (p != PAPER_NONE)
    {
      if (!was_active)
        paper_enter(p);
      else
        paper_select_type(p);
      /* Entering Paper may restore a PDF document from the settings, so use
       * the page's actual background for the mode flag. */
      g.paper = paper_page_background();
      /* An opened PDF uses its own page proportions, so the canvas may need to
       * change size when the document changes. */
      ui_resize_canvas_for_doc();
    }
  else
    {
      paper_left_pending = 0;
      paper_left_panning = 0;
      paper_middle_pending = 0;
      if (was_active) paper_leave();
      g.paper = PAPER_NONE;
    }
  if (p != PAPER_NONE)
    {
      g.paper_prev_focus = GetForegroundWindow();
      g.paper_topmost = 0;
      g.paper_windowed = 0;
      app_paper_on_top = 1;
      {
        /* Paper is a real application window: it appears in Alt+Tab and on
         * the taskbar so it can be switched back to from another app. */
        LONG_PTR ex = GetWindowLongPtrW(g.overlay, GWL_EXSTYLE);
        SetWindowLongPtrW(g.overlay, GWL_EXSTYLE,
                          ((ex & ~WS_EX_NOACTIVATE & ~WS_EX_TOOLWINDOW &
                            ~WS_EX_LAYERED & ~WS_EX_TRANSPARENT) |
                           WS_EX_APPWINDOW));
        paper_normal_mode = 1;
        SetWindowPos(g.overlay, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE | SWP_FRAMECHANGED);
        app_size_overlay();
      }
      app_set_click_through(0);
      ShowWindow(g.overlay, SW_SHOWNA);
      SetWindowPos(g.overlay, HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                   SWP_FRAMECHANGED | SWP_SHOWWINDOW);
      app_paper_zorder();
      app_force_foreground(g.overlay);
      (void) grab_focus;
    }
  else
    {
      {
        LONG_PTR ex = GetWindowLongPtrW(g.overlay, GWL_EXSTYLE);
        SetWindowLongPtrW(g.overlay, GWL_EXSTYLE,
                          ((ex | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
                            WS_EX_LAYERED | WS_EX_TRANSPARENT) &
                           ~WS_EX_APPWINDOW));
        paper_normal_mode = 0;
        SetWindowPos(g.overlay, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE | SWP_FRAMECHANGED);
        app_size_overlay();
        if (g.paper_prev_focus && IsWindow(g.paper_prev_focus) &&
            g.paper_prev_focus != g.overlay)
          SetForegroundWindow(g.paper_prev_focus);
        g.paper_prev_focus = NULL;
      }
      app_paper_on_top = 0;
      g.paper_windowed = 0;
      app_set_window_topmost(g.overlay, 1);
      ShowWindow(g.overlay, SW_SHOWNA);
      if (g.tool == TOOL_NONE)
        app_set_click_through(1);
      else
        {
          /* Returning to Screen mode must raise the overlay again: in Paper
           * mode it is deliberately not topmost so other windows can cover
           * it. */
          app_set_click_through(0);
        }
    }
  app_hotkeys_refresh();
  if (p != PAPER_NONE)
    SetTimer(g.msgwin, TIMER_PAPER_PAN, 16, NULL);
  else
    {
      KillTimer(g.msgwin, TIMER_PAPER_PAN);
      paper_pan_middle_prev = 0;
      if (g.paper_panning) paper_pan_end();
    }
  if (g.iconwin)
    {
      if (g.show_icon)
        SetWindowPos(g.iconwin, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                     SWP_SHOWWINDOW);
      else
        ShowWindow(g.iconwin, SW_HIDE);
    }
  app_render_all();
  dlog("set_paper done paper=%d render_fail=%d", (int) g.paper,
       g.render_failures);
  ui_refresh_tray();
}

void app_exit_paper(void)
{
  app_set_paper(PAPER_NONE, 0);
}

/* Open a PDF passed on the command line or forwarded by a second instance. */
static void app_open_pdf_file(const wchar_t *path)
{
  char error[512] = "";
  if (!path || !path[0]) return;
  if (!paper_open_pdf_path(path, error, sizeof(error)))
    {
      ui_prepare_dialog();
      MessageBoxA(NULL, error[0] ? error : "The PDF file could not be opened.",
                  "Screenator", MB_OK | MB_ICONERROR);
      return;
    }
  app_set_paper(PAPER_PDF, 0);
  app_force_foreground(g.overlay);
  app_render_all();
}

/* Pick the first .pdf file argument, ignoring --options and other files. */
static int app_find_pdf_arg(wchar_t *out, size_t cap)
{
  int argc = 0, found = 0;
  LPWSTR *argv;
  if (!out || cap == 0) return 0;
  argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) return 0;
  for (int i = 1; i < argc && !found; i++)
    {
      size_t n;
      if (!argv[i] || argv[i][0] == L'-') continue;
      n = wcslen(argv[i]);
      if (n > 4 && _wcsicmp(argv[i] + n - 4, L".pdf") == 0)
        {
          wcsncpy(out, argv[i], cap - 1);
          out[cap - 1] = 0;
          found = 1;
        }
    }
  LocalFree(argv);
  return found;
}

/* Ctrl+D in Paper mode: keep the paper above the taskbar (topmost) or let
 * the taskbar and other windows cover it. */
static void app_paper_toggle_topmost(void)
{
  int vh;
  if (g.paper == PAPER_NONE) return;
  g.paper_topmost = !g.paper_topmost;
  vh = app_paper_view_height();
  app_paper_zorder();
  app_size_overlay();
  if (g.inputwin && IsWindowVisible(g.inputwin))
    {
      SetWindowPos(g.inputwin,
                   g.paper_topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                   g.vx, g.vy, g.vw, vh,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
  app_raise_icon();
  app_render_all();
}

void app_cancel_timer(void)
{
  KillTimer(g.msgwin, TIMER_COUNTDOWN);
  g.timer_running = 0;
}

void app_start_timer(void)
{
  if (g.timer_running) return;
  /* Finish the current edit and enter Cursor before locking tool selection
   * for the countdown. tools_exit() also cancels any delayed tool change. */
  app_end_text(1);
  tools_exit();
  g.timer_running = 1;
  SetTimer(g.msgwin, TIMER_COUNTDOWN,
           (g.timer_delay_seconds ? g.timer_delay_seconds : 2) * 1000, NULL);
  dlog("timer start %u s", g.timer_delay_seconds);
}

void app_take_screenshot(void)
{
  if (ui_is_modal() || g.menu_open) return;
  int icon_was_visible = g.show_icon && g.iconwin &&
                         IsWindowVisible(g.iconwin);
  int toolbar_was_hidden = g.hide_toolbar_temp;
  toolbar_hide_temporary(1);
  if (icon_was_visible) ShowWindow(g.iconwin, SW_HIDE);
  app_render_all();
  GdiFlush();
  Sleep(90);
  int w = g.vw, h = g.vh;
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  HDC screen = GetDC(NULL);
  void *bits = NULL;
  HBITMAP bmp = screen ? CreateDIBSection(screen, &bi, DIB_RGB_COLORS,
                                          &bits, NULL, 0) : NULL;
  if (!bmp || !bits)
    {
      if (screen) ReleaseDC(NULL, screen);
      toolbar_hide_temporary(toolbar_was_hidden);
      if (icon_was_visible) { ShowWindow(g.iconwin, SW_SHOWNA);
                              app_raise_icon(); }
      return;
    }
  HDC mem = CreateCompatibleDC(screen);
  HGDIOBJ old = SelectObject(mem, bmp);
  BitBlt(mem, 0, 0, w, h, screen, g.vx, g.vy, SRCCOPY);
  SelectObject(mem, old);
  DeleteDC(mem);
  ReleaseDC(NULL, screen);
  uint32_t *px = (uint32_t *) bits;
  for (int i = 0; i < w * h; i++) px[i] |= 0xFF000000u;
  wchar_t dir[MAX_PATH] = L"";
  if (FAILED(SHGetFolderPathW(NULL, CSIDL_MYPICTURES, NULL, 0, dir)))
    SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, dir);
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t path[MAX_PATH];
  _snwprintf(path, MAX_PATH,
             L"%s\\Screenator-%04d%02d%02d-%02d%02d%02d.png", dir,
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  cairo_surface_t *surf =
    cairo_image_surface_create_for_data((unsigned char *) bits,
                                        CAIRO_FORMAT_ARGB32, w, h, w * 4);
  cairo_surface_mark_dirty(surf);
  int ulen = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
  char *upath = (char *) malloc((size_t) ulen);
  if (upath)
    WideCharToMultiByte(CP_UTF8, 0, path, -1, upath, ulen, NULL, NULL);
  int ok = upath && cairo_surface_write_to_png(surf, upath) ==
                         CAIRO_STATUS_SUCCESS;
  free(upath);
  cairo_surface_destroy(surf);
  DeleteObject(bmp);
  toolbar_hide_temporary(toolbar_was_hidden);
  if (icon_was_visible) { ShowWindow(g.iconwin, SW_SHOWNA); app_raise_icon(); }
  ui_prepare_dialog();
  if (ok)
    MessageBoxW(g.overlay, path, L"Screenshot saved",
                MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
  else
    MessageBoxW(g.overlay, L"Could not save the screenshot.",
                L"Screenshot failed", MB_OK | MB_ICONERROR | MB_TOPMOST);
  dlog("screenshot %ls ok=%d", path, ok);
}

void ui_set_paper(Paper p)
{
  app_set_paper(p, 0);
}

/* Select a paper document explicitly (toolbar/menu Paper Type).  Entering
 * Paper first keeps the screen annotation layer separate; selecting the type
 * afterwards switches to that document. */
void app_select_paper_type(Paper p)
{
  if (p == PAPER_NONE)
    {
      app_set_paper(PAPER_NONE, 0);
      return;
    }
  if (g.paper == PAPER_NONE)
    {
      app_set_paper(p, 0);
      if (paper_page_count() > 0 && paper_page_background() != p)
        paper_select_type(p);
    }
  else
    {
      paper_select_type(p);
    }
  ui_resize_canvas_for_doc();
  /* Choosing a paper type or PDF always brings that document to the front,
   * even when the paper was covered by another application. */
  app_activate_overlay_input();
  app_render_all();
}

void ui_toggle_visibility(void)
{
  if (g.paper != PAPER_NONE) return;
  g.hide_drawings = !g.hide_drawings;
  app_render_all();
  ui_refresh_tray();
}

/* ------------------------------------------------------------------ */
/* text editor                                                        */
/* ------------------------------------------------------------------ */
static void editor_make_font(void)
{
  double px = font_pixel_size(g.font);
  int bold = strstr(g.font, "Bold") != NULL;
  int italic = strstr(g.font, "Italic") != NULL;
  wchar_t face[128] = L"Arial";
  char family[128];
  strncpy(family, g.font, sizeof(family) - 1);
  family[sizeof(family) - 1] = 0;
  char *sp = strstr(family, " Bold");
  if (sp) *sp = 0;
  sp = strstr(family, " Italic");
  if (sp) *sp = 0;
  sp = strrchr(family, ' ');
  if (sp) *sp = 0;
  if (family[0])
    MultiByteToWideChar(CP_UTF8, 0, family, -1, face, 128);
  if (g.editor_font) DeleteObject(g.editor_font);
  g.editor_font = CreateFontW(-(int) (px + 0.5), 0, 0, 0,
                              bold ? FW_BOLD : FW_NORMAL,
                              italic ? TRUE : FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, face);
}

static LRESULT CALLBACK edit_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
    {
    case WM_KEYDOWN:
      if (wp == VK_RETURN)
        {
          PostMessageW(g.msgwin, WM_COMMIT_TEXT, 1, 0);
          return 0;
        }
      if (wp == VK_ESCAPE)
        {
          PostMessageW(g.msgwin, WM_COMMIT_TEXT, 0, 0);
          return 0;
        }
      break;
    case WM_LBUTTONDOWN:
      {
        POINT pt;
        GetCursorPos(&pt);
        Pt p = { (double) (pt.x - g.vx), (double) (pt.y - g.vy) };
        if (g.paper != PAPER_NONE) paper_map_point(&p);
        text_accept_and_restart(p);
      }
      return 0;
    case WM_RBUTTONDOWN:
      /* While the editor is open, button 2 accepts the text and a button 1
       * press only changes the colour, so the editor stays active.  Button 2
       * presses are claimed by the pointer/raw-HID path. */
      if (g.pen_barrel_down ||
          (is_pen_mouse() && !g.pen_second_down && !pen_second_recent() &&
           GetTickCount64() < g.suppress_mouse_until))
        return 0;                     /* colour step already done */
      if (pen_now_first_side() && !g.pen_second_down &&
          !pen_second_recent())
        {
          stylus_step(1, 1);
          return 0;
        }
      PostMessageW(g.msgwin, WM_COMMIT_TEXT, 1, 0);
      return 0;
    case WM_RBUTTONUP:
      return 0;
    case WM_MBUTTONDOWN:
      /* While editing, middle click is the color step for both the mouse and
       * stylus Button 1.  It never accepts text or closes shape handles. */
      dlog("edit middle pen=%d recent=%d first=%d barrel=%d scroll=%d second=%d",
           is_pen_mouse(), pen_input_recent(), pen_now_first_side(),
           g.pen_barrel_down, pen_first_scroll_only, g.pen_second_down);
      if (g.pen_barrel_down && pen_first_scroll_only) return 0;
      pen_scroll_tap((Pt) { 0, 0 }, 1);
      return 0;
    case WM_MBUTTONUP:
      if (g.pen_barrel_down) pen_barrel_end();
      return 0;
    case WM_XBUTTONDOWN:
      /* Pens whose side buttons are mapped to the browser buttons: the
       * second one accepts the text like a right click, the first recolours
       * it like the wheel. */
      if (GET_XBUTTON_WPARAM(wp) == XBUTTON2)
        PostMessageW(g.msgwin, WM_COMMIT_TEXT, 1, 0);
      else stylus_step(1, 1);
      return 0;
    case WM_XBUTTONUP:
      return 0;
    case WM_GETDLGCODE:
      return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    case WM_CHAR:
      /* The key-down message already commits/cancels.  Swallow the generated
       * character too, otherwise the edit control beeps for Enter/Escape. */
      if (wp == VK_RETURN || wp == VK_ESCAPE)
        return 0;
      break;
    case WM_KILLFOCUS:
      dlog("edit killfocus barrel=%d scroll=%d", g.pen_barrel_down,
           pen_first_scroll_only);
      if (g.pen_barrel_down && !text_closing)
        {
          /* A stylus first side button is changing the colour; the editor
           * keeps the text and takes the keyboard focus back. */
          PostMessageW(g.msgwin, WM_REFOCUS_TEXT, 0, 0);
          break;
        }
      if (!text_closing) PostMessageW(g.msgwin, WM_COMMIT_TEXT, 1, 0);
      break;
    }
  LRESULT r = CallWindowProcW(edit_old_proc, hwnd, msg, wp, lp);
  /* Mirror every edit into the on-screen preview and restart the caret. */
  if (msg == WM_CHAR || msg == WM_KEYDOWN || msg == WM_PASTE ||
      msg == WM_CUT || msg == WM_CLEAR || msg == WM_UNDO)
    {
      g.text_caret_visible = 1;
      SetTimer(g.msgwin, TIMER_CARET, 500, NULL);
      app_render_all();
    }
  return r;
}

void app_begin_text(Pt p)
{
  ULONGLONG bt_start = GetTickCount64();
  if (g.text_editor_active) app_end_text(0);
  dlog("begin_text %.0f,%.0f", p.x, p.y);
  g.text_editor_active = 1;
  g.text_caret_visible = 1;
  g.text_editor_click_x = (int) p.x;
  g.text_editor_click_y = (int) p.y;
  app_hotkeys_refresh();
  editor_make_font();
  /* Another foreground app may have covered the click-through overlay since
   * Text was selected. Raise the input surfaces again before focusing the
   * inline editor. */
  app_activate_overlay_input();
  int x = (int) p.x;
  int y = (int) p.y;
  if (g.paper != PAPER_NONE) y += paper_current_page_view_top();
  /* The real editor is a tiny fully transparent window: it keeps keyboard
   * focus, IME, clipboard and caret movement working while everything the
   * user types is painted directly on the overlay. */
  g.textwin = CreateWindowExW(
    WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
    L"EDIT", L"",
    WS_POPUP | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
    x, y, 16, 16, NULL, NULL, g.hinst, NULL);
  if (!g.textwin)
    {
      dlog("begin_text: CreateWindow failed");
      g.text_editor_active = 0;
      return;
    }
  SetLayeredWindowAttributes(g.textwin, 0, 0, LWA_ALPHA);
  dlog("begin_text: win=%p", (void *) g.textwin);
  SendMessageW(g.textwin, WM_SETFONT, (WPARAM) g.editor_font, TRUE);
  edit_old_proc = (WNDPROC) SetWindowLongPtrW(g.textwin, GWLP_WNDPROC,
                                              (LONG_PTR) edit_proc);
  SetWindowTextW(g.textwin, L"");
  ShowWindow(g.textwin, SW_SHOW);
  app_force_foreground(g.textwin);
  SetTimer(g.msgwin, TIMER_CARET, 500, NULL);
  app_render_all();
  dlog("begin_text ready in %llu ms",
       (unsigned long long) (GetTickCount64() - bt_start));
}

void app_end_text(int commit)
{
  if (!g.text_editor_active) return;
  dlog("end_text commit=%d", commit);
  KillTimer(g.msgwin, TIMER_CARET);
  g.text_caret_visible = 1;
  wchar_t wbuf[2048] = L"";
  if (g.textwin)
    GetWindowTextW(g.textwin, wbuf, 2048);
  char utf8[8192];
  WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, utf8, sizeof(utf8), NULL, NULL);
  if (g.textwin)
    {
      SetWindowLongPtrW(g.textwin, GWLP_WNDPROC, (LONG_PTR) edit_old_proc);
      text_closing = 1;
      DestroyWindow(g.textwin);
      text_closing = 0;
      g.textwin = NULL;
    }
  g.text_editor_active = 0;
  app_hotkeys_refresh();
  if (commit)
    tools_text_commit(utf8, 0);
  app_render_all();
}

/* Accept the current text and start a new editor at p.  The work is done from
 * the message queue so it is safe when the request comes from the edit
 * control's own window procedure. */
static void text_accept_and_restart(Pt p)
{
  if (text_restart_pending) return;
  text_restart_pending = 1;
  text_restart_point = p;
  PostMessageW(g.msgwin, WM_START_TEXT, 0, 0);
}

/* Accept the current text, then use the same middle click as a temporary
 * eraser stroke at p. */
static void text_accept_and_erase(Pt p)
{
  if (erase_start_pending) return;
  erase_start_pending = 1;
  erase_start_point = p;
  PostMessageW(g.msgwin, WM_START_ERASE, 0, 0);
}

/* ------------------------------------------------------------------ */
/* right button                                                       */
/* ------------------------------------------------------------------ */
void app_start_right_press(Pt p)
{
  if (g.tool == TOOL_ERASER && g.paper == PAPER_NONE &&
      g.edit == EDIT_NONE &&
      !g.text_editor_active)
    {
      step_eraser_width(1);
      g.right_short_done = 1;
      return;
    }
  if (g.text_editor_active)
    {
      g.ignore_next_right_up = 1;
      g.right_short_done = 1;
      PostMessageW(g.msgwin, WM_COMMIT_TEXT, 1, 0);
      return;
    }
  /* Right click is staged: text and editable handles are consumed first;
   * once those are gone, hold right click to erase in either mode. */
  if (g.edit != EDIT_NONE)
    {
      tools_commit(0);
      g.ignore_next_right_up = 1;
      g.right_short_done = 1;
      return;
    }
  g.right_down = p;
  g.right_erase_active = 1;
  tools_erase_start(p);
  return;
}

static void right_button_up(Pt p)
{
  /* The press was already consumed (text commit or finishing an edit); the
   * matching release must not perform a second right-click action. */
  if (g.ignore_next_right_up)
    {
      g.ignore_next_right_up = 0;
      g.right_short_done = 0;
      g.right_menu_open = 0;
      return;
    }
  if (g.right_erase_active)
    {
      g.right_erase_active = 0;
      tools_erase_end();
      g.right_short_done = 1;
      return;
    }
  if (g.right_timer_active)
    {
      KillTimer(g.msgwin, TIMER_LONG_PRESS);
      g.right_timer_active = 0;
      if (!g.right_menu_open && !g.right_short_done)
        tools_right_short(p);
    }
  else if (!g.right_menu_open && !g.right_short_done)
    tools_right_short(p);
  g.right_menu_open = 0;
  g.right_short_done = 0;
}

/* ------------------------------------------------------------------ */
/* stylus (Windows Ink pointer input)                                 */
/* ------------------------------------------------------------------ */
#ifndef PEN_FLAG_BARREL
#define PEN_FLAG_BARREL   0x1
#define PEN_FLAG_INVERTED 0x2
#define PEN_FLAG_ERASER   0x4
#endif
#ifndef HID_USAGE_DIGITIZER_SECONDARY_BARREL_SWITCH
#define HID_USAGE_DIGITIZER_SECONDARY_BARREL_SWITCH ((USAGE) 0x5A)
#endif
/* Mouse messages promoted from a pen carry this signature in the extra
 * information.  Once we have seen a real pointer message, those promoted
 * events are ignored so the pen is not handled twice. */
static int is_pen_mouse(void)
{
  ULONG_PTR extra = (ULONG_PTR) GetMessageExtraInfo();
  return (extra & 0xFFFFFF00u) == 0xFF515700u;
}

/* Some pen drivers report side buttons only as promoted mouse messages, and
 * the signature is not always present.  Recent pen pointer/HID activity makes
 * those side-button messages distinguishable from a real middle/X button. */
static int pen_input_recent(void)
{
  return pen_activity_at && GetTickCount64() - pen_activity_at < 1500;
}

/* TRUE while the live pointer reports the first side button (and not the
 * second one).  Used to tell a promoted first-side mouse message from the
 * real second side button, which must keep its normal action. */
static int pen_now_first_side(void)
{
  POINTER_INFO pi;
  POINTER_PEN_INFO pen;
  int first = 0, second = 0;
  if (!g.pen_id) return 0;
  memset(&pi, 0, sizeof(pi));
  if (!GetPointerInfo(g.pen_id, &pi) || pi.pointerType != PT_PEN) return 0;
  memset(&pen, 0, sizeof(pen));
  GetPointerPenInfo(g.pen_id, &pen);
  pen_classify((DWORD) pi.pointerFlags, (DWORD) pen.penFlags,
               &first, &second);
  return first && !second;
}

/* TRUE while a placed shape or text still shows its editing handles (or the
 * text editor is open).  In that state the stylus first side button must
 * recolour the object instead of panning, erasing or changing thickness. */
static int editable_handles_active(void)
{
  if (g.text_editor_active) return 1;
  if (g.edit == EDIT_NONE) return 0;
  if (g.edit == EDIT_POLYGON && g.poly_placing) return 0;
  if (g.edit == EDIT_TRIANGLE && g.tri_placing) return 0;
  return 1;
}

static int shape_or_text_tool(Tool tool)
{
  switch (tool)
    {
    case TOOL_TEXT:
    case TOOL_LINE:
    case TOOL_SPLINE:
    case TOOL_POLYGON:
    case TOOL_TRIANGLE:
    case TOOL_ARROW:
    case TOOL_ELLIPSE:
    case TOOL_RRECT:
    case TOOL_MATH:
    case TOOL_BARS:
    case TOOL_RANDOM:
      return 1;
    default:
      return 0;
    }
}

/* One notch of the "mouse wheel" action for the stylus first side button.
 * colours_only is used while a shape or text shows its handles: the wheel
 * recolours it there instead of changing the brush size. */
static void stylus_step(int dir, int colors_only)
{
  /* While text is being typed or a shape/text still shows its handles the
   * first side button must always change the colour, never the thickness or
   * the eraser width. */
  if (editable_handles_active()) colors_only = 1;
  dlog("stylus step dir=%d colors=%d tool=%d", dir, colors_only,
       (int) g.tool);
  if (colors_only)
    {
      if (g.tool == TOOL_BUCKET) step_fill_color(dir);
      else step_line_color(dir);
      return;
    }
  if (g.tool == TOOL_ERASER) step_eraser_width(dir);
  else if (g.tool == TOOL_BUCKET) step_fill_color(dir);
  else if (g.tool == TOOL_MARKER) step_line_color(dir);
  else if (g.stylus_mode == STYLUS_THICKNESS) step_pen_width(dir);
  else step_line_color(dir);
}

/* A first-side press while handles are visible is a colour step, not the
 * start of an eraser stroke or a paper pan.  The press is consumed until the
 * button is released so it cannot also trigger the long-press eraser. */
static void pen_scroll_tap(Pt p, int source)
{
  if (g.pen_barrel_down) return;
  KillTimer(g.msgwin, TIMER_STYLUS_HOLD);
  g.pen_first_source = source;
  g.pen_barrel_down = 1;
  g.pen_barrel_eraser = 0;
  pen_first_scroll_only = 1;
  g.pen_first_begin = GetTickCount64();
  g.pen_last = p;
  g.suppress_mouse_until = GetTickCount64() + 600;
  stylus_step(1, 1);
  app_render_all();
  g.right_short_done = 1;
  dlog("pen scroll tap");
}

static void pen_barrel_begin(Pt p)
{
  if (g.pen_barrel_down) return;
  g.pen_barrel_down = 1;
  g.pen_barrel_eraser = 0;
  pen_first_scroll_only = 0;
  g.pen_first_begin = GetTickCount64();
  g.pen_last = p;
  g.suppress_mouse_until = GetTickCount64() + 600;
  if (g.right_timer_active)
    {
      KillTimer(g.msgwin, TIMER_LONG_PRESS);
      g.right_timer_active = 0;
      g.right_short_done = 1;
    }
  SetTimer(g.msgwin, TIMER_STYLUS_HOLD, 500, NULL);
  dlog("pen barrel down");
}

static void pen_barrel_end(void)
{
  if (!g.pen_barrel_down) return;
  KillTimer(g.msgwin, TIMER_STYLUS_HOLD);
  if (pen_first_scroll_only)
    {
      /* The press already performed its colour step. */
      pen_first_scroll_only = 0;
    }
  else if (g.pen_barrel_eraser)
    {
      tools_erase_end();
    }
  else
    {
      /* Short press: one step of the selected first-button action.  While a
       * shape or text shows its handles, only the colour changes, exactly
       * like the mouse wheel. */
      stylus_step(1, editable_handles_active());
    }
  g.pen_barrel_down = 0;
  g.pen_barrel_eraser = 0;
  g.pen_first_source = 0;
  g.pen_barrel_release = GetTickCount64();
  g.right_short_done = 1;
  g.right_menu_open = 0;
  app_render_all();
  app_raise_icon();
  dlog("pen barrel up");
}

/* An explicit side-button press always starts a fresh first-side action, even
 * if a previous release was lost.  This keeps a stale down flag from making
 * later presses look dead. */
static void pen_first_press(Pt p, int source)
{
  /* With visible editing handles the first side button only recolours the
   * object, in Screen and Paper mode alike. */
  if (editable_handles_active() || g.tool == TOOL_BUCKET)
    {
      pen_scroll_tap(p, source);
      return;
    }
  if (g.pen_barrel_down)
    {
      KillTimer(g.msgwin, TIMER_STYLUS_HOLD);
      if (g.pen_barrel_eraser) tools_erase_end();
      g.pen_barrel_down = 0;
      g.pen_barrel_eraser = 0;
      g.pen_first_source = 0;
      pen_first_scroll_only = 0;
    }
  g.pen_first_source = source;
  if (g.paper == PAPER_NONE && shape_or_text_tool(g.tool))
    {
      /* Once text/shape editing is closed, Button 1 has no Screen-mode
       * action. Consume its hold so it cannot fall through to an eraser. */
      g.pen_barrel_down = 1;
      g.pen_barrel_eraser = 0;
      pen_first_scroll_only = 1;
      g.pen_first_begin = GetTickCount64();
      g.pen_last = p;
      g.suppress_mouse_until = GetTickCount64() + 600;
      g.right_short_done = 1;
      return;
    }
  if (g.tool == TOOL_ERASER)
    {
      /* The first side button holds the selected eraser, including while
       * the pen is hovering above the tablet. */
      g.pen_barrel_down = 1;
      g.pen_barrel_eraser = 1;
      pen_first_scroll_only = 0;
      g.pen_first_begin = GetTickCount64();
      g.pen_last = p;
      g.suppress_mouse_until = GetTickCount64() + 600;
      tools_erase_start(p);
      return;
    }
  if (g.paper == PAPER_NONE)
    {
      /* In Screen mode the first side button keeps its wheel-step action. */
      pen_barrel_begin(p);
      return;
    }
  pen_barrel_begin(p);
}

static void pen_eraser_begin(Pt p)
{
  if (g.pen_eraser_down) return;
  g.pen_eraser_down = 1;
  tools_erase_start(p);
  dlog("pen eraser down");
}

static void pen_eraser_end(void)
{
  if (!g.pen_eraser_down) return;
  g.pen_eraser_down = 0;
  tools_erase_end();
  g.pen_eraser_release = GetTickCount64();
  dlog("pen eraser up");
}

static void release_pen_capture(void)
{
  HWND cap = GetCapture();
  if (cap == g.overlay || cap == g.inputwin) ReleaseCapture();
}

/* Windows Ink and the raw HID fallback both report the second side button.
 * Whichever path sees the press first owns it; the other path ignores the
 * duplicate edge, otherwise one button press behaves like two right clicks. */
static int pen_second_owner_is_other(int source)
{
  return pen_second_owner && pen_second_owner != source &&
         GetTickCount64() - pen_second_owner_at < 400;
}

static int pen_second_recent(void)
{
  return pen_second_owner && GetTickCount64() - pen_second_owner_at < 500;
}

static void pen_second_begin(Pt p, int source)
{
  if (g.pen_second_down || g.pen_barrel_down) return;
  if (pen_second_owner_is_other(source)) return;
  pen_second_owner = source;
  pen_second_owner_at = GetTickCount64();
  g.pen_last = p;
  g.pen_second_down = 1;
  g.menu_hold_is_stylus = 1;
  app_start_right_press(p);
}

static void pen_second_end(Pt p)
{
  if (!g.pen_second_down) return;
  g.pen_second_down = 0;
  pen_second_owner_at = GetTickCount64();
  right_button_up(p);
  release_pen_capture();
  app_raise_icon();
}

/* Windows reports the primary pen barrel button with POINTER_FLAG_SECONDBUTTON
 * and/or PEN_FLAG_BARREL.  Treat those as Button 1; Button 2 is obtained from
 * the device's secondary raw-HID button or a promoted right-click event. */
static void pen_classify(DWORD pointer_flags, DWORD pen_flags,
                         int *first, int *second)
{
  int barrel = (pen_flags & PEN_FLAG_BARREL) != 0;
  int third = (pointer_flags & POINTER_FLAG_THIRDBUTTON) != 0;
  int extra = (pointer_flags & (POINTER_FLAG_SECONDBUTTON |
                                POINTER_FLAG_FOURTHBUTTON |
                                POINTER_FLAG_FIFTHBUTTON)) != 0;
  if (first) *first = !third && (extra || barrel);
  if (second) *second = third;
}

static void pen_cancel(void)
{
  if (g.pen_pointer_active)
    {
      tools_pointer_up(g.pen_last, 0, 0);
      g.pen_pointer_active = 0;
    }
  if (g.pen_eraser_down) pen_eraser_end();
  if (g.pen_second_down)
    {
      if (g.right_erase_active)
        {
          g.right_erase_active = 0;
          tools_erase_end();
        }
      g.pen_second_down = 0;
      pen_second_owner = 0;
      KillTimer(g.msgwin, TIMER_LONG_PRESS);
      g.right_timer_active = 0;
      g.right_menu_open = 0;
      g.right_short_done = 1;
    }
  if (g.pen_barrel_down)
    {
      KillTimer(g.msgwin, TIMER_STYLUS_HOLD);
      if (g.pen_barrel_eraser) tools_erase_end();
      g.pen_barrel_down = 0;
      g.pen_barrel_eraser = 0;
      g.pen_first_source = 0;
      pen_first_scroll_only = 0;
    }
  g.suppress_mouse_until = GetTickCount64() + 400;
  if (g.paper_panning && g.paper_pan_pen) paper_pan_end();
  release_pen_capture();
}

/* Switching from the pen to the mouse must not leave a pen gesture (or the
 * pen-only side-button handling) behind.  A real mouse message clears stale
 * pen state unless the pen tip is still in contact. */
static void mouse_reset_pen_state(void)
{
  POINTER_INFO pi;
  int active;

  if (is_pen_mouse()) return;
  if (g.pen_id && GetPointerInfo(g.pen_id, &pi) &&
      (pi.pointerFlags & POINTER_FLAG_INCONTACT))
    return;
  active = g.pen_pointer_active || g.pen_eraser_down ||
           g.pen_barrel_down || g.pen_second_down ||
           (g.paper_panning && g.paper_pan_pen);
  if (active) pen_cancel();
  if (g.paper_panning && g.paper_pan_pen) paper_pan_end();
  g.pen_seen = 0;
  pen_second_owner = 0;
}

static void pen_pointer_event(HWND hwnd, UINT msg, WPARAM wp)
{
  UINT32 id = GET_POINTERID_WPARAM(wp);
  POINTER_INFO pi;
  POINTER_PEN_INFO pen;
  if (!GetPointerInfo(id, &pi)) return;
  if (pi.pointerType != PT_PEN) return;
  pen_activity_at = GetTickCount64();
  memset(&pen, 0, sizeof(pen));
  GetPointerPenInfo(id, &pen);
  Pt p = { (double) pi.ptPixelLocation.x, (double) pi.ptPixelLocation.y };
  int barrel = (pen.penFlags & PEN_FLAG_BARREL) != 0;
  int first_side = 0, second = 0;
  pen_classify((DWORD) pi.pointerFlags, (DWORD) pen.penFlags,
               &first_side, &second);
  int eraser = (pen.penFlags & (PEN_FLAG_ERASER | PEN_FLAG_INVERTED)) != 0;
  int down = (pi.pointerFlags & POINTER_FLAG_DOWN) != 0;
  int up = (pi.pointerFlags & POINTER_FLAG_UP) != 0;
  int update = (pi.pointerFlags & POINTER_FLAG_UPDATE) != 0;
  int contact = (pi.pointerFlags & POINTER_FLAG_INCONTACT) != 0;
  int inrange = (pi.pointerFlags & POINTER_FLAG_INRANGE) != 0;
  int on_screen = p.x >= g.vx && p.x < g.vx + g.vw &&
                  p.y >= g.vy && p.y < g.vy + g.vh;
  int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  /* While text is being typed, the side buttons change the colour or accept
   * the text, and another tip press accepts the text and starts a new one. */
  if (g.text_editor_active)
    {
      if ((msg == WM_POINTERDOWN || down) && contact && !first_side &&
          !second && !barrel && !eraser)
        {
          Pt tp = { p.x - g.vx, p.y - g.vy };
          if (g.paper != PAPER_NONE) paper_map_point(&tp);
          text_accept_and_restart(tp);
          return;
        }
      if (g.pen_barrel_down)
        {
          if (!first_side) pen_barrel_end();
          return;
        }
      if (g.pen_second_down && !second)
        {
          pen_second_end(p);
          return;
        }
      if (second)
        {
          pen_second_begin(p, 1);
          return;
        }
      /* The first side button is reported on in-range updates as well: the
       * pen is already hovering (or touching), so the press is not a DOWN
       * transition.  Handle it on every message. */
      if (first_side)
        {
          pen_scroll_tap(p, 2);
          return;
        }
      return;
    }
  /* Convert to page-local coordinates for the continuous paper view.  A
   * contact or press also selects the page under the pen.  Keep the raw
   * viewport position for paper panning. */
  if (g.paper != PAPER_NONE)
    {
      Pt raw = p;
      int prepare = down || msg == WM_POINTERDOWN ||
                    (contact && !g.pen_pointer_active &&
                     !g.pen_eraser_down && !g.pen_second_down &&
                     !g.pen_barrel_down);
      if (g.pen_barrel_down && pen_first_scroll_only)
        {
          if (!first_side) pen_barrel_end();
          return;
        }
      if (g.paper_panning && g.paper_pan_pen)
        {
          /* Keep panning on every pointer update until the pen is released.
           * Some drivers only report the side button on the press, so waiting
           * for first_side in each update made the page jump only at release. */
          if (msg == WM_POINTERUP || up)
            {
              paper_pan_finish();
              return;
            }
          paper_pan_move(raw);
          return;
        }
      if (first_side && !g.pen_barrel_down)
        {
          /* With handles visible the first side button recolours the selected
           * shape or text instead of panning the paper. */
          if (editable_handles_active() || g.tool == TOOL_BUCKET)
            {
              pen_scroll_tap(raw, 2);
              return;
            }
          paper_pan_begin(raw);
          g.paper_pan_pen = 1;
          return;
        }
      {
        POINT pt = { (int) p.x, (int) p.y };
        ScreenToClient(g.overlay, &pt);
        p.x = (double) pt.x + g.vx;
        p.y = (double) pt.y + g.vy;
      }
      if (prepare) paper_prepare_point(&p);
      else p = paper_map_move_point(p);
    }
  DWORD button_flags = pi.pointerFlags &
    (POINTER_FLAG_FIRSTBUTTON | POINTER_FLAG_SECONDBUTTON |
     POINTER_FLAG_THIRDBUTTON | POINTER_FLAG_FOURTHBUTTON |
     POINTER_FLAG_FIFTHBUTTON);
  if (down || up || barrel || eraser || second || button_flags)
    dlog("pen msg=%u at %.0f,%.0f flags=%08lx buttons=%08lx pen=%08x "
         "down=%d up=%d contact=%d barrel=%d second=%d eraser=%d tool=%d",
         msg, p.x, p.y, (unsigned long) pi.pointerFlags,
         (unsigned long) button_flags, pen.penFlags, down,
         up, contact, barrel, second, eraser, (int) g.tool);
  if (first_side)
    dlog("pen first side (pointer second button) at %.0f,%.0f", p.x, p.y);
  g.pen_seen = 1;
  g.pen_id = id;
  g.pen_last = p;
  g.suppress_mouse_until = GetTickCount64() + 600;
  SetTimer(g.msgwin, TIMER_PEN_POLL, 40, NULL);
  /* Side-button presses should not capture the pointer: the popup menus and
   * the floating icon must still receive the pen tip. */
  if (down && !second && !barrel && !first_side)
    SetCapture(hwnd);
  if (msg == WM_POINTERDOWN || down)
    {
      if (eraser)
        {
          pen_eraser_begin(p);
          return;
        }
      if (second)
        {
          pen_second_begin(p, 1);
          return;
        }
      if (raw_pen_available && barrel) return;  /* raw HID owns it */
      if (first_side)
        {
          pen_first_press(p, 2);
          return;
        }
      if (g.pen_barrel_down) return;   /* first side is held */
      if (!g.pen_pointer_active)
        {
          if (g.right_timer_active || g.right_menu_open) return;
          g.pen_pointer_active = 1;
          tools_pointer_down(p, 0, ctrl, shift);
        }
      return;
    }
  if (msg == WM_POINTERUP || up)
    {
      if (g.pen_second_down)
        {
          pen_second_end(p);
          return;
        }
      if (g.pen_eraser_down)
        pen_eraser_end();
      else if (g.pen_barrel_down)
        pen_barrel_end();
      else if (g.pen_pointer_active)
        {
          g.pen_pointer_active = 0;
          tools_pointer_up(p, ctrl, shift);
        }
      if (!g.pen_barrel_down && !g.pen_eraser_down) release_pen_capture();
      return;
    }
  /* In-range updates, including hovering with a side button held. */
  if (g.pen_second_down)
    {
      if (!second)
        {
          pen_second_end(p);
        }
      else if (g.right_erase_active)
        tools_erase_move(p);
      return;
    }
  if (g.pen_eraser_down)
    {
      tools_erase_move(p);
      return;
    }
  if (g.pen_barrel_down)
    {
      /* If a middle-button release was missed, the key state still tells us
       * the button is up; otherwise wait for WM_POINTERUP. */
      if (g.pen_first_source == 1 &&
          !(GetKeyState(VK_MBUTTON) & 0x8000))
        {
          pen_barrel_end();
          return;
        }
      g.pen_last = p;
      if (g.pen_barrel_eraser) tools_erase_move(p);
      return;
    }
  if (raw_pen_available && barrel) return;  /* raw HID owns it */
  if (first_side)
    {
      g.pen_first_source = 2;
      pen_first_press(p, 2);
      return;
    }
  if (g.pen_pointer_active)
    {
      if (g.right_timer_active || g.right_menu_open) return;
      /* Ignore a final out-of-range update; its coordinates can be stale and
       * would flash a stray segment while drawing. */
      if (!inrange && !contact) return;
      tools_pointer_move(p, ctrl, shift);
      return;
    }
  /* A barrel/second-button press while hovering arrives as an update. */
  if (second)
    {
      pen_second_begin(p, 1);
      return;
    }
  if (eraser && contact)
    {
      pen_eraser_begin(p);
      return;
    }
  /* Hovering without contact drives the live shape previews.  Some pen
   * drivers send a final out-of-range update before WM_POINTERLEAVE; using
   * its coordinates would draw a stray rubber-band line for one frame. */
  if (!g.right_timer_active && !g.right_menu_open && inrange && on_screen)
    {
      if (g.edit == EDIT_SPLINE && !g.pen_pointer_active)
        tools_spline_hover(p);
      else if (g.edit == EDIT_TRIANGLE && g.tri_placing &&
               !g.pen_pointer_active)
        tools_triangle_hover(p);
    }
  (void) update;
}

/* Some pen drivers only report the side buttons together with tip contact,
 * so a press or release while merely hovering can produce no message at all.
 * Poll the current pen pointer while it is in range to catch those edges. */
static int pen_poll_state(void)
{
  if (!g.pen_id) return 0;
  POINTER_INFO pi;
  memset(&pi, 0, sizeof(pi));
  if (!GetPointerInfo(g.pen_id, &pi) || pi.pointerType != PT_PEN) return 0;
  if (!(pi.pointerFlags & POINTER_FLAG_INRANGE)) return 0;
  POINTER_PEN_INFO pen;
  memset(&pen, 0, sizeof(pen));
  GetPointerPenInfo(g.pen_id, &pen);
  int pointer_first = 0, pointer_second = 0;
  pen_classify((DWORD) pi.pointerFlags, (DWORD) pen.penFlags,
               &pointer_first, &pointer_second);
  int middle = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
  Pt p = { (double) pi.ptPixelLocation.x, (double) pi.ptPixelLocation.y };
  /* While the paper is being dragged the side button belongs to the pan, not
   * to an eraser or colour step.  Polling the pen position keeps the page
   * moving even when the driver does not send pointer updates during hover. */
  if (g.paper_panning)
    {
      if (g.paper_pan_pen) paper_pan_move(p);
      g.pen_last = p;
      return 1;
    }
  if (pointer_first && !g.pen_barrel_down && !g.pen_second_down)
    {
      /* If the press was missed, the poll can still start a paper pan. */
      if (g.paper != PAPER_NONE && g.tool != TOOL_BUCKET && !editable_handles_active() &&
          !g.paper_panning)
        {
          dlog("poll first side down -> paper pan");
          paper_pan_begin(p);
          g.paper_pan_pen = 1;
          return 1;
        }
      dlog("poll first side down (pointer button)");
      pen_first_press(p, 2);
      return 1;
    }
  if (!pointer_first && middle && !g.pen_barrel_down && !g.pen_second_down)
    {
      dlog("poll first side down (middle button)");
      pen_first_press(p, 1);
      return 1;
    }
  if (g.pen_barrel_down)
    {
      if (g.pen_first_source == 1 && !middle)
        {
          dlog("poll first side up (middle button)");
          pen_barrel_end();
          return 1;
        }
      g.pen_last = p;
      if (g.pen_barrel_eraser) tools_erase_move(p);
      return 1;
    }
  if (pointer_second && !g.pen_second_down)
    {
      if (g.paper != PAPER_NONE)
        {
          POINT pt = { (int) p.x, (int) p.y };
          ScreenToClient(g.overlay, &pt);
          p.x = (double) pt.x + g.vx;
          p.y = (double) pt.y + g.vy;
          paper_prepare_point(&p);
        }
      dlog("poll second side down");
      pen_second_begin(p, 1);
      return 1;
    }
  if (!pointer_second && g.pen_second_down)
    {
      dlog("poll second side up");
      pen_second_end(p);
      return 1;
    }
  return 1;
}

/* ------------------------------------------------------------------ */
/* raw pen input                                                      */
/* ------------------------------------------------------------------ */
/* Windows Ink sometimes only reports the side buttons together with tip
 * contact.  Reading the pen's HID buttons directly lets the first side button
 * work while the pen is merely hovering. */
#define RAW_PEN_MAX 8
typedef struct
{
  HANDLE hDevice;
  HANDLE file;
  PHIDP_PREPARSED_DATA ppd;
  HIDP_CAPS caps;
  HIDP_BUTTON_CAPS *button_caps;
  USHORT button_cap_count;
} RawPenDevice;
static RawPenDevice raw_pens[RAW_PEN_MAX];
static int raw_pen_count;
static int raw_barrel_prev;
static int raw_second_prev;
static int raw_tip_prev;
static void app_register_raw_pen(void)
{
  RAWINPUTDEVICE rid;
  memset(&rid, 0, sizeof(rid));
  rid.usUsagePage = HID_USAGE_PAGE_DIGITIZER;
  rid.usUsage = HID_USAGE_DIGITIZER_PEN;
  rid.dwFlags = RIDEV_INPUTSINK;
  /* Raw input is not delivered to HWND_MESSAGE windows. */
  rid.hwndTarget = g.overlay;
  BOOL ok = RegisterRawInputDevices(&rid, 1, sizeof(rid));
  dlog("raw pen register %d err=%lu", ok ? 1 : 0,
       ok ? 0UL : (unsigned long) GetLastError());
  /* Find out whether a pen digitizer is actually present, so the Windows Ink
   * barrel mapping can defer to the raw HID barrel switch. */
  UINT count = 0;
  if (GetRawInputDeviceList(NULL, &count, sizeof(RAWINPUTDEVICELIST)) ==
      (UINT) -1 || count == 0)
    return;
  RAWINPUTDEVICELIST *list =
    (RAWINPUTDEVICELIST *) calloc(count, sizeof(RAWINPUTDEVICELIST));
  if (!list) return;
  UINT got = count;
  if (GetRawInputDeviceList(list, &got, sizeof(RAWINPUTDEVICELIST)) !=
        (UINT) -1 &&
      got <= count)
    {
      for (UINT i = 0; i < got; i++)
        {
          if (list[i].dwType != RIM_TYPEHID) continue;
          RID_DEVICE_INFO info;
          memset(&info, 0, sizeof(info));
          info.cbSize = sizeof(info);
          UINT sz = sizeof(info);
          if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICEINFO,
                                     &info, &sz) == (UINT) -1)
            continue;
          dlog("raw hid %u usage=%04x/%04x", i,
               (unsigned) info.hid.usUsagePage,
               (unsigned) info.hid.usUsage);
          if (info.hid.usUsagePage == HID_USAGE_PAGE_DIGITIZER &&
              info.hid.usUsage == HID_USAGE_DIGITIZER_PEN)
            {
              dlog("raw pen device present");
              break;
            }
        }
    }
  free(list);
}

static RawPenDevice *raw_pen_lookup(HANDLE hDevice)
{
  for (int i = 0; i < raw_pen_count; i++)
    if (raw_pens[i].hDevice == hDevice)
      return &raw_pens[i];
  if (raw_pen_count >= RAW_PEN_MAX) return NULL;
  UINT size = 0;
  if (GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, NULL, &size) != 0)
    return NULL;
  if (size == 0) return NULL;
  wchar_t *name = (wchar_t *) malloc((size_t) size * sizeof(wchar_t));
  if (!name) return NULL;
  if (GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, name, &size) ==
      (UINT) -1)
    {
      free(name);
      return NULL;
    }
  HANDLE file = CreateFileW(name, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_EXISTING, 0, NULL);
  free(name);
  if (file == INVALID_HANDLE_VALUE) return NULL;
  PHIDP_PREPARSED_DATA ppd = NULL;
  if (!HidD_GetPreparsedData(file, &ppd))
    {
      CloseHandle(file);
      return NULL;
    }
  HIDP_CAPS caps;
  memset(&caps, 0, sizeof(caps));
  if (HidP_GetCaps(ppd, &caps) != HIDP_STATUS_SUCCESS)
    {
      HidD_FreePreparsedData(ppd);
      CloseHandle(file);
      return NULL;
    }
  RawPenDevice *dev = &raw_pens[raw_pen_count++];
  dev->hDevice = hDevice;
  dev->file = file;
  dev->ppd = ppd;
  dev->caps = caps;
  USHORT n = caps.NumberInputButtonCaps;
  if (n > 0)
    {
      HIDP_BUTTON_CAPS *bc =
        (HIDP_BUTTON_CAPS *) calloc(n, sizeof(HIDP_BUTTON_CAPS));
      if (bc && HidP_GetButtonCaps(HidP_Input, bc, &n, ppd) ==
                HIDP_STATUS_SUCCESS)
        {
          dev->button_caps = bc;
          dev->button_cap_count = n;
          for (USHORT i = 0; i < n; i++)
            {
              USAGE lo = bc[i].IsRange ? bc[i].Range.UsageMin
                                       : bc[i].NotRange.Usage;
              USAGE hi = bc[i].IsRange ? bc[i].Range.UsageMax
                                       : bc[i].NotRange.Usage;
              dlog("raw pen button cap page=0x%02x usage=0x%02x-0x%02x",
                   (unsigned) bc[i].UsagePage, (unsigned) lo, (unsigned) hi);
            }
        }
      else
        free(bc);
    }
  dlog("raw pen device %d buttons=%u", raw_pen_count,
       (unsigned) dev->button_cap_count);
  return dev;
}

static void raw_pen_report(PRAWINPUTHEADER hdr, PRAWHID hid)
{
  RawPenDevice *dev = raw_pen_lookup(hdr->hDevice);
  if (!dev || !dev->ppd || !dev->button_caps) return;
  int barrel = 0, second = 0, tip = 0;
  USAGE usages[64];
  for (USHORT i = 0; i < dev->button_cap_count; i++)
    {
      HIDP_BUTTON_CAPS *cap = &dev->button_caps[i];
      ULONG count = 64;
      if (HidP_GetUsages(HidP_Input, cap->UsagePage, cap->LinkCollection,
                         usages, &count, dev->ppd, (PCHAR) hid->bRawData,
                         hid->dwSizeHid) != HIDP_STATUS_SUCCESS)
        continue;
      if (cap->UsagePage != HID_USAGE_PAGE_DIGITIZER) continue;
      for (ULONG j = 0; j < count; j++)
        {
          if (usages[j] == HID_USAGE_DIGITIZER_TIP_SWITCH) tip = 1;
          else if (usages[j] == HID_USAGE_DIGITIZER_BARREL_SWITCH)
            barrel = 1;
          else if (usages[j] ==
                   HID_USAGE_DIGITIZER_SECONDARY_BARREL_SWITCH)
            second = 1;
        }
    }
  if (!raw_pen_available)
    {
      raw_pen_available = 1;
      dlog("raw pen reports active");
    }
  if (tip != raw_tip_prev || barrel != raw_barrel_prev ||
      second != raw_second_prev)
    dlog("raw pen state tip=%d barrel=%d second=%d", tip, barrel, second);
  raw_tip_prev = tip;
  pen_activity_at = GetTickCount64();
  if (g.tool == TOOL_NONE && g.paper == PAPER_NONE)
    {
      raw_barrel_prev = barrel;
      raw_second_prev = second;
      return;
    }
  POINT pt;
  GetCursorPos(&pt);
  Pt p = { (double) pt.x, (double) pt.y };
  /* Prefer the pen pointer position: some drivers only update the system
   * cursor at contact/release, which made a hovering side-button drag appear
   * to jump only when the button was released. */
  if (g.pen_id)
    {
      POINTER_INFO ppi;
      memset(&ppi, 0, sizeof(ppi));
      if (GetPointerInfo(g.pen_id, &ppi) && ppi.pointerType == PT_PEN)
        {
          p.x = (double) ppi.ptPixelLocation.x;
          p.y = (double) ppi.ptPixelLocation.y;
        }
    }
  if (ui_is_modal() || g.menu_open || g.text_editor_active)
    {
      /* Raw HID still reports the side buttons while the text editor owns
       * the pointer.  Button 2 accepts the text like a right click and
       * button 1 changes its colour like the wheel. */
      if (g.text_editor_active)
        {
          if (barrel && !raw_barrel_prev && !g.pen_barrel_down)
            pen_scroll_tap(p, 2);
          else if (!barrel && raw_barrel_prev && g.pen_barrel_down)
            pen_barrel_end();
          if (second && !raw_second_prev && !g.pen_second_down)
            PostMessageW(g.msgwin, WM_COMMIT_TEXT, 1, 0);
        }
      raw_barrel_prev = barrel;
      raw_second_prev = second;
      return;
    }
  if (g.paper != PAPER_NONE)
    {
      Pt raw = p;
      int prepare = (barrel && !raw_barrel_prev) ||
                    (second && !raw_second_prev);
      if (barrel && !raw_barrel_prev && !g.pen_barrel_down)
        {
          /* With handles visible the first side button recolours the
           * selected object instead of panning. */
          if (editable_handles_active() || g.tool == TOOL_BUCKET)
            {
              pen_scroll_tap(raw, 2);
              raw_barrel_prev = barrel;
              raw_second_prev = second;
              return;
            }
          paper_pan_begin(raw);
          g.paper_pan_pen = 1;
          raw_barrel_prev = barrel;
          raw_second_prev = second;
          return;
        }
      if (g.paper_panning && g.paper_pan_pen)
        {
          /* Movement reports may omit the button usage; keep panning until
           * the pen pointer itself reports its release. */
          paper_pan_move(raw);
          raw_barrel_prev = barrel;
          raw_second_prev = second;
          return;
        }
      {
        POINT pt = { (int) p.x, (int) p.y };
        ScreenToClient(g.overlay, &pt);
        p.x = (double) pt.x + g.vx;
        p.y = (double) pt.y + g.vy;
      }
      if (prepare) paper_prepare_point(&p);
      else paper_map_point(&p);
    }
  if (g.pen_barrel_down)
    {
      g.pen_last = p;
      if (g.pen_barrel_eraser) tools_erase_move(p);
    }
  if (g.pen_second_down && g.right_erase_active)
    tools_erase_move(p);
  if (barrel && !raw_barrel_prev)
    {
      if (!g.pen_barrel_down && !g.pen_second_down)
        {
          dlog("raw pen barrel down");
          g.pen_first_source = 2;
          pen_first_press(p, 2);
        }
    }
  if (second && !raw_second_prev)
    {
      if (!g.pen_second_down && !g.pen_barrel_down)
        {
          dlog("raw pen second down");
          pen_second_begin(p, 2);
        }
    }
  raw_barrel_prev = barrel;
  raw_second_prev = second;
}

static void raw_pen_input(HRAWINPUT hri)
{
  UINT size = 0;
  if (GetRawInputData(hri, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER)) !=
      0)
    return;
  if (size == 0) return;
  BYTE *buf = (BYTE *) malloc(size);
  if (!buf) return;
  UINT got = GetRawInputData(hri, RID_INPUT, buf, &size,
                             sizeof(RAWINPUTHEADER));
  if (got == size)
    {
      RAWINPUT *raw = (RAWINPUT *) buf;
      if (raw->header.dwType == RIM_TYPEHID)
        raw_pen_report(&raw->header, &raw->data.hid);
    }
  free(buf);
}

/* ------------------------------------------------------------------ */
/* overlay window proc                                                */
/* ------------------------------------------------------------------ */
static Pt overlay_point(LPARAM lp)
{
  Pt p;
  p.x = GET_X_LPARAM(lp) + g.vx;
  p.y = GET_Y_LPARAM(lp) + g.vy;
  return p;
}

/* Map a pointer position from the viewport to the annotation coordinates of
 * the page currently shown in the continuous paper view.  A press also
 * activates the page under the pointer, like the Linux build. */
static Pt overlay_point_page(LPARAM lp, int prepare)
{
  Pt p = overlay_point(lp);
  if (g.paper != PAPER_NONE)
    {
      if (prepare) paper_prepare_point(&p);
      else paper_map_point(&p);
    }
  return p;
}

static Pt paper_view_point(Pt p, int prepare)
{
  if (g.paper != PAPER_NONE)
    {
      if (prepare) paper_prepare_point(&p);
      else paper_map_point(&p);
    }
  return p;
}

/* Map a viewport point for a continuous-paper move, splitting a live pen,
 * marker or temporary-eraser stroke when it crosses onto the adjacent page so
 * each part lands on the page it was drawn over. */
static Pt paper_map_move_point(Pt p)
{
  int idx;
  if (g.paper != PAPER_NONE && paper_continuous_active())
    {
      idx = paper_page_at_view(p);
      if (idx != paper_page_index() && (g.pen_drawing || g.middle_erasing))
        {
          int was_pen = g.pen_drawing;
          int was_erase = g.middle_erasing;
          int ctrl = g.ctrl_down, shift = g.shift_down;
          tools_split_page_stroke();
          paper_switch_page_index(idx);
          paper_map_point(&p);
          if (was_pen)
            tools_pointer_down(p, 0, ctrl, shift);
          else if (was_erase)
            tools_erase_start(p);
          return p;
        }
      paper_map_point(&p);
    }
  return p;
}

/* Paper panning: the middle mouse button and stylus side button 1 drag the
 * continuous page strip, like the Linux build.  The coordinates are viewport
 * coordinates, not page coordinates. */
static void paper_pan_begin(Pt p)
{
  if (g.paper == PAPER_NONE) return;
  g.paper_panning = 1;
  paper_pan_started_at = GetTickCount64();
  paper_pan_origin = p;
  paper_pan_moved = 0;
  g.paper_pan_last = p;
  SetCursor(LoadCursorW(NULL, IDC_SIZEALL));
  if (GetCapture() != g.overlay) SetCapture(g.overlay);
  dlog("paper pan begin %.0f,%.0f", p.x, p.y);
}

static void paper_pan_move(Pt p)
{
  int dy;
  if (!g.paper_panning) return;
  if (fabs(p.x - paper_pan_origin.x) >= 12 ||
      fabs(p.y - paper_pan_origin.y) >= 12)
    paper_pan_moved = 1;
  if (g.tool == TOOL_ERASER && !paper_pan_moved)
    return;
  dy = (int) (p.y - g.paper_pan_last.y);
  g.paper_pan_last = p;
  if (dy) dlog("paper pan y=%.0f dy=%d", p.y, dy);
  if (dy) paper_scroll_pixels(-dy);
}

static void paper_pan_end(void)
{
  if (!g.paper_panning) return;
  g.paper_panning = 0;
  g.paper_pan_pen = 0;
  if (GetCapture() == g.overlay) ReleaseCapture();
  paper_pan_timer_started = 0;
  app_update_cursor();
  /* Always paint the final position; the drag itself may have skipped the
   * last frame because of the repaint limiter. */
  app_render_all();
}

static void paper_pan_finish(void)
{
  int step = g.paper_panning && g.paper != PAPER_NONE &&
             g.tool == TOOL_ERASER && !paper_pan_moved &&
             GetTickCount64() - paper_pan_started_at < 500;
  paper_pan_end();
  if (step) step_eraser_width(1);
}

static int paper_drag_started(Pt origin, Pt point)
{
  int dx = (int) fabs(point.x - origin.x);
  int dy = (int) fabs(point.y - origin.y);
  return dx >= GetSystemMetrics(SM_CXDRAG) ||
         dy >= GetSystemMetrics(SM_CYDRAG);
}

static LRESULT CALLBACK overlay_proc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp)
{
  switch (msg)
    {
    case WM_INPUT:
      raw_pen_input((HRAWINPUT) lp);
      return 0;
    case WM_LBUTTONDOWN:
      {
        Pt click = overlay_point(lp);
        if (ui_modal_outside_click(click)) return 0;
      }
      if (ui_is_modal()) return 0;
      /* Another left click while typing accepts the current text and starts a
       * new one.  A pen tip press is normally handled by the pointer path, so
       * ignore its promoted mouse event. */
      if (g.text_editor_active)
        {
          if (is_pen_mouse() && GetTickCount64() < g.suppress_mouse_until)
            return 0;
          text_accept_and_restart(overlay_point_page(lp, 1));
          return 0;
        }
      mouse_reset_pen_state();
      if (g.pen_seen && is_pen_mouse()) return 0;
      if (g.pen_pointer_active || g.pen_eraser_down) return 0;
      /* In Paper mode, a left drag pans only with Cursor or Bucket selected.
       * Bucket filling is deferred until release so a click still fills. */
      if (g.paper != PAPER_NONE &&
          (g.tool == TOOL_NONE || g.tool == TOOL_BUCKET) &&
          !editable_handles_active() && !g.pen_id && !pen_input_recent())
        {
          paper_left_pending = 1;
          paper_left_panning = 0;
          paper_left_view = overlay_point(lp);
          POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
          ClientToScreen(hwnd, &pt);
          paper_left_origin = (Pt) { (double) pt.x, (double) pt.y };
          SetCapture(hwnd);
          return 0;
        }
      /* Clicks usually arrive on the invisible input window.  Making that
       * window the foreground window would add it to the Alt+Tab history, so
       * the first Alt+Tab would only move between the input window and the
       * paper instead of switching to another application.  Keep the real
       * paper window in front and let the input window stay owned by it. */
      if (g.paper != PAPER_NONE)
        SetForegroundWindow(g.overlay ? g.overlay : hwnd);
      dlog("lbuttondown %ld,%ld tool=%d", (long) GET_X_LPARAM(lp),
           (long) GET_Y_LPARAM(lp), (int) g.tool);
      SetCapture(hwnd);
      g.button_down = 1;
      tools_pointer_down(overlay_point_page(lp, 1), 0,
                         (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                         (GetKeyState(VK_SHIFT) & 0x8000) != 0);
      return 0;
    case WM_LBUTTONDBLCLK:
      if (ui_modal_outside_click(overlay_point(lp))) return 0;
      if (ui_is_modal()) return 0;
      if (g.pen_seen && is_pen_mouse()) return 0;
      if (g.pen_pointer_active || g.pen_eraser_down) return 0;
      if (g.text_editor_active) break;
      tools_double_click(overlay_point_page(lp, 1));
      return 0;
    case WM_MOUSEMOVE:
      {
        if (ui_is_modal()) return 0;
        if (paper_left_pending && (wp & MK_LBUTTON))
          {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ClientToScreen(hwnd, &pt);
            Pt mp = { (double) pt.x, (double) pt.y };
            if (paper_drag_started(paper_left_origin, mp))
              {
                paper_left_pending = 0;
                paper_left_panning = 1;
                paper_pan_begin(paper_left_origin);
                paper_pan_move(mp);
              }
            return 0;
          }
        if (paper_middle_pending && (wp & MK_MBUTTON))
          {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ClientToScreen(hwnd, &pt);
            Pt mp = { (double) pt.x, (double) pt.y };
            if (paper_drag_started(paper_middle_origin, mp))
              {
                paper_middle_pending = 0;
                paper_pan_middle_prev = 1;
                paper_pan_begin(paper_middle_origin);
                paper_pan_move(mp);
              }
            return 0;
          }
        if (g.paper_panning)
          {
            /* Promoted pen movement and real mouse movement both pan.  Use
             * screen coordinates for every pan source so mixed pointer/mouse
             * updates do not introduce a jump. */
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ClientToScreen(hwnd, &pt);
            Pt mp = { (double) pt.x, (double) pt.y };
            paper_pan_move(mp);
            return 0;
          }
        Pt p = paper_map_move_point(overlay_point(lp));
        if (g.middle_erasing)
          {
            /* Middle-button / stylus side-button eraser: keep erasing while
             * the button is dragged. */
            tools_erase_move(p);
            g.last = p;
            return 0;
          }
        if (g.right_erase_active)
          {
            tools_erase_move(p);
            g.last = p;
            return 0;
          }
        /* A pen first-side press may arrive as a middle-button message with
         * no matching pointer updates; keep its erase/timer position live. */
        if (g.pen_barrel_down)
          {
            g.pen_last = p;
            if (g.pen_barrel_eraser) tools_erase_move(p);
            g.last = p;
            return 0;
          }
        if (g.pen_seen && is_pen_mouse()) return 0;
        if (g.pen_pointer_active || g.pen_eraser_down) return 0;
        int pen_recent = GetTickCount64() < g.suppress_mouse_until;
        if (g.button_down)
          tools_pointer_move(p,
                             (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                             (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        else if (g.edit == EDIT_SPLINE && !g.pen_id && !pen_recent)
          tools_spline_hover(p);
        else if (g.edit == EDIT_TRIANGLE && g.tri_placing && !g.pen_id &&
                 !pen_recent)
          tools_triangle_hover(p);
        g.last = p;
      }
      return 0;
    case WM_LBUTTONUP:
      if (paper_left_panning)
        {
          paper_left_panning = 0;
          paper_pan_end();
          return 0;
        }
      if (paper_left_pending)
        {
          paper_left_pending = 0;
          if (GetCapture() == hwnd) ReleaseCapture();
          if (g.tool == TOOL_BUCKET)
            {
              Pt p = paper_view_point(paper_left_view, 1);
              g.button_down = 1;
              tools_pointer_down(p, 0,
                                 (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                                 (GetKeyState(VK_SHIFT) & 0x8000) != 0);
              g.button_down = 0;
              tools_pointer_up(p,
                               (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                               (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            }
          return 0;
        }
      if (g.pen_seen && is_pen_mouse()) return 0;
      if (g.pen_pointer_active || g.pen_eraser_down) return 0;
      if (g.button_down)
        {
          dlog("lbuttonup tool=%d edit=%d", (int) g.tool, (int) g.edit);
          g.button_down = 0;
          tools_pointer_up(overlay_point_page(lp, 0),
                           (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                           (GetKeyState(VK_SHIFT) & 0x8000) != 0);
          ReleaseCapture();
        }
      return 0;
    case WM_CAPTURECHANGED:
      paper_left_pending = 0;
      paper_middle_pending = 0;
      if (g.paper_panning) paper_pan_end();
      return 0;
    case WM_RBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
      if (ui_modal_outside_click(overlay_point(lp))) return 0;
      if (ui_is_modal()) return 0;
      if (!(g.paper != PAPER_NONE && g.tool == TOOL_ERASER &&
            pen_input_recent()))
        mouse_reset_pen_state();
      if (g.pen_second_down) return 0;
      if (g.pen_barrel_down) return 0;   /* promoted first-side event */
      if (is_pen_mouse() && pen_second_recent()) return 0;
      if (pen_now_first_side() &&
          !g.pen_second_down && !pen_second_recent() &&
          editable_handles_active())
        {
          /* A button 1 press (pen side button) recolours the text being
           * typed or the object whose handles are visible.  It must not
           * accept the text or open the right-click action. */
          pen_scroll_tap(overlay_point(lp), 2);
          return 0;
        }
      if (g.text_editor_active)
        {
          g.ignore_next_right_up = 1;
          PostMessageW(g.msgwin, WM_COMMIT_TEXT, 1, 0);
          return 0;
        }
      {
        int pen = is_pen_mouse() ||
                  (g.paper != PAPER_NONE && g.tool == TOOL_ERASER &&
                   pen_input_recent());
        dlog("right down pen=%d seen=%d second=%d barrel=%d", pen,
             g.pen_seen, g.pen_second_down, g.pen_barrel_down);
        if (pen && (g.pen_barrel_down || g.pen_eraser_down ||
                    GetTickCount64() - g.pen_barrel_release < 400 ||
                    GetTickCount64() - g.pen_eraser_release < 400))
          return 0;
        g.menu_hold_is_stylus = pen;
        if (pen) dlog("pen second side down");
        app_start_right_press(overlay_point_page(lp, 1));
      }
      return 0;
    case WM_RBUTTONUP:
      if (is_pen_mouse() && pen_second_recent()) return 0;
      if (g.ignore_next_right_up)
        {
          g.ignore_next_right_up = 0;
          return 0;
        }
      if (g.pen_second_down) return 0;
      if (g.pen_barrel_down)
        {
          /* A stylus side-button release, with or without the pen signature:
           * finish the held press (colour step / eraser), never the
           * right-click action. */
          pen_barrel_end();
          return 0;
        }
      if (is_pen_mouse() && (g.pen_eraser_down ||
                             GetTickCount64() - g.pen_barrel_release < 400 ||
                             GetTickCount64() - g.pen_eraser_release < 400))
        {
          if (g.right_timer_active)
            {
              KillTimer(g.msgwin, TIMER_LONG_PRESS);
              g.right_timer_active = 0;
            }
          g.right_short_done = 1;
          g.right_menu_open = 0;
          return 0;
        }
      right_button_up(overlay_point_page(lp, 0));
      return 0;
    case WM_MBUTTONDOWN:
      if (ui_modal_outside_click(overlay_point(lp))) return 0;
      if (ui_is_modal()) return 0;
      /* Bucket middle-click changes fill color. Defer that click in Paper
       * mode so a middle drag can pan without changing the color. */
      if (g.paper != PAPER_NONE && g.tool == TOOL_BUCKET &&
          !editable_handles_active() && !g.pen_id && !is_pen_mouse() &&
          !pen_input_recent())
        {
          paper_middle_pending = 1;
          paper_middle_view = overlay_point(lp);
          POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
          ClientToScreen(hwnd, &pt);
          paper_middle_origin = (Pt) { (double) pt.x, (double) pt.y };
          SetCapture(hwnd);
          return 0;
        }
      if (g.tool == TOOL_BUCKET && !editable_handles_active())
        {
          pen_scroll_tap(overlay_point_page(lp, 1), 1);
          return 0;
        }
      /* Middle click accepts typed text, then finishes visible shape/text
       * handle editing on the next press.  It must not recolour, pan, or
       * start erasing while an editable item is active. */
      if (editable_handles_active())
        {
          /* Middle click changes color and keeps text/shape editing active;
           * real mouse and stylus Button 1 use the same action here. */
          dlog("overlay middle pen=%d recent=%d first=%d barrel=%d scroll=%d "
               "second=%d edit=%d text=%d",
               is_pen_mouse(), pen_input_recent(), pen_now_first_side(),
               g.pen_barrel_down, pen_first_scroll_only, g.pen_second_down,
               g.edit, g.text_editor_active);
          if (g.pen_barrel_down && pen_first_scroll_only) return 0;
          pen_scroll_tap(overlay_point_page(lp, 1), 1);
          return 0;
        }
      /* Middle click is not an eraser shortcut while the Pen tool is active. */
      if (g.paper == PAPER_NONE && g.tool == TOOL_PEN) return 0;
      if (g.paper != PAPER_NONE)
        {
          POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
          ClientToScreen(hwnd, &pt);
          Pt rp = { (double) pt.x, (double) pt.y };
          if (!g.paper_panning)
            {
              paper_pan_begin(rp);
              g.paper_pan_pen = 0;
            }
          return 0;
        }
      if (shape_or_text_tool(g.tool))
        {
          /* After editing closes, a middle click is ignored for shape and
           * text tools; it must neither recolor nor erase. */
          return 0;
        }
      dlog("mbutton down pen=%d seen=%d", is_pen_mouse(), g.pen_seen);
      if (is_pen_mouse() || pen_input_recent())
        {
          dlog("pen middle -> temporary eraser");
          SetCapture(hwnd);
          pen_first_press(overlay_point_page(lp, 1), 1);
          return 0;
        }
      if (g.pen_pointer_active || g.pen_eraser_down) return 0;
      SetCapture(hwnd);
      tools_erase_start(overlay_point_page(lp, 1));
      return 0;
    case WM_MBUTTONUP:
      dlog("mbutton up pen=%d seen=%d barrel=%d", is_pen_mouse(),
           g.pen_seen, g.pen_barrel_down);
      if (g.paper_panning)
        {
          paper_pan_finish();
          return 0;
        }
      if (paper_middle_pending)
        {
          paper_middle_pending = 0;
          if (GetCapture() == hwnd) ReleaseCapture();
          if (g.tool == TOOL_BUCKET && g.paper != PAPER_NONE)
            {
              Pt p = paper_view_point(paper_middle_view, 1);
              pen_scroll_tap(p, 1);
              pen_barrel_end();
            }
          return 0;
        }
      if (g.middle_erasing)
        {
          tools_erase_end();
          if (GetCapture() == hwnd) ReleaseCapture();
          return 0;
        }
      if (g.pen_barrel_down)
        {
          pen_barrel_end();
          app_raise_icon();
          return 0;
        }
      if (g.pen_pointer_active || g.pen_eraser_down) return 0;
      if (g.button_down)
        {
          g.button_down = 0;
          tools_pointer_up(overlay_point_page(lp, 0), 0, 0);
          ReleaseCapture();
        }
      return 0;
    case WM_XBUTTONDOWN:
      if (ui_modal_outside_click(overlay_point(lp))) return TRUE;
      if (ui_is_modal()) return TRUE;
      if (g.text_editor_active)
        {
          if (GET_XBUTTON_WPARAM(wp) == XBUTTON2)
            {
              g.ignore_next_right_up = 1;
              PostMessageW(g.msgwin, WM_COMMIT_TEXT, 1, 0);
            }
          else stylus_step(1, 1);
          return TRUE;
        }
      if (GET_XBUTTON_WPARAM(wp) != XBUTTON2 &&
          (editable_handles_active() || g.tool == TOOL_BUCKET))
        {
          /* Button 1 recolours the text/object instead of opening the
           * right-click action, with or without a pen signature. */
          pen_scroll_tap(overlay_point_page(lp, 1), 1);
          return TRUE;
        }
      if (g.pen_barrel_down) return TRUE;
      if (!is_pen_mouse() && !pen_input_recent()) return TRUE;
      dlog("xbutton down %u pen=%d seen=%d",
           (unsigned) GET_XBUTTON_WPARAM(wp), is_pen_mouse(), g.pen_seen);
      g.menu_hold_is_stylus = 1;
      app_start_right_press(overlay_point_page(lp, 1));
      return TRUE;
    case WM_XBUTTONUP:
      if (!is_pen_mouse() && !pen_input_recent()) return TRUE;
      if (g.pen_barrel_down)
        {
          pen_barrel_end();
          return TRUE;
        }
      right_button_up(overlay_point_page(lp, 0));
      return TRUE;
    case WM_POINTERENTER:
      {
        UINT32 id = GET_POINTERID_WPARAM(wp);
        POINTER_INFO pi;
        if (GetPointerInfo(id, &pi) && pi.pointerType == PT_PEN)
          {
            g.pen_id = id;
            g.pen_seen = 1;
            SetTimer(g.msgwin, TIMER_PEN_POLL, 40, NULL);
          }
      }
      return 0;
    case WM_POINTERDOWN:
      if (ui_is_modal())
        {
          UINT32 id = GET_POINTERID_WPARAM(wp);
          POINTER_INFO pi;
          if (GetPointerInfo(id, &pi) && pi.pointerType == PT_PEN)
            {
              Pt p = { (double) pi.ptPixelLocation.x,
                       (double) pi.ptPixelLocation.y };
              ui_modal_outside_click(p);
            }
          return 0;
        }
      if (!ui_is_modal())
        pen_pointer_event(hwnd, msg, wp);
      return 0;
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
      if (!ui_is_modal())
        pen_pointer_event(hwnd, msg, wp);
      return 0;
    case WM_POINTERCAPTURECHANGED:
      pen_cancel();
      return 0;
    case WM_POINTERLEAVE:
      if (g.pen_barrel_down || g.pen_eraser_down || g.pen_second_down)
        pen_cancel();
      /* Do not leave a rubber-band line to the last in-range point. */
      if (g.edit == EDIT_SPLINE && g.spline_preview_on && g.nnodes > 0)
        {
          g.spline_cursor = g.nodes[g.nnodes - 1].p;
          app_request_render();
        }
      g.pen_id = 0;
      raw_barrel_prev = 0;
      raw_second_prev = 0;
      raw_tip_prev = 0;
      KillTimer(g.msgwin, TIMER_PEN_POLL);
      return 0;
    case WM_MOUSEWHEEL:
      {
        int dir = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
        if (ui_is_modal()) return 0;
        /* While the text editor or shape handles are visible the wheel
         * recolours the object instead of scrolling the paper. */
        if (editable_handles_active())
          {
            stylus_step(dir, 1);
            return 0;
          }
        if (g.tool == TOOL_BUCKET)
          {
            step_fill_color(dir);
            return 0;
          }
        if (g.paper != PAPER_NONE && paper_continuous_active())
          {
            paper_scroll_step(-dir);
            return 0;
          }
        if (g.paper == PAPER_NONE && g.tool == TOOL_ERASER)
          {
            step_eraser_width(dir);
            return 0;
          }
        tools_wheel(GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
      }
    case WM_KEYDOWN:
      if (g.paper != PAPER_NONE)
        {
          UINT mods = 0;
          if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
          if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
          if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
          paper_shortcut_dispatch((UINT) wp, mods);
          return 0;
        }
      break;
    case WM_SIZE:
      if (g.paper != PAPER_NONE && g.paper_windowed && !g.shutting_down)
        {
          int vw = app_view_w(), vh = app_view_h();
          if (vw != g.dibw || vh != g.dibh)
            {
              tools_commit(0);
              paper_sync_current();
              ui_resize_canvas_for_doc();
              paper_scroll_reclamp();
            }
          return 0;
        }
      break;
    case WM_GETMINMAXINFO:
      if (g.paper_windowed)
        {
          MINMAXINFO *mmi = (MINMAXINFO *) lp;
          mmi->ptMinTrackSize.x = 320;
          mmi->ptMinTrackSize.y = 240;
          return 0;
        }
      break;
    case WM_ACTIVATE:
      /* While the paper is active it is topmost (so stylus side-button input
       * arrives live); when the user switches to another application it drops
       * to non-topmost so that application can cover it.  Coming back also
       * raises the input window. */
      if (g.paper != PAPER_NONE)
        {
          app_paper_on_top = (LOWORD(wp) != WA_INACTIVE);
          app_paper_zorder();
          if (app_paper_on_top && !g.hide_drawings)
            app_set_click_through(0);
        }
      break;
    case WM_SYSKEYDOWN:
      /* Alt+F4 arrives as WM_SYSKEYDOWN, not WM_KEYDOWN.  While the screen
       * paper owns the keyboard focus, handle it here as well as through the
       * registered hotkey. */
      if (g.paper != PAPER_NONE)
        {
          UINT mods = MOD_ALT;
          if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
          if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
          if (paper_shortcut_dispatch((UINT) wp, mods)) return 0;
        }
      break;
    case WM_CONTEXTMENU:
      return 0;
    case WM_CLOSE:
      /* Alt+F4 or a window close must go through the save prompt instead of
       * destroying the overlay and skipping the settings save. */
      if (!g.shutting_down) app_save_and_quit();
      return 0;
    case WM_SYSCOMMAND:
      if ((wp & 0xFFF0u) == SC_CLOSE)
        {
          if (!g.shutting_down) app_save_and_quit();
          return 0;
        }
      break;
    case WM_SETCURSOR:
      SetCursor(tool_cursor());
      return TRUE;
    case WM_DISPLAYCHANGE:
      app_clear_menu_ghost();
      app_cancel_timer();
      if (g.tool != TOOL_NONE) tools_exit();
      ui_rebuild_surfaces();
      app_update_cursor();
      ui_refresh_tray();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
      if (paper_normal_mode)
        {
          PAINTSTRUCT ps;
          HDC dc = BeginPaint(hwnd, &ps);
          if (g.dibdc)
            BitBlt(dc, 0, 0, g.vw, app_paper_view_height(), g.dibdc, 0, 0,
                   SRCCOPY);
          EndPaint(hwnd, &ps);
          return 0;
        }
      break;
    }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* icon window proc                                                   */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK icon_proc(HWND hwnd, UINT msg, WPARAM wp,
                                  LPARAM lp)
{
  switch (msg)
    {
    case WM_NCHITTEST:
      {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &p);
        int size;
        icon_geometry(&size);
        double c = size / 2.0;
        double r = g.icon_size / 2.0 + ICON_RING_EXTRA * icon_ring_scale();
        if (hypot(p.x - c, p.y - c) > r + 2) return HTTRANSPARENT;
        return HTCLIENT;
      }
    case WM_LBUTTONDOWN:
      {
        SetCapture(hwnd);
        g.icon_drag_started = 0;
        POINT p;
        GetCursorPos(&p);
        g.icon_press.x = p.x;
        g.icon_press.y = p.y;
        RECT wr;
        GetWindowRect(hwnd, &wr);
        g.icon_drag_pos = wr;
      }
      return 0;
    case WM_MOUSEMOVE:
      if (GetCapture() == hwnd)
        {
          POINT p;
          GetCursorPos(&p);
          int dx = p.x - g.icon_press.x;
          int dy = p.y - g.icon_press.y;
          if (!g.icon_drag_started &&
              (abs(dx) > 4 || abs(dy) > 4))
            g.icon_drag_started = 1;
          if (g.icon_drag_started)
            SetWindowPos(hwnd, HWND_TOPMOST,
                         g.icon_drag_pos.left + dx,
                         g.icon_drag_pos.top + dy, 0, 0,
                         SWP_NOSIZE | SWP_NOACTIVATE);
        }
      return 0;
    case WM_LBUTTONUP:
      if (GetCapture() == hwnd) ReleaseCapture();
      if (!g.icon_drag_started)
        {
          POINT p;
          GetCursorPos(&p);
          ui_show_menu(p.x, p.y);
        }
      g.icon_drag_started = 0;
      return 0;
    case WM_RBUTTONUP:
      {
        POINT p;
        GetCursorPos(&p);
        ui_show_menu(p.x, p.y);
      }
      return 0;
    case WM_MOUSEWHEEL:
      {
        int dir = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
        if (editable_handles_active())
          {
            stylus_step(dir, 1);
            return 0;
          }
        if (g.tool == TOOL_BUCKET)
          {
            step_fill_color(dir);
            return 0;
          }
        if (g.paper != PAPER_NONE && paper_continuous_active())
          {
            paper_scroll_step(-dir);
            return 0;
          }
        tools_wheel(GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
      }
    case WM_SETCURSOR:
      SetCursor(LoadCursorW(NULL, IDC_HAND));
      return TRUE;
    case WM_ERASEBKGND:
      return 1;
    }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* message window proc                                                */
/* ------------------------------------------------------------------ */
static void reassert_topmost(void)
{
  if (ui_is_modal() || g.menu_open || g.text_editor_active) return;
  if (g.tool == TOOL_NONE && g.paper == PAPER_NONE) return;
  /* In Paper mode the paper is topmost only while it is active.  Re-assert
   * the z-order so no other topmost window stays in front of it, and do not
   * re-raise the icon on every tick. */
  if (g.paper != PAPER_NONE)
    {
      app_paper_zorder();
      if (g.show_icon && g.iconwin && !IsWindowVisible(g.iconwin))
        app_raise_icon();
      return;
    }
  SetWindowPos(g.overlay, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  if (g.inputwin && IsWindowVisible(g.inputwin))
    SetWindowPos(g.inputwin, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  app_raise_icon();
}

static LRESULT CALLBACK msg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
    {
    case WM_PAPER_SHORTCUT:
      paper_shortcut_dispatch((UINT) wp, (UINT) lp);
      return 0;
    case WM_COPYDATA:
      {
        COPYDATASTRUCT *cd = (COPYDATASTRUCT *) lp;
        if (cd && cd->dwData == 1 && cd->lpData &&
            cd->cbData >= sizeof(wchar_t))
          {
            wchar_t path[MAX_PATH];
            DWORD chars = cd->cbData / sizeof(wchar_t);
            if (chars >= MAX_PATH) chars = MAX_PATH - 1;
            memcpy(path, cd->lpData, (size_t) chars * sizeof(wchar_t));
            path[chars] = 0;
            app_open_pdf_file(path);
          }
      }
      return TRUE;
    case WM_TIMER:
      if (wp == TIMER_COUNTDOWN)
        {
          KillTimer(hwnd, TIMER_COUNTDOWN);
          g.timer_running = 0;
          dlog("timer done -> pen");
          /* Keep an image of an open context menu, like Screenator 7.0: the
           * snapshot stays as the drawing background after the menu closes,
           * so the menu does not swallow the first drawing click. */
          {
            int external_menu = 0;
            menu_capture_screen();
            if (g.menu_capture && g.menu_open)
              dlog("timer: captured own menu before pen");
            else if (g.menu_capture)
              {
                g.menu_capture_kind = 3;
                g.menu_capture_marker = 0;
                external_menu = dismiss_external_menus();
                dlog("timer: captured external menu before pen");
              }
            if (g.menu_open) EndMenu();
            tools_activate(TOOL_PEN);
            if (external_menu) app_menu_capture_stop(1);
          }
        }
      else if (wp == TIMER_TOOL_DELAY)
        {
          KillTimer(hwnd, TIMER_TOOL_DELAY);
          if (g.pending_tool != TOOL_NONE)
            {
              /* Take the last menu snapshot just before the delayed tool is
               * activated.  The tool can then activate while the native menu
               * is still open, and the snapshot becomes the ghost menu when
               * the menu closes. */
              dlog("delay: timer fired menu_open=%d capture=%d",
                   g.menu_open, g.menu_capture != NULL);
              /* Take the snapshot exactly once, immediately before
               * activation, so it includes a submenu that is open then.
               * Our own menu stays open while activation is deferred; a menu
               * owned by another application (File Explorer and so on) is
               * closed first and replaced by the snapshot. */
              int external_menu = 0;
              menu_capture_screen();
              if (g.menu_capture && g.menu_open)
                dlog("delay: captured menu before tool activation");
              else if (g.menu_capture)
                {
                  g.menu_capture_kind = 3;
                  g.menu_capture_marker = 0;
                  external_menu = dismiss_external_menus();
                  dlog("delay: captured external menu before tool activation");
                }
              Tool t = g.pending_tool;
              g.pending_tool = TOOL_NONE;
              g.tool_delay_deferred = 0;
              tools_activate(t);
              if (external_menu)
                app_menu_capture_stop(1);
            }
        }
      else if (wp == TIMER_LONG_PRESS)
        {
          KillTimer(hwnd, TIMER_LONG_PRESS);
          g.right_timer_active = 0;
          if (!g.right_short_done)
            {
              g.right_menu_open = 1;
              g.right_short_done = 1;
              if (g.tool == TOOL_MARKER)
                ui_show_wheel_menu(1);
              else if (g.menu_hold_is_stylus && g.tool == TOOL_ERASER)
                {
                  /* No menu with the Eraser tool. */
                  g.right_menu_open = 0;
                  g.right_short_done = 0;
                }
              else if (g.menu_hold_is_stylus)
                ui_show_stylus_menu();
              else
                ui_show_wheel_menu(0);
              /* The menu runs its own modal loop, so the button release may
               * never reach us.  Clear the held state here, otherwise the
               * stylus stays blocked after the menu closes. */
              if (g.pen_second_down)
                {
                  g.pen_second_down = 0;
                  ReleaseCapture();
                }
              g.right_menu_open = 0;
              g.right_timer_active = 0;
              g.right_short_done = 1;
              app_raise_icon();
            }
        }
      else if (wp == TIMER_PAPER_PAN)
        {
          int down = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
          HWND fg;
          int ours;
          if (g.paper == PAPER_NONE)
            {
              KillTimer(hwnd, TIMER_PAPER_PAN);
              paper_pan_middle_prev = 0;
              paper_pan_timer_started = 0;
              return 0;
            }
          if (g.tool == TOOL_BUCKET && g.pen_barrel_down &&
              pen_first_scroll_only)
            {
              paper_pan_middle_prev = down;
              return 0;
            }
          if (paper_middle_pending)
            {
              paper_pan_middle_prev = down;
              return 0;
            }
          if (g.paper_panning)
            {
              /* A pan started by the pen pointer/raw path is kept alive and
               * moved from the live cursor; only a pan started here is ended
               * by the middle button itself. */
              if (paper_pan_timer_started && !down)
                {
                  paper_pan_middle_prev = 0;
                  paper_pan_finish();
                  return 0;
                }
              if (ui_is_modal() || g.menu_open || g.text_editor_active)
                {
                  paper_pan_middle_prev = down;
                  paper_pan_end();
                  return 0;
                }
              {
                POINT pt;
                GetCursorPos(&pt);
                Pt p = { (double) pt.x, (double) pt.y };
                paper_pan_move(p);
              }
              paper_pan_middle_prev = down;
              return 0;
            }
          fg = GetForegroundWindow();
          ours = fg == g.overlay || fg == g.inputwin || fg == g.iconwin ||
                 (g.textwin && fg == g.textwin);
          if (down && !paper_pan_middle_prev && ours &&
              !editable_handles_active() && !ui_is_modal() && !g.menu_open &&
              !g.text_editor_active)
            {
              POINT pt;
              GetCursorPos(&pt);
              Pt p = { (double) pt.x, (double) pt.y };
              paper_pan_begin(p);
              g.paper_pan_pen = 0;
              paper_pan_timer_started = 1;
            }
          paper_pan_middle_prev = down;
          return 0;
        }
      else if (wp == TIMER_STYLUS_HOLD)
        {
          KillTimer(hwnd, TIMER_STYLUS_HOLD);
          if (g.pen_barrel_down && !g.pen_barrel_eraser)
            {
              g.pen_barrel_eraser = 1;
              tools_erase_start(g.pen_last);
              dlog("pen barrel long -> eraser");
            }
        }
      else if (wp == TIMER_PEN_POLL)
        {
          KillTimer(hwnd, TIMER_PEN_POLL);
          if (g.tool != TOOL_NONE || g.paper != PAPER_NONE)
            {
              int active = 1;
              if (!ui_is_modal() && !g.menu_open && !g.text_editor_active)
                active = pen_poll_state();
              /* Keep polling through menus and dialogs so the next side
               * button press is still seen when they close. */
              if (active)
                SetTimer(hwnd, TIMER_PEN_POLL, 40, NULL);
            }
        }
      else if (wp == TIMER_PRESENT)
        {
          KillTimer(hwnd, TIMER_PRESENT);
          g.render_pending = 0;
          g.last_present = GetTickCount64();
          app_render_all();
        }
      else if (wp == TIMER_HINTS)
        {
          KillTimer(hwnd, TIMER_HINTS);
          ui_show_hints(1);
        }
      else if (wp == 5)
        {
          KillTimer(hwnd, 5);
          app_save_and_quit();
        }
      else if (wp == 6)
        {
          reassert_topmost();
        }
      else if (wp == TIMER_CARET)
        {
          if (g.text_editor_active)
            {
              g.text_caret_visible = !g.text_caret_visible;
              app_render_all();
            }
          else
            KillTimer(hwnd, TIMER_CARET);
        }
      return 0;
    case WM_HOTKEY:
      {
        int id = (int) wp;
        dlog("hotkey id=%d tool=%d", id, (int) g.tool);
        if (id == HK_ICON)
          {
            ui_set_icon_visible(!g.show_icon);
          }
        else if (id == HK_PEN)
          {
            if (g.tool != TOOL_NONE || g.paper != PAPER_NONE)
              tools_activate(TOOL_PEN);
            else tools_request(TOOL_PEN);
          }
        else if (id == HK_TEXT)
          {
            if (g.tool != TOOL_NONE || g.paper != PAPER_NONE)
              tools_activate(TOOL_TEXT);
            else tools_request(TOOL_TEXT);
          }
        else if (id == HK_ARROW)
          {
            if (g.tool != TOOL_NONE || g.paper != PAPER_NONE)
              tools_activate(TOOL_ARROW);
            else tools_request(TOOL_ARROW);
          }
        else if (id == HK_QUICK)
          {
            ui_quick_menu();
          }
        else if (id == HK_TOOLBAR)
          {
            toolbar_toggle();
          }
        else if (id == HK_SWITCH)
          {
            ui_execute_command(CMD_SWITCH_MODE);
          }
        else if (id == HK_SHOT)
          {
            app_take_screenshot();
          }
        else if (id == HK_TIMER)
          {
            app_start_timer();
          }
        else if (id == HK_ESC)
          {
            tools_key(VK_ESCAPE, 0, 0);
          }
        else if (id == HK_UNDO)
          {
            tools_undo();
          }
        else if (id == HK_REDO)
          {
            tools_redo();
          }
        else if (id == HK_PAPER_Q)
          {
            app_exit_paper();
          }
        else if (id == HK_PAPER_T)
          {
            if (g.paper == PAPER_NONE) ui_toggle_visibility();
          }
        else if (id == HK_QUIT)
          {
            app_save_and_quit();
          }
      }
      return 0;
    case WM_TRAY:
      {
        WORD ev = LOWORD(lp);
        if (ev == WM_LBUTTONUP || ev == WM_RBUTTONUP ||
            ev == WM_CONTEXTMENU)
          {
            POINT p;
            GetCursorPos(&p);
            ui_show_menu(p.x, p.y);
          }
        else if (ev == WM_LBUTTONDBLCLK)
          {
            POINT p;
            GetCursorPos(&p);
            ui_show_menu(p.x, p.y);
          }
      }
      return 0;
    case WM_START_HINTS:
      if (!g.hide_hints) ui_show_hints(1);
      return 0;
    case WM_COMMIT_TEXT:
      dlog("WM_COMMIT_TEXT %d", (int) wp);
      app_end_text(wp ? 1 : 0);
      return 0;
    case WM_START_TEXT:
      app_end_text(1);
      if (text_restart_pending)
        {
          text_restart_pending = 0;
          if (g.tool == TOOL_TEXT)
            {
              tools_commit(0);
              app_begin_text(text_restart_point);
            }
        }
      return 0;
    case WM_START_ERASE:
      if (!erase_start_pending) return 0;
      erase_start_pending = 0;
      app_end_text(1);
      tools_commit(0);
      if (!g.middle_erasing)
        {
          SetCapture(g.overlay);
          tools_erase_start(erase_start_point);
        }
      dlog("text middle click -> erase");
      return 0;
    case WM_REFOCUS_TEXT:
      if (g.text_editor_active && g.textwin && IsWindow(g.textwin))
        {
          SetFocus(g.textwin);
          app_hotkeys_refresh();
        }
      return 0;
    case WM_DISPLAYCHANGE:
      app_clear_menu_ghost();
      app_cancel_timer();
      if (g.tool != TOOL_NONE) tools_exit();
      ui_rebuild_surfaces();
      app_update_cursor();
      ui_refresh_tray();
      return 0;
    case WM_SETTINGCHANGE:
      if (wp == SPI_SETWORKAREA)
        {
          if (g.show_icon) ui_position_icon();
        }
      return 0;
    }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* quit                                                               */
/* ------------------------------------------------------------------ */
static int count_alpha(cairo_surface_t *s);

void app_save_and_quit(void)
{
  if (g.shutting_down) return;

  ui_prepare_dialog();

  /* Paper exports are explicit files; screen annotations follow the saved
   * Save on exit setting without an extra confirmation. */
  if (!g.smoke)
    {
      if (!paper_confirm_save_all()) return;
    }

  g.shutting_down = 1;
  if (g.text_editor_active) app_end_text(1);
  if (g.tool != TOOL_NONE) tools_exit();
  else tools_commit(0);
  app_remove_wheel_hook();
  if (!g.smoke)
    {
      if (g.save_on_exit) annotations_save();
      else annotations_remove();
      config_save();
    }
  unregister_managed_hotkeys();
  if (g.tray_added)
    {
      Shell_NotifyIconW(NIM_DELETE, &g.nid);
      g.tray_added = 0;
    }
  PostQuitMessage(0);
}

void app_status(const char *text)
{
  (void) text;
}

void app_show_menu_at_icon(void)
{
  RECT r;
  GetWindowRect(g.iconwin, &r);
  ui_show_menu(r.left + 8, r.bottom + 4);
}

/* ------------------------------------------------------------------ */
/* startup                                                            */
/* ------------------------------------------------------------------ */
static void set_dpi_aware(void)
{
  HMODULE user = GetModuleHandleW(L"user32.dll");
  if (user)
    {
      typedef BOOL (WINAPI * SetCtx)(HANDLE);
      SetCtx fn = (SetCtx)(void *) GetProcAddress(user,
                                                 "SetProcessDpiAwarenessContext");
      if (fn && fn((HANDLE) -4)) return;   /* PER_MONITOR_AWARE_V2 */
    }
  SetProcessDPIAware();
}

static void register_classes(void)
{
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.hInstance = g.hinst;
  wc.lpfnWndProc = overlay_proc;
  wc.lpszClassName = OVERLAY_CLASS;
  wc.hCursor = NULL;
  wc.style = CS_DBLCLKS;
  RegisterClassExW(&wc);
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.hInstance = g.hinst;
  wc.lpfnWndProc = icon_proc;
  wc.lpszClassName = ICON_CLASS;
  wc.hCursor = LoadCursorW(NULL, IDC_HAND);
  RegisterClassExW(&wc);
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.hInstance = g.hinst;
  wc.lpfnWndProc = msg_proc;
  wc.lpszClassName = MSG_CLASS;
  RegisterClassExW(&wc);
}

static void load_icons(void)
{
  wchar_t path[MAX_PATH];
  app_asset_path(path, MAX_PATH, L"icon.ico");
  g.hicon_normal = (HICON) LoadImageW(NULL, path, IMAGE_ICON, 32, 32,
                                      LR_LOADFROMFILE);
  g.hicon_active = g.hicon_normal;
  if (!g.hicon_normal)
    g.hicon_normal = LoadIconW(NULL, IDI_APPLICATION);
  asset_icon_normal = load_png_asset(L"icon-normal.png");
  asset_icon_active = load_png_asset(L"icon-active.png");
  if (!asset_icon_active) asset_icon_active = asset_icon_normal;
  /* A red active tray icon, rendered from the same SVG-derived PNG. */
  if (asset_icon_active)
    {
      int size = 32;
      BITMAPINFO bi;
      memset(&bi, 0, sizeof(bi));
      bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bi.bmiHeader.biWidth = size;
      bi.bmiHeader.biHeight = -size;
      bi.bmiHeader.biPlanes = 1;
      bi.bmiHeader.biBitCount = 32;
      bi.bmiHeader.biCompression = BI_RGB;
      HDC screen = GetDC(NULL);
      HDC dc = CreateCompatibleDC(screen);
      void *bits = NULL;
      HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
      if (bmp && bits)
        {
          SelectObject(dc, bmp);
          cairo_surface_t *surf = cairo_image_surface_create_for_data(
            (unsigned char *) bits, CAIRO_FORMAT_ARGB32, size, size, size * 4);
          cairo_t *cr = cairo_create(surf);
          cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
          cairo_paint(cr);
          cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
          double iw = cairo_image_surface_get_width(asset_icon_active);
          double ih = cairo_image_surface_get_height(asset_icon_active);
          cairo_scale(cr, size / iw, size / ih);
          cairo_set_source_surface(cr, asset_icon_active, 0, 0);
          cairo_paint(cr);
          cairo_surface_flush(surf);
          HBITMAP mask = CreateBitmap(size, size, 1, 1, NULL);
          ICONINFO ii;
          memset(&ii, 0, sizeof(ii));
          ii.fIcon = TRUE;
          ii.hbmMask = mask;
          ii.hbmColor = bmp;
          HICON active = CreateIconIndirect(&ii);
          if (active) g.hicon_active = active;
          DeleteObject(mask);
          cairo_destroy(cr);
          cairo_surface_destroy(surf);
        }
      DeleteObject(bmp);
      DeleteDC(dc);
      ReleaseDC(NULL, screen);
    }
}

static void add_tray(void)
{
  memset(&g.nid, 0, sizeof(g.nid));
  g.nid.cbSize = sizeof(g.nid);
  g.nid.hWnd = g.msgwin;
  g.nid.uID = 1;
  g.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  g.nid.uCallbackMessage = WM_TRAY;
  g.nid.hIcon = g.hicon_normal;
  wcscpy(g.nid.szTip, L"Screenator 8.0");
  if (Shell_NotifyIconW(NIM_ADD, &g.nid))
    g.tray_added = 1;
}

static int shortcut_hotkey_id(ShortcutId id)
{
  switch (id)
    {
    case SK_ARROW: return HK_ARROW;
    case SK_TEXT: return HK_TEXT;
    case SK_PEN: return HK_PEN;
    case SK_ICON: return HK_ICON;
    case SK_TOOLBAR: return HK_TOOLBAR;
    case SK_SWITCH: return HK_SWITCH;
    case SK_SHOT: return HK_SHOT;
    case SK_TIMER: return HK_TIMER;
    case SK_UNDO: return HK_UNDO;
    case SK_REDO: return HK_REDO;
    case SK_PAPER_Q: return HK_PAPER_Q;
    case SK_PAPER_T: return HK_PAPER_T;
    case SK_QUIT: return HK_QUIT;
    default: return 0;
    }
}

static void unregister_managed_hotkeys(void)
{
  static const int ids[] =
    {
      HK_PEN, HK_TEXT, HK_ARROW, HK_ICON, HK_QUICK, HK_ESC, HK_UNDO,
      HK_REDO, HK_PAPER_Q, HK_PAPER_T, HK_QUIT, HK_SWITCH, HK_SHOT,
      HK_TIMER, HK_TOOLBAR
    };
  for (int i = 0; i < (int) (sizeof(ids) / sizeof(ids[0])); i++)
    UnregisterHotKey(g.msgwin, ids[i]);
}

static int register_shortcut(ShortcutId id, char *error, int error_size)
{
  UINT mods = 0, vk = 0;
  if (!shortcut_parse(g.shortcuts[id], &mods, &vk)) return 0;
  if (!vk) return 1;
  if (!RegisterHotKey(g.msgwin, shortcut_hotkey_id(id),
                      mods | MOD_NOREPEAT, vk))
    {
      if (error && error_size > 0)
        snprintf(error, (size_t) error_size,
                 "%s (%s) is already in use or cannot be registered.",
                 g_shortcut_labels[id], g.shortcuts[id]);
      return 0;
    }
  return 1;
}

/* Register the shortcuts that apply to the current mode.  With check set,
 * a registration failure is fatal so the Shortcuts dialog can restore the
 * previous bindings.  Alt+F4 is normally owned by the active window, so a
 * failed RegisterHotKey for Quit is not treated as a configuration error. */
static int register_runtime_shortcuts(int check, char *error, int error_size)
{
  static const ShortcutId always[] =
    {
      SK_PEN, SK_TEXT, SK_ARROW, SK_ICON, SK_TOOLBAR, SK_SWITCH,
      SK_SHOT
    };
  for (int i = 0; i < (int) (sizeof(always) / sizeof(always[0])); i++)
    if (!register_shortcut(always[i], error, error_size) && check)
      return 0;
  RegisterHotKey(g.msgwin, HK_QUICK, MOD_CONTROL | MOD_NOREPEAT, VK_F10);
  if (g.text_editor_active) return 1;
  if (g.tool != TOOL_NONE)
    RegisterHotKey(g.msgwin, HK_ESC, MOD_NOREPEAT, VK_ESCAPE);
  if (g.tool != TOOL_NONE || g.paper != PAPER_NONE)
    {
      if (!register_shortcut(SK_UNDO, error, error_size) && check) return 0;
      if (!register_shortcut(SK_REDO, error, error_size) && check) return 0;
    }
  if (g.paper != PAPER_NONE)
    {
      if (!register_shortcut(SK_PAPER_Q, error, error_size) && check)
        return 0;
      register_shortcut(SK_QUIT, NULL, 0);
    }
  else if (!register_shortcut(SK_PAPER_T, error, error_size) && check)
    return 0;
  return 1;
}

void app_hotkeys_refresh(void)
{
  if (!g.msgwin) return;
  unregister_managed_hotkeys();
  register_runtime_shortcuts(0, NULL, 0);
}

int app_hotkeys_apply(const char new_shortcuts[SK_COUNT][SHORTCUT_TEXT_MAX],
                      char *error, int error_size)
{
  char old[SK_COUNT][SHORTCUT_TEXT_MAX];
  memcpy(old, g.shortcuts, sizeof(old));
  for (int i = 0; i < SK_COUNT; i++)
    {
      UINT mods = 0, vk = 0;
      if (!shortcut_parse(new_shortcuts[i], &mods, &vk))
        {
          if (error && error_size > 0)
            snprintf(error, (size_t) error_size,
                     "%s: use F8, Ctrl+S, Alt+F4 or None.",
                     g_shortcut_labels[i]);
          return 0;
        }
      if (vk == VK_ESCAPE)
        {
          if (error && error_size > 0)
            snprintf(error, (size_t) error_size,
                     "%s: Escape is fixed and cannot be changed.",
                     g_shortcut_labels[i]);
          return 0;
        }
      if (mods == MOD_CONTROL && vk == VK_F10)
        {
          if (error && error_size > 0)
            snprintf(error, (size_t) error_size,
                     "%s: Ctrl+F10 is reserved for the quick tool menu.",
                     g_shortcut_labels[i]);
          return 0;
        }
      for (int j = 0; j < i; j++)
        {
          UINT pm = 0, pv = 0;
          shortcut_parse(new_shortcuts[j], &pm, &pv);
          if (pv && pv == vk && pm == mods)
            {
              if (error && error_size > 0)
                snprintf(error, (size_t) error_size,
                         "%s and %s use the same shortcut.",
                         g_shortcut_labels[j], g_shortcut_labels[i]);
              return 0;
            }
        }
    }
  /* Try the bindings that are active in this mode before keeping them.
   * This catches shortcuts owned by another application as well as
   * collisions with our own fixed keys. */
  unregister_managed_hotkeys();
  memcpy(g.shortcuts, new_shortcuts, sizeof(old));
  if (!register_runtime_shortcuts(1, error, error_size))
    {
      memcpy(g.shortcuts, old, sizeof(old));
      unregister_managed_hotkeys();
      register_runtime_shortcuts(0, NULL, 0);
      return 0;
    }
  unregister_managed_hotkeys();
  config_save();
  app_hotkeys_refresh();
  return 1;
}

static void register_global_hotkeys(void)
{
  /* The keyboard hook carries the Timer shortcut (F12 cannot be registered)
   * and must exist even while no tool is active. */
  app_install_wheel_hook();
  app_hotkeys_refresh();
  dlog("hotkeys registered from shortcuts: pen=%s text=%s arrow=%s "
       "toolbar=%s icon=%s err=%lu",
       g.shortcuts[SK_PEN], g.shortcuts[SK_TEXT], g.shortcuts[SK_ARROW],
       g.shortcuts[SK_TOOLBAR], g.shortcuts[SK_ICON],
       (unsigned long) GetLastError());
}

static int count_alpha(cairo_surface_t *s)
{
  cairo_surface_flush(s);
  int w = cairo_image_surface_get_width(s);
  int h = cairo_image_surface_get_height(s);
  int stride = cairo_image_surface_get_stride(s);
  unsigned char *data = cairo_image_surface_get_data(s);
  int n = 0;
  for (int y = 0; y < h; y++)
    {
      unsigned int *row = (unsigned int *) (data + (size_t) y * stride);
      for (int x = 0; x < w; x++)
        if ((row[x] >> 24) != 0) n++;
    }
  return n;
}

static int run_uitest(void)
{
  int ok = 1;
  char log[4096];
  size_t pos = 0;
  char path[MAX_PATH * 3];
  wchar_t wpath[MAX_PATH];
  log[0] = 0;
  app_exe_dir(wpath, MAX_PATH);
  {
    cairo_t *cr = cairo_create(g.backbuf);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_destroy(cr);
  }
  g.undo_depth = g.redo_depth = 0;
  g.tool = TOOL_NONE;
  int ox = g.vx, oy = g.vy;
  /* Pen stroke. */
  tools_activate(TOOL_PEN);
  tools_pointer_down((Pt) { ox + 120, oy + 120 }, 0, 0, 0);
  tools_pointer_move((Pt) { ox + 220, oy + 170 }, 0, 0);
  tools_pointer_move((Pt) { ox + 300, oy + 140 }, 0, 0);
  tools_pointer_up((Pt) { ox + 300, oy + 140 }, 0, 0);
  int pen_pixels = count_alpha(g.backbuf);
  if (pen_pixels < 100)
    { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                       "pen: no pixels\n"); }
  else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                "pen: %d pixels\n", pen_pixels);
  /* Marker followed by Pen: switching tools must not leave the marker stroke
   * buffer or the drawing state blocking the pen. */
  tools_activate(TOOL_MARKER);
  tools_pointer_down((Pt) { ox + 350, oy + 120 }, 0, 0, 0);
  tools_pointer_move((Pt) { ox + 420, oy + 150 }, 0, 0);
  tools_pointer_up((Pt) { ox + 420, oy + 150 }, 0, 0);
  int marker_pixels = count_alpha(g.backbuf);
  tools_activate(TOOL_PEN);
  tools_pointer_down((Pt) { ox + 380, oy + 220 }, 0, 0, 0);
  tools_pointer_move((Pt) { ox + 450, oy + 250 }, 0, 0);
  tools_pointer_up((Pt) { ox + 450, oy + 250 }, 0, 0);
  int after_marker_pen = count_alpha(g.backbuf);
  if (after_marker_pen <= marker_pixels)
    { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                       "marker->pen: pen did not draw "
                                       "(%d -> %d, a=%.2f op=%.2f buf=%d)\n",
                                       marker_pixels, after_marker_pen,
                                       g.pen_color.a, g.opacity,
                                       g.stroke_buffer_active); }
  else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                "marker->pen: pen draws ok\n");
  pen_pixels = after_marker_pen;
  /* Line. */
  tools_activate(TOOL_LINE);
  tools_pointer_down((Pt) { ox + 150, oy + 300 }, 0, 0, 0);
  tools_pointer_move((Pt) { ox + 420, oy + 380 }, 0, 1);
  tools_pointer_up((Pt) { ox + 420, oy + 380 }, 0, 1);
  tools_commit(0);
  int after_line = count_alpha(g.backbuf);
  if (after_line <= pen_pixels)
    { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                       "line: not committed\n"); }
  else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                "line: committed\n");
  /* Ellipse. */
  tools_activate(TOOL_ELLIPSE);
  tools_pointer_down((Pt) { ox + 500, oy + 200 }, 0, 0, 0);
  tools_pointer_move((Pt) { ox + 760, oy + 360 }, 0, 0);
  tools_pointer_up((Pt) { ox + 760, oy + 360 }, 0, 0);
  tools_commit(0);
  int after_ellipse = count_alpha(g.backbuf);
  if (after_ellipse <= after_line)
    { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                       "ellipse: not committed\n"); }
  else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                "ellipse: committed\n");
  /* Text. */
  tools_activate(TOOL_TEXT);
  g.text_editor_click_x = ox + 160;
  g.text_editor_click_y = oy + 470;
  tools_text_commit("Screenator on Windows", 0);
  tools_commit(0);
  int after_text = count_alpha(g.backbuf);
  if (after_text <= after_ellipse)
    { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                       "text: not committed\n"); }
  else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                "text: committed\n");
  /* Math plot. */
  g.math_ready = 1;
  tools_activate(TOOL_MATH);
  tools_pointer_down((Pt) { ox + 480, oy + 430 }, 0, 0, 0);
  tools_pointer_move((Pt) { ox + 780, oy + 620 }, 0, 0);
  tools_pointer_up((Pt) { ox + 780, oy + 620 }, 0, 0);
  tools_commit(0);
  int after_math = count_alpha(g.backbuf);
  if (after_math <= after_text)
    { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                       "math: not committed\n"); }
  else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                "math: committed\n");
  /* Undo and redo. */
  tools_undo();
  int after_undo = count_alpha(g.backbuf);
  tools_redo();
  int after_redo = count_alpha(g.backbuf);
  if (after_undo >= after_math)
    { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                       "undo: no change\n"); }
  else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                "undo: %d -> %d\n", after_math, after_undo);
  if (after_redo != after_math)
    { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                       "redo: %d != %d\n", after_redo,
                                       after_math); }
  else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                "redo: restored\n");
  /* Clear and undo the clear. */
  tools_clear();
  int after_clear = count_alpha(g.backbuf);
  tools_undo();
  int after_clear_undo = count_alpha(g.backbuf);
  if (after_clear != 0 || after_clear_undo == 0)
    { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                       "clear: failed (%d, %d)\n",
                                       after_clear, after_clear_undo); }
  else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                "clear: undoable\n");
  /* The rounded-rectangle roundness handle must follow the current radius and
   * keep it when clicked, then change it while dragged. */
  {
    FrameShape rr;
    memset(&rr, 0, sizeof(rr));
    rr.type = SHAPE_RRECT;
    rr.frame.cx = 600;
    rr.frame.cy = 400;
    rr.frame.w = 200;
    rr.frame.h = 100;
    rr.extra = 20;
    Pt hp;
    frame_handle_pos(&rr, FH_EXTRA, &hp);
    if (fabs(hp.x - (rr.frame.cx - rr.frame.w / 2 + rr.extra)) > 0.5)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "rrect: handle is not at the radius\n"); }
    else
      {
        g.edit = EDIT_FRAME;
        g.frame = rr;
        g.handle = FH_EXTRA;
        g.press = hp;
        g.press_cx = rr.frame.cx;
        g.press_cy = rr.frame.cy;
        tools_pointer_move((Pt) { hp.x + 10, hp.y }, 0, 0);
        if (fabs(g.frame.extra - 30) > 0.5)
          { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                             "rrect: drag changed to %.0f\n",
                                             g.frame.extra); }
        else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                      "rrect: handle drag ok\n");
        g.edit = EDIT_NONE;
        memset(&g.frame, 0, sizeof(g.frame));
      }
  }
  /* Polygon: the inner handle snaps to the flat (regular polygon) radius and
   * Ctrl snaps the rotation to the common 30/45 degree steps. */
  {
    memset(&g.poly, 0, sizeof(g.poly));
    g.poly.cx = 600;
    g.poly.cy = 400;
    g.poly.radius = 100;
    g.poly.inner = 0.45;
    g.poly.points = 5;
    g.poly.rotation = 0;
    g.tool = TOOL_POLYGON;
    g.edit = EDIT_POLYGON;
    g.handle = PH_INNER;
    double flat = cos(M_PI / 5.0);
    tools_pointer_move((Pt) { 600 + 100 * flat + 2, 400 }, 0, 0);
    if (fabs(g.poly.inner - flat) > 0.01)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "polygon: flat snap failed\n"); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "polygon: flat snap ok\n");
    g.handle = PH_ROTATION;
    double a = 37.0 * M_PI / 180.0;
    tools_pointer_move((Pt) { 600 + 100 * cos(a), 400 + 100 * sin(a) }, 1, 0);
    double deg = g.poly.rotation * 180.0 / M_PI;
    if (fabs(deg - 30.0) > 0.2)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "polygon: ctrl snap failed (%.1f)\n",
                                         deg); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "polygon: ctrl snap ok\n");
    g.edit = EDIT_NONE;
  }
  /* Polygon: the first press fixes the centre; dragging sets its radius and
   * rotation around that centre. */
  {
    g.tool = TOOL_POLYGON;
    g.edit = EDIT_NONE;
    tools_pointer_down((Pt) { 600, 400 }, 0, 0, 0);
    tools_pointer_move((Pt) { 700, 400 }, 0, 0);
    if (g.edit != EDIT_POLYGON || !g.poly_placing ||
        fabs(g.poly.cx - 600) > 0.5 || fabs(g.poly.cy - 400) > 0.5 ||
        fabs(g.poly.radius - 100) > 0.5 || fabs(g.poly.rotation) > 0.01)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "polygon: press-drag radius/rotation failed\n"); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "polygon: press-drag radius/rotation ok\n");
    tools_pointer_up((Pt) { 700, 400 }, 0, 0);
    g.edit = EDIT_NONE;
  }
  /* Triangle: three clicks set the vertices, with the third vertex tracking
   * the pointer after the second click. */
  {
    g.tool = TOOL_TRIANGLE;
    g.edit = EDIT_NONE;
    g.tri_placing = 0;
    tools_pointer_down((Pt) { 300, 600 }, 0, 0, 0);
    int first = g.tri_placing == 1 && fabs(g.tri.v[0].x - 300) < 0.5 &&
                fabs(g.tri.v[0].y - 600) < 0.5;
    tools_triangle_hover((Pt) { 400, 600 });
    tools_pointer_down((Pt) { 400, 600 }, 0, 0, 0);
    tools_triangle_hover((Pt) { 350, 700 });
    int live = fabs(g.tri.v[2].x - 350) < 0.5 &&
               fabs(g.tri.v[2].y - 700) < 0.5;
    tools_pointer_down((Pt) { 350, 700 }, 0, 0, 0);
    if (g.tri_placing != 0 || fabs(g.tri.v[0].x - 300) > 0.5 ||
        fabs(g.tri.v[1].x - 400) > 0.5 || fabs(g.tri.v[2].y - 700) > 0.5 ||
        !live || !first)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "triangle: three-click placement failed\n"); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "triangle: three-click placement ok\n");
    tools_pointer_down((Pt) { 400, 600 }, 0, 0, 0);
    tools_pointer_move((Pt) { 410, 610 }, 0, 0);
    if (fabs(g.tri.v[1].x - 410) > 0.5 || fabs(g.tri.v[1].y - 610) > 0.5)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "triangle: vertex drag failed\n"); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "triangle: vertex drag ok\n");
    tools_pointer_up((Pt) { 410, 610 }, 0, 0);
    g.edit = EDIT_NONE;
  }
  /* Arrow-style handles keep cycling while the drag continues. */
  {
    g.tool = TOOL_ARROW;
    g.edit = EDIT_LINE;
    g.line.type = TOOL_ARROW;
    g.line.x0 = 100;
    g.line.y0 = 100;
    g.line.x1 = 300;
    g.line.y1 = 100;
    g.line.heads = ARROW_NONE;
    g.handle = LH_HEADS;
    g.press = (Pt) { 200, 100 };
    g.drag_started = 1;
    tools_pointer_move((Pt) { 230, 100 }, 0, 0);
    int line_start = g.line.heads == ARROW_START;
    tools_pointer_move((Pt) { 300, 100 }, 0, 0);
    int line_cycle = g.line.heads == ARROW_NONE;
    g.edit = EDIT_FRAME;
    g.frame.type = SHAPE_BARS;
    g.frame.arrows = BARS_NONE;
    g.handle = FH_ARROWS;
    g.press = (Pt) { 600, 400 };
    g.drag_new_frame = 0;
    g.drag_started = 1;
    tools_pointer_move((Pt) { 630, 400 }, 0, 0);
    int bars_top = g.frame.arrows == BARS_TOP;
    tools_pointer_move((Pt) { 700, 400 }, 0, 0);
    int bars_cycle = g.frame.arrows == BARS_NONE;
    if (!line_start || !line_cycle || !bars_top || !bars_cycle)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "arrow styles: cycling failed "
                                         "(line=%d/%d bars=%d/%d)\n",
                                         line_start, line_cycle, bars_top,
                                         bars_cycle); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "arrow styles: handle cycling ok\n");
    g.edit = EDIT_NONE;
    g.handle = 0;
    memset(&g.line, 0, sizeof(g.line));
    memset(&g.frame, 0, sizeof(g.frame));
  }
  /* Spline: the last node can be dragged; a click without movement finishes. */
  {
    if (!g.nodes)
      {
        g.nodecap = 64;
        g.nodes = (SplineNode *) calloc((size_t) g.nodecap, sizeof(SplineNode));
      }
    memset(g.nodes, 0, sizeof(SplineNode) * 4);
    g.nodes[0].p = (Pt) { 300, 500 };
    g.nodes[1].p = (Pt) { 500, 500 };
    g.nodes[0].in = g.nodes[0].out = g.nodes[0].p;
    g.nodes[1].in = g.nodes[1].out = g.nodes[1].p;
    g.nnodes = 2;
    spline_update_auto(g.nodes, 2);
    g.tool = TOOL_SPLINE;
    g.edit = EDIT_SPLINE;
    g.handle = 1;
    g.press_index = 1;
    g.spline_finish_pending = 1;
    g.drag_started = 0;
    tools_pointer_move((Pt) { 510, 500 }, 0, 0);
    if (g.spline_finish_pending || fabs(g.nodes[1].p.x - 510) > 0.5)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "spline: last node drag failed\n"); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "spline: last node drag ok\n");
    /* After editing a node the rubber-band segment stays hidden while
     * hovering; only a new point arms it again. */
    g.handle = 0;
    g.spline_finish_pending = 0;
    g.spline_preview_on = 0;
    g.spline_cursor = g.nodes[1].p;
    tools_spline_hover((Pt) { 650, 520 });
    int parked = fabs(g.spline_cursor.x - g.nodes[1].p.x) < 0.5 &&
                 fabs(g.spline_cursor.y - g.nodes[1].p.y) < 0.5;
    g.spline_preview_on = 1;
    tools_spline_hover((Pt) { 650, 520 });
    int armed = fabs(g.spline_cursor.x - 650) < 0.5 &&
                fabs(g.spline_cursor.y - 520) < 0.5;
    if (!parked || !armed)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "spline: preview state failed\n"); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "spline: preview stays parked ok\n");
    g.edit = EDIT_SPLINE;
    g.handle = 1;
    g.press_index = 1;
    g.spline_finish_pending = 1;
    g.nnodes = 2;
    tools_pointer_up((Pt) { 510, 500 }, 0, 0);
    if (g.edit != EDIT_NONE)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "spline: last node click did not finish\n"); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "spline: last node click finishes\n");
    g.nnodes = 0;
  }
  /* Stylus side button 1 (reported as the middle button) and the middle
   * mouse button are immediate temporary erasers in Screen mode unless a
   * shape or the text editor still shows handles (then they recolour). */
  {
    Paper stylus_saved_paper = g.paper;
    g.paper = PAPER_NONE;
    tools_activate(TOOL_PEN);
    g.pen_seen = 1;
    Pt ep = { (double) (g.vw * 3 / 5), (double) (g.vh * 3 / 4) };
    tools_pointer_down(ep, 0, 0, 0);
    tools_pointer_move((Pt) { ep.x + 20, ep.y + 10 }, 0, 0);
    tools_pointer_up((Pt) { ep.x + 20, ep.y + 10 }, 0, 0);
    int drawn = count_alpha(g.backbuf);
    SendMessageW(g.inputwin, WM_MBUTTONDOWN, MK_MBUTTON,
                 MAKELPARAM((int) ep.x - g.vx, (int) ep.y - g.vy));
    int erasing = g.middle_erasing;
    SendMessageW(g.inputwin, WM_MBUTTONUP, 0,
                 MAKELPARAM((int) ep.x - g.vx, (int) ep.y - g.vy));
    int after_erase = count_alpha(g.backbuf);
    int erased = after_erase < drawn;
    if (!erasing || !erased)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "stylus: middle eraser failed "
                                         "(erasing=%d erased=%d drawn=%d "
                                         "after=%d)\n",
                                         erasing, erased, drawn,
                                         after_erase); }
    else
      pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                               "stylus: middle button eraser ok\n");
    int f = 0, s = 0;
    pen_classify(POINTER_FLAG_SECONDBUTTON, 0, &f, &s);
    int pointer_button1_ok = f && !s;
    pen_classify(0, PEN_FLAG_BARREL, &f, &s);
    int barrel_button1_ok = f && !s;
    pen_classify(POINTER_FLAG_SECONDBUTTON, PEN_FLAG_BARREL, &f, &s);
    int combined_button1_ok = f && !s;
    pen_classify(POINTER_FLAG_THIRDBUTTON, 0, &f, &s);
    int button2_ok = !f && s;
    if (!pointer_button1_ok || !barrel_button1_ok ||
        !combined_button1_ok || !button2_ok)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "stylus: flag classification failed "
                                         "(pointer=%d barrel=%d combined=%d "
                                         "b2=%d)\n",
                                         pointer_button1_ok, barrel_button1_ok,
                                         combined_button1_ok, button2_ok); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "stylus: button flag classification ok\n");
    /* The raw HID secondary barrel and promoted Windows Ink mouse event may
     * both report button 2.  The duplicate must not start another action. */
    {
      Paper saved = g.paper;
      g.paper = PAPER_PLAIN;
      g.edit = EDIT_NONE;
      g.pen_second_down = 0;
      g.pen_barrel_down = 0;
      g.pen_pointer_active = 0;
      pen_second_owner = 0;
      g.right_erase_active = 0;
      g.right_short_done = 0;
      g.tool = TOOL_PEN;
      Pt sp = { 400, 400 };
      pen_second_begin(sp, 2);
      int raw_down = g.pen_second_down && g.right_erase_active;
      int owner_raw = pen_second_owner == 2;
      pen_second_begin(sp, 1);          /* duplicate from Windows Ink */
      int still_raw = pen_second_owner == 2 && g.pen_second_down;
      pen_second_end(sp);
      int ended = !g.pen_second_down && !g.right_erase_active;
      right_button_up(sp);              /* duplicate promoted mouse release */
      int tool_kept = g.tool == TOOL_PEN;
      if (!raw_down || !owner_raw || !still_raw || !ended || !tool_kept)
        { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                           "stylus: duplicate second button "
                                           "failed (down=%d owner=%d dup=%d "
                                           "end=%d tool=%d)\n",
                                           raw_down, owner_raw, still_raw,
                                           ended, tool_kept); }
      else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                    "stylus: one button-2 press one right "
                                    "action ok\n");
      g.paper = saved;
    }
    /* After a pen session a stale pan/pen state must not swallow the first
     * real mouse click. */
    {
      Paper saved = g.paper;
      g.paper = PAPER_NONE;
      tools_activate(TOOL_PEN);
      g.pen_seen = 1;
      g.paper_panning = 1;
      g.paper_pan_pen = 1;
      g.pen_pointer_active = 0;
      g.pen_eraser_down = 0;
      g.pen_barrel_down = 0;
      g.pen_second_down = 0;
      g.pen_id = 0;
      int before = count_alpha(g.backbuf);
      Pt mp = { 300, 300 };
      SendMessageW(g.inputwin, WM_LBUTTONDOWN, MK_LBUTTON,
                   MAKELPARAM((int) mp.x - g.vx, (int) mp.y - g.vy));
      SendMessageW(g.inputwin, WM_MOUSEMOVE, MK_LBUTTON,
                   MAKELPARAM((int) mp.x + 20 - g.vx, (int) mp.y + 10 - g.vy));
      SendMessageW(g.inputwin, WM_LBUTTONUP, 0,
                   MAKELPARAM((int) mp.x + 20 - g.vx, (int) mp.y + 10 - g.vy));
      int drew = count_alpha(g.backbuf) > before;
      if (g.paper_panning || g.pen_seen || !drew)
        { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                           "stylus: mouse after pen failed "
                                           "(pan=%d seen=%d drew=%d)\n",
                                           g.paper_panning, g.pen_seen, drew); }
      else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                    "stylus: mouse after pen ok\n");
      g.paper = saved;
    }
    /* Button 1 / wheel while a frame still shows its handles must recolour
     * it, in Screen and Paper mode. */
    {
      Paper saved = g.paper;
      g.paper = PAPER_PLAIN;
      memset(&g.frame, 0, sizeof(g.frame));
      g.frame.type = SHAPE_RRECT;
      g.frame.frame.cx = 300;
      g.frame.frame.cy = 300;
      g.frame.frame.w = 100;
      g.frame.frame.h = 80;
      g.frame.color = g.pen_color;
      g.edit = EDIT_FRAME;
      g.pen_barrel_down = 0;
      pen_first_scroll_only = 0;
      Color before = g.frame.color;
      int c0 = line_color_index();
      pen_scroll_tap((Pt) { 300, 300 }, 1);
      int c1 = line_color_index();
      int frame_tap = g.frame.color.r != before.r ||
                      g.frame.color.g != before.g ||
                      g.frame.color.b != before.b;
      pen_barrel_end();
      int tap_ok = c1 != c0 && frame_tap && !g.pen_barrel_down;
      int c2 = line_color_index();
      Color before_wheel = g.frame.color;
      SendMessageW(g.inputwin, WM_MOUSEWHEEL,
                   (WPARAM) MAKEWPARAM(0, WHEEL_DELTA), 0);
      int c3 = line_color_index();
      int frame_wheel = g.frame.color.r != before_wheel.r ||
                        g.frame.color.g != before_wheel.g ||
                        g.frame.color.b != before_wheel.b;
      int wheel_ok = c3 != c2 && frame_wheel;
      if (!tap_ok || !wheel_ok)
        { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                           "stylus: handle colour step failed "
                                           "(tap=%d wheel=%d)\n",
                                           tap_ok, wheel_ok); }
      else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                    "stylus: button 1 and wheel recolour "
                                    "handles ok\n");
      g.edit = EDIT_NONE;
      g.paper = saved;
    }
    g.paper = stylus_saved_paper;
  }
  /* Clicking outside a dialog marked as outside-close must close it. */
  {
    Paper saved_paper = g.paper;
    g.paper = PAPER_PLAIN;
    HWND h = CreateWindowExW(0, L"STATIC", L"modal test", WS_POPUP,
                             100, 100, 200, 100, NULL, NULL, g.hinst, NULL);
    g.modal_dialog = h;
    g.modal_outside_close = 1;
    int inside = ui_modal_outside_click((Pt) { 150, 150 });
    int alive_inside = h && IsWindow(h);
    int outside = ui_modal_outside_click((Pt) { 10, 10 });
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE))
      {
        TranslateMessage(&m);
        DispatchMessageW(&m);
      }
    int closed = !h || !IsWindow(h);
    if (inside || !alive_inside || !outside || !closed)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "dialog: outside close failed "
                                         "(in=%d alive=%d out=%d closed=%d)\n",
                                         inside, alive_inside, outside,
                                         closed); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "dialog: outside click closes\n");
    if (h && IsWindow(h)) DestroyWindow(h);
    g.modal_dialog = NULL;
    g.modal_outside_close = 0;
    g.paper = saved_paper;
  }
  /* A delayed tool activates at the delay even while a menu is open; the
   * menu capture supplies the ghost menu.  The overlay input is restored
   * when the menu closes. */
  {
    g.tool = TOOL_NONE;
    g.edit = EDIT_NONE;
    g.pending_tool = TOOL_LINE;
    g.tool_delay_deferred = 0;
    g.menu_open = 1;
    g.overlay_activate_pending = 0;
    SendMessageW(g.msgwin, WM_TIMER, TIMER_TOOL_DELAY, 0);
    int activated = g.tool == TOOL_LINE && g.pending_tool == TOOL_NONE &&
                    !g.tool_delay_deferred;
    int pending_overlay = g.overlay_activate_pending;
    g.menu_open = 0;
    ui_menu_finish();
    int flushed = !g.overlay_activate_pending;
    if (!activated || !pending_overlay || !flushed)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "menu: delayed tool activation failed "
                                         "(active=%d pending=%d flushed=%d)\n",
                                         activated, pending_overlay, flushed); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "menu: delayed tool activates in menu\n");
  }
  /* The snapshot taken just before delayed activation is retained as the
   * ghost menu when the native menu closes without a selection. */
  {
    cairo_surface_t *snap =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 24, 24);
    cairo_t *cr = cairo_create(snap);
    cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_destroy(cr);
    if (g.menu_capture) cairo_surface_destroy(g.menu_capture);
    g.menu_capture = snap;
    g.menu_capture_rect = (RECT) { 100, 100, 124, 124 };
    g.menu_capture_kind = 0;
    app_menu_capture_stop(1);
    int kept = g.menu_ghost_active && g.menu_ghost == snap;
    app_clear_menu_ghost();
    int cleared = !g.menu_ghost_active && !g.menu_ghost;
    if (!kept || !cleared)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "menu: ghost retention failed "
                                         "(kept=%d cleared=%d)\n",
                                         kept, cleared); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "menu: delay snapshot becomes ghost\n");
  }
  /* Switching tools keeps the captured menu; going idle clears it. */
  {
    cairo_surface_t *snap =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 24, 24);
    cairo_t *cr = cairo_create(snap);
    cairo_set_source_rgb(cr, 0.0, 0.5, 1.0);
    cairo_paint(cr);
    cairo_destroy(cr);
    if (g.menu_capture) cairo_surface_destroy(g.menu_capture);
    g.menu_capture = snap;
    g.menu_capture_rect = (RECT) { 100, 100, 124, 124 };
    g.menu_capture_kind = 3;
    app_menu_capture_stop(1);
    g.tool = TOOL_TEXT;
    g.edit = EDIT_NONE;
    tools_activate(TOOL_LINE);
    int kept_switch = g.menu_ghost_active && g.menu_ghost == snap;
    tools_exit();
    int cleared_exit = !g.menu_ghost_active && !g.menu_ghost;
    if (!kept_switch || !cleared_exit)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "menu: ghost lifetime failed "
                                         "(switch=%d exit=%d)\n",
                                         kept_switch, cleared_exit); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "menu: ghost kept on tool switch, cleared "
                                  "on exit\n");
  }
  /* Esc first closes shape handles; only an Esc that exits the tool clears
   * the captured menu. */
  {
    cairo_surface_t *snap =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 24, 24);
    cairo_t *cr = cairo_create(snap);
    cairo_set_source_rgb(cr, 0.5, 0.8, 0.0);
    cairo_paint(cr);
    cairo_destroy(cr);
    if (g.menu_capture) cairo_surface_destroy(g.menu_capture);
    g.menu_capture = snap;
    g.menu_capture_rect = (RECT) { 100, 100, 124, 124 };
    g.menu_capture_kind = 0;
    app_menu_capture_stop(1);
    g.tool = TOOL_LINE;
    g.edit = EDIT_LINE;
    memset(&g.line, 0, sizeof(g.line));
    g.line.type = TOOL_LINE;
    g.line.x0 = 100;
    g.line.y0 = 100;
    g.line.x1 = 220;
    g.line.y1 = 180;
    tools_key(VK_ESCAPE, 0, 0);
    int kept_handles = g.menu_ghost_active && g.menu_ghost == snap &&
                       g.edit == EDIT_NONE && g.tool == TOOL_LINE;
    tools_key(VK_ESCAPE, 0, 0);
    int cleared_tool = !g.menu_ghost_active && !g.menu_ghost &&
                       g.tool == TOOL_NONE;
    if (!kept_handles || !cleared_tool)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "menu: ghost Esc handling failed "
                                         "(handles=%d tool=%d)\n",
                                         kept_handles, cleared_tool); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "menu: ghost survives handle-only Esc\n");
  }
  if (!ui_selfcheck())
    { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                       "ui: menu/icon check failed\n"); }
  else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                "ui: menus and icons ok\n");
  if (!ui_dialog_selfcheck())
    { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                       "ui: dialog creation failed\n"); }
  else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                "ui: dialogs ok\n");
  if (!toolbar_selfcheck())
    { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                       "toolbar: items/tooltips failed\n"); }
  else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                "toolbar: items and tooltips ok\n");
  /* Save the annotated backbuffer for visual inspection. */
  {
    wchar_t png[MAX_PATH];
    _snwprintf(png, MAX_PATH, L"%s\\uitest.png", wpath);
    WideCharToMultiByte(CP_UTF8, 0, png, -1, path, sizeof(path), NULL, NULL);
  }  if (cairo_surface_write_to_png(g.backbuf, path) != CAIRO_STATUS_SUCCESS)
    { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                       "png: write failed\n"); }
  else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                "png: wrote uitest.png\n");
  /* Settings and annotations round trip. */
  {
    wchar_t cfg[MAX_PATH], apng[MAX_PATH];
    _snwprintf(cfg, MAX_PATH, L"%s\\cfgtest.ini", wpath);
    _snwprintf(apng, MAX_PATH, L"%s\\cfgtest.png", wpath);
    DeleteFileW(cfg);
    DeleteFileW(apng);
    wcscpy(g.config_path, cfg);
    wcscpy(g.annotations_path, apng);
    config_defaults();
    g.pen_width = 24;
    g.eraser_width = 60;
    g.opacity = 0.6;
    g.pen_color = color_from_hex("#0000FF", NULL);
    g.pen_color.a = g.opacity;
    g.fill_enabled = 1;
    g.fill_color = color_from_hex("#FF8000", NULL);
    g.fill_color.a = 1.0;
    strcpy(g.font, "Arial Bold 30");
    g.icon_size = 48;
    g.icon_position = ICON_BL;
    g.show_icon = 0;
    g.show_indicator = 0;
    g.save_on_exit = 0;
    g.hide_hints = 1;
    g.wheel_mode = WHEEL_THICKNESS;
    g.stylus_mode = STYLUS_THICKNESS;
    g.marker_opacity = 0.7;
    strcpy(g.toolbar_items, "cursor,pen,quit");
    strcpy(g.toolbar_order, "pen,cursor,text,quit");
    strcpy(g.shortcuts[SK_TEXT], "Ctrl+Alt+T");
    strcpy(g.shortcuts[SK_UNDO], "None");
    config_save();
    config_defaults();
    config_load_from(cfg);
    int cfgok = g.pen_width == 24 && g.eraser_width == 60 &&
                fabs(g.opacity - 0.6) < 0.01 &&
                fabs(g.pen_color.b - 1.0) < 0.01 &&
                fabs(g.pen_color.r) < 0.01 &&
                g.fill_enabled &&
                fabs(g.fill_color.r - 1.0) < 0.01 &&
                fabs(g.fill_color.g - 0.5) < 0.01 &&
                !strcmp(g.font, "Arial Bold 30") &&
                g.icon_size == 48 && g.icon_position == ICON_BL &&
                g.show_icon == 0 && g.show_indicator == 0 &&
                g.save_on_exit == 0 && g.hide_hints == 1 &&
                g.wheel_mode == WHEEL_THICKNESS &&
                g.stylus_mode == STYLUS_THICKNESS &&
                fabs(g.marker_opacity - 0.7) < 0.01 &&
                !strcmp(g.toolbar_items, "cursor,pen,quit") &&
                !strcmp(g.toolbar_order, "pen,cursor,text,quit") &&
                !strcmp(g.shortcuts[SK_TEXT], "Ctrl+Alt+T") &&
                !strcmp(g.shortcuts[SK_UNDO], "None");
    /* One annotation round trip. */
    {
      cairo_t *acr = cairo_create(g.backbuf);
      cairo_set_operator(acr, CAIRO_OPERATOR_SOURCE);
      cairo_set_source_rgba(acr, 0, 0, 0, 0);
      cairo_paint(acr);
      cairo_set_source_rgb(acr, 0, 0.7, 0.2);
      cairo_rectangle(acr, 40, 40, 120, 80);
      cairo_fill(acr);
      cairo_destroy(acr);
    }
    annotations_save();
    {
      cairo_t *acr = cairo_create(g.backbuf);
      cairo_set_operator(acr, CAIRO_OPERATOR_CLEAR);
      cairo_paint(acr);
      cairo_destroy(acr);
    }
    annotations_load();
    int saved_pixels = count_alpha(g.backbuf);
    if (!cfgok || saved_pixels < 100)
      {
        ok = 0;
        pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                 "config: failed (readback %d, pixels %d)\n",
                                 cfgok, saved_pixels);
      }
    else
      pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                               "config: settings and annotations ok\n");
    DeleteFileW(cfg);
    DeleteFileW(apng);
  }
  if (g.render_failures != 0) ok = 0;
  pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                           "render: %d failures\n", g.render_failures);
  pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                           ok ? "UITEST OK\n" : "UITEST FAILED\n");
  {
    wchar_t txt[MAX_PATH];
    _snwprintf(txt, MAX_PATH, L"%s\\uitest.txt", wpath);
    WideCharToMultiByte(CP_UTF8, 0, txt, -1, path, sizeof(path), NULL, NULL);
    FILE *f = fopen(path, "w");
    if (f) { fputs(log, f); fclose(f); }
  }
  return ok ? 0 : 1;
}

/* Render a math expression into a small transparent surface and report how
 * many pixels it covers, how many lie in the centre-middle band (where a
 * false asymptote line would appear) and how many lie in the centre-top band
 * (where a delta arrow points). */
static void math_probe(const char *expr, double xmin, double xmax,
                       int *total, int *center_mid, int *center_top)
{
  const int W = 220, H = 120;
  cairo_surface_t *s =
    cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
  cairo_t *cr = cairo_create(s);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  unsigned saved_width = g.pen_width;
  g.pen_width = 4;
  FrameShape fs;
  memset(&fs, 0, sizeof(fs));
  fs.type = SHAPE_MATH;
  fs.expr = (char *) expr;
  fs.xmin = xmin;
  fs.xmax = xmax;
  fs.frame.cx = W / 2.0;
  fs.frame.cy = H / 2.0;
  fs.frame.w = 200;
  fs.frame.h = 100;
  fs.color = color_rgb(255, 255, 255);
  fs.color.a = 1.0;
  draw_frame_shape(cr, &fs);
  g.pen_width = saved_width;
  cairo_destroy(cr);
  cairo_surface_flush(s);
  int stride = cairo_image_surface_get_stride(s);
  unsigned char *data = cairo_image_surface_get_data(s);
  *total = 0;
  *center_mid = 0;
  *center_top = 0;
  for (int y = 0; y < H; y++)
    {
      unsigned int *row = (unsigned int *) (data + (size_t) y * stride);
      for (int x = 0; x < W; x++)
        {
          if ((row[x] >> 24) == 0) continue;
          (*total)++;
          if (x >= 109 && x <= 111)
            {
              if (y >= 30 && y <= 90) (*center_mid)++;
              if (y >= 5 && y <= 30) (*center_top)++;
            }
        }
    }
  cairo_surface_destroy(s);
}

static int run_selftest(void)
{
  int ok = 1;
  char log[4096];
  size_t pos = 0;
  const char *err = NULL;
  wchar_t wpath[MAX_PATH];
  char path[MAX_PATH * 3];
  log[0] = 0;
  /* Editable shortcut parser and formatter. */
  {
    UINT m = 0, v = 0;
    char fmt[32];
    int okparse = shortcut_parse("F8", &m, &v) && !m && v == VK_F8;
    okparse = okparse && shortcut_parse("Ctrl+S", &m, &v) &&
              m == MOD_CONTROL && v == 'S';
    okparse = okparse && shortcut_parse("Alt+F4", &m, &v) &&
              m == MOD_ALT && v == VK_F4;
    okparse = okparse && shortcut_parse("None", &m, &v) && !m && !v;
    okparse = okparse && !shortcut_parse("S", &m, &v);
    okparse = okparse && !shortcut_parse("Ctrl+", &m, &v);
    okparse = okparse && shortcut_format(MOD_CONTROL | MOD_SHIFT, 'Z',
                                          fmt, sizeof(fmt)) &&
              !strcmp(fmt, "Ctrl+Shift+Z");
    if (!okparse)
      {
        ok = 0;
        pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                 "shortcuts: parser failed\n");
      }
    else
      pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                               "shortcuts: parser ok\n");
  }
  /* Math parser and evaluator. */
  {
    MathParser *m = math_parse("sin(2*pi*x)/x", &err);
    double v = 0;
    if (!m)
      {
        ok = 0;
        pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                 "math: parse failed (%s)\n",
                                 err ? err : "?");
      }
    else
      {
        if (!math_eval(m, 1.0, &v))
          { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                             "math: eval failed\n"); }
        else
          pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                   "math: sin(2*pi*1)/1 = %.6f\n", v);
        math_free(m);
      }
    if (!math_validate("cos(3*theta)", &err))
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "math: polar validate failed\n"); }
    if (math_validate("2**x", &err))
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "math: invalid expression accepted\n"); }
  }
  /* Plotting: the pole of 1/x must break the curve instead of drawing a
   * vertical line through the centre, and delta(x) must draw its arrow. */
  {
    int total = 0, center_mid = 0, center_top = 0;
    math_probe("1/x", -4, 4, &total, &center_mid, &center_top);
    if (total < 100 || center_mid != 0)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "math: 1/x pole not broken "
                                         "(total=%d centre=%d)\n",
                                         total, center_mid); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "math: 1/x pole broken\n");
    math_probe("delta(x)", -4, 4, &total, &center_mid, &center_top);
    if (total < 50 || center_top == 0)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "math: delta arrow missing "
                                         "(total=%d top=%d)\n",
                                         total, center_top); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "math: delta arrow drawn\n");
  }
  int W = 1200, H = 800;
  cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
  cairo_t *cr = cairo_create(surf);
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_paint(cr);
  g.pen_width = 6;
  g.pen_color = color_from_hex("#E03020", NULL);
  g.pen_color.a = 1.0;
  g.fill_enabled = 1;
  g.fill_color = color_from_hex("#00AA00", NULL);
  g.fill_color.a = 1.0;
  g.opacity = 1.0;
  draw_paper(cr, PAPER_GRAPH, W, H);
  FrameShape fs;
  memset(&fs, 0, sizeof(fs));
  fs.type = SHAPE_ELLIPSE;
  fs.frame.cx = 340; fs.frame.cy = 200;
  fs.frame.w = 360; fs.frame.h = 200;
  fs.color = g.pen_color;
  draw_frame_shape(cr, &fs);
  memset(&fs, 0, sizeof(fs));
  fs.type = SHAPE_RRECT;
  fs.frame.cx = 860; fs.frame.cy = 200;
  fs.frame.w = 320; fs.frame.h = 180;
  fs.extra = 40;
  fs.color = color_from_hex("#2050C0", NULL);
  draw_frame_shape(cr, &fs);
  LineShape ln;
  memset(&ln, 0, sizeof(ln));
  ln.type = TOOL_ARROW;
  ln.x0 = 90; ln.y0 = 400; ln.x1 = 520; ln.y1 = 400;
  ln.heads = ARROW_BOTH;
  draw_line_shape(cr, &ln, 6, color_from_hex("#C02020", NULL));
  StarShape poly;
  memset(&poly, 0, sizeof(poly));
  poly.cx = 900; poly.cy = 440;
  poly.radius = 120; poly.inner = 0.5; poly.points = 5;
  poly.rotation = -M_PI / 2;
  draw_polygon_shape(cr, &poly, 6, color_from_hex("#8040C0", NULL));
  Triangle tri;
  tri.v[0].x = 120; tri.v[0].y = 700;
  tri.v[1].x = 300; tri.v[1].y = 560;
  tri.v[2].x = 380; tri.v[2].y = 730;
  draw_triangle_shape(cr, &tri, 6, color_from_hex("#207040", NULL));
  /* Text through pango. */
  memset(&fs, 0, sizeof(fs));
  fs.type = SHAPE_TEXT;
  fs.text = _strdup("Screenator 8.0");
  fs.font = _strdup("Arial Bold 34");
  fs.font_size = 34;
  fs.color = color_from_hex("#202020", NULL);
  measure_frame_text(&fs);
  if (fs.frame.w < 20 || fs.frame.h < 10)
    {
      ok = 0;
      pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                               "text: measure failed (%.0fx%.0f)\n",
                               fs.frame.w, fs.frame.h);
    }
  else
    {
      pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                               "text: %.0fx%.0f px\n",
                               fs.frame.w, fs.frame.h);
      fs.frame.cx = 600; fs.frame.cy = 470;
      draw_frame_shape(cr, &fs);
    }
  free(fs.text);
  free(fs.font);
  /* A math plot. */
  memset(&fs, 0, sizeof(fs));
  fs.type = SHAPE_MATH;
  fs.expr = _strdup("sin(2*pi*x)/x");
  fs.xmin = -10; fs.xmax = 10;
  fs.frame.cx = 600; fs.frame.cy = 660;
  fs.frame.w = 520; fs.frame.h = 200;
  fs.color = color_from_hex("#1050A0", NULL);
  draw_frame_shape(cr, &fs);
  free(fs.expr);
  /* A spline. */
  SplineNode nodes[4];
  memset(nodes, 0, sizeof(nodes));
  nodes[0].p = (Pt) { 80, 520 };
  nodes[1].p = (Pt) { 200, 500 };
  nodes[2].p = (Pt) { 300, 540 };
  nodes[3].p = (Pt) { 420, 520 };
  spline_update_auto(nodes, 4);
  draw_spline_shape(cr, nodes, 4, 6, color_from_hex("#D08000", NULL));
  cairo_destroy(cr);
  /* Paint bucket: fill an enclosed circle and refuse to flood the canvas. */
  {
    cairo_surface_t *bs =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 200, 200);
    cairo_t *bcr = cairo_create(bs);
    cairo_set_source_rgb(bcr, 1, 0, 0);
    cairo_set_line_width(bcr, 6);
    cairo_arc(bcr, 100, 100, 60, 0, 2 * M_PI);
    cairo_stroke(bcr);
    cairo_destroy(bcr);
    cairo_surface_t *saved_back = g.backbuf;
    int svx = g.vx, svy = g.vy, svw = g.vw, svh = g.vh;
    int sfill = g.fill_enabled;
    Color sfcolor = g.fill_color;
    g.backbuf = bs;
    g.vx = 0; g.vy = 0; g.vw = 200; g.vh = 200;
    g.fill_enabled = 1;
    g.fill_color = color_from_hex("#00AA00", NULL);
    g.fill_color.a = 1.0;
    bucket_fill((Pt) { 100, 100 });
    cairo_surface_flush(bs);
    unsigned int *px = (unsigned int *) cairo_image_surface_get_data(bs);
    int st = cairo_image_surface_get_stride(bs) / 4;
    unsigned int got = px[100 * st + 100];
    if ((got & 0x00FFFFFF) != 0x0000AA00)
      {
        ok = 0;
        pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                 "bucket: fill failed (%08X)\n", got);
      }
    else
      pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                               "bucket: enclosed area filled\n");
    bucket_fill((Pt) { 10, 10 });        /* would reach the edge */
    cairo_surface_flush(bs);
    if (px[10 * st + 10] != 0)
      {
        ok = 0;
        pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                 "bucket: edge protection failed\n");
      }
    else
      pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                               "bucket: edge protected\n");
    g.backbuf = saved_back;
    g.vx = svx; g.vy = svy; g.vw = svw; g.vh = svh;
    g.fill_enabled = sfill;
    g.fill_color = sfcolor;
    cairo_surface_destroy(bs);
  }
  /* Paper page model, per-page annotations and PDF failure handling. */
  {
    char paper_log[1024];
    if (!paper_selfcheck(paper_log, sizeof(paper_log)))
      {
        ok = 0;
        pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                 "paper: FAILED\n%s", paper_log);
      }
    else
      pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                               "%s", paper_log);
  }
  app_exe_dir(wpath, MAX_PATH);
  {
    wchar_t png[MAX_PATH];
    _snwprintf(png, MAX_PATH, L"%s\\selftest.png", wpath);
    WideCharToMultiByte(CP_UTF8, 0, png, -1, path, sizeof(path), NULL, NULL);
  }
  if (cairo_surface_write_to_png(surf, path) != CAIRO_STATUS_SUCCESS)
    {
      ok = 0;
      pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                               "png: write failed\n");
    }
  else
    pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                             "png: wrote selftest.png\n");
  cairo_surface_destroy(surf);
  /* Asset icons. */
  {
    wchar_t p[MAX_PATH];
    app_asset_path(p, MAX_PATH, L"icon-normal.png");
    WideCharToMultiByte(CP_UTF8, 0, p, -1, path, sizeof(path), NULL, NULL);
    cairo_surface_t *i = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(i) != CAIRO_STATUS_SUCCESS)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "icons: load failed\n"); }
    else
      pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                               "icons: %dx%d\n",
                               cairo_image_surface_get_width(i),
                               cairo_image_surface_get_height(i));
    cairo_surface_destroy(i);
  }
  pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                           ok ? "SELFTEST OK\n" : "SELFTEST FAILED\n");
  {
    wchar_t txt[MAX_PATH];
    _snwprintf(txt, MAX_PATH, L"%s\\selftest.txt", wpath);
    WideCharToMultiByte(CP_UTF8, 0, txt, -1, path, sizeof(path), NULL, NULL);
    FILE *f = fopen(path, "w");
    if (f)
      {
        fputs(log, f);
        fclose(f);
      }
  }
  /* Run the same teardown as a normal exit.  Without it the process exits
   * with live Cairo/PDFium objects, which can trip the heap checker. */
  destroy_buffers();
  paper_shutdown();
  pdfium_shutdown();
  return ok ? 0 : 1;
}

static void demo_draw(void)
{
  double w = g.vw, h = g.vh;
  cairo_t *cr = cairo_create(g.backbuf);
  g.pen_width = 6;
  g.fill_enabled = 0;
  Color red = color_from_hex("#E02020", NULL);
  red.a = 1.0;
  FrameShape fs;
  memset(&fs, 0, sizeof(fs));
  fs.type = SHAPE_ELLIPSE;
  fs.frame.cx = w * 0.34;
  fs.frame.cy = h * 0.30;
  fs.frame.w = 320;
  fs.frame.h = 190;
  fs.color = red;
  draw_frame_shape(cr, &fs);
  LineShape ln;
  memset(&ln, 0, sizeof(ln));
  ln.type = TOOL_ARROW;
  ln.x0 = w * 0.16;
  ln.y0 = h * 0.56;
  ln.x1 = w * 0.46;
  ln.y1 = h * 0.56;
  ln.heads = ARROW_END;
  draw_line_shape(cr, &ln, 6, color_from_hex("#1040C0", NULL));
  memset(&fs, 0, sizeof(fs));
  fs.type = SHAPE_TEXT;
  fs.text = _strdup("Screenator 8.0 for Windows");
  fs.font = _strdup("Arial Bold 40");
  fs.font_size = 40;
  fs.color = color_from_hex("#B00020", NULL);
  measure_frame_text(&fs);
  fs.frame.cx = w * 0.40;
  fs.frame.cy = h * 0.66;
  draw_frame_shape(cr, &fs);
  free(fs.text);
  free(fs.font);
  cairo_destroy(cr);
  cairo_surface_mark_dirty(g.backbuf);
  app_render_all();
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE prev, LPSTR cmdline, int show)
{
  (void) prev;
  (void) cmdline;
  (void) show;
  wchar_t startup_pdf[MAX_PATH] = L"";
  int open_pdf_on_start = app_find_pdf_arg(startup_pdf, MAX_PATH);
  HANDLE instance_mutex = CreateMutexW(NULL, TRUE,
                                       L"Screenator.Windows.SingleInstance");
  if (instance_mutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
      if (open_pdf_on_start)
        {
          HWND existing = FindWindowW(MSG_CLASS, L"Screenator");
          if (existing)
            {
              COPYDATASTRUCT cd;
              cd.dwData = 1;
              cd.cbData = (DWORD) ((wcslen(startup_pdf) + 1) *
                                   sizeof(wchar_t));
              cd.lpData = startup_pdf;
              SendMessageW(existing, WM_COPYDATA, 0, (LPARAM) &cd);
              CloseHandle(instance_mutex);
              return 0;
            }
        }
      MessageBoxW(NULL, L"Screenator is already running.", L"Screenator",
                  MB_ICONINFORMATION | MB_OK);
      CloseHandle(instance_mutex);
      return 0;
    }
  memset(&g, 0, sizeof(g));
  g.hinst = hinst;
  set_dpi_aware();
  INITCOMMONCONTROLSEX icc;
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_STANDARD_CLASSES;
  InitCommonControlsEx(&icc);
  g.richedit = LoadLibraryW(L"Msftedit.dll");
  config_load();
  if (cmdline && strstr(cmdline, "--debug")) g.debug = 1;
  g.pen_color.a = g.opacity;
  g.fill_color.a = 1.0;
  g.custom_line_color.a = 1.0;
  g.custom_fill_color.a = 1.0;
  if (cmdline && strstr(cmdline, "--selftest"))
    return run_selftest();
  if (cmdline && (strstr(cmdline, "--demo") ||
                  strstr(cmdline, "--demo-paper") || strstr(cmdline, "--demo-math") ||
                  strstr(cmdline, "--pen-test")))
    {
      g.debug = 1;
      wchar_t dbg[MAX_PATH];
      app_exe_dir(dbg, MAX_PATH);
      wcscat(dbg, L"\\debug.txt");
      DeleteFileW(dbg);
    }
  g.smoke = (cmdline && (strstr(cmdline, "--smoke") ||
                         strstr(cmdline, "--smoke-icon") ||
                         strstr(cmdline, "--demo") ||
                         strstr(cmdline, "--demo-paper") || strstr(cmdline, "--demo-math") ||
                         strstr(cmdline, "--pen-test") ||
                         strstr(cmdline, "--uitest"))) ? 1 : 0;
  if (g.smoke)
    {
      if (!(cmdline && (strstr(cmdline, "--smoke-icon") ||
                        strstr(cmdline, "--demo") ||
                        strstr(cmdline, "--demo-paper") || strstr(cmdline, "--demo-math") ||
                        strstr(cmdline, "--pen-test"))))
        g.show_icon = 0;
      g.hide_hints = 1;
      g.save_on_exit = 0;
    }
  get_virtual_screen();
  old_vx = g.vx; old_vy = g.vy; old_vw = g.vw; old_vh = g.vh;
  register_classes();
  g.msgwin = CreateWindowExW(0, MSG_CLASS, L"Screenator", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, NULL, hinst, NULL);
  g.overlay = CreateWindowExW(
    WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
      WS_EX_TRANSPARENT,
    OVERLAY_CLASS, L"Screenator", WS_POPUP,
    g.vx, g.vy, g.vw, g.vh, NULL, NULL, hinst, NULL);
  /* Invisible input window.  Layered windows are hit-tested per pixel, so the
   * drawing overlay cannot catch clicks on transparent pixels; this window is
   * a uniform, almost invisible surface that does catch them.  It is owned by
   * the visible overlay so it always stays directly above it; otherwise
   * activating the paper can put it behind the transparent overlay and clicks
   * fall through to the application below. */
  g.inputwin = CreateWindowExW(
    WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
    OVERLAY_CLASS, L"Screenator Input", WS_POPUP,
    g.vx, g.vy, g.vw, g.vh, g.overlay, NULL, hinst, NULL);
  SetLayeredWindowAttributes(g.inputwin, 0, 1, LWA_ALPHA);
  ShowWindow(g.inputwin, SW_HIDE);
  app_register_raw_pen();
  ui_rebuild_surfaces();
  if (!g.smoke) annotations_load();
  int isz;
  icon_geometry(&isz);
  g.iconwin = CreateWindowExW(
    WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
    ICON_CLASS, L"Screenator", WS_POPUP,
    0, 0, isz, isz, g.overlay, NULL, hinst, NULL);
  app_update_cursor();
  ui_init();
  load_icons();
  draw_warm_up_text();
  add_tray();
  pdfium_init();
  toolbar_init();
  register_global_hotkeys();
  if (g.show_icon)
    {
      ShowWindow(g.iconwin, SW_SHOWNA);
      ui_position_icon();
    }
  else
    {
      ShowWindow(g.iconwin, SW_HIDE);
    }
  app_update_tray_icon();
  ui_update_icon();
  app_render_all();
  if (open_pdf_on_start)
    {
      app_open_pdf_file(startup_pdf);
      app_render_all();
    }
  if (cmdline && strstr(cmdline, "--uitest"))
    {
      g.exit_code = run_uitest();
      app_save_and_quit();
    }
  if (!g.hide_hints && !open_pdf_on_start)
    SetTimer(g.msgwin, TIMER_HINTS, 700, NULL);
  SetTimer(g.msgwin, 6, 1200, NULL);
  if (cmdline && (strstr(cmdline, "--demo") ||
                  strstr(cmdline, "--demo-paper") || strstr(cmdline, "--demo-math")))
    {
      demo_draw();
      if (strstr(cmdline, "--demo-paper"))
        app_set_paper(PAPER_PLAIN, 0);
      SetTimer(g.msgwin, 5, 8000, NULL);
      if (strstr(cmdline, "--demo-math"))
        ui_show_math_dialog();
    }
  else if (g.smoke)
    SetTimer(g.msgwin, 5, 2000, NULL);
  if (cmdline && strstr(cmdline, "--pen-test"))
    {
      tools_activate(TOOL_PEN);
      SetTimer(g.msgwin, 5, 20000, NULL);
    }
  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
      if (msg.message == WM_KEYDOWN && msg.hwnd == g.textwin)
        {
          /* handled by the edit subclass */
        }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  destroy_buffers();
  if (asset_icon_normal) cairo_surface_destroy(asset_icon_normal);
  if (asset_icon_active && asset_icon_active != asset_icon_normal)
    cairo_surface_destroy(asset_icon_active);
  if (g.cur_erase) DestroyCursor(g.cur_erase);
  if (g.editor_font) DeleteObject(g.editor_font);
  if (g.richedit) FreeLibrary(g.richedit);
  paper_shutdown();
  pdfium_shutdown();
  if (instance_mutex) CloseHandle(instance_mutex);
  return g.exit_code;
}
