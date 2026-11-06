/*
 * Screenator for Windows -- menus, dialogs, tray and quick tool menu.
 * GPL-2.0-or-later.  See app.h.
 */

#include "app.h"
#include <richedit.h>
#include <stdarg.h>
#include <wchar.h>

#define ID_HINTS_CHECK   3001
#define ID_HINTS_CLOSE   3002

#define ID_MATH_RECT     4001
#define ID_MATH_POLAR    4002
#define ID_MATH_EXPR     4003
#define ID_MATH_FROM     4004
#define ID_MATH_TO       4005
#define ID_MATH_PRESET   4006
#define ID_MATH_ERR      4007

#define ID_POS_TR        5001
#define ID_POS_TL        5002
#define ID_POS_BR        5003
#define ID_POS_BL        5004

#define ID_ABOUT_CLOSE   6001

static HFONT ui_font;
static int modal_depth;

static const wchar_t *QUICK_CLASS = L"ScreenatorQuick";
static const wchar_t *HINTS_CLASS = L"ScreenatorHints";
static const wchar_t *MATH_CLASS = L"ScreenatorMath";
static const wchar_t *POS_CLASS = L"ScreenatorIconPos";
static const wchar_t *ABOUT_CLASS = L"ScreenatorAbout";

/* ------------------------------------------------------------------ */
/* small helpers                                                      */
/* ------------------------------------------------------------------ */

static wchar_t *w_utf8(const char *s)
{
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  wchar_t *w = (wchar_t *) malloc((size_t) n * sizeof(wchar_t));
  if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}

static char *u_wide(const wchar_t *w)
{
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  char *s = (char *) malloc((size_t) n);
  if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
  return s;
}

static void set_ui_font(HWND h)
{
  if (ui_font) SendMessageW(h, WM_SETFONT, (WPARAM) ui_font, TRUE);
}

static void center_window(HWND h, int w, int hgt)
{
  RECT wa;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  int x = wa.left + (wa.right - wa.left - w) / 2;
  int y = wa.top + (wa.bottom - wa.top - hgt) / 2;
  SetWindowPos(h, NULL, x, y, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
}

static HHOOK modal_mouse_hook;
static HHOOK font_mouse_hook;

static LRESULT CALLBACK font_mouse_proc(int code, WPARAM wp, LPARAM lp)
{
  if (code == HC_ACTION && g.paper != PAPER_NONE &&
      (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN ||
       wp == WM_MBUTTONDOWN || wp == WM_XBUTTONDOWN))
    {
      MSLLHOOKSTRUCT *m = (MSLLHOOKSTRUCT *) lp;
      HWND dlg = GetForegroundWindow();
      wchar_t cls[64];

      if (!dlg || !GetClassNameW(dlg, cls, 64) ||
          wcscmp(cls, L"#32770") != 0)
        return CallNextHookEx(font_mouse_hook, code, wp, lp);

      DWORD pid = 0;
      GetWindowThreadProcessId(dlg, &pid);
      if (pid != GetCurrentProcessId())
        return CallNextHookEx(font_mouse_hook, code, wp, lp);

      RECT r;
      if (!GetWindowRect(dlg, &r))
        return CallNextHookEx(font_mouse_hook, code, wp, lp);

      int inside = m->pt.x >= r.left && m->pt.x < r.right &&
                   m->pt.y >= r.top && m->pt.y < r.bottom;
      if (!inside)
        {
          HWND under = WindowFromPoint(m->pt);
          HWND root = under ? GetAncestor(under, GA_ROOT) : NULL;
          int popup = 0;

          if (root == dlg)
            popup = 1;
          else if (under)
            {
              wchar_t ucls[64];
              DWORD upid = 0;
              GetWindowThreadProcessId(under, &upid);
              if (upid == GetCurrentProcessId() &&
                  GetClassNameW(under, ucls, 64) &&
                  (!wcscmp(ucls, L"ComboLBox") ||
                   !wcscmp(ucls, L"#32768")))
                popup = 1;
            }

          if (!popup)
            {
              PostMessageW(dlg, WM_COMMAND, IDCANCEL, 0);
              return 1;
            }
        }
    }
  return CallNextHookEx(font_mouse_hook, code, wp, lp);
}

static LRESULT CALLBACK modal_mouse_proc(int code, WPARAM wp, LPARAM lp)
{
  if (code == HC_ACTION && g.paper != PAPER_NONE &&
      g.modal_outside_close && g.modal_dialog &&
      (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN ||
       wp == WM_MBUTTONDOWN || wp == WM_XBUTTONDOWN))
    {
      MSLLHOOKSTRUCT *m = (MSLLHOOKSTRUCT *) lp;
      Pt p = { (double) m->pt.x, (double) m->pt.y };
      if (ui_modal_outside_click(p))
        return 1;                /* swallow the click behind the dialog */
    }
  return CallNextHookEx(modal_mouse_hook, code, wp, lp);
}

static void run_modal(HWND hwnd, int outside_close)
{
  MSG msg;
  HWND prev_dialog = g.modal_dialog;
  int prev_flag = g.modal_outside_close;

  modal_depth++;
  g.modal_dialog = hwnd;
  g.modal_outside_close = outside_close;
  if (outside_close && !modal_mouse_hook)
    modal_mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, modal_mouse_proc,
                                         g.hinst, 0);
  ShowWindow(hwnd, SW_SHOW);
  app_force_foreground(hwnd);
  while (IsWindow(hwnd) && GetMessageW(&msg, NULL, 0, 0) > 0)
    {
      if (!IsDialogMessageW(hwnd, &msg))
        {
          TranslateMessage(&msg);
          DispatchMessageW(&msg);
        }
    }
  if (outside_close && modal_mouse_hook)
    {
      UnhookWindowsHookEx(modal_mouse_hook);
      modal_mouse_hook = NULL;
    }
  g.modal_dialog = prev_dialog;
  g.modal_outside_close = prev_flag;
  modal_depth--;
}

int ui_modal_outside_click(Pt p)
{
  if (g.paper == PAPER_NONE || !g.modal_outside_close || !g.modal_dialog)
    return 0;
  if (!IsWindow(g.modal_dialog))
    {
      g.modal_dialog = NULL;
      return 0;
    }

  RECT r;
  if (!GetWindowRect(g.modal_dialog, &r)) return 0;
  if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom)
    return 0;

  PostMessageW(g.modal_dialog, WM_CLOSE, 0, 0);
  return 1;
}

int ui_is_modal(void)
{
  return modal_depth > 0;
}

void ui_message(const char *text, const char *title, UINT flags)
{
  wchar_t *wt = w_utf8(text);
  wchar_t *wc = w_utf8(title);
  MessageBoxW(g.overlay, wt ? wt : L"", wc ? wc : L"Screenator", flags);
  free(wt);
  free(wc);
}

/* ------------------------------------------------------------------ */
/* icons for menus                                                    */
/* ------------------------------------------------------------------ */

typedef struct
{
  char name[64];
  int size;
  HBITMAP bmp;
} IconCache;

static IconCache icon_cache[64];
static int icon_cache_count;

HBITMAP ui_load_icon_bitmap(const char *name, int size)
{
  for (int i = 0; i < icon_cache_count; i++)
    if (icon_cache[i].size == size && !strcmp(icon_cache[i].name, name))
      return icon_cache[i].bmp;

  wchar_t wname[128];
  MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 128);
  wchar_t rel[192];
  _snwprintf(rel, 192, L"icons\\%s", wname);
  wchar_t path[MAX_PATH];
  char utf8[MAX_PATH * 3];
  app_asset_path(path, MAX_PATH, rel);
  WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);

  cairo_surface_t *img = cairo_image_surface_create_from_png(utf8);
  HBITMAP bmp = NULL;

  if (cairo_surface_status(img) == CAIRO_STATUS_SUCCESS)
    {
      int iw = cairo_image_surface_get_width(img);
      int ih = cairo_image_surface_get_height(img);

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
      bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
      if (bmp && bits)
        {
          cairo_surface_t *surf = cairo_image_surface_create_for_data(
            (unsigned char *) bits, CAIRO_FORMAT_ARGB32, size, size, size * 4);
          cairo_t *cr = cairo_create(surf);

          COLORREF bg = GetSysColor(COLOR_MENU);
          cairo_set_source_rgb(cr, GetRValue(bg) / 255.0,
                               GetGValue(bg) / 255.0, GetBValue(bg) / 255.0);
          cairo_paint(cr);
          cairo_scale(cr, (double) size / iw, (double) size / ih);
          cairo_set_source_surface(cr, img, 0, 0);
          cairo_paint(cr);
          cairo_surface_flush(surf);
          cairo_destroy(cr);
          cairo_surface_destroy(surf);
        }
      DeleteDC(dc);
      ReleaseDC(NULL, screen);
    }
  cairo_surface_destroy(img);

  if (icon_cache_count < 64)
    {
      IconCache *c = &icon_cache[icon_cache_count++];
      strncpy(c->name, name, sizeof(c->name) - 1);
      c->name[sizeof(c->name) - 1] = 0;
      c->size = size;
      c->bmp = bmp;
    }
  return bmp;
}

static void menu_icon(HMENU menu, int pos, const char *name)
{
  HBITMAP bmp = ui_load_icon_bitmap(name, 16);
  if (bmp)
    SetMenuItemBitmaps(menu, pos, MF_BYPOSITION, bmp, bmp);
}

static void add_item(HMENU menu, UINT id, const wchar_t *label,
                     const char *icon, int active)
{
  AppendMenuW(menu, MF_STRING, id, label);
  int pos = GetMenuItemCount(menu) - 1;
  if (icon) menu_icon(menu, pos, icon);
  if (active) SetMenuDefaultItem(menu, id, FALSE);
}

