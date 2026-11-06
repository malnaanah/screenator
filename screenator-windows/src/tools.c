/*
 * Screenator for Windows -- tools, shape editing, undo and the paint bucket.
 * GPL-2.0-or-later.  See app.h.
 */

#include "app.h"

const unsigned g_widths[9] = { 1, 2, 3, 4, 5, 6, 12, 24, 48 };
const unsigned g_eraser_widths[4] = { 20, 40, 60, 120 };
const unsigned g_delays[9] = { 0, 1, 2, 3, 4, 5, 7, 9, 11 };

const NamedColor g_line_colors[8] =
{
  { "Red", "#FF0000" }, { "Green", "#00AA00" }, { "Blue", "#0000FF" },
  { "Yellow", "#FFFF00" }, { "Black", "#000000" }, { "White", "#FFFFFF" },
  { "Orange", "#FF8000" }, { "Gray", "#808080" }
};

const NamedColor g_fill_colors[9] =
{
  { "None", "none" }, { "Green", "#00AA00" }, { "Blue", "#0000FF" },
  { "Yellow", "#FFFF00" }, { "Black", "#000000" }, { "White", "#FFFFFF" },
  { "Red", "#FF0000" }, { "Orange", "#FF8000" }, { "Gray", "#808080" }
};

static double angle0;
static double line_press[4];
static double tri_press[6];
static int    spline_drag_out;

/* ------------------------------------------------------------------ */
/* small helpers                                                      */
/* ------------------------------------------------------------------ */

static int same_rgb(Color a, Color b)
{
  return fabs(a.r - b.r) < 0.002 && fabs(a.g - b.g) < 0.002 &&
         fabs(a.b - b.b) < 0.002;
}

int line_color_index(void)
{
  for (int i = 0; i < 8; i++)
    {
      Color c = color_from_hex(g_line_colors[i].hex, NULL);
      if (same_rgb(c, g.pen_color)) return i;
    }
  return 8;                     /* custom */
}

int fill_color_index(void)
{
  if (!g.fill_enabled) return 0;
  for (int i = 1; i < 9; i++)
    {
      Color c = color_from_hex(g_fill_colors[i].hex, NULL);
      if (same_rgb(c, g.fill_color)) return i;
    }
  return 9;                     /* custom */
}

static void refresh_all(void)
{
  ui_apply_pen();
  ui_refresh_tray();
  ui_update_icon();
  app_request_render();
}

void apply_line_color_index(int i)
{
  if (i < 8) g.pen_color = color_from_hex(g_line_colors[i].hex, NULL);
  else g.pen_color = g.custom_line_color;
  g.pen_color.a = g.opacity;
  tools_set_frame_color();
  refresh_all();
}

void apply_fill_color_index(int i)
{
  if (i == 0)
    {
      g.fill_enabled = 0;
    }
  else
    {
      g.fill_enabled = 1;
      g.fill_color = (i <= 8) ? color_from_hex(g_fill_colors[i].hex, NULL)
                              : g.custom_fill_color;
      g.fill_color.a = 1.0;
    }
  refresh_all();
}

void apply_pen_width(unsigned w)
{
  g.pen_width = w;
  refresh_all();
}

void apply_eraser_width(unsigned w)
{
  g.eraser_width = w;
  app_update_cursor();
}

void step_line_color(int dir)
{
  int i = line_color_index();
  int n = 9;
  i = (i + dir + n) % n;
  apply_line_color_index(i);
}

void step_fill_color(int dir)
{
  int i = fill_color_index();
  int n = 10;
  i = (i + dir + n) % n;
  apply_fill_color_index(i);
}

void step_pen_width(int dir)
{
  int idx = 0;
  int best = 100000;
  for (int i = 0; i < 9; i++)
    {
      int d = abs((int) g.pen_width - (int) g_widths[i]);
      if (d < best) { best = d; idx = i; }
    }
  idx = (idx + dir + 9) % 9;
  apply_pen_width(g_widths[idx]);
}

void step_eraser_width(int dir)
{
  int idx = 0;
  for (int i = 0; i < 4; i++)
    if (g.eraser_width == g_eraser_widths[i]) idx = i;
  idx = (idx + dir + 4) % 4;
  apply_eraser_width(g_eraser_widths[idx]);
}

/* ------------------------------------------------------------------ */
/* undo / redo                                                        */
/* ------------------------------------------------------------------ */

static void ensure_surface(cairo_surface_t **s)
{
  if (!*s)
    *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, g.vw, g.vh);
}

static void copy_surface(cairo_surface_t *dst, cairo_surface_t *src)
{
  cairo_t *cr = cairo_create(dst);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_mark_dirty(dst);
}

void tools_push_undo(void)
{
  ensure_surface(&g.undo[g.undo_head]);
  copy_surface(g.undo[g.undo_head], g.backbuf);
  g.undo_head = (g.undo_head + 1) % MAX_UNDO;
  if (g.undo_depth < MAX_UNDO) g.undo_depth++;
  g.redo_depth = 0;
  g.redo_head = 0;
}

void tools_undo(void)
{
  if (g.undo_depth <= 0) return;
  tools_commit(0);

  ensure_surface(&g.redo[g.redo_head]);
  copy_surface(g.redo[g.redo_head], g.backbuf);
  g.redo_head = (g.redo_head + 1) % MAX_UNDO;
  if (g.redo_depth < MAX_UNDO) g.redo_depth++;

  g.undo_head = (g.undo_head - 1 + MAX_UNDO) % MAX_UNDO;
  g.undo_depth--;
  copy_surface(g.backbuf, g.undo[g.undo_head]);
  app_request_render();
  ui_refresh_tray();
}

void tools_redo(void)
{
  if (g.redo_depth <= 0) return;
  tools_commit(0);

  ensure_surface(&g.undo[g.undo_head]);
  copy_surface(g.undo[g.undo_head], g.backbuf);
  g.undo_head = (g.undo_head + 1) % MAX_UNDO;
  if (g.undo_depth < MAX_UNDO) g.undo_depth++;

  g.redo_head = (g.redo_head - 1 + MAX_UNDO) % MAX_UNDO;
  g.redo_depth--;
  copy_surface(g.backbuf, g.redo[g.redo_head]);
  app_request_render();
  ui_refresh_tray();
}

void tools_clear(void)
{
  tools_commit(0);
  tools_push_undo();
  cairo_t *cr = cairo_create(g.backbuf);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_destroy(cr);
  if (!g.smoke) annotations_remove();
  app_request_render();
  ui_refresh_tray();
}

/* ------------------------------------------------------------------ */
/* activation                                                         */
/* ------------------------------------------------------------------ */

static void marker_enter(void)
{
  if (!g.marker_saved_valid)
    {
      g.marker_saved_color = g.pen_color;
      g.marker_saved_valid = 1;
    }
  g.pen_color = color_from_hex("#FFFF00", NULL);
  g.pen_color.a = 1.0;
  g.marker_active = 1;
}

static void marker_leave(void)
{
  g.marker_active = 0;
  if (g.marker_saved_valid)
    {
      g.pen_color = g.marker_saved_color;
      g.pen_color.a = g.opacity;
      g.marker_saved_valid = 0;
    }
}

