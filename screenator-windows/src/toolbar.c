/*
 * Screenator for Windows -- persistent tool bar.
 * GPL-2.0-or-later.  See app.h.
 */
#include "app.h"
#include <cairo/cairo.h>
#include <stdio.h>
#include <string.h>

#define TB_CLASS L"ScreenatorToolbar"
#define TB_CFG_CLASS L"ScreenatorToolbarConfig"
#define TB_PAD 0

#define ID_TB_LIST       7101
#define ID_TB_SHOW       7102
#define ID_TB_UP         7103
#define ID_TB_DOWN       7104

typedef struct
{
  const char *id;              /* config token */
  const char *label;           /* tooltip and Configure dialog */
  const char *icon;            /* asset file */
  int cmd;                     /* command, or 0 for a popup menu */
  int popup;                   /* ToolbarPopup when cmd is 0 */
} TbAction;

static const TbAction tb_actions[] =
{
  { "switch_mode", "Switch Mode", "switch_mode.png", CMD_SWITCH_MODE, 0 },
  { "cursor", "Cursor", "cursor.png", CMD_CURSOR, 0 },
  { "pen", "Pen", "pen.png", CMD_PEN, 0 },
  { "text", "Text", "text.png", CMD_TEXT, 0 },
  { "eraser", "Eraser", "eraser.png", CMD_ERASER, 0 },
  { "arrow", "Arrow", "arrow.png", CMD_ARROW, 0 },
  { "timer", "Timer", "timer.png", CMD_TIMER, 0 },
  { "shapes", "Shapes", "shapes.png", 0, TB_POPUP_SHAPES },
  { "undo", "Undo", "undo.png", CMD_UNDO, 0 },
  { "redo", "Redo", "redo.png", CMD_REDO, 0 },
  { "save", "Save / Screenshot", "save.png", CMD_SAVE_OR_SHOT, 0 },
  { "paper", "Paper Type", "paper.png", 0, TB_POPUP_PAPER_TYPE },
  { "page_insert_before", "Insert Page Before", "add_before.png",
    CMD_PAPER_INSERT_BEFORE, 0 },
  { "page_delete", "Delete Page", "delete-page.png", CMD_PAPER_DELETE, 0 },
  { "page_insert_after", "Insert Page After", "add_after.png",
    CMD_PAPER_INSERT_AFTER, 0 },
  { "page_number", "Current Page (Press to change page)", "paper.png",
    CMD_PAPER_GOTO, 0 },
  { "goto_page", "Go to Page", "goto-page.png", CMD_PAPER_GOTO, 0 },
  { "clear", "Clear Screen", "trash.png", CMD_CLEAR, 0 },
  { "visibility", "Toggle Visibility", "hide.png", CMD_VISIBILITY, 0 },
  { "eraser_size", "Eraser Size", "eraser.png", 0, TB_POPUP_ERASER_SIZE },
  { "line_color", "Line Color", "pen.png", 0, TB_POPUP_LINE_COLOR },
  { "thickness", "Thickness", "free-line.png", 0, TB_POPUP_THICKNESS },
  { "fill_color", "Fill Color", "bucket.png", 0, TB_POPUP_FILL_COLOR },
  { "opacity", "Opacity", "opacity.png", 0, TB_POPUP_OPACITY },
  { "marker_opacity", "Marker Opacity", "marker_opacity.png", 0,
    TB_POPUP_MARKER_OPACITY },
  { "settings", "Settings", "settings.png", CMD_SETTINGS, 0 },
  { "hints", "Show Hints", "hint.png", CMD_SHOW_HINTS, 0 },
  { "quit", "Quit", "quit.png", CMD_QUIT, 0 }
};
#define TB_ACTION_COUNT ((int) (sizeof(tb_actions) / sizeof(tb_actions[0])))
/* The cache holds each icon at more than one size: the toolbar uses
 * g.toolbar_icon_size while the Configure dialog always draws 24 px icons. */
#define TB_ICON_CACHE (TB_ACTION_COUNT * 2 + 8)

static int tb_items[TB_ACTION_COUNT];
static int tb_count;
static int tb_vis[TB_ACTION_COUNT];
static int tb_vis_count;

typedef struct
{
  char name[64];
  int size;
  HBITMAP bmp;
} TbIcon;

static HWND tb_wnd;
static HWND tb_tip;
static wchar_t tb_tip_text[TB_ACTION_COUNT][96];
static int tb_hover = -1;
static int tb_pressed = -1;
static int tb_drag;
static int tb_pending;
static POINT tb_press_origin;
static POINT tb_drag_origin;
static POINT tb_drag_pos;
static TbIcon tb_icons[TB_ICON_CACHE];
static int tb_nicons;
static int tb_size = 32;

int toolbar_pointer_hit(POINT p)
{
  RECT r;
  if (!IsWindow(tb_wnd) || !IsWindowVisible(tb_wnd) ||
      !GetWindowRect(tb_wnd, &r))
    return 0;
  return PtInRect(&r, p);
}

/* ------------------------------------------------------------------ */
/* item list                                                          */
/* ------------------------------------------------------------------ */

static char *tb_trim(char *s)
{
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
  char *end = s + strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                     end[-1] == '\r' || end[-1] == '\n'))
    *--end = 0;
  return s;
}

static int tb_ascii_iequal(const char *a, const char *b)
{
  while (*a && *b)
    {
      int ca = (unsigned char) *a;
      int cb = (unsigned char) *b;
      if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
      if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
      if (ca != cb) return 0;
      a++;
      b++;
    }
  return *a == 0 && *b == 0;
}

static const char *tb_canonical_id(const char *id)
{
  if (tb_ascii_iequal(id, "hand")) return "cursor";
  if (tb_ascii_iequal(id, "screenshot") ||
      tb_ascii_iequal(id, "screenshot_save") ||
      tb_ascii_iequal(id, "save_paper")) return "save";
  if (tb_ascii_iequal(id, "page_prev") || tb_ascii_iequal(id, "page_next") ||
      tb_ascii_iequal(id, "floating_icon") ||
      tb_ascii_iequal(id, "show_icon")) return "";
  return id;
}

static int tb_action_index(const char *id)
{
  for (int i = 0; i < TB_ACTION_COUNT; i++)
    if (tb_ascii_iequal(id, tb_actions[i].id)) return i;
  return -1;
}

