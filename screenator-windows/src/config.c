/*
 * Screenator for Windows -- settings, annotations and paths.
 * GPL-2.0-or-later.  See app.h.
 */

#include "app.h"

#include <wchar.h>
#include <ctype.h>

const char *g_shortcut_keys[SK_COUNT] =
{
  "arrow", "text", "pen", "floating_icon", "toolbar", "switch_mode",
  "screenshot", "timer", "undo", "redo", "leave_paper",
  "toggle_visibility", "quit"
};

const char *g_shortcut_defaults[SK_COUNT] =
{
  "F6", "F7", "F8", "F9", "F10", "F3", "F4", "F12",
  "Ctrl+Z", "Ctrl+Shift+Z", "Ctrl+Q", "Ctrl+T", "Alt+F4"
};

const char *g_shortcut_labels[SK_COUNT] =
{
  "Arrow", "Text", "Pen", "Floating Icon", "Toolbar", "Switch Mode",
  "Screenshot", "Timer", "Undo", "Redo", "Leave Paper",
  "Toggle Visibility", "Quit"
};

static wchar_t *wide_from_utf8(const char *s)
{
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  wchar_t *w = (wchar_t *) malloc((size_t) n * sizeof(wchar_t));
  if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}

static char *utf8_from_wide(const wchar_t *w)
{
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  char *s = (char *) malloc((size_t) n);
  if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
  return s;
}

static void make_app_dir(void)
{
  wchar_t dir[MAX_PATH];
  wcscpy(dir, g.config_path);
  wchar_t *slash = wcsrchr(dir, L'\\');
  if (slash) *slash = 0;
  SHCreateDirectoryExW(NULL, dir, NULL);
}

static int ascii_iequal(const char *a, const char *b)
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

static char *trim_ascii(char *s)
{
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
  char *end = s + strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                     end[-1] == '\r' || end[-1] == '\n'))
    *--end = 0;
  return s;
}

static int parse_key_name(const char *name, UINT *vk)
{
  if (!name || !name[0]) return 0;

  if (name[0] == 'F' || name[0] == 'f')
    {
      char *end = NULL;
      long n = strtol(name + 1, &end, 10);
      if (end && end != name + 1 && *end == 0 && n >= 1 && n <= 24)
        {
          *vk = VK_F1 + (UINT) (n - 1);
          return 1;
        }
      return 0;
    }

  if (name[1] == 0)
    {
      int c = (unsigned char) name[0];
      if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
      if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        {
          *vk = (UINT) c;
          return 1;
        }
    }

  if (ascii_iequal(name, "Escape") || ascii_iequal(name, "Esc"))
    *vk = VK_ESCAPE;
  else if (ascii_iequal(name, "Tab")) *vk = VK_TAB;
  else if (ascii_iequal(name, "Space")) *vk = VK_SPACE;
  else if (ascii_iequal(name, "Enter") || ascii_iequal(name, "Return"))
    *vk = VK_RETURN;
  else if (ascii_iequal(name, "Backspace")) *vk = VK_BACK;
  else if (ascii_iequal(name, "Delete") || ascii_iequal(name, "Del"))
    *vk = VK_DELETE;
  else if (ascii_iequal(name, "Insert") || ascii_iequal(name, "Ins"))
    *vk = VK_INSERT;
  else if (ascii_iequal(name, "Home")) *vk = VK_HOME;
  else if (ascii_iequal(name, "End")) *vk = VK_END;
  else if (ascii_iequal(name, "PageUp") || ascii_iequal(name, "PgUp"))
    *vk = VK_PRIOR;
  else if (ascii_iequal(name, "PageDown") || ascii_iequal(name, "PgDn"))
    *vk = VK_NEXT;
  else if (ascii_iequal(name, "Left")) *vk = VK_LEFT;
  else if (ascii_iequal(name, "Right")) *vk = VK_RIGHT;
  else if (ascii_iequal(name, "Up")) *vk = VK_UP;
  else if (ascii_iequal(name, "Down")) *vk = VK_DOWN;
  else return 0;
  return 1;
}

