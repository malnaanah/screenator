/*
 * Screenator for Windows -- a screen annotation tool for the Windows desktop,
 * based on Screenator 6.0 (GPL-2.0-or-later) by Mahmoud Alnaanah, which is
 * itself based on Gromit-MPX 1.4.2 and Gromit.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef APP_H
#define APP_H

#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#define _USE_MATH_DEFINES
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_NAME     "Screenator"
#define APP_VERSION  "7.0"
#define APP_TITLE    "Screenator 7.0"
#define MAX_UNDO     4
#define FRAME_MIN_SIZE 8.0
#define LINE_HEADS_HANDLE_OFFSET 28.0

/* Timer ids. */
#define TIMER_TOOL_DELAY   1
#define TIMER_LONG_PRESS   2
#define TIMER_PRESENT      3
#define TIMER_CARET        7
#define TIMER_STYLUS_HOLD  8
#define TIMER_PEN_POLL     9

/* Hotkey ids. */
#define HK_PEN       100
#define HK_TEXT      101
#define HK_ARROW     102
#define HK_ICON      103
#define HK_QUICK     104
#define HK_ESC       105
#define HK_UNDO      106
#define HK_REDO      107
#define HK_PAPER_Q   108
#define HK_PAPER_T   109

/* Menu command ids. */
enum
{
  CMD_PEN = 1000, CMD_TEXT, CMD_ERASER,
  CMD_MARKER, CMD_LINE, CMD_SPLINE, CMD_POLYGON, CMD_TRIANGLE,
  CMD_ARROW, CMD_ELLIPSE, CMD_RRECT, CMD_BUCKET, CMD_MATH, CMD_BARS,
  CMD_RANDOM,
  CMD_DELAY_0, CMD_DELAY_1, CMD_DELAY_2, CMD_DELAY_3, CMD_DELAY_4,
  CMD_DELAY_5, CMD_DELAY_7, CMD_DELAY_9, CMD_DELAY_11,
  CMD_WIDTH_1, CMD_WIDTH_2, CMD_WIDTH_3, CMD_WIDTH_4, CMD_WIDTH_5,
  CMD_WIDTH_6, CMD_WIDTH_12, CMD_WIDTH_24, CMD_WIDTH_48,
  CMD_COLOR_BASE,              /* +8 presets */
  CMD_COLOR_CUSTOM = CMD_COLOR_BASE + 8,
  CMD_FILL_BASE,               /* +9 presets, first is None */
  CMD_FILL_CUSTOM = CMD_FILL_BASE + 9,
  CMD_PAPER_NONE, CMD_PAPER_PLAIN, CMD_PAPER_RULED, CMD_PAPER_GRAPH,
  CMD_ERASER_20, CMD_ERASER_40, CMD_ERASER_60, CMD_ERASER_120,
  CMD_OPACITY_10, CMD_OPACITY_20, CMD_OPACITY_30, CMD_OPACITY_40,
  CMD_OPACITY_50, CMD_OPACITY_60, CMD_OPACITY_70, CMD_OPACITY_80,
  CMD_OPACITY_90,
  CMD_ICON_16, CMD_ICON_24, CMD_ICON_32, CMD_ICON_48, CMD_ICON_64,
  CMD_ICON_128, CMD_ICON_256,
  CMD_UNDO, CMD_REDO, CMD_CLEAR, CMD_VISIBILITY,
  CMD_SHOW_ICON, CMD_SHOW_HINTS,
  CMD_SAVE_ON_EXIT, CMD_FONT, CMD_SHOW_INDICATOR, CMD_ICON_POS,
  CMD_RESET, CMD_ABOUT, CMD_QUIT,
  CMD_WHEEL_COLOR, CMD_WHEEL_THICKNESS,
  CMD_STYLUS_COLOR, CMD_STYLUS_THICKNESS,
  CMD_MARKER_10, CMD_MARKER_20, CMD_MARKER_30, CMD_MARKER_40,
  CMD_MARKER_50, CMD_MARKER_60, CMD_MARKER_70, CMD_MARKER_80,
  CMD_MARKER_90
};

typedef struct { double r, g, b, a; } Color;

typedef struct { double x, y; } Pt;

typedef enum
{
  PAPER_NONE = 0, PAPER_PLAIN, PAPER_RULED, PAPER_GRAPH
} Paper;

typedef enum
{
  TOOL_NONE = 0,
  TOOL_PEN, TOOL_TEXT, TOOL_ERASER,
  TOOL_MARKER, TOOL_LINE, TOOL_SPLINE, TOOL_POLYGON, TOOL_TRIANGLE,
  TOOL_ARROW, TOOL_ELLIPSE, TOOL_RRECT, TOOL_BUCKET, TOOL_MATH,
  TOOL_BARS, TOOL_RANDOM
} Tool;