static HMENU add_sub(HMENU parent, const wchar_t *label, const char *icon)
{
  HMENU sub = CreatePopupMenu();
  AppendMenuW(parent, MF_POPUP | MF_STRING, (UINT_PTR) sub, label);
  int pos = GetMenuItemCount(parent) - 1;
  if (icon) menu_icon(parent, pos, icon);
  return sub;
}

static void add_radio(HMENU menu, UINT id, const wchar_t *label, int checked)
{
  AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : 0), id, label);
}

/* ------------------------------------------------------------------ */
/* commands                                                           */
/* ------------------------------------------------------------------ */

static void tool_command(Tool t)
{
  if (t == TOOL_MATH) ui_show_math_dialog();
  else tools_request(t);
}

static void menu_command(int id)
{
  dlog("menu command %d", id);
  if (id == CMD_PEN) { tool_command(TOOL_PEN); return; }
  if (id == CMD_TEXT) { tool_command(TOOL_TEXT); return; }
  if (id == CMD_ERASER) { tool_command(TOOL_ERASER); return; }
  if (id == CMD_MARKER) { tool_command(TOOL_MARKER); return; }
  if (id == CMD_LINE) { tool_command(TOOL_LINE); return; }
  if (id == CMD_SPLINE) { tool_command(TOOL_SPLINE); return; }
  if (id == CMD_POLYGON) { tool_command(TOOL_POLYGON); return; }
  if (id == CMD_TRIANGLE) { tool_command(TOOL_TRIANGLE); return; }
  if (id == CMD_ARROW) { tool_command(TOOL_ARROW); return; }
  if (id == CMD_ELLIPSE) { tool_command(TOOL_ELLIPSE); return; }
  if (id == CMD_RRECT) { tool_command(TOOL_RRECT); return; }
  if (id == CMD_BUCKET) { tool_command(TOOL_BUCKET); return; }
  if (id == CMD_MATH) { ui_show_math_dialog(); return; }
  if (id == CMD_BARS) { tool_command(TOOL_BARS); return; }
  if (id == CMD_RANDOM) { tool_command(TOOL_RANDOM); return; }

  if (id >= CMD_DELAY_0 && id <= CMD_DELAY_11)
    {
      g.tool_delay_seconds = g_delays[id - CMD_DELAY_0];
      ui_refresh_tray();
      return;
    }
  if (id >= CMD_WIDTH_1 && id <= CMD_WIDTH_48)
    {
      apply_pen_width(g_widths[id - CMD_WIDTH_1]);
      return;
    }
  if (id >= CMD_COLOR_BASE && id < CMD_COLOR_BASE + 8)
    {
      apply_line_color_index(id - CMD_COLOR_BASE);
      return;
    }
  if (id == CMD_COLOR_CUSTOM) { ui_choose_color(0); return; }
  if (id >= CMD_FILL_BASE && id < CMD_FILL_BASE + 9)
    {
      apply_fill_color_index(id - CMD_FILL_BASE);
      return;
    }
  if (id == CMD_FILL_CUSTOM) { ui_choose_color(1); return; }
  if (id >= CMD_PAPER_NONE && id <= CMD_PAPER_GRAPH)
    {
      app_set_paper((Paper) (id - CMD_PAPER_NONE), 0);
      return;
    }
  if (id >= CMD_ERASER_20 && id <= CMD_ERASER_120)
    {
      apply_eraser_width(g_eraser_widths[id - CMD_ERASER_20]);
      return;
    }
  if (id >= CMD_OPACITY_10 && id <= CMD_OPACITY_90)
    {
      g.opacity = (10.0 * (double) (id - CMD_OPACITY_10 + 1)) / 100.0;
      g.pen_color.a = g.opacity;
      tools_set_frame_color();
      ui_apply_pen();
      app_render_all();
      ui_update_icon();
      ui_refresh_tray();
      return;
    }
  if (id >= CMD_ICON_16 && id <= CMD_ICON_256)
    {
      static const unsigned vals[7] = { 16, 24, 32, 48, 64, 128, 256 };
      g.icon_size = vals[id - CMD_ICON_16];
      ui_position_icon();
      ui_update_icon();
      config_save();
      return;
    }

  switch (id)
    {
    case CMD_UNDO: tools_undo(); break;
    case CMD_REDO: tools_redo(); break;
    case CMD_CLEAR: tools_clear(); break;
    case CMD_VISIBILITY: ui_toggle_visibility(); break;
    case CMD_SHOW_ICON: ui_set_icon_visible(!g.show_icon); break;
    case CMD_SHOW_HINTS: ui_show_hints(0); break;
    case CMD_SAVE_ON_EXIT:
      g.save_on_exit = !g.save_on_exit;
      config_save();
      break;
    case CMD_FONT: ui_choose_font(); break;
    case CMD_SHOW_INDICATOR:
      if (g.show_icon)
        {
          g.show_indicator = !g.show_indicator;
          ui_update_icon();
          config_save();
        }
      break;
    case CMD_ICON_POS: ui_show_icon_position(); break;
    case CMD_RESET: ui_reset_settings(); break;
    case CMD_ABOUT: ui_show_about(); break;
    case CMD_QUIT: app_save_and_quit(); break;
    case CMD_WHEEL_COLOR: g.wheel_mode = WHEEL_COLOR; break;
    case CMD_WHEEL_THICKNESS: g.wheel_mode = WHEEL_THICKNESS; break;
    case CMD_STYLUS_COLOR: g.stylus_mode = STYLUS_COLOR; config_save(); break;
    case CMD_STYLUS_THICKNESS: g.stylus_mode = STYLUS_THICKNESS; config_save(); break;
    default:
      if (id >= CMD_MARKER_10 && id <= CMD_MARKER_90)
        g.marker_opacity =
          (10.0 * (double) (id - CMD_MARKER_10 + 1)) / 100.0;
      break;
    }
  config_save();
  app_render_all();
  ui_refresh_tray();
}

/* ------------------------------------------------------------------ */
/* main menu                                                          */
/* ------------------------------------------------------------------ */

