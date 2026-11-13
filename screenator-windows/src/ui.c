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
#define ID_HINTS_ABOUT   3003

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

#define ID_SC_EDIT       7001
#define ID_SC_ERR        (ID_SC_EDIT + SK_COUNT)
#define ID_SC_NOTE       (ID_SC_ERR + 1)

static HFONT ui_font;
static int modal_depth;

static const wchar_t *QUICK_CLASS = L"ScreenatorQuick";
static const wchar_t *HINTS_CLASS = L"ScreenatorHints";
static const wchar_t *MATH_CLASS = L"ScreenatorMath";
static const wchar_t *POS_CLASS = L"ScreenatorIconPos";
static const wchar_t *ABOUT_CLASS = L"ScreenatorAbout";
static const wchar_t *SHORTCUT_CLASS = L"ScreenatorShortcuts";
static const wchar_t *SETTINGS_CLASS = L"ScreenatorSettings";

#define ID_SET_SAVE     7201
#define ID_SET_IND      7202
#define ID_SET_FONT     7203
#define ID_SET_POS      7204
#define ID_SET_HORIZ    7206
#define ID_SET_VERT     7207
#define ID_SET_SIZE     7208
#define ID_SET_CFG      7209
#define ID_SET_SC       7210
#define ID_SET_RESET    7211
#define ID_SET_SHOW_ICON 7212
#define ID_SET_ICON_SIZE 7213
#define ID_SET_OPEN_PDF 7214
#define ID_SET_SAVE_PAPER 7215
#define ID_SET_TAB      7216
#define ID_SET_LABEL_PATH 7218
#define ID_SET_LABEL_TOOL_SIZE 7219
#define ID_SET_LABEL_ICON_SIZE 7220
#define ID_SET_LABEL_SHORTCUTS 7221
#define ID_SET_POS2     7222
#define ID_SET_SC2      7223
#define ID_SET_CONTINUOUS 7224
#define ID_SET_OPEN_CFG 7226
#define ID_SET_SIZE_VAL 7227
#define ID_SET_PAPER_SIZE 7228
#define ID_SET_PAPER_W 7229
#define ID_SET_PAPER_H 7230
#define ID_SET_PAPER_W_LABEL 7231
#define ID_SET_PAPER_H_LABEL 7232
#define ID_SET_PAPER_SIZE_LABEL 7233
static const wchar_t *GOTO_CLASS = L"ScreenatorGotoPage";

#define ID_GOTO_EDIT 8101
#define ID_GOTO_ERR  8102

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

void ui_set_font(HWND h)
{
  set_ui_font(h);
}

