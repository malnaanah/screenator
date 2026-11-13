/*
 * Screenator for Windows -- cairo rendering of annotations and shapes.
 * GPL-2.0-or-later.  See app.h.
 */

#include "app.h"

Color color_rgb(double r, double g, double b)
{
  Color c;
  c.r = r / 255.0;
  c.g = g / 255.0;
  c.b = b / 255.0;
  c.a = 1.0;
  return c;
}

Color color_from_hex(const char *hex, int *ok)
{
  Color c = color_rgb(255, 0, 0);
  unsigned r = 255, g = 0, b = 0;

  if (ok) *ok = 0;
  if (!hex) return c;
  while (*hex == ' ' || *hex == '#') hex++;
  if (sscanf(hex, "%2x%2x%2x", &r, &g, &b) == 3)
    {
      c.r = r / 255.0;
      c.g = g / 255.0;
      c.b = b / 255.0;
      c.a = 1.0;
      if (ok) *ok = 1;
    }
  return c;
}

void color_to_hex(Color c, char *buf, int size)
{
  int r = (int) (c.r * 255.0 + 0.5);
  int g2 = (int) (c.g * 255.0 + 0.5);
  int b = (int) (c.b * 255.0 + 0.5);
  if (r < 0) r = 0;
  if (r > 255) r = 255;
  if (g2 < 0) g2 = 0;
  if (g2 > 255) g2 = 255;
  if (b < 0) b = 0;
  if (b > 255) b = 255;
  snprintf(buf, (size_t) size, "#%02X%02X%02X", r, g2, b);
}

void set_source_color(cairo_t *cr, Color c)
{
  cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
}

/* Fill colours keep their own hue; the global opacity applies to both the
 * fill and the stroke. */
static Color fill_color_with_opacity(void)
{
  Color c = g.fill_color;
  c.a = g.opacity;
  return c;
}

static int display_dpi(void)
{
  HDC dc = GetDC(NULL);
  int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
  if (dc) ReleaseDC(NULL, dc);
  return dpi > 0 ? dpi : 96;
}

/* The font string stores a point size; Pango wants pixels. */
double font_pixel_size(const char *font)
{
  const char *p = font ? strrchr(font, ' ') : NULL;
  double points = p ? atof(p + 1) : 24.0;
  if (points <= 1.0) points = 24.0;
  return points * (double) display_dpi() / 72.0;
}

char *font_make(const char *family, int bold, int italic, double px)
{
  char buf[256];
  snprintf(buf, sizeof(buf), "%s%s%s %.0f", family,
           bold ? " Bold" : "", italic ? " Italic" : "", px);
  size_t n = strlen(buf) + 1;
  char *s = (char *) malloc(n);
  if (s) memcpy(s, buf, n);
  return s;
}

void measure_frame_text(FrameShape *shape)
{
  if (!shape || !shape->text || !shape->text[0])
    {
      shape->frame.w = FRAME_MIN_SIZE;
      shape->frame.h = FRAME_MIN_SIZE;
      shape->text_off_x = shape->text_off_y = 0;
      return;
    }

  cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
  cairo_t *cr = cairo_create(surf);
  PangoLayout *layout = pango_cairo_create_layout(cr);
  PangoFontDescription *fd = pango_font_description_from_string(shape->font);
  PangoRectangle ink, logical;

  pango_font_description_set_absolute_size(fd,
                                           shape->font_size * PANGO_SCALE);
  pango_layout_set_font_description(layout, fd);
  pango_layout_set_text(layout, shape->text, -1);
  pango_layout_set_auto_dir(layout, TRUE);
  pango_layout_get_pixel_extents(layout, &ink, &logical);

  shape->frame.w = ink.width > 1 ? ink.width : 1;
  shape->frame.h = ink.height > 1 ? ink.height : 1;
  shape->text_off_x = ink.x;
  shape->text_off_y = ink.y;

  pango_font_description_free(fd);
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
}

void draw_paper(cairo_t *cr, Paper paper, int w, int h)
{
  if (paper == PAPER_NONE) return;

  /* PDF pages are rendered by paper.c (PDFium raster image). */
  if (paper == PAPER_PDF)
    {
      paper_draw_pdf_page(cr, w, h);
      return;
    }

  cairo_save(cr);
  set_source_color(cr, color_rgb(255, 255, 255));
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);

  if (paper == PAPER_RULED)
    {
      cairo_set_source_rgba(cr, 0.35, 0.55, 0.85, 0.65);
      cairo_set_line_width(cr, 1.0);
      for (double y = 48; y < h; y += 32.0)
        {
          cairo_move_to(cr, 0, floor(y) + 0.5);
          cairo_line_to(cr, w, floor(y) + 0.5);
          cairo_stroke(cr);
        }
    }
  else if (paper == PAPER_GRAPH)
    {
      cairo_set_line_width(cr, 1.0);
      for (int x = 0; x < w; x += 25)
        {
          cairo_set_source_rgba(cr, 0.72, 0.75, 0.78, 0.55);
          cairo_move_to(cr, x + 0.5, 0);
          cairo_line_to(cr, x + 0.5, h);
          cairo_stroke(cr);
        }
      for (int y = 0; y < h; y += 25)
        {
          cairo_set_source_rgba(cr, 0.72, 0.75, 0.78, 0.55);
          cairo_move_to(cr, 0, y + 0.5);
          cairo_line_to(cr, w, y + 0.5);
          cairo_stroke(cr);
        }
      cairo_set_source_rgba(cr, 0.55, 0.60, 0.65, 0.65);
      for (int x = 0; x < w; x += 100)
        {
          cairo_move_to(cr, x + 0.5, 0);
          cairo_line_to(cr, x + 0.5, h);
          cairo_stroke(cr);
        }
      for (int y = 0; y < h; y += 100)
        {
          cairo_move_to(cr, 0, y + 0.5);
          cairo_line_to(cr, w, y + 0.5);
          cairo_stroke(cr);
        }
    }
  cairo_restore(cr);
}

void draw_backbuffer(cairo_t *cr)
{
  if (!g.backbuf) return;
  cairo_save(cr);
  cairo_set_source_surface(cr, g.backbuf, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);
}

static void arrow_head_sized(cairo_t *cr, double x, double y, double angle,
                             double len, double half)
{
  cairo_save(cr);
  cairo_translate(cr, x, y);
  cairo_rotate(cr, angle);
  cairo_move_to(cr, 0, 0);
  cairo_line_to(cr, -len, -half);
  cairo_line_to(cr, -len, half);
  cairo_close_path(cr);
  cairo_fill(cr);
  cairo_restore(cr);
}