static HMENU build_menu(void)
{
  HMENU m = CreatePopupMenu();
  HMENU sub;
  int i;

  add_item(m, CMD_PEN, L"Pen\tF8", "pen.png", g.tool == TOOL_PEN);
  add_item(m, CMD_TEXT, L"Text\tF7", "text.png", g.tool == TOOL_TEXT);
  add_item(m, CMD_ERASER, L"Eraser", "eraser.png", g.tool == TOOL_ERASER);
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);

  sub = add_sub(m, L"Shapes", "shapes.png");
  add_item(sub, CMD_MARKER, L"Marker", "marker.png", g.tool == TOOL_MARKER);
  add_item(sub, CMD_LINE, L"Line", "free-line.png", g.tool == TOOL_LINE);
  add_item(sub, CMD_SPLINE, L"Spline", "spline.png", g.tool == TOOL_SPLINE);
  add_item(sub, CMD_POLYGON, L"Polygon", "polygon.png", g.tool == TOOL_POLYGON);
  add_item(sub, CMD_TRIANGLE, L"Triangle", "triangle.png", g.tool == TOOL_TRIANGLE);
  add_item(sub, CMD_ARROW, L"Arrow\tF6", "arrow.png", g.tool == TOOL_ARROW);
  add_item(sub, CMD_ELLIPSE, L"Ellipse", "ellipse.png", g.tool == TOOL_ELLIPSE);
  add_item(sub, CMD_RRECT, L"Rounded Rectangle", "rounded-rectangle.png",
           g.tool == TOOL_RRECT);
  add_item(sub, CMD_BUCKET, L"Paint Bucket", "bucket.png", g.tool == TOOL_BUCKET);
  add_item(sub, CMD_MATH, L"Math Function", "math.png", g.tool == TOOL_MATH);
  add_item(sub, CMD_BARS, L"Parallel Lines", "bars.png", g.tool == TOOL_BARS);
  add_item(sub, CMD_RANDOM, L"Random Signal", "random-signal.png",
           g.tool == TOOL_RANDOM);

  /* Tool activation delay. */
  sub = CreatePopupMenu();
  {
    int checked = -1;
    for (i = 0; i < 9; i++)
      {
        wchar_t label[16];
        _snwprintf(label, 16, L"%u s", g_delays[i]);
        add_radio(sub, CMD_DELAY_0 + i, label,
                  g.tool_delay_seconds == g_delays[i]);
        if (g.tool_delay_seconds == g_delays[i]) checked = i;
      }
    if (checked >= 0)
      CheckMenuRadioItem(sub, CMD_DELAY_0, CMD_DELAY_11,
                         CMD_DELAY_0 + checked, MF_BYCOMMAND);
  }
  AppendMenuW(m, MF_POPUP | MF_STRING, (UINT_PTR) sub,
              L"Tool Activation Delay");

  /* Line thickness. */
  sub = CreatePopupMenu();
  {
    int closest = 0, best = 100000, checked = 0;
    static const wchar_t *labels[9] = {
      L"1 px", L"2 px", L"3 px", L"4 px", L"5 px", L"6 px", L"12 px",
      L"24 px", L"48 px"
    };
    for (i = 0; i < 9; i++)
      {
        int d = abs((int) g.pen_width - (int) g_widths[i]);
        if (d < best) { best = d; closest = i; }
      }
    for (i = 0; i < 9; i++)
      {
        add_radio(sub, CMD_WIDTH_1 + i, labels[i], i == closest);
        if (i == closest) checked = i;
      }
    CheckMenuRadioItem(sub, CMD_WIDTH_1, CMD_WIDTH_48,
                       CMD_WIDTH_1 + checked, MF_BYCOMMAND);
  }
  AppendMenuW(m, MF_POPUP | MF_STRING, (UINT_PTR) sub, L"Line Thickness");

  /* Line colour. */
  sub = CreatePopupMenu();
  {
    int idx = line_color_index();
    for (i = 0; i < 8; i++)
      {
        wchar_t wl[32];
        MultiByteToWideChar(CP_UTF8, 0, g_line_colors[i].label, -1, wl, 32);
        add_radio(sub, CMD_COLOR_BASE + i, wl, idx == i);
      }
    AppendMenuW(sub, MF_SEPARATOR, 0, NULL);
    add_radio(sub, CMD_COLOR_CUSTOM, L"Custom...", idx == 8);
    CheckMenuRadioItem(sub, CMD_COLOR_BASE, CMD_COLOR_CUSTOM,
                       idx == 8 ? CMD_COLOR_CUSTOM : CMD_COLOR_BASE + idx,
                       MF_BYCOMMAND);
  }
  AppendMenuW(m, MF_POPUP | MF_STRING, (UINT_PTR) sub, L"Line Color");

  /* Shape fill colour. */
  sub = CreatePopupMenu();
  {
    int idx = fill_color_index();
    for (i = 0; i < 9; i++)
      {
        wchar_t wl[32];
        MultiByteToWideChar(CP_UTF8, 0, g_fill_colors[i].label, -1, wl, 32);
        add_radio(sub, CMD_FILL_BASE + i, wl, idx == i);
      }
    AppendMenuW(sub, MF_SEPARATOR, 0, NULL);
    add_radio(sub, CMD_FILL_CUSTOM, L"Custom...", idx == 9);
    CheckMenuRadioItem(sub, CMD_FILL_BASE, CMD_FILL_CUSTOM,
                       idx == 9 ? CMD_FILL_CUSTOM : CMD_FILL_BASE + idx,
                       MF_BYCOMMAND);
  }
  AppendMenuW(m, MF_POPUP | MF_STRING, (UINT_PTR) sub, L"Shapes Fill Color");

  /* Screen paper. */
  sub = add_sub(m, L"Screen Paper", "paper.png");
  {
    static const wchar_t *names[4] = { L"None", L"Plain", L"Ruled", L"Graph" };
    for (i = 0; i < 4; i++) add_radio(sub, CMD_PAPER_NONE + i, names[i],
                                      (int) g.paper == i);
    CheckMenuRadioItem(sub, CMD_PAPER_NONE, CMD_PAPER_GRAPH,
                       CMD_PAPER_NONE + (int) g.paper, MF_BYCOMMAND);
  }

  /* Eraser size. */
  sub = CreatePopupMenu();
  {
    static const wchar_t *labels[4] = { L"20 px", L"40 px", L"60 px", L"120 px" };
    int checked = 2;
    for (i = 0; i < 4; i++)
      {
        add_radio(sub, CMD_ERASER_20 + i, labels[i],
                  g.eraser_width == g_eraser_widths[i]);
        if (g.eraser_width == g_eraser_widths[i]) checked = i;
      }
    CheckMenuRadioItem(sub, CMD_ERASER_20, CMD_ERASER_120,
                       CMD_ERASER_20 + checked, MF_BYCOMMAND);
  }
  AppendMenuW(m, MF_POPUP | MF_STRING, (UINT_PTR) sub, L"Eraser Size");

  /* Opacity: 10% to 90% in steps of 10. */
  sub = CreatePopupMenu();
  {
    static const int vals[9] = { 10, 20, 30, 40, 50, 60, 70, 80, 90 };
    int checked = -1;
    for (i = 0; i < 9; i++)
      {
        wchar_t label[8];
        _snwprintf(label, 8, L"%d%%", vals[i]);
        int is = fabs(g.opacity * 100.0 - vals[i]) < 1.0;
        add_radio(sub, CMD_OPACITY_10 + i, label, is);
        if (is) checked = i;
      }
    if (checked >= 0)
      CheckMenuRadioItem(sub, CMD_OPACITY_10, CMD_OPACITY_90,
                         CMD_OPACITY_10 + checked, MF_BYCOMMAND);
  }
  AppendMenuW(m, MF_POPUP | MF_STRING, (UINT_PTR) sub, L"Opacity");

  /* Floating icon size. */
  sub = CreatePopupMenu();
  {
    static const unsigned vals[7] = { 16, 24, 32, 48, 64, 128, 256 };
    static const wchar_t *labels[7] = {
      L"16x16", L"24x24", L"32x32", L"48x48", L"64x64",
      L"128x128", L"256x256"
    };
    int checked = 3;
    for (i = 0; i < 7; i++)
      {
        add_radio(sub, CMD_ICON_16 + i, labels[i], g.icon_size == vals[i]);
        if (g.icon_size == vals[i]) checked = i;
      }
    CheckMenuRadioItem(sub, CMD_ICON_16, CMD_ICON_256,
                       CMD_ICON_16 + checked, MF_BYCOMMAND);
  }
  AppendMenuW(m, MF_POPUP | MF_STRING, (UINT_PTR) sub, L"Floating Icon Size");

  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  add_item(m, CMD_UNDO, L"Undo\tCtrl+Z", "undo.png", 0);
  add_item(m, CMD_REDO, L"Redo\tCtrl+Shift+Z", "redo.png", 0);
  add_item(m, CMD_CLEAR, L"Clear Screen", "trash.png", 0);
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  add_item(m, CMD_VISIBILITY, L"Toggle Visibility", "hide.png", 0);
  AppendMenuW(m, MF_STRING | (g.show_icon ? MF_CHECKED : 0), CMD_SHOW_ICON,
              L"Show Floating Icon\tF9");
  add_item(m, CMD_SHOW_HINTS, L"Show Hints", "hint.png", 0);

  /* Settings. */
  sub = add_sub(m, L"Settings", "font-settings.png");
  AppendMenuW(sub, MF_STRING | (g.save_on_exit ? MF_CHECKED : 0),
              CMD_SAVE_ON_EXIT, L"Save annotations on exit");
  add_item(sub, CMD_FONT, L"Change Font", "font-settings.png", 0);
  {
    UINT flags = MF_STRING | (g.show_indicator ? MF_CHECKED : 0);
    if (!g.show_icon) flags |= MF_GRAYED;
    AppendMenuW(sub, flags, CMD_SHOW_INDICATOR, L"Show Settings Indicator");
  }
  add_item(sub, CMD_ICON_POS, L"Floating Icon Position...", "shapes.png", 0);
  AppendMenuW(sub, MF_SEPARATOR, 0, NULL);
  add_item(sub, CMD_RESET, L"Reset Settings", "reset-settings.png", 0);

  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  add_item(m, CMD_ABOUT, L"About", "screenator.png", 0);
  add_item(m, CMD_QUIT, L"Quit", "trash.png", 0);
  return m;
}

static HWND menu_owner(void)
{
  HWND fg = GetForegroundWindow();
  if (fg && GetWindowThreadProcessId(fg, NULL) == GetCurrentThreadId())
    return fg;
  if (g.iconwin && IsWindowVisible(g.iconwin)) return g.iconwin;
  return g.overlay;
}

/* Popup menus dismiss properly only when their owner is the foreground
 * window.  Our overlay and icon are normally non-activating, so borrow
 * activation for the duration of the menu. */
static int menu_owner_activate(HWND owner, LONG_PTR *saved)
{
  if (!owner) return 0;
  *saved = GetWindowLongPtrW(owner, GWL_EXSTYLE);
  if (*saved & WS_EX_NOACTIVATE)
    {
      SetWindowLongPtrW(owner, GWL_EXSTYLE, *saved & ~WS_EX_NOACTIVATE);
      SetWindowPos(owner, NULL, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                   SWP_FRAMECHANGED);
      app_force_foreground(owner);
      return 1;
    }
  app_force_foreground(owner);
  return 0;
}

static void menu_owner_restore(HWND owner, LONG_PTR saved, int changed)
{
  PostMessageW(owner, WM_NULL, 0, 0);
  if (changed)
    {
      SetWindowLongPtrW(owner, GWL_EXSTYLE, saved);
      SetWindowPos(owner, NULL, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                   SWP_FRAMECHANGED);
    }
}

void ui_menu_finish(void)
{
  g.menu_open = 0;
  if (g.tool_delay_deferred)
    {
      g.tool_delay_deferred = 0;
      if (g.pending_tool != TOOL_NONE)
        {
          Tool t = g.pending_tool;
          g.pending_tool = TOOL_NONE;
          tools_activate(t);
        }
    }
  if (g.overlay_activate_pending)
    {
      g.overlay_activate_pending = 0;
      app_activate_overlay_input();
    }
}

void ui_show_menu(int x, int y)
{
  if (g.menu_open) return;
  g.menu_open = 1;
  dlog("menu open at %d,%d", x, y);
  app_menu_capture_start(0, 0);

  HMENU m = build_menu();
  HWND owner = menu_owner();
  HWND prev = GetForegroundWindow();
  LONG_PTR saved = 0;
  int changed = menu_owner_activate(owner, &saved);

  int cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN |
                             TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                             x, y, owner, NULL);
  DestroyMenu(m);
  menu_owner_restore(owner, saved, changed);
  if (prev && prev != owner && IsWindow(prev)) SetForegroundWindow(prev);
  dlog("menu close cmd=%d", cmd);
  app_menu_capture_stop(cmd == 0);
  ui_menu_finish();

  if (cmd) menu_command(cmd);
}

/* Long-press menus are opened from a pen second-side press.  Windows does not
 * move the mouse cursor for every pen hover update, so use the last pen point
 * instead of GetCursorPos; the pen tip also has to select the item, because
 * the side button is still held while the menu is shown. */
static void popup_point(POINT *p)
{
  if (g.menu_hold_is_stylus)
    {
      /* The pen cursor may be hidden while the side button is held, so use
       * the point where the long press started. */
      p->x = (LONG) g.right_down.x;
      p->y = (LONG) g.right_down.y;
    }
  else
    GetCursorPos(p);
}

static UINT popup_flags(void)
{
  UINT flags = TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY;
  flags |= g.menu_hold_is_stylus ? TPM_LEFTBUTTON : TPM_RIGHTBUTTON;
  return flags;
}