typedef enum
{
  SHAPE_ELLIPSE, SHAPE_RRECT, SHAPE_MATH, SHAPE_BARS, SHAPE_RANDOM,
  SHAPE_TEXT
} ShapeKind;

typedef enum { ARROW_NONE = 0, ARROW_START, ARROW_END, ARROW_BOTH } ArrowHeads;
typedef enum { BARS_NONE = 0, BARS_TOP, BARS_BOTTOM, BARS_BOTH } BarsArrows;
typedef enum { ICON_TR = 0, ICON_TL, ICON_BR, ICON_BL } IconPos;
typedef enum { WHEEL_COLOR = 0, WHEEL_THICKNESS } WheelMode;
typedef enum { STYLUS_COLOR = 0, STYLUS_THICKNESS } StylusMode;

/* Spline node: an on-curve point and its two absolute control points. */
typedef struct
{
  Pt p, in, out;
  int manual;
} SplineNode;

/* Polygon (called the star internally in Screenator).  inner == 1.0 draws a
 * regular polygon. */
typedef struct
{
  double cx, cy, radius, inner, rotation;
  int points;
} StarShape;

typedef struct { Pt v[3]; } Triangle;

typedef struct
{
  int type;                    /* TOOL_LINE or TOOL_ARROW */
  double x0, y0, x1, y1;
  ArrowHeads heads;
} LineShape;

typedef struct
{
  double cx, cy, w, h, rotation;
} Frame;

typedef struct
{
  ShapeKind type;
  Frame frame;
  double extra;                /* rounded rectangle corner radius */
  int count;                   /* parallel lines */
  BarsArrows arrows;
  unsigned seed;
  char *expr;
  double xmin, xmax;
  int polar;
  char *text;
  char *font;
  double font_size;
  int text_off_x, text_off_y;
  Color color;
} FrameShape;

/* Handles used by the editable frame shapes. */
typedef enum
{
  FH_NONE = 0, FH_MOVE, FH_TL, FH_TR, FH_BL, FH_BR, FH_ROTATE, FH_EXTRA,
  FH_ARROWS
} FrameHandle;

/* What is currently being edited. */
typedef enum
{
  EDIT_NONE = 0, EDIT_LINE, EDIT_SPLINE, EDIT_POLYGON, EDIT_TRIANGLE,
  EDIT_FRAME
} EditKind;

/* Line/arrow handles. */
typedef enum
{
  LH_NONE = 0, LH_START, LH_END, LH_HEADS, LH_MOVE
} LineHandle;

/* Polygon handles. */
typedef enum
{
  PH_NONE = 0, PH_RADIUS, PH_INNER, PH_ROTATION, PH_POINTS, PH_CENTRE
} PolygonHandle;

/* Triangle handles. */
typedef enum
{
  TH_NONE = 0, TH_V0, TH_V1, TH_V2, TH_CENTRE
} TriangleHandle;

