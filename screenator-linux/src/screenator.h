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

/* Toolbar items in their default order.  The same ids are used by the
 * Settings > Toolbar page and by the tool bar builder. */
#define PAPER_TOOLBAR_DEFAULT_ITEMS \
  "switch_mode,cursor,pen,text,eraser,arrow,timer,shapes,undo,redo,save," \
  "paper,insert_before,delete_page,insert_after,page_number,goto_page,clear," \
  "visibility,eraser_size," \
  "line_color,thickness,fill_color,opacity,marker_opacity,settings,hints,quit"

#define SCREENATOR_MOUSE_EVENTS ( GDK_POINTER_MOTION_MASK | \
                              GDK_BUTTON_MOTION_MASK | \
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
#define GA_TOGGLEICON gdk_atom_intern ("Screenator/toggle-icon", FALSE)
#define GA_VISIBILITY gdk_atom_intern ("Screenator/visibility", FALSE)
#define GA_CLEAR      gdk_atom_intern ("Screenator/clear", FALSE)
#define GA_RELOAD     gdk_atom_intern ("Screenator/reload", FALSE)
#define GA_UNDO       gdk_atom_intern ("Screenator/undo", FALSE)
#define GA_REDO       gdk_atom_intern ("Screenator/redo", FALSE)
#define GA_OPENPDF    gdk_atom_intern ("Screenator/open-pdf", FALSE)

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
  SCREENATOR_SHAPE_RANDOM,
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
  SCREENATOR_PAPER_GRAPH,
  SCREENATOR_PAPER_PDF
} ScreenatorPaperType;

/* Size of the Plain, Ruled and Graph paper pages.  PDF papers keep the size
 * of the PDF pages. */
typedef enum
{
  SCREENATOR_PAPER_SIZE_SCREEN = 0,
  SCREENATOR_PAPER_SIZE_A4_PORTRAIT,
  SCREENATOR_PAPER_SIZE_A4_LANDSCAPE,
  SCREENATOR_PAPER_SIZE_CUSTOM
} ScreenatorPaperSize;

/* One page of a Paper Drawing document.  Every page has its own annotation
 * layer, so drawings stay on the page where they were made and never appear
 * in Screen Annotation mode.  For a PDF page background is the rendered PDF
 * page; view_x/view_y are the visible part of it on screen. */
typedef struct
{
  ScreenatorPaperType style;
  cairo_surface_t    *annotations;
  cairo_surface_t    *background;
  gint                background_width;
  gint                background_height;
  gint                canvas_width;      /* page size in screen pixels */
  gint                canvas_height;
  gdouble             pdf_width;         /* page size in PDF points */
  gdouble             pdf_height;
  gint                view_x;
  gint                view_y;
  gboolean            painted;
  guint32             background_stamp;  /* LRU stamp while scrolling */
  gboolean            pdf_render_pending;
  guint               pdf_render_serial;

  /* Vector copy of the annotations, used for PDF export.  It is invalidated
   * when an operation cannot be represented as vectors (undo, bucket fill). */
  cairo_surface_t    *vector;
  gboolean            vector_valid;
} ScreenatorPaperPage;

/* A reversible page insertion or deletion.  The action owns the surface
 * references in page while the page is outside the document. */
typedef enum
{
  SCREENATOR_PAGE_ACTION_INSERT = 0,
  SCREENATOR_PAGE_ACTION_DELETE
} ScreenatorPageActionType;

typedef struct
{
  ScreenatorPageActionType type;
  ScreenatorPaperType      document;
  guint                    index;
  ScreenatorPaperPage      page;
} ScreenatorPageAction;

/* One selected paper type and its pages.  Plain, Ruled, Graph and PDF each
 * keep their own pages and drawings. */