void ui_center_window(HWND h, int w, int hgt)
{
  center_window(h, w, hgt);
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

void ui_prepare_dialog(void)
{
  if (g.text_editor_active)
    {
      g.ignore_next_right_up = 1;
      app_end_text(1);
    }
  if (g.tool != TOOL_NONE) tools_exit();
}

static void run_modal(HWND hwnd, int outside_close)
{
  MSG msg;
  HWND prev_dialog = g.modal_dialog;
  int prev_flag = g.modal_outside_close;

  ui_prepare_dialog();
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

void ui_run_modal(HWND hwnd, int outside_close)
{
  run_modal(hwnd, outside_close);
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
  ui_prepare_dialog();
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
#define MENU_COLOR_BITMAP_MAX 20
static HBITMAP menu_color_bitmaps[MENU_COLOR_BITMAP_MAX];
static int menu_color_bitmap_count;

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

static void menu_color_swatch(HMENU menu, UINT id, Color color)
{
  BITMAPINFO bi;
  void *bits = NULL;
  HBITMAP bmp;
  if (menu_color_bitmap_count >= MENU_COLOR_BITMAP_MAX) return;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = 18;
  bi.bmiHeader.biHeight = -16;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!bmp || !bits)
    {
      if (bmp) DeleteObject(bmp);
      return;
    }
  {
    uint32_t *px = (uint32_t *) bits;
    uint32_t rgb = 0xFF000000u |
      ((uint32_t) (BYTE) (color.r * 255.0 + 0.5) << 16) |
      ((uint32_t) (BYTE) (color.g * 255.0 + 0.5) << 8) |
      (uint32_t) (BYTE) (color.b * 255.0 + 0.5);
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 18; x++)
        px[y * 18 + x] = (x == 0 || x == 17 || y == 0 || y == 15)
          ? 0xFF303030u : rgb;
  }
  menu_color_bitmaps[menu_color_bitmap_count++] = bmp;
  SetMenuItemBitmaps(menu, id, MF_BYCOMMAND, bmp, bmp);
}

static void menu_color_bitmaps_clear(void)
{
  for (int i = 0; i < menu_color_bitmap_count; i++)
    if (menu_color_bitmaps[i]) DeleteObject(menu_color_bitmaps[i]);
  menu_color_bitmap_count = 0;
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

static void add_popup(HMENU parent, HMENU sub, const wchar_t *label,
                      const char *icon)
{
  int pos = GetMenuItemCount(parent);
  AppendMenuW(parent, MF_POPUP | MF_STRING, (UINT_PTR) sub, label);
  if (icon) menu_icon(parent, pos, icon);
}

static HMENU build_thickness_menu(void)
{
  static const wchar_t *labels[9] = {
    L"1 px", L"2 px", L"3 px", L"4 px", L"5 px", L"6 px", L"12 px",
    L"24 px", L"48 px"
  };
  HMENU sub = CreatePopupMenu();
  int closest = 0, best = 100000, checked = 0;

  for (int i = 0; i < 9; i++)
    {
      int d = abs((int) g.pen_width - (int) g_widths[i]);
      if (d < best) { best = d; closest = i; }
    }
  for (int i = 0; i < 9; i++)
    {
      add_radio(sub, CMD_WIDTH_1 + i, labels[i], i == closest);
      if (i == closest) checked = i;
    }
  CheckMenuRadioItem(sub, CMD_WIDTH_1, CMD_WIDTH_48,
                     CMD_WIDTH_1 + checked, MF_BYCOMMAND);
  return sub;
}

static HMENU build_line_color_menu(void)
{
  HMENU sub = CreatePopupMenu();
  int idx = line_color_index();

  for (int i = 0; i < 8; i++)
    {
      wchar_t wl[32];
      MultiByteToWideChar(CP_UTF8, 0, g_line_colors[i].label, -1, wl, 32);
      add_radio(sub, CMD_COLOR_BASE + i, wl, idx == i);
      menu_color_swatch(sub, CMD_COLOR_BASE + i,
                        color_from_hex(g_line_colors[i].hex, NULL));
    }
  AppendMenuW(sub, MF_SEPARATOR, 0, NULL);
  add_radio(sub, CMD_COLOR_CUSTOM, L"Custom...", idx == 8);
  menu_color_swatch(sub, CMD_COLOR_CUSTOM, g.custom_line_color);
  CheckMenuRadioItem(sub, CMD_COLOR_BASE, CMD_COLOR_CUSTOM,
                     idx == 8 ? CMD_COLOR_CUSTOM : CMD_COLOR_BASE + idx,
                     MF_BYCOMMAND);
  return sub;
}

static HMENU build_fill_color_menu(void)
{
  HMENU sub = CreatePopupMenu();
  int idx = fill_color_index();

  for (int i = 0; i < 9; i++)
    {
      wchar_t wl[32];
      MultiByteToWideChar(CP_UTF8, 0, g_fill_colors[i].label, -1, wl, 32);
      add_radio(sub, CMD_FILL_BASE + i, wl, idx == i);
      menu_color_swatch(sub, CMD_FILL_BASE + i,
                        color_from_hex(g_fill_colors[i].hex, NULL));
    }
  AppendMenuW(sub, MF_SEPARATOR, 0, NULL);
  add_radio(sub, CMD_FILL_CUSTOM, L"Custom...", idx == 9);
  menu_color_swatch(sub, CMD_FILL_CUSTOM, g.custom_fill_color);
  CheckMenuRadioItem(sub, CMD_FILL_BASE, CMD_FILL_CUSTOM,
                     idx == 9 ? CMD_FILL_CUSTOM : CMD_FILL_BASE + idx,
                     MF_BYCOMMAND);
  return sub;
}

static HMENU build_eraser_size_menu(void)
{
  static const wchar_t *labels[4] = {
    L"20 px", L"40 px", L"60 px", L"120 px"
  };
  HMENU sub = CreatePopupMenu();
  int checked = 2;

  for (int i = 0; i < 4; i++)
    {
      add_radio(sub, CMD_ERASER_20 + i, labels[i],
                g.eraser_width == g_eraser_widths[i]);
      if (g.eraser_width == g_eraser_widths[i]) checked = i;
    }
  CheckMenuRadioItem(sub, CMD_ERASER_20, CMD_ERASER_120,
                     CMD_ERASER_20 + checked, MF_BYCOMMAND);
  return sub;
}

static HMENU build_opacity_menu(void)
{
  static const int vals[10] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
  HMENU sub = CreatePopupMenu();
  int checked = -1;

  for (int i = 0; i < 10; i++)
    {
      wchar_t label[8];
      _snwprintf(label, 8, L"%d%%", vals[i]);
      int is = fabs(g.opacity * 100.0 - vals[i]) < 1.0;
      add_radio(sub, CMD_OPACITY_10 + i, label, is);
      if (is) checked = i;
    }
  if (checked >= 0)
    CheckMenuRadioItem(sub, CMD_OPACITY_10, CMD_OPACITY_100,
                       CMD_OPACITY_10 + checked, MF_BYCOMMAND);
  return sub;
}

static HMENU build_marker_opacity_menu(void)
{
  HMENU sub = CreatePopupMenu();
  int checked = -1;

  for (int i = 0; i < 9; i++)
    {
      int pct = (i + 1) * 10;
      wchar_t label[8];
      _snwprintf(label, 8, L"%d%%", pct);
      int is = fabs(g.marker_opacity * 100.0 - pct) < 1.0;
      add_radio(sub, CMD_MARKER_10 + i, label, is);
      if (is) checked = i;
    }
  if (checked >= 0)
    CheckMenuRadioItem(sub, CMD_MARKER_10, CMD_MARKER_90,
                       CMD_MARKER_10 + checked, MF_BYCOMMAND);
  return sub;
}

static const char *shortcut_text(ShortcutId id)
{
  const char *s = g.shortcuts[id];
  return (s && s[0] && _stricmp(s, "None")) ? s : NULL;
}

static void shortcut_label(const wchar_t *base, ShortcutId id,
                           wchar_t *out, int size)
{
  const char *key = shortcut_text(id);
  if (key)
    {
      wchar_t wkey[SHORTCUT_TEXT_MAX];
      MultiByteToWideChar(CP_UTF8, 0, key, -1, wkey, SHORTCUT_TEXT_MAX);
      _snwprintf(out, (size_t) size, L"%s\t%s", base, wkey);
    }
  else
    {
      wcsncpy(out, base, (size_t) size - 1);
      out[size - 1] = 0;
    }
}

static void add_shortcut_item(HMENU menu, UINT id, const wchar_t *base,
                              ShortcutId sc, const char *icon, int active)
{
  wchar_t label[128];
  shortcut_label(base, sc, label, 128);
  add_item(menu, id, label, icon, active);
}

/* ------------------------------------------------------------------ */
/* commands                                                           */
/* ------------------------------------------------------------------ */

static void tool_command(Tool t)
{
  /* The icon and toolbar do not activate their windows.  Put Paper in the
   * foreground after a menu tool selection so its Paper shortcuts, including
   * Ctrl+W, receive the next key even if the menu restored another app. */
  if (g.paper != PAPER_NONE) app_force_foreground(g.overlay);
  if (t == TOOL_MATH) ui_show_math_dialog();
  else tools_request(t);
}

static void paper_ensure_mode(void)
{
  if (g.paper == PAPER_NONE) app_set_paper(PAPER_PLAIN, 0);
}

/* Offer to save before a page is reset or another PDF replaces the current
 * document.  Returns 1 when the action should continue. */
static int paper_confirm_discard(void)
{
  int changed;
  const wchar_t *text;
  int r;

  if (!paper_page_has_document()) return 1;
  if (paper_page_background() == PAPER_PDF)
    {
      changed = paper_doc_has_annotations();
      text = L"This PDF has annotations.\n\n"
             L"Save the annotated PDF before opening another file?";
    }
  else
    {
      changed = paper_page_has_annotations();
      text = L"This page has annotations.\n\n"
             L"Save the paper before resetting the page?";
    }
  if (!changed) return 1;

  ui_prepare_dialog();
  r = MessageBoxW(g.overlay, text, L"Reset Page or PDF File",
                  MB_YESNOCANCEL | MB_ICONQUESTION | MB_TOPMOST);
  if (r == IDCANCEL) return 0;
  if (r == IDYES) paper_save_dialog();
  return 1;
}

static void menu_command(int id)
{
  dlog("menu command %d", id);
  /* Toolbar and floating-icon windows do not activate on click, and their
   * popup menus restore the previous foreground window when they close.
   * Return focus to Paper after a Paper command so its keyboard shortcuts
   * work immediately.  Switch Mode keeps its special bring-to-front behavior
   * below, which lets Alt+Tab continue to cover Paper with another app. */
  if (g.paper != PAPER_NONE && id != CMD_SWITCH_MODE)
    {
      app_note_paper_control_action();
      app_force_foreground(g.overlay);
    }
  if (id == CMD_CURSOR)
    { app_cancel_timer(); tools_exit(); return; }
  if (id == CMD_SWITCH_MODE)
    {
      /* If the paper is behind another application (for example after
       * Alt+Tab), bring it forward instead of leaving Paper mode. */
      if (g.paper != PAPER_NONE)
        {
          HWND fg = GetForegroundWindow();
          DWORD fg_thread = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
          if (fg_thread != GetCurrentThreadId())
            {
              app_force_foreground(g.overlay);
              app_render_all();
              return;
            }
        }
      app_set_paper(g.paper == PAPER_NONE ? PAPER_PLAIN : PAPER_NONE, 0);
      return;
    }
  if (id == CMD_PAPER_OPEN_PDF) { paper_open_pdf_dialog(); return; }
  if (id == CMD_PAPER_RESET)
    {
      /* One contextual action: open a PDF when the current type is PDF,
       * otherwise reset the current page.  Both ask before discarding
       * changed annotations. */
      paper_ensure_mode();
      if (paper_page_background() == PAPER_PDF)
        {
          /* paper_open_pdf_dialog offers to save an annotated PDF. */
          paper_open_pdf_dialog();
        }
      else
        {
          if (!paper_confirm_discard()) return;
          paper_reset_current_page();
        }
      config_save();
      return;
    }
  if (id == CMD_PAPER_SAVE) { paper_ensure_mode(); paper_save_dialog(); return; }
  if (id == CMD_PAPER_INSERT_BEFORE || id == CMD_PAPER_INSERT_AFTER)
    {
      paper_ensure_mode();
      int before = id == CMD_PAPER_INSERT_BEFORE;
      if (!paper_insert_page(before))
        ui_message("The page could not be inserted. The document may already "
                   "contain the maximum of 32 pages.",
                   "Insert Page", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
      config_save();
      return;
    }
  if (id == CMD_PAPER_DELETE)
    {
      paper_ensure_mode();
      if (!paper_delete_page())
        ui_message("A paper must keep at least one page.",
                   "Delete Page", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
      config_save();
      return;
    }
  if (id == CMD_PAPER_PREV)
    {
      paper_ensure_mode();
      paper_prev_page();
      config_save();
      return;
    }
  if (id == CMD_PAPER_NEXT)
    {
      paper_ensure_mode();
      paper_next_page();
      config_save();
      return;
    }
  if (id == CMD_PAPER_GOTO) { paper_ensure_mode(); ui_show_goto_page(); return; }
  if (id == CMD_PAPER_UNDO)
    {
      paper_ensure_mode();
      if (paper_page_undo()) config_save();
      return;
    }
  if (id == CMD_PAPER_REDO)
    {
      paper_ensure_mode();
      if (paper_page_redo()) config_save();
      return;
    }
  if (id == CMD_PAPER_PAGE_INDICATOR) return;
  if (id == CMD_PAPER_CONTINUOUS)
    {
      paper_set_continuous(!g.paper_continuous_scroll);
      config_save();
      return;
    }
  if (id == CMD_TIMER) { app_start_timer(); return; }
  if (id == CMD_SCREENSHOT) { app_take_screenshot(); return; }
  if (id == CMD_SAVE_OR_SHOT)
    {
      if (g.paper != PAPER_NONE && paper_page_has_document())
        menu_command(CMD_PAPER_SAVE);
      else
        app_take_screenshot();
      return;
    }
  if (id == CMD_SETTINGS) { ui_show_settings(); return; }
  if (id == CMD_TOOLBAR_SHOW) { toolbar_toggle(); return; }
  if (id == CMD_TOOLBAR_HORIZ) { toolbar_set_vertical(0); return; }
  if (id == CMD_TOOLBAR_VERT) { toolbar_set_vertical(1); return; }
  if (id >= CMD_TOOLBAR_16 && id <= CMD_TOOLBAR_48)
    {
      static const int sizes[4] = { 16, 24, 32, 48 };
      toolbar_set_icon_size(sizes[id - CMD_TOOLBAR_16]);
      return;
    }
  if (id == CMD_TOOLBAR_CONFIGURE) { toolbar_show_configure(); return; }
  if (id == CMD_SHORTCUTS) { ui_show_shortcuts(); return; }
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
  if (id >= CMD_PAPER_NONE && id <= CMD_PAPER_PDF)
    {
      Paper wanted = (Paper) (id - CMD_PAPER_NONE);
      if (wanted == PAPER_NONE)
        {
          app_set_paper(PAPER_NONE, 0);
          config_save();
          return;
        }
      if (wanted == PAPER_PDF && !paper_pdf_available())
        {
          /* Let the user choose a file; the dialog opens and switches to it. */
          if (!paper_open_pdf_dialog()) return;
          config_save();
          return;
        }
      /* Enter Paper (saving the screen layer) and then select the document. */
      app_select_paper_type(wanted);
      config_save();
      return;
    }
  if (id >= CMD_ERASER_20 && id <= CMD_ERASER_120)
    {
      apply_eraser_width(g_eraser_widths[id - CMD_ERASER_20]);
      return;
    }
  if (id >= CMD_OPACITY_10 && id <= CMD_OPACITY_100)
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
    case CMD_RESET: ui_reset_settings(); return;
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

void ui_execute_command(int id)
{
  menu_command(id);
}

/* ------------------------------------------------------------------ */
/* main menu                                                          */
/* ------------------------------------------------------------------ */

static HMENU build_menu(void)
{
  HMENU m = CreatePopupMenu();
  HMENU sub;
  int i;

  add_item(m, CMD_CURSOR, L"Cursor (Esc)", "cursor.png", g.tool == TOOL_NONE);
  add_shortcut_item(m, CMD_PEN, L"Pen", SK_PEN, "pen.png",
                    g.tool == TOOL_PEN);
  add_shortcut_item(m, CMD_TEXT, L"Text", SK_TEXT, "text.png",
                    g.tool == TOOL_TEXT);
  add_item(m, CMD_ERASER, L"Eraser", "eraser.png", g.tool == TOOL_ERASER);
  add_shortcut_item(m, CMD_TIMER, L"Timer", SK_TIMER, "timer.png",
                    g.timer_running);
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);

  sub = add_sub(m, L"Shapes", "shapes.png");
  add_item(sub, CMD_MARKER, L"Marker", "marker.png", g.tool == TOOL_MARKER);
  add_item(sub, CMD_LINE, L"Line", "free-line.png", g.tool == TOOL_LINE);
  add_item(sub, CMD_SPLINE, L"Spline", "spline.png", g.tool == TOOL_SPLINE);
  add_item(sub, CMD_POLYGON, L"Polygon", "polygon.png", g.tool == TOOL_POLYGON);
  add_item(sub, CMD_TRIANGLE, L"Triangle", "triangle.png", g.tool == TOOL_TRIANGLE);
  add_shortcut_item(sub, CMD_ARROW, L"Arrow", SK_ARROW, "arrow.png",
                    g.tool == TOOL_ARROW);
  add_item(sub, CMD_ELLIPSE, L"Ellipse", "ellipse.png", g.tool == TOOL_ELLIPSE);
  add_item(sub, CMD_RRECT, L"Rounded Rectangle", "rounded-rectangle.png",
           g.tool == TOOL_RRECT);
  add_item(sub, CMD_BUCKET, L"Paint Bucket", "bucket.png", g.tool == TOOL_BUCKET);
  add_item(sub, CMD_MATH, L"Math Function", "math.png", g.tool == TOOL_MATH);
  add_item(sub, CMD_BARS, L"Parallel Lines", "bars.png", g.tool == TOOL_BARS);
  add_item(sub, CMD_RANDOM, L"Random Signal", "random-signal.png",
           g.tool == TOOL_RANDOM);

  sub = add_sub(m, L"Actions", "actions.png");
  add_shortcut_item(sub, CMD_SWITCH_MODE, L"Switch Mode", SK_SWITCH,
                    "switch_mode.png", g.paper != PAPER_NONE);
  add_shortcut_item(sub, CMD_UNDO, L"Undo", SK_UNDO, "undo.png", 0);
  add_shortcut_item(sub, CMD_REDO, L"Redo", SK_REDO, "redo.png", 0);
  add_item(sub, CMD_CLEAR, L"Clear Screen", "trash.png", 0);
  if (g.paper == PAPER_NONE)
    add_shortcut_item(sub, CMD_VISIBILITY, L"Toggle Visibility",
                      SK_PAPER_T, "hide.png", 0);
  add_shortcut_item(sub, CMD_SCREENSHOT, L"Screenshot", SK_SHOT,
                    "screenshot.png", 0);
  {
    wchar_t label[128];
    shortcut_label(L"Show Toolbar", SK_TOOLBAR, label, 128);
    AppendMenuW(sub, MF_STRING | (g.show_toolbar ? MF_CHECKED : 0),
                CMD_TOOLBAR_SHOW, label);
  }
  {
    wchar_t label[128];
    shortcut_label(L"Show Floating Icon", SK_ICON, label, 128);
    AppendMenuW(sub, MF_STRING | (g.show_icon ? MF_CHECKED : 0),
                CMD_SHOW_ICON, label);
  }
  add_item(sub, CMD_SHOW_HINTS, L"Show Hints", "hint.png", 0);

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
  add_popup(m, build_thickness_menu(), L"Line Thickness", NULL);

  /* Line colour. */
  add_popup(m, build_line_color_menu(), L"Line Color", NULL);

  /* Shape fill colour. */
  add_popup(m, build_fill_color_menu(), L"Shapes Fill Color", NULL);

  /* Paper pages. */
  sub = add_sub(m, L"Paper", "paper.png");
  {
    static const wchar_t *names[4] =
      { L"Plain", L"Ruled", L"Graph", L"PDF File" };
    for (i = 0; i < 4; i++) add_radio(sub, CMD_PAPER_PLAIN + i, names[i],
                                      (int) g.paper == i + 1);
    if (g.paper >= PAPER_PLAIN && g.paper <= PAPER_PDF)
      CheckMenuRadioItem(sub, CMD_PAPER_PLAIN, CMD_PAPER_PDF,
                         CMD_PAPER_PLAIN + ((int) g.paper - 1), MF_BYCOMMAND);

    int pcount = paper_page_count();
    int active = g.paper != PAPER_NONE;

    AppendMenuW(sub, MF_SEPARATOR, 0, NULL);
    AppendMenuW(sub, MF_STRING | (pcount > 0 ? 0 : MF_GRAYED),
                CMD_PAPER_GOTO, L"Go to Page...");
    AppendMenuW(sub, MF_SEPARATOR, 0, NULL);
    AppendMenuW(sub, MF_STRING | (pcount < PAPER_MAX_PAGES ? 0 : MF_GRAYED),
                CMD_PAPER_INSERT_BEFORE, L"Insert Page Before\tCtrl+B");
    AppendMenuW(sub, MF_STRING | (pcount < PAPER_MAX_PAGES ? 0 : MF_GRAYED),
                CMD_PAPER_INSERT_AFTER, L"Insert Page After\tCtrl+A");
    AppendMenuW(sub, MF_STRING | (pcount > 1 ? 0 : MF_GRAYED),
                CMD_PAPER_DELETE, L"Delete Page");
    AppendMenuW(sub, MF_SEPARATOR, 0, NULL);
    AppendMenuW(sub, MF_STRING, CMD_PAPER_RESET, L"Reset Page or PDF File");
    AppendMenuW(sub, MF_STRING | (active && paper_page_has_document()
                                  ? 0 : MF_GRAYED),
                CMD_PAPER_SAVE, L"Save Paper...\tCtrl+S");
  }

  /* Eraser size. */
  add_popup(m, build_eraser_size_menu(), L"Eraser Size", NULL);

  /* Opacity: 10% to 90% in steps of 10. */
  add_popup(m, build_opacity_menu(), L"Opacity", "opacity.png");

  /* Marker opacity. */
  add_popup(m, build_marker_opacity_menu(), L"Marker Opacity",
            "marker_opacity.png");

  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  add_item(m, CMD_SETTINGS, L"Settings...", "settings.png", 0);
  add_item(m, CMD_SHOW_HINTS, L"Show Hints", "hint.png", 0);

  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  add_item(m, CMD_ABOUT, L"About", "screenator.png", 0);
  add_shortcut_item(m, CMD_QUIT, L"Quit", SK_QUIT, "quit.png", 0);
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
  menu_color_bitmaps_clear();
  menu_owner_restore(owner, saved, changed);
  if (prev && prev != owner && IsWindow(prev)) SetForegroundWindow(prev);
  dlog("menu close cmd=%d", cmd);
  app_menu_capture_stop(cmd == 0);
  ui_menu_finish();

  if (cmd) menu_command(cmd);
}

static HMENU build_paper_type_menu(void)
{
  HMENU m = CreatePopupMenu();
  static const wchar_t *names[4] =
    { L"Plain", L"Ruled", L"Graph", L"PDF File" };
  for (int i = 0; i < 4; i++)
    add_radio(m, CMD_PAPER_PLAIN + i, names[i], (int) g.paper == i + 1);
  if (g.paper >= PAPER_PLAIN && g.paper <= PAPER_PDF)
    CheckMenuRadioItem(m, CMD_PAPER_PLAIN, CMD_PAPER_PDF,
                       CMD_PAPER_PLAIN + ((int) g.paper - 1), MF_BYCOMMAND);
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  AppendMenuW(m, MF_STRING, CMD_PAPER_RESET, L"Reset Page or PDF File");
  return m;
}

static HMENU build_shapes_menu(void)
{
  HMENU m = CreatePopupMenu();
  add_item(m, CMD_MARKER, L"Marker", "marker.png", g.tool == TOOL_MARKER);
  add_item(m, CMD_LINE, L"Line", "free-line.png", g.tool == TOOL_LINE);
  add_item(m, CMD_SPLINE, L"Spline", "spline.png", g.tool == TOOL_SPLINE);
  add_item(m, CMD_POLYGON, L"Polygon", "polygon.png", g.tool == TOOL_POLYGON);
  add_item(m, CMD_TRIANGLE, L"Triangle", "triangle.png",
           g.tool == TOOL_TRIANGLE);
  add_item(m, CMD_ARROW, L"Arrow\tF6", "arrow.png", g.tool == TOOL_ARROW);
  add_item(m, CMD_ELLIPSE, L"Ellipse", "ellipse.png", g.tool == TOOL_ELLIPSE);
  add_item(m, CMD_RRECT, L"Rounded Rectangle", "rounded-rectangle.png",
           g.tool == TOOL_RRECT);
  add_item(m, CMD_BUCKET, L"Paint Bucket", "bucket.png",
           g.tool == TOOL_BUCKET);
  add_item(m, CMD_MATH, L"Math Function", "math.png", g.tool == TOOL_MATH);
  add_item(m, CMD_BARS, L"Parallel Lines", "bars.png", g.tool == TOOL_BARS);
  add_item(m, CMD_RANDOM, L"Random Signal", "random-signal.png",
           g.tool == TOOL_RANDOM);
  return m;
}

void ui_show_toolbar_menu(int popup, int x, int y)
{
  HMENU m = NULL;

  switch (popup)
    {
    case TB_POPUP_ERASER_SIZE: m = build_eraser_size_menu(); break;
    case TB_POPUP_LINE_COLOR: m = build_line_color_menu(); break;
    case TB_POPUP_THICKNESS: m = build_thickness_menu(); break;
    case TB_POPUP_FILL_COLOR: m = build_fill_color_menu(); break;
    case TB_POPUP_OPACITY: m = build_opacity_menu(); break;
    case TB_POPUP_MARKER_OPACITY: m = build_marker_opacity_menu(); break;
    case TB_POPUP_SHAPES: m = build_shapes_menu(); break;
    case TB_POPUP_PAPER_TYPE: m = build_paper_type_menu(); break;
    default: return;
    }
  if (!m) return;
  if (g.menu_open)
    { DestroyMenu(m); menu_color_bitmaps_clear(); return; }

  g.menu_open = 1;
  app_menu_capture_start(0, 0);

  HWND owner = menu_owner();
  HWND prev = GetForegroundWindow();
  LONG_PTR saved = 0;
  int changed = menu_owner_activate(owner, &saved);

  int cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_LEFTALIGN |
                             TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                             x, y, owner, NULL);
  DestroyMenu(m);
  menu_color_bitmaps_clear();
  menu_owner_restore(owner, saved, changed);
  if (prev && prev != owner && IsWindow(prev)) SetForegroundWindow(prev);
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
  toolbar_refresh();
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
  /* Reset also wipes every drawing: the screen annotation layer, the drawing
   * history and all paper documents. */
  tools_clear();
  tools_clear_undo();
  paper_reset();
  ui_apply_pen();
  ui_position_icon();
  ui_update_icon();
  toolbar_reload();
  toolbar_place_top_center();
  app_hotkeys_refresh();
  /* Remove the complete configuration directory, including hints and saved
   * annotations.  Defaults stay in memory and are written on the next save. */
  config_remove_dir();
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
  ui_prepare_dialog();
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

  ui_prepare_dialog();
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
  run_modal(h, 0);
}

/* ------------------------------------------------------------------ */
/* go to page dialog                                                  */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK goto_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
    {
    case WM_CREATE:
      {
        int count = paper_page_count();
        int current = paper_page_index() + 1;
        wchar_t prompt[128];
        _snwprintf(prompt, 128, L"Page number (1-%d):", count > 0 ? count : 1);

        HWND st = CreateWindowW(L"STATIC", prompt, WS_CHILD | WS_VISIBLE,
                                18, 16, 300, 22, h, NULL, g.hinst, NULL);
        set_ui_font(st);

        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                    ES_NUMBER | ES_AUTOHSCROLL,
                                    18, 44, 100, 26, h,
                                    (HMENU) (INT_PTR) ID_GOTO_EDIT, g.hinst,
                                    NULL);
        set_ui_font(edit);
        wchar_t num[32];
        _snwprintf(num, 32, L"%d", current > 0 ? current : 1);
        SetWindowTextW(edit, num);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        SetFocus(edit);

        HWND err = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                 18, 78, 310, 24, h,
                                 (HMENU) (INT_PTR) ID_GOTO_ERR, g.hinst,
                                 NULL);
        set_ui_font(err);

        HWND ok = CreateWindowW(L"BUTTON", L"OK",
                                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                150, 112, 80, 28, h, (HMENU) IDOK, g.hinst,
                                NULL);
        HWND ca = CreateWindowW(L"BUTTON", L"Cancel",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                240, 112, 80, 28, h, (HMENU) IDCANCEL,
                                g.hinst, NULL);
        set_ui_font(ok);
        set_ui_font(ca);
      }
      return 0;

    case WM_COMMAND:
      if (LOWORD(wp) == IDOK)
        {
          wchar_t text[32];
          GetDlgItemTextW(h, ID_GOTO_EDIT, text, 32);
          wchar_t *end = NULL;
          long n = wcstol(text, &end, 10);
          int count = paper_page_count();
          if (end == text || n < 1 || n > count)
            {
              wchar_t err[128];
              _snwprintf(err, 128, L"Enter a page between 1 and %d.",
                         count > 0 ? count : 1);
              SetDlgItemTextW(h, ID_GOTO_ERR, err);
              return 0;
            }
          paper_goto_page((int) n - 1);
          config_save();
          DestroyWindow(h);
        }
      else if (LOWORD(wp) == IDCANCEL)
        DestroyWindow(h);
      return 0;

    case WM_CLOSE:
      DestroyWindow(h);
      return 0;

    case WM_CTLCOLORSTATIC:
      if (GetDlgCtrlID((HWND) lp) == ID_GOTO_ERR)
        SetTextColor((HDC) wp, RGB(180, 0, 0));
      SetBkColor((HDC) wp, GetSysColor(COLOR_WINDOW));
      return (LRESULT) GetSysColorBrush(COLOR_WINDOW);
    }
  return DefWindowProcW(h, msg, wp, lp);
}