static int tb_list_is_none(const char *text)
{
  char buf[32];
  if (!text) return 0;
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  return tb_ascii_iequal(tb_trim(buf), "none");
}

static void tb_default_order(int *order, int *count)
{
  *count = TB_ACTION_COUNT;
  for (int i = 0; i < TB_ACTION_COUNT; i++) order[i] = i;
}

static void tb_parse_order(const char *text, int *order, int *count)
{
  int seen[TB_ACTION_COUNT];
  char buf[TOOLBAR_ITEMS_MAX];

  memset(seen, 0, sizeof(seen));
  *count = 0;
  if (!text || !text[0])
    {
      tb_default_order(order, count);
      return;
    }

  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  char *p = buf;
  while (p && *p)
    {
      char *comma = strchr(p, ',');
      if (comma) *comma = 0;
      char *id = tb_trim(p);
      int idx = tb_action_index(tb_canonical_id(id));
      if (idx >= 0 && !seen[idx] && *count < TB_ACTION_COUNT)
        {
          seen[idx] = 1;
          order[(*count)++] = idx;
        }
      p = comma ? comma + 1 : NULL;
    }

  /* Keep the setting list complete; new actions are appended so they stay
   * available without disturbing an existing order. */
  for (int i = 0; i < TB_ACTION_COUNT; i++)
    if (!seen[i] && *count < TB_ACTION_COUNT)
      order[(*count)++] = i;
}

static void tb_parse_visible(const char *text, int *visible)
{
  char buf[TOOLBAR_ITEMS_MAX];

  memset(visible, 0, sizeof(int) * (size_t) TB_ACTION_COUNT);
  if (!text || !text[0])
    {
      for (int i = 0; i < TB_ACTION_COUNT; i++) visible[i] = 1;
      return;
    }
  if (tb_list_is_none(text)) return;

  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  char *p = buf;
  while (p && *p)
    {
      char *comma = strchr(p, ',');
      if (comma) *comma = 0;
      char *id = tb_trim(p);
      int idx = tb_action_index(tb_canonical_id(id));
      if (idx >= 0) visible[idx] = 1;
      p = comma ? comma + 1 : NULL;
    }
}

static int tb_load_items(void)
{
  int old[TB_ACTION_COUNT];
  int old_count = tb_count;
  int seen[TB_ACTION_COUNT];
  char buf[TOOLBAR_ITEMS_MAX];

  memcpy(old, tb_items, sizeof(tb_items));
  memset(seen, 0, sizeof(seen));
  tb_count = 0;

  if (!g.toolbar_items[0])
    {
      for (int i = 0; i < TB_ACTION_COUNT; i++) tb_items[tb_count++] = i;
    }
  else if (tb_list_is_none(g.toolbar_items))
    {
      /* no items */
    }
  else
    {
      strncpy(buf, g.toolbar_items, sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = 0;
      char *p = buf;
      while (p && *p && tb_count < TB_ACTION_COUNT)
        {
          char *comma = strchr(p, ',');
          if (comma) *comma = 0;
          char *id = tb_trim(p);
          int idx = tb_action_index(tb_canonical_id(id));
          if (idx >= 0 && !seen[idx])
            {
              seen[idx] = 1;
              tb_items[tb_count++] = idx;
            }
          p = comma ? comma + 1 : NULL;
        }
      /* An unknown non-empty list falls back to the defaults. */
      if (tb_count == 0)
        for (int i = 0; i < TB_ACTION_COUNT; i++) tb_items[tb_count++] = i;
    }

  if (tb_count != old_count) return 1;
  for (int i = 0; i < tb_count; i++)
    if (tb_items[i] != old[i]) return 1;
  return 0;
}

static int tb_action_visible(int idx)
{
  const char *id = tb_actions[idx].id;
  int paper = g.paper != PAPER_NONE;
  if (!strcmp(id, "page_insert_before") ||
      !strcmp(id, "page_insert_after") || !strcmp(id, "page_delete") ||
      !strcmp(id, "page_number") || !strcmp(id, "goto_page"))
    return paper;
  if (!strcmp(id, "timer") || !strcmp(id, "visibility"))
    return !paper;
  return 1;
}

static int tb_action_enabled(int idx)
{
  const char *id = tb_actions[idx].id;
  int paper = g.paper != PAPER_NONE;
  if (!strcmp(id, "page_insert_before") ||
      !strcmp(id, "page_insert_after"))
    return paper && g.paper != PAPER_PDF;
  if (!strcmp(id, "page_delete"))
    return paper && g.paper != PAPER_PDF && paper_page_count() > 1;
  if (!strcmp(id, "goto_page"))
    return paper && paper_page_count() > 1;
  if (!strcmp(id, "page_number"))
    return paper;
  return 1;
}

static int tb_rebuild_visible(void)
{
  int old = tb_vis_count;
  tb_vis_count = 0;
  for (int i = 0; i < tb_count; i++)
    if (tb_action_visible(tb_items[i]))
      tb_vis[tb_vis_count++] = tb_items[i];
  return old != tb_vis_count;
}

/* ------------------------------------------------------------------ */
/* layout                                                             */
/* ------------------------------------------------------------------ */

static RECT tb_button_rect(int i)
{
  int cell = tb_size + 2 * TB_PAD;
  RECT r;
  if (g.toolbar_vertical)
    {
      r.left = 0;
      r.top = i * cell;
      r.right = r.left + cell;
      r.bottom = r.top + cell;
    }
  else
    {
      r.left = i * cell;
      r.top = 0;
      r.right = r.left + cell;
      r.bottom = r.top + cell;
    }
  return r;
}

static void tb_window_size(int *w, int *h)
{
  int cell = tb_size + 2 * TB_PAD;
  int n = tb_vis_count > 0 ? tb_vis_count : 1;
  if (g.toolbar_vertical)
    {
      *w = cell;
      *h = n * cell;
    }
  else
    {
      *w = n * cell;
      *h = cell;
    }
}

static void tb_clear_icons(void)
{
  for (int i = 0; i < tb_nicons; i++)
    if (tb_icons[i].bmp) DeleteObject(tb_icons[i].bmp);
  tb_nicons = 0;
}

static HBITMAP tb_icon(const char *name, int size)
{
  for (int i = 0; i < tb_nicons; i++)
    if (tb_icons[i].size == size && !strcmp(tb_icons[i].name, name))
      return tb_icons[i].bmp;

  wchar_t wname[128];
  MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 128);
  wchar_t rel[192];
  _snwprintf(rel, 192, L"icons\\%s", wname);
  wchar_t path[MAX_PATH];
  app_asset_path(path, MAX_PATH, rel);
  char utf8[MAX_PATH * 3];
  WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);

  cairo_surface_t *img = cairo_image_surface_create_from_png(utf8);
  if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy(img);
      return NULL;
    }

  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = size;
  bi.bmiHeader.biHeight = -size;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;

  HDC screen = GetDC(NULL);
  void *bits = NULL;
  HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits,
                                 NULL, 0);
  ReleaseDC(NULL, screen);
  if (!bmp || !bits)
    {
      cairo_surface_destroy(img);
      return NULL;
    }

  cairo_surface_t *dst = cairo_image_surface_create_for_data(
    (unsigned char *) bits, CAIRO_FORMAT_ARGB32, size, size, size * 4);
  cairo_t *cr = cairo_create(dst);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  int iw = cairo_image_surface_get_width(img);
  int ih = cairo_image_surface_get_height(img);
  if (iw > 0 && ih > 0)
    {
      double s = (double) size / (double) (iw > ih ? iw : ih);
      double ox = (size - iw * s) / 2.0;
      double oy = (size - ih * s) / 2.0;
      cairo_save(cr);
      cairo_translate(cr, ox, oy);
      cairo_scale(cr, s, s);
      cairo_set_source_surface(cr, img, 0, 0);
      cairo_paint(cr);
      cairo_restore(cr);
    }
  cairo_surface_flush(dst);
  cairo_destroy(cr);
  cairo_surface_destroy(dst);
  cairo_surface_destroy(img);

  if (tb_nicons < TB_ICON_CACHE)
    {
      strncpy(tb_icons[tb_nicons].name, name,
              sizeof(tb_icons[0].name) - 1);
      tb_icons[tb_nicons].name[sizeof(tb_icons[0].name) - 1] = 0;
      tb_icons[tb_nicons].size = size;
      tb_icons[tb_nicons].bmp = bmp;
      tb_nicons++;
    }
  else
    {
      DeleteObject(bmp);
      return NULL;
    }
  return bmp;
}

