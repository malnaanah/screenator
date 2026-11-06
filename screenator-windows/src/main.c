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
#define TIMER_HINTS      4

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
static int raw_pen_available;

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

  SetForegroundWindow(hwnd);
  SetFocus(hwnd);

  if (attached)
    AttachThreadInput(fg_thread, my_thread, FALSE);
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

  if (g.dibw == g.vw && g.dibh == g.vh && g.dibbits)
    return 1;

  if (g.dibbmp) { DeleteObject(g.dibbmp); g.dibbmp = NULL; }
  if (g.dibdc) { DeleteDC(g.dibdc); g.dibdc = NULL; }
  if (g.present) { cairo_surface_destroy(g.present); g.present = NULL; }

  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = g.vw;
  bi.bmiHeader.biHeight = -g.vh;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;

  g.dibdc = CreateCompatibleDC(screen);
  g.dibbmp = CreateDIBSection(g.dibdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  ReleaseDC(NULL, screen);
  if (!g.dibbmp || !bits) return 0;

  SelectObject(g.dibdc, g.dibbmp);
  g.dibbits = bits;
  g.dibw = g.vw;
  g.dibh = g.vh;

  g.present = cairo_image_surface_create_for_data((unsigned char *) bits,
                                                  CAIRO_FORMAT_ARGB32,
                                                  g.vw, g.vh, g.vw * 4);
  return g.present != NULL;
}

void ui_rebuild_surfaces(void)
{
  int was_valid = g.backbuf != NULL;
  int ox = old_vx, oy = old_vy;

  get_virtual_screen();

  cairo_surface_t *newbuf =
    cairo_image_surface_create(CAIRO_FORMAT_ARGB32, g.vw, g.vh);

  if (was_valid && g.backbuf && (ox != g.vx || oy != g.vy))
    {
      cairo_t *cr = cairo_create(newbuf);
      cairo_set_source_surface(cr, g.backbuf, ox - g.vx, oy - g.vy);
      cairo_paint(cr);
      cairo_destroy(cr);
    }
  else if (was_valid && g.backbuf)
    {
      cairo_t *cr = cairo_create(newbuf);
      cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
      cairo_set_source_surface(cr, g.backbuf, 0, 0);
      cairo_paint(cr);
      cairo_destroy(cr);
    }

  destroy_buffers();
  g.backbuf = newbuf;
  if (!make_dib())
    {
      /* Fall back to a plain cairo surface if DIB creation failed. */
      g.present = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, g.vw, g.vh);
    }

  if (g.overlay)
    {
      SetWindowPos(g.overlay, HWND_TOPMOST, g.vx, g.vy, g.vw, g.vh,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
      ShowWindow(g.overlay, SW_SHOWNA);
    }
  if (g.inputwin)
    {
      SetWindowPos(g.inputwin, HWND_TOPMOST, g.vx, g.vy, g.vw, g.vh,
                   SWP_NOACTIVATE);
      app_raise_icon();
    }

  old_vx = g.vx; old_vy = g.vy; old_vw = g.vw; old_vh = g.vh;

  ui_position_icon();
  ui_layout();
  app_render_all();
}

void app_render(int x, int y, int w, int h)
{
  if (!g.present || !g.overlay || g.shutting_down) return;

  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (w <= 0 || h <= 0) return;
  if (x + w > g.vw) w = g.vw - x;
  if (y + h > g.vh) h = g.vh - y;

  cairo_t *cr = cairo_create(g.present);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  if (g.paper != PAPER_NONE)
    draw_paper(cr, g.paper, g.vw, g.vh);

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
    }

  if (g.text_editor_active)
    draw_text_editor(cr);

  cairo_destroy(cr);

  if (!g.dibbits)
    return;
  cairo_surface_flush(g.present);

  POINT dst = { g.vx, g.vy };
  SIZE size = { g.vw, g.vh };
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
  info.prcDirty = (x == 0 && y == 0 && w == g.vw && h == g.vh) ? NULL : &dirty;

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
  app_render(0, 0, g.vw, g.vh);
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