typedef struct
{
  HINSTANCE hinst;
  HMODULE richedit;

  HWND overlay, iconwin, msgwin, textwin, textedit;
  HWND inputwin;
  HWND hintsdlg, mathdlg, aboutdlg, posdlg, quickwnd;
  HWND quick_owner;

  int vx, vy, vw, vh;                  /* virtual screen, physical pixels */
  int dpi;

  /* Annotation buffer and the layered window's DIB. */
  cairo_surface_t *backbuf;
  cairo_surface_t *present;
  cairo_surface_t *strokebuf;
  HDC dibdc;
  HBITMAP dibbmp;
  void *dibbits;
  int dibw, dibh;

  cairo_surface_t *undo[MAX_UNDO];
  cairo_surface_t *redo[MAX_UNDO];
  int undo_head, undo_depth, redo_head, redo_depth;

  /* Settings. */
  Tool tool;
  Tool pending_tool;
  int tool_delay_deferred;
  unsigned tool_delay_seconds;
  double opacity;
  unsigned pen_width;
  unsigned eraser_width;
  Color pen_color;
  Color custom_line_color;
  Color marker_saved_color;
  int marker_saved_valid;
  double marker_opacity;
  Color fill_color;
  Color custom_fill_color;
  int fill_enabled;
  char font[128];
  unsigned icon_size;
  IconPos icon_position;
  int show_icon;
  int show_indicator;
  int save_on_exit;
  int hide_hints;
  int hide_drawings;
  Paper paper;
  WheelMode wheel_mode;
  StylusMode stylus_mode;
  int autostart;

  /* Config paths. */
  wchar_t config_path[MAX_PATH];
  wchar_t annotations_path[MAX_PATH];

  /* Floating icon. */
  int icon_drag_started;
  Pt icon_press;
  RECT icon_drag_pos;
  int icon_menu_open;

  /* Tray. */
  NOTIFYICONDATAW nid;
  int tray_added;
  HICON hicon_normal, hicon_active;
  HBITMAP menu_icons[32];
  int menu_icon_count;

  /* Input / painting state. */
  Pt last;
  Pt down;
  int button_down;
  int middle_erasing;
  int pen_drawing;
  int stroke_buffer_active;
  int marker_active;
  int right_timer_active;
  int right_menu_open;
  int right_short_done;
  int right_start_pending;
  int right_started_edit;
  int menu_hold_is_stylus;
  Pt right_down;
  int stylus_eraser_hold;
  int pen_seen;
  int pen_pointer_active;
  int pen_barrel_down;
  int pen_barrel_eraser;
  int pen_first_source;           /* 1 = middle mouse, 2 = pointer button */
  ULONGLONG pen_first_begin;
  int pen_eraser_down;
  int pen_second_down;
  unsigned pen_id;
  Pt pen_last;
  ULONGLONG pen_barrel_release;
  ULONGLONG pen_eraser_release;
  int suppress_mouse_until;

  /* Current editing shape. */
  EditKind edit;
  LineShape line;
  StarShape poly;
  Triangle tri;
  FrameShape frame;
  SplineNode *nodes;
  int nnodes, nodecap;
  Pt spline_cursor;
  int spline_finish_pending;
  int spline_preview_on;
  int poly_placing;
  int tri_placing;                /* 1 = first vertex set, 2 = second set */
  int handle;
  int drag_started;
  int drag_new_frame;
  Pt press;
  double press_cx, press_cy, press_w, press_h, press_rot, press_font_size;
  double press_distance;
  int press_index;
  unsigned frame_seed;

  /* Pen smoothing. */
  Pt smooth_last;
  int smooth_first;

  /* Text editor. */
  int text_editor_active;
  int text_editor_rtl;
  int text_caret_visible;
  int text_editor_click_x, text_editor_click_y;
  WNDPROC edit_oldproc;
  HFONT editor_font;

  /* Math settings. */
  int math_polar;
  int math_ready;
  char math_expr[256];
  double math_xmin, math_xmax;
  char math_polar_expr[256];
  double math_tmin, math_tmax;

  /* Hints / dialogs. */
  int hints_shown;
  int modal_depth;
  HWND modal_dialog;
  int modal_outside_close;

  /* Screen paper focus. */
  HWND paper_prev_focus;
  int paper_has_focus;

  /* Quick menu. */
  int quick_sel;

  /* Cursors. */
  HCURSOR cur_pen, cur_erase, cur_text, cur_bucket;
  int cur_erase_size;

  /* Misc. */
  int shutting_down;
  int smoke;
  int debug;
  int exit_code;
  int render_failures;
  int icon_update_failures;
  int display_valid;
  ULONGLONG last_present;
  int render_pending;
  int menu_open;
  int overlay_activate_pending;
  cairo_surface_t *menu_capture;
  RECT menu_capture_rect;
  int menu_capture_kind;
  int menu_capture_marker;
  cairo_surface_t *menu_ghost;
  RECT menu_ghost_rect;
  int menu_ghost_active;
  int menu_ghost_kind;
  int menu_ghost_marker;
  int ignore_next_right_up;
  int ctrl_down, shift_down;
} App;

extern App g;

/* Tool presets shared by the menus and the mouse wheel. */
extern const unsigned g_widths[9];
extern const unsigned g_eraser_widths[4];
extern const unsigned g_delays[9];
typedef struct { const char *label; const char *hex; } NamedColor;
extern const NamedColor g_line_colors[8];
extern const NamedColor g_fill_colors[9];

int  line_color_index(void);
int  fill_color_index(void);
void apply_line_color_index(int i);
void apply_fill_color_index(int i);
void apply_pen_width(unsigned w);
void apply_eraser_width(unsigned w);
void step_line_color(int dir);
void step_fill_color(int dir);
void step_pen_width(int dir);
void step_eraser_width(int dir);
void tools_finish_stroke(void);

/* ---- config.c ---- */
void config_load(void);
void config_load_from(const wchar_t *path);
void config_save(void);
void annotations_save(void);
void annotations_load(void);
void annotations_remove(void);
void config_defaults(void);

/* ---- draw.c ---- */
Color color_rgb(double r, double g, double b);
Color color_from_hex(const char *hex, int *ok);
void color_to_hex(Color c, char *buf, int size);
void set_source_color(cairo_t *cr, Color c);

double font_pixel_size(const char *font);
char *font_make(const char *family, int bold, int italic, double px);
void measure_frame_text(FrameShape *shape);

void draw_paper(cairo_t *cr, Paper paper, int w, int h);
void draw_backbuffer(cairo_t *cr);
void draw_preview(cairo_t *cr);
void draw_frame_shape(cairo_t *cr, const FrameShape *shape);
void draw_line_shape(cairo_t *cr, const LineShape *line, unsigned width,
                     Color color);