int shortcut_parse(const char *text, UINT *mods, UINT *vk)
{
  char work[SHORTCUT_TEXT_MAX * 2];
  UINT m = 0, key = 0;

  if (!text) return 0;
  strncpy(work, text, sizeof(work) - 1);
  work[sizeof(work) - 1] = 0;
  char *p = trim_ascii(work);
  if (!p[0] || ascii_iequal(p, "None"))
    {
      *mods = 0;
      *vk = 0;
      return 1;
    }

  while (1)
    {
      char *plus = strchr(p, '+');
      if (!plus) break;
      *plus = 0;
      char *mod = trim_ascii(p);
      if (ascii_iequal(mod, "Ctrl") || ascii_iequal(mod, "Control"))
        m |= MOD_CONTROL;
      else if (ascii_iequal(mod, "Shift"))
        m |= MOD_SHIFT;
      else if (ascii_iequal(mod, "Alt"))
        m |= MOD_ALT;
      else if (ascii_iequal(mod, "Win") || ascii_iequal(mod, "Windows") ||
               ascii_iequal(mod, "Super") || ascii_iequal(mod, "Meta"))
        m |= MOD_WIN;
      else
        return 0;
      p = plus + 1;
    }

  p = trim_ascii(p);
  if (!p[0] || !parse_key_name(p, &key)) return 0;

  /* A bare letter or digit would steal ordinary typing from every
   * application, so require a modifier for those keys. */
  if (!m && ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')))
    return 0;

  *mods = m;
  *vk = key;
  return 1;
}

static const char *key_name(UINT vk, char *buf, int size)
{
  if (vk >= VK_F1 && vk <= VK_F24)
    {
      snprintf(buf, (size_t) size, "F%u", (unsigned) (vk - VK_F1 + 1));
      return buf;
    }
  if (vk >= 'A' && vk <= 'Z')
    {
      snprintf(buf, (size_t) size, "%c", (char) vk);
      return buf;
    }
  if (vk >= '0' && vk <= '9')
    {
      snprintf(buf, (size_t) size, "%c", (char) vk);
      return buf;
    }
  switch (vk)
    {
    case VK_ESCAPE: return "Escape";
    case VK_TAB: return "Tab";
    case VK_SPACE: return "Space";
    case VK_RETURN: return "Enter";
    case VK_BACK: return "Backspace";
    case VK_DELETE: return "Delete";
    case VK_INSERT: return "Insert";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    default: return NULL;
    }
}

int shortcut_format(UINT mods, UINT vk, char *buf, int size)
{
  char key[16];
  const char *name;

  if (!buf || size <= 0) return 0;
  buf[0] = 0;
  if (!vk && !mods)
    {
      strncpy(buf, "None", (size_t) size - 1);
      buf[size - 1] = 0;
      return 1;
    }
  name = key_name(vk, key, sizeof(key));
  if (!name) return 0;

  if (mods & MOD_CONTROL) strncat(buf, "Ctrl+", (size_t) size - strlen(buf) - 1);
  if (mods & MOD_ALT) strncat(buf, "Alt+", (size_t) size - strlen(buf) - 1);
  if (mods & MOD_SHIFT) strncat(buf, "Shift+", (size_t) size - strlen(buf) - 1);
  if (mods & MOD_WIN) strncat(buf, "Win+", (size_t) size - strlen(buf) - 1);
  strncat(buf, name, (size_t) size - strlen(buf) - 1);
  return 1;
}

static int shortcut_reserved(UINT mods, UINT vk)
{
  if (vk == VK_ESCAPE) return 1;                       /* fixed Escape */
  if (mods == MOD_CONTROL && vk == VK_F10) return 1;   /* quick tool menu */
  return 0;
}

void config_shortcuts_load(const wchar_t *path)
{
  int i, j;

  for (i = 0; i < SK_COUNT; i++)
    {
      wchar_t *wkey = wide_from_utf8(g_shortcut_keys[i]);
      wchar_t *wdef = wide_from_utf8(g_shortcut_defaults[i]);
      wchar_t wbuf[SHORTCUT_TEXT_MAX * 2];
      UINT mods = 0, vk = 0;

      wbuf[0] = 0;
      if (wkey && wdef)
        GetPrivateProfileStringW(L"Shortcuts", wkey, wdef, wbuf,
                                 (DWORD) (sizeof(wbuf) / sizeof(wbuf[0])),
                                 path);

      char *utf8 = utf8_from_wide(wbuf);
      char defbuf[SHORTCUT_TEXT_MAX];
      int borrowed = 0;
      int ok = utf8 && shortcut_parse(utf8, &mods, &vk);
      if (!ok || shortcut_reserved(mods, vk))
        {
          free(utf8);
          strncpy(defbuf, g_shortcut_defaults[i], sizeof(defbuf) - 1);
          defbuf[sizeof(defbuf) - 1] = 0;
          utf8 = defbuf;
          borrowed = 1;
          ok = shortcut_parse(utf8, &mods, &vk);
          if (!ok || shortcut_reserved(mods, vk))
            {
              mods = 0;
              vk = 0;
            }
        }

      char canonical[SHORTCUT_TEXT_MAX];
      if (!shortcut_format(mods, vk, canonical, sizeof(canonical)))
        strcpy(canonical, "None");

      /* Reject duplicates instead of registering only one of them. */
      for (j = 0; j < i; j++)
        {
          UINT pm = 0, pv = 0;
          if (!shortcut_parse(g.shortcuts[j], &pm, &pv)) continue;
          if (pv && pv == vk && pm == mods)
            {
              strcpy(canonical, "None");
              break;
            }
        }

      strncpy(g.shortcuts[i], canonical, SHORTCUT_TEXT_MAX - 1);
      g.shortcuts[i][SHORTCUT_TEXT_MAX - 1] = 0;
      if (!borrowed) free(utf8);
      free(wkey);
      free(wdef);
    }
}

void config_shortcuts_save(void)
{
  int i;
  for (i = 0; i < SK_COUNT; i++)
    {
      wchar_t *wkey = wide_from_utf8(g_shortcut_keys[i]);
      wchar_t *wval = wide_from_utf8(g.shortcuts[i]);
      if (wkey && wval)
        WritePrivateProfileStringW(L"Shortcuts", wkey, wval, g.config_path);
      free(wkey);
      free(wval);
    }
}

void config_defaults(void)
{
  g.pen_width = 6;
  g.eraser_width = 60;
  g.opacity = 1.0;
  g.pen_color = color_from_hex("#FF0000", NULL);
  g.custom_line_color = color_from_hex("#FF00FF", NULL);
  g.fill_color = color_from_hex("#00AA00", NULL);
  g.custom_fill_color = color_from_hex("#FF00FF", NULL);
  g.fill_enabled = 0;
  g.marker_opacity = 0.4;
  g.marker_saved_valid = 0;
  strcpy(g.font, "Arial Bold 24");
  g.icon_size = 48;
  g.icon_position = ICON_TR;
  g.show_icon = 1;
  g.show_indicator = 1;
  g.show_toolbar = 1;
  g.toolbar_vertical = 0;
  g.toolbar_icon_size = 32;
  g.paper_size_kind = PAPER_SIZE_SCREEN;
  g.paper_custom_w_mm = 210;
  g.paper_custom_h_mm = 297;
  g.toolbar_x = 40;
  g.toolbar_y = 40;
  strncpy(g.toolbar_items, TOOLBAR_DEFAULT_ITEMS,
          sizeof(g.toolbar_items) - 1);
  g.toolbar_items[sizeof(g.toolbar_items) - 1] = 0;
  strncpy(g.toolbar_order, TOOLBAR_DEFAULT_ITEMS,
          sizeof(g.toolbar_order) - 1);
  g.toolbar_order[sizeof(g.toolbar_order) - 1] = 0;
  for (int i = 0; i < SK_COUNT; i++)
    {
      strncpy(g.shortcuts[i], g_shortcut_defaults[i],
              sizeof(g.shortcuts[i]) - 1);
      g.shortcuts[i][sizeof(g.shortcuts[i]) - 1] = 0;
    }
  g.save_on_exit = 1;
  g.hide_hints = 0;
  g.wheel_mode = WHEEL_COLOR;
  g.stylus_mode = STYLUS_COLOR;
  g.tool = TOOL_NONE;
  g.pending_tool = TOOL_NONE;
  g.tool_delay_seconds = 0;
  g.timer_running = 0;
  g.timer_delay_seconds = 2;
  g.paper = PAPER_NONE;
  g.paper_continuous_scroll = 1;
  g.math_polar = 0;
  strcpy(g.math_expr, "cos(2*pi*x)");
  g.math_xmin = -2.0;
  g.math_xmax = 2.0;
  strcpy(g.math_polar_expr, "cos(3*theta)");
  g.math_tmin = 0.0;
  g.math_tmax = 2.0 * M_PI;
}

static void get_app_paths(void)
{
  wchar_t appdata[MAX_PATH] = L"";
  if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata)))
    wcscpy(appdata, L".");

  _snwprintf(g.config_dir, MAX_PATH, L"%s\\Screenator", appdata);
  SHCreateDirectoryExW(NULL, g.config_dir, NULL);
  _snwprintf(g.config_path, MAX_PATH, L"%s\\screenator.ini", g.config_dir);
  _snwprintf(g.annotations_path, MAX_PATH,
             L"%s\\screenator-annotations.png", g.config_dir);
  /* Older builds copied hints.html here; hints now come from the installed
   * asset only. */
  {
    wchar_t old_hints[MAX_PATH];
    _snwprintf(old_hints, MAX_PATH, L"%s\\hints.html", g.config_dir);
    DeleteFileW(old_hints);
  }
}