typedef struct
{
  ScreenatorPaperType type;
  GArray             *pages;
  guint               page;
  gchar              *pdf_path;
  gpointer            pdf_document;   /* PopplerDocument for PDF papers */
  gint                pdf_page_count;
  gboolean            pdf_loaded;

  /* Continuous PDF view.  Page tops and heights are in the stacked canvas;
   * pdf_scroll_y is the top of the visible part. */
  GArray             *pdf_page_tops;
  GArray             *pdf_page_heights;
  gint                pdf_stack_width;
  gint                pdf_stack_height;
  gint                pdf_scroll_y;
  gboolean            modified;       /* changed since the last save */
} ScreenatorPaperDocument;

/* What the first stylus side button does.  The second side button opens the
 * stylus menu. */
typedef enum
{
  SCREENATOR_STYLUS_COLOR = 0,
  SCREENATOR_STYLUS_THICKNESS
} ScreenatorStylusMode;

/* What the mouse wheel changes, chosen from the long right-click menu. */
typedef enum
{
  SCREENATOR_WHEEL_COLOR = 0,
  SCREENATOR_WHEEL_THICKNESS
} ScreenatorWheelMode;

/* Corner where the floating icon is placed. */
typedef enum
{
  SCREENATOR_ICON_TOP_RIGHT = 0,
  SCREENATOR_ICON_TOP_LEFT,
  SCREENATOR_ICON_BOTTOM_RIGHT,
  SCREENATOR_ICON_BOTTOM_LEFT
} ScreenatorIconPosition;

typedef enum
{
  SCREENATOR_SHORTCUT_FLOATING_ICON = 0,
  SCREENATOR_SHORTCUT_PEN,
  SCREENATOR_SHORTCUT_TEXT,
  SCREENATOR_SHORTCUT_ARROW,
  SCREENATOR_SHORTCUT_TIMER,
  SCREENATOR_SHORTCUT_TOOLBAR,
  SCREENATOR_SHORTCUT_SWITCH_MODE,
  SCREENATOR_SHORTCUT_SCREENSHOT,
  SCREENATOR_SHORTCUT_UNDO,
  SCREENATOR_SHORTCUT_REDO,
  SCREENATOR_SHORTCUT_SAVE,
  SCREENATOR_SHORTCUT_INSERT_BEFORE,
  SCREENATOR_SHORTCUT_INSERT_AFTER,
  SCREENATOR_SHORTCUT_TOGGLE_VISIBILITY,
  SCREENATOR_SHORTCUT_EXIT_PAPER,
  SCREENATOR_SHORTCUT_QUIT,
  SCREENATOR_SHORTCUT_ESCAPE,
  SCREENATOR_SHORTCUT_COUNT
} ScreenatorShortcutId;

typedef enum
{
  SCREENATOR_SHORTCUT_ALWAYS = 0,
  SCREENATOR_SHORTCUT_PAINTING,
  SCREENATOR_SHORTCUT_PAPER
} ScreenatorShortcutScope;

typedef struct
{
  gchar  *text;
  guint   keyval;
  guint   keycode;
  guint   modifier;
} ScreenatorShortcut;

extern const gchar * const screenator_shortcut_defaults[
  SCREENATOR_SHORTCUT_COUNT];
extern const gchar * const screenator_shortcut_labels[
  SCREENATOR_SHORTCUT_COUNT];
extern const ScreenatorShortcutScope screenator_shortcut_scopes[
  SCREENATOR_SHORTCUT_COUNT];

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
  SCREENATOR_STAR_HANDLE_CENTRE,
  /* Not a visible handle: the drag that sets the radius and rotation from a
   * newly placed centre. */
  SCREENATOR_STAR_HANDLE_PLACEMENT
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

/* Arrowheads of the arrow tool.  The order matches the cycle of its handle;
 * END is the default so an arrow keeps its traditional look. */
typedef enum
{
  SCREENATOR_ARROW_HEADS_NONE = 0,
  SCREENATOR_ARROW_HEADS_START,
  SCREENATOR_ARROW_HEADS_END,
  SCREENATOR_ARROW_HEADS_BOTH
} ScreenatorArrowHeads;

/* The line and arrow tools stay editable after the drag: each end point has
 * its own round handle, pressing the line itself moves both ends, and an arrow
 * has a square handle that changes its arrowheads. */
