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
                              GDK_BUTTON_RELEASE_MASK )

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

/* Shape tools.  The angled line is a line, the others are closed shapes that
 * can optionally be filled. */
typedef enum
{
  SCREENATOR_SHAPE_RECTANGLE,
  SCREENATOR_SHAPE_ELLIPSE,
  SCREENATOR_SHAPE_ROUNDED_RECTANGLE,
  SCREENATOR_SHAPE_ANGLED_LINE,
  SCREENATOR_SHAPE_FREE_LINE,
  SCREENATOR_SHAPE_ANGLED_ARROW,
  SCREENATOR_SHAPE_FREE_ARROW,
  SCREENATOR_SHAPE_SPLINE,
  SCREENATOR_SHAPE_BUCKET,
  SCREENATOR_SHAPE_STAR,
  SCREENATOR_SHAPE_TRIANGLE
} ScreenatorShapeType;

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

/* The rounded rectangle keeps its rectangle and corner radius editable after
 * it is dragged out.  "radius" is in pixels and is clamped to half the shorter
 * side, where the shape becomes a stadium. */
typedef struct
{
  gint    x0;
  gint    y0;
  gint    x1;
  gint    y1;
  gdouble radius;
} ScreenatorRoundedRect;

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

  /* Rounded rectangle: after the drag its corner radius stays adjustable
   * until the shape is committed. */
  gboolean              rounded_active;
  ScreenatorRoundedRect rounded;
  gboolean              rounded_radius_drag;
  gboolean              rounded_move_drag;
  gint                  rounded_press_x;
  gint                  rounded_press_y;
  ScreenatorRoundedRect rounded_press;

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
  gint         icon_drag_start_x;
  gint         icon_drag_start_y;
  gboolean     icon_dragging;
  gint         icon_menu_x;
  gint         icon_menu_y;

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
  gboolean     hide_tool_hint;
  gboolean     hide_spline_hint;

  /* Shape tools */
  ScreenatorPaintContext *default_shape;
  ScreenatorShapeType     shape_type;
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
  GtkWidget   *tray_menu;

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

  gchar       *clientdata;

  cairo_surface_t *undobuffer[SCREENATOR_MAX_UNDO];
  gint            undo_head, undo_depth, redo_depth;


} ScreenatorData;


void toggle_visibility (ScreenatorData *data);
void hide_window (ScreenatorData *data);
void show_window (ScreenatorData *data);

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
void draw_text (ScreenatorData *data, GdkDevice *dev, gint x, gint y, const gchar *text);
void draw_shape (ScreenatorData *data, cairo_t *cr, ScreenatorShapeType type,
		 gint x0, gint y0, gint x1, gint y1);
void shape_get_bounds (ScreenatorData *data, ScreenatorShapeType type,
		       gint x0, gint y0, gint x1, gint y1,
		       GdkRectangle *bounds);
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
void rounded_rect_get_bounds (const ScreenatorRoundedRect *rect, gint pad,
			      GdkRectangle *bounds);
void rounded_rect_finish (ScreenatorData *data,
			  ScreenatorDeviceData *devdata);
void rounded_rect_finish_all (ScreenatorData *data);
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
void set_tray_icon (ScreenatorData *data);

int app_parse_args (int argc, char **argv, ScreenatorData *data);

#endif