static unsigned get_int(const wchar_t *path, const wchar_t *key, unsigned def)
{
  return (unsigned) GetPrivateProfileIntW(L"screenator", key, (int) def, path);
}

static double get_double(const wchar_t *path, const wchar_t *key, double def)
{
  wchar_t buf[64];
  GetPrivateProfileStringW(L"screenator", key, L"", buf, 64, path);
  if (!buf[0]) return def;
  wchar_t *end = NULL;
  double v = wcstod(buf, &end);
  return end && end != buf ? v : def;
}

static void get_string(const wchar_t *path, const wchar_t *key,
                       wchar_t *buf, int size, const wchar_t *def)
{
  GetPrivateProfileStringW(L"screenator", key, def, buf, size, path);
}

static Color get_color(const wchar_t *path, const wchar_t *key, Color def)
{
  wchar_t buf[64];
  int ok = 0;
  get_string(path, key, buf, 64, L"");
  char *s = utf8_from_wide(buf);
  Color c = s && s[0] ? color_from_hex(s, &ok) : def;
  free(s);
  return ok ? c : def;
}

void config_load_from(const wchar_t *path)
{
  wchar_t wbuf[MAX_PATH];

  g.pen_width = get_int(path, L"pen_width", g.pen_width);
  if (g.pen_width < 1 || g.pen_width > 100) g.pen_width = 6;
  g.eraser_width = get_int(path, L"eraser_width", g.eraser_width);
  g.opacity = get_double(path, L"opacity", g.opacity);
  if (g.opacity < 0.05 || g.opacity > 1.0) g.opacity = 1.0;
  g.pen_color = get_color(path, L"pen_color", g.pen_color);
  g.custom_line_color = get_color(path, L"custom_line_color",
                                  g.custom_line_color);
  g.fill_color = get_color(path, L"fill_color", g.fill_color);
  g.custom_fill_color = get_color(path, L"custom_fill_color",
                                  g.custom_fill_color);
  g.fill_enabled = (int) get_int(path, L"fill_enabled", 0);
  g.marker_opacity = get_double(path, L"marker_opacity", 0.4);
  if (g.marker_opacity < 0.1 || g.marker_opacity > 1.0) g.marker_opacity = 0.4;

  get_string(path, L"font", wbuf, MAX_PATH, L"Arial Bold 24");
  { char *s = utf8_from_wide(wbuf);
    if (s) { strncpy(g.font, s, sizeof(g.font) - 1); g.font[sizeof(g.font)-1] = 0; free(s); } }
  if (!g.font[0]) strcpy(g.font, "Arial Bold 24");

  g.icon_size = get_int(path, L"icon_size", 48);
  if (g.icon_size != 16 && g.icon_size != 24 && g.icon_size != 32 &&
      g.icon_size != 48 && g.icon_size != 64 && g.icon_size != 128 &&
      g.icon_size != 256)
    g.icon_size = 48;
  g.icon_position = (IconPos) get_int(path, L"icon_position", ICON_TR);
  if (g.icon_position > ICON_BL) g.icon_position = ICON_TR;
  g.show_icon = (int) get_int(path, L"show_icon", 1);
  g.show_indicator = (int) get_int(path, L"show_indicator", 1);
  g.show_toolbar = (int) get_int(path, L"show_toolbar", 1);
  g.toolbar_vertical = (int) get_int(path, L"toolbar_vertical", 0);
  g.toolbar_icon_size = (int) get_int(path, L"toolbar_icon_size", 32);
  if (g.toolbar_icon_size < 16 || g.toolbar_icon_size > 48)
    g.toolbar_icon_size = 32;
  g.paper_size_kind = (int) get_int(path, L"paper_size", PAPER_SIZE_SCREEN);
  if (g.paper_size_kind < PAPER_SIZE_SCREEN ||
      g.paper_size_kind > PAPER_SIZE_CUSTOM)
    g.paper_size_kind = PAPER_SIZE_SCREEN;
  g.paper_custom_w_mm = (int) get_int(path, L"paper_custom_w", 210);
  g.paper_custom_h_mm = (int) get_int(path, L"paper_custom_h", 297);
  if (g.paper_custom_w_mm < 10) g.paper_custom_w_mm = 210;
  if (g.paper_custom_h_mm < 10) g.paper_custom_h_mm = 297;
  if (g.paper_custom_w_mm > 2000) g.paper_custom_w_mm = 2000;
  if (g.paper_custom_h_mm > 2000) g.paper_custom_h_mm = 2000;
  g.toolbar_x = (int) get_int(path, L"toolbar_x", 40);
  g.toolbar_y = (int) get_int(path, L"toolbar_y", 40);
  {
    wchar_t tbuf[TOOLBAR_ITEMS_MAX];
    wchar_t tdef[TOOLBAR_ITEMS_MAX];
    MultiByteToWideChar(CP_UTF8, 0, g.toolbar_items, -1, tdef,
                        TOOLBAR_ITEMS_MAX);
    GetPrivateProfileStringW(L"screenator", L"toolbar_items", tdef, tbuf,
                             TOOLBAR_ITEMS_MAX, path);
    char *s = utf8_from_wide(tbuf);
    if (s)
      {
        strncpy(g.toolbar_items, s, sizeof(g.toolbar_items) - 1);
        g.toolbar_items[sizeof(g.toolbar_items) - 1] = 0;
        free(s);
      }

    MultiByteToWideChar(CP_UTF8, 0, g.toolbar_order, -1, tdef,
                        TOOLBAR_ITEMS_MAX);
    GetPrivateProfileStringW(L"screenator", L"toolbar_order", tdef, tbuf,
                             TOOLBAR_ITEMS_MAX, path);
    s = utf8_from_wide(tbuf);
    if (s)
      {
        strncpy(g.toolbar_order, s, sizeof(g.toolbar_order) - 1);
        g.toolbar_order[sizeof(g.toolbar_order) - 1] = 0;
        free(s);
      }
  }
  if (strstr(g.toolbar_items, "page_prev") ||
      strstr(g.toolbar_items, "screenshot") ||
      strstr(g.toolbar_items, "save_paper") ||
      strstr(g.toolbar_items, "floating_icon"))
    {
      strncpy(g.toolbar_items, TOOLBAR_DEFAULT_ITEMS,
              sizeof(g.toolbar_items) - 1);
      g.toolbar_items[sizeof(g.toolbar_items) - 1] = 0;
    }
  if (strstr(g.toolbar_order, "page_prev") ||
      strstr(g.toolbar_order, "screenshot") ||
      strstr(g.toolbar_order, "save_paper") ||
      strstr(g.toolbar_order, "floating_icon"))
    {
      strncpy(g.toolbar_order, TOOLBAR_DEFAULT_ITEMS,
              sizeof(g.toolbar_order) - 1);
      g.toolbar_order[sizeof(g.toolbar_order) - 1] = 0;
    }
  g.save_on_exit = (int) get_int(path, L"save_annotations", 1);
  g.paper_continuous_scroll =
    (int) get_int(path, L"paper_continuous_scroll", 1);
  g.hide_hints = (int) get_int(path, L"hide_hints", 0);
  g.wheel_mode = (WheelMode) get_int(path, L"wheel_mode", 0);
  g.stylus_mode = (StylusMode) get_int(path, L"stylus_mode", 0);
  if (g.wheel_mode > WHEEL_THICKNESS) g.wheel_mode = WHEEL_COLOR;
  if (g.stylus_mode > STYLUS_THICKNESS) g.stylus_mode = STYLUS_COLOR;

  config_shortcuts_load(path);

  /* Start on the desktop with a fresh Paper session. Per-file PDF positions
   * are loaded separately. The activation delay also starts at zero. */
  g.paper = PAPER_NONE;
  g.tool_delay_seconds = 0;
}