typedef struct
{
  ScreenatorShapeType type;   /* SCREENATOR_SHAPE_LINE or ..._ARROW */
  gint                x0;
  gint                y0;
  gint                x1;
  gint                y1;
  ScreenatorArrowHeads heads;
} ScreenatorLine;

/* Distance of the arrow-heads handle from the middle of the line. */
#define LINE_HEADS_HANDLE_OFFSET 28.0

/* Handles of the line and arrow tools.  NONE is zero so a freshly allocated
 * device data starts with no drag in progress. */
typedef enum
{
  SCREENATOR_LINE_HANDLE_NONE = 0,
  SCREENATOR_LINE_HANDLE_START,
  SCREENATOR_LINE_HANDLE_END,
  SCREENATOR_LINE_HANDLE_HEADS,
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
  SCREENATOR_FRAME_HANDLE_SIZE_TL,  /* square handles on the four corners */
  SCREENATOR_FRAME_HANDLE_SIZE_TR,
  SCREENATOR_FRAME_HANDLE_SIZE_BL,
  SCREENATOR_FRAME_HANDLE_SIZE_BR,
  SCREENATOR_FRAME_HANDLE_ROTATE,   /* round handle above the frame */
  SCREENATOR_FRAME_HANDLE_EXTRA,    /* roundness or line count */
  SCREENATOR_FRAME_HANDLE_ARROWS    /* parallel lines: arrowheads */
} ScreenatorFrameHandle;

/* Arrowheads on the parallel-lines tool.  The order is the order in which the
 * handle cycles them. */