void ui_show_wheel_menu(int marker)
{
  if (g.menu_open) return;
  g.menu_open = 1;
  app_menu_capture_start(1, marker);

  HMENU m = CreatePopupMenu();
  int cmd;

  if (marker)
    {
      AppendMenuW(m, MF_STRING | MF_DISABLED, 0, L"Marker Opacity");
      AppendMenuW(m, MF_SEPARATOR, 0, NULL);
      static const int vals[9] =
        { 10, 20, 30, 40, 50, 60, 70, 80, 90 };
      int checked = -1;
      for (int i = 0; i < 9; i++)
        {
          wchar_t label[8];
          _snwprintf(label, 8, L"%d%%", vals[i]);
          int is = fabs(g.marker_opacity * 100.0 - vals[i]) < 1.0;
          add_radio(m, CMD_MARKER_10 + i, label, is);
          if (is) checked = i;
        }
      if (checked >= 0)
        CheckMenuRadioItem(m, CMD_MARKER_10, CMD_MARKER_90,
                           CMD_MARKER_10 + checked, MF_BYCOMMAND);
    }
  else
    {
      AppendMenuW(m, MF_STRING | MF_DISABLED, 0, L"Scroll Mode");
      AppendMenuW(m, MF_SEPARATOR, 0, NULL);
      add_radio(m, CMD_WHEEL_COLOR, L"Color", g.wheel_mode == WHEEL_COLOR);
      add_radio(m, CMD_WHEEL_THICKNESS, L"Thickness",
                g.wheel_mode == WHEEL_THICKNESS);
      CheckMenuRadioItem(m, CMD_WHEEL_COLOR, CMD_WHEEL_THICKNESS,
                         g.wheel_mode == WHEEL_COLOR ? CMD_WHEEL_COLOR
                                                     : CMD_WHEEL_THICKNESS,
                         MF_BYCOMMAND);
    }

  HWND owner = menu_owner();
  HWND prev = GetForegroundWindow();
  LONG_PTR saved = 0;
  int changed = menu_owner_activate(owner, &saved);
  POINT p;
  popup_point(&p);
  ReleaseCapture();
  cmd = TrackPopupMenuEx(m, popup_flags(), p.x, p.y, owner, NULL);
  DestroyMenu(m);
  menu_owner_restore(owner, saved, changed);
  if (prev && prev != owner && IsWindow(prev)) SetForegroundWindow(prev);
  app_menu_capture_stop(cmd == 0);
  ui_menu_finish();
  if (cmd) menu_command(cmd);
}

void ui_show_stylus_menu(void)
{
  if (g.menu_open) return;
  g.menu_open = 1;
  app_menu_capture_start(2, 0);

  HMENU m = CreatePopupMenu();

  AppendMenuW(m, MF_STRING | MF_DISABLED, 0, L"Button 1 Mode");
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  add_radio(m, CMD_STYLUS_COLOR, L"Color", g.stylus_mode == STYLUS_COLOR);
  add_radio(m, CMD_STYLUS_THICKNESS, L"Thickness",
            g.stylus_mode == STYLUS_THICKNESS);
  CheckMenuRadioItem(m, CMD_STYLUS_COLOR, CMD_STYLUS_THICKNESS,
                     g.stylus_mode == STYLUS_COLOR ? CMD_STYLUS_COLOR
                                                   : CMD_STYLUS_THICKNESS,
                     MF_BYCOMMAND);

  HWND owner = menu_owner();
  HWND prev = GetForegroundWindow();
  LONG_PTR saved = 0;
  int changed = menu_owner_activate(owner, &saved);
  POINT p;
  popup_point(&p);
  ReleaseCapture();
  int cmd = TrackPopupMenuEx(m, popup_flags(), p.x, p.y, owner, NULL);
  DestroyMenu(m);
  menu_owner_restore(owner, saved, changed);
  if (prev && prev != owner && IsWindow(prev)) SetForegroundWindow(prev);
  app_menu_capture_stop(cmd == 0);
  ui_menu_finish();
  if (cmd) menu_command(cmd);
}

/* ------------------------------------------------------------------ */
/* icon visibility / settings                                         */
/* ------------------------------------------------------------------ */

void ui_refresh_tray(void)
{
  app_update_tray_icon();
}

void ui_set_icon_visible(int show)
{
  g.show_icon = show ? 1 : 0;
  if (g.iconwin)
    {
      ShowWindow(g.iconwin, show ? SW_SHOWNA : SW_HIDE);
      if (show) ui_position_icon();
    }
  config_save();
  ui_refresh_tray();
}

void ui_apply_pen(void)
{
  g.pen_color.a = g.marker_active ? 1.0 : g.opacity;
  g.fill_color.a = 1.0;
  app_update_cursor();
}

void ui_reset_settings(void)
{
  int keep_hints = g.hide_hints;
  tools_commit(0);
  if (g.tool != TOOL_NONE) tools_exit();

  config_defaults();
  g.hide_hints = keep_hints;
  g.pen_color.a = g.opacity;
  g.fill_color.a = 1.0;
  g.marker_saved_valid = 0;
  g.marker_active = 0;
  g.math_ready = 0;
  g.show_icon = 1;
  g.show_indicator = 1;
  ShowWindow(g.iconwin, SW_SHOWNA);
  app_set_paper(PAPER_NONE, 0);
  ui_apply_pen();
  ui_position_icon();
  ui_update_icon();
  config_save();
  app_render_all();
  ui_refresh_tray();
}

/* ------------------------------------------------------------------ */
/* colour and font choosers                                           */
/* ------------------------------------------------------------------ */

void ui_choose_color(int fill)
{
  static COLORREF customs[16];
  CHOOSECOLORW cc;
  Color cur = fill ? (g.fill_enabled ? g.fill_color : g.custom_fill_color)
                   : (line_color_index() == 8 ? g.custom_line_color
                                              : g.pen_color);

  customs[0] = RGB(255, 0, 255);
  memset(&cc, 0, sizeof(cc));
  cc.lStructSize = sizeof(cc);
  cc.hwndOwner = g.overlay;
  cc.rgbResult = RGB((BYTE) (cur.r * 255), (BYTE) (cur.g * 255),
                     (BYTE) (cur.b * 255));
  cc.lpCustColors = customs;
  cc.Flags = CC_FULLOPEN | CC_RGBINIT;

  if (!ChooseColorW(&cc)) return;

  Color c;
  c.r = GetRValue(cc.rgbResult) / 255.0;
  c.g = GetGValue(cc.rgbResult) / 255.0;
  c.b = GetBValue(cc.rgbResult) / 255.0;
  c.a = 1.0;

  if (fill)
    {
      g.custom_fill_color = c;
      g.fill_enabled = 1;
      g.fill_color = c;
      apply_fill_color_index(9);
    }
  else
    {
      g.custom_line_color = c;
      g.pen_color = c;
      g.pen_color.a = g.marker_active ? 1.0 : g.opacity;
      tools_set_frame_color();
      apply_line_color_index(8);
    }
}

void ui_choose_font(void)
{
  LOGFONTW lf;
  CHOOSEFONTW cf;
  char family[128] = "Arial";
  int bold = 0, italic = 0;
  double points = 24;
  int n = 0;

  HDC screen_dc = GetDC(NULL);
  int dpi = screen_dc ? GetDeviceCaps(screen_dc, LOGPIXELSY) : 96;
  if (screen_dc) ReleaseDC(NULL, screen_dc);
  if (dpi <= 0) dpi = 96;

  /* Parse "Family Bold Italic 24". */
  {
    char tmp[128];
    strncpy(tmp, g.font, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    char *p = strrchr(tmp, ' ');
    if (p) { points = atof(p + 1); *p = 0; }
    if (strstr(tmp, " Bold")) bold = 1;
    if (strstr(tmp, " Italic")) italic = 1;
    {
      char *b = strstr(tmp, " Bold"); if (b) *b = 0;
      char *i = strstr(tmp, " Italic"); if (i) *i = 0;
    }
    strncpy(family, tmp, sizeof(family) - 1);
    family[sizeof(family) - 1] = 0;
  }

  memset(&lf, 0, sizeof(lf));
  lf.lfHeight = -MulDiv((LONG) (points + 0.5), dpi, 72);
  lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
  lf.lfItalic = italic ? TRUE : FALSE;
  lf.lfCharSet = DEFAULT_CHARSET;
  MultiByteToWideChar(CP_UTF8, 0, family, -1, lf.lfFaceName, LF_FACESIZE);

  memset(&cf, 0, sizeof(cf));
  cf.lStructSize = sizeof(cf);
  cf.hwndOwner = g.overlay;
  cf.lpLogFont = &lf;
  cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_FORCEFONTEXIST |
             CF_NOVERTFONTS;

  if (g.paper != PAPER_NONE && !font_mouse_hook)
    font_mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, font_mouse_proc,
                                        g.hinst, 0);
  BOOL chosen = ChooseFontW(&cf);
  if (font_mouse_hook)
    {
      UnhookWindowsHookEx(font_mouse_hook);
      font_mouse_hook = NULL;
    }
  if (!chosen) return;

  points = fabs((double) lf.lfHeight) * 72.0 / (double) dpi;
  if (points < 4) points = 24;
  bold = lf.lfWeight >= FW_SEMIBOLD;
  italic = lf.lfItalic;

  char *face = u_wide(lf.lfFaceName);
  char buf[256];
  snprintf(buf, sizeof(buf), "%s%s%s %.0f", face ? face : "Arial",
           bold ? " Bold" : "", italic ? " Italic" : "", points);
  strncpy(g.font, buf, sizeof(g.font) - 1);
  g.font[sizeof(g.font) - 1] = 0;
  free(face);

  n = 1;
  (void) n;
  config_save();
}