void ui_show_goto_page(void)
{
  if (!paper_page_has_document())
    {
      ui_message("There is no paper document to navigate.",
                 "Go to Page", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
      return;
    }

  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.hInstance = g.hinst;
  wc.lpfnWndProc = goto_proc;
  wc.lpszClassName = GOTO_CLASS;
  wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  RegisterClassExW(&wc);

  HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, GOTO_CLASS,
                           L"Go to Page", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                           CW_USEDEFAULT, CW_USEDEFAULT, 350, 190, g.overlay,
                           NULL, g.hinst, NULL);
  center_window(h, 350, 190);
  run_modal(h, 0);
}

/* ------------------------------------------------------------------ */
/* settings dialog                                                    */
/* ------------------------------------------------------------------ */

static void settings_sync(HWND h)
{
  static int syncing;
  if (syncing) return;
  syncing = 1;
  CheckDlgButton(h, ID_SET_SAVE,
                 g.save_on_exit ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(h, ID_SET_IND,
                 g.show_indicator ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(h, ID_SET_CONTINUOUS,
                 g.paper_continuous_scroll ? BST_CHECKED : BST_UNCHECKED);
  {
    int psel = g.paper_size_kind;
    wchar_t num[16];
    if (psel < PAPER_SIZE_SCREEN || psel > PAPER_SIZE_CUSTOM)
      psel = PAPER_SIZE_SCREEN;
    SendDlgItemMessageW(h, ID_SET_PAPER_SIZE, CB_SETCURSEL, (WPARAM) psel, 0);
    _snwprintf(num, 16, L"%d", g.paper_custom_w_mm);
    SetDlgItemTextW(h, ID_SET_PAPER_W, num);
    _snwprintf(num, 16, L"%d", g.paper_custom_h_mm);
    SetDlgItemTextW(h, ID_SET_PAPER_H, num);
  }
  CheckRadioButton(h, ID_SET_HORIZ, ID_SET_VERT,
                   g.toolbar_vertical ? ID_SET_VERT : ID_SET_HORIZ);
  SendDlgItemMessageW(h, ID_SET_SIZE, TBM_SETRANGE, TRUE,
                      MAKELPARAM(16, 48));
  SendDlgItemMessageW(h, ID_SET_SIZE, TBM_SETPOS, TRUE,
                      g.toolbar_icon_size);
  {
    wchar_t sz[32];
    _snwprintf(sz, 32, L"%d px", g.toolbar_icon_size);
    SetDlgItemTextW(h, ID_SET_SIZE_VAL, sz);
  }

  static const unsigned icons[7] = { 16, 24, 32, 48, 64, 128, 256 };
  int isz = 3;
  for (int i = 0; i < 7; i++) if (g.icon_size == icons[i]) isz = i;
  SendDlgItemMessageW(h, ID_SET_ICON_SIZE, CB_SETCURSEL, isz, 0);

  syncing = 0;
}

static HWND set_control(HWND parent, const wchar_t *cls, const wchar_t *text,
                        DWORD style, int x, int y, int w, int h, int id)
{
  HWND c = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style,
                         x, y, w, h, parent, (HMENU) (INT_PTR) id,
                         g.hinst, NULL);
  if (c) set_ui_font(c);
  return c;
}

static HWND settings_ctl(HWND h, int tab, const wchar_t *cls,
                         const wchar_t *text, DWORD style, int x, int y,
                         int w, int ht, int id)
{
  HWND c = set_control(h, cls, text, style, x, y, w, ht, id);
  if (c) ShowWindow(c, tab == 0 ? SW_SHOW : SW_HIDE);
  return c;
}

static void settings_show(HWND h, int id, int show)
{
  HWND c = GetDlgItem(h, id);
  if (c) ShowWindow(c, show ? SW_SHOW : SW_HIDE);
}

static void settings_show_tab(HWND h, int tab)
{
  for (int id = ID_SET_SAVE; id <= ID_SET_OPEN_CFG; id++)
    if (id != ID_SET_TAB) settings_show(h, id, 0);
  settings_show(h, CMD_PAPER_NONE, 0);
  settings_show(h, CMD_PAPER_PLAIN, 0);
  settings_show(h, CMD_PAPER_RULED, 0);
  settings_show(h, CMD_PAPER_GRAPH, 0);
  settings_show(h, ID_SET_SIZE_VAL, 0);
  settings_show(h, ID_SET_PAPER_SIZE_LABEL, 0);
  settings_show(h, ID_SET_PAPER_SIZE, 0);
  settings_show(h, ID_SET_PAPER_W_LABEL, 0);
  settings_show(h, ID_SET_PAPER_W, 0);
  settings_show(h, ID_SET_PAPER_H_LABEL, 0);
  settings_show(h, ID_SET_PAPER_H, 0);

  if (tab == 0)
    {
      settings_show(h, ID_SET_SAVE, 1);
      settings_show(h, ID_SET_IND, 1);
      settings_show(h, ID_SET_FONT, 1);
      settings_show(h, ID_SET_SC, 1);
      settings_show(h, ID_SET_RESET, 1);
      settings_show(h, ID_SET_OPEN_CFG, 1);
      settings_show(h, ID_SET_LABEL_PATH, 1);
    }
  else if (tab == 1)
    {
      settings_show(h, ID_SET_PAPER_SIZE_LABEL, 1);
      settings_show(h, ID_SET_PAPER_SIZE, 1);
      if (g.paper_size_kind == PAPER_SIZE_CUSTOM)
        {
          settings_show(h, ID_SET_PAPER_W_LABEL, 1);
          settings_show(h, ID_SET_PAPER_W, 1);
          settings_show(h, ID_SET_PAPER_H_LABEL, 1);
          settings_show(h, ID_SET_PAPER_H, 1);
        }
      settings_show(h, ID_SET_CONTINUOUS, 1);
    }
  else if (tab == 2)
    {
      settings_show(h, ID_SET_HORIZ, 1);
      settings_show(h, ID_SET_VERT, 1);
      settings_show(h, ID_SET_LABEL_TOOL_SIZE, 1);
      settings_show(h, ID_SET_SIZE, 1);
      settings_show(h, ID_SET_SIZE_VAL, 1);
      settings_show(h, ID_SET_CFG, 1);
    }
  else
    {
      settings_show(h, ID_SET_LABEL_ICON_SIZE, 1);
      settings_show(h, ID_SET_ICON_SIZE, 1);
      settings_show(h, ID_SET_POS2, 1);
    }
}

static void ui_open_config_folder(void)
{
  if (!g.config_dir[0]) return;
  ShellExecuteW(g.overlay, L"open", g.config_dir, NULL, NULL, SW_SHOWNORMAL);
}

static void settings_apply_icon_size(HWND h)
{
  static const unsigned vals[7] = { 16, 24, 32, 48, 64, 128, 256 };
  int sel = (int) SendDlgItemMessageW(h, ID_SET_ICON_SIZE, CB_GETCURSEL, 0, 0);
  if (sel < 0 || sel >= 7) return;
  g.icon_size = vals[sel];
  ui_position_icon();
  app_refresh_icon_image();
  ui_update_icon();
  config_save();
}

static void settings_apply_paper_size(HWND h)
{
  int sel = (int) SendDlgItemMessageW(h, ID_SET_PAPER_SIZE, CB_GETCURSEL, 0, 0);
  int w, hgt;
  BOOL ok = FALSE;

  if (sel < PAPER_SIZE_SCREEN || sel > PAPER_SIZE_CUSTOM)
    sel = PAPER_SIZE_SCREEN;
  g.paper_size_kind = sel;
  if (sel == PAPER_SIZE_CUSTOM)
    {
      w = (int) GetDlgItemInt(h, ID_SET_PAPER_W, &ok, FALSE);
      if (!ok || w < 10) w = g.paper_custom_w_mm;
      if (w < 10) w = 210;
      if (w > 2000) w = 2000;
      hgt = (int) GetDlgItemInt(h, ID_SET_PAPER_H, &ok, FALSE);
      if (!ok || hgt < 10) hgt = g.paper_custom_h_mm;
      if (hgt < 10) hgt = 297;
      if (hgt > 2000) hgt = 2000;
      g.paper_custom_w_mm = w;
      g.paper_custom_h_mm = hgt;
    }
  {
    int tab = (int) TabCtrl_GetCurSel(GetDlgItem(h, ID_SET_TAB));
    if (tab < 0) tab = 0;
    settings_show_tab(h, tab);
  }
  ui_resize_canvas();
  config_save();
}

static LRESULT CALLBACK settings_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
    {
    case WM_CREATE:
      {
        HWND tab = set_control(h, WC_TABCONTROLW, L"", WS_CLIPSIBLINGS,
                               15, 10, 450, 300, ID_SET_TAB);
        TCITEMW item;
        memset(&item, 0, sizeof(item));
        item.mask = TCIF_TEXT;
        item.pszText = L"General";
        TabCtrl_InsertItem(tab, 0, &item);
        item.pszText = L"Paper";
        TabCtrl_InsertItem(tab, 1, &item);
        item.pszText = L"Toolbar";
        TabCtrl_InsertItem(tab, 2, &item);
        item.pszText = L"Floating Icon";
        TabCtrl_InsertItem(tab, 3, &item);
      }
      settings_ctl(h, 0, L"BUTTON", L"Save annotations on exit",
                   BS_AUTOCHECKBOX, 30, 55, 300, 24, ID_SET_SAVE);
      settings_ctl(h, 0, L"BUTTON",
                   L"Show settings indicator on Floating Icon",
                   BS_AUTOCHECKBOX, 30, 83, 380, 24, ID_SET_IND);
      settings_ctl(h, 0, L"BUTTON", L"Change Font...", BS_PUSHBUTTON,
                   30, 115, 160, 28, ID_SET_FONT);
      settings_ctl(h, 0, L"BUTTON", L"Edit Shortcuts...", BS_PUSHBUTTON,
                   210, 115, 160, 28, ID_SET_SC);
      settings_ctl(h, 0, L"BUTTON", L"Reset Settings", BS_PUSHBUTTON,
                   30, 155, 160, 28, ID_SET_RESET);
      settings_ctl(h, 0, L"BUTTON", L"Open Configuration Folder",
                   BS_PUSHBUTTON, 210, 155, 190, 28, ID_SET_OPEN_CFG);
      settings_ctl(h, 0, L"STATIC",
                   L"Settings are stored in %APPDATA%\\Screenator",
                   SS_LEFT, 30, 200, 380, 22, ID_SET_LABEL_PATH);

      settings_ctl(h, 1, L"STATIC", L"Paper size", SS_LEFT,
                   30, 55, 90, 22, ID_SET_PAPER_SIZE_LABEL);
      {
        HWND cb = settings_ctl(h, 1, L"COMBOBOX", L"", CBS_DROPDOWNLIST |
                               WS_VSCROLL, 130, 51, 200, 200,
                               ID_SET_PAPER_SIZE);
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM) L"Screen");
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM) L"A4 portrait");
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM) L"A4 landscape");
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM) L"Custom");
      }
      settings_ctl(h, 1, L"STATIC", L"Width (mm)", SS_LEFT,
                   30, 92, 90, 22, ID_SET_PAPER_W_LABEL);
      settings_ctl(h, 1, L"EDIT", L"210",
                   ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
                   130, 88, 70, 24, ID_SET_PAPER_W);
      settings_ctl(h, 1, L"STATIC", L"Height (mm)", SS_LEFT,
                   215, 92, 90, 22, ID_SET_PAPER_H_LABEL);
      settings_ctl(h, 1, L"EDIT", L"297",
                   ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
                   315, 88, 70, 24, ID_SET_PAPER_H);
      settings_ctl(h, 1, L"BUTTON", L"Continuous page scrolling",
                   BS_AUTOCHECKBOX, 30, 130, 320, 24, ID_SET_CONTINUOUS);

      settings_ctl(h, 2, L"BUTTON", L"Horizontal",
                   BS_AUTORADIOBUTTON | WS_GROUP, 30, 55, 100, 24,
                   ID_SET_HORIZ);
      settings_ctl(h, 2, L"BUTTON", L"Vertical", BS_AUTORADIOBUTTON,
                   145, 55, 90, 24, ID_SET_VERT);
      settings_ctl(h, 2, L"STATIC", L"Icon Size", SS_LEFT,
                   30, 90, 70, 22, ID_SET_LABEL_TOOL_SIZE);
      {
        HWND tb = settings_ctl(h, 2, TRACKBAR_CLASSW, L"",
                               WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
                               100, 84, 170, 32, ID_SET_SIZE);
        SendMessageW(tb, TBM_SETRANGE, TRUE, MAKELPARAM(16, 48));
        SendMessageW(tb, TBM_SETPOS, TRUE, g.toolbar_icon_size);
      }
      settings_ctl(h, 2, L"STATIC", L"", SS_LEFT,
                   278, 90, 70, 22, ID_SET_SIZE_VAL);
      settings_ctl(h, 2, L"BUTTON", L"Configure Toolbar...", BS_PUSHBUTTON,
                   30, 125, 180, 28, ID_SET_CFG);

      settings_ctl(h, 3, L"STATIC", L"Floating Icon Size", SS_LEFT,
                   30, 55, 120, 22, ID_SET_LABEL_ICON_SIZE);
      {
        HWND cb = settings_ctl(h, 3, L"COMBOBOX", L"", CBS_DROPDOWNLIST |
                               WS_VSCROLL, 160, 51, 100, 200,
                               ID_SET_ICON_SIZE);
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM) L"16x16");
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM) L"24x24");
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM) L"32x32");
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM) L"48x48");
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM) L"64x64");
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM) L"128x128");
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM) L"256x256");
      }
      settings_ctl(h, 3, L"BUTTON", L"Floating Icon Position...",
                   BS_PUSHBUTTON, 30, 90, 210, 28, ID_SET_POS2);

      settings_ctl(h, 0, L"BUTTON", L"Close", BS_DEFPUSHBUTTON,
                   350, 330, 105, 30, IDOK);
      settings_show_tab(h, 0);
      settings_sync(h);
      return 0;

    case WM_NOTIFY:
      if (((NMHDR *) lp)->idFrom == ID_SET_TAB &&
          ((NMHDR *) lp)->code == TCN_SELCHANGE)
        {
          int sel = TabCtrl_GetCurSel(GetDlgItem(h, ID_SET_TAB));
          settings_show_tab(h, sel < 0 ? 0 : sel);
        }
      return 0;

    case WM_HSCROLL:
      {
        HWND tb = (HWND) lp;
        if (tb && GetDlgCtrlID(tb) == ID_SET_SIZE)
          {
            int v = (int) SendMessageW(tb, TBM_GETPOS, 0, 0);
            wchar_t sz[32];
            toolbar_set_icon_size(v);
            _snwprintf(sz, 32, L"%d px", v);
            SetDlgItemTextW(h, ID_SET_SIZE_VAL, sz);
          }
      }
      return 0;

    case WM_COMMAND:
      switch (LOWORD(wp))
        {
        case ID_SET_SAVE: ui_execute_command(CMD_SAVE_ON_EXIT); break;
        case ID_SET_IND: ui_execute_command(CMD_SHOW_INDICATOR); break;
        case ID_SET_FONT: ui_execute_command(CMD_FONT); break;
        case ID_SET_POS:
        case ID_SET_POS2: ui_execute_command(CMD_ICON_POS); break;
        case ID_SET_HORIZ: ui_execute_command(CMD_TOOLBAR_HORIZ); break;
        case ID_SET_VERT: ui_execute_command(CMD_TOOLBAR_VERT); break;
        case ID_SET_CFG: ui_execute_command(CMD_TOOLBAR_CONFIGURE); break;
        case ID_SET_SC:
        case ID_SET_SC2: ui_execute_command(CMD_SHORTCUTS); break;
        case ID_SET_RESET: ui_execute_command(CMD_RESET); break;
        case ID_SET_OPEN_PDF: ui_execute_command(CMD_PAPER_OPEN_PDF); break;
        case ID_SET_SAVE_PAPER: ui_execute_command(CMD_PAPER_SAVE); break;
        case ID_SET_OPEN_CFG: ui_open_config_folder(); break;
        case ID_SET_CONTINUOUS:
          paper_set_continuous(IsDlgButtonChecked(h, ID_SET_CONTINUOUS) ==
                               BST_CHECKED);
          config_save();
          break;
        case ID_SET_SHOW_ICON: ui_execute_command(CMD_SHOW_ICON); break;
        case CMD_PAPER_NONE:
        case CMD_PAPER_PLAIN:
        case CMD_PAPER_RULED:
        case CMD_PAPER_GRAPH:
          ui_execute_command(LOWORD(wp));
          break;
        case ID_SET_ICON_SIZE:
          if (HIWORD(wp) == CBN_SELCHANGE) settings_apply_icon_size(h);
          return 0;
        case ID_SET_PAPER_SIZE:
          if (HIWORD(wp) == CBN_SELCHANGE) settings_apply_paper_size(h);
          return 0;
        case ID_SET_PAPER_W:
        case ID_SET_PAPER_H:
          if (HIWORD(wp) == EN_KILLFOCUS) settings_apply_paper_size(h);
          return 0;
        case IDOK:
          settings_apply_icon_size(h);
          settings_apply_paper_size(h);
          DestroyWindow(h);
          return 0;
        case IDCANCEL:
          DestroyWindow(h);
          return 0;
        }
      settings_sync(h);
      return 0;

    case WM_CLOSE:
      DestroyWindow(h);
      return 0;
    }
  return DefWindowProcW(h, msg, wp, lp);
}