void config_load(void)
{
  config_defaults();
  get_app_paths();
  config_load_from(g.config_path);
  paper_config_load();
}

void config_save(void)
{
  char hex[32];

  make_app_dir();

  {
    wchar_t num[32];
    _snwprintf(num, 32, L"%u", g.pen_width);
    WritePrivateProfileStringW(L"screenator", L"pen_width", num, g.config_path);
    _snwprintf(num, 32, L"%u", g.eraser_width);
    WritePrivateProfileStringW(L"screenator", L"eraser_width", num, g.config_path);
    _snwprintf(num, 32, L"%.17g", g.opacity);
    WritePrivateProfileStringW(L"screenator", L"opacity", num, g.config_path);
    _snwprintf(num, 32, L"%.17g", g.marker_opacity);
    WritePrivateProfileStringW(L"screenator", L"marker_opacity", num, g.config_path);
    _snwprintf(num, 32, L"%d", g.fill_enabled);
    WritePrivateProfileStringW(L"screenator", L"fill_enabled", num, g.config_path);
    _snwprintf(num, 32, L"%u", g.icon_size);
    WritePrivateProfileStringW(L"screenator", L"icon_size", num, g.config_path);
    _snwprintf(num, 32, L"%d", (int) g.icon_position);
    WritePrivateProfileStringW(L"screenator", L"icon_position", num, g.config_path);
    _snwprintf(num, 32, L"%d", g.show_icon);
    WritePrivateProfileStringW(L"screenator", L"show_icon", num, g.config_path);
    _snwprintf(num, 32, L"%d", g.show_indicator);
    WritePrivateProfileStringW(L"screenator", L"show_indicator", num, g.config_path);
    _snwprintf(num, 32, L"%d", g.show_toolbar);
    WritePrivateProfileStringW(L"screenator", L"show_toolbar", num, g.config_path);
    _snwprintf(num, 32, L"%d", g.toolbar_vertical);
    WritePrivateProfileStringW(L"screenator", L"toolbar_vertical", num, g.config_path);
    _snwprintf(num, 32, L"%d", g.paper_size_kind);
    WritePrivateProfileStringW(L"screenator", L"paper_size", num, g.config_path);
    _snwprintf(num, 32, L"%d", g.paper_custom_w_mm);
    WritePrivateProfileStringW(L"screenator", L"paper_custom_w", num, g.config_path);
    _snwprintf(num, 32, L"%d", g.paper_custom_h_mm);
    WritePrivateProfileStringW(L"screenator", L"paper_custom_h", num, g.config_path);
    _snwprintf(num, 32, L"%d", g.toolbar_icon_size);
    WritePrivateProfileStringW(L"screenator", L"toolbar_icon_size", num, g.config_path);
    _snwprintf(num, 32, L"%d", g.toolbar_x);
    WritePrivateProfileStringW(L"screenator", L"toolbar_x", num, g.config_path);
    _snwprintf(num, 32, L"%d", g.toolbar_y);
    WritePrivateProfileStringW(L"screenator", L"toolbar_y", num, g.config_path);

    wchar_t *items = wide_from_utf8(g.toolbar_items[0] ? g.toolbar_items
                                                       : "none");
    if (items)
      {
        WritePrivateProfileStringW(L"screenator", L"toolbar_items", items,
                                   g.config_path);
        free(items);
      }
    wchar_t *order = wide_from_utf8(g.toolbar_order[0] ? g.toolbar_order
                                                       : TOOLBAR_DEFAULT_ITEMS);
    if (order)
      {
        WritePrivateProfileStringW(L"screenator", L"toolbar_order", order,
                                   g.config_path);
        free(order);
      }
    _snwprintf(num, 32, L"%d", g.save_on_exit);
    WritePrivateProfileStringW(L"screenator", L"save_annotations", num, g.config_path);
    _snwprintf(num, 32, L"%d", g.paper_continuous_scroll);
    WritePrivateProfileStringW(L"screenator", L"paper_continuous_scroll", num,
                               g.config_path);
    _snwprintf(num, 32, L"%d", g.hide_hints);
    WritePrivateProfileStringW(L"screenator", L"hide_hints", num, g.config_path);
    _snwprintf(num, 32, L"%d", (int) g.wheel_mode);
    WritePrivateProfileStringW(L"screenator", L"wheel_mode", num, g.config_path);
    _snwprintf(num, 32, L"%d", (int) g.stylus_mode);
    WritePrivateProfileStringW(L"screenator", L"stylus_mode", num, g.config_path);
  }

  color_to_hex(g.pen_color, hex, sizeof(hex));
  { wchar_t *w = wide_from_utf8(hex);
    WritePrivateProfileStringW(L"screenator", L"pen_color", w, g.config_path); free(w); }
  color_to_hex(g.custom_line_color, hex, sizeof(hex));
  { wchar_t *w = wide_from_utf8(hex);
    WritePrivateProfileStringW(L"screenator", L"custom_line_color", w, g.config_path); free(w); }
  color_to_hex(g.fill_color, hex, sizeof(hex));
  { wchar_t *w = wide_from_utf8(hex);
    WritePrivateProfileStringW(L"screenator", L"fill_color", w, g.config_path); free(w); }
  color_to_hex(g.custom_fill_color, hex, sizeof(hex));
  { wchar_t *w = wide_from_utf8(hex);
    WritePrivateProfileStringW(L"screenator", L"custom_fill_color", w, g.config_path); free(w); }

  { wchar_t *w = wide_from_utf8(g.font);
    WritePrivateProfileStringW(L"screenator", L"font", w, g.config_path); free(w); }

  config_shortcuts_save();
  paper_config_save();

  WritePrivateProfileStringW(NULL, NULL, NULL, NULL);
}