/* ------------------------------------------------------------------ */
/* icon position dialog                                               */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK pos_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
    {
    case WM_CREATE:
      {
        int ids[4] = { ID_POS_TR, ID_POS_TL, ID_POS_BR, ID_POS_BL };
        const wchar_t *labels[4] = {
          L"Top Right (default)", L"Top Left", L"Bottom Right",
          L"Bottom Left"
        };
        HWND st = CreateWindowW(L"STATIC", L"Choose where the floating icon sits.",
                                WS_CHILD | WS_VISIBLE, 18, 14, 300, 22, h,
                                NULL, g.hinst, NULL);
        set_ui_font(st);
        for (int i = 0; i < 4; i++)
          {
            HWND b = CreateWindowW(L"BUTTON", labels[i],
                                   WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON |
                                   (i == 0 ? WS_GROUP : 0),
                                   24, 48 + i * 30, 280, 24, h,
                                   (HMENU) (INT_PTR) ids[i], g.hinst, NULL);
            set_ui_font(b);
            SendMessageW(b, BM_SETCHECK,
                         (WPARAM) ((int) g.icon_position == i ? BST_CHECKED
                                                              : BST_UNCHECKED), 0);
          }
        HWND ok = CreateWindowW(L"BUTTON", L"OK",
                                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                150, 176, 80, 28, h, (HMENU) IDOK, g.hinst,
                                NULL);
        HWND ca = CreateWindowW(L"BUTTON", L"Cancel",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                240, 176, 80, 28, h, (HMENU) IDCANCEL,
                                g.hinst, NULL);
        set_ui_font(ok);
        set_ui_font(ca);
      }
      return 0;
    case WM_COMMAND:
      if (LOWORD(wp) == IDOK)
        {
          int pos = 0;
          if (IsDlgButtonChecked(h, ID_POS_TL) == BST_CHECKED) pos = ICON_TL;
          else if (IsDlgButtonChecked(h, ID_POS_BR) == BST_CHECKED) pos = ICON_BR;
          else if (IsDlgButtonChecked(h, ID_POS_BL) == BST_CHECKED) pos = ICON_BL;
          g.icon_position = (IconPos) pos;
          ui_position_icon();
          config_save();
          DestroyWindow(h);
        }
      else if (LOWORD(wp) == IDCANCEL)
        DestroyWindow(h);
      return 0;
    case WM_CLOSE:
      DestroyWindow(h);
      return 0;
    }
  return DefWindowProcW(h, msg, wp, lp);
}

void ui_show_icon_position(void)
{
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.hInstance = g.hinst;
  wc.lpfnWndProc = pos_proc;
  wc.lpszClassName = POS_CLASS;
  wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  RegisterClassExW(&wc);

  HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, POS_CLASS,
                           L"Floating Icon Position", WS_POPUP | WS_CAPTION |
                           WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 340,
                           250, g.overlay, NULL, g.hinst, NULL);
  center_window(h, 340, 250);
  run_modal(h, g.paper != PAPER_NONE);
}

/* ------------------------------------------------------------------ */
/* hints dialog                                                       */
/* ------------------------------------------------------------------ */

typedef struct
{
  char *text;
  size_t len;
  size_t cap;
} SB;

static void sb_init(SB *b)
{
  b->cap = 1024;
  b->len = 0;
  b->text = (char *) malloc(b->cap);
  if (b->text) b->text[0] = 0;
}

static void sb_add(SB *b, const char *s)
{
  size_t n = strlen(s);
  if (b->len + n + 1 > b->cap)
    {
      while (b->len + n + 1 > b->cap) b->cap *= 2;
      b->text = (char *) realloc(b->text, b->cap);
    }
  if (b->text)
    {
      memcpy(b->text + b->len, s, n + 1);
      b->len += n;
    }
}

static void sb_addf(SB *b, const char *fmt, ...)
{
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  sb_add(b, buf);
}

static char *hints_rtf(void)
{
  SB b;
  sb_init(&b);

  sb_add(&b, "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss Segoe UI;}}"
             "{\\colortbl ;\\red192\\green57\\blue43;"
             "\\red142\\green68\\blue173;\\red211\\green84\\blue0;"
             "\\red41\\green128\\blue185;\\red230\\green126\\blue34;"
             "\\red185\\green119\\blue14;\\red22\\green160\\blue133;"
             "\\red44\\green62\\blue80;}\n\\fs18\n");

  sb_addf(&b, "\\cf1\\b\\fs22 Mouse\\b0\\fs18\\par\n");
      sb_addf(&b, "\\cf1\\bullet\\cf0  \\b Long right click\\b0  opens the "
                  "Scroll Mode menu and chooses what the \\b mouse wheel\\b0  "
                  "changes: \\b Color\\b0  or \\b Thickness\\b0.\\par\n");
      sb_addf(&b, "\\cf1\\bullet\\cf0  Left click and drag to draw with the "
                  "selected tool.\\par\n");
      sb_addf(&b, "\\cf1\\bullet\\cf0  Middle click and drag to erase.\\par\n");
      sb_addf(&b, "\\cf1\\bullet\\cf0  With the Eraser tool, the wheel "
                  "changes the eraser size.\\par\n");
      sb_addf(&b, "\\cf1\\bullet\\cf0  Short right click finishes the shape "
                  "or exits the tool.\\par\n");

      sb_addf(&b, "\\cf2\\b\\fs22 Stylus\\b0\\fs18\\par\n");
      sb_addf(&b, "\\cf1\\b IMPORTANT:\\b0 In your pen driver, set "
                  "\\b button 1\\b0  (nearest the tip) to \\b Middle Click\\b0  "
                  "and \\b button 2\\b0  to \\b Right Click\\b0.\\par\n");
      sb_addf(&b, "\\cf2\\bullet\\cf0  \\b Long press side button 1\\b0  "
                  "activates the eraser while it is held, even while "
                  "hovering.\\par\n");
      sb_addf(&b, "\\cf2\\bullet\\cf0  \\b Long press side button 2\\b0  "
                  "opens the Button 1 Mode menu (Color or Thickness); a "
                  "long right click opens Scroll Mode.\\par\n");
      sb_addf(&b, "\\cf2\\bullet\\cf0  Short press side button 1: step "
                  "through the selected Color or Thickness, or the eraser "
                  "size with the Eraser tool.\\par\n");
      sb_addf(&b, "\\cf2\\bullet\\cf0  Short click side button 2: finish the "
                  "shape or exit the tool. With the Eraser tool, a long "
                  "press does not open the menu.\\par\n");

      sb_addf(&b, "\\cf3\\b\\fs22 Global Shortcut\\b0\\fs18\\par\n");
      sb_addf(&b, "\\cf3\\bullet\\cf0  \\b F9\\b0  shows or hides the floating "
                  "icon at any time.\\par\n");
      sb_addf(&b, "\\cf3\\bullet\\cf0  \\b F8\\b0  selects the Pen, "
                  "\\b F6\\b0  the Arrow and \\b F7\\b0  the Text tool.\\par\n");
      sb_addf(&b, "\\cf3\\bullet\\cf0  \\b F10\\b0  opens the centered quick "
                  "tool menu with all tools.\\par\n");

      sb_addf(&b, "\\par\n");
      sb_addf(&b, "\\cf4\\b\\fs22 Shape Editing\\b0\\fs18\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  Hold \\b Ctrl\\b0  while rotating a "
                  "shape to snap to 0, 30, 45, 60, 90 degrees, and so "
                  "on.\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  Spline: hold \\b Ctrl\\b0  while "
                  "dragging a handle to move each control point "
                  "separately.\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  Polygon: the first press fixes the "
                  "centre; drag to set the radius and rotation.\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  Triangle: click the first two "
                  "vertices, move the pointer to preview the third, then "
                  "click to place it.\\par\n");

      sb_addf(&b, "\\cf5\\b\\fs22 Text Tool\\b0\\fs18\\par\n");
      sb_addf(&b, "\\cf5\\bullet\\cf0  After typing, press \\b Enter\\b0  to "
                  "accept the text and re-enable the mouse.\\par\n");

      sb_addf(&b, "\\cf6\\b\\fs22 Marker Tool\\b0\\fs18\\par\n");
      sb_addf(&b, "\\cf6\\bullet\\cf0  The wheel or stylus side button 1 "
                  "changes its color.\\par\n");
      sb_addf(&b, "\\cf6\\bullet\\cf0  \\b Long right click\\b0  or "
                  "\\b Long press side button 2\\b0  opens the Marker Opacity "
                  "menu (10%% to 90%% in steps of 10%%).\\par\n");

      sb_addf(&b, "\\cf7\\b\\fs22 Drawing Mode Shortcuts\\b0\\fs18\\par\n");
      sb_addf(&b, "\\cf7\\bullet\\cf0  \\b Ctrl+Z\\b0  undo, "
                  "\\b Ctrl+Shift+Z\\b0  redo.\\par\n");
      sb_addf(&b, "\\cf7\\bullet\\cf0  \\b Escape\\b0  finishes the shape or "
                  "exits the tool.\\par\n");

      sb_addf(&b, "\\cf4\\b\\fs22 Screen Paper Mode\\b0\\fs18\\par\n");
      sb_addf(&b, "\\cf4\\b\\i IMPORTANT:\\i0\\b0  \\b Clicking outside any "
                  "dialog, like equation editor, in screen paper mode will "
                  "close the dialog.\\b0\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  \\b Ctrl+Z\\b0  undo, "
                  "\\b Ctrl+Shift+Z\\b0  redo.\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  \\b Ctrl+T\\b0  shows or hides the "
                  "drawings.\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  \\b Ctrl+Q\\b0  exits the screen "
                  "paper.\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  \\b Alt+F4\\b0  quits.\\par\n");

  sb_add(&b, "}");
  return b.text;
}

typedef struct
{
  const char *data;
  size_t len;
  size_t pos;
} StreamIn;