typedef enum
{
  SCREENATOR_BARS_ARROWS_NONE = 0,
  SCREENATOR_BARS_ARROWS_TOP,
  SCREENATOR_BARS_ARROWS_BOTTOM,
  SCREENATOR_BARS_ARROWS_BOTH
} ScreenatorBarsArrows;

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
  ScreenatorBarsArrows   arrows;
  guint                  seed;         /* Random Signal: generation seed */
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
  /* Window mode does not take a global pointer grab.  The tool is still
   * active while the pointer is over the Paper window, but other windows and
   * applications keep their normal pointer input and cursor. */
  gboolean     window_input;
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
  /* After a node or handle has been edited, keep the rubber-band segment
   * hidden until the next node is placed. */
  gboolean         spline_preview_paused;

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
  /* How many vertices have been placed by clicking.  While this is below
   * three, the next click places a vertex instead of grabbing a handle. */
  guint                   triangle_vertices;
  /* Time of the click that placed the third vertex, so the double-click event
   * that can follow it does not commit the triangle immediately. */
  guint32                 triangle_placed_time;

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
  guint                 frame_seed;

  /* State of the current stylus side-button action.  The tip is button 1, the
   * first side button is button 2 (eraser or scroll) and the second side
   * button is button 3 (opens the stylus menu). */
  gboolean             stylus_eraser_active;
  gboolean             stylus_side_button_down;
  gboolean             menu_button_down;

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
  GtkWidget   *stylus_menu;
  GtkWidget   *wheel_menu;
  GtkWidget   *marker_menu;
  GtkWidget   *quick_menu;
  ScreenatorStylusMode stylus_mode;
  ScreenatorWheelMode  wheel_mode;
  gboolean     hide_hints;
  gboolean     hints_shown;
  guint        hints_id;
  GtkWidget   *hints_dialog;
  guint        menu_hold_id;
  ScreenatorDeviceData *menu_hold_devdata;
  gboolean     menu_hold_is_stylus;
  gint         menu_hold_x;
  gint         menu_hold_y;
  guint        stylus_eraser_hold_id;
  ScreenatorDeviceData *stylus_eraser_hold_devdata;
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

  /* Whether the drawings are saved when Screenator quits and restored on the
   * next start. */
  gboolean     save_annotations_on_exit;

  /* Dialog shown above the drawings, and the state that is restored when the
   * last dialog closes (see show_dialog_above_drawings()). */
  GtkWidget   *top_dialog;
  gboolean     dialog_released_grab;  /* painting grab released for it */
  gulong       dialog_prev_focus;     /* X focus before the first dialog */

  GdkCursor   *paint_cursor;
  GdkCursor   *erase_cursor;
  GdkCursor   *text_cursor;
  GdkCursor   *bucket_cursor;
  GdkCursor   *hand_cursor;

  GdkDisplay  *display;
  GdkScreen   *screen;
  gboolean     xinerama;
  gboolean     composited;
  GdkWindow   *root;
  gchar       *hot_keyval;
  guint        hot_keycode;
  /* Hardware keycode of "z", grabbed for Ctrl+Z / Ctrl+Shift+Z while painting
   * mode is active; 0 when the shortcut is not grabbed. */
  guint        undo_ctrl_keycode;
  guint        escape_keycode;
  /* Global F8 / F7 shortcuts for the Pen and Text tools. */
  guint        pen_keycode;
  guint        text_keycode;
  guint        arrow_keycode;
  guint        timer_keycode;
  guint        quick_keycode;

  /* Editable shortcuts, resolved to key values and keycodes. */
  ScreenatorShortcut shortcuts[SCREENATOR_SHORTCUT_COUNT];
  gdouble      opacity;
  guint        pen_width;
  guint        eraser_width;
  GdkRGBA      pen_color;
  GdkRGBA      custom_line_color;
  GdkRGBA      marker_color; /* line colour with marker_opacity for the Marker */
  gdouble      marker_opacity; /* Marker-only opacity, independent of the pen */
  GdkRGBA      marker_saved_color; /* line colour before the Marker was selected */
  gboolean     marker_saved_color_valid;
  gint         pen_tool;   /* 0 = config default, 1 = pen, 2 = arrow, 3 = eraser,
			      4 = text, 5 = rectangle, 6 = ellipse,
			      7 = rounded rectangle, 8 = angled line,
			      9 = free line, 10 = angled arrow, 11 = marker */
  gchar       *font;       /* Pango font description for the Text tool */
  guint        icon_size;
  ScreenatorIconPosition icon_position;
  /* Delay before a newly selected tool becomes active. */
  guint        tool_delay_seconds;
  guint        tool_delay_id;
  gboolean     timer_active;

  /* Number of Screenator control windows (floating icon, settings ring,
   * tool bar) under the pointer.  The drawing tool cursor is replaced by the
   * normal cursor while this is above zero. */
  gint         controls_hover;
  guint        controls_hover_idle;
  guint        tool_grab_retry_id;
  gboolean     force_active_icon;
  /* The icon window was created without an alpha visual, so it keeps a shape
   * mask even if the compositor starts later. */
  gboolean     icon_shaped;

  /* Experimental fallback for drawing while another application holds the
   * pointer grab: XInput2 raw events plus XQueryPointer. */
  int          xi_opcode;
  gboolean     raw_filter_installed;
  gboolean     raw_input_enabled;
  gboolean     raw_button_down;
  GdkDevice   *raw_device;

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
  GdkRGBA              custom_fill_color;

  /* Inline text editor shown at the click position */
  GtkWidget   *text_editor;
  GtkWidget   *text_entry;
  GdkDevice   *text_device;
  gint         text_x;
  gint         text_y;
  gint         text_editor_x;   /* requested editor window position */
  gint         text_editor_y;
  gboolean     text_pressed;
  gboolean     text_editor_active;
  gboolean     text_editor_positioned; /* the editor window has been placed */
  gboolean     text_editor_sized;  /* fixed width chosen for this editor */
  gboolean     text_editor_rtl;    /* editor width was sized for RTL text */
  guint        text_focus_id;
  guint        text_focus_attempts;
  gboolean     text_focus_committed;   /* next paper click follows focus-out commit */
  guint        text_focus_commit_id;
  guint        text_commit_suppress_button;
  /* The text being typed is previewed in the annotation window, above the
   * annotations, because the editor window itself can be hidden behind them.
   * The caret is drawn there too, so it is never hidden by a drawing or the
   * paper page. */
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

  /* Cached paper page background (surround, page and ruling/PDF content) in
   * screen coordinates.  Drawing a stroke only changes annotations, so the
   * background does not have to be rebuilt for every expose. */
  cairo_surface_t *paper_background_cache;
  gboolean     paper_background_cache_valid;

  /* Screen area touched since the last page store in the continuous view.
   * Only the pages intersecting it need their annotation layers updated. */
  GdkRectangle paper_pending_ink_bounds;
  gboolean     paper_pending_ink_valid;
  gboolean     paper_page_dirty;
  gint         paper_view_height_current;
  gdouble      eraser_cursor_scale;

  /* Asynchronous PDF page rendering.  Jobs capture this serial and are
   * discarded if the document or zoom changed in the meantime. */
  gint         pdf_render_serial;

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
  gboolean     hide_drawings;  /* paper stays visible, annotations do not */
  GtkWidget   *about_dialog;
  guint        paper_z_keycode;
  guint        paper_q_keycode;
  guint        paper_t_keycode;
  guint        paper_s_keycode;
  guint        paper_b_keycode;
  guint        paper_a_keycode;
  guint        paper_f3_keycode;
  guint        paper_f4_keycode;
  gboolean     debug;

  /* Paper Drawing documents.  The tools always draw on the shared
   * backbuffer; entering, leaving and switching pages copies the right
   * annotation layer into it.  Plain, Ruled, Graph and PDF each have their own
   * ScreenatorPaperDocument, so every type keeps its own drawings.  paper is
   * the active type, or SCREENATOR_PAPER_NONE outside the mode; paper_selected
   * is the type chosen in the submenu. */
  ScreenatorPaperType paper;
  ScreenatorPaperType paper_selected;
  ScreenatorPaperDocument *paper_docs[SCREENATOR_PAPER_PDF + 1];
  cairo_surface_t *screen_annotations;
  gboolean     screen_annotations_painted;
  gchar       *paper_path;
  gboolean     start_paper;

  /* PDF file and page remembered between runs.  pdf_page_memory maps a
   * canonical PDF path to the last page used for that file. */
  gchar       *paper_pdf_path;
  gint         paper_pdf_page;
  GHashTable  *pdf_page_memory;
  gboolean     paper_saving;

  /* The Hand tool in the paper tool bar: the left mouse button or stylus
   * tip pans the paper instead of drawing. */
  gboolean     paper_hand_tool;

  /* Middle-button / stylus-pen panning in Paper Drawing. */
  gboolean     paper_panning;
  gint         paper_pan_x;
  gint         paper_pan_y;
  guint        paper_pan_button;

  /* Temporary eraser while the right mouse button or stylus side button 2
   * is pressed in Paper Drawing. */
  gboolean     paper_eraser_active;
  GdkDevice   *paper_eraser_device;
  gboolean     paper_eraser_was_grabbed;

  /* Tool bar toggled with F10; horizontal on the top by default. */
  GtkWidget   *paper_top_bar;
  gpointer     paper_top_bar_data;
  guint        paper_toolbar_icon_size;
  gboolean     paper_toolbar_open;
  gboolean     paper_toolbar_vertical;  /* else horizontal */
  gchar       *paper_toolbar_items;     /* visible ids, comma separated */
  gchar       *paper_toolbar_order;     /* all ids in settings-list order */

  /* A desktop file chooser is open; ignore overlay input while it is. */
  gboolean     native_dialog_open;

  /* Screen Annotation uses the fullscreen popup overlay.  Paper mode uses a
   * normal top-level window so other applications and dialogs can cover it
   * and Alt+Tab works normally.  data->win points at the active one. */
  GtkWidget   *overlay_win;
  GtkWidget   *paper_win;
  gboolean     paper_window_fullscreen;
  gboolean     paper_window_mode;
  gboolean     paper_window_focused;

  /* Guards against a dialog-opening menu entry firing twice. */
  gboolean     paper_pdf_chooser_busy;
  gboolean     custom_color_pending;
  guint        custom_color_idle_id;

  /* Paper Drawing settings. */
  gboolean     paper_continuous_scroll;
  ScreenatorPaperSize paper_size;
  gdouble      paper_custom_width_mm;
  gdouble      paper_custom_height_mm;
  /* A one-off screenshot used as the frozen background for a delayed tool
   * when another application owns the pointer grab. */
  cairo_surface_t *screenshot_paper;
  guint        screenshot_close_menu_id;
  guint        screenshot_escape_attempts;
  /* X11 window that had the focus before a paper took it. */
  gulong       paper_prev_focus;

  gchar       *clientdata;

  cairo_surface_t *undobuffer[SCREENATOR_MAX_UNDO];
  gint            undo_head, undo_depth, redo_depth;

  /* Parallel vector snapshots for the active paper page, so undo and redo
   * restore the recorded vector layer instead of falling back to the raster
   * annotation surface. */
  cairo_surface_t *vector_undobuffer[SCREENATOR_MAX_UNDO];
  gboolean         vector_undo_valid[SCREENATOR_MAX_UNDO];

  /* Undo/redo for page insertion and deletion.  Drawing undo takes
   * precedence while its own history is non-empty. */
  GArray         *page_undo_stack;
  GArray         *page_redo_stack;


} ScreenatorData;


