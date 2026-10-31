/* 
 * Gromit -- a program for painting on the screen
 * Screenator -- a vibe-coded screen annotation tool based on Gromit-MPX 1.4.2
 * Screenator Copyright (C) 2026 Mahmoud Alnaanah <malnaanah@gmail.com>
 * Copyright (C) 2000 Simon Budig <Simon.Budig@unix-ag.org>
 *
 * MPX modifications Copyright (C) 2009 Christian Beier <dontmind@freeshell.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef SCREENATOR_H
#define SCREENATOR_H

#include "build-config.h"

#include <glib.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#ifdef APPINDICATOR_IS_LEGACY
#include <libappindicator/app-indicator.h>
#else
#include <libayatana-appindicator/app-indicator.h>
#endif

#define SCREENATOR_MOUSE_EVENTS ( GDK_BUTTON_MOTION_MASK | \
                              GDK_BUTTON_PRESS_MASK | \
                              GDK_BUTTON_RELEASE_MASK | \
                              GDK_SCROLL_MASK )

#define SCREENATOR_WINDOW_EVENTS ( SCREENATOR_MOUSE_EVENTS | GDK_EXPOSURE_MASK)

/* Atoms used to control Gromit */
#define GA_CONTROL    gdk_atom_intern ("Screenator/control", FALSE)
#define GA_STATUS     gdk_atom_intern ("Screenator/status", FALSE)
#define GA_QUIT       gdk_atom_intern ("Screenator/quit", FALSE)
#define GA_ACTIVATE   gdk_atom_intern ("Screenator/activate", FALSE)
#define GA_DEACTIVATE gdk_atom_intern ("Screenator/deactivate", FALSE)
#define GA_TOGGLE     gdk_atom_intern ("Screenator/toggle", FALSE)
#define GA_VISIBILITY gdk_atom_intern ("Screenator/visibility", FALSE)
#define GA_CLEAR      gdk_atom_intern ("Screenator/clear", FALSE)
#define GA_RELOAD     gdk_atom_intern ("Screenator/reload", FALSE)
#define GA_UNDO       gdk_atom_intern ("Screenator/undo", FALSE)
#define GA_REDO       gdk_atom_intern ("Screenator/redo", FALSE)

#define GA_DATA       gdk_atom_intern ("Screenator/data", FALSE)
#define GA_TOGGLEDATA gdk_atom_intern ("Screenator/toggledata", FALSE)

#define SCREENATOR_MAX_UNDO 4

/* Smallest size a placed frame shape can be resized to. */
#define FRAME_MIN_SIZE 8.0

/* Arrow size used by the "Arrow" tool in the icon menu. */
#define PEN_ARROW_SIZE 1.5

typedef enum
{
  SCREENATOR_PEN,
  SCREENATOR_ERASER,
  SCREENATOR_RECOLOR,
  SCREENATOR_TEXT,
  SCREENATOR_SHAPE
} ScreenatorPaintType;

/* Shape tools.  The line and arrow are drawn from one end point to the other;
 * the closed shapes can optionally be filled. */
typedef enum
{
  SCREENATOR_SHAPE_ELLIPSE,
  SCREENATOR_SHAPE_ROUNDED_RECTANGLE,
  SCREENATOR_SHAPE_LINE,
  SCREENATOR_SHAPE_ARROW,
  SCREENATOR_SHAPE_SPLINE,
  SCREENATOR_SHAPE_BUCKET,
  SCREENATOR_SHAPE_STAR,
  SCREENATOR_SHAPE_TRIANGLE,
  SCREENATOR_SHAPE_MATH,
  SCREENATOR_SHAPE_BARS,
  /* Not a menu tool: the type of a text frame that is being edited. */
  SCREENATOR_SHAPE_TEXT
} ScreenatorShapeType;

/* Background shown on the screen for drawing and writing.  The paper is drawn
 * below the annotations and stays when the screen is cleared; NONE turns it
 * off again. */
typedef enum
{
  SCREENATOR_PAPER_NONE = 0,
  SCREENATOR_PAPER_PLAIN,
  SCREENATOR_PAPER_RULED,
  SCREENATOR_PAPER_GRAPH
} ScreenatorPaperType;

/* One entry for every tool hint, so "Never show this message again" applies to
 * that tool only.  The order matches the names in config.c. */