static int tb_active(int cmd)
{
  if (cmd == CMD_CURSOR) return g.tool == TOOL_NONE;
  if (cmd == CMD_PEN) return g.tool == TOOL_PEN;
  if (cmd == CMD_TEXT) return g.tool == TOOL_TEXT;
  if (cmd == CMD_ERASER) return g.tool == TOOL_ERASER;
  if (cmd == CMD_ARROW) return g.tool == TOOL_ARROW;
  if (cmd == CMD_TIMER) return g.timer_running;
  if (cmd == CMD_SWITCH_MODE) return g.paper != PAPER_NONE;
  if (cmd == CMD_VISIBILITY) return g.hide_drawings;
  if (cmd == CMD_SHOW_ICON) return g.show_icon;
  return 0;
}

static void tb_tooltips_destroy(void)
{
  if (tb_tip)
    {
      DestroyWindow(tb_tip);
      tb_tip = NULL;
    }
}

static int tb_shortcut_id(int cmd)
{
  switch (cmd)
    {
    case CMD_PEN: return SK_PEN;
    case CMD_TEXT: return SK_TEXT;
    case CMD_ARROW: return SK_ARROW;
    case CMD_UNDO: return SK_UNDO;
    case CMD_REDO: return SK_REDO;
    case CMD_SWITCH_MODE: return SK_SWITCH;
    case CMD_TIMER: return SK_TIMER;
    case CMD_VISIBILITY: return SK_PAPER_T;
    case CMD_QUIT: return SK_QUIT;
    case CMD_SAVE_OR_SHOT: return SK_SHOT;
    default: return -1;
    }
}

static void tb_tooltips_create(void)
{
  tb_tooltips_destroy();
  if (!IsWindow(tb_wnd)) return;

  tb_tip = CreateWindowExW(
    WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
    WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
    tb_wnd, NULL, g.hinst, NULL);
  if (!tb_tip) return;

  SendMessageW(tb_tip, TTM_SETMAXTIPWIDTH, 0, 280);
  SendMessageW(tb_tip, TTM_SETDELAYTIME, TTDT_INITIAL, 400);
  SendMessageW(tb_tip, TTM_SETDELAYTIME, TTDT_RESHOW, 80);

  for (int i = 0; i < tb_vis_count; i++)
    {
      const TbAction *a = &tb_actions[tb_vis[i]];
      TOOLINFOW ti;
      char tip[192];
      int sid = tb_shortcut_id(a->cmd);
      memset(&ti, 0, sizeof(ti));
      ti.cbSize = sizeof(ti);
      ti.uFlags = TTF_SUBCLASS;
      ti.hwnd = tb_wnd;
      ti.uId = (UINT_PTR) (i + 1);
      ti.rect = tb_button_rect(i);
      _snprintf(tip, sizeof(tip), "%s", a->label);
      tip[sizeof(tip) - 1] = 0;
      if (!strcmp(a->id, "page_number"))
        {
          /* The page tool shows the page and the total in its tooltip; it is
           * refreshed whenever the page count or the current page changes. */
          int count = paper_page_count();
          int index = paper_view_page_index();
          if (count > 0)
            _snprintf(tip, sizeof(tip), "Page %d/%d (Press to change page)",
                      index + 1, count);
          else
            _snprintf(tip, sizeof(tip), "Page -/- (Press to change page)");
          tip[sizeof(tip) - 1] = 0;
        }
      if (sid >= 0 && g.shortcuts[sid][0])
        {
          size_t used = strlen(tip);
          _snprintf(tip + used, sizeof(tip) - used, "\n%s",
                    g.shortcuts[sid]);
          tip[sizeof(tip) - 1] = 0;
        }
      MultiByteToWideChar(CP_UTF8, 0, tip, -1, tb_tip_text[i],
                          (int) (sizeof(tb_tip_text[i]) /
                                 sizeof(tb_tip_text[i][0])));
      ti.lpszText = tb_tip_text[i];
      SendMessageW(tb_tip, TTM_ADDTOOLW, 0, (LPARAM) &ti);
    }
}

/* Update the page tool's tooltip text (page number and total) without
 * recreating the tooltip window. */