void toggle_visibility (ScreenatorData *data);
void toggle_drawings_visibility (ScreenatorData *data);
void hide_window (ScreenatorData *data);
void show_window (ScreenatorData *data);

/* Paper Drawing mode.  The paper background is drawn below the annotations
 * on screen and in a saved PDF. */
void draw_paper_background (cairo_t *cr, ScreenatorPaperType paper,
			    gint width, gint height);
void paper_size_dimensions (ScreenatorData *data, gint *canvas_width,
			    gint *canvas_height, gdouble *pdf_width,
			    gdouble *pdf_height);
void draw_paper_pattern (cairo_t *cr, ScreenatorPaperType paper,
			 gint width, gint height);
void paper_take_focus (ScreenatorData *data);
void paper_restore_focus (ScreenatorData *data);
void screenator_enter_paper_drawing (ScreenatorData *data,
				     ScreenatorPaperType paper);
void screenator_exit_paper_drawing (ScreenatorData *data);
void screenator_toggle_paper_drawing (ScreenatorData *data);
void screenator_paper_select (ScreenatorData *data, ScreenatorPaperType paper);
gboolean screenator_paper_open_pdf (ScreenatorData *data);
void screenator_mark_modified (ScreenatorData *data);
gboolean screenator_open_pdf_path (ScreenatorData *data, const gchar *path);
void screenator_create_paper_window (ScreenatorData *data);
void screenator_invalidate_rect (ScreenatorData *data,
				 const GdkRectangle *rect);