void tools_activate(Tool t)
{
  dlog("tools_activate %d (current %d)", (int) t, (int) g.tool);
  g.tool_delay_deferred = 0;
  if (t == TOOL_MATH && !g.math_ready)
    {
      ui_show_math_dialog();
      return;
    }

  KillTimer(g.msgwin, TIMER_TOOL_DELAY);
  g.pending_tool = TOOL_NONE;

  if (t == g.tool && g.edit == EDIT_NONE)
    {
      app_activate_overlay_input();
      app_update_cursor();
      ui_update_icon();
      ui_refresh_tray();
      return;
    }

  /* Finish any live stroke while the old tool still owns it, so a marker
   * stroke cannot leave its buffer or drawing state behind for the next
   * tool. */
  tools_finish_stroke();
  tools_commit(0);

  if (t == TOOL_MARKER)
    {
      if (g.tool != TOOL_MARKER) marker_enter();
    }
  else if (g.tool == TOOL_MARKER)
    marker_leave();

  g.tool = t;
  g.handle = 0;
  g.drag_started = 0;
  g.drag_new_frame = 0;
  g.poly_placing = 0;
  g.tri_placing = 0;
  g.stroke_buffer_active = 0;

  app_activate_overlay_input();
  app_install_wheel_hook();
  RegisterHotKey(g.msgwin, HK_ESC, MOD_NOREPEAT, VK_ESCAPE);
  RegisterHotKey(g.msgwin, HK_UNDO, MOD_CONTROL | MOD_NOREPEAT, 'Z');
  RegisterHotKey(g.msgwin, HK_REDO,
                 MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'Z');

  app_update_cursor();
  ui_update_icon();
  ui_refresh_tray();
  app_request_render();
}

void tools_request(Tool t)
{
  if (t == g.tool && g.edit == EDIT_NONE)
    {
      tools_activate(t);
      return;
    }
  if (g.tool_delay_seconds == 0)
    {
      tools_activate(t);
      return;
    }
  KillTimer(g.msgwin, TIMER_TOOL_DELAY);
  g.tool_delay_deferred = 0;
  g.pending_tool = t;
  SetTimer(g.msgwin, TIMER_TOOL_DELAY, g.tool_delay_seconds * 1000, NULL);
}

void tools_exit(void)
{
  dlog("tools_exit");
  /* A captured menu belongs to the active tool session.  Do not let it
   * outlive the tool when Screenator becomes idle again. */
  app_clear_menu_ghost();
  tools_finish_stroke();
  tools_commit(0);
  KillTimer(g.msgwin, TIMER_TOOL_DELAY);
  g.pending_tool = TOOL_NONE;

  if (g.tool == TOOL_MARKER) marker_leave();
  g.tool = TOOL_NONE;
  g.edit = EDIT_NONE;
  g.handle = 0;
  g.drag_started = 0;
  g.drag_new_frame = 0;
  g.pen_drawing = 0;
  g.middle_erasing = 0;
  g.stroke_buffer_active = 0;
  g.spline_finish_pending = 0;
  g.spline_preview_on = 0;
  g.poly_placing = 0;
  g.tri_placing = 0;

  UnregisterHotKey(g.msgwin, HK_ESC);
  UnregisterHotKey(g.msgwin, HK_UNDO);
  UnregisterHotKey(g.msgwin, HK_REDO);

  app_remove_wheel_hook();

  if (g.paper != PAPER_NONE)
    {
      /* The screen paper keeps the undo shortcuts even without a tool. */
      RegisterHotKey(g.msgwin, HK_UNDO, MOD_CONTROL | MOD_NOREPEAT, 'Z');
      RegisterHotKey(g.msgwin, HK_REDO,
                     MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'Z');
    }

  app_deactivate_overlay_input();
  app_update_cursor();
  ui_update_icon();
  ui_refresh_tray();
  app_request_render();
}

int tools_editing(void)
{
  return g.edit != EDIT_NONE;
}

/* ------------------------------------------------------------------ */
/* committing                                                         */
/* ------------------------------------------------------------------ */

void tools_commit(int cancel)
{
  if (g.edit == EDIT_NONE) return;

  Color c = g.pen_color;
  c.a = g.marker_active ? 1.0 : g.opacity;
  int draw_it = 1;

  if (g.edit == EDIT_SPLINE && g.nnodes < 2) draw_it = 0;
  if (g.edit == EDIT_FRAME && g.frame.type == SHAPE_TEXT &&
      (!g.frame.text || !g.frame.text[0])) draw_it = 0;
  if (g.edit == EDIT_TRIANGLE && g.tri_placing) draw_it = 0;
  if (cancel) draw_it = 0;

  if (draw_it)
    {
      tools_push_undo();
      cairo_t *cr = cairo_create(g.backbuf);
      switch (g.edit)
        {
        case EDIT_LINE:
          draw_line_shape(cr, &g.line, g.pen_width, c);
          break;
        case EDIT_SPLINE:
          draw_spline_shape(cr, g.nodes, g.nnodes, g.pen_width, c);
          break;
        case EDIT_POLYGON:
          draw_polygon_shape(cr, &g.poly, g.pen_width, c);
          break;
        case EDIT_TRIANGLE:
          draw_triangle_shape(cr, &g.tri, g.pen_width, c);
          break;
        case EDIT_FRAME:
          draw_frame_shape(cr, &g.frame);
          break;
        default:
          break;
        }
      cairo_destroy(cr);
      cairo_surface_mark_dirty(g.backbuf);
    }

  if (g.edit == EDIT_FRAME && g.frame.type == SHAPE_TEXT)
    {
      free(g.frame.text);
      free(g.frame.font);
    }
  if (g.edit == EDIT_FRAME && g.frame.type == SHAPE_MATH)
    free(g.frame.expr);

  if (g.edit == EDIT_FRAME)
    memset(&g.frame, 0, sizeof(g.frame));

  g.edit = EDIT_NONE;
  g.handle = 0;
  g.drag_started = 0;
  g.drag_new_frame = 0;
  g.spline_finish_pending = 0;
  g.spline_preview_on = 0;
  g.poly_placing = 0;
  g.tri_placing = 0;
  g.nnodes = 0;

  app_request_render();
  ui_refresh_tray();
}

void tools_commit_all(void)
{
  tools_commit(0);
}

void tools_set_frame_color(void)
{
  if (g.edit == EDIT_FRAME)
    {
      g.frame.color = g.pen_color;
      if (g.frame.type != SHAPE_TEXT) g.frame.color.a = g.opacity;
      else g.frame.color.a = 1.0;
      app_request_render();
    }
}

/* ------------------------------------------------------------------ */
/* pen, marker and eraser                                             */
/* ------------------------------------------------------------------ */

static cairo_t *stroke_cr(void)
{
  /* The stroke buffer is allocated on first use and kept for later strokes,
   * so only draw into it while a buffered stroke is actually active. */
  if (g.stroke_buffer_active && g.strokebuf)
    return cairo_create(g.strokebuf);
  return cairo_create(g.backbuf);
}

static void stroke_clear(void)
{
  if (!g.strokebuf) return;
  cairo_t *cr = cairo_create(g.strokebuf);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_destroy(cr);
}