static void arrow_head(cairo_t *cr, double x, double y, double angle,
                       double width)
{
  arrow_head_sized(cr, x, y, angle, 12.0 + width * 3.0,
                   5.0 + width * 1.2);
}

static void stroke_arrow(cairo_t *cr, double x0, double y0, double x1,
                         double y1, ArrowHeads heads, unsigned width)
{
  double angle = atan2(y1 - y0, x1 - x0);
  double dx = x1 - x0, dy = y1 - y0;
  double length = hypot(dx, dy);

  if (heads == ARROW_NONE || length < 1.0)
    {
      cairo_set_line_width(cr, width);
      cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
      cairo_move_to(cr, x0, y0);
      cairo_line_to(cr, x1, y1);
      cairo_stroke(cr);
      return;
    }

  int start_head = heads == ARROW_START || heads == ARROW_BOTH;
  int end_head = heads == ARROW_END || heads == ARROW_BOTH;
  double head_len = 12.0 + width * 3.0;
  double head_half = 5.0 + width * 1.2;

  if (start_head && end_head)
    head_len = fmin(head_len, length * 0.45);
  else
    head_len = fmin(head_len, length * 0.9);

  double ca = dx / length, sa = dy / length;
  double sx = x0 + (start_head ? ca * head_len : 0.0);
  double sy = y0 + (start_head ? sa * head_len : 0.0);
  double ex = x1 - (end_head ? ca * head_len : 0.0);
  double ey = y1 - (end_head ? sa * head_len : 0.0);

  /* The shaft ends at the base of the arrowhead, so the head is the end of
   * the line instead of the line running through its point. */
  cairo_set_line_width(cr, width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, sx, sy);
  cairo_line_to(cr, ex, ey);
  cairo_stroke(cr);

  if (end_head)
    arrow_head_sized(cr, x1, y1, angle, head_len, head_half);
  if (start_head)
    arrow_head_sized(cr, x0, y0, angle + M_PI, head_len, head_half);
}

void draw_line_shape(cairo_t *cr, const LineShape *line, unsigned width,
                     Color color)
{
  cairo_save(cr);
  set_source_color(cr, color);
  stroke_arrow(cr, line->x0, line->y0, line->x1, line->y1,
               line->type == TOOL_ARROW ? line->heads : ARROW_NONE, width);
  cairo_restore(cr);
}

void spline_update_auto(SplineNode *nodes, int count)
{
  for (int i = 0; i < count; i++)
    {
      if (nodes[i].manual) continue;
      Pt prev = i > 0 ? nodes[i - 1].p : nodes[i].p;
      Pt next = i + 1 < count ? nodes[i + 1].p : nodes[i].p;
      double dx = (next.x - prev.x) / 6.0;
      double dy = (next.y - prev.y) / 6.0;
      nodes[i].in.x = nodes[i].p.x - dx;
      nodes[i].in.y = nodes[i].p.y - dy;
      nodes[i].out.x = nodes[i].p.x + dx;
      nodes[i].out.y = nodes[i].p.y + dy;
    }
}

void draw_spline_shape(cairo_t *cr, const SplineNode *nodes, int count,
                       unsigned width, Color color)
{
  if (count < 2) return;

  cairo_save(cr);
  set_source_color(cr, color);
  cairo_set_line_width(cr, width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_move_to(cr, nodes[0].p.x, nodes[0].p.y);
  for (int i = 1; i < count; i++)
    cairo_curve_to(cr, nodes[i - 1].out.x, nodes[i - 1].out.y,
                   nodes[i].in.x, nodes[i].in.y, nodes[i].p.x, nodes[i].p.y);
  cairo_stroke(cr);
  cairo_restore(cr);
}

void polygon_vertices(const StarShape *poly, Pt *out, int *count)
{
  int n = poly->points * 2;
  double sx = poly->sx > 0.01 ? poly->sx : 1.0;
  double sy = poly->sy > 0.01 ? poly->sy : 1.0;
  double c = cos(poly->rotation), s = sin(poly->rotation);

  for (int i = 0; i < n; i++)
    {
      double a = (double) i * M_PI / poly->points;
      double r = (i % 2 == 0) ? poly->radius : poly->radius * poly->inner;
      /* Rotate the vertex first, then scale it along the screen's x and y
       * axes, so the scale handles keep their meaning when the shape is
       * rotated. */
      double vx = cos(a) * r, vy = sin(a) * r;
      double rx = vx * c - vy * s;
      double ry = vx * s + vy * c;
      out[i].x = poly->cx + rx * sx;
      out[i].y = poly->cy + ry * sy;
    }
  *count = n;
}

static void add_polygon_path(cairo_t *cr, const Pt *pts, int n)
{
  if (n < 3) return;
  cairo_move_to(cr, pts[0].x, pts[0].y);
  for (int i = 1; i < n; i++) cairo_line_to(cr, pts[i].x, pts[i].y);
  cairo_close_path(cr);
}

static void add_triangle_path(cairo_t *cr, const Triangle *tri)
{
  cairo_move_to(cr, tri->v[0].x, tri->v[0].y);
  cairo_line_to(cr, tri->v[1].x, tri->v[1].y);
  cairo_line_to(cr, tri->v[2].x, tri->v[2].y);
  cairo_close_path(cr);
}

static void add_ellipse_path(cairo_t *cr, double w, double h)
{
  cairo_new_sub_path(cr);
  cairo_save(cr);
  cairo_scale(cr, w / 2.0, h / 2.0);
  cairo_arc(cr, 0, 0, 1.0, 0, 2 * M_PI);
  cairo_restore(cr);
}

static void add_rrect_path(cairo_t *cr, double w, double h, double r)
{
  double maxr = fmin(w, h) / 2.0;
  if (r < 0) r = 0;
  if (r > maxr) r = maxr;

  double x0 = -w / 2.0, y0 = -h / 2.0, x1 = w / 2.0, y1 = h / 2.0;
  cairo_new_sub_path(cr);
  cairo_arc(cr, x1 - r, y0 + r, r, -M_PI / 2, 0);
  cairo_arc(cr, x1 - r, y1 - r, r, 0, M_PI / 2);
  cairo_arc(cr, x0 + r, y1 - r, r, M_PI / 2, M_PI);
  cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 1.5 * M_PI);
  cairo_close_path(cr);
}