typedef enum
{
  SCREENATOR_HINT_PEN = 0,
  SCREENATOR_HINT_MARKER,
  SCREENATOR_HINT_ERASER,
  SCREENATOR_HINT_TEXT,
  SCREENATOR_HINT_LINE,
  SCREENATOR_HINT_ARROW,
  SCREENATOR_HINT_SPLINE,
  SCREENATOR_HINT_POLYGON,
  SCREENATOR_HINT_TRIANGLE,
  SCREENATOR_HINT_ELLIPSE,
  SCREENATOR_HINT_ROUNDED_RECTANGLE,
  SCREENATOR_HINT_MATH,
  SCREENATOR_HINT_BARS,
  SCREENATOR_HINT_BUCKET,
  SCREENATOR_HINT_COUNT
} ScreenatorToolHint;

typedef struct
{
  ScreenatorPaintType type;
  guint           width;
  gfloat          arrowsize;
  guint           minwidth;
  GdkRGBA         *paint_color;
  cairo_t         *paint_ctx;
  gdouble         pressure;
} ScreenatorPaintContext;

typedef struct
{
  gint x;
  gint y;
  gint width;
} ScreenatorStrokeCoordinate;

/* One node of the spline tool: an on-curve point plus its two Bezier control
 * points.  The control points are stored as absolute screen positions so the
 * curve can be drawn without further maths.  "manual" is set once the user
 * moves one of the handles; automatic handles are recomputed from the
 * neighbouring nodes whenever the spline changes. */
typedef struct
{
  GdkPoint point;
  GdkPoint in;
  GdkPoint out;
  gboolean manual;
} ScreenatorSplineNode;

/* The star tool draws a regular star that stays editable until it is
 * committed.  "inner" is the inner radius as a fraction of "radius": at 1.0
 * the star becomes a regular polygon.  "points" is the number of star points,
 * so the polygon has twice as many vertices. */
typedef struct
{
  gdouble cx;
  gdouble cy;
  gdouble radius;
  gdouble inner;
  gdouble rotation;
  gint    points;
} ScreenatorStar;

/* Handles of the star tool.  NONE is zero so a freshly allocated device data
 * starts with no drag in progress. */
typedef enum
{
  SCREENATOR_STAR_HANDLE_NONE = 0,
  SCREENATOR_STAR_HANDLE_RADIUS,
  SCREENATOR_STAR_HANDLE_INNER,
  SCREENATOR_STAR_HANDLE_ROTATION,
  SCREENATOR_STAR_HANDLE_POINTS,
  SCREENATOR_STAR_HANDLE_CENTRE
} ScreenatorStarHandle;

/* The triangle tool keeps three editable vertices.  Every vertex has its own
 * handle, so the shape can be skewed freely instead of staying equilateral. */
typedef struct
{
  GdkPoint v[3];
} ScreenatorTriangle;

/* Handles of the triangle tool.  NONE is zero so a freshly allocated device
 * data starts with no drag in progress. */
typedef enum
{
  SCREENATOR_TRIANGLE_HANDLE_NONE = 0,
  SCREENATOR_TRIANGLE_HANDLE_V0,
  SCREENATOR_TRIANGLE_HANDLE_V1,
  SCREENATOR_TRIANGLE_HANDLE_V2,
  SCREENATOR_TRIANGLE_HANDLE_CENTRE
} ScreenatorTriangleHandle;

/* The line and arrow tools stay editable after the drag: each end point has
 * its own round handle, and pressing the line itself moves both ends. */
typedef struct
{
  ScreenatorShapeType type;   /* SCREENATOR_SHAPE_LINE or ..._ARROW */
  gint                x0;
  gint                y0;
  gint                x1;
  gint                y1;
} ScreenatorLine;

/* Handles of the line and arrow tools.  NONE is zero so a freshly allocated
 * device data starts with no drag in progress. */
typedef enum
{
  SCREENATOR_LINE_HANDLE_NONE = 0,
  SCREENATOR_LINE_HANDLE_START,
  SCREENATOR_LINE_HANDLE_END,
  SCREENATOR_LINE_HANDLE_MOVE
} ScreenatorLineHandle;

/* The rounded rectangle keeps its rectangle and corner radius editable after
 * it is dragged out.  "radius" is in pixels and is clamped to half the shorter
 * side, where the shape becomes a stadium. */