void screenator_resize_drawing_buffer (ScreenatorData *data,
				      gint width, gint height);
void screenator_paper_mark_ink (ScreenatorData *data,
			       const GdkRectangle *bounds);
void screenator_paper_shortcuts_focus (ScreenatorData *data,
				       gboolean focused);
void screenator_use_paper_window (ScreenatorData *data, gboolean use);
void screenator_window_mode_enter (ScreenatorData *data);
void screenator_window_mode_leave (ScreenatorData *data);
void screenator_window_mode_clear (ScreenatorData *data);
void screenator_toggle_paper_window_fullscreen (ScreenatorData *data);
void screenator_toggle_paper_panel (ScreenatorData *data);
gboolean screenator_paper_local_input (ScreenatorData *data);
gdouble screenator_paper_window_scale (ScreenatorData *data);
void screenator_paper_insert_page (ScreenatorData *data, gboolean before);
void screenator_paper_delete_page (ScreenatorData *data);
void screenator_paper_switch_page (ScreenatorData *data, gint delta);
void screenator_paper_goto_page (ScreenatorData *data, gint index);
gboolean screenator_request_quit (ScreenatorData *data);
void screenator_paper_scroll (ScreenatorData *data, gint direction);
void screenator_paper_erase_line (ScreenatorData *data,
				  gint x1, gint y1, gint x2, gint y2,
				  gint width);