/* Clip to the region outside the current closed path.  The caller rebuilds
 * the path and strokes it with twice the width, leaving only the outer half
 * of the stroke visible. */
static void clip_outside_path(cairo_t *cr)
{
  cairo_path_t *path = cairo_copy_path(cr);
  cairo_new_path(cr);
  cairo_rectangle(cr, -1e6, -1e6, 2e6, 2e6);
  cairo_append_path(cr, path);
  cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
  cairo_clip(cr);
  cairo_path_destroy(path);
  cairo_new_path(cr);
}

void draw_polygon_shape(cairo_t *cr, const StarShape *poly, unsigned width,
                        Color color)
{
  Pt pts[64];
  int n = 0;

  polygon_vertices(poly, pts, &n);
  if (n < 3) return;

  cairo_save(cr);
  add_polygon_path(cr, pts, n);
  if (g.fill_enabled)
    {
      set_source_color(cr, fill_color_with_opacity());
      cairo_fill_preserve(cr);
    }

  clip_outside_path(cr);
  add_polygon_path(cr, pts, n);
  set_source_color(cr, color);
  cairo_set_line_width(cr, width * 2.0);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_stroke(cr);
  cairo_restore(cr);
}

void draw_triangle_shape(cairo_t *cr, const Triangle *tri, unsigned width,
                         Color color)
{
  cairo_save(cr);
  add_triangle_path(cr, tri);
  if (g.fill_enabled)
    {
      set_source_color(cr, fill_color_with_opacity());
      cairo_fill_preserve(cr);
    }

  clip_outside_path(cr);
  add_triangle_path(cr, tri);
  set_source_color(cr, color);
  cairo_set_line_width(cr, width * 2.0);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_stroke(cr);
  cairo_restore(cr);
}

void frame_local_to_screen(const Frame *f, double lx, double ly, double *x,
                           double *y)
{
  double c = cos(f->rotation), s = sin(f->rotation);
  *x = f->cx + lx * c - ly * s;
  *y = f->cy + lx * s + ly * c;
}

void frame_screen_to_local(const Frame *f, double x, double y, double *lx,
                           double *ly)
{
  double dx = x - f->cx, dy = y - f->cy;
  double c = cos(-f->rotation), s = sin(-f->rotation);
  *lx = dx * c - dy * s;
  *ly = dx * s + dy * c;
}

void frame_handle_pos(const FrameShape *shape, FrameHandle h, Pt *p)
{
  const Frame *f = &shape->frame;
  double lx = 0, ly = 0;

  switch (h)
    {
    case FH_TL: lx = -f->w / 2; ly = -f->h / 2; break;
    case FH_TR: lx = f->w / 2; ly = -f->h / 2; break;
    case FH_BL: lx = -f->w / 2; ly = f->h / 2; break;
    case FH_BR: lx = f->w / 2; ly = f->h / 2; break;
    case FH_ROTATE: lx = 0; ly = -f->h / 2 - 26.0; break;
    case FH_EXTRA:
      if (shape->type == SHAPE_RRECT)
        { lx = -f->w / 2 + shape->extra; ly = -f->h / 2; }
      else if (shape->type == SHAPE_BARS || shape->type == SHAPE_RANDOM)
        { lx = -f->w / 2 - 28.0; ly = 0; }
      else
        { lx = 0; ly = f->h / 2; }
      break;
    case FH_ARROWS: lx = f->w / 2 + 28.0; ly = 0; break;
    default: break;
    }
  frame_local_to_screen(f, lx, ly, &p->x, &p->y);
}

int frame_hit(const FrameShape *shape, Pt p, FrameHandle *handle)
{
  static const FrameHandle corners[] =
    { FH_TL, FH_TR, FH_BL, FH_BR };

  for (int i = 0; i < 4; i++)
    {
      Pt h;
      frame_handle_pos(shape, corners[i], &h);
      if (hypot(h.x - p.x, h.y - p.y) <= 9.0)
        {
          if (handle) *handle = corners[i];
          return 1;
        }
    }

  {
    Pt h;
    frame_handle_pos(shape, FH_ROTATE, &h);
    if (hypot(h.x - p.x, h.y - p.y) <= 9.0)
      {
        if (handle) *handle = FH_ROTATE;
        return 1;
      }
  }

  if (shape->type == SHAPE_RRECT || shape->type == SHAPE_BARS ||
      shape->type == SHAPE_RANDOM)
    {
      Pt h;
      frame_handle_pos(shape, FH_EXTRA, &h);
      if (hypot(h.x - p.x, h.y - p.y) <= 9.0)
        {
          if (handle) *handle = FH_EXTRA;
          return 1;
        }
    }
  if (shape->type == SHAPE_BARS)
    {
      Pt h;
      frame_handle_pos(shape, FH_ARROWS, &h);
      if (hypot(h.x - p.x, h.y - p.y) <= 9.0)
        {
          if (handle) *handle = FH_ARROWS;
          return 1;
        }
    }

  {
    double lx, ly;
    frame_screen_to_local(&shape->frame, p.x, p.y, &lx, &ly);
    if (fabs(lx) <= shape->frame.w / 2 && fabs(ly) <= shape->frame.h / 2)
      {
        if (handle) *handle = FH_MOVE;
        return 1;
      }
  }
  return 0;
}

/* Convert an expression sample to the frame; returns 0 when the point is not
 * part of the curve. */
static int math_limit(MathParser *m, double x, double span, double *out)
{
  double a, b;
  double eps = span * 1e-6;
  if (eps <= 0) eps = 1e-9;

  int oka = math_eval(m, x - eps, &a);
  int okb = math_eval(m, x + eps, &b);
  if (oka && okb)
    {
      double scale = fmax(1.0, fmax(fabs(a), fabs(b)));
      if (fabs(a - b) <= scale * 1e-3)
        {
          *out = (a + b) / 2.0;
          return 1;
        }
    }
  return 0;
}

static int math_compare_doubles(const void *a, const void *b)
{
  double da = *(const double *) a;
  double db = *(const double *) b;
  return da < db ? -1 : (da > db ? 1 : 0);
}

/* Open V-shaped arrow head, used for delta spikes. */
static void math_draw_arrow_head(cairo_t *cr, double tip_x, double tip_y,
                                 double ux, double uy, double size)
{
  double px = -uy, py = ux;
  double base_x = tip_x - ux * size;
  double base_y = tip_y - uy * size;
  double half = size * 0.55;

  cairo_move_to(cr, tip_x, tip_y);
  cairo_line_to(cr, base_x + px * half, base_y + py * half);
  cairo_move_to(cr, tip_x, tip_y);
  cairo_line_to(cr, base_x - px * half, base_y - py * half);
}