static void tb_page_tooltip_update(void)
{
  wchar_t txt[96];
  int count, index;

  if (!IsWindow(tb_tip) || !IsWindow(tb_wnd)) return;
  for (int i = 0; i < tb_vis_count; i++)
    {
      const TbAction *a = &tb_actions[tb_vis[i]];
      TOOLINFOW ti;
      if (strcmp(a->id, "page_number")) continue;
      count = paper_page_count();
      index = paper_view_page_index();
      if (count > 0)
        _snwprintf(txt, 96, L"Page %d/%d (Press to change page)", index + 1,
                   count);
      else
        wcscpy(txt, L"Page -/- (Press to change page)");
      memset(&ti, 0, sizeof(ti));
      ti.cbSize = sizeof(ti);
      ti.hwnd = tb_wnd;
      ti.uId = (UINT_PTR) (i + 1);
      ti.lpszText = txt;
      SendMessageW(tb_tip, TTM_UPDATETIPTEXTW, 0, (LPARAM) &ti);
      return;
    }
}

/* ------------------------------------------------------------------ */
/* painting                                                           */
/* ------------------------------------------------------------------ */static COLORREF tb_color(Color c)
{
  int r = (int) (c.r * 255.0 + 0.5);
  int g = (int) (c.g * 255.0 + 0.5);
  int b = (int) (c.b * 255.0 + 0.5);
  if (r < 0) r = 0; if (r > 255) r = 255;
  if (g < 0) g = 0; if (g > 255) g = 255;
  if (b < 0) b = 0; if (b > 255) b = 255;
  return RGB(r, g, b);
}

static void tb_draw_sample(HDC dc, RECT r, const char *id)
{
  int cx = (r.left + r.right) / 2;
  int cy = (r.top + r.bottom) / 2;
  int box = tb_size - 8;
  if (box < 12) box = 12;
  RECT sw = { cx - box / 2, cy - box / 2, cx + box / 2, cy + box / 2 };

  if (!strcmp(id, "line_color"))
    {
      HBRUSH b = CreateSolidBrush(tb_color(g.pen_color));
      FillRect(dc, &sw, b);
      DeleteObject(b);
      FrameRect(dc, &sw, (HBRUSH) GetStockObject(BLACK_BRUSH));
    }
  else if (!strcmp(id, "thickness"))
    {
      int w = (int) g.pen_width;
      /* The sample runs opposite to the toolbar so it reads as a line
       * sample: a vertical bar in a horizontal toolbar and a horizontal bar
       * in a vertical toolbar.  It spans 70% of the toolbar's height
       * (horizontal toolbar) or width (vertical toolbar). */
      int span = (g.toolbar_vertical ? (r.right - r.left)
                                     : (r.bottom - r.top));
      int len;
      LOGBRUSH lb;
      HPEN p;
      HGDIOBJ old;

      if (span < 1) span = 1;
      len = (int) (span * 0.70 + 0.5);
      if (len < 1) len = 1;
      if (len > span) len = span;
      if (w < 1) w = 1;
      if (w > span) w = span;
      lb.lbStyle = BS_SOLID;
      lb.lbColor = RGB(0, 0, 0);
      lb.lbHatch = 0;
      p = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT |
                       PS_JOIN_MITER, (DWORD) w, &lb, 0, NULL);
      if (!p) p = CreatePen(PS_SOLID, w, RGB(0, 0, 0));
      old = SelectObject(dc, p);
      if (g.toolbar_vertical)
        {
          int x0 = cx - len / 2;
          int x1 = x0 + len;
          MoveToEx(dc, x0, cy, NULL);
          LineTo(dc, x1, cy);
        }
      else
        {
          int y0 = cy - len / 2;
          int y1 = y0 + len;
          MoveToEx(dc, cx, y0, NULL);
          LineTo(dc, cx, y1);
        }
      SelectObject(dc, old);
      DeleteObject(p);
    }
  else if (!strcmp(id, "fill_color"))
    {
      if (g.fill_enabled)
        {
          HBRUSH b = CreateSolidBrush(tb_color(g.fill_color));
          FillRect(dc, &sw, b);
          DeleteObject(b);
          FrameRect(dc, &sw, (HBRUSH) GetStockObject(BLACK_BRUSH));
        }
      else
        {
          HBRUSH b = (HBRUSH) GetStockObject(NULL_BRUSH);
          HGDIOBJ ob = SelectObject(dc, b);
          Rectangle(dc, sw.left, sw.top, sw.right, sw.bottom);
          MoveToEx(dc, sw.left, sw.top, NULL);
          LineTo(dc, sw.right, sw.bottom);
          SelectObject(dc, ob);
        }
    }
  else if (!strcmp(id, "eraser_size"))
    {
      int s = (int) g.eraser_width / 2;
      if (s < 8) s = 8;
      if (s > box) s = box;
      HBRUSH b = (HBRUSH) GetStockObject(WHITE_BRUSH);
      HPEN p = CreatePen(PS_SOLID, 1, RGB(110, 110, 110));
      HGDIOBJ ob = SelectObject(dc, b);
      HGDIOBJ op = SelectObject(dc, p);
      Ellipse(dc, cx - s / 2, cy - s / 2, cx + s / 2, cy + s / 2);
      SelectObject(dc, op);
      SelectObject(dc, ob);
      DeleteObject(p);
    }
}