void draw_spline_shape(cairo_t *cr, const SplineNode *nodes, int count,
                       unsigned width, Color color);
void draw_polygon_shape(cairo_t *cr, const StarShape *poly, unsigned width,
                        Color color);
void draw_triangle_shape(cairo_t *cr, const Triangle *tri, unsigned width,
                         Color color);
void draw_handles(cairo_t *cr);
void draw_text_editor(cairo_t *cr);

void spline_update_auto(SplineNode *nodes, int count);
void spline_bounds(const SplineNode *nodes, int count, RECT *r);
void polygon_vertices(const StarShape *poly, Pt *out, int *count);
void frame_local_to_screen(const Frame *f, double lx, double ly, double *x,
                           double *y);
void frame_screen_to_local(const Frame *f, double x, double y, double *lx,
                           double *ly);
void frame_handle_pos(const FrameShape *shape, FrameHandle h, Pt *p);
int frame_hit(const FrameShape *shape, Pt p, FrameHandle *handle);
void math_plot_path(cairo_t *cr, const FrameShape *shape, int w, int h);

/* ---- math.c ---- */
typedef struct MathParser MathParser;
MathParser *math_parse(const char *expr, const char **error);
void math_free(MathParser *m);
int math_eval(MathParser *m, double x, double *out);
int math_delta_roots(MathParser *m, double lo, double hi, int polar,
                     double *roots, int max);
int math_validate(const char *expr, const char **error);

/* ---- tools.c ---- */
void tools_activate(Tool t);          /* immediate */
void tools_request(Tool t);           /* honours the activation delay */
void tools_exit(void);
void tools_commit(int cancel);
void tools_commit_all(void);
void tools_pointer_down(Pt p, int button, int ctrl, int shift);
void tools_pointer_move(Pt p, int ctrl, int shift);
void tools_pointer_up(Pt p, int ctrl, int shift);
void tools_double_click(Pt p);
void tools_right_short(Pt p);
void tools_wheel(int delta);
void tools_key(int vk, int ctrl, int shift);
int  tools_editing(void);
void tools_clear(void);
void bucket_fill(Pt p);
void tools_erase_start(Pt p);
void tools_erase_move(Pt p);
void tools_erase_end(void);
void tools_spline_hover(Pt p);
void tools_triangle_hover(Pt p);
void tools_undo(void);
void tools_redo(void);
void tools_push_undo(void);
void tools_set_frame_color(void);
void tools_text_commit(const char *text, int cancelled);

/* ---- ui.c ---- */
void ui_show_menu(int x, int y);
void ui_menu_finish(void);
int  ui_modal_outside_click(Pt p);
void ui_show_wheel_menu(int marker);
void ui_show_stylus_menu(void);
void ui_refresh_tray(void);
void ui_update_icon(void);
void ui_set_icon_visible(int show);
void ui_position_icon(void);
void ui_show_hints(int startup);
void ui_show_math_dialog(void);
void ui_show_about(void);
void ui_show_icon_position(void);
void ui_choose_color(int fill);
void ui_choose_font(void);
void ui_quick_menu(void);
void ui_layout(void);
void ui_rebuild_surfaces(void);
void ui_apply_pen(void);
void ui_set_paper(Paper p);
void ui_toggle_visibility(void);
void ui_reset_settings(void);
void ui_message(const char *text, const char *title, UINT flags);
int  ui_is_modal(void);
HBITMAP ui_load_icon_bitmap(const char *name, int size);
void ui_init(void);
int  ui_selfcheck(void);
int  ui_dialog_selfcheck(void);
void app_exe_dir(wchar_t *buf, int cap);
void app_asset_path(wchar_t *buf, int cap, const wchar_t *name);
void app_force_foreground(HWND hwnd);
void dlog(const char *fmt, ...);
void app_menu_capture_start(int kind, int marker);
void app_menu_capture_stop(int keep);
void app_clear_menu_ghost(void);

/* ---- main.c ---- */
void app_render(int x, int y, int w, int h);
void app_render_all(void);
void app_request_render(void);
void app_set_click_through(int on);
void app_update_cursor(void);
void app_activate_overlay_input(void);
void app_deactivate_overlay_input(void);
void app_start_right_press(Pt p);
void app_update_tray_icon(void);
void app_raise_icon(void);
void app_set_paper(Paper p, int grab_focus);
void app_exit_paper(void);
void app_save_and_quit(void);
void app_status(const char *text);
void app_begin_text(Pt p);
void app_end_text(int commit);
void app_refresh_icon_image(void);
void app_show_menu_at_icon(void);
void app_text_editor_place(void);
void app_update_editor_font(void);
void app_install_wheel_hook(void);
void app_remove_wheel_hook(void);

#endif