static DWORD CALLBACK rtf_stream(DWORD_PTR cookie, LPBYTE buf, LONG cb,
                                 LONG *pcb)
{
  StreamIn *s = (StreamIn *) cookie;
  LONG n = cb;
  if (s->pos + (size_t) n > s->len) n = (LONG) (s->len - s->pos);
  if (n < 0) n = 0;
  memcpy(buf, s->data + s->pos, (size_t) n);
  s->pos += (size_t) n;
  *pcb = n;
  return 0;
}

static void set_rich_text(HWND h, const char *rtf)
{
  StreamIn s;
  EDITSTREAM es;
  s.data = rtf;
  s.len = strlen(rtf);
  s.pos = 0;
  memset(&es, 0, sizeof(es));
  es.dwCookie = (DWORD_PTR) &s;
  es.pfnCallback = rtf_stream;
  SendMessageW(h, EM_STREAMIN, SF_RTF, (LPARAM) &es);
}

static LRESULT CALLBACK hints_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
    {
    case WM_CREATE:
      {
        RECT cr;
        GetClientRect(h, &cr);
        int cw = cr.right > 0 ? cr.right : 750;
        int ch = cr.bottom > 0 ? cr.bottom : 550;

        HWND title = CreateWindowW(L"STATIC", L"Screenator Hints",
                                   WS_CHILD | WS_VISIBLE, 18, 12,
                                   cw - 36, 30, h, NULL, g.hinst, NULL);
        HWND sub = CreateWindowW(L"STATIC",
                                 L"The important controls at a glance.",
                                 WS_CHILD | WS_VISIBLE, 20, 42,
                                 cw - 40, 20, h, NULL, g.hinst, NULL);
        HFONT big = CreateFontW(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH, L"Segoe UI");
        if (big)
          {
            SendMessageW(title, WM_SETFONT, (WPARAM) big, TRUE);
            /* Leak is bounded to one dialog run. */
          }
        set_ui_font(sub);

        int edit_h = ch - 74 - 64;
        if (edit_h < 120) edit_h = 120;
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                    ES_MULTILINE | ES_READONLY,
                                    18, 74, cw - 36, edit_h, h, NULL,
                                    g.hinst, NULL);
        set_ui_font(edit);

        char *rtf = hints_rtf();
        if (rtf) { set_rich_text(edit, rtf); free(rtf); }

        HWND chk = CreateWindowW(L"BUTTON", L"Never show hints again",
                                 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                 20, ch - 40, 240, 24, h,
                                 (HMENU) (INT_PTR) ID_HINTS_CHECK, g.hinst,
                                 NULL);
        set_ui_font(chk);
        SendMessageW(chk, BM_SETCHECK,
                     g.hide_hints ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND close = CreateWindowW(L"BUTTON", L"Close",
                                   WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                   cw - 18 - 84, ch - 44, 84, 28, h,
                                   (HMENU) IDOK, g.hinst, NULL);
        set_ui_font(close);
      }
      return 0;

    case WM_COMMAND:
      if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)
        {
          HWND chk = GetDlgItem(h, ID_HINTS_CHECK);
          int val = (int) SendMessageW(chk, BM_GETCHECK, 0, 0);
          g.hide_hints = val == BST_CHECKED;
          config_save();
          DestroyWindow(h);
          return 0;
        }
      return 0;

    case WM_CLOSE:
      SendMessageW(h, WM_COMMAND, IDOK, 0);
      return 0;

    case WM_CTLCOLORSTATIC:
      SetBkColor((HDC) wp, GetSysColor(COLOR_WINDOW));
      return (LRESULT) GetSysColorBrush(COLOR_WINDOW);
    }
  return DefWindowProcW(h, msg, wp, lp);
}

void ui_show_hints(int startup)
{
  (void) startup;
  if (g.hintsdlg) { app_force_foreground(g.hintsdlg); return; }

  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.hInstance = g.hinst;
  wc.lpfnWndProc = hints_proc;
  wc.lpszClassName = HINTS_CLASS;
  wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  RegisterClassExW(&wc);

  int w = 750, hgt = 550;
  RECT wa;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  if (wa.right - wa.left - 80 < w) w = wa.right - wa.left - 80;
  if (w < 500) w = 500;
  if (wa.bottom - wa.top - 80 < hgt) hgt = wa.bottom - wa.top - 80;
  if (hgt < 400) hgt = 400;

  g.hintsdlg = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                               HINTS_CLASS, L"Screenator Hints",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, w, hgt,
                               g.overlay, NULL, g.hinst, NULL);
  center_window(g.hintsdlg, w, hgt);
  run_modal(g.hintsdlg, g.paper != PAPER_NONE);
  g.hintsdlg = NULL;
  g.hints_shown = 1;
}

/* ------------------------------------------------------------------ */
/* math dialog                                                        */
/* ------------------------------------------------------------------ */

static const char *math_presets[] =
{
  "cos(2*pi*x)", "sin(2*pi*x)", "unit(x)", "delta(x)", "rect(x)",
  "e^(x)", "log(x)", "1/x", "sin(2*pi*x)/(2*pi*x)",
  "(cos(2*pi*x)+2)*cos(20*pi*x)"
};

static void math_set_expr(HWND h, const char *expr)
{
  wchar_t *w = w_utf8(expr);
  SetDlgItemTextW(h, ID_MATH_EXPR, w ? w : L"");
  free(w);
}

static void math_load_mode(HWND h, int polar)
{
  if (polar)
    {
      math_set_expr(h, g.math_polar_expr);
      wchar_t buf[64];
      _snwprintf(buf, 64, L"%.10g", g.math_tmin);
      SetDlgItemTextW(h, ID_MATH_FROM, buf);
      _snwprintf(buf, 64, L"%.10g", g.math_tmax);
      SetDlgItemTextW(h, ID_MATH_TO, buf);
    }
  else
    {
      math_set_expr(h, g.math_expr);
      wchar_t buf[64];
      _snwprintf(buf, 64, L"%.10g", g.math_xmin);
      SetDlgItemTextW(h, ID_MATH_FROM, buf);
      _snwprintf(buf, 64, L"%.10g", g.math_xmax);
      SetDlgItemTextW(h, ID_MATH_TO, buf);
    }
}

static int math_save_mode(HWND h, int polar, int validate)
{
  wchar_t wbuf[512];
  char expr[256] = "";
  double a = 0, b = 0;

  GetDlgItemTextW(h, ID_MATH_EXPR, wbuf, 512);
  { char *s = u_wide(wbuf); if (s) { strncpy(expr, s, sizeof(expr) - 1); free(s); } }

  GetDlgItemTextW(h, ID_MATH_FROM, wbuf, 64);
  a = _wtof(wbuf);
  GetDlgItemTextW(h, ID_MATH_TO, wbuf, 64);
  b = _wtof(wbuf);

  if (!expr[0] || a == b)
    {
      if (validate)
        SetDlgItemTextW(h, ID_MATH_ERR,
                        L"Enter an expression and a non-empty range.");
      return 0;
    }

  if (validate)
    {
      const char *err = NULL;
      if (!math_validate(expr, &err))
        {
          wchar_t *we = w_utf8(err ? err : "Invalid expression");
          SetDlgItemTextW(h, ID_MATH_ERR, we ? we : L"Invalid expression.");
          free(we);
          return 0;
        }
    }

  if (polar)
    {
      snprintf(g.math_polar_expr, sizeof(g.math_polar_expr), "%s", expr);
      g.math_tmin = a;
      g.math_tmax = b;
    }
  else
    {
      snprintf(g.math_expr, sizeof(g.math_expr), "%s", expr);
      g.math_xmin = a;
      g.math_xmax = b;
    }
  return 1;
}