void ui_show_settings(void)
{
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.hInstance = g.hinst;
  wc.lpfnWndProc = settings_proc;
  wc.lpszClassName = SETTINGS_CLASS;
  wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  RegisterClassExW(&wc);

  HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, SETTINGS_CLASS,
                           L"Screenator Settings", WS_POPUP | WS_CAPTION |
                           WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 480, 410,
                           g.overlay, NULL, g.hinst, NULL);
  center_window(h, 480, 410);
  run_modal(h, 0);
}
/* ------------------------------------------------------------------ */
/* shortcuts dialog                                                   */
/* ------------------------------------------------------------------ */

static int shortcuts_collect(HWND h,
                             char out[SK_COUNT][SHORTCUT_TEXT_MAX],
                             char *error, int error_size)
{
  UINT mods[SK_COUNT], keys[SK_COUNT];

  for (int i = 0; i < SK_COUNT; i++)
    {
      wchar_t wbuf[SHORTCUT_TEXT_MAX];
      GetDlgItemTextW(h, ID_SC_EDIT + i, wbuf, SHORTCUT_TEXT_MAX);
      char *s = u_wide(wbuf);
      char *text = s ? s : (char *) "";
      mods[i] = keys[i] = 0;

      if (!shortcut_parse(text, &mods[i], &keys[i]))
        {
          snprintf(error, (size_t) error_size,
                   "%s: use a shortcut such as F8, Ctrl+S, Alt+F4 or None.",
                   g_shortcut_labels[i]);
          free(s);
          return 0;
        }
      if (keys[i] == VK_ESCAPE)
        {
          snprintf(error, (size_t) error_size,
                   "%s: Escape is fixed and cannot be changed.",
                   g_shortcut_labels[i]);
          free(s);
          return 0;
        }
      if (mods[i] == MOD_CONTROL && keys[i] == VK_F10)
        {
          snprintf(error, (size_t) error_size,
                   "%s: Ctrl+F10 is reserved for the quick tool menu.",
                   g_shortcut_labels[i]);
          free(s);
          return 0;
        }
      if (!shortcut_format(mods[i], keys[i], out[i], SHORTCUT_TEXT_MAX))
        {
          snprintf(error, (size_t) error_size,
                   "%s: invalid shortcut.", g_shortcut_labels[i]);
          free(s);
          return 0;
        }
      free(s);
    }

  for (int i = 0; i < SK_COUNT; i++)
    {
      if (!keys[i]) continue;
      for (int j = 0; j < i; j++)
        if (keys[j] == keys[i] && mods[j] == mods[i])
          {
            snprintf(error, (size_t) error_size,
                     "%s and %s use the same shortcut (%s).",
                     g_shortcut_labels[j], g_shortcut_labels[i], out[i]);
            return 0;
          }
    }
  return 1;
}