/* A placed shape that can be edited with handles.  The rectangle, the ellipse,
 * the rounded rectangle, the text, the math plot and the parallel lines all
 * use the same frame: a centre, a size and a rotation.  "type" says how the
 * frame is painted and which extra handle it offers. */
typedef struct
{
  gdouble cx;
  gdouble cy;
  gdouble w;          /* full width in pixels */
  gdouble h;          /* full height in pixels */
  gdouble rotation;   /* radians, clockwise on screen */
} ScreenatorFrame;

typedef enum
{
  SCREENATOR_FRAME_HANDLE_NONE = 0,
  SCREENATOR_FRAME_HANDLE_MOVE,     /* drag inside the frame */
  SCREENATOR_FRAME_HANDLE_SIZE,     /* square handle on the bottom-right corner */
  SCREENATOR_FRAME_HANDLE_ROTATE,   /* round handle above the frame */
  SCREENATOR_FRAME_HANDLE_EXTRA     /* roundness or line count */
} ScreenatorFrameHandle;

/* Functions the Math tool can plot are entered as a text expression, e.g.
 * "cos(2*pi*x)", so the frame stores the expression and the x range. */

/* One placed frame shape.  "extra" is the corner radius of a rounded
 * rectangle; "count" is the number of lines of the parallel-lines tool; text
 * shapes keep their string, font and colour so later menu changes do not
 * rewrite them. */
typedef struct
{
  ScreenatorShapeType    type;
  ScreenatorFrame        frame;
  gdouble                extra;
  gint                   count;
  gchar                 *expression;   /* Math tool: y = f(x) or r = f(theta) */
  gdouble                xmin;         /* x range, or theta range in polar mode */
  gdouble                xmax;
  gboolean               polar;        /* TRUE: r = f(theta), FALSE: y = f(x) */
  gchar                 *text;
  gchar                 *font;
  gdouble                font_size;
  gint                   text_off_x;   /* ink offset inside the frame */
  gint                   text_off_y;
  GdkRGBA                color;
} ScreenatorFrameShape;

typedef struct
{
  gdouble      lastx;
  gdouble      lasty;
  guint32      motion_time;
  GList*       coordlist;
  GdkDevice*   device;
  guint        index;
  guint        state;
  ScreenatorPaintContext *cur_context;
  ScreenatorPaintContext *saved_context;
  gboolean     is_grabbed;
  gboolean     was_grabbed;
  GdkDevice*   lastslave;

  /* Layer for the translucent stroke in progress: the stroke is drawn there
   * at full opacity and composited onto the backbuffer once it is finished. */
  cairo_surface_t *strokebuffer;
  cairo_t         *stroke_ctx;
  gdouble          stroke_alpha;
  GdkRectangle     stroke_bounds;

  /* Shape preview while the pointer is being dragged. */
  gboolean         shape_active;
  gint             shape_x0;
  gint             shape_y0;
  gint             shape_x1;
  gint             shape_y1;
  GArray          *shape_points;  /* ScreenatorSplineNode array for the spline */

  /* Spline node editing: drag a placed node to move it, drag a Bezier handle
   * to shape the curve, and click the last node again to finish. */
  gboolean         spline_node_drag;
  gint             spline_drag_index;
  gboolean         spline_finish_pending;
  gint             spline_press_x;
  gint             spline_press_y;

  /* Spline handle editing: "spline_drag_out" selects the outgoing handle of
   * the node at "spline_drag_index". */
  gboolean         spline_handle_drag;
  gboolean         spline_drag_out;

  /* Star tool: the star stays editable with its handles until committed. */
  gboolean             star_active;
  ScreenatorStar       star;
  ScreenatorStarHandle star_drag;
  gint                 star_press_x;
  gint                 star_press_y;
  gdouble              star_press_cx;
  gdouble              star_press_cy;

  /* Triangle tool: three vertex handles keep the shape editable until it is
   * committed. */
  gboolean                triangle_active;
  ScreenatorTriangle      triangle;
  ScreenatorTriangleHandle triangle_drag;
  gint                    triangle_press_x;
  gint                    triangle_press_y;
  GdkPoint                triangle_press_v[3];

  /* Line and arrow tools: both end points stay editable until the shape is
   * committed. */
  gboolean                line_active;
  ScreenatorLine          line;
  ScreenatorLineHandle    line_drag;
  gint                    line_press_x;
  gint                    line_press_y;
  gint                    line_press_x0;
  gint                    line_press_y0;
  gint                    line_press_x1;
  gint                    line_press_y1;

  /* Placed frame shapes (ellipse, rounded rectangle, text, math plot,
   * parallel lines).  They stay editable until they are committed. */
  gboolean              frame_active;
  ScreenatorFrameShape  frame_shape;
  ScreenatorFrameHandle frame_drag;
  gint                  frame_press_x;
  gint                  frame_press_y;
  gdouble               frame_press_cx;
  gdouble               frame_press_cy;
  gdouble               frame_press_w;
  gdouble               frame_press_h;
  gdouble               frame_press_rotation;
  gdouble               frame_press_font_size;
  gdouble               frame_press_distance;

  /* Smooth pen strokes: the last smoothed end point and the last raw
   * motion sample, used to finish the curve when the button is released. */
  gboolean         smooth_active;
  gdouble          smooth_x;
  gdouble          smooth_y;
  gdouble          smooth_target_x;
  gdouble          smooth_target_y;
} ScreenatorDeviceData;