void annotations_save(void)
{
  cairo_surface_t *src = paper_screen_annotations();
  cairo_surface_t *tmp = NULL;
  if (!src) return;
  make_app_dir();
  char *path = utf8_from_wide(g.annotations_path);
  if (path)
    {
      int sw = cairo_image_surface_get_width(src);
      int sh = cairo_image_surface_get_height(src);
      if (sw != g.vw || sh != g.vh)
        {
          /* The paper canvas may be taller than the screen; save just the
           * screen-sized annotation layer. */
          tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, g.vw, g.vh);
          cairo_t *cr = cairo_create(tmp);
          cairo_set_source_surface(cr, src, 0, 0);
          cairo_paint(cr);
          cairo_destroy(cr);
          src = tmp;
        }
      cairo_surface_flush(src);
      cairo_surface_write_to_png(src, path);
      if (tmp) cairo_surface_destroy(tmp);
      free(path);
    }
}

void annotations_load(void)
{
  if (!g.backbuf) return;
  char *path = utf8_from_wide(g.annotations_path);
  if (!path) return;

  cairo_surface_t *img = cairo_image_surface_create_from_png(path);
  free(path);
  if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy(img);
      return;
    }

  cairo_t *cr = cairo_create(g.backbuf);
  int w = cairo_image_surface_get_width(img);
  int h = cairo_image_surface_get_height(img);
  if (w != g.vw || h != g.vh)
    {
      cairo_scale(cr, (double) g.vw / (double) w,
                  (double) g.vh / (double) h);
    }
  cairo_set_source_surface(cr, img, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(img);
}

void annotations_remove(void)
{
  DeleteFileW(g.annotations_path);
}

void config_remove_dir(void)
{
  wchar_t path[MAX_PATH + 2];
  _snwprintf(path, MAX_PATH, L"%s", g.config_dir);
  path[wcslen(path) + 1] = 0;

  SHFILEOPSTRUCTW op;
  memset(&op, 0, sizeof(op));
  op.wFunc = FO_DELETE;
  op.pFrom = path;
  op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_SILENT;
  SHFileOperationW(&op);

  /* Remove legacy flat files from older builds. */
  wchar_t appdata[MAX_PATH] = L"";
  if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata)))
    {
      wchar_t old[MAX_PATH];
      _snwprintf(old, MAX_PATH, L"%s\\screenator.ini", appdata);
      DeleteFileW(old);
      _snwprintf(old, MAX_PATH, L"%s\\screenator-annotations.png", appdata);
      DeleteFileW(old);
    }
}