void math_plot_path(cairo_t *cr, const FrameShape *shape, int w, int h)
{
  const char *err = NULL;
  MathParser *m = math_parse(shape->expr ? shape->expr : "", &err);
  if (!m) return;

  const int N = 900;
  double xs[N];
  double ys[N];
  double mags[N];
  int ok[N];
  int first = 1;
  double maxabs = 0.0;
  int nmag = 0;
  double ymin = 0.0, ymax = 0.0;
  int flat = 1;

  double span = shape->xmax - shape->xmin;
  if (span == 0) span = 1e-9;

  for (int i = 0; i < N; i++)
    {
      double t = shape->xmin + span * (double) i / (double) (N - 1);
      double v = 0;
      int valid = 0;

      xs[i] = t;
      if (math_eval(m, t, &v)) valid = 1;
      else if (math_limit(m, t, span, &v)) valid = 1;

      if (valid && isfinite(v))
        {
          ok[i] = 1;
          ys[i] = v;
          double a = fabs(v);
          mags[nmag++] = a;
          if (a > maxabs) maxabs = a;
        }
      else
        {
          ok[i] = 0;
          ys[i] = 0;
        }
    }

  /* A pole can be orders of magnitude larger than the rest of the curve.
   * Cap the scale near the median, and break the path where the values
   * exceed that scale, as the original Screenator does.  Without this, 1/x
   * and tan(x) draw a false vertical line across their asymptote. */
  if (maxabs < 1e-9) maxabs = 1.0;
  if (nmag > 1)
    {
      qsort(mags, (size_t) nmag, sizeof(double), math_compare_doubles);
      double median = mags[nmag / 2];
      if (median > 1e-9 && maxabs > 20.0 * median)
        maxabs = 20.0 * median;
    }
  for (int i = 0; i < N; i++)
    if (ok[i] && fabs(ys[i]) > maxabs * 1.02)
      ok[i] = 0;

  cairo_save(cr);
  if (shape->polar)
    {
      double scale = fmin(w, h) / (2.0 * maxabs);
      if (scale == 0.0) { math_free(m); cairo_restore(cr); return; }
      for (int i = 0; i < N; i++)
        {
          if (!ok[i]) { first = 1; continue; }
          double th = xs[i];
          double px = cos(th) * ys[i] * scale;
          double py = -sin(th) * ys[i] * scale;
          if (first) { cairo_move_to(cr, px, py); first = 0; }
          else cairo_line_to(cr, px, py);
        }
    }
  else
    {
      int any = 0;
      ymin = 1e300;
      ymax = -1e300;
      for (int i = 0; i < N; i++)
        if (ok[i])
          {
            if (ys[i] < ymin) ymin = ys[i];
            if (ys[i] > ymax) ymax = ys[i];
            any = 1;
          }
      if (!any) { ymin = ymax = 0.0; }
      flat = ymax - ymin <= 1e-9;
      if (!flat)
        {
          /* A little headroom keeps strokes inside the frame. */
          double pad = (ymax - ymin) * 0.02;
          ymin -= pad;
          ymax += pad;
        }

      for (int i = 0; i < N; i++)
        {
          if (!ok[i]) { first = 1; continue; }
          double px = -w / 2.0 + w * (xs[i] - shape->xmin) / span;
          double py = flat
            ? h / 2.0
            : h / 2.0 - h * (ys[i] - ymin) / (ymax - ymin);
          if (first) { cairo_move_to(cr, px, py); first = 0; }
          else cairo_line_to(cr, px, py);
        }
    }
  cairo_stroke(cr);

  /* Every delta() in the expression gets an arrow at the zero of its
   * argument.  The curve itself is flat because delta evaluates to zero. */
  {
    double roots[32];
    int nr = math_delta_roots(m, shape->xmin, shape->xmax, shape->polar,
                              roots, 32);
    if (nr > 0)
      {
        double lw = fmax(1.0, (double) g.pen_width);
        double head = lw * 4.0;
        if (head < 8.0) head = 8.0;
        if (head > 18.0) head = 18.0;

        set_source_color(cr, shape->color);
        cairo_set_line_width(cr, lw);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

        if (shape->polar)
          {
            double max_head = fmin(w, h) * 0.25;
            double radius = fmin(w, h) / 2.0 * 0.9;
            if (head > max_head) head = max_head;
            for (int i = 0; i < nr; i++)
              {
                double ux = cos(roots[i]);
                double uy = -sin(roots[i]);
                double tip_x = radius * ux;
                double tip_y = radius * uy;
                cairo_move_to(cr, 0.0, 0.0);
                cairo_line_to(cr, tip_x, tip_y);
                math_draw_arrow_head(cr, tip_x, tip_y, ux, uy, head);
              }
          }
        else
          {
            double max_head = h * 0.25;
            double baseline = flat
              ? h / 2.0
              : h / 2.0 - h * (0.0 - ymin) / (ymax - ymin);
            double tip_y = -h / 2.0 * 0.9;
            if (baseline < -h / 2.0) baseline = -h / 2.0;
            if (baseline > h / 2.0) baseline = h / 2.0;
            if (head > max_head) head = max_head;
            for (int i = 0; i < nr; i++)
              {
                double px = -w / 2.0 +
                  w * (roots[i] - shape->xmin) / span;
                cairo_move_to(cr, px, baseline);
                cairo_line_to(cr, px, tip_y);
                math_draw_arrow_head(cr, px, tip_y, 0.0, -1.0, head);
              }
          }
        cairo_stroke(cr);
      }
  }

  math_free(m);
  cairo_restore(cr);
}

#define RANDOM_SAMPLES 192
#define RANDOM_PAD 56
#define RANDOM_TOTAL (RANDOM_SAMPLES + 2 * RANDOM_PAD)
#define RANDOM_AMPLITUDE 0.60