gboolean screenator_paper_save (ScreenatorData *data);
gboolean screenator_has_annotations (cairo_surface_t *surface);
void screenator_save_screenshot (ScreenatorData *data);
void reset_undo_history (ScreenatorData *data);
void screenator_marker_prepare (ScreenatorData *data);
void screenator_marker_leave (ScreenatorData *data);
void screenator_schedule_tool (ScreenatorData *data, gint pen_tool,
			       gint shape_type);
void screenator_start_timer (ScreenatorData *data);
void screenator_show_text (ScreenatorData *data, GdkDevice *device,
			   gint x, gint y);
void screenator_show_quick_menu (ScreenatorData *data);
void screenator_toggle_paper_top_bar (ScreenatorData *data);
gboolean screenator_paper_grab_owner_events (ScreenatorData *data);
void screenator_position_icon (ScreenatorData *data);
void screenator_raw_input_enable (ScreenatorData *data);
void screenator_raw_input_disable (ScreenatorData *data);
void screenator_take_screenshot_paper (ScreenatorData *data);
void screenator_clear_screenshot_paper (ScreenatorData *data);
void update_window_shape (ScreenatorData *data);
void text_preview_add_shape (ScreenatorData *data, cairo_region_t *region);
void shape_previews_add_shape (ScreenatorData *data, cairo_region_t *region);

void parse_print_help (gpointer key, gpointer value, gpointer user_data);

void select_tool (ScreenatorData *data, GdkDevice *device, GdkDevice *slave_device, guint state);
void schedule_hints (ScreenatorData *data);
gboolean screenator_exit_tool (ScreenatorData *data);

void apply_pen_settings (ScreenatorData *data);
void set_pen_opacity (ScreenatorData *data, gdouble opacity);
void update_eraser_cursor (ScreenatorData *data);
void refresh_tray_menu (ScreenatorData *data);

void copy_surface (cairo_surface_t *dst, cairo_surface_t *src);
void swap_surfaces (cairo_surface_t *a, cairo_surface_t *b);
void snap_undo_state (ScreenatorData *data);
void undo_drawing (ScreenatorData *data);
void redo_drawing (ScreenatorData *data);
void screenator_undo (ScreenatorData *data);
void screenator_redo (ScreenatorData *data);
void screenator_page_history_clear (ScreenatorData *data);
void screenator_page_redo_clear (ScreenatorData *data);

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
void draw_line_shape (ScreenatorData *data, cairo_t *cr,
		      const ScreenatorLine *line);
void line_finish_all (ScreenatorData *data);
void draw_spline (ScreenatorData *data, cairo_t *cr, GArray *nodes);
void screenator_paper_record_spline (ScreenatorData *data, GArray *nodes);
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
		    const ScreenatorTriangle *triangle, guint vertices);
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
void frame_local_to_screen (const ScreenatorFrame *frame, gdouble lx, gdouble ly,
			    gdouble *x, gdouble *y);
void frame_finish (ScreenatorData *data, ScreenatorDeviceData *devdata);
void frame_finish_all (ScreenatorData *data);
void text_editor_finish (ScreenatorData *data);
gboolean math_expression_is_valid (const gchar *expression, gchar **error);
gboolean fill_bucket (ScreenatorData *data, gint x, gint y);
void clear_screen (ScreenatorData *data);
void remove_saved_annotations (void);
void set_floating_icon_visible (ScreenatorData *data, gboolean show);
void update_paper_shortcuts (ScreenatorData *data, gboolean grab);
void screenator_resolve_shortcuts (ScreenatorData *data);
gboolean screenator_shortcut_matches (ScreenatorData *data,
				      ScreenatorShortcutId id,
				      GdkEventKey *event);
void screenator_apply_shortcuts (ScreenatorData *data,
				 gchar * const *new_texts);

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