static LRESULT CALLBACK shortcuts_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
    {
    case WM_CREATE:
      {
        HWND st = CreateWindowW(L"STATIC",
          L"Type a shortcut such as F8, Ctrl+S, Alt+F4 or None. "
          L"Escape stays fixed. Press OK to register the new keys.",
          WS_CHILD | WS_VISIBLE, 16, 10, 520, 34, h,
          (HMENU) (INT_PTR) ID_SC_NOTE, g.hinst, NULL);
        ui_set_font(st);

        for (int i = 0; i < SK_COUNT; i++)
          {
            wchar_t *wlabel = w_utf8(g_shortcut_labels[i]);
            st = CreateWindowW(L"STATIC", wlabel ? wlabel : L"",
                               WS_CHILD | WS_VISIBLE | SS_RIGHT,
                               16, 56 + i * 30, 180, 22, h, NULL, g.hinst,
                               NULL);
            ui_set_font(st);
            free(wlabel);

            HWND ed = CreateWindowExW(
              WS_EX_CLIENTEDGE, L"EDIT", L"",
              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
              204, 52 + i * 30, 320, 24, h,
              (HMENU) (INT_PTR) (ID_SC_EDIT + i), g.hinst, NULL);
            ui_set_font(ed);

            wchar_t *wkey = w_utf8(g.shortcuts[i]);
            SetWindowTextW(ed, wkey ? wkey : L"");
            free(wkey);
          }

        st = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                           16, 456, 520, 38, h,
                           (HMENU) (INT_PTR) ID_SC_ERR, g.hinst, NULL);
        ui_set_font(st);

        HWND b = CreateWindowW(L"BUTTON", L"OK",
                               WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                               350, 500, 80, 28, h, (HMENU) IDOK, g.hinst,
                               NULL);
        ui_set_font(b);
        b = CreateWindowW(L"BUTTON", L"Cancel",
                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          442, 500, 80, 28, h, (HMENU) IDCANCEL, g.hinst,
                          NULL);
        ui_set_font(b);
      }
      return 0;

    case WM_COMMAND:
      if (LOWORD(wp) == IDOK)
        {
          char values[SK_COUNT][SHORTCUT_TEXT_MAX];
          char error[512] = "";
          if (!shortcuts_collect(h, values, error, sizeof(error)))
            {
              wchar_t *we = w_utf8(error);
              SetDlgItemTextW(h, ID_SC_ERR, we ? we : L"Invalid shortcut.");
              free(we);
              return 0;
            }
          if (!app_hotkeys_apply(values, error, sizeof(error)))
            {
              wchar_t *we = w_utf8(error);
              SetDlgItemTextW(h, ID_SC_ERR,
                              we ? we : L"The shortcut could not be registered.");
              free(we);
              return 0;
            }
          DestroyWindow(h);
        }
      else if (LOWORD(wp) == IDCANCEL)
        {
          app_hotkeys_refresh();
          DestroyWindow(h);
        }
      return 0;

    case WM_CLOSE:
      app_hotkeys_refresh();
      DestroyWindow(h);
      return 0;

    case WM_CTLCOLORSTATIC:
      if (GetDlgCtrlID((HWND) lp) == ID_SC_ERR)
        SetTextColor((HDC) wp, RGB(180, 0, 0));
      SetBkColor((HDC) wp, GetSysColor(COLOR_WINDOW));
      return (LRESULT) GetSysColorBrush(COLOR_WINDOW);
    }
  return DefWindowProcW(h, msg, wp, lp);
}

void ui_show_shortcuts(void)
{
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.hInstance = g.hinst;
  wc.lpfnWndProc = shortcuts_proc;
  wc.lpszClassName = SHORTCUT_CLASS;
  wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  RegisterClassExW(&wc);

  HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, SHORTCUT_CLASS,
                           L"Shortcuts", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                           CW_USEDEFAULT, CW_USEDEFAULT, 550, 580,
                           g.overlay, NULL, g.hinst, NULL);
  center_window(h, 550, 580);
  run_modal(h, 0);
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

typedef struct { unsigned char r, g, b; } HtmlColor;
typedef struct { char tag[16]; HtmlColor color; } CssRule;
typedef struct { CssRule rules[24]; int count; } CssSheet;
typedef struct { HtmlColor color[32]; int count; } HtmlPalette;
typedef struct
{
  int bold, italic, underline, mono, size, color;
} RtfState;
typedef struct { char name[16]; RtfState prev; } HtmlFrame;