static void draw_random_signal(cairo_t *cr, const FrameShape *shape,
                               int w, int h)
{
  int smooth = shape->count;
  if (smooth < 1) smooth = 5;
  if (smooth > 16) smooth = 16;

  double sigma = (smooth - 1) * 1.2;
  int radius = (int) ceil(3.0 * sigma);
  unsigned state = shape->seed ? shape->seed : 1u;
  double noise[RANDOM_TOTAL];
  double values[RANDOM_SAMPLES];
  double xs[RANDOM_SAMPLES];
  double ys[RANDOM_SAMPLES];
  double mean = 0.0, peak = 0.0;
  double line_width = g.pen_width;
  double half_h = h / 2.0 - line_width / 2.0;

  if (w < 4 || h < 4) return;
  if (half_h < 1) half_h = 1;

  /* xorshift noise, the same generator and seed handling as the original. */
  for (int i = 0; i < RANDOM_TOTAL; i++)
    {
      unsigned x = state;
      x ^= x << 13;
      x ^= x >> 17;
      x ^= x << 5;
      state = x;
      noise[i] = (x / (double) 0xffffffffu) * 2.0 - 1.0;
    }

  if (radius > 0)
    {
      double weights[2 * RANDOM_PAD + 1];
      double sumw = 0.0;

      for (int j = -radius; j <= radius; j++)
        {
          double wgt = exp(-0.5 * (j / sigma) * (j / sigma));
          weights[j + radius] = wgt;
          sumw += wgt;
        }

      for (int i = 0; i < RANDOM_SAMPLES; i++)
        {
          double sum = 0.0;
          int center = i + RANDOM_PAD;
          for (int j = -radius; j <= radius; j++)
            sum += noise[center + j] * weights[j + radius];
          values[i] = sum / sumw;
        }
    }
  else
    {
      for (int i = 0; i < RANDOM_SAMPLES; i++)
        values[i] = noise[i + RANDOM_PAD];
    }

  for (int i = 0; i < RANDOM_SAMPLES; i++) mean += values[i];
  mean /= RANDOM_SAMPLES;
  for (int i = 0; i < RANDOM_SAMPLES; i++)
    peak = fmax(peak, fabs(values[i] - mean));

  for (int i = 0; i < RANDOM_SAMPLES; i++)
    {
      double value = peak > 1e-9
        ? (values[i] - mean) / peak * RANDOM_AMPLITUDE : 0.0;
      xs[i] = -w / 2.0 + (i / (double) (RANDOM_SAMPLES - 1)) * w;
      ys[i] = -value * half_h;
    }

  cairo_save(cr);
  set_source_color(cr, shape->color);
  cairo_set_line_width(cr, line_width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

  if (smooth > 1)
    {
      cairo_move_to(cr, xs[0], ys[0]);
      for (int i = 1; i < RANDOM_SAMPLES; i++)
        {
          int i0 = i - 2 < 0 ? 0 : i - 2;
          int i1 = i - 1;
          int i2 = i;
          int i3 = i + 1 >= RANDOM_SAMPLES ? RANDOM_SAMPLES - 1 : i + 1;
          double c1x = xs[i1] + (xs[i2] - xs[i0]) / 6.0;
          double c1y = ys[i1] + (ys[i2] - ys[i0]) / 6.0;
          double c2x = xs[i2] - (xs[i3] - xs[i1]) / 6.0;
          double c2y = ys[i2] - (ys[i3] - ys[i1]) / 6.0;
          cairo_curve_to(cr, c1x, c1y, c2x, c2y, xs[i2], ys[i2]);
        }
    }
  else
    {
      cairo_move_to(cr, xs[0], ys[0]);
      for (int i = 1; i < RANDOM_SAMPLES; i++)
        cairo_line_to(cr, xs[i], ys[i]);
    }
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void draw_bars(cairo_t *cr, const FrameShape *shape, int w, int h)
{
  int n = shape->count;
  if (n < 2) n = 2;
  if (n > 40) n = 40;

  int top = shape->arrows == BARS_TOP || shape->arrows == BARS_BOTH;
  int bottom = shape->arrows == BARS_BOTTOM || shape->arrows == BARS_BOTH;
  int heads = 0;
  double head_len = 0.0, head_half = 0.0;

  if (shape->arrows != BARS_NONE)
    {
      double spacing = (double) w / (double) n;
      double hw = fmax(1.0, (double) g.pen_width) * 0.5 + 2.0;

      head_half = fmin(1.5 * hw, spacing * 0.42);
      head_len = fmin(6.0 * hw, h * 0.28);
      head_len = fmin(head_len, head_half * 4.0);
      heads = head_half >= 1.0 && head_len >= 2.0;
    }

  cairo_save(cr);
  set_source_color(cr, shape->color);
  cairo_set_line_width(cr, g.pen_width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  for (int i = 0; i < n; i++)
    {
      double x = -w / 2.0 + (i + 0.5) * w / (double) n;
      double y0 = -h / 2.0, y1 = h / 2.0;

      /* Stop the shaft at the base of an arrowhead, so the head is the end
       * of the line instead of the line running through its point. */
      if (heads && top) y0 += head_len;
      if (heads && bottom) y1 -= head_len;
      if (y1 - y0 < 1.0)
        {
          y0 = -h / 2.0;
          y1 = h / 2.0;
        }

      cairo_move_to(cr, x, y0);
      cairo_line_to(cr, x, y1);
      cairo_stroke(cr);
    }

  if (heads)
    {
      for (int i = 0; i < n; i++)
        {
          double x = -w / 2.0 + (i + 0.5) * w / (double) n;
          if (top)
            arrow_head_sized(cr, x, -h / 2.0, -M_PI / 2.0, head_len,
                             head_half);
          if (bottom)
            arrow_head_sized(cr, x, h / 2.0, M_PI / 2.0, head_len,
                             head_half);
        }
    }
  cairo_restore(cr);
}

void draw_frame_shape(cairo_t *cr, const FrameShape *shape)
{
  if (!shape) return;

  cairo_save(cr);
  cairo_translate(cr, shape->frame.cx, shape->frame.cy);
  cairo_rotate(cr, shape->frame.rotation);

  double w = shape->frame.w, h = shape->frame.h;
  if (w < 1) w = 1;
  if (h < 1) h = 1;

  set_source_color(cr, shape->color);

  switch (shape->type)
    {
    case SHAPE_ELLIPSE:
      add_ellipse_path(cr, w, h);
      if (g.fill_enabled)
        {
          set_source_color(cr, fill_color_with_opacity());
          cairo_fill_preserve(cr);
        }
      clip_outside_path(cr);
      add_ellipse_path(cr, w, h);
      set_source_color(cr, shape->color);
      cairo_set_line_width(cr, g.pen_width * 2.0);
      cairo_stroke(cr);
      break;

    case SHAPE_RRECT:
      add_rrect_path(cr, w, h, shape->extra);
      if (g.fill_enabled)
        {
          set_source_color(cr, fill_color_with_opacity());
          cairo_fill_preserve(cr);
        }
      clip_outside_path(cr);
      add_rrect_path(cr, w, h, shape->extra);
      set_source_color(cr, shape->color);
      cairo_set_line_width(cr, g.pen_width * 2.0);
      cairo_stroke(cr);
      break;

    case SHAPE_MATH:
      cairo_set_line_width(cr, g.pen_width);
      cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
      math_plot_path(cr, shape, (int) w, (int) h);
      break;

    case SHAPE_BARS:
      draw_bars(cr, shape, (int) w, (int) h);
      break;

    case SHAPE_RANDOM:
      draw_random_signal(cr, shape, (int) w, (int) h);
      break;

    case SHAPE_TEXT:
      if (shape->text && shape->text[0])
        {
          PangoLayout *layout = pango_cairo_create_layout(cr);
          PangoFontDescription *fd =
            pango_font_description_from_string(shape->font);
          pango_font_description_set_absolute_size(fd,
                                                   shape->font_size * PANGO_SCALE);
          pango_layout_set_font_description(layout, fd);
          pango_layout_set_text(layout, shape->text, -1);
          pango_layout_set_auto_dir(layout, TRUE);
          set_source_color(cr, shape->color);
          cairo_move_to(cr, -w / 2 - shape->text_off_x,
                        -h / 2 - shape->text_off_y);
          pango_cairo_show_layout(cr, layout);
          pango_font_description_free(fd);
          g_object_unref(layout);
        }
      break;
    }
  cairo_restore(cr);
}

static void dashed_frame(cairo_t *cr, const FrameShape *shape)
{
  cairo_save(cr);
  cairo_translate(cr, shape->frame.cx, shape->frame.cy);
  cairo_rotate(cr, shape->frame.rotation);
  double dashes[2] = { 4.0, 4.0 };
  cairo_set_dash(cr, dashes, 2, 0);
  cairo_set_line_width(cr, 1.0);
  cairo_set_source_rgba(cr, 0.15, 0.35, 0.85, 0.9);
  cairo_rectangle(cr, -shape->frame.w / 2, -shape->frame.h / 2,
                  shape->frame.w, shape->frame.h);
  cairo_stroke(cr);
  cairo_restore(cr);
}

static void handle_square(cairo_t *cr, double x, double y)
{
  cairo_set_source_rgba(cr, 0, 0, 0, 0.85);
  cairo_rectangle(cr, x - 7, y - 7, 14, 14);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
  cairo_rectangle(cr, x - 6, y - 6, 12, 12);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 0, 0, 0, 0.9);
  cairo_set_line_width(cr, 1.0);
  cairo_rectangle(cr, x - 6, y - 6, 12, 12);
  cairo_stroke(cr);
}

static void handle_circle(cairo_t *cr, double x, double y)
{
  cairo_set_source_rgba(cr, 0, 0, 0, 0.85);
  cairo_arc(cr, x, y, 6.0, 0, 2 * M_PI);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
  cairo_arc(cr, x, y, 5.0, 0, 2 * M_PI);
  cairo_fill(cr);
}

/* A number written beside a square handle, with a white halo. */
static void handle_label(cairo_t *cr, double x, double y, int value)
{
  char label[16];
  cairo_text_extents_t ext;
  snprintf(label, sizeof(label), "%d", value);

  cairo_save(cr);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 11.0);
  cairo_text_extents(cr, label, &ext);

  double tx = x + 11.0;
  double ty = y - ext.y_bearing - ext.height / 2.0;
  cairo_move_to(cr, tx, ty);
  cairo_text_path(cr, label);

  cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
  cairo_set_line_width(cr, 3.0);
  cairo_stroke_preserve(cr);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.95);
  cairo_fill(cr);
  cairo_restore(cr);
}