static LRESULT CALLBACK math_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
    {
    case WM_CREATE:
      {
        HWND st;
        HWND b;
        int polar = g.math_polar;

        st = CreateWindowW(L"STATIC", L"Mode", WS_CHILD | WS_VISIBLE,
                           18, 16, 80, 20, h, NULL, g.hinst, NULL);
        set_ui_font(st);
        b = CreateWindowW(L"BUTTON", L"Rectangular",
                          WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                          100, 14, 130, 24, h, (HMENU) ID_MATH_RECT, g.hinst,
                          NULL);
        set_ui_font(b);
        b = CreateWindowW(L"BUTTON", L"Polar",
                          WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                          250, 14, 100, 24, h, (HMENU) ID_MATH_POLAR, g.hinst,
                          NULL);
        set_ui_font(b);

        st = CreateWindowW(L"STATIC", L"Preset", WS_CHILD | WS_VISIBLE,
                           18, 58, 90, 20, h, NULL, g.hinst, NULL);
        set_ui_font(st);
        HWND cb = CreateWindowW(L"COMBOBOX", L"",
                                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST |
                                WS_VSCROLL, 110, 54, 320, 280, h,
                                (HMENU) ID_MATH_PRESET, g.hinst, NULL);
        set_ui_font(cb);
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM) L"(choose a preset)");
        for (int i = 0; i < (int) (sizeof(math_presets) /
                                   sizeof(math_presets[0])); i++)
          {
            wchar_t *w = w_utf8(math_presets[i]);
            SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM) w);
            free(w);
          }
        SendMessageW(cb, CB_SETCURSEL, 0, 0);

        st = CreateWindowW(L"STATIC", L"Expression", WS_CHILD | WS_VISIBLE,
                           18, 98, 90, 20, h, NULL, g.hinst, NULL);
        set_ui_font(st);
        HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                  110, 94, 320, 26, h,
                                  (HMENU) ID_MATH_EXPR, g.hinst, NULL);
        set_ui_font(ed);

        st = CreateWindowW(L"STATIC", L"From", WS_CHILD | WS_VISIBLE,
                           110, 140, 45, 20, h, NULL, g.hinst, NULL);
        set_ui_font(st);
        ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                             158, 136, 100, 26, h, (HMENU) ID_MATH_FROM,
                             g.hinst, NULL);
        set_ui_font(ed);
        st = CreateWindowW(L"STATIC", L"To", WS_CHILD | WS_VISIBLE,
                           278, 140, 30, 20, h, NULL, g.hinst, NULL);
        set_ui_font(st);
        ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                             308, 136, 122, 26, h, (HMENU) ID_MATH_TO,
                             g.hinst, NULL);
        set_ui_font(ed);

        st = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                           18, 178, 484, 40, h, (HMENU) ID_MATH_ERR, g.hinst,
                           NULL);
        set_ui_font(st);

        b = CreateWindowW(L"BUTTON", L"OK",
                          WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                          290, 276, 90, 30, h, (HMENU) IDOK, g.hinst, NULL);
        set_ui_font(b);
        b = CreateWindowW(L"BUTTON", L"Cancel",
                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          394, 276, 90, 30, h, (HMENU) IDCANCEL, g.hinst,
                          NULL);
        set_ui_font(b);

        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR) polar);
        SendMessageW(h, BM_SETCHECK, 0, 0);
        CheckRadioButton(h, ID_MATH_RECT, ID_MATH_POLAR,
                         polar ? ID_MATH_POLAR : ID_MATH_RECT);
        math_load_mode(h, polar);
      }
      return 0;

    case WM_COMMAND:
      {
        int id = LOWORD(wp);
        int cur = (int) GetWindowLongPtrW(h, GWLP_USERDATA);

        if (id == ID_MATH_RECT || id == ID_MATH_POLAR)
          {
            int want = (id == ID_MATH_POLAR);
            if (want != cur)
              {
                if (!math_save_mode(h, cur, 0)) return 0;
                SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR) want);
                math_load_mode(h, want);
                SetDlgItemTextW(h, ID_MATH_ERR, L"");
              }
            CheckRadioButton(h, ID_MATH_RECT, ID_MATH_POLAR,
                             want ? ID_MATH_POLAR : ID_MATH_RECT);
            return 0;
          }

        if (id == ID_MATH_PRESET && HIWORD(wp) == CBN_SELCHANGE)
          {
            HWND cb = GetDlgItem(h, ID_MATH_PRESET);
            int sel = (int) SendMessageW(cb, CB_GETCURSEL, 0, 0);
            if (sel > 0 && sel <= (int) (sizeof(math_presets) /
                                         sizeof(math_presets[0])))
              {
                const char *p = math_presets[sel - 1];
                char buf[256];
                const char *var = cur ? "theta" : "x";
                size_t j = 0;
                for (size_t i = 0; p[i] && j < sizeof(buf) - 8; i++)
                  {
                    if (p[i] == 'x')
                      {
                        size_t vl = strlen(var);
                        memcpy(buf + j, var, vl);
                        j += vl;
                      }
                    else buf[j++] = p[i];
                  }
                buf[j] = 0;
                math_set_expr(h, buf);
                SetDlgItemTextW(h, ID_MATH_ERR, L"");
              }
            return 0;
          }

        if (id == IDOK)
          {
            if (!math_save_mode(h, cur, 1)) return 0;
            g.math_polar = cur;
            g.math_ready = 1;
            if (g.edit == EDIT_FRAME && g.frame.type == SHAPE_MATH)
              {
                const char *expr = cur ? g.math_polar_expr : g.math_expr;
                free(g.frame.expr);
                g.frame.expr = _strdup(expr);
                g.frame.polar = cur;
                g.frame.xmin = cur ? g.math_tmin : g.math_xmin;
                g.frame.xmax = cur ? g.math_tmax : g.math_xmax;
                g.frame.color = g.pen_color;
                g.frame.color.a = g.opacity;
                app_render_all();
              }
            DestroyWindow(h);
            if (g.tool != TOOL_MATH) tools_activate(TOOL_MATH);
            return 0;
          }
        if (id == IDCANCEL)
          {
            DestroyWindow(h);
            return 0;
          }
      }
      return 0;

    case WM_CLOSE:
      DestroyWindow(h);
      return 0;

    case WM_CTLCOLORSTATIC:
      {
        HDC dc = (HDC) wp;
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        if ((HWND) lp == GetDlgItem(h, ID_MATH_ERR))
          SetTextColor(dc, RGB(180, 0, 0));
        return (LRESULT) GetSysColorBrush(COLOR_WINDOW);
      }
    }
  return DefWindowProcW(h, msg, wp, lp);
}

void ui_show_math_dialog(void)
{
  if (g.mathdlg) { app_force_foreground(g.mathdlg); return; }

  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.hInstance = g.hinst;
  wc.lpfnWndProc = math_proc;
  wc.lpszClassName = MATH_CLASS;
  wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  RegisterClassExW(&wc);

  g.mathdlg = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, MATH_CLASS,
                              L"Math Function", WS_POPUP | WS_CAPTION |
                              WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 520,
                              360, g.overlay, NULL, g.hinst, NULL);
  center_window(g.mathdlg, 520, 360);
  run_modal(g.mathdlg, g.paper != PAPER_NONE);
  g.mathdlg = NULL;
}

/* ------------------------------------------------------------------ */
/* about                                                              */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK about_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
    {
    case WM_CREATE:
      {
        HWND icon = CreateWindowW(L"STATIC", L"",
                                  WS_CHILD | WS_VISIBLE | SS_ICON,
                                  18, 16, 64, 64, h, NULL, g.hinst, NULL);
        if (g.hicon_normal)
          SendMessageW(icon, STM_SETICON, (WPARAM) g.hicon_normal, 0);

        HWND t = CreateWindowW(L"STATIC", L"Screenator 7.0",
                               WS_CHILD | WS_VISIBLE, 96, 20, 300, 28, h,
                               NULL, g.hinst, NULL);
        HFONT big = CreateFontW(-22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH, L"Segoe UI");
        if (big) SendMessageW(t, WM_SETFONT, (WPARAM) big, TRUE);
        set_ui_font(t);

        HWND txt = CreateWindowW(L"STATIC",
          L"A screen annotation tool for Windows.\r\n"
          L"Based on Screenator 6.0 by Mahmoud Alnaanah, Gromit-MPX 1.4.2\r\n"
          L"and Gromit (GPL-2.0-or-later).\r\n\r\n"
          L"This program is free software under the GNU General Public\r\n"
          L"License version 2 or later.",
          WS_CHILD | WS_VISIBLE, 20, 92, 390, 130, h, NULL, g.hinst, NULL);
        set_ui_font(txt);

        HWND b = CreateWindowW(L"BUTTON", L"Close",
                               WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                               330, 236, 84, 28, h, (HMENU) IDOK, g.hinst,
                               NULL);
        set_ui_font(b);
      }
      return 0;
    case WM_COMMAND:
      if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)
        { DestroyWindow(h); return 0; }
      return 0;
    case WM_CLOSE:
      DestroyWindow(h);
      return 0;
    case WM_CTLCOLORSTATIC:
      SetBkColor((HDC) wp, GetSysColor(COLOR_WINDOW));
      return (LRESULT) GetSysColorBrush(COLOR_WINDOW);
    }
  return DefWindowProcW(h, msg, wp, lp);
}

void ui_show_about(void)
{
  if (g.aboutdlg) { app_force_foreground(g.aboutdlg); return; }

  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.hInstance = g.hinst;
  wc.lpfnWndProc = about_proc;
  wc.lpszClassName = ABOUT_CLASS;
  wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  RegisterClassExW(&wc);

  g.aboutdlg = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, ABOUT_CLASS,
                               L"About Screenator", WS_POPUP | WS_CAPTION |
                               WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 440,
                               310, g.overlay, NULL, g.hinst, NULL);
  center_window(g.aboutdlg, 440, 310);
  run_modal(g.aboutdlg, g.paper != PAPER_NONE);
  g.aboutdlg = NULL;
}

/* ------------------------------------------------------------------ */
/* quick tool menu                                                    */
/* ------------------------------------------------------------------ */

typedef struct { Tool tool; const wchar_t *label; const char *icon; } QuickTool;

static const QuickTool quick_tools[] =
{
  { TOOL_PEN, L"Pen", "pen.png" },
  { TOOL_TEXT, L"Text", "text.png" },
  { TOOL_ERASER, L"Eraser", "eraser.png" },
  { TOOL_MARKER, L"Marker", "marker.png" },
  { TOOL_LINE, L"Line", "free-line.png" },
  { TOOL_SPLINE, L"Spline", "spline.png" },
  { TOOL_POLYGON, L"Polygon", "polygon.png" },
  { TOOL_TRIANGLE, L"Triangle", "triangle.png" },
  { TOOL_ARROW, L"Arrow", "arrow.png" },
  { TOOL_ELLIPSE, L"Ellipse", "ellipse.png" },
  { TOOL_RRECT, L"Rounded Rect", "rounded-rectangle.png" },
  { TOOL_BUCKET, L"Bucket", "bucket.png" },
  { TOOL_MATH, L"Math", "math.png" },
  { TOOL_BARS, L"Parallel", "bars.png" },
  { TOOL_RANDOM, L"Random", "random-signal.png" }
};

#define QUICK_COLS 5
#define QUICK_ROWS 3
#define QUICK_CELL_W 116
#define QUICK_CELL_H 108
#define QUICK_MARGIN 12

static HBITMAP quick_icons[15];
static int quick_result;
static int quick_done;

static RECT quick_cell_rect(int i)
{
  int col = i % QUICK_COLS, row = i / QUICK_COLS;
  RECT r;
  r.left = QUICK_MARGIN + col * QUICK_CELL_W;
  r.top = QUICK_MARGIN + row * QUICK_CELL_H;
  r.right = r.left + QUICK_CELL_W - 8;
  r.bottom = r.top + QUICK_CELL_H - 8;
  return r;
}

static int quick_hit(int x, int y)
{
  for (int i = 0; i < 15; i++)
    {
      RECT r = quick_cell_rect(i);
      if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
        return i;
    }
  return -1;
}

