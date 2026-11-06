/*
 * Screenator for Windows -- settings, annotations and paths.
 * GPL-2.0-or-later.  See app.h.
 */

#include "app.h"

#include <wchar.h>

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
  g.save_on_exit = 1;
  g.hide_hints = 0;
  g.wheel_mode = WHEEL_COLOR;
  g.stylus_mode = STYLUS_COLOR;
  g.tool = TOOL_NONE;
  g.pending_tool = TOOL_NONE;
  g.tool_delay_seconds = 0;
  g.paper = PAPER_NONE;
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

  _snwprintf(g.config_path, MAX_PATH, L"%s\\Screenator", appdata);
  SHCreateDirectoryExW(NULL, g.config_path, NULL);
  _snwprintf(g.config_path, MAX_PATH, L"%s\\Screenator\\screenator.ini", appdata);
  _snwprintf(g.annotations_path, MAX_PATH,
             L"%s\\Screenator\\screenator-annotations.png", appdata);
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
  g.save_on_exit = (int) get_int(path, L"save_annotations", 1);
  g.hide_hints = (int) get_int(path, L"hide_hints", 0);
  g.wheel_mode = (WheelMode) get_int(path, L"wheel_mode", 0);
  g.stylus_mode = (StylusMode) get_int(path, L"stylus_mode", 0);
  if (g.wheel_mode > WHEEL_THICKNESS) g.wheel_mode = WHEEL_COLOR;
  if (g.stylus_mode > STYLUS_THICKNESS) g.stylus_mode = STYLUS_COLOR;

  /* The screen paper and the activation delay are deliberately not restored:
   * Screenator always starts on the normal desktop with no delay. */
  g.paper = PAPER_NONE;
  g.tool_delay_seconds = 0;
}

void config_load(void)
{
  config_defaults();
  get_app_paths();
  config_load_from(g.config_path);
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
    _snwprintf(num, 32, L"%d", g.save_on_exit);
    WritePrivateProfileStringW(L"screenator", L"save_annotations", num, g.config_path);
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

  WritePrivateProfileStringW(NULL, NULL, NULL, NULL);
}

void annotations_save(void)
{
  if (!g.backbuf) return;
  make_app_dir();
  char *path = utf8_from_wide(g.annotations_path);
  if (path)
    {
      cairo_surface_flush(g.backbuf);
      cairo_surface_write_to_png(g.backbuf, path);
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