void app_set_click_through(int on)
{
  if (!g.inputwin) return;
  if (on)
    {
      ShowWindow(g.inputwin, SW_HIDE);
    }
  else
    {
      SetWindowPos(g.inputwin, HWND_TOPMOST, g.vx, g.vy, g.vw, g.vh,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
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
  SetWindowPos(g.overlay, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
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

static LRESULT CALLBACK wheel_hook_proc(int code, WPARAM wp, LPARAM lp)
{
  if (code == HC_ACTION)
    {
      MSLLHOOKSTRUCT *m = (MSLLHOOKSTRUCT *) lp;
      if (wp == WM_MBUTTONDOWN || wp == WM_MBUTTONUP ||
          wp == WM_RBUTTONDOWN || wp == WM_RBUTTONUP ||
          wp == WM_XBUTTONDOWN || wp == WM_XBUTTONUP)
        dlog("t=%llu hook mouse msg=%u at %ld,%ld data=%08lx extra=%llx",
             (unsigned long long) GetTickCount64(), (unsigned) wp,
             (long) m->pt.x, (long) m->pt.y,
             (unsigned long) m->mouseData,
             (unsigned long long) m->dwExtraInfo);

      if (wp == WM_MOUSEWHEEL && g.tool != TOOL_NONE &&
          !g.text_editor_active && !ui_is_modal())
        {
          tools_wheel(GET_WHEEL_DELTA_WPARAM(m->mouseData));
          return 1;              /* the wheel only changes the tool setting */
        }
    }
  return CallNextHookEx(g_wheel_hook, code, wp, lp);
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
    }
  return CallNextHookEx(g_key_hook, code, wp, lp);
}

void app_install_wheel_hook(void)
{
  if (!g_wheel_hook)
    g_wheel_hook = SetWindowsHookExW(WH_MOUSE_LL, wheel_hook_proc, g.hinst, 0);
  if (g.debug && !g_key_hook)
    g_key_hook = SetWindowsHookExW(WH_KEYBOARD_LL, key_hook_proc, g.hinst, 0);
}

void app_remove_wheel_hook(void)
{
  if (g_wheel_hook)
    {
      UnhookWindowsHookEx(g_wheel_hook);
      g_wheel_hook = NULL;
    }
  if (g_key_hook)
    {
      UnhookWindowsHookEx(g_key_hook);
      g_key_hook = NULL;
    }
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
}

void app_update_tray_icon(void)
{
  if (!g.tray_added) return;
  g.nid.uFlags = NIF_ICON | NIF_TIP;
  g.nid.hIcon = (g.tool != TOOL_NONE) ? g.hicon_active : g.hicon_normal;
  wcscpy(g.nid.szTip, L"Screenator 7.0");
  Shell_NotifyIconW(NIM_MODIFY, &g.nid);
}

void app_raise_icon(void)
{
  if (!g.iconwin) return;
  if (!g.show_icon)
    {
      ShowWindow(g.iconwin, SW_HIDE);
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

  SetWindowPos(g.iconwin, HWND_TOPMOST, x, y, size, size,
               SWP_NOACTIVATE | (g.show_icon ? SWP_SHOWWINDOW : 0));
  app_refresh_icon_image();
}

/* ------------------------------------------------------------------ */
/* paper                                                              */
/* ------------------------------------------------------------------ */

void app_set_paper(Paper p, int grab_focus)
{
  dlog("set_paper %d", (int) p);
  g.paper = p;

  if (p != PAPER_NONE)
    {
      g.paper_prev_focus = GetForegroundWindow();
      LONG_PTR ex = GetWindowLongPtrW(g.overlay, GWL_EXSTYLE);
      SetWindowLongPtrW(g.overlay, GWL_EXSTYLE, ex & ~WS_EX_NOACTIVATE);
      app_set_click_through(0);
      ShowWindow(g.overlay, SW_SHOWNA);
      SetWindowPos(g.overlay, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                   SWP_FRAMECHANGED | SWP_SHOWWINDOW);
      app_force_foreground(g.overlay);
      (void) grab_focus;
      RegisterHotKey(g.msgwin, HK_PAPER_Q, MOD_CONTROL | MOD_NOREPEAT, 'Q');
      RegisterHotKey(g.msgwin, HK_PAPER_T, MOD_CONTROL | MOD_NOREPEAT, 'T');
      RegisterHotKey(g.msgwin, 110, MOD_ALT | MOD_NOREPEAT, VK_F4);
      if (g.tool == TOOL_NONE)
        {
          RegisterHotKey(g.msgwin, HK_UNDO, MOD_CONTROL | MOD_NOREPEAT, 'Z');
          RegisterHotKey(g.msgwin, HK_REDO,
                         MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'Z');
        }
    }
  else
    {
      UnregisterHotKey(g.msgwin, HK_PAPER_Q);
      UnregisterHotKey(g.msgwin, HK_PAPER_T);
      UnregisterHotKey(g.msgwin, 110);
      {
        LONG_PTR ex = GetWindowLongPtrW(g.overlay, GWL_EXSTYLE);
        SetWindowLongPtrW(g.overlay, GWL_EXSTYLE, ex | WS_EX_NOACTIVATE);
        if (g.paper_prev_focus && IsWindow(g.paper_prev_focus) &&
            g.paper_prev_focus != g.overlay)
          SetForegroundWindow(g.paper_prev_focus);
        g.paper_prev_focus = NULL;
      }
      if (g.tool == TOOL_NONE)
        {
          UnregisterHotKey(g.msgwin, HK_UNDO);
          UnregisterHotKey(g.msgwin, HK_REDO);
          app_set_click_through(1);
        }
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

void ui_set_paper(Paper p)
{
  app_set_paper(p, 0);
}

void ui_toggle_visibility(void)
{
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
    case WM_GETDLGCODE:
      return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    case WM_CHAR:
      /* The key-down message already commits/cancels.  Swallow the generated
       * character too, otherwise the edit control beeps for Enter/Escape. */
      if (wp == VK_RETURN || wp == VK_ESCAPE)
        return 0;
      break;
    case WM_KILLFOCUS:
      dlog("edit killfocus");
      PostMessageW(g.msgwin, WM_COMMIT_TEXT, 1, 0);
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
  if (g.text_editor_active) app_end_text(0);

  dlog("begin_text %.0f,%.0f", p.x, p.y);
  g.text_editor_active = 1;
  g.text_caret_visible = 1;
  g.text_editor_click_x = (int) p.x;
  g.text_editor_click_y = (int) p.y;

  UnregisterHotKey(g.msgwin, HK_ESC);
  UnregisterHotKey(g.msgwin, HK_UNDO);
  UnregisterHotKey(g.msgwin, HK_REDO);

  editor_make_font();
  int x = (int) p.x;
  int y = (int) p.y;

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
      DestroyWindow(g.textwin);
      g.textwin = NULL;
    }
  g.text_editor_active = 0;

  if (g.tool != TOOL_NONE)
    {
      RegisterHotKey(g.msgwin, HK_ESC, MOD_NOREPEAT, VK_ESCAPE);
      RegisterHotKey(g.msgwin, HK_UNDO, MOD_CONTROL | MOD_NOREPEAT, 'Z');
      RegisterHotKey(g.msgwin, HK_REDO,
                     MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'Z');
    }

  if (commit)
    tools_text_commit(utf8, 0);

  app_render_all();
}

/* ------------------------------------------------------------------ */
/* right button                                                       */
/* ------------------------------------------------------------------ */

void app_start_right_press(Pt p)
{
  g.right_down = p;
  g.right_timer_active = 1;
  g.right_short_done = 0;
  g.right_menu_open = 0;
  SetTimer(g.msgwin, TIMER_LONG_PRESS, 600, NULL);
}

static void right_button_up(Pt p)
{
  if (g.right_timer_active)
    {
      KillTimer(g.msgwin, TIMER_LONG_PRESS);
      g.right_timer_active = 0;
      if (!g.right_menu_open && !g.right_short_done)
        tools_right_short(p);
    }
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

/* Mouse messages promoted from a pen carry this signature in the extra
 * information.  Once we have seen a real pointer message, those promoted
 * events are ignored so the pen is not handled twice. */
static int is_pen_mouse(void)
{
  ULONG_PTR extra = (ULONG_PTR) GetMessageExtraInfo();
  return (extra & 0xFFFFFF00u) == 0xFF515700u;
}

static void pen_barrel_begin(Pt p)
{
  if (g.pen_barrel_down) return;

  g.pen_barrel_down = 1;
  g.pen_barrel_eraser = 0;
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

  if (g.pen_barrel_eraser)
    {
      tools_erase_end();
    }
  else
    {
      /* Short press: one step of the selected first-button action. */
      if (g.tool == TOOL_ERASER) step_eraser_width(1);
      else if (g.tool == TOOL_MARKER) step_line_color(1);
      else if (g.tool == TOOL_BUCKET) step_fill_color(1);
      else if (g.stylus_mode == STYLUS_THICKNESS) step_pen_width(1);
      else step_line_color(1);
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
  if (g.pen_barrel_down)
    {
      KillTimer(g.msgwin, TIMER_STYLUS_HOLD);
      if (g.pen_barrel_eraser) tools_erase_end();
      g.pen_barrel_down = 0;
      g.pen_barrel_eraser = 0;
      g.pen_first_source = 0;
    }
  g.pen_first_source = source;
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

/* Classify the side buttons from Windows Ink flags.  On this pen, the
 * combination of PEN_FLAG_BARREL and the pointer's second button is button 2;
 * button 1 is expected as the pointer's second button without the barrel flag
 * (or as the middle mouse button). */
static void pen_classify(DWORD pointer_flags, DWORD pen_flags,
                         int *first, int *second)
{
  int barrel = (pen_flags & PEN_FLAG_BARREL) != 0;
  int third = (pointer_flags & POINTER_FLAG_THIRDBUTTON) != 0;
  int extra = (pointer_flags & (POINTER_FLAG_SECONDBUTTON |
                                POINTER_FLAG_FOURTHBUTTON |
                                POINTER_FLAG_FIFTHBUTTON)) != 0;
  if (first) *first = extra && !barrel && !third;
  if (second) *second = third || barrel;
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
      g.pen_second_down = 0;
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
    }
  g.suppress_mouse_until = GetTickCount64() + 400;
  release_pen_capture();
}

static void pen_pointer_event(HWND hwnd, UINT msg, WPARAM wp)
{
  UINT32 id = GET_POINTERID_WPARAM(wp);
  POINTER_INFO pi;
  POINTER_PEN_INFO pen;

  if (!GetPointerInfo(id, &pi)) return;
  if (pi.pointerType != PT_PEN) return;

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
          if (!g.pen_barrel_down && !g.pen_second_down)
            {
              g.pen_second_down = 1;
              g.menu_hold_is_stylus = 1;
              app_start_right_press(p);
            }
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
          g.pen_second_down = 0;
          right_button_up(p);
          release_pen_capture();
          app_raise_icon();
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
          g.pen_second_down = 0;
          right_button_up(p);
          release_pen_capture();
          app_raise_icon();
        }
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
      if (g.pen_first_source == 2 && !first_side && !barrel && !second &&
          GetTickCount64() - g.pen_first_begin > 200)
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
      pen_barrel_begin(p);
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
      if (!g.pen_barrel_down && !g.pen_second_down)
        {
          g.pen_second_down = 1;
          g.menu_hold_is_stylus = 1;
          app_start_right_press(p);
        }
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

  if (pointer_first && !g.pen_barrel_down && !g.pen_second_down)
    {
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
      if (g.pen_first_source == 2 && !pointer_first &&
          GetTickCount64() - g.pen_first_begin > 250)
        {
          dlog("poll first side up (pointer button)");
          pen_barrel_end();
          return 1;
        }
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
      dlog("poll second side down");
      g.pen_last = p;
      g.pen_second_down = 1;
      g.menu_hold_is_stylus = 1;
      app_start_right_press(p);
      return 1;
    }
  if (!pointer_second && g.pen_second_down)
    {
      dlog("poll second side up");
      g.pen_second_down = 0;
      right_button_up(p);
      release_pen_capture();
      app_raise_icon();
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
          else if (usages[j] == 0x45)           /* secondary barrel */
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

  if (g.tool == TOOL_NONE && g.paper == PAPER_NONE)
    {
      raw_barrel_prev = barrel;
      raw_second_prev = second;
      return;
    }
  if (ui_is_modal() || g.menu_open || g.text_editor_active)
    {
      raw_barrel_prev = barrel;
      raw_second_prev = second;
      return;
    }

  POINT pt;
  GetCursorPos(&pt);
  Pt p = { (double) pt.x, (double) pt.y };

  if (g.pen_barrel_down)
    {
      g.pen_last = p;
      if (g.pen_barrel_eraser) tools_erase_move(p);
    }

  if (barrel && !raw_barrel_prev)
    {
      if (!g.pen_barrel_down && !g.pen_second_down)
        {
          dlog("raw pen barrel down");
          g.pen_first_source = 2;
          pen_first_press(p, 2);
        }
    }
  else if (!barrel && raw_barrel_prev)
    {
      if (g.pen_barrel_down && g.pen_first_source == 2)
        {
          dlog("raw pen barrel up");
          pen_barrel_end();
        }
    }

  if (second && !raw_second_prev)
    {
      if (!g.pen_second_down && !g.pen_barrel_down)
        {
          dlog("raw pen second down");
          g.pen_last = p;
          g.pen_second_down = 1;
          g.menu_hold_is_stylus = 1;
          app_start_right_press(p);
        }
    }
  else if (!second && raw_second_prev)
    {
      if (g.pen_second_down)
        {
          dlog("raw pen second up");
          g.pen_second_down = 0;
          right_button_up(p);
          release_pen_capture();
          app_raise_icon();
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
      if (g.pen_seen && is_pen_mouse()) return 0;
      if (g.pen_pointer_active || g.pen_eraser_down) return 0;
      if (g.paper != PAPER_NONE) SetForegroundWindow(hwnd);
      if (g.text_editor_active) break;
      dlog("lbuttondown %ld,%ld tool=%d", (long) GET_X_LPARAM(lp),
           (long) GET_Y_LPARAM(lp), (int) g.tool);
      SetCapture(hwnd);
      g.button_down = 1;
      tools_pointer_down(overlay_point(lp), 0,
                         (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                         (GetKeyState(VK_SHIFT) & 0x8000) != 0);
      return 0;

    case WM_LBUTTONDBLCLK:
      if (ui_modal_outside_click(overlay_point(lp))) return 0;
      if (ui_is_modal()) return 0;
      if (g.pen_seen && is_pen_mouse()) return 0;
      if (g.pen_pointer_active || g.pen_eraser_down) return 0;
      if (g.text_editor_active) break;
      tools_double_click(overlay_point(lp));
      return 0;

    case WM_MOUSEMOVE:
      {
        if (ui_is_modal()) return 0;
        Pt p = overlay_point(lp);
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
      if (g.pen_seen && is_pen_mouse()) return 0;
      if (g.pen_pointer_active || g.pen_eraser_down) return 0;
      if (g.button_down)
        {
          dlog("lbuttonup tool=%d edit=%d", (int) g.tool, (int) g.edit);
          g.button_down = 0;
          tools_pointer_up(overlay_point(lp),
                           (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                           (GetKeyState(VK_SHIFT) & 0x8000) != 0);
          ReleaseCapture();
        }
      return 0;

    case WM_RBUTTONDOWN:
      if (ui_modal_outside_click(overlay_point(lp))) return 0;
      if (ui_is_modal()) return 0;
      if (g.pen_second_down) return 0;
      if (g.pen_barrel_down) return 0;   /* promoted first-side event */
      if (g.text_editor_active) break;
      {
        int pen = is_pen_mouse();
        dlog("right down pen=%d seen=%d second=%d barrel=%d", pen,
             g.pen_seen, g.pen_second_down, g.pen_barrel_down);
        if (pen && (g.pen_barrel_down || g.pen_eraser_down ||
                    GetTickCount64() - g.pen_barrel_release < 400 ||
                    GetTickCount64() - g.pen_eraser_release < 400))
          return 0;
        g.menu_hold_is_stylus = pen;
        if (pen) dlog("pen second side down");
        app_start_right_press(overlay_point(lp));
      }
      return 0;

    case WM_RBUTTONUP:
      if (g.pen_second_down) return 0;
      if (is_pen_mouse() && g.pen_barrel_down)
        {
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
      right_button_up(overlay_point(lp));
      return 0;

    case WM_MBUTTONDOWN:
      if (ui_modal_outside_click(overlay_point(lp))) return 0;
      if (ui_is_modal()) return 0;
      if (g.text_editor_active) return 0;
      dlog("mbutton down pen=%d seen=%d", is_pen_mouse(), g.pen_seen);
      if (is_pen_mouse() || g.pen_seen)
        {
          dlog("pen middle -> first side button");
          pen_first_press(overlay_point(lp), 1);
          return 0;
        }
      if (g.pen_pointer_active || g.pen_eraser_down) return 0;
      SetCapture(hwnd);
      g.button_down = 1;
      tools_pointer_down(overlay_point(lp), 1, 0, 0);
      return 0;

    case WM_MBUTTONUP:
      dlog("mbutton up pen=%d seen=%d barrel=%d", is_pen_mouse(),
           g.pen_seen, g.pen_barrel_down);
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
          tools_pointer_up(overlay_point(lp), 0, 0);
          ReleaseCapture();
        }
      return 0;

    case WM_XBUTTONDOWN:
      if (ui_modal_outside_click(overlay_point(lp))) return TRUE;
      if (ui_is_modal() || g.text_editor_active) return TRUE;
      if (g.pen_barrel_down) return TRUE;
      if (!is_pen_mouse() && !g.pen_seen) return TRUE;
      dlog("xbutton down %u pen=%d seen=%d",
           (unsigned) GET_XBUTTON_WPARAM(wp), is_pen_mouse(), g.pen_seen);
      g.menu_hold_is_stylus = 1;
      app_start_right_press(overlay_point(lp));
      return TRUE;

    case WM_XBUTTONUP:
      if (!is_pen_mouse() && !g.pen_seen) return TRUE;
      right_button_up(overlay_point(lp));
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
      if (!g.text_editor_active)
        pen_pointer_event(hwnd, msg, wp);
      return 0;

    case WM_POINTERUPDATE:
    case WM_POINTERUP:
      if (!ui_is_modal() && !g.text_editor_active)
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
      if (ui_is_modal()) return 0;
      tools_wheel(GET_WHEEL_DELTA_WPARAM(wp));
      return 0;

    case WM_KEYDOWN:
      if (g.paper != PAPER_NONE)
        {
          int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
          int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
          if (ctrl && (wp == 'Q' || wp == 'q')) app_exit_paper();
          else if (ctrl && (wp == 'T' || wp == 't')) ui_toggle_visibility();
          else if (ctrl && (wp == 'Z' || wp == 'z'))
            { if (shift) tools_redo(); else tools_undo(); }
          else if (wp == VK_F4 && (GetKeyState(VK_MENU) & 0x8000))
            app_save_and_quit();
          return 0;
        }
      break;

    case WM_SYSKEYDOWN:
      /* Alt+F4 arrives as WM_SYSKEYDOWN, not WM_KEYDOWN.  While the screen
       * paper owns the keyboard focus, handle it here as well as through the
       * registered hotkey. */
      if (g.paper != PAPER_NONE && wp == VK_F4 &&
          (GetKeyState(VK_MENU) & 0x8000))
        {
          app_save_and_quit();
          return 0;
        }
      break;

    case WM_CONTEXTMENU:
      return 0;

    case WM_SETCURSOR:
      SetCursor(tool_cursor());
      return TRUE;

    case WM_DISPLAYCHANGE:
      app_clear_menu_ghost();
      ui_rebuild_surfaces();
      return 0;

    case WM_ERASEBKGND:
      return 1;
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
      tools_wheel(GET_WHEEL_DELTA_WPARAM(wp));
      return 0;

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
    case WM_TIMER:
      if (wp == TIMER_TOOL_DELAY)
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
            ui_toggle_visibility();
          }
        else if (id == 110)
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

    case WM_DISPLAYCHANGE:
      app_clear_menu_ghost();
      ui_rebuild_surfaces();
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

void app_save_and_quit(void)
{
  if (g.shutting_down) return;
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

  UnregisterHotKey(g.msgwin, HK_PEN);
  UnregisterHotKey(g.msgwin, HK_TEXT);
  UnregisterHotKey(g.msgwin, HK_ARROW);
  UnregisterHotKey(g.msgwin, HK_ICON);
  UnregisterHotKey(g.msgwin, HK_QUICK);
  UnregisterHotKey(g.msgwin, HK_ESC);
  UnregisterHotKey(g.msgwin, HK_UNDO);
  UnregisterHotKey(g.msgwin, HK_REDO);
  UnregisterHotKey(g.msgwin, HK_PAPER_Q);
  UnregisterHotKey(g.msgwin, HK_PAPER_T);
  UnregisterHotKey(g.msgwin, 110);

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
  wcscpy(g.nid.szTip, L"Screenator 7.0");
  if (Shell_NotifyIconW(NIM_ADD, &g.nid))
    g.tray_added = 1;
}

static void register_global_hotkeys(void)
{
  BOOL a = RegisterHotKey(g.msgwin, HK_PEN, MOD_NOREPEAT, VK_F8);
  BOOL b = RegisterHotKey(g.msgwin, HK_TEXT, MOD_NOREPEAT, VK_F7);
  BOOL c = RegisterHotKey(g.msgwin, HK_ARROW, MOD_NOREPEAT, VK_F6);
  BOOL d = RegisterHotKey(g.msgwin, HK_ICON, MOD_NOREPEAT, VK_F9);
  BOOL e = RegisterHotKey(g.msgwin, HK_QUICK, MOD_NOREPEAT, VK_F10);
  dlog("hotkeys F8=%d F7=%d F6=%d F9=%d F10=%d err=%lu",
       a, b, c, d, e, (unsigned long) GetLastError());
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

  /* Stylus side button 1 (reported as the middle button on many pens):
   * a short press must step colour/thickness, and a long press must switch to
   * the temporary eraser. */
  {
    tools_activate(TOOL_PEN);
    g.pen_seen = 1;
    int before = line_color_index();
    unsigned before_width = g.pen_width;
    SendMessageW(g.inputwin, WM_MBUTTONDOWN, MK_MBUTTON,
                 MAKELPARAM(1100 - g.vx, 900 - g.vy));
    SendMessageW(g.inputwin, WM_MBUTTONUP, 0,
                 MAKELPARAM(1100 - g.vx, 900 - g.vy));
    int stepped = line_color_index() != before ||
                  g.pen_width != before_width;

    Pt ep = { 1100, 900 };
    tools_pointer_down(ep, 0, 0, 0);
    tools_pointer_move((Pt) { 1120, 910 }, 0, 0);
    tools_pointer_up((Pt) { 1120, 910 }, 0, 0);
    int drawn = count_alpha(g.backbuf);

    g.pen_last = ep;
    SendMessageW(g.inputwin, WM_MBUTTONDOWN, MK_MBUTTON,
                 MAKELPARAM(1100 - g.vx, 900 - g.vy));
    SendMessageW(g.msgwin, WM_TIMER, TIMER_STYLUS_HOLD, 0);
    int erasing = g.pen_barrel_eraser;
    SendMessageW(g.inputwin, WM_MBUTTONUP, 0,
                 MAKELPARAM(1100 - g.vx, 900 - g.vy));
    int erased = count_alpha(g.backbuf) < drawn;

    if (!stepped || !erasing || !erased)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "stylus: first side failed "
                                         "(step=%d erase=%d/%d)\n",
                                         stepped, erasing, erased); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "stylus: first side short/long press ok\n");

    int f = 0, s = 0;
    pen_classify(POINTER_FLAG_SECONDBUTTON, 0, &f, &s);
    int button1_ok = f && !s;
    pen_classify(POINTER_FLAG_SECONDBUTTON, PEN_FLAG_BARREL, &f, &s);
    int button2_ok = !f && s;
    if (!button1_ok || !button2_ok)
      { ok = 0; pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                         "stylus: flag classification failed "
                                         "(b1=%d b2=%d)\n",
                                         button1_ok, button2_ok); }
    else pos += (size_t) snprintf(log + pos, sizeof(log) - pos,
                                  "stylus: button flag classification ok\n");
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
                fabs(g.marker_opacity - 0.7) < 0.01;

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
  fs.text = _strdup("Screenator 7.0");
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
  fs.text = _strdup("Screenator 7.0 for Windows");
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

  HANDLE instance_mutex = CreateMutexW(NULL, TRUE,
                                       L"Screenator.Windows.SingleInstance");
  if (instance_mutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
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
   * a uniform, almost invisible surface that does catch them. */
  g.inputwin = CreateWindowExW(
    WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
    OVERLAY_CLASS, L"Screenator Input", WS_POPUP,
    g.vx, g.vy, g.vw, g.vh, NULL, NULL, hinst, NULL);
  SetLayeredWindowAttributes(g.inputwin, 0, 1, LWA_ALPHA);
  ShowWindow(g.inputwin, SW_HIDE);
  app_register_raw_pen();

  ui_rebuild_surfaces();
  if (!g.smoke) annotations_load();

  int isz;
  icon_geometry(&isz);
  g.iconwin = CreateWindowExW(
    WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
    ICON_CLASS, L"Screenator", WS_POPUP,
    0, 0, isz, isz, NULL, NULL, hinst, NULL);

  app_update_cursor();
  ui_init();
  load_icons();
  add_tray();

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

  if (cmdline && strstr(cmdline, "--uitest"))
    {
      g.exit_code = run_uitest();
      app_save_and_quit();
    }

  if (!g.hide_hints)
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

  if (instance_mutex) CloseHandle(instance_mutex);
  return g.exit_code;
}