static int ci_equal(const char *a, const char *b)
{
  while (*a && *b)
    {
      char ca = (*a >= 'A' && *a <= 'Z') ? (char) (*a + 32) : *a;
      char cb = (*b >= 'A' && *b <= 'Z') ? (char) (*b + 32) : *b;
      if (ca != cb) return 0;
      a++; b++;
    }
  return *a == 0 && *b == 0;
}

static const char *ci_find(const char *s, const char *end, const char *needle)
{
  size_t n = strlen(needle);
  if (!n) return s;
  for (; s + n <= end; s++)
    {
      size_t i = 0;
      while (i < n)
        {
          char ca = (s[i] >= 'A' && s[i] <= 'Z') ? (char) (s[i] + 32) : s[i];
          char cb = (needle[i] >= 'A' && needle[i] <= 'Z')
                      ? (char) (needle[i] + 32) : needle[i];
          if (ca != cb) break;
          i++;
        }
      if (i == n) return s;
    }
  return NULL;
}

static void trim_range(const char **a, const char **b)
{
  while (*a < *b && (**a == ' ' || **a == '\t' ||
                     **a == '\r' || **a == '\n'))
    (*a)++;
  while (*b > *a && ((*b)[-1] == ' ' || (*b)[-1] == '\t' ||
                     (*b)[-1] == '\r' || (*b)[-1] == '\n'))
    (*b)--;
}

static int parse_hex_color(const char *s, const char *end, HtmlColor *c)
{
  while (s < end && (*s == ' ' || *s == '\t' || *s == '"' || *s == '\''))
    s++;
  if (s >= end || *s != '#') return 0;
  s++;
  int n = (end - s >= 6) ? 6 : (end - s >= 3 ? 3 : 0);
  if (!n) return 0;
  unsigned v = 0;
  for (int i = 0; i < n; i++)
    {
      char ch = s[i];
      int d;
      if (ch >= '0' && ch <= '9') d = ch - '0';
      else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
      else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
      else return 0;
      v = (v << 4) | (unsigned) d;
    }
  if (n == 3)
    {
      unsigned r = (v >> 8) & 0xf, g = (v >> 4) & 0xf, b = v & 0xf;
      c->r = (unsigned char) (r * 17);
      c->g = (unsigned char) (g * 17);
      c->b = (unsigned char) (b * 17);
    }
  else
    {
      c->r = (unsigned char) ((v >> 16) & 0xff);
      c->g = (unsigned char) ((v >> 8) & 0xff);
      c->b = (unsigned char) (v & 0xff);
    }
  return 1;
}

static void css_add(CssSheet *css, const char *tag, const char *a,
                    const char *b, HtmlColor color)
{
  if (css->count >= 24) return;
  trim_range(&a, &b);
  if (a >= b) return;
  size_t n = (size_t) (b - a);
  if (n >= sizeof(css->rules[0].tag)) n = sizeof(css->rules[0].tag) - 1;
  char *dst = css->rules[css->count].tag;
  for (size_t i = 0; i < n; i++)
    dst[i] = (a[i] >= 'A' && a[i] <= 'Z') ? (char) (a[i] + 32) : a[i];
  dst[n] = 0;
  css->rules[css->count].color = color;
  css->count++;
}

static void extract_css(const char *html, CssSheet *css)
{
  const char *end = html + strlen(html);
  const char *p = html;
  while ((p = ci_find(p, end, "<style")) != NULL)
    {
      p = memchr(p, '>', (size_t) (end - p));
      if (!p) break;
      p++;
      const char *e = ci_find(p, end, "</style>");
      if (!e) e = end;
      const char *q = p;
      while (q < e)
        {
          const char *brace = memchr(q, '{', (size_t) (e - q));
          if (!brace) break;
          const char *close = memchr(brace, '}', (size_t) (e - brace));
          if (!close) break;
          const char *col = ci_find(brace, close, "color");
          if (col)
            {
              const char *colon = memchr(col, ':', (size_t) (close - col));
              HtmlColor c;
              if (colon && parse_hex_color(colon + 1, close, &c))
                {
                  /* Support comma-separated simple tag selectors. */
                  const char *sel = q;
                  while (sel < brace)
                    {
                      const char *comma = memchr(sel, ',',
                                                 (size_t) (brace - sel));
                      const char *send = comma ? comma : brace;
                      css_add(css, "", sel, send, c);
                      if (!comma) break;
                      sel = comma + 1;
                    }
                }
            }
          q = close + 1;
        }
      p = e + 8;
    }
}

static int css_color_for(const CssSheet *css, const char *tag, HtmlColor *c)
{
  for (int i = 0; i < css->count; i++)
    if (ci_equal(css->rules[i].tag, tag))
      {
        *c = css->rules[i].color;
        return 1;
      }
  return 0;
}

static int attr_color(const char *attrs, const char *end, HtmlColor *c)
{
  const char *style = ci_find(attrs, end, "style");
  if (style)
    {
      const char *q = memchr(style, '=', (size_t) (end - style));
      if (q)
        {
          const char *col = ci_find(q, end, "color");
          if (col)
            {
              const char *colon = memchr(col, ':', (size_t) (end - col));
              if (colon && parse_hex_color(colon + 1, end, c)) return 1;
            }
        }
    }
  const char *col = ci_find(attrs, end, "color");
  if (col)
    {
      const char *eq = memchr(col, '=', (size_t) (end - col));
      if (eq && parse_hex_color(eq + 1, end, c)) return 1;
    }
  return 0;
}

static int palette_add(HtmlPalette *p, HtmlColor c)
{
  for (int i = 0; i < p->count; i++)
    if (p->color[i].r == c.r && p->color[i].g == c.g &&
        p->color[i].b == c.b)
      return i + 1;
  if (p->count >= 32) return 1;
  p->color[p->count] = c;
  p->count++;
  return p->count;
}

static void emit_state(SB *b, RtfState *cur, RtfState next)
{
  if (next.bold != cur->bold)
    sb_add(b, next.bold ? "\\b " : "\\b0 ");
  if (next.italic != cur->italic)
    sb_add(b, next.italic ? "\\i " : "\\i0 ");
  if (next.underline != cur->underline)
    sb_add(b, next.underline ? "\\ul " : "\\ul0 ");
  if (next.mono != cur->mono)
    sb_add(b, next.mono ? "\\f1 " : "\\f0 ");
  if (next.size != cur->size)
    sb_addf(b, "\\fs%d ", next.size);
  if (next.color != cur->color)
    sb_addf(b, "\\cf%d ", next.color);
  *cur = next;
}

static void rtf_char(SB *b, unsigned cp)
{
  if (cp == '\\' || cp == '{' || cp == '}')
    {
      char s[3] = { '\\', (char) cp, 0 };
      sb_add(b, s);
    }
  else if (cp >= 32 && cp < 127)
    {
      char s[2] = { (char) cp, 0 };
      sb_add(b, s);
    }
  else if (cp == '\n' || cp == '\r' || cp == '\t')
    sb_add(b, " ");
  else
    {
      long v = (long) cp;
      if (v > 32767) v -= 65536;
      sb_addf(b, "\\u%ld?", v);
    }
}

static unsigned utf8_cp(const unsigned char **pp, const unsigned char *end)
{
  const unsigned char *s = *pp;
  unsigned c = *s++;
  if (c < 0x80) { *pp = s; return c; }
  if ((c & 0xe0) == 0xc0 && s < end)
    {
      unsigned cp = ((c & 0x1f) << 6) | (*s++ & 0x3f);
      *pp = s; return cp;
    }
  if ((c & 0xf0) == 0xe0 && s + 1 < end)
    {
      unsigned cp = ((c & 0x0f) << 12) | ((s[0] & 0x3f) << 6) |
                    (s[1] & 0x3f);
      *pp = s + 2; return cp;
    }
  if ((c & 0xf8) == 0xf0 && s + 2 < end)
    {
      unsigned cp = ((c & 0x07) << 18) | ((s[0] & 0x3f) << 12) |
                    ((s[1] & 0x3f) << 6) | (s[2] & 0x3f);
      *pp = s + 3; return cp;
    }
  *pp = s;
  return '?';
}

static void rtf_text(SB *b, const char *s, const char *end, int *last_space)
{
  const unsigned char *p = (const unsigned char *) s;
  const unsigned char *e = (const unsigned char *) end;
  while (p < e)
    {
      if (*p == '&')
        {
          const char *sc = (const char *) p;
          const char *semi = memchr(sc, ';', (size_t) (e - p));
          if (semi && semi - sc < 12)
            {
              if (!strncmp(sc + 1, "amp", 3) && semi == sc + 4)
                { rtf_char(b, '&'); p = (const unsigned char *) semi + 1; *last_space = 0; continue; }
              if (!strncmp(sc + 1, "lt", 2) && semi == sc + 3)
                { rtf_char(b, '<'); p = (const unsigned char *) semi + 1; *last_space = 0; continue; }
              if (!strncmp(sc + 1, "gt", 2) && semi == sc + 3)
                { rtf_char(b, '>'); p = (const unsigned char *) semi + 1; *last_space = 0; continue; }
              if (!strncmp(sc + 1, "quot", 4) && semi == sc + 5)
                { rtf_char(b, '"'); p = (const unsigned char *) semi + 1; *last_space = 0; continue; }
              if (!strncmp(sc + 1, "nbsp", 4) && semi == sc + 5)
                { rtf_char(b, ' '); p = (const unsigned char *) semi + 1; *last_space = 1; continue; }
              if (sc[1] == '#')
                {
                  unsigned v = 0;
                  const char *d = sc + 2;
                  int hex = (*d == 'x' || *d == 'X');
                  if (hex) d++;
                  while (d < semi)
                    {
                      int n = -1;
                      if (*d >= '0' && *d <= '9') n = *d - '0';
                      else if (hex && *d >= 'a' && *d <= 'f') n = *d - 'a' + 10;
                      else if (hex && *d >= 'A' && *d <= 'F') n = *d - 'A' + 10;
                      if (n < 0) break;
                      v = v * (hex ? 16u : 10u) + (unsigned) n;
                      d++;
                    }
                  if (d == semi)
                    {
                      rtf_char(b, v);
                      p = (const unsigned char *) semi + 1;
                      *last_space = 0;
                      continue;
                    }
                }
            }
        }
      unsigned cp = utf8_cp(&p, e);
      if (cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t')
        {
          if (!*last_space) { rtf_char(b, ' '); *last_space = 1; }
        }
      else
        {
          rtf_char(b, cp);
          *last_space = 0;
        }
    }
}

static char *read_text_file(const wchar_t *path)
{
  HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return NULL;
  LARGE_INTEGER size;
  if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 ||
      size.QuadPart > 4 * 1024 * 1024)
    {
      CloseHandle(h);
      return NULL;
    }
  char *buf = (char *) malloc((size_t) size.QuadPart + 1);
  if (!buf) { CloseHandle(h); return NULL; }
  DWORD got = 0;
  BOOL ok = ReadFile(h, buf, (DWORD) size.QuadPart, &got, NULL);
  CloseHandle(h);
  if (!ok) { free(buf); return NULL; }
  buf[got] = 0;
  if ((unsigned char) buf[0] == 0xef && (unsigned char) buf[1] == 0xbb &&
      (unsigned char) buf[2] == 0xbf)
    memmove(buf, buf + 3, got - 2);
  return buf;
}

/* Hints are read from the installed asset.  They are not copied into the
 * settings folder, so editing the source hints.html is all that is needed. */