static void pen_begin(Pt p)
{
  tools_push_undo();

  int use_buffer = (g.tool == TOOL_MARKER) || (g.opacity < 0.999);
  g.stroke_buffer_active = use_buffer;
  if (use_buffer)
    {
      ensure_surface(&g.strokebuf);
      stroke_clear();
    }

  g.pen_drawing = 1;
  g.smooth_first = 1;
  g.smooth_last = p;

  cairo_t *cr = stroke_cr();
  Color c = g.pen_color;
  c.a = 1.0;
  set_source_color(cr, c);
  cairo_set_line_width(cr, g.tool == TOOL_MARKER ? 32.0 : g.pen_width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_arc(cr, p.x, p.y, (g.tool == TOOL_MARKER ? 16.0 : g.pen_width / 2.0),
            0, 2 * M_PI);
  cairo_fill(cr);
  cairo_destroy(cr);
}

static void pen_move(Pt p)
{
  if (!g.pen_drawing) return;
  cairo_t *cr = stroke_cr();
  Color c = g.pen_color;
  c.a = 1.0;
  set_source_color(cr, c);
  cairo_set_line_width(cr, g.tool == TOOL_MARKER ? 32.0 : g.pen_width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_move_to(cr, g.smooth_last.x, g.smooth_last.y);
  cairo_line_to(cr, p.x, p.y);
  cairo_stroke(cr);
  cairo_destroy(cr);
  g.smooth_last = p;
  g.smooth_first = 0;
}

static void pen_finish(void)
{
  if (!g.pen_drawing) return;
  g.pen_drawing = 0;

  if (g.stroke_buffer_active && g.strokebuf)
    {
      cairo_t *cr = cairo_create(g.backbuf);
      double alpha = (g.tool == TOOL_MARKER) ? g.marker_opacity : g.opacity;
      cairo_set_source_surface(cr, g.strokebuf, 0, 0);
      cairo_paint_with_alpha(cr, alpha);
      cairo_destroy(cr);
      stroke_clear();
    }
  g.stroke_buffer_active = 0;
  cairo_surface_mark_dirty(g.backbuf);
  app_request_render();
  ui_refresh_tray();
}

static void erase_dot(Pt p)
{
  cairo_t *cr = cairo_create(g.backbuf);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_arc(cr, p.x, p.y, g.eraser_width / 2.0, 0, 2 * M_PI);
  cairo_fill(cr);
  cairo_destroy(cr);
  cairo_surface_mark_dirty(g.backbuf);
  g.smooth_last = p;
}

static void erase_segment(Pt p)
{
  cairo_t *cr = cairo_create(g.backbuf);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_set_line_width(cr, g.eraser_width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, g.smooth_last.x, g.smooth_last.y);
  cairo_line_to(cr, p.x, p.y);
  cairo_stroke(cr);
  cairo_arc(cr, p.x, p.y, g.eraser_width / 2.0, 0, 2 * M_PI);
  cairo_fill(cr);
  cairo_destroy(cr);
  cairo_surface_mark_dirty(g.backbuf);
  g.smooth_last = p;
}

void tools_finish_stroke(void)
{
  if (g.pen_drawing) pen_finish();
}

/* Temporary eraser used by the stylus eraser tip, the stylus side button and
 * the middle mouse button. */
void tools_erase_start(Pt p)
{
  tools_commit(0);
  tools_push_undo();
  g.middle_erasing = 1;
  g.smooth_last = p;
  erase_dot(p);
  app_update_cursor();
  app_request_render();
}

void tools_erase_move(Pt p)
{
  if (!g.middle_erasing) return;
  erase_segment(p);
  app_request_render();
}

void tools_erase_end(void)
{
  if (!g.middle_erasing) return;
  g.middle_erasing = 0;
  app_update_cursor();
  app_request_render();
}

/* ------------------------------------------------------------------ */
/* hit testing                                                        */
/* ------------------------------------------------------------------ */

static double dist_pt(Pt a, Pt b)
{
  return hypot(a.x - b.x, a.y - b.y);
}

/* Snap an angle to the closest multiple of 30 or 45 degrees, matching the
 * Linux build's snap_common_rotation(). */
static double snap_common_rotation(double rotation)
{
  static const double steps[2] = { 30.0, 45.0 };
  double degrees = rotation * 180.0 / M_PI;
  double best = rotation;
  double best_diff = 1e300;

  for (int i = 0; i < 2; i++)
    {
      double candidate = floor(degrees / steps[i] + 0.5) * steps[i];
      double diff = fabs(degrees - candidate);
      if (diff < best_diff)
        {
          best_diff = diff;
          best = candidate;
        }
    }
  return best * M_PI / 180.0;
}

static double dist_seg(Pt p, Pt a, Pt b)
{
  double dx = b.x - a.x, dy = b.y - a.y;
  double len2 = dx * dx + dy * dy;
  if (len2 < 1e-9) return dist_pt(p, a);
  double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  Pt q = { a.x + t * dx, a.y + t * dy };
  return dist_pt(p, q);
}

static int point_in_poly(Pt p, const Pt *v, int n)
{
  int inside = 0;
  for (int i = 0, j = n - 1; i < n; j = i++)
    {
      if (((v[i].y > p.y) != (v[j].y > p.y)) &&
          (p.x < (v[j].x - v[i].x) * (p.y - v[i].y) /
                     (v[j].y - v[i].y) + v[i].x))
        inside = !inside;
    }
  return inside;
}

static Pt line_heads_handle(void)
{
  double mx = (g.line.x0 + g.line.x1) / 2.0;
  double my = (g.line.y0 + g.line.y1) / 2.0;
  double dx = g.line.x1 - g.line.x0, dy = g.line.y1 - g.line.y0;
  double len = hypot(dx, dy);
  Pt p = { mx, my };
  if (len > 0.001)
    {
      p.x += -dy / len * LINE_HEADS_HANDLE_OFFSET;
      p.y += dx / len * LINE_HEADS_HANDLE_OFFSET;
    }
  return p;
}

static int line_hit(Pt p, int *handle)
{
  if (dist_pt(p, (Pt) { g.line.x0, g.line.y0 }) <= 9)
    { *handle = LH_START; return 1; }
  if (dist_pt(p, (Pt) { g.line.x1, g.line.y1 }) <= 9)
    { *handle = LH_END; return 1; }
  if (g.line.type == TOOL_ARROW && dist_pt(p, line_heads_handle()) <= 9)
    { *handle = LH_HEADS; return 1; }
  if (dist_seg(p, (Pt) { g.line.x0, g.line.y0 },
               (Pt) { g.line.x1, g.line.y1 }) <= 9)
    { *handle = LH_MOVE; return 1; }
  return 0;
}

static int polygon_hit(Pt p, int *handle)
{
  double r = g.poly.radius;
  Pt pts[64];
  int n = 0;

  if (dist_pt(p, (Pt) { g.poly.cx + r + 22, g.poly.cy }) <= 9)
    { *handle = PH_POINTS; return 1; }
  if (dist_pt(p, (Pt) { g.poly.cx + r, g.poly.cy }) <= 9)
    { *handle = PH_RADIUS; return 1; }
  if (dist_pt(p, (Pt) { g.poly.cx + cos(g.poly.rotation) * r,
                        g.poly.cy + sin(g.poly.rotation) * r }) <= 9)
    { *handle = PH_ROTATION; return 1; }
  if (dist_pt(p, (Pt) {
        g.poly.cx + cos(g.poly.rotation + M_PI / g.poly.points) *
                        r * g.poly.inner,
        g.poly.cy + sin(g.poly.rotation + M_PI / g.poly.points) *
                        r * g.poly.inner }) <= 9)
    { *handle = PH_INNER; return 1; }

  polygon_vertices(&g.poly, pts, &n);
  if (point_in_poly(p, pts, n))
    { *handle = PH_CENTRE; return 1; }
  return 0;
}

static int triangle_hit(Pt p, int *handle)
{
  for (int i = 0; i < 3; i++)
    if (dist_pt(p, g.tri.v[i]) <= 9)
      { *handle = TH_V0 + i; return 1; }

  if (point_in_poly(p, g.tri.v, 3))
    { *handle = TH_CENTRE; return 1; }
  return 0;
}

/* ------------------------------------------------------------------ */
/* shape creation                                                     */
/* ------------------------------------------------------------------ */

static void default_polygon(double x, double y)
{
  memset(&g.poly, 0, sizeof(g.poly));
  g.poly.cx = x;
  g.poly.cy = y;
  g.poly.radius = 64;
  g.poly.inner = 0.45;
  g.poly.rotation = -M_PI / 2;
  g.poly.points = 5;
  g.edit = EDIT_POLYGON;
  g.handle = PH_RADIUS;
  g.poly_placing = 1;
  g.press_cx = x;
  g.press_cy = y;
  g.drag_started = 0;
}

/* A new triangle starts with one vertex under the first click.  The second
 * click fixes the second vertex, the pointer then previews the third, and
 * the third click fixes it. */
static void default_triangle(double x, double y)
{
  memset(&g.tri, 0, sizeof(g.tri));
  for (int i = 0; i < 3; i++)
    {
      g.tri.v[i].x = x;
      g.tri.v[i].y = y;
    }
  g.edit = EDIT_TRIANGLE;
  g.handle = TH_NONE;
  g.tri_placing = 1;
  g.drag_started = 0;
}

static void default_frame(Tool tool, double x, double y)
{
  memset(&g.frame, 0, sizeof(g.frame));
  g.frame.frame.cx = x;
  g.frame.frame.cy = y;
  g.frame.frame.w = 1;
  g.frame.frame.h = 1;
  g.frame.color = g.pen_color;
  g.frame.color.a = g.opacity;

  switch (tool)
    {
    case TOOL_ELLIPSE: g.frame.type = SHAPE_ELLIPSE; break;
    case TOOL_RRECT:
      g.frame.type = SHAPE_RRECT;
      g.frame.extra = 20;
      break;
    case TOOL_MATH:
      g.frame.type = SHAPE_MATH;
      g.frame.expr = (char *) malloc(256);
      if (g.frame.expr)
        strcpy(g.frame.expr, g.math_polar ? g.math_polar_expr : g.math_expr);
      g.frame.polar = g.math_polar;
      g.frame.xmin = g.math_polar ? g.math_tmin : g.math_xmin;
      g.frame.xmax = g.math_polar ? g.math_tmax : g.math_xmax;
      break;
    case TOOL_BARS:
      g.frame.type = SHAPE_BARS;
      g.frame.count = 6;
      g.frame.arrows = BARS_NONE;
      break;
    case TOOL_RANDOM:
      g.frame.type = SHAPE_RANDOM;
      g.frame.seed = (unsigned) (GetTickCount64() ^ (unsigned) (size_t) &g);
      g.frame.count = 5;
      break;
    default:
      break;
    }

  g.edit = EDIT_FRAME;
  g.handle = FH_BR;
  g.drag_started = 0;
  g.drag_new_frame = 1;
}

static void frame_click_defaults(void)
{
  double w = 160, h = 100;
  if (g.frame.type == SHAPE_MATH) { w = 360; h = 240; }
  else if (g.frame.type == SHAPE_BARS) { w = 260; h = 180; }
  else if (g.frame.type == SHAPE_RANDOM) { w = 360; h = 180; }

  if (g.frame.frame.w < FRAME_MIN_SIZE || g.frame.frame.h < FRAME_MIN_SIZE)
    {
      g.frame.frame.cx = g.press.x;
      g.frame.frame.cy = g.press.y;
      g.frame.frame.w = w;
      g.frame.frame.h = h;
    }
  g.drag_new_frame = 0;
  g.handle = FH_NONE;
}

/* ------------------------------------------------------------------ */
/* pointer down                                                       */
/* ------------------------------------------------------------------ */

static void spline_add(Pt p)
{
  if (g.nnodes >= 64) return;
  if (!g.nodes)
    {
      g.nodecap = 64;
      g.nodes = (SplineNode *) calloc((size_t) g.nodecap, sizeof(SplineNode));
    }
  SplineNode *n = &g.nodes[g.nnodes++];
  memset(n, 0, sizeof(*n));
  n->p = p;
  n->in = p;
  n->out = p;
  g.spline_cursor = p;
  g.spline_finish_pending = 0;
  g.spline_preview_on = 1;
  spline_update_auto(g.nodes, g.nnodes);
  g.edit = EDIT_SPLINE;
  g.handle = 0;
}

static void handle_spline_down(Pt p)
{
  for (int i = g.nnodes - 1; i >= 0; i--)
    {
      if (dist_pt(p, g.nodes[i].p) <= 9)
        {
          if (i == g.nnodes - 1 && g.nnodes >= 2)
            {
              /* A click finishes the spline; a drag moves this last node. */
              g.spline_finish_pending = 1;
            }
          g.handle = 1;
          g.press_index = i;
          g.drag_started = 0;
          /* Remove the rubber-band segment while a node is being edited. */
          g.spline_preview_on = 0;
          g.spline_cursor = g.nodes[g.nnodes - 1].p;
          return;
        }
    }

  for (int i = 1; i < g.nnodes; i++)
    {
      if (dist_pt(p, g.nodes[i].in) <= 8)
        {
          g.handle = 2;
          g.press_index = i;
          spline_drag_out = 0;
          g.drag_started = 0;
          g.spline_preview_on = 0;
          g.spline_cursor = g.nodes[g.nnodes - 1].p;
          return;
        }
      if (dist_pt(p, g.nodes[i].out) <= 8)
        {
          g.handle = 3;
          g.press_index = i;
          spline_drag_out = 1;
          g.drag_started = 0;
          g.spline_preview_on = 0;
          g.spline_cursor = g.nodes[g.nnodes - 1].p;
          return;
        }
    }

  spline_add(p);
}

void tools_pointer_down(Pt p, int button, int ctrl, int shift)
{
  if (g.text_editor_active) return;
  if (g.tool == TOOL_NONE) return;
  dlog("pointer_down tool=%d button=%d at %ld,%ld", (int) g.tool, button,
       (long) p.x, (long) p.y);

  g.ctrl_down = ctrl;
  g.shift_down = shift;
  g.press = p;

  if (button == 1)                /* middle button: erase while held */
    {
      tools_commit(0);
      tools_push_undo();
      g.middle_erasing = 1;
      g.smooth_last = p;
      erase_dot(p);
      app_update_cursor();
      app_request_render();
      return;
    }
  if (button != 0) return;

  if (g.edit != EDIT_NONE)
    {
      int h = 0;

      if (g.edit == EDIT_LINE)
        {
          if (line_hit(p, &h))
            {
              g.handle = h;
              line_press[0] = g.line.x0;
              line_press[1] = g.line.y0;
              line_press[2] = g.line.x1;
              line_press[3] = g.line.y1;
              return;
            }
          tools_commit(0);
          return;
        }
      if (g.edit == EDIT_SPLINE)
        {
          handle_spline_down(p);
          return;
        }
      if (g.edit == EDIT_POLYGON)
        {
          if (polygon_hit(p, &h))
            {
              g.handle = h;
              g.poly_placing = 0;
              g.press_cx = g.poly.cx;
              g.press_cy = g.poly.cy;
              g.press_distance = dist_pt(p, (Pt) { g.poly.cx, g.poly.cy });
              angle0 = atan2(p.y - g.poly.cy, p.x - g.poly.cx);
              if (h == PH_POINTS) g.poly.points = 3;
              g.drag_started = 0;
              return;
            }
          tools_commit(0);
          default_polygon(p.x, p.y);
          app_request_render();
          return;
        }
      if (g.edit == EDIT_TRIANGLE)
        {
          if (g.tri_placing == 1)
            {
              g.tri.v[1] = p;
              g.tri.v[2] = p;
              g.tri_placing = 2;
              app_request_render();
              return;
            }
          if (g.tri_placing == 2)
            {
              g.tri.v[2] = p;
              g.tri_placing = 0;
              app_request_render();
              return;
            }
          if (triangle_hit(p, &h))
            {
              g.handle = h;
              for (int i = 0; i < 3; i++)
                {
                  tri_press[i * 2] = g.tri.v[i].x;
                  tri_press[i * 2 + 1] = g.tri.v[i].y;
                }
              g.press_cx = (g.tri.v[0].x + g.tri.v[1].x + g.tri.v[2].x) / 3.0;
              g.press_cy = (g.tri.v[0].y + g.tri.v[1].y + g.tri.v[2].y) / 3.0;
              g.drag_started = 0;
              return;
            }
          tools_commit(0);
          default_triangle(p.x, p.y);
          app_request_render();
          return;
        }
      if (g.edit == EDIT_FRAME)
        {
          FrameHandle fh;
          if (frame_hit(&g.frame, p, &fh))
            {
              g.handle = fh;
              g.press_cx = g.frame.frame.cx;
              g.press_cy = g.frame.frame.cy;
              g.press_w = g.frame.frame.w;
              g.press_h = g.frame.frame.h;
              g.press_rot = g.frame.frame.rotation;
              g.press_font_size = g.frame.font_size;
              g.press_distance = dist_pt(p, (Pt) { g.press_cx, g.press_cy });
              angle0 = atan2(p.y - g.press_cy, p.x - g.press_cx);
              g.drag_started = 0;
              return;
            }
          Tool keep = g.tool;
          tools_commit(0);
          if (keep == TOOL_TEXT) app_begin_text(p);
          return;
        }

      tools_commit(0);
    }

  /* No shape is being edited. */
  switch (g.tool)
    {
    case TOOL_PEN:
    case TOOL_MARKER:
      pen_begin(p);
      break;
    case TOOL_ERASER:
      tools_push_undo();
      g.middle_erasing = 1;
      g.smooth_last = p;
      erase_dot(p);
      break;
    case TOOL_TEXT:
      app_begin_text(p);
      break;
    case TOOL_LINE:
    case TOOL_ARROW:
      memset(&g.line, 0, sizeof(g.line));
      g.line.type = g.tool;
      g.line.x0 = g.line.x1 = p.x;
      g.line.y0 = g.line.y1 = p.y;
      g.line.heads = ARROW_END;
      g.edit = EDIT_LINE;
      g.handle = LH_END;
      g.drag_started = 0;
      break;
    case TOOL_SPLINE:
      spline_add(p);
      break;
    case TOOL_POLYGON:
      default_polygon(p.x, p.y);
      break;
    case TOOL_TRIANGLE:
      default_triangle(p.x, p.y);
      break;
    case TOOL_ELLIPSE:
    case TOOL_RRECT:
    case TOOL_MATH:
    case TOOL_BARS:
    case TOOL_RANDOM:
      default_frame(g.tool, p.x, p.y);
      break;
    case TOOL_BUCKET:
      bucket_fill(p);
      break;
    default:
      break;
    }

  app_request_render();
}

/* ------------------------------------------------------------------ */
/* pointer move                                                       */
/* ------------------------------------------------------------------ */

/* Snap a dragged triangle vertex so the interior angle at that vertex is
 * exactly 30, 45, 60 or 90 degrees.  All points seeing a segment under the
 * same angle lie on a circle through the segment's endpoints, so the vertex
 * is snapped onto that circle. */
static Pt snap_triangle_vertex(const Triangle *tri, int idx, Pt v)
{
  Pt A = tri->v[(idx + 1) % 3];
  Pt B = tri->v[(idx + 2) % 3];
  double ab = hypot(B.x - A.x, B.y - A.y);
  if (ab < 1) return v;

  double vax = A.x - v.x, vay = A.y - v.y;
  double vbx = B.x - v.x, vby = B.y - v.y;
  double la = hypot(vax, vay), lb = hypot(vbx, vby);
  if (la < 1 || lb < 1) return v;

  double ang = acos(fmax(-1.0, fmin(1.0,
                                    (vax * vbx + vay * vby) / (la * lb))));
  static const double targets[4] =
    { M_PI / 6, M_PI / 4, M_PI / 3, M_PI / 2 };
  double th = targets[0], best = 1e9;
  for (int i = 0; i < 4; i++)
    {
      double d = fabs(ang - targets[i]);
      if (d < best) { best = d; th = targets[i]; }
    }

  double R = ab / (2.0 * sin(th));
  double h = ab / (2.0 * tan(th));
  double mx = (A.x + B.x) / 2.0, my = (A.y + B.y) / 2.0;
  double ux = -(B.y - A.y) / ab, uy = (B.x - A.x) / ab;
  double c1x = mx + ux * h, c1y = my + uy * h;
  double c2x = mx - ux * h, c2y = my - uy * h;

  double side = (B.x - A.x) * (v.y - A.y) - (B.y - A.y) * (v.x - A.x);
  double s1 = (B.x - A.x) * (c1y - A.y) - (B.y - A.y) * (c1x - A.x);
  int same = (side >= 0) == (s1 >= 0);
  double cx = same ? c1x : c2x;
  double cy = same ? c1y : c2y;

  double dx = v.x - cx, dy = v.y - cy, d = hypot(dx, dy);
  if (d < 1) return v;
  Pt out = { cx + dx / d * R, cy + dy / d * R };
  return out;
}

static void move_spline(Pt p)
{
  if (g.handle == 1 && g.press_index >= 0 && g.press_index < g.nnodes)
    {
      SplineNode *n = &g.nodes[g.press_index];
      if (g.spline_finish_pending)
        {
          if (dist_pt(p, n->p) < 4.0) return;
          g.spline_finish_pending = 0;
        }
      double dx = p.x - n->p.x, dy = p.y - n->p.y;
      n->p = p;
      n->in.x += dx; n->in.y += dy;
      n->out.x += dx; n->out.y += dy;
      spline_update_auto(g.nodes, g.nnodes);
      return;
    }
  if ((g.handle == 2 || g.handle == 3) && g.press_index > 0 &&
      g.press_index < g.nnodes)
    {
      SplineNode *n = &g.nodes[g.press_index];
      n->manual = 1;
      if (g.handle == 2)
        {
          n->in = p;
          if (!g.ctrl_down)
            {
              n->out.x = 2 * n->p.x - p.x;
              n->out.y = 2 * n->p.y - p.y;
            }
        }
      else
        {
          n->out = p;
          if (!g.ctrl_down)
            {
              n->in.x = 2 * n->p.x - p.x;
              n->in.y = 2 * n->p.y - p.y;
            }
        }
    }
}

void tools_pointer_move(Pt p, int ctrl, int shift)
{
  g.ctrl_down = ctrl;
  g.shift_down = shift;

  if (g.middle_erasing)
    {
      erase_segment(p);
      g.last = p;
      app_request_render();
      return;
    }
  if (g.pen_drawing)
    {
      pen_move(p);
      g.last = p;
      app_request_render();
      return;
    }

  if (g.edit == EDIT_NONE)
    {
      g.last = p;
      return;
    }

  if (g.edit == EDIT_LINE)
    {
      if (g.handle == LH_START)
        {
          Pt q = p;
          if (ctrl)
            {
              double a = atan2(q.y - g.line.y1, q.x - g.line.x1);
              a = snap_common_rotation(a);
              double len = hypot(q.x - g.line.x1, q.y - g.line.y1);
              q.x = g.line.x1 + cos(a) * len;
              q.y = g.line.y1 + sin(a) * len;
            }
          g.line.x0 = q.x;
          g.line.y0 = q.y;
        }
      else if (g.handle == LH_END)
        {
          Pt q = p;
          if (ctrl)
            {
              double a = atan2(q.y - g.line.y0, q.x - g.line.x0);
              a = snap_common_rotation(a);
              double len = hypot(q.x - g.line.x0, q.y - g.line.y0);
              q.x = g.line.x0 + cos(a) * len;
              q.y = g.line.y0 + sin(a) * len;
            }
          g.line.x1 = q.x;
          g.line.y1 = q.y;
        }
      else if (g.handle == LH_HEADS)
        {
          /* Keep cycling through the four head styles as the drag continues,
           * instead of stopping at the last style. */
          double d = dist_pt(p, g.press);
          int idx = (int) (d / 24.0) % 4;
          g.line.heads = (ArrowHeads) idx;
        }
      else if (g.handle == LH_MOVE)
        {
          g.line.x0 = line_press[0] + (p.x - g.press.x);
          g.line.y0 = line_press[1] + (p.y - g.press.y);
          g.line.x1 = line_press[2] + (p.x - g.press.x);
          g.line.y1 = line_press[3] + (p.y - g.press.y);
        }
      app_request_render();
      return;
    }

  if (g.edit == EDIT_SPLINE)
    {
      if (g.handle == 0 && g.spline_preview_on) g.spline_cursor = p;
      move_spline(p);
      app_request_render();
      return;
    }

  if (g.edit == EDIT_POLYGON)
    {
      if (g.handle == PH_RADIUS)
        {
          double dx = p.x - g.poly.cx;
          double dy = p.y - g.poly.cy;
          double dist = hypot(dx, dy);
          if (dist > 12.0)
            {
              g.poly.radius = dist;
              /* While the polygon is being placed, the drag direction also
               * sets its rotation; the centre stays where it was clicked. */
              if (g.poly_placing)
                {
                  double rot = atan2(dy, dx);
                  if (ctrl) rot = snap_common_rotation(rot);
                  g.poly.rotation = rot;
                }
            }
        }
      else if (g.handle == PH_ROTATION)
        {
          g.poly.rotation = atan2(p.y - g.poly.cy, p.x - g.poly.cx);
          if (ctrl) g.poly.rotation = snap_common_rotation(g.poly.rotation);
        }
      else if (g.handle == PH_INNER)
        {
          double d = dist_pt(p, (Pt) { g.poly.cx, g.poly.cy });
          if (g.poly.radius > 1)
            {
              double inner = d / g.poly.radius;
              double tolerance = g.poly.radius * 0.08;
              if (tolerance < 3.0) tolerance = 3.0;
              if (tolerance > 10.0) tolerance = 10.0;
              double flat = cos(M_PI / g.poly.points);

              if (fabs(d - g.poly.radius * flat) <= tolerance)
                inner = flat;

              if (inner < 0.08) inner = 0.08;
              if (inner > 1.0) inner = 1.0;
              g.poly.inner = inner;
            }
        }
      else if (g.handle == PH_POINTS)
        {
          double d = dist_pt(p, g.press);
          int n = 3 + (int) (d / 12.0);
          if (n < 3) n = 3;
          if (n > 16) n = 16;
          g.poly.points = n;
        }
      else if (g.handle == PH_CENTRE)
        {
          double dx = p.x - g.press.x, dy = p.y - g.press.y;
          g.poly.cx = g.press_cx + dx;
          g.poly.cy = g.press_cy + dy;
          if (g.poly_placing && hypot(dx, dy) > 4.0)
            {
              double rot = atan2(dy, dx);
              if (ctrl) rot = snap_common_rotation(rot);
              g.poly.rotation = rot;
            }
        }
      app_request_render();
      return;
    }

  if (g.edit == EDIT_TRIANGLE)
    {
      if (g.tri_placing == 1)
        {
          g.tri.v[1] = p;
          g.tri.v[2] = p;
          app_request_render();
          return;
        }
      if (g.tri_placing == 2)
        {
          g.tri.v[2] = p;
          app_request_render();
          return;
        }
      if (g.handle >= TH_V0 && g.handle <= TH_V2)
        {
          int i = g.handle - TH_V0;
          g.tri.v[i] = p;
          if (ctrl) g.tri.v[i] = snap_triangle_vertex(&g.tri, i, p);
        }
      else if (g.handle == TH_CENTRE)
        {
          double dx = p.x - g.press.x, dy = p.y - g.press.y;
          for (int i = 0; i < 3; i++)
            {
              g.tri.v[i].x = tri_press[i * 2] + dx;
              g.tri.v[i].y = tri_press[i * 2 + 1] + dy;
            }
        }
      app_request_render();
      return;
    }

  if (g.edit == EDIT_FRAME)
    {
      if (g.drag_new_frame)
        {
          g.frame.frame.cx = (g.press.x + p.x) / 2.0;
          g.frame.frame.cy = (g.press.y + p.y) / 2.0;
          g.frame.frame.w = fabs(p.x - g.press.x);
          g.frame.frame.h = fabs(p.y - g.press.y);
          app_request_render();
          return;
        }

      Frame *f = &g.frame.frame;

      if (g.handle == FH_MOVE)
        {
          if (!g.drag_started && dist_pt(p, g.press) > 3)
            g.drag_started = 1;
          if (g.drag_started)
            {
              f->cx = g.press_cx + (p.x - g.press.x);
              f->cy = g.press_cy + (p.y - g.press.y);
            }
        }
      else if (g.handle == FH_TL || g.handle == FH_TR ||
               g.handle == FH_BL || g.handle == FH_BR)
        {
          if (g.frame.type == SHAPE_TEXT)
            {
              double d = dist_pt(p, (Pt) { g.press_cx, g.press_cy });
              if (g.press_distance > 1 && d > 1)
                {
                  g.frame.font_size = g.press_font_size * (d / g.press_distance);
                  if (g.frame.font_size < 6) g.frame.font_size = 6;
                  if (g.frame.font_size > 500) g.frame.font_size = 500;
                  measure_frame_text(&g.frame);
                }
            }
          else
            {
              Frame saved;
              saved.cx = g.press_cx;
              saved.cy = g.press_cy;
              saved.w = g.press_w;
              saved.h = g.press_h;
              saved.rotation = g.press_rot;

              double lx, ly, ox, oy;
              frame_screen_to_local(&saved, p.x, p.y, &lx, &ly);
              if (g.handle == FH_TL) { ox = saved.w / 2; oy = saved.h / 2; }
              else if (g.handle == FH_TR) { ox = -saved.w / 2; oy = saved.h / 2; }
              else if (g.handle == FH_BL) { ox = saved.w / 2; oy = -saved.h / 2; }
              else { ox = -saved.w / 2; oy = -saved.h / 2; }

              double nw = fabs(lx - ox), nh = fabs(ly - oy);
              if (nw < FRAME_MIN_SIZE) nw = FRAME_MIN_SIZE;
              if (nh < FRAME_MIN_SIZE) nh = FRAME_MIN_SIZE;

              double mcx = (ox + lx) / 2.0, mcy = (oy + ly) / 2.0;
              double c = cos(saved.rotation), s = sin(saved.rotation);
              f->cx = saved.cx + mcx * c - mcy * s;
              f->cy = saved.cy + mcx * s + mcy * c;
              f->w = nw;
              f->h = nh;
            }
        }
      else if (g.handle == FH_ROTATE)
        {
          double a = atan2(p.y - g.press_cy, p.x - g.press_cx);
          double rot = g.press_rot + (a - angle0);
          if (ctrl) rot = snap_common_rotation(rot);
          f->rotation = rot;
        }
      else if (g.handle == FH_EXTRA)
        {
          if (g.frame.type == SHAPE_RRECT)
            {
              double lx, ly;
              frame_screen_to_local(f, p.x, p.y, &lx, &ly);
              double r = lx + f->w / 2.0;
              double maxr = fmin(f->w, f->h) / 2.0;
              if (r < 0) r = 0;
              if (r > maxr) r = maxr;
              g.frame.extra = r;
            }
          else if (g.frame.type == SHAPE_BARS)
            {
              double d = dist_pt(p, g.press);
              int n = 2 + (int) (d / 10.0);
              if (n < 2) n = 2;
              if (n > 40) n = 40;
              g.frame.count = n;
            }
          else if (g.frame.type == SHAPE_RANDOM)
            {
              double d = dist_pt(p, g.press);
              int n = 1 + (int) (d / 8.0);
              if (n < 1) n = 1;
              if (n > 16) n = 16;
              g.frame.count = n;
            }
        }
      else if (g.handle == FH_ARROWS)
        {
          /* Cycle through none, top, bottom and both as the drag continues. */
          double d = dist_pt(p, g.press);
          int idx = (int) (d / 24.0) % 4;
          g.frame.arrows = (BarsArrows) idx;
        }

      app_request_render();
    }

  g.last = p;
}

/* ------------------------------------------------------------------ */
/* pointer up                                                         */
/* ------------------------------------------------------------------ */

void tools_pointer_up(Pt p, int ctrl, int shift)
{
  (void) p;
  (void) ctrl;
  (void) shift;

  if (g.middle_erasing)
    {
      g.middle_erasing = 0;
      app_update_cursor();
      g.last = p;
      app_request_render();
      return;
    }

  if (g.pen_drawing)
    {
      pen_move(p);
      pen_finish();
      g.last = p;
      return;
    }

  if (g.edit == EDIT_SPLINE && g.spline_finish_pending)
    {
      g.spline_finish_pending = 0;
      tools_commit(0);
      g.last = p;
      return;
    }

  if (g.edit == EDIT_FRAME && g.drag_new_frame)
    frame_click_defaults();

  if (g.edit == EDIT_FRAME && g.handle == FH_MOVE &&
      !g.drag_started && g.frame.type == SHAPE_RANDOM)
    {
      g.frame.seed = (unsigned) (GetTickCount64() ^ (unsigned) (size_t) &g);
      app_request_render();
    }

  g.drag_started = 0;
  if (g.edit == EDIT_SPLINE) g.handle = 0;
  if (g.edit == EDIT_POLYGON) g.poly_placing = 0;
  g.last = p;
  app_request_render();
}

void tools_spline_hover(Pt p)
{
  if (g.edit == EDIT_SPLINE && g.handle == 0 && g.spline_preview_on)
    {
      g.spline_cursor = p;
      app_request_render();
    }
}

void tools_triangle_hover(Pt p)
{
  if (g.edit != EDIT_TRIANGLE || g.tri_placing == 0) return;
  if (g.tri_placing == 1) g.tri.v[1] = g.tri.v[2] = p;
  else g.tri.v[2] = p;
  app_request_render();
}

void tools_double_click(Pt p)
{
  (void) p;
  if (g.edit != EDIT_NONE) tools_commit(0);
  app_request_render();
}

void tools_right_short(Pt p)
{
  (void) p;
  if (g.pen_drawing) pen_finish();
  if (g.middle_erasing) g.middle_erasing = 0;

  if (g.edit != EDIT_NONE)
    {
      tools_commit(0);
      return;                    /* keep the tool active */
    }
  if (g.tool != TOOL_NONE) tools_exit();
}

/* ------------------------------------------------------------------ */
/* wheel and keyboard                                                 */
/* ------------------------------------------------------------------ */

void tools_wheel(int delta)
{
  if (g.tool == TOOL_NONE) return;
  int dir = delta > 0 ? 1 : -1;

  if (g.tool == TOOL_ERASER)
    step_eraser_width(dir);
  else if (g.tool == TOOL_BUCKET)
    step_fill_color(dir);
  else if (g.tool == TOOL_MARKER)
    step_line_color(dir);
  else if (g.wheel_mode == WHEEL_THICKNESS)
    step_pen_width(dir);
  else
    step_line_color(dir);
}

void tools_key(int vk, int ctrl, int shift)
{
  if (vk == VK_ESCAPE)
    {
      /* The captured menu is cleared by tools_exit() only when the tool is
       * actually dismissed.  The first Esc often just closes shape handles. */
      tools_right_short(g.last);
      return;
    }
  if (ctrl && (vk == 'Z' || vk == 'z'))
    {
      if (shift) tools_redo();
      else tools_undo();
    }
}

/* ------------------------------------------------------------------ */
/* text                                                               */
/* ------------------------------------------------------------------ */

static int text_is_rtl(const char *s)
{
  const unsigned char *p = (const unsigned char *) s;
  while (*p)
    {
      unsigned cp = 0;
      if (p[0] < 0x80) cp = p[0], p += 1;
      else if ((p[0] & 0xE0) == 0xC0) cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F), p += 2;
      else if ((p[0] & 0xF0) == 0xE0) cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F), p += 3;
      else if ((p[0] & 0xF8) == 0xF0) cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F), p += 4;
      else { p++; continue; }

      if ((cp >= 0x0590 && cp <= 0x08FF) ||
          (cp >= 0xFB1D && cp <= 0xFDFF) ||
          (cp >= 0xFE70 && cp <= 0xFEFF))
        return 1;
      if (cp >= 0x0041 && cp <= 0x007A) return 0;
    }
  return 0;
}