static void tb_paint(HWND h)
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(h, &ps);
  RECT cr;
  GetClientRect(h, &cr);

  HDC mem = CreateCompatibleDC(dc);
  HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
  HGDIOBJ old = SelectObject(mem, bmp);

  HBRUSH bg = CreateSolidBrush(RGB(250, 250, 250));
  FillRect(mem, &cr, bg);
  DeleteObject(bg);

  for (int i = 0; i < tb_vis_count; i++)
    {
      const TbAction *a = &tb_actions[tb_vis[i]];
      RECT r = tb_button_rect(i);
      if (tb_active(a->cmd))
        {
          HBRUSH b = CreateSolidBrush(RGB(205, 228, 252));
          FillRect(mem, &r, b);
          DeleteObject(b);
        }
      else if (i == tb_hover || i == tb_pressed)
        {
          HBRUSH b = CreateSolidBrush(RGB(232, 240, 250));
          FillRect(mem, &r, b);
          DeleteObject(b);
        }

      if (!strcmp(a->id, "page_number"))
        {
          wchar_t txt[32];
          int count = paper_page_count();
          int index = paper_view_page_index();
          HFONT f;
          HGDIOBJ of;
          /* Only the current page number on the icon; the total is in the
           * tooltip. */
          if (count > 0)
            _snwprintf(txt, 32, L"%d", index + 1);
          else
            wcscpy(txt, L"-");
          f = CreateFontW(-(int) (tb_size * 0.55), 0, 0, 0, FW_NORMAL, 0, 0,
                          0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
          of = f ? SelectObject(mem, f) : NULL;
          SetBkMode(mem, TRANSPARENT);
          SetTextColor(mem, RGB(40, 40, 40));
          {
            RECT tr = r;
            DrawTextW(mem, txt, -1, &tr,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
          }
          if (of) SelectObject(mem, of);
          if (f) DeleteObject(f);
        }
      else if (!strcmp(a->id, "line_color") || !strcmp(a->id, "thickness") ||
          !strcmp(a->id, "fill_color") || !strcmp(a->id, "eraser_size"))
        {
          tb_draw_sample(mem, r, a->id);
        }
      else
        {
          const char *icon_name = a->icon;
          if (!strcmp(a->id, "save"))
            icon_name = g.paper == PAPER_NONE ? "screenshot.png" : "save.png";
          HBITMAP icon = tb_icon(icon_name, tb_size);
          if (icon)
            {
              HDC idc = CreateCompatibleDC(dc);
              HGDIOBJ io = SelectObject(idc, icon);
              int x = r.left + (r.right - r.left - tb_size) / 2;
              int y = r.top + (r.bottom - r.top - tb_size) / 2;
              BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
              if (!tb_action_enabled(tb_vis[i])) bf.SourceConstantAlpha = 90;
              AlphaBlend(mem, x, y, tb_size, tb_size, idc, 0, 0, tb_size,
                         tb_size, bf);
              SelectObject(idc, io);
              DeleteDC(idc);
            }
        }
    }

  BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
  SelectObject(mem, old);
  DeleteObject(bmp);
  DeleteDC(mem);
  EndPaint(h, &ps);
}

static int tb_hit(int x, int y)
{
  for (int i = 0; i < tb_vis_count; i++)
    {
      RECT r = tb_button_rect(i);
      if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
        return i;
    }
  return -1;
}

static void tb_save_position(void)
{
  RECT r;
  if (!IsWindow(tb_wnd) || !GetWindowRect(tb_wnd, &r)) return;
  g.toolbar_x = r.left;
  g.toolbar_y = r.top;
  config_save();
}

static void tb_snap(void)
{
  RECT r;
  if (!IsWindow(tb_wnd) || !GetWindowRect(tb_wnd, &r)) return;
  int w = r.right - r.left, h = r.bottom - r.top;
  int left = g.vx, top = g.vy, right = g.vx + g.vw, bottom = g.vy + g.vh;
  /* Snap flush against the screen edge, without a gap. */
  if (r.left - left < 24) r.left = left;
  if (r.top - top < 24) r.top = top;
  if (right - r.right < 24) r.left = right - w;
  if (bottom - r.bottom < 24) r.top = bottom - h;
  if (r.left < left) r.left = left;
  if (r.top < top) r.top = top;
  if (r.left + w > right) r.left = right - w;
  if (r.top + h > bottom) r.top = bottom - h;
  SetWindowPos(tb_wnd, HWND_TOPMOST, r.left, r.top, 0, 0,
               SWP_NOSIZE | SWP_NOACTIVATE);
  tb_save_position();
}

static void tb_activate_button(HWND h, int index)
{
  if (index < 0 || index >= tb_vis_count) return;
  const TbAction *a = &tb_actions[tb_vis[index]];
  if (!tb_action_enabled(tb_vis[index])) return;

  if (a->cmd)
    {
      ui_execute_command(a->cmd);
      return;
    }

  RECT r = tb_button_rect(index);
  POINT p = { r.left, r.bottom };
  ClientToScreen(h, &p);

  if (a->popup < 0)
    ui_show_menu(p.x, p.y);
  else
    ui_show_toolbar_menu(a->popup, p.x, p.y);
}

static LRESULT CALLBACK tb_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
    {
    case WM_PAINT:
      tb_paint(h);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_MOUSEMOVE:
      {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (tb_pending && !tb_drag)
          {
            POINT p;
            GetCursorPos(&p);
            if (abs(p.x - tb_press_origin.x) > 10 ||
                abs(p.y - tb_press_origin.y) > 10)
              {
                tb_drag = 1;
                tb_pressed = -1;
                InvalidateRect(h, NULL, FALSE);
              }
          }
        if (tb_drag)
          {
            POINT p;
            GetCursorPos(&p);
            SetWindowPos(h, HWND_TOPMOST,
                         tb_drag_pos.x + p.x - tb_drag_origin.x,
                         tb_drag_pos.y + p.y - tb_drag_origin.y, 0, 0,
                         SWP_NOSIZE | SWP_NOACTIVATE);
            return 0;
          }
        int hit = tb_hit(x, y);
        if (hit != tb_hover)
          {
            tb_hover = hit;
            InvalidateRect(h, NULL, FALSE);
          }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
      }
      return 0;
    case WM_MOUSELEAVE:
      if (tb_hover != -1)
        {
          tb_hover = -1;
          InvalidateRect(h, NULL, FALSE);
        }
      return 0;
    case WM_LBUTTONDOWN:
      {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        POINT p;
        GetCursorPos(&p);
        RECT r;
        GetWindowRect(h, &r);
        tb_pending = 1;
        tb_drag = 0;
        tb_press_origin = p;
        tb_drag_origin = p;
        tb_drag_pos.x = r.left;
        tb_drag_pos.y = r.top;
        tb_pressed = tb_hit(x, y);
        SetCapture(h);
        InvalidateRect(h, NULL, FALSE);
      }
      return 0;
    case WM_LBUTTONUP:
      if (tb_drag)
        {
          tb_drag = 0;
          tb_pending = 0;
          ReleaseCapture();
          tb_snap();
          return 0;
        }
      if (tb_pending)
        {
          int hit = tb_hit(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
          int index = (hit == tb_pressed) ? hit : -1;
          tb_pending = 0;
          tb_pressed = -1;
          ReleaseCapture();
          InvalidateRect(h, NULL, FALSE);
          if (index >= 0) tb_activate_button(h, index);
        }
      return 0;
    case WM_RBUTTONUP:
      {
        POINT p;
        GetCursorPos(&p);
        ui_show_menu(p.x, p.y);
      }
      return 0;
    case WM_SETCURSOR:
      {
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;
      }
    case WM_DESTROY:
      tb_tooltips_destroy();
      tb_wnd = NULL;
      return 0;
    }
  return DefWindowProcW(h, msg, wp, lp);
}

static void tb_place(void)
{
  if (!IsWindow(tb_wnd)) return;
  int w, h;
  tb_window_size(&w, &h);
  int x = g.toolbar_x, y = g.toolbar_y;
  int left = g.vx, top = g.vy, right = g.vx + g.vw, bottom = g.vy + g.vh;
  if (x < left || x + w > right) x = left + 40;
  if (y < top || y + h > bottom) y = top + 40;
  if (x + w > right) x = right - w;
  if (y + h > bottom) y = bottom - h;
  if (x < left) x = left;
  if (y < top) y = top;
  SetWindowPos(tb_wnd, HWND_TOPMOST, x, y, w, h,
               SWP_NOACTIVATE |
               ((g.show_toolbar && !g.hide_toolbar_temp)
                ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

/* ------------------------------------------------------------------ */
/* configure dialog                                                   */
/* ------------------------------------------------------------------ */

typedef struct
{
  int order[TB_ACTION_COUNT];
  int visible[TB_ACTION_COUNT];
  int count;
} TbConfig;

static void tb_cfg_add_row(HWND list, const TbConfig *c, int i)
{
  const TbAction *a = &tb_actions[c->order[i]];
  wchar_t wlabel[128];
  MultiByteToWideChar(CP_UTF8, 0, a->label, -1, wlabel, 128);
  wchar_t row[160];
  _snwprintf(row, 160, L"%s %s", c->visible[c->order[i]] ? L"[x]" : L"[ ]",
             wlabel);
  SendMessageW(list, LB_ADDSTRING, 0, (LPARAM) row);
}

static void tb_cfg_refresh(HWND h, TbConfig *c, int select)
{
  HWND list = GetDlgItem(h, ID_TB_LIST);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  for (int i = 0; i < c->count; i++) tb_cfg_add_row(list, c, i);
  if (select < 0) select = 0;
  if (select >= c->count) select = c->count - 1;
  SendMessageW(list, LB_SETCURSEL, (WPARAM) select, 0);
  EnableWindow(GetDlgItem(h, ID_TB_SHOW), c->count > 0);
  EnableWindow(GetDlgItem(h, ID_TB_UP), c->count > 0);
  EnableWindow(GetDlgItem(h, ID_TB_DOWN), c->count > 0);
}

static WNDPROC tb_cfg_list_old;
static int tb_cfg_drag_from = -1;

static int tb_cfg_index_at(HWND list, int y, int count)
{
  LRESULT p = SendMessageW(list, LB_ITEMFROMPOINT, 0, MAKELPARAM(0, y));
  if (HIWORD(p)) return y < 0 ? 0 : count - 1;
  return (int) LOWORD(p);
}

/* Drag rows by their handle to change the toolbar order. */
static LRESULT CALLBACK tb_cfg_list_proc(HWND h, UINT msg, WPARAM wp,
                                         LPARAM lp)
{
  HWND parent = GetParent(h);
  TbConfig *c = (TbConfig *) GetWindowLongPtrW(parent, GWLP_USERDATA);

  switch (msg)
    {
    case WM_LBUTTONDOWN:
      if (c)
        {
          int idx = tb_cfg_index_at(h, GET_Y_LPARAM(lp), c->count);
          if (idx >= 0 && idx < c->count)
            {
              SetFocus(h);
              tb_cfg_drag_from = idx;
              SendMessageW(h, LB_SETCURSEL, (WPARAM) idx, 0);
              SetCapture(h);
              InvalidateRect(h, NULL, FALSE);
              return 0;
            }
        }
      break;

    case WM_MOUSEMOVE:
      if (c && tb_cfg_drag_from >= 0 && GetCapture() == h)
        {
          int to = tb_cfg_index_at(h, GET_Y_LPARAM(lp), c->count);
          if (to >= 0 && to < c->count && to != tb_cfg_drag_from)
            {
              int moving = c->order[tb_cfg_drag_from];
              if (to > tb_cfg_drag_from)
                for (int i = tb_cfg_drag_from; i < to; i++)
                  c->order[i] = c->order[i + 1];
              else
                for (int i = tb_cfg_drag_from; i > to; i--)
                  c->order[i] = c->order[i - 1];
              c->order[to] = moving;
              tb_cfg_drag_from = to;
              tb_cfg_refresh(parent, c, to);
            }
        }
      return 0;

    case WM_LBUTTONUP:
      if (tb_cfg_drag_from >= 0)
        {
          tb_cfg_drag_from = -1;
          if (GetCapture() == h) ReleaseCapture();
          InvalidateRect(h, NULL, FALSE);
          return 0;
        }
      break;

    case WM_CAPTURECHANGED:
      tb_cfg_drag_from = -1;
      break;
    }
  return CallWindowProcW(tb_cfg_list_old, h, msg, wp, lp);
}

static void tb_cfg_serialize(const TbConfig *c){
  char order[TOOLBAR_ITEMS_MAX];
  char items[TOOLBAR_ITEMS_MAX];
  size_t op = 0, ip = 0;
  order[0] = 0;
  items[0] = 0;

  for (int i = 0; i < c->count; i++)
    {
      const char *id = tb_actions[c->order[i]].id;
      size_t n = strlen(id);
      if (op + n + 2 >= sizeof(order)) break;
      if (op) order[op++] = ',';
      memcpy(order + op, id, n);
      op += n;
      order[op] = 0;

      if (c->visible[c->order[i]])
        {
          if (ip + n + 2 >= sizeof(items)) break;
          if (ip) items[ip++] = ',';
          memcpy(items + ip, id, n);
          ip += n;
          items[ip] = 0;
        }
    }
  if (!items[0]) strcpy(items, "none");
  strncpy(g.toolbar_order, order, sizeof(g.toolbar_order) - 1);
  g.toolbar_order[sizeof(g.toolbar_order) - 1] = 0;
  strncpy(g.toolbar_items, items, sizeof(g.toolbar_items) - 1);
  g.toolbar_items[sizeof(g.toolbar_items) - 1] = 0;
}

static LRESULT CALLBACK tb_cfg_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  TbConfig *c = (TbConfig *) GetWindowLongPtrW(h, GWLP_USERDATA);

  switch (msg)
    {
    case WM_CREATE:
      {
        c = (TbConfig *) calloc(1, sizeof(TbConfig));
        if (!c) return -1;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR) c);
        tb_parse_order(g.toolbar_order, c->order, &c->count);
        tb_parse_visible(g.toolbar_items, c->visible);

        HWND st = CreateWindowW(L"STATIC",
          L"Show or hide toolbar actions. Drag a row by its handle to change "
          L"the order; double-click a row to show or hide it.",
          WS_CHILD | WS_VISIBLE, 16, 12, 388, 36, h, NULL, g.hinst, NULL);
        ui_set_font(st);

        HWND list = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY |
          LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
          16, 52, 388, 352, h,
          (HMENU) (INT_PTR) ID_TB_LIST, g.hinst, NULL);
        ui_set_font(list);
        SendMessageW(list, LB_SETITEMHEIGHT, 0, 26);
        tb_cfg_list_old = (WNDPROC) SetWindowLongPtrW(
          list, GWLP_WNDPROC, (LONG_PTR) tb_cfg_list_proc);

        HWND b = CreateWindowW(L"BUTTON", L"Show/Hide",
                               WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                               16, 416, 100, 28, h,
                               (HMENU) (INT_PTR) ID_TB_SHOW, g.hinst, NULL);
        ui_set_font(b);
        b = CreateWindowW(L"BUTTON", L"Up",
                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          124, 416, 70, 28, h,
                          (HMENU) (INT_PTR) ID_TB_UP, g.hinst, NULL);
        ui_set_font(b);
        b = CreateWindowW(L"BUTTON", L"Down",
                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          202, 416, 70, 28, h,
                          (HMENU) (INT_PTR) ID_TB_DOWN, g.hinst, NULL);
        ui_set_font(b);
        b = CreateWindowW(L"BUTTON", L"OK",
                          WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                          230, 462, 80, 28, h, (HMENU) IDOK, g.hinst, NULL);
        ui_set_font(b);
        b = CreateWindowW(L"BUTTON", L"Cancel",
                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          318, 462, 80, 28, h, (HMENU) IDCANCEL, g.hinst,
                          NULL);
        ui_set_font(b);

        tb_cfg_refresh(h, c, 0);
      }
      return 0;

    case WM_COMMAND:
      {
        int id = LOWORD(wp);
        if (!c) return 0;
        HWND list = GetDlgItem(h, ID_TB_LIST);
        int sel = (int) SendMessageW(list, LB_GETCURSEL, 0, 0);

        if (id == ID_TB_SHOW && sel >= 0 && sel < c->count)
          {
            int idx = c->order[sel];
            c->visible[idx] = !c->visible[idx];
            tb_cfg_refresh(h, c, sel);
            return 0;
          }
        if (id == ID_TB_UP && sel > 0)
          {
            int tmp = c->order[sel];
            c->order[sel] = c->order[sel - 1];
            c->order[sel - 1] = tmp;
            tb_cfg_refresh(h, c, sel - 1);
            return 0;
          }
        if (id == ID_TB_DOWN && sel >= 0 && sel < c->count - 1)
          {
            int tmp = c->order[sel];
            c->order[sel] = c->order[sel + 1];
            c->order[sel + 1] = tmp;
            tb_cfg_refresh(h, c, sel + 1);
            return 0;
          }
        if (id == ID_TB_LIST && HIWORD(wp) == LBN_DBLCLK && sel >= 0 &&
            sel < c->count)
          {
            int idx = c->order[sel];
            c->visible[idx] = !c->visible[idx];
            tb_cfg_refresh(h, c, sel);
            return 0;
          }
        if (id == IDOK)
          {
            tb_cfg_serialize(c);
            toolbar_reload();
            config_save();
            DestroyWindow(h);
            return 0;
          }
        if (id == IDCANCEL)
          {
            DestroyWindow(h);
            return 0;
          }
      }
      return 0;

    case WM_DRAWITEM:
      {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *) lp;
        const TbAction *a;
        wchar_t text[180];
        COLORREF fg, bg;
        HBRUSH br;
        int selected;

        if (!c || !dis || dis->CtlID != ID_TB_LIST ||
            dis->itemID == (UINT) -1)
          return TRUE;
        if ((int) dis->itemID >= c->count) return TRUE;
        a = &tb_actions[c->order[dis->itemID]];
        text[0] = 0;
        SendMessageW(dis->hwndItem, LB_GETTEXT, (WPARAM) dis->itemID,
                     (LPARAM) text);
        selected = (dis->itemState & ODS_SELECTED) != 0;
        bg = GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
        fg = GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
        br = CreateSolidBrush(bg);
        FillRect(dis->hDC, &dis->rcItem, br);
        DeleteObject(br);

        {
          HBITMAP icon = tb_icon(a->icon, 24);
          if (icon)
            {
              HDC idc = CreateCompatibleDC(dis->hDC);
              HGDIOBJ old = SelectObject(idc, icon);
              int iy = dis->rcItem.top +
                       (dis->rcItem.bottom - dis->rcItem.top - 24) / 2;
              BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
              AlphaBlend(dis->hDC, dis->rcItem.left + 5, iy, 24, 24, idc,
                         0, 0, 24, 24, bf);
              SelectObject(idc, old);
              DeleteDC(idc);
            }
        }

        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, fg);
        {
          RECT tr = dis->rcItem;
          tr.left += 36;
          tr.right -= 26;
          DrawTextW(dis->hDC, text, -1, &tr,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX |
                    DT_END_ELLIPSIS);
        }
        {
          RECT gr = dis->rcItem;
          gr.left = gr.right - 24;
          SetTextColor(dis->hDC, RGB(120, 120, 120));
          DrawTextW(dis->hDC, L"\x2261", -1, &gr,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
      }
      return TRUE;

    case WM_CLOSE:
      DestroyWindow(h);
      return 0;

    case WM_DESTROY:
      tb_cfg_drag_from = -1;
      if (c) free(c);
      SetWindowLongPtrW(h, GWLP_USERDATA, 0);
      return 0;
    }
  return DefWindowProcW(h, msg, wp, lp);
}