static int hints_file(wchar_t *path, int cap)
{
  wchar_t exe[MAX_PATH];
  app_exe_dir(exe, MAX_PATH);
  _snwprintf(path, (size_t) cap,
             L"%s\\Screenator Data\\assets\\hints.html", exe);
  return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static char *html_to_rtf(const char *html)
{
  CssSheet css;
  memset(&css, 0, sizeof(css));
  extract_css(html, &css);

  HtmlPalette palette;
  memset(&palette, 0, sizeof(palette));
  SB body;
  sb_init(&body);
  sb_add(&body, "\\ulnone ");
  RtfState state = { 0, 0, 0, 0, 18, 0 };
  HtmlFrame stack[64];
  int depth = 0;
  int last_space = 0;
  int list_depth = 0;
  int after_list = 0;
  const char *p = html;
  const char *end = html + strlen(html);

  while (p < end)
    {
      if (*p != '<')
        {
          const char *next = memchr(p, '<', (size_t) (end - p));
          if (!next) next = end;
          rtf_text(&body, p, next, &last_space);
          after_list = 0;
          p = next;
          continue;
        }

      if (!strncmp(p, "<!--", 4))
        {
          const char *e = ci_find(p + 4, end, "-->");
          p = e ? e + 3 : end;
          continue;
        }
      if (p + 1 < end && (p[1] == '!' || p[1] == '?'))
        {
          const char *e = memchr(p, '>', (size_t) (end - p));
          p = e ? e + 1 : end;
          continue;
        }

      const char *gt = memchr(p, '>', (size_t) (end - p));
      if (!gt) break;
      const char *inside = p + 1;
      int closing = 0;
      if (inside < gt && *inside == '/') { closing = 1; inside++; }
      const char *name_start = inside;
      while (inside < gt &&
             ((*inside >= 'a' && *inside <= 'z') ||
              (*inside >= 'A' && *inside <= 'Z') ||
              (*inside >= '0' && *inside <= '9')))
        inside++;
      char name[16];
      size_t nl = (size_t) (inside - name_start);
      if (nl >= sizeof(name)) nl = sizeof(name) - 1;
      for (size_t i = 0; i < nl; i++)
        name[i] = (name_start[i] >= 'A' && name_start[i] <= 'Z')
                    ? (char) (name_start[i] + 32) : name_start[i];
      name[nl] = 0;
      const char *attrs = inside;
      const char *tag_end = gt;
      p = gt + 1;

      if (!strcmp(name, "head") || !strcmp(name, "style") ||
          !strcmp(name, "title") || !strcmp(name, "script") ||
          !strcmp(name, "meta") || !strcmp(name, "link"))
        {
          if (!closing)
            {
              char closebuf[24];
              _snprintf(closebuf, sizeof(closebuf), "</%s>", name);
              const char *e = ci_find(p, end, closebuf);
              if (e) p = e + strlen(closebuf);
            }
          continue;
        }
      if (!closing && !strcmp(name, "br"))
        {
          sb_add(&body, "\\line ");
          last_space = 1;
          continue;
        }
      if (!strcmp(name, "ul") || !strcmp(name, "ol"))
        {
          if (closing)
            {
              if (list_depth > 0) list_depth--;
              after_list = 1;
            }
          else
            {
              /* A nested list starts a new paragraph below the parent item;
               * its items are indented further in. */
              if (list_depth > 0)
                {
                  sb_add(&body, "\\par ");
                  after_list = 0;
                }
              if (list_depth < 16) list_depth++;
            }
          /* Suppress the whitespace between <ul> and the first <li>, which
           * otherwise shifts only the first bullet to the right. */
          last_space = 1;
          continue;
        }

      if (closing)
        {
          int found = -1;
          for (int i = depth - 1; i >= 0; i--)
            if (!strcmp(stack[i].name, name)) { found = i; break; }
          if (found < 0) continue;
          while (depth > found + 1)
            {
              HtmlFrame *above = &stack[depth - 1];
              emit_state(&body, &state, above->prev);
              state = above->prev;
              depth--;
            }
          HtmlFrame *match = &stack[found];
          emit_state(&body, &state, match->prev);
          state = match->prev;
          if (!strcmp(name, "h1") || !strcmp(name, "h2") ||
              !strcmp(name, "h3") || !strcmp(name, "p") ||
              !strcmp(name, "li"))
            {
              /* A list that just ended already closed its last paragraph. */
              if (!(after_list && !strcmp(name, "li")))
                sb_add(&body, "\\par \\ulnone ");
              after_list = 0;
              last_space = 1;
            }
          depth = found;
          continue;
        }

      int is_block_heading = !strcmp(name, "h1") || !strcmp(name, "h2") ||
                             !strcmp(name, "h3");
      int is_known = is_block_heading || !strcmp(name, "p") ||
                     !strcmp(name, "ul") || !strcmp(name, "ol") ||
                     !strcmp(name, "li") || !strcmp(name, "b") ||
                     !strcmp(name, "strong") || !strcmp(name, "i") ||
                     !strcmp(name, "em") || !strcmp(name, "u") ||
                     !strcmp(name, "tt") || !strcmp(name, "code") ||
                     !strcmp(name, "a") || !strcmp(name, "span") ||
                     !strcmp(name, "font");
      if (!is_known) continue;
      if (depth >= 64) continue;

      HtmlFrame *f = &stack[depth];
      memset(f, 0, sizeof(*f));
      strncpy(f->name, name, sizeof(f->name) - 1);
      f->prev = state;
      depth++;

      RtfState next = state;
      if (is_block_heading)
        {
          int size = !strcmp(name, "h1") ? 24 : (!strcmp(name, "h2") ? 21 : 19);
          next.size = size;
          next.bold = 1;
          HtmlColor c;
          if (attr_color(attrs, tag_end, &c) ||
              css_color_for(&css, name, &c))
            next.color = palette_add(&palette, c);
          else if (!strcmp(name, "h2"))
            {
              HtmlColor def = { 0, 136, 0 };
              next.color = palette_add(&palette, def);
            }
          else if (!strcmp(name, "h3"))
            {
              HtmlColor def = { 42, 74, 128 };
              next.color = palette_add(&palette, def);
            }
          sb_add(&body, "\\par \\li0\\fi0 \\ulnone ");
          after_list = 0;
          last_space = 1;
        }
      else if (!strcmp(name, "b") || !strcmp(name, "strong"))
        next.bold = 1;
      else if (!strcmp(name, "i") || !strcmp(name, "em"))
        next.italic = 1;
      else if (!strcmp(name, "u"))
        next.underline = 1;
      else if (!strcmp(name, "tt") || !strcmp(name, "code"))
        next.mono = 1;
      else if (!strcmp(name, "li"))
        {
          int indent = 360 * (list_depth > 0 ? list_depth : 1);
          sb_addf(&body, "\\li%d\\fi-180 \\bullet  ", indent);
          after_list = 0;
          last_space = 0;
        }

      if (!is_block_heading)
        {
          HtmlColor c;
          if (attr_color(attrs, tag_end, &c) ||
              css_color_for(&css, name, &c))
            next.color = palette_add(&palette, c);
        }
      emit_state(&body, &state, next);
    }

  while (depth > 0)
    {
      HtmlFrame *f = &stack[depth - 1];
      emit_state(&body, &state, f->prev);
      state = f->prev;
      if (!strcmp(f->name, "h1") || !strcmp(f->name, "h2") ||
          !strcmp(f->name, "h3") || !strcmp(f->name, "p") ||
          !strcmp(f->name, "li"))
        sb_add(&body, "\\par \\ulnone ");
      depth--;
    }

  int visible = 0;
  for (size_t i = 0; body.text && i < body.len; i++)
    if (body.text[i] != ' ' && body.text[i] != '\\' &&
        body.text[i] != '{' && body.text[i] != '}' &&
        body.text[i] != 'p' && body.text[i] != 'a' && body.text[i] != 'r')
      { visible = 1; break; }
  if (!visible)
    {
      free(body.text);
      return NULL;
    }

  SB out;
  sb_init(&out);
  sb_add(&out, "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss Segoe UI;}"
               "{\\f1\\fmodern Consolas;}}\n{\\colortbl ;");
  for (int i = 0; i < palette.count; i++)
    sb_addf(&out, "\\red%d\\green%d\\blue%d;",
            palette.color[i].r, palette.color[i].g, palette.color[i].b);
  sb_add(&out, "}\n\\fs18\n");
  sb_add(&out, body.text ? body.text : "");
  sb_add(&out, "}\n");
  free(body.text);
  return out.text;
}

static char *hints_html_rtf(void)
{
  wchar_t path[MAX_PATH];
  if (!hints_file(path, MAX_PATH)) return NULL;
  char *html = read_text_file(path);
  if (!html) return NULL;
  char *rtf = html_to_rtf(html);
  free(html);
  return rtf;
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
      sb_addf(&b, "\\cf1\\bullet\\cf0  Left click and drag to draw with the "
                  "selected tool.\\par\n");
      sb_addf(&b, "\\cf1\\bullet\\cf0  While text or shape handles are being edited, middle click changes color and keeps editing active. After editing ends, it does nothing in Screen mode with the Text or a shape tool selected. Otherwise it erases while held, except with the Pen tool, or pans the page in Paper mode.\\par\n");
      sb_addf(&b, "\\cf1\\bullet\\cf0  In Screen mode, the mouse wheel changes color only while typing text or editing text/shape handles. Otherwise it does nothing, even with a tool selected. In Paper mode it scrolls outside editing.\\par\n");
      sb_addf(&b, "\\cf1\\bullet\\cf0  Right click first accepts text, then "
                  "finishes shape editing; the next right click erases while "
                  "held.\\par\n");

      sb_addf(&b, "\\cf2\\b\\fs22 Stylus\\b0\\fs18\\par\n");
      sb_addf(&b, "\\cf1\\b IMPORTANT:\\b0 In your pen driver, set "
                  "\\b button 1\\b0  (nearest the tip) to \\b Middle Click\\b0  "
                  "and \\b button 2\\b0  to \\b Right Click\\b0.\\par\n");
      sb_addf(&b, "\\cf2\\bullet\\cf0  Hold side button 1 with the "
                  "Eraser selected to erase, even while hovering.\\par\n");
      sb_addf(&b, "\\cf2\\bullet\\cf0  \\b Long press side button 2\\b0  "
                  "opens the Button 1 Mode menu (Color or Thickness); a "
                  "long right click opens Scroll Mode.\\par\n");
      sb_addf(&b, "\\cf2\\bullet\\cf0  Short press side button 1: step "
                  "through the selected Color or Thickness, or the eraser "
                  "size with the Eraser tool.\\par\n");
      sb_addf(&b, "\\cf2\\bullet\\cf0  In Paper mode, side button 1 "
                  "or middle click pans the page when no edit handles are "
                  "active.\\par\n");
      sb_addf(&b, "\\cf2\\bullet\\cf0  Side button 2 follows the right-click "
                  "sequence: accept text, finish shape editing, then erase "
                  "while held.\\par\n");

      sb_addf(&b, "\\cf3\\b\\fs22 Global Shortcut\\b0\\fs18\\par\n");
      sb_addf(&b, "\\cf3\\bullet\\cf0  \\b F9\\b0  shows or hides the floating "
                  "icon at any time.\\par\n");
      sb_addf(&b, "\\cf3\\bullet\\cf0  \\b F8\\b0  selects the Pen, "
                  "\\b F6\\b0  the Arrow and \\b F7\\b0  the Text tool.\\par\n");
      sb_addf(&b, "\\cf3\\bullet\\cf0  \\b F10\\b0  shows or hides the tool "
                  "bar; \\b Ctrl+F10\\b0  opens the quick tool menu.\\par\n");

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
      sb_addf(&b, "\\cf5\\bullet\\cf0  After typing, press \\b Enter\\b0, "
                  "right click or stylus button 2 to accept the "
                  "text and re-enable the mouse.\\par\n");

      sb_addf(&b, "\\cf6\\b\\fs22 Marker Tool\\b0\\fs18\\par\n");
      sb_addf(&b, "\\cf6\\bullet\\cf0  Stylus side button 1 "
                  "changes its color.\\par\n");
      sb_addf(&b, "\\cf6\\bullet\\cf0  \\b Long right click\\b0  or "
                  "\\b Long press side button 2\\b0  opens the Marker Opacity "
                  "menu (10%% to 90%% in steps of 10%%).\\par\n");

      sb_addf(&b, "\\cf7\\b\\fs22 Drawing Mode Shortcuts\\b0\\fs18\\par\n");
      sb_addf(&b, "\\cf7\\bullet\\cf0  F4 saves a screenshot to Pictures, hiding the floating icon and toolbar during capture and restoring them afterward.\\par\n");
      sb_addf(&b, "\\cf7\\bullet\\cf0  Timer finishes the current edit and selects Cursor while waiting, then activates Pen when the timer expires.\\par\n");
      sb_addf(&b, "\\cf7\\bullet\\cf0  \\b Ctrl+Z\\b0  undo, "
                  "\\b Ctrl+Shift+Z\\b0  redo.\\par\n");
      sb_addf(&b, "\\cf7\\bullet\\cf0  \\b Escape\\b0  finishes the shape or "
                  "exits the tool.\\par\n");

      sb_addf(&b, "\\cf4\\b\\fs22 Paper Mode\\b0\\fs18\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  Closing lists all paper types and the PDF with unsaved changes. Choose Yes to save all listed documents and their pages to Documents. The final message lists saved files; save failures keep Screenator open.\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  Each launch starts with fresh pages and no PDF open. Reopening a PDF returns to its saved page. Drawing undo/redo follows edits across all pages and switches to the affected page.\\par\n");
      sb_addf(&b, "\\cf4\\b\\i NOTE:\\i0\\b0  Dialogs stay open until you "
                  "close them; clicking outside a dialog no longer dismisses "
                  "it in paper mode.\\b0\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  \\b Ctrl+Z\\b0  undo, "
                  "\\b Ctrl+Shift+Z\\b0  redo.\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  Toggle Visibility and Ctrl+T are "
                  "available in Screen mode only.\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  \\b Ctrl+Q\\b0  exits the screen "
                  "paper.\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  \\b Ctrl+W\\b0  toggles full-screen "
                  "Paper and the resizable Paper window; \\b Ctrl+D\\b0  "
                  "toggles whether Paper covers the taskbar.\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  \\b Ctrl+S\\b0  saves Paper as a "
                  "PDF; \\b Ctrl+O\\b0  opens a PDF. Ctrl+B/ Ctrl+A insert "
                  "a page before/after the current page.\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  Page Up/Page Down move or scroll "
                  "through pages; Home/End go to the first/last page. These "
                  "shortcuts also work with the pointer over the toolbar or "
                  "floating icon.\\par\n");
      sb_addf(&b, "\\cf4\\bullet\\cf0  Clear Screen clears annotations "
                  "on the page occupying most of the visible Paper area, "
                  "including in the resizable Paper window.\\par\n");
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
        int cw = cr.right > 0 ? cr.right : 700;
        int ch = cr.bottom > 0 ? cr.bottom : 500;

        int edit_h = ch - 16 - 80;
        if (edit_h < 120) edit_h = 120;
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                    ES_MULTILINE | ES_READONLY,
                                    16, 16, cw - 32, edit_h, h, NULL,
                                    g.hinst, NULL);
        set_ui_font(edit);

        char *rtf = hints_html_rtf();
        if (!rtf) rtf = hints_rtf();
        if (rtf) { set_rich_text(edit, rtf); free(rtf); }
        /* Disable RichEdit proofing and remove any automatic link formatting. */
        SendMessageW(edit, EM_SETLANGOPTIONS, 0, 0);
        SendMessageW(edit, EM_AUTOURLDETECT, FALSE, 0);
        {
          CHARFORMAT2W cf;
          memset(&cf, 0, sizeof(cf));
          cf.cbSize = sizeof(cf);
          cf.dwMask = CFM_UNDERLINE | CFM_LINK;
          cf.dwEffects = 0;
          SendMessageW(edit, EM_SETSEL, 0, -1);
          SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM) &cf);
          SendMessageW(edit, EM_SETSEL, 0, 0);
        }

        HWND chk = CreateWindowW(L"BUTTON", L"Never show hints again",
                                 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                 20, ch - 40, 240, 24, h,
                                 (HMENU) (INT_PTR) ID_HINTS_CHECK, g.hinst,
                                 NULL);
        set_ui_font(chk);
        SendMessageW(chk, BM_SETCHECK,
                     g.hide_hints ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND about = CreateWindowW(L"BUTTON", L"About",
                                   WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   cw - 18 - 84 - 8 - 84, ch - 44, 84, 28, h,
                                   (HMENU) (INT_PTR) ID_HINTS_ABOUT, g.hinst,
                                   NULL);
        set_ui_font(about);

        HWND close = CreateWindowW(L"BUTTON", L"Close",
                                   WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                   cw - 18 - 84, ch - 44, 84, 28, h,
                                   (HMENU) IDOK, g.hinst, NULL);
        set_ui_font(close);
      }
      return 0;

    case WM_COMMAND:
      if (LOWORD(wp) == ID_HINTS_ABOUT)
        {
          ui_show_about();
          return 0;
        }
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

  int w = 700, hgt = 500;
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
  run_modal(g.hintsdlg, 0);
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
  run_modal(g.mathdlg, 0);
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

        HWND t = CreateWindowW(L"STATIC", L"Screenator 8.0",
                               WS_CHILD | WS_VISIBLE, 96, 20, 300, 28, h,
                               NULL, g.hinst, NULL);
        HFONT big = CreateFontW(-22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH, L"Segoe UI");
        if (big) SendMessageW(t, WM_SETFONT, (WPARAM) big, TRUE);
        set_ui_font(t);

        HWND txt = CreateWindowW(L"STATIC",
          L"A screen annotation tool for Windows, developed by\r\n"
          L"Mahmoud Alnaanah (malnaanah@gmail.com).\r\n\r\n"
          L"Based on Gromit-MPX 1.4.2\r\n"
          L"Copyright 2009-2022 Christian Beier\r\n"
          L"Copyright 2000 Simon Budig\r\n\r\n"
          L"Copyright 2026 Mahmoud Alnaanah\r\n\r\n"
          L"This program comes with absolutely no warranty.\r\n"
          L"See the GNU General Public License, version 2 or later for details.",
          WS_CHILD | WS_VISIBLE, 20, 92, 390, 148, h, NULL, g.hinst, NULL);
        set_ui_font(txt);

        HWND b = CreateWindowW(L"BUTTON", L"Close",
                               WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                               330, 276, 84, 28, h, (HMENU) IDOK, g.hinst,
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
  center_window(g.aboutdlg, 440, 330);
  run_modal(g.aboutdlg, 0);
  g.aboutdlg = NULL;
}