/* Arrow-heads mode icon for the line/arrow handle. */
static void handle_line_heads_icon(cairo_t *cr, double x, double y,
                                   ArrowHeads heads)
{
  int start = heads == ARROW_START || heads == ARROW_BOTH;
  int end = heads == ARROW_END || heads == ARROW_BOTH;

  cairo_save(cr);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.9);
  cairo_set_line_width(cr, 1.2);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  if (heads == ARROW_NONE)
    {
      cairo_move_to(cr, x - 3, y);
      cairo_line_to(cr, x + 3, y);
      cairo_stroke(cr);
      cairo_restore(cr);
      return;
    }

  cairo_move_to(cr, x - 4, y);
  cairo_line_to(cr, x + 4, y);
  cairo_stroke(cr);

  if (start)
    {
      cairo_move_to(cr, x - 5.5, y);
      cairo_line_to(cr, x - 2.0, y - 2.5);
      cairo_line_to(cr, x - 2.0, y + 2.5);
      cairo_close_path(cr);
      cairo_fill(cr);
    }
  if (end)
    {
      cairo_move_to(cr, x + 5.5, y);
      cairo_line_to(cr, x + 2.0, y - 2.5);
      cairo_line_to(cr, x + 2.0, y + 2.5);
      cairo_close_path(cr);
      cairo_fill(cr);
    }
  cairo_restore(cr);
}

/* Top/bottom/both arrow-head icon for the parallel-lines handle. */
static void handle_bars_arrow_icon(cairo_t *cr, double x, double y,
                                   BarsArrows arrows)
{
  int top = arrows == BARS_TOP || arrows == BARS_BOTH;
  int bottom = arrows == BARS_BOTTOM || arrows == BARS_BOTH;

  cairo_save(cr);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.9);
  cairo_set_line_width(cr, 1.2);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  if (arrows == BARS_NONE)
    {
      cairo_move_to(cr, x - 3, y);
      cairo_line_to(cr, x + 3, y);
      cairo_stroke(cr);
      cairo_restore(cr);
      return;
    }

  cairo_move_to(cr, x, y - 4);
  cairo_line_to(cr, x, y + 4);
  cairo_stroke(cr);

  if (top)
    {
      cairo_move_to(cr, x, y - 5.5);
      cairo_line_to(cr, x - 2.5, y - 2.0);
      cairo_line_to(cr, x + 2.5, y - 2.0);
      cairo_close_path(cr);
      cairo_fill(cr);
    }
  if (bottom)
    {
      cairo_move_to(cr, x, y + 5.5);
      cairo_line_to(cr, x - 2.5, y + 2.0);
      cairo_line_to(cr, x + 2.5, y + 2.0);
      cairo_close_path(cr);
      cairo_fill(cr);
    }
  cairo_restore(cr);
}