void tools_text_commit(const char *text, int cancelled)
{
  if (cancelled || !text || !text[0]) return;

  memset(&g.frame, 0, sizeof(g.frame));
  g.frame.type = SHAPE_TEXT;
  g.frame.text = _strdup(text);
  g.frame.font = _strdup(g.font);
  g.frame.font_size = font_pixel_size(g.font);
  g.frame.color = g.pen_color;
  g.frame.color.a = 1.0;
  measure_frame_text(&g.frame);

  int rtl = text_is_rtl(text);
  g.frame.frame.cy = g.text_editor_click_y;
  g.frame.frame.cx = g.text_editor_click_x +
                     (rtl ? -g.frame.frame.w / 2.0 : g.frame.frame.w / 2.0);
  g.frame.frame.rotation = 0;

  g.edit = EDIT_FRAME;
  g.handle = FH_NONE;
  g.drag_started = 0;
  g.drag_new_frame = 0;
  app_request_render();
}

/* ------------------------------------------------------------------ */
/* paint bucket                                                       */
/* ------------------------------------------------------------------ */

static int bucket_match(unsigned int *pix, unsigned int target)
{
  return pix[0] == target;
}

void bucket_fill(Pt p)
{
  int x = (int) floor(p.x) - g.vx;
  int y = (int) floor(p.y) - g.vy;
  if (x < 0 || y < 0 || x >= g.vw || y >= g.vh) return;

  cairo_surface_flush(g.backbuf);
  int stride = cairo_image_surface_get_stride(g.backbuf);
  unsigned char *data = cairo_image_surface_get_data(g.backbuf);

  unsigned int target = ((unsigned int *) (data + (size_t) y * stride))[x];
  unsigned int target_alpha = (target >> 24) & 0xff;

  int erase = 0;
  if (!g.fill_enabled)
    {
      if (target_alpha == 0) return;
      /* Only erase a fill colour; clicking a stroke does nothing.  Fills may
       * be translucent, so undo the premultiplication before comparing. */
      int r = (target >> 16) & 0xff, gg = (target >> 8) & 0xff;
      int b = target & 0xff;
      int a = (int) target_alpha;
      if (a > 0 && a < 255)
        {
          r = r * 255 / a;
          gg = gg * 255 / a;
          b = b * 255 / a;
          if (r > 255) r = 255;
          if (gg > 255) gg = 255;
          if (b > 255) b = 255;
        }
      int known = 0;
      for (int i = 1; i < 9; i++)
        {
          Color c = color_from_hex(g_fill_colors[i].hex, NULL);
          if (abs((int) (c.r * 255 + 0.5) - r) < 3 &&
              abs((int) (c.g * 255 + 0.5) - gg) < 3 &&
              abs((int) (c.b * 255 + 0.5) - b) < 3)
            known = 1;
        }
      {
        Color c = g.custom_fill_color;
        if (abs((int) (c.r * 255 + 0.5) - r) < 3 &&
            abs((int) (c.g * 255 + 0.5) - gg) < 3 &&
            abs((int) (c.b * 255 + 0.5) - b) < 3)
          known = 1;
      }
      if (!known) return;
      erase = 1;
    }

  size_t total = (size_t) g.vw * (size_t) g.vh;
  unsigned char *mask = (unsigned char *) calloc(total, 1);
  if (!mask) return;

  typedef struct { int x, y; } Seed;
  Seed *stack = (Seed *) malloc(sizeof(Seed) * 4096);
  int cap = 4096, n = 0;
  if (!stack) { free(mask); return; }

#define PUSH(px, py) do { \
  if (n >= cap) { cap *= 2; Seed *ns = (Seed *) realloc(stack, sizeof(Seed) * cap); \
    if (!ns) { free(stack); free(mask); return; } stack = ns; } \
  stack[n].x = (px); stack[n].y = (py); n++; } while (0)

  PUSH(x, y);
  int touched_edge = 0;

  while (n > 0)
    {
      Seed s = stack[--n];
      int sx = s.x, sy = s.y;
      if (sy < 0 || sy >= g.vh) continue;
      unsigned int *r = (unsigned int *) (data + (size_t) sy * stride);
      if (sx < 0 || sx >= g.vw) continue;
      if (mask[(size_t) sy * g.vw + sx]) continue;
      if (!bucket_match(&r[sx], target)) continue;

      int x1 = sx, x2 = sx;
      while (x1 > 0 && !mask[(size_t) sy * g.vw + x1 - 1] &&
             bucket_match(&r[x1 - 1], target)) x1--;
      while (x2 < g.vw - 1 && !mask[(size_t) sy * g.vw + x2 + 1] &&
             bucket_match(&r[x2 + 1], target)) x2++;

      for (int i = x1; i <= x2; i++)
        mask[(size_t) sy * g.vw + i] = 1;

      if (x1 == 0 || x2 == g.vw - 1 || sy == 0 || sy == g.vh - 1)
        touched_edge = 1;

      if (sy > 0)
        {
          unsigned int *up = (unsigned int *) (data + (size_t) (sy - 1) * stride);
          int i = x1;
          while (i <= x2)
            {
              while (i <= x2 && (mask[(size_t) (sy - 1) * g.vw + i] ||
                                 !bucket_match(&up[i], target))) i++;
              if (i > x2) break;
              int start = i;
              while (i <= x2 && !mask[(size_t) (sy - 1) * g.vw + i] &&
                     bucket_match(&up[i], target)) i++;
              PUSH(start, sy - 1);
            }
        }
      if (sy < g.vh - 1)
        {
          unsigned int *dn = (unsigned int *) (data + (size_t) (sy + 1) * stride);
          int i = x1;
          while (i <= x2)
            {
              while (i <= x2 && (mask[(size_t) (sy + 1) * g.vw + i] ||
                                 !bucket_match(&dn[i], target))) i++;
              if (i > x2) break;
              int start = i;
              while (i <= x2 && !mask[(size_t) (sy + 1) * g.vw + i] &&
                     bucket_match(&dn[i], target)) i++;
              PUSH(start, sy + 1);
            }
        }
    }

  if (touched_edge)
    {
      free(stack);
      free(mask);
      return;                    /* do not flood the whole screen */
    }

  /* The annotation buffer stores premultiplied ARGB, and a fill follows the
   * global opacity just like a stroke. */
  int fr = 0, fg = 0, fb = 0;
  unsigned int fa = 0;
  if (!erase)
    {
      fr = (int) (g.fill_color.r * 255 + 0.5);
      fg = (int) (g.fill_color.g * 255 + 0.5);
      fb = (int) (g.fill_color.b * 255 + 0.5);
      fa = (unsigned int) (g.opacity * 255.0 + 0.5);
      if (fa < 1) fa = 1;
      if (fa > 255) fa = 255;
    }

  tools_push_undo();
  for (int yy = 0; yy < g.vh; yy++)
    {
      unsigned int *r = (unsigned int *) (data + (size_t) yy * stride);
      for (int xx = 0; xx < g.vw; xx++)
        {
          if (!mask[(size_t) yy * g.vw + xx]) continue;
          if (erase)
            {
              r[xx] = 0;
              continue;
            }

          unsigned int dst = r[xx];
          unsigned int inv = 255u - fa;
          unsigned int sr = ((unsigned int) fr * fa + 127u) / 255u;
          unsigned int sg = ((unsigned int) fg * fa + 127u) / 255u;
          unsigned int sb = ((unsigned int) fb * fa + 127u) / 255u;
          unsigned int da = (dst >> 24) & 0xffu;
          unsigned int dr = (dst >> 16) & 0xffu;
          unsigned int dg = (dst >> 8) & 0xffu;
          unsigned int db = dst & 0xffu;

          unsigned int oa = fa + (da * inv + 127u) / 255u;
          unsigned int or_ = sr + (dr * inv + 127u) / 255u;
          unsigned int og = sg + (dg * inv + 127u) / 255u;
          unsigned int ob = sb + (db * inv + 127u) / 255u;
          if (oa > 255u) oa = 255u;
          if (or_ > 255u) or_ = 255u;
          if (og > 255u) og = 255u;
          if (ob > 255u) ob = 255u;
          r[xx] = (oa << 24) | (or_ << 16) | (og << 8) | ob;
        }
    }

  free(stack);
  free(mask);
  cairo_surface_mark_dirty(g.backbuf);
  app_request_render();
  ui_refresh_tray();
#undef PUSH
}