/* ------------------------------------------------------------------ */
/* quick tool menu                                                    */
/* ------------------------------------------------------------------ */

typedef struct { Tool tool; const wchar_t *label; const char *icon; } QuickTool;

#define QUICK_CURSOR (-2)

static const QuickTool quick_tools[] =
{
  { (Tool) QUICK_CURSOR, L"Cursor", "cursor.png" },
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

#define QUICK_COUNT 16
#define QUICK_COLS 4
#define QUICK_ROWS 4
#define QUICK_CELL_W 116
#define QUICK_CELL_H 108
#define QUICK_MARGIN 12

static HBITMAP quick_icons[QUICK_COUNT];
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
  for (int i = 0; i < QUICK_COUNT; i++)
    {
      RECT r = quick_cell_rect(i);
      if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
        return i;
    }
  return -1;
}

static void quick_activate(HWND h)
{
  if (g.quick_sel >= 0 && g.quick_sel < QUICK_COUNT)
    quick_result = quick_tools[g.quick_sel].tool;
  quick_done = 1;
  DestroyWindow(h);
}

static LRESULT CALLBACK quick_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
    {
    case WM_CREATE:
      for (int i = 0; i < QUICK_COUNT; i++)
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
        for (int i = 0; i < QUICK_COUNT; i++)
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
  ui_prepare_dialog();

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

  for (int i = 0; i < QUICK_COUNT; i++)
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

  if (quick_result == QUICK_CURSOR)
    { app_cancel_timer(); tools_exit(); }
  else if (quick_result == TOOL_MATH)
    {
      if (g.paper != PAPER_NONE) app_force_foreground(g.overlay);
      ui_show_math_dialog();
    }
  else if (quick_result != TOOL_NONE)
    {
      if (g.paper != PAPER_NONE) app_force_foreground(g.overlay);
      tools_activate((Tool) quick_result);
    }
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
      /* The opacity submenu must offer 10% through 100% in ten-percent
       * steps, and the icon submenu keeps all seven sizes. */
      int found_opacity = 0;
      for (int i = 0; i < GetMenuItemCount(m); i++)
        {
          wchar_t text[64];
          if (!GetMenuStringW(m, (UINT) i, text, 64, MF_BYPOSITION)) continue;
          if (!wcscmp(text, L"Opacity"))
            {
              HMENU sub = GetSubMenu(m, i);
              found_opacity = sub && GetMenuItemCount(sub) == 10;
            }
        }
      if (!found_opacity) ok = 0;
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
  register_dialog(SHORTCUT_CLASS, shortcuts_proc);
  register_dialog(SETTINGS_CLASS, settings_proc);
  register_dialog(GOTO_CLASS, goto_proc);

  h = CreateWindowExW(WS_EX_TOOLWINDOW, MATH_CLASS, L"Math Function",
                      WS_POPUP, 0, 0, 520, 360, g.overlay, NULL, g.hinst,
                      NULL);
  if (!h || !GetDlgItem(h, ID_MATH_EXPR) || !GetDlgItem(h, ID_MATH_PRESET))
    ok = 0;
  if (h) DestroyWindow(h);

  h = CreateWindowExW(WS_EX_TOOLWINDOW, SETTINGS_CLASS,
                      L"Screenator Settings", WS_POPUP, 0, 0, 480, 410,
                      g.overlay, NULL, g.hinst, NULL);
  if (!h || !GetDlgItem(h, ID_SET_TAB) ||
      !GetDlgItem(h, ID_SET_ICON_SIZE) ||
      !GetDlgItem(h, ID_SET_CONTINUOUS) ||
      !GetDlgItem(h, ID_SET_SAVE)) ok = 0;
  if (h) DestroyWindow(h);

  h = CreateWindowExW(WS_EX_TOOLWINDOW, HINTS_CLASS, L"Screenator Hints",
                      WS_POPUP, 0, 0, 700, 500, g.overlay, NULL, g.hinst,
                      NULL);
  if (!h || !FindWindowExW(h, NULL, MSFTEDIT_CLASS, NULL) ||
      !GetDlgItem(h, ID_HINTS_ABOUT)) ok = 0;
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

  h = CreateWindowExW(WS_EX_TOOLWINDOW, SHORTCUT_CLASS, L"Shortcuts",
                      WS_POPUP, 0, 0, 550, 580, g.overlay, NULL, g.hinst,
                      NULL);
  if (!h || !GetDlgItem(h, ID_SC_EDIT) || !GetDlgItem(h, ID_SC_ERR))
    ok = 0;
  if (h) DestroyWindow(h);

  return ok;
}