void draw_handles(cairo_t *cr)
{
  /* Keep the placement preview clean; the handles appear after placement. */
  if ((g.edit == EDIT_POLYGON && g.poly_placing) ||
      (g.edit == EDIT_TRIANGLE && g.tri_placing))
    return;

  if (g.edit == EDIT_FRAME)
    {
      Pt p;
      dashed_frame(cr, &g.frame);
      static const FrameHandle corners[] =
        { FH_TL, FH_TR, FH_BL, FH_BR };
      for (int i = 0; i < 4; i++)
        {
          frame_handle_pos(&g.frame, corners[i], &p);
          handle_square(cr, p.x, p.y);
        }
      frame_handle_pos(&g.frame, FH_ROTATE, &p);
      handle_circle(cr, p.x, p.y);
      {
        /* The guide must start at the frame's rotated top edge, not at the
         * unrotated top-centre position. */
        double rc = cos(g.frame.frame.rotation);
        double rs = sin(g.frame.frame.rotation);
        cairo_move_to(cr, g.frame.frame.cx + g.frame.frame.h / 2 * rs,
                      g.frame.frame.cy - g.frame.frame.h / 2 * rc);
      }
      cairo_line_to(cr, p.x, p.y);
      cairo_set_line_width(cr, 1.0);
      cairo_set_source_rgba(cr, 0.15, 0.35, 0.85, 0.9);
      cairo_stroke(cr);

      if (g.frame.type == SHAPE_RRECT)
        {
          frame_handle_pos(&g.frame, FH_EXTRA, &p);
          handle_circle(cr, p.x, p.y);
        }
      else if (g.frame.type == SHAPE_BARS)
        {
          frame_handle_pos(&g.frame, FH_EXTRA, &p);
          handle_square(cr, p.x, p.y);
          handle_label(cr, p.x, p.y, g.frame.count < 1 ? 1 :
                       (g.frame.count > 40 ? 40 : g.frame.count));

          frame_handle_pos(&g.frame, FH_ARROWS, &p);
          handle_square(cr, p.x, p.y);
          handle_bars_arrow_icon(cr, p.x, p.y, g.frame.arrows);
        }
      else if (g.frame.type == SHAPE_RANDOM)
        {
          frame_handle_pos(&g.frame, FH_EXTRA, &p);
          handle_square(cr, p.x, p.y);
          handle_label(cr, p.x, p.y, g.frame.count < 1 ? 1 :
                       (g.frame.count > 16 ? 16 : g.frame.count));
        }

      /* The text frame also keeps its rotation and corner handles. */
    }
  else if (g.edit == EDIT_LINE)
    {
      handle_circle(cr, g.line.x0, g.line.y0);
      handle_circle(cr, g.line.x1, g.line.y1);
      if (g.line.type == TOOL_ARROW)
        {
          double mx = (g.line.x0 + g.line.x1) / 2.0;
          double my = (g.line.y0 + g.line.y1) / 2.0;
          double dx = g.line.x1 - g.line.x0;
          double dy = g.line.y1 - g.line.y0;
          double len = hypot(dx, dy);
          double ox = 0, oy = 0;
          if (len > 0.001)
            {
              ox = -dy / len * LINE_HEADS_HANDLE_OFFSET;
              oy = dx / len * LINE_HEADS_HANDLE_OFFSET;
            }
          handle_square(cr, mx + ox, my + oy);
          handle_line_heads_icon(cr, mx + ox, my + oy, g.line.heads);
        }
    }
  else if (g.edit == EDIT_SPLINE)
    {
      for (int i = 0; i < g.nnodes; i++)
        {
          if (i > 0)
            {
              /* Each tangent handle gets its own line from the node; chaining
               * them made the second handle start where the first one ended. */
              cairo_move_to(cr, g.nodes[i].p.x, g.nodes[i].p.y);
              cairo_line_to(cr, g.nodes[i].in.x, g.nodes[i].in.y);
              cairo_move_to(cr, g.nodes[i].p.x, g.nodes[i].p.y);
              cairo_line_to(cr, g.nodes[i].out.x, g.nodes[i].out.y);
              cairo_set_line_width(cr, 1.0);
              cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 0.55);
              cairo_stroke(cr);
              handle_circle(cr, g.nodes[i].in.x, g.nodes[i].in.y);
              handle_circle(cr, g.nodes[i].out.x, g.nodes[i].out.y);
            }
          handle_square(cr, g.nodes[i].p.x, g.nodes[i].p.y);
        }
    }
  else if (g.edit == EDIT_POLYGON)
    {
      Pt verts[64];
      int n = 0;
      double r = g.poly.radius, rr = r * g.poly.inner;
      double sx = g.poly.sx > 0.01 ? g.poly.sx : 1.0;
      double sy = g.poly.sy > 0.01 ? g.poly.sy : 1.0;
      double rx = r * sx, ry = r * sy;
      double rc = cos(g.poly.rotation), rs = sin(g.poly.rotation);
      polygon_vertices(&g.poly, verts, &n);

      /* Corner handles scale the polygon on the x and y axes independently. */
      handle_square(cr, g.poly.cx - rx, g.poly.cy - ry);
      handle_square(cr, g.poly.cx + rx, g.poly.cy - ry);
      handle_square(cr, g.poly.cx - rx, g.poly.cy + ry);
      handle_square(cr, g.poly.cx + rx, g.poly.cy + ry);

      handle_square(cr, g.poly.cx + rx + 22, g.poly.cy);
      handle_label(cr, g.poly.cx + rx + 22, g.poly.cy,
                   g.poly.points < 3 ? 3 : (g.poly.points > 16 ? 16 :
                                            g.poly.points));
      handle_circle(cr, g.poly.cx + rx, g.poly.cy);
      {
        /* First vertex (rotation handle) and the inner vertex, both rotated
         * and then scaled on the screen axes. */
        double vx = r, vy = 0;
        handle_circle(cr, g.poly.cx + (vx * rc - vy * rs) * sx,
                      g.poly.cy + (vx * rs + vy * rc) * sy);
        vx = cos(M_PI / g.poly.points) * rr;
        vy = sin(M_PI / g.poly.points) * rr;
        handle_circle(cr, g.poly.cx + (vx * rc - vy * rs) * sx,
                      g.poly.cy + (vx * rs + vy * rc) * sy);
      }
      handle_circle(cr, g.poly.cx, g.poly.cy);
    }
  else if (g.edit == EDIT_TRIANGLE)
    {
      for (int i = 0; i < 3; i++)
        handle_circle(cr, g.tri.v[i].x, g.tri.v[i].y);
    }
}