static void quick_activate(HWND h)
{
  if (g.quick_sel >= 0 && g.quick_sel < 15)
    quick_result = quick_tools[g.quick_sel].tool;
  quick_done = 1;
  DestroyWindow(h);
}

static LRESULT CALLBACK quick_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
    {
    case WM_CREATE:
      for (int i = 0; i < 15; i++)
        quick_icons[i] = ui_load_icon_bitmap(quick_tools[i].icon, 48);
      return 0;

    case WM_PAINT:
      {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);

        HBRUSH bg = CreateSolidBrush(RGB(250, 250, 250));
        FillRect(dc, &cr, bg);
        DeleteObject(bg);

        SetBkMode(dc, TRANSPARENT);
        for (int i = 0; i < 15; i++)
          {
            RECT r = quick_cell_rect(i);
            HBRUSH b;
            if (i == g.quick_sel)
              b = CreateSolidBrush(RGB(205, 228, 252));
            else
              b = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(dc, &r, b);
            DeleteObject(b);

            HPEN pen = CreatePen(PS_SOLID, 1, RGB(205, 205, 205));
            HGDIOBJ old = SelectObject(dc, pen);
            HGDIOBJ oldbrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, r.left, r.top, r.right, r.bottom);
            SelectObject(dc, oldbrush);
            SelectObject(dc, old);
            DeleteObject(pen);

            if (quick_icons[i])
              {
                HDC mem = CreateCompatibleDC(dc);
                HGDIOBJ ob = SelectObject(mem, quick_icons[i]);
                int sz = 48;
                int x = r.left + (r.right - r.left - sz) / 2;
                int y = r.top + 10;
                BitBlt(dc, x, y, sz, sz, mem, 0, 0, SRCCOPY);
                SelectObject(mem, ob);
                DeleteDC(mem);
              }

            RECT tr = r;
            tr.top = r.top + 66;
            tr.bottom = r.bottom - 4;
            SetTextColor(dc, RGB(30, 30, 30));
            SelectObject(dc, ui_font ? ui_font : GetStockObject(DEFAULT_GUI_FONT));
            DrawTextW(dc, quick_tools[i].label, -1, &tr,
                      DT_CENTER | DT_WORDBREAK | DT_TOP);
          }

        HPEN border = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
        HGDIOBJ ob = SelectObject(dc, border);
        HGDIOBJ obb = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, 0, 0, cr.right, cr.bottom);
        SelectObject(dc, obb);
        SelectObject(dc, ob);
        DeleteObject(border);

        EndPaint(h, &ps);
      }
      return 0;

    case WM_MOUSEMOVE:
      {
        int i = quick_hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (i != g.quick_sel)
          {
            g.quick_sel = i;
            InvalidateRect(h, NULL, TRUE);
          }
      }
      return 0;

    case WM_LBUTTONUP:
      {
        int i = quick_hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (i >= 0) { g.quick_sel = i; quick_activate(h); }
      }
      return 0;

    case WM_KEYDOWN:
      {
        int sel = g.quick_sel < 0 ? 0 : g.quick_sel;
        int col = sel % QUICK_COLS, row = sel / QUICK_COLS;
        if (wp == VK_LEFT) col = (col + QUICK_COLS - 1) % QUICK_COLS;
        else if (wp == VK_RIGHT) col = (col + 1) % QUICK_COLS;
        else if (wp == VK_UP) row = (row + QUICK_ROWS - 1) % QUICK_ROWS;
        else if (wp == VK_DOWN) row = (row + 1) % QUICK_ROWS;
        else if (wp == VK_HOME) { row = 0; col = 0; }
        else if (wp == VK_END) { row = QUICK_ROWS - 1; col = QUICK_COLS - 1; }
        else if (wp == VK_RETURN) { quick_activate(h); return 0; }
        else if (wp == VK_ESCAPE) { quick_result = 0; DestroyWindow(h); return 0; }
        else return 0;

        g.quick_sel = row * QUICK_COLS + col;
        InvalidateRect(h, NULL, TRUE);
      }
      return 0;

    case WM_KILLFOCUS:
      DestroyWindow(h);
      return 0;

    case WM_CLOSE:
      DestroyWindow(h);
      return 0;
    }
  return DefWindowProcW(h, msg, wp, lp);
}

void ui_quick_menu(void)
{
  if (g.quickwnd) { app_force_foreground(g.quickwnd); return; }

  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.hInstance = g.hinst;
  wc.lpfnWndProc = quick_proc;
  wc.lpszClassName = QUICK_CLASS;
  wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  RegisterClassExW(&wc);

  int w = QUICK_MARGIN * 2 + QUICK_COLS * QUICK_CELL_W;
  int h = QUICK_MARGIN * 2 + QUICK_ROWS * QUICK_CELL_H;

  g.quick_sel = 0;
  quick_result = 0;
  quick_done = 0;

  for (int i = 0; i < 15; i++)
    quick_icons[i] = NULL;

  g.quickwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, QUICK_CLASS,
                               L"Quick Tools", WS_POPUP,
                               CW_USEDEFAULT, CW_USEDEFAULT, w, h, g.overlay,
                               NULL, g.hinst, NULL);
  center_window(g.quickwnd, w, h);

  modal_depth++;
  ShowWindow(g.quickwnd, SW_SHOW);
  app_force_foreground(g.quickwnd);

  MSG msg;
  while (IsWindow(g.quickwnd) && GetMessageW(&msg, NULL, 0, 0) > 0)
    {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  modal_depth--;
  g.quickwnd = NULL;

  if (quick_result == TOOL_MATH) ui_show_math_dialog();
  else if (quick_result != TOOL_NONE) tools_activate((Tool) quick_result);
}

/* ------------------------------------------------------------------ */
/* init                                                               */
/* ------------------------------------------------------------------ */

void ui_init(void)
{
  NONCLIENTMETRICSW ncm;
  memset(&ncm, 0, sizeof(ncm));
  ncm.cbSize = sizeof(ncm);
  if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
    ui_font = CreateFontIndirectW(&ncm.lfMessageFont);
  else
    ui_font = (HFONT) GetStockObject(DEFAULT_GUI_FONT);

  if (ui_font == (HFONT) GetStockObject(DEFAULT_GUI_FONT)) ui_font = NULL;

  /* Make sure the asset icon cache is warm for the menus. */
  ui_load_icon_bitmap("pen.png", 16);
}

void ui_layout(void)
{
  /* Surfaces and the floating icon are positioned by the platform layer. */
}

int ui_selfcheck(void)
{
  int ok = 1;
  HMENU m = build_menu();

  if (!m || GetMenuItemCount(m) < 20) ok = 0;
  if (m)
    {
      /* The opacity submenu must offer 10% through 90% in ten-percent
       * steps, and the icon submenu keeps all seven sizes. */
      int found_opacity = 0, found_icon = 0;
      for (int i = 0; i < GetMenuItemCount(m); i++)
        {
          wchar_t text[64];
          if (!GetMenuStringW(m, (UINT) i, text, 64, MF_BYPOSITION)) continue;
          if (!wcscmp(text, L"Opacity"))
            {
              HMENU sub = GetSubMenu(m, i);
              found_opacity = sub && GetMenuItemCount(sub) == 9;
            }
          else if (!wcscmp(text, L"Floating Icon Size"))
            {
              HMENU sub = GetSubMenu(m, i);
              found_icon = sub && GetMenuItemCount(sub) == 7;
            }
        }
      if (!found_opacity || !found_icon) ok = 0;
      DestroyMenu(m);
    }

  if (!ui_load_icon_bitmap("pen.png", 16)) ok = 0;
  if (!ui_load_icon_bitmap("shapes.png", 16)) ok = 0;
  if (!ui_load_icon_bitmap("screenator.png", 16)) ok = 0;
  if (!ui_load_icon_bitmap("pen.png", 48)) ok = 0;
  return ok;
}

static void register_dialog(const wchar_t *cls, WNDPROC proc)
{
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.hInstance = g.hinst;
  wc.lpfnWndProc = proc;
  wc.lpszClassName = cls;
  wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  RegisterClassExW(&wc);
}

int ui_dialog_selfcheck(void)
{
  int ok = 1;
  HWND h;

  register_dialog(HINTS_CLASS, hints_proc);
  register_dialog(MATH_CLASS, math_proc);
  register_dialog(POS_CLASS, pos_proc);
  register_dialog(ABOUT_CLASS, about_proc);

  h = CreateWindowExW(WS_EX_TOOLWINDOW, MATH_CLASS, L"Math Function",
                      WS_POPUP, 0, 0, 520, 360, g.overlay, NULL, g.hinst,
                      NULL);
  if (!h || !GetDlgItem(h, ID_MATH_EXPR) || !GetDlgItem(h, ID_MATH_PRESET))
    ok = 0;
  if (h) DestroyWindow(h);

  h = CreateWindowExW(WS_EX_TOOLWINDOW, HINTS_CLASS, L"Screenator Hints",
                      WS_POPUP, 0, 0, 750, 550, g.overlay, NULL, g.hinst,
                      NULL);
  if (!h || !FindWindowExW(h, NULL, MSFTEDIT_CLASS, NULL)) ok = 0;
  if (h) DestroyWindow(h);

  h = CreateWindowExW(WS_EX_TOOLWINDOW, POS_CLASS, L"Floating Icon Position",
                      WS_POPUP, 0, 0, 340, 250, g.overlay, NULL, g.hinst,
                      NULL);
  if (!h || !IsDlgButtonChecked(h, ID_POS_TR)) ok = 0;
  if (h) DestroyWindow(h);

  h = CreateWindowExW(WS_EX_TOOLWINDOW, ABOUT_CLASS, L"About Screenator",
                      WS_POPUP, 0, 0, 440, 310, g.overlay, NULL, g.hinst,
                      NULL);
  if (!h) ok = 0;
  if (h) DestroyWindow(h);

  return ok;
}