typedef struct
{
  GtkWidget   *win;
  AppIndicator *trayicon;

  /* Movable on-screen icon window */
  GtkWidget   *icon_win;
  GtkWidget   *icon_image;
  gint         icon_drag_start_x;  /* pointer offset inside the icon */
  gint         icon_drag_start_y;
  gint         icon_press_x;       /* root position of the button press */
  gint         icon_press_y;
  gboolean     icon_dragging;
  gboolean     icon_pointer_grabbed; /* pointer held during a click/drag */
  gboolean     icon_focus_taken;     /* focus borrowed for the icon menu */
  gulong       icon_prev_focus;      /* X11 window that had the focus */
  GtkWidget   *icon_menu_pending;    /* menu waiting for the keyboard */
  guint        icon_menu_retry_id;
  guint        icon_menu_retry_count;
  gint         icon_menu_x;
  gint         icon_menu_y;

  /* Small indicator of the current line and fill settings, shown to the left
   * of the floating icon. */
  GtkWidget   *settings_win;
  gboolean     show_settings_indicator;

  /* Whether the floating icon (the menu button) is shown. */
  gboolean     show_floating_icon;

  /* Dialog shown above the drawings, and the state that is restored when the
   * last dialog closes (see show_dialog_above_drawings()). */
  GtkWidget   *top_dialog;
  gboolean     dialog_released_grab;  /* painting grab released for it */

  GdkCursor   *paint_cursor;
  GdkCursor   *erase_cursor;
  GdkCursor   *text_cursor;
  GdkCursor   *bucket_cursor;

  GdkDisplay  *display;
  GdkScreen   *screen;
  gboolean     xinerama;
  gboolean     composited;
  GdkWindow   *root;
  gchar       *hot_keyval;
  guint        hot_keycode;
  gchar       *undo_keyval;
  guint        undo_keycode;
  /* Hardware keycode of "z", grabbed for Ctrl+Z / Ctrl+Shift+Z while painting
   * mode is active; 0 when the shortcut is not grabbed. */
  guint        undo_ctrl_keycode;
  gdouble      opacity;
  guint        pen_width;
  guint        eraser_width;
  GdkRGBA      pen_color;
  GdkRGBA      marker_color; /* fixed yellow, 50% opacity for the Marker tool */
  gint         pen_tool;   /* 0 = config default, 1 = pen, 2 = arrow, 3 = eraser,
			      4 = text, 5 = rectangle, 6 = ellipse,
			      7 = rounded rectangle, 8 = angled line,
			      9 = free line, 10 = angled arrow, 11 = marker */
  gchar       *font;       /* Pango font description for the Text tool */
  guint        icon_size;

  /* Tool hints.  These are independent of the other settings: Reset Settings
   * must not re-enable hints the user has turned off. */
  gboolean     hide_tool_hints[SCREENATOR_HINT_COUNT];
  gboolean     hide_spline_hint;
  gboolean     hide_snap_hint;
  gboolean     hide_text_hint;

  /* Shape tools */
  ScreenatorPaintContext *default_shape;
  ScreenatorShapeType     shape_type;
  /* Math tool settings.  Rectangular plots use y = f(x) over [xmin, xmax];
   * polar plots use r = f(theta) over [theta_min, theta_max].  Each mode
   * keeps its own expression and range so switching between them does not
   * lose the other plot. */
  gboolean                math_polar;
  gchar                  *math_expression;         /* y as a function of x */
  gdouble                 math_xmin;
  gdouble                 math_xmax;
  gchar                  *math_polar_expression;   /* r as a function of theta */
  gdouble                 math_theta_min;
  gdouble                 math_theta_max;
  gboolean                suspend_frame_finish;
  gboolean             shape_fill_enabled;
  GdkRGBA              shape_fill_color;

  /* Inline text editor shown at the click position */
  GtkWidget   *text_editor;
  GtkWidget   *text_entry;
  GdkDevice   *text_device;
  gint         text_x;
  gint         text_y;
  gboolean     text_pressed;
  gboolean     text_editor_active;
  gboolean     text_editor_sized;  /* fixed width chosen for this editor */
  gboolean     text_editor_rtl;    /* editor width was sized for RTL text */
  /* The text being typed is previewed in the annotation window, above the
   * annotations, because the editor window itself can be hidden behind them.
   * The caret is drawn there too, so it is never hidden by a drawing or the
   * screen paper. */
  gboolean     text_preview_valid;
  GdkRectangle text_preview_bounds; /* last area invalidated for the preview */
  gboolean     text_caret_visible;
  guint        text_caret_timeout;
  GtkWidget   *tray_menu;
  guint        tray_refresh_id;   /* pending tray menu rebuild */

  GdkRGBA     *white;
  GdkRGBA     *black;
  GdkRGBA     *red;

  ScreenatorPaintContext *default_pen;
  ScreenatorPaintContext *default_eraser;
  ScreenatorPaintContext *default_text;
  ScreenatorPaintContext *default_marker;
  ScreenatorPaintContext *forced_context;
 
  GHashTable  *tool_config;

  cairo_surface_t *backbuffer;

  GHashTable  *devdatatable;

  guint        timeout_id;
  guint        modified;
  guint        delayed;
  guint        maxwidth;
  guint        width;
  guint        height;
  guint        client;
  guint        painted;
  gboolean     hidden;
  gboolean     debug;

  /* Screen paper drawn below the annotations. */
  ScreenatorPaperType paper;
  /* X11 window that had the focus before a paper took it. */
  gulong       paper_prev_focus;

  gchar       *clientdata;

  cairo_surface_t *undobuffer[SCREENATOR_MAX_UNDO];
  gint            undo_head, undo_depth, redo_depth;


} ScreenatorData;