static int preview_is_rtl(const char *s)
{
  const unsigned char *p = (const unsigned char *) s;
  while (*p)
    {
      unsigned cp = 0;
      if (p[0] < 0x80) { cp = p[0]; p += 1; }
      else if ((p[0] & 0xE0) == 0xC0)
        { cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
      else if ((p[0] & 0xF0) == 0xE0)
        { cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) |
               (p[2] & 0x3F); p += 3; }
      else if ((p[0] & 0xF8) == 0xF0)
        { cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
               ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); p += 4; }
      else { p++; continue; }

      if ((cp >= 0x0590 && cp <= 0x08FF) ||
          (cp >= 0xFB1D && cp <= 0xFDFF) ||
          (cp >= 0xFE70 && cp <= 0xFEFF))
        return 1;
      if (cp >= 0x0041 && cp <= 0x007A) return 0;
    }
  return 0;
}

/* Paint the live text and blinking caret directly on the overlay.  The real
 * EDIT is a transparent 16x16 window used only for focus, IME and editing. */
void draw_text_editor(cairo_t *cr)
{
  if (!g.textwin) return;

  wchar_t wbuf[2048] = L"";
  GetWindowTextW(g.textwin, wbuf, 2048);

  char utf8[8192];
  WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, utf8, sizeof(utf8), NULL, NULL);

  DWORD sel_start = 0, sel_end = 0;
  SendMessageW(g.textwin, EM_GETSEL, (WPARAM) &sel_start,
               (LPARAM) &sel_end);

  PangoLayout *layout = pango_cairo_create_layout(cr);
  PangoFontDescription *fd = pango_font_description_from_string(g.font);
  pango_font_description_set_absolute_size(fd, font_pixel_size(g.font) *
                                           PANGO_SCALE);
  pango_layout_set_font_description(layout, fd);
  pango_layout_set_text(layout, utf8, -1);
  pango_layout_set_auto_dir(layout, TRUE);

  PangoRectangle ink, logical;
  pango_layout_get_pixel_extents(layout, &ink, &logical);
  (void) ink;

  int rtl = preview_is_rtl(utf8);
  double w = logical.width;
  double h = logical.height > 0 ? logical.height : font_pixel_size(g.font);
  double x = g.text_editor_click_x;
  double y = g.text_editor_click_y - h / 2.0;

  if (rtl && w > 0) x -= w;

  Color c = g.pen_color;
  c.a = 1.0;
  set_source_color(cr, c);
  cairo_move_to(cr, x, y);
  pango_cairo_show_layout(cr, layout);

  if (g.text_caret_visible)
    {
      int caret_chars = (int) sel_end;
      int bytes = WideCharToMultiByte(CP_UTF8, 0, wbuf, caret_chars,
                                      NULL, 0, NULL, NULL);
      if (bytes < 0) bytes = 0;
      if (bytes > (int) strlen(utf8)) bytes = (int) strlen(utf8);

      PangoRectangle strong;
      pango_layout_get_cursor_pos(layout, bytes, &strong, NULL);

      double cx = x + PANGO_PIXELS(strong.x);
      double cy = y + PANGO_PIXELS(strong.y);
      double ch = PANGO_PIXELS(strong.height);
      if (ch <= 0) ch = h;

      cairo_set_line_width(cr, 1.5);
      cairo_move_to(cr, cx, cy);
      cairo_line_to(cr, cx, cy + ch);
      cairo_stroke(cr);
    }

  pango_font_description_free(fd);
  g_object_unref(layout);
}

/* Populate the Pango/Fontconfig/FreeType caches at startup.  Without this the
 * first text editor creation pauses for a few hundred milliseconds while the
 * font is loaded. */
void draw_warm_up_text(void)
{
  cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
  cairo_t *cr;
  PangoLayout *layout;
  PangoFontDescription *fd;

  if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy(s);
      return;
    }
  cr = cairo_create(s);
  layout = pango_cairo_create_layout(cr);
  fd = pango_font_description_from_string(g.font);
  if (fd)
    {
      pango_font_description_set_absolute_size(fd, font_pixel_size(g.font) *
                                               PANGO_SCALE);
      pango_layout_set_font_description(layout, fd);
      pango_layout_set_text(layout, "Screenator", -1);
      pango_layout_get_pixel_extents(layout, NULL, NULL);
      pango_cairo_show_layout(cr, layout);
      pango_font_description_free(fd);
    }
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(s);
}

void draw_preview(cairo_t *cr)
{
  cairo_save(cr);
  cairo_set_line_width(cr, g.pen_width);

  switch (g.edit)
    {
    case EDIT_LINE:
      draw_line_shape(cr, &g.line, g.pen_width, g.pen_color);
      break;
    case EDIT_SPLINE:
      {
        if (g.spline_preview_on && g.nnodes > 0 && g.nnodes < 64 &&
            (g.spline_cursor.x != g.nodes[g.nnodes - 1].p.x ||
             g.spline_cursor.y != g.nodes[g.nnodes - 1].p.y))
          {
            SplineNode temp[65];
            memcpy(temp, g.nodes, sizeof(SplineNode) * (size_t) g.nnodes);
            memset(&temp[g.nnodes], 0, sizeof(SplineNode));
            temp[g.nnodes].p = g.spline_cursor;
            temp[g.nnodes].in = g.spline_cursor;
            temp[g.nnodes].out = g.spline_cursor;
            spline_update_auto(temp, g.nnodes + 1);
            draw_spline_shape(cr, temp, g.nnodes + 1, g.pen_width,
                              g.pen_color);
          }
        else
          {
            draw_spline_shape(cr, g.nodes, g.nnodes, g.pen_width,
                              g.pen_color);
          }
      }
      break;
    case EDIT_POLYGON:
      draw_polygon_shape(cr, &g.poly, g.pen_width, g.pen_color);
      break;
    case EDIT_TRIANGLE:
      draw_triangle_shape(cr, &g.tri, g.pen_width, g.pen_color);
      break;
    case EDIT_FRAME:
      draw_frame_shape(cr, &g.frame);
      break;
    default:
      break;
    }

  if (g.edit != EDIT_NONE) draw_handles(cr);
  cairo_restore(cr);
}