static void tb_cfg_register(void)
{
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.hInstance = g.hinst;
  wc.lpfnWndProc = tb_cfg_proc;
  wc.lpszClassName = TB_CFG_CLASS;
  wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  RegisterClassExW(&wc);
}

void toolbar_show_configure(void)
{
  tb_cfg_register();

  HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, TB_CFG_CLASS,
                           L"Configure Toolbar", WS_POPUP | WS_CAPTION |
                           WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 430,
                           540, g.overlay, NULL, g.hinst, NULL);
  ui_center_window(h, 430, 540);
  ui_run_modal(h, 0);
}

/* ------------------------------------------------------------------ */
/* public API                                                         */
/* ------------------------------------------------------------------ */

void toolbar_reload(void)
{
  if (!IsWindow(tb_wnd)) return;
  tb_load_items();
  tb_size = g.toolbar_icon_size;
  tb_hover = tb_pressed = -1;
  tb_clear_icons();
  tb_tooltips_create();
  tb_place();
  InvalidateRect(tb_wnd, NULL, FALSE);
}

/* Put a horizontal toolbar at the top centre of the screen, flush with the
 * top edge.  Used after Reset Settings. */
void toolbar_place_top_center(void)
{
  int w, h;

  if (!IsWindow(tb_wnd)) return;
  g.toolbar_vertical = 0;
  tb_window_size(&w, &h);
  g.toolbar_x = g.vx + (g.vw - w) / 2;
  g.toolbar_y = g.vy;
  tb_place();
  tb_tooltips_create();
  InvalidateRect(tb_wnd, NULL, FALSE);
}