void toggle_visibility (ScreenatorData *data);
void hide_window (ScreenatorData *data);
void show_window (ScreenatorData *data);

/* Screen paper drawn below the annotations. */
void draw_screen_paper (ScreenatorData *data, cairo_t *cr);
void paper_take_focus (ScreenatorData *data);
void paper_restore_focus (ScreenatorData *data);
void update_window_shape (ScreenatorData *data);
void text_preview_add_shape (ScreenatorData *data, cairo_region_t *region);
void shape_previews_add_shape (ScreenatorData *data, cairo_region_t *region);

void parse_print_help (gpointer key, gpointer value, gpointer user_data);

void select_tool (ScreenatorData *data, GdkDevice *device, GdkDevice *slave_device, guint state);

void apply_pen_settings (ScreenatorData *data);
void set_pen_opacity (ScreenatorData *data, gdouble opacity);
void update_eraser_cursor (ScreenatorData *data);
void refresh_tray_menu (ScreenatorData *data);

void copy_surface (cairo_surface_t *dst, cairo_surface_t *src);
void swap_surfaces (cairo_surface_t *a, cairo_surface_t *b);
void snap_undo_state (ScreenatorData *data);
void undo_drawing (ScreenatorData *data);
void redo_drawing (ScreenatorData *data);

void draw_line (ScreenatorData *data, GdkDevice *dev, gint x1, gint y1, gint x2, gint y2);
void draw_line_finish (ScreenatorData *data, GdkDevice *dev);
void draw_arrow (ScreenatorData *data, GdkDevice *dev, gint x1, gint y1, gint width, gfloat direction);
void draw_shape (ScreenatorData *data, cairo_t *cr, ScreenatorShapeType type,
		 gint x0, gint y0, gint x1, gint y1);
void shape_get_bounds (ScreenatorData *data, ScreenatorShapeType type,
		       gint x0, gint y0, gint x1, gint y1,
		       GdkRectangle *bounds);
void line_get_bounds (ScreenatorData *data, const ScreenatorLine *line,
		      GdkRectangle *bounds);
void line_finish (ScreenatorData *data, ScreenatorDeviceData *devdata);
void line_finish_all (ScreenatorData *data);
void draw_spline (ScreenatorData *data, cairo_t *cr, GArray *nodes);
void spline_get_bounds (ScreenatorData *data, GArray *nodes,
			GdkRectangle *bounds);
void spline_update_auto (GArray *nodes);
void spline_cancel_all (ScreenatorData *data);
void draw_star (ScreenatorData *data, cairo_t *cr,
		const ScreenatorStar *star);
void star_get_bounds (const ScreenatorStar *star, gint pad,
		      GdkRectangle *bounds);
void star_finish (ScreenatorData *data, ScreenatorDeviceData *devdata);
void star_finish_all (ScreenatorData *data);
void draw_triangle (ScreenatorData *data, cairo_t *cr,
		    const ScreenatorTriangle *triangle);
void triangle_get_bounds (const ScreenatorTriangle *triangle, gint pad,
			  GdkRectangle *bounds);
void triangle_finish (ScreenatorData *data, ScreenatorDeviceData *devdata);
void triangle_finish_all (ScreenatorData *data);
void draw_rounded_rect (ScreenatorData *data, cairo_t *cr,
			gint x0, gint y0, gint x1, gint y1, gdouble radius);

/* Editable frame shapes. */
void frame_shape_clear (ScreenatorFrameShape *shape);
gdouble frame_font_pixel_size (const gchar *font);
void frame_shape_measure_text (ScreenatorFrameShape *shape);
void draw_frame_shape (ScreenatorData *data, cairo_t *cr,
		       const ScreenatorFrameShape *shape);
void frame_shape_get_bounds (const ScreenatorFrameShape *shape, gint pad,
			     GdkRectangle *bounds);
void frame_handle_position (const ScreenatorFrameShape *shape,
			    ScreenatorFrameHandle handle, GdkPoint *pos);
void frame_screen_to_local (const ScreenatorFrame *frame, gdouble x, gdouble y,
			    gdouble *lx, gdouble *ly);
void frame_finish (ScreenatorData *data, ScreenatorDeviceData *devdata);
void frame_finish_all (ScreenatorData *data);
void text_editor_finish (ScreenatorData *data);
gboolean math_expression_is_valid (const gchar *expression, gchar **error);
gboolean fill_bucket (ScreenatorData *data, gint x, gint y);
void clear_screen (ScreenatorData *data);

void devdata_free (ScreenatorDeviceData *devdata);
void stroke_begin (ScreenatorData *data, GdkDevice *dev);
void stroke_finish (ScreenatorData *data, GdkDevice *dev);
void stroke_finish_all (ScreenatorData *data);

GdkCursor *get_tool_cursor (ScreenatorData *data,
			    ScreenatorPaintContext *context);
void set_device_cursor (ScreenatorData *data, GdkDevice *device,
			ScreenatorPaintContext *context);

gboolean coord_list_get_arrow_param (ScreenatorData *data,
					    GdkDevice  *dev,
					    gint        search_radius,
					    gint       *ret_width,
					    gfloat     *ret_direction);
void coord_list_prepend (ScreenatorData *data, GdkDevice* dev, gint x, gint y, gint width);
void coord_list_free (ScreenatorData *data, GdkDevice* dev);


ScreenatorPaintContext *paint_context_new (ScreenatorData *data, ScreenatorPaintType type,
				       GdkRGBA *fg_color, guint width, guint arrowsize,
                                       guint minwidth);
void paint_context_free (ScreenatorPaintContext *context);

void indicate_active(ScreenatorData *data, gboolean YESNO);

/* Movable on-screen icon */
void create_icon_window (ScreenatorData *data);
void update_icon_image (ScreenatorData *data);
void raise_icon_window (ScreenatorData *data);
void update_settings_indicator (ScreenatorData *data);
void set_tray_icon (ScreenatorData *data);

/* Keep dialogs and message boxes visible while the drawings are on screen. */
void show_dialog_above_drawings (ScreenatorData *data, GtkWidget *dialog);

int app_parse_args (int argc, char **argv, ScreenatorData *data);

#endif