void toolbar_init(void)
{
  if (tb_wnd) return;

  INITCOMMONCONTROLSEX icc;
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_WIN95_CLASSES;
  InitCommonControlsEx(&icc);

  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.hInstance = g.hinst;
  wc.lpfnWndProc = tb_proc;
  wc.lpszClassName = TB_CLASS;
  wc.hbrBackground = (HBRUSH) (COLOR_MENU + 1);
  wc.hCursor = LoadCursorW(NULL, IDC_HAND);
  RegisterClassExW(&wc);

  tb_size = g.toolbar_icon_size;
  if (tb_size < 16 || tb_size > 48) tb_size = 32;
  tb_load_items();
  tb_rebuild_visible();
  int w, h;
  tb_window_size(&w, &h);
  tb_wnd = CreateWindowExW(
    WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, TB_CLASS,
    L"Screenator Toolbar", WS_POPUP, g.toolbar_x, g.toolbar_y, w, h,
    g.overlay, NULL, g.hinst, NULL);
  tb_nicons = 0;
  tb_place();
  tb_tooltips_create();
}

void toolbar_raise(void)
{
  if (!IsWindow(tb_wnd)) return;
  /* SWP_SHOWWINDOW is needed even when the toolbar is already visible: it
   * re-inserts the owned popup above the full-screen input window. */
  SetWindowPos(tb_wnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
               ((g.show_toolbar && !g.hide_toolbar_temp)
                ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

void toolbar_refresh(void)
{
  if (!IsWindow(tb_wnd)) return;

  int changed = tb_load_items();
  if (tb_rebuild_visible()) changed = 1;
  if (tb_size != g.toolbar_icon_size)
    {
      tb_size = g.toolbar_icon_size;
      if (tb_size < 16 || tb_size > 48) tb_size = 32;
      tb_clear_icons();
      changed = 1;
    }
  if (changed)
    {
      tb_hover = tb_pressed = -1;
      tb_tooltips_create();
      tb_place();
    }
  else
    tb_page_tooltip_update();
  InvalidateRect(tb_wnd, NULL, FALSE);
  toolbar_raise();
}

void toolbar_set_visible(int visible)
{
  g.show_toolbar = visible ? 1 : 0;
  if (IsWindow(tb_wnd))
    ShowWindow(tb_wnd, g.show_toolbar ? SW_SHOWNA : SW_HIDE);
  config_save();
}

/* Hide the toolbar temporarily without changing the Show Toolbar setting. */
void toolbar_hide_temporary(int hide)
{
  g.hide_toolbar_temp = hide ? 1 : 0;
  if (IsWindow(tb_wnd))
    ShowWindow(tb_wnd, (hide || !g.show_toolbar) ? SW_HIDE : SW_SHOWNA);
}

void toolbar_toggle(void)
{
  toolbar_set_visible(!g.show_toolbar);
}

void toolbar_set_vertical(int vertical)
{
  g.toolbar_vertical = vertical ? 1 : 0;
  tb_place();
  tb_tooltips_create();
  if (IsWindow(tb_wnd)) InvalidateRect(tb_wnd, NULL, FALSE);
  config_save();
}

void toolbar_set_icon_size(int size)
{
  if (size < 16 || size > 48) return;
  g.toolbar_icon_size = size;
  tb_size = size;
  tb_clear_icons();
  tb_place();
  tb_tooltips_create();
  if (IsWindow(tb_wnd)) InvalidateRect(tb_wnd, NULL, FALSE);
  config_save();
}

int toolbar_selfcheck(void)
{
  int ok = 1;

  if (TB_ACTION_COUNT < 23) ok = 0;
  for (int i = 0; i < TB_ACTION_COUNT; i++)
    if (!tb_actions[i].id[0] || !tb_actions[i].label[0] ||
        !tb_actions[i].icon[0])
      ok = 0;

  if (!IsWindow(tb_wnd) || !IsWindow(tb_tip))
    {
      /* --selftest does not create windows; only --uitest reaches here. */
      if (g.msgwin) ok = 0;
    }
  else
    {
      int tools = (int) SendMessageW(tb_tip, TTM_GETTOOLCOUNT, 0, 0);
      if (tools != tb_vis_count) ok = 0;
    }

  tb_cfg_register();
  HWND h = CreateWindowExW(WS_EX_TOOLWINDOW, TB_CFG_CLASS,
                           L"Configure Toolbar", WS_POPUP, 0, 0, 430, 540,
                           g.overlay, NULL, g.hinst, NULL);
  if (!h) ok = 0;
  else
    {
      HWND list = GetDlgItem(h, ID_TB_LIST);
      if (!list || SendMessageW(list, LB_GETCOUNT, 0, 0) != TB_ACTION_COUNT)
        ok = 0;
      DestroyWindow(h);
    }
  return ok;
}
