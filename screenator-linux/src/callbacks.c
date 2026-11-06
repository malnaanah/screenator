/* 
 * Gromit-MPX -- a program for painting on the screen
 * Screenator -- a vibe-coded screen annotation tool based on Gromit-MPX 1.4.2
 * Screenator Copyright (C) 2026 Mahmoud Alnaanah <malnaanah@gmail.com>
 *
 *
 * Gromit Copyright (C) 2000 Simon Budig <Simon.Budig@unix-ag.org>
 *
 * Gromit-MPX Copyright (C) 2009,2010 Christian Beier <dontmind@freeshell.org>
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

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <gdk/gdk.h>
#include <glib/gstdio.h>
#ifdef GDK_WINDOWING_X11
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <gdk/gdkx.h>
#ifdef HAVE_XTEST
#include <X11/extensions/XTest.h>
#endif
#endif
#include "screenator.h"
#include "input.h"
#include "callbacks.h"
#include "config.h"
#include "build-config.h"


/* The tray menu shows the same tool and settings entries as the floating
 * icon menu.  Rebuild it when those settings change so the radio items stay
 * in sync.  Rebuilding is deferred so the menu is not destroyed while one of
 * its own items is being activated. */
static gboolean
refresh_tray_menu_idle (gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  data->tray_refresh_id = 0;
  refresh_tray_menu (data);
  return G_SOURCE_REMOVE;
}

static void
schedule_tray_menu_refresh (ScreenatorData *data)
{
  /* Coalesce a burst of changes (for example while the wheel is turned) into
   * one rebuild. */
  if (data->tray_refresh_id)
    return;

  data->tray_refresh_id = g_idle_add (refresh_tray_menu_idle, data);
}

/* ------------------------------------------------------------------
 * Dialogs and message boxes
 * ------------------------------------------------------------------ */

/* The annotation window is an override-redirect popup, and the window manager
 * keeps such windows above the dialogs it manages.  Make the dialogs
 * override-redirect too, so they live in the same layer as the annotation
 * window and can be raised above it.  The drawings and the screen paper then
 * stay visible behind them instead of being hidden.  Nothing centres these
 * dialogs or gives them the keyboard focus, so that is done here as well. */

static void
on_dialog_realize (GtkWidget *widget, gpointer user_data)
{
  GdkWindow *window = gtk_widget_get_window (widget);

  /* The override-redirect flag can only be set before the window is mapped.
   * Doing it when GTK realizes the window keeps every dialog, including the
   * font chooser, on its normal realize path. */
  if (window)
    gdk_window_set_override_redirect (window, TRUE);
}

static void
make_dialog_popup (GtkWidget *dialog)
{
  /* Centring is done when the dialog is mapped, once its content has its
   * final size. */
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_NONE);
  g_signal_connect (dialog, "realize", G_CALLBACK (on_dialog_realize), NULL);
}

static void
dialog_take_focus (ScreenatorData *data, GtkWidget *dialog)
{
#ifdef GDK_WINDOWING_X11
  GdkWindow *window;
  GdkWindow *annotation;
  GdkDisplay *display;
  Display *xdisplay;
  Window focus = None;
  int revert = 0;

  window = gtk_widget_get_window (dialog);
  if (!window || !GDK_IS_X11_WINDOW (window))
    return;

  display = gdk_window_get_display (window);
  xdisplay = GDK_DISPLAY_XDISPLAY (display);
  annotation = data->win ? gtk_widget_get_window (data->win) : NULL;

  /* Remember what had the focus, unless it was the annotation window (the
   * screen paper), which is restored on its own when the dialog closes. */
  if (data->dialog_prev_focus == 0)
    {
      XGetInputFocus (xdisplay, &focus, &revert);
      if (focus != None && focus != PointerRoot &&
	  (!annotation || focus != GDK_WINDOW_XID (annotation)))
	data->dialog_prev_focus = focus;
    }

  gdk_x11_display_error_trap_push (display);
  XSetInputFocus (xdisplay, GDK_WINDOW_XID (window), RevertToParent,
		  CurrentTime);
  XFlush (xdisplay);
  gdk_x11_display_error_trap_pop_ignored (display);
#endif
}

static void
dialog_restore_focus (ScreenatorData *data)
{
  if (data->paper != SCREENATOR_PAPER_NONE)
    {
      /* The screen paper is the active window again. */
      paper_take_focus (data);
      data->dialog_prev_focus = 0;
      return;
    }

#ifdef GDK_WINDOWING_X11
  if (data->dialog_prev_focus)
    {
      GdkWindow *window = data->win ? gtk_widget_get_window (data->win) : NULL;

      if (window && GDK_IS_X11_WINDOW (window))
	{
	  GdkDisplay *display = gdk_window_get_display (window);
	  Display *xdisplay = GDK_DISPLAY_XDISPLAY (display);

	  gdk_x11_display_error_trap_push (display);
	  XSetInputFocus (xdisplay, data->dialog_prev_focus, RevertToParent,
			  CurrentTime);
	  XFlush (xdisplay);
	  gdk_x11_display_error_trap_pop_ignored (display);
	}
    }
#endif
  data->dialog_prev_focus = 0;
}

static gboolean
on_dialog_map (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  GdkWindow *window = gtk_widget_get_window (widget);
  GtkRequisition req;

  gtk_window_present (GTK_WINDOW (widget));

  if (window)
    {
      /* No window manager places an override-redirect window: centre it. */
      gtk_widget_get_preferred_size (widget, NULL, &req);
      gtk_window_move (GTK_WINDOW (widget),
		       MAX (0, (gdk_screen_width () - req.width) / 2),
		       MAX (0, (gdk_screen_height () - req.height) / 2));

      /* The annotation window and the floating icon are raised above the
       * other windows, so the dialog has to be raised again to stay in front
       * of them. */
      gdk_window_raise (window);
    }

  dialog_take_focus (data, widget);

  return FALSE;
}

static void
on_dialog_destroy (GtkWidget *widget, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  GtkWidget *previous;

  previous = g_object_get_data (G_OBJECT (widget),
				"screenator-previous-dialog");
  data->top_dialog = previous;

  /* A dialog opened from another dialog closes above the outer one. */
  if (previous)
    return;

  /* Do not raise the annotation window or the icon here: they were already
   * in their stacking position, and raising the fullscreen annotation window
   * again can put it over the desktop panel and its menus. */

  /* Give the keyboard back to the paper or to the previous application. */
  dialog_restore_focus (data);

  if (data->dialog_released_grab)
    {
      data->dialog_released_grab = FALSE;
      acquire_grab (data, NULL);
    }
}

static void
center_managed_dialog (GtkWidget *widget)
{
  GdkScreen *screen = gtk_widget_get_screen (widget);
  GtkRequisition req;
  GdkRectangle area;
  gint monitor, max_w, max_h;

  if (!screen || !gtk_widget_get_window (widget))
    return;

  monitor = gdk_screen_get_monitor_at_window (screen,
					      gtk_widget_get_window (widget));
  gdk_screen_get_monitor_workarea (screen, monitor, &area);

  max_w = MAX (320, area.width - 40);
  max_h = MAX (240, area.height - 80);

  gtk_widget_get_preferred_size (widget, NULL, &req);
  req.width = MIN (MAX (req.width, MIN (745, max_w)), max_w);
  req.height = MIN (MAX (req.height, MIN (602, max_h)), max_h);
  gtk_window_resize (GTK_WINDOW (widget), req.width, req.height);
  gtk_window_move (GTK_WINDOW (widget),
		   area.x + (area.width - req.width) / 2,
		   area.y + (area.height - req.height) / 2);
}

static gboolean
recenter_hints_idle (gpointer user_data)
{
  ScreenatorData *data = user_data;

  if (data->hints_dialog && gtk_widget_get_visible (data->hints_dialog))
    center_managed_dialog (data->hints_dialog);

  return G_SOURCE_REMOVE;
}

/* A managed dialog is placed by the window manager.  Keep it within the
 * current monitor and let it shrink when the screen is smaller than its
 * contents. */
static gboolean
on_managed_dialog_map (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  gtk_window_present (GTK_WINDOW (widget));
  center_managed_dialog (widget);

  /* At login the panel/work area can settle after the dialog is mapped. */
  g_timeout_add (1500, recenter_hints_idle, data);
  g_timeout_add (4000, recenter_hints_idle, data);

  dialog_take_focus (data, widget);
  return FALSE;
}

static void
show_dialog_common (ScreenatorData *data, GtkWidget *dialog, gboolean popup)
{
  if (popup)
    {
      /* The dialog lives in the same layer as the annotation window, so it
       * can be raised above the drawings and the screen paper. */
      make_dialog_popup (dialog);
    }
  else
    {
      /* The startup hints are a normal managed window, so the window manager
       * can keep them on screen and the user can move or close them. */
      gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
      gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
    }

  /* Release the painting grab for the outermost dialog, so its buttons and
   * entries can be used.  The drawings stay visible behind it. */
  if (!data->top_dialog)
    {
      data->dialog_released_grab = screenator_any_grabbed (data);
      if (data->dialog_released_grab)
	release_grab (data, NULL);
    }

  g_object_set_data (G_OBJECT (dialog), "screenator-previous-dialog",
		     data->top_dialog);
  data->top_dialog = dialog;

  g_signal_connect (dialog, "map-event",
		    G_CALLBACK (popup ? on_dialog_map : on_managed_dialog_map),
		    data);
  g_signal_connect (dialog, "destroy", G_CALLBACK (on_dialog_destroy), data);
}

void
show_dialog_above_drawings (ScreenatorData *data, GtkWidget *dialog)
{
  show_dialog_common (data, dialog, TRUE);
}

static void on_icon_eraser_size (GtkMenuItem *menuitem, gpointer user_data);
static void on_icon_paper (GtkMenuItem *menuitem, gpointer user_data);
static void on_icon_toggle_settings_indicator (GtkMenuItem *menuitem,
					       gpointer user_data);
static GArray *spline_preview_points (ScreenatorDeviceData *devdata);
static void spline_draw_handles (ScreenatorData *data, cairo_t *cr,
				 GArray *nodes, guint count);
static void star_draw_handles (ScreenatorData *data, cairo_t *cr,
			       const ScreenatorStar *star);
static void triangle_draw_handles (ScreenatorData *data, cairo_t *cr,
				   const ScreenatorTriangle *triangle,
				   guint vertices);
static void frame_draw_handles (ScreenatorData *data, cairo_t *cr,
				const ScreenatorFrameShape *shape);
static void frame_invalidate (ScreenatorData *data,
			      const ScreenatorFrameShape *shape);
static void line_draw_handles (ScreenatorData *data, cairo_t *cr,
			       const ScreenatorLine *line);
static void line_invalidate (ScreenatorData *data, const ScreenatorLine *line);
static ScreenatorLineHandle line_handle_at (const ScreenatorLine *line,
					    gint x, gint y);
static gboolean line_contains (const ScreenatorLine *line, gint x, gint y,
			       gint width);
static void line_begin (ScreenatorDeviceData *devdata,
			ScreenatorShapeType type, gint x, gint y);
static void line_begin_drag (ScreenatorDeviceData *devdata,
			     ScreenatorLineHandle handle, gint x, gint y);
static void line_move_handle (ScreenatorData *data,
			      ScreenatorDeviceData *devdata,
			      gint x, gint y, gboolean snap);
static gboolean text_preview_shape (ScreenatorData *data,
				    ScreenatorFrameShape *shape);
static void text_preview_refresh (ScreenatorData *data);
static void text_preview_clear (ScreenatorData *data);
static gboolean text_caret_bounds (ScreenatorData *data, GdkRectangle *rect);
static gint text_editor_line_height (ScreenatorData *data);
static void icon_grab_pointer (ScreenatorData *data);
static void icon_ungrab_pointer (ScreenatorData *data);
static void icon_cancel_menu_retry (ScreenatorData *data);
static gboolean on_icon_grab_broken (GtkWidget *widget,
				     GdkEventGrabBroken *event,
				     gpointer user_data);
static gboolean event_source_is_stylus (const GdkEvent *event);
static void apply_scroll_step (ScreenatorData *data, gboolean down,
			       gboolean colors_only);
static void apply_stylus_step (ScreenatorData *data, gboolean down);
static void show_stylus_menu (ScreenatorData *data,
			      ScreenatorDeviceData *devdata,
			      gint x, gint y);
static void show_wheel_menu (ScreenatorData *data,
			     ScreenatorDeviceData *devdata,
			     gint x, gint y);
static void show_marker_menu (ScreenatorData *data,
			      ScreenatorDeviceData *devdata,
			      gint x, gint y);
static void show_hints_dialog (ScreenatorData *data, gboolean startup);
static void on_show_hints (GtkMenuItem *menuitem, gpointer user_data);
static void activate_tool_now (ScreenatorData *data, gint pen_tool,
			       gint shape_type);
static gboolean eraser_tool_active (ScreenatorData *data);
static gboolean marker_tool_active (ScreenatorData *data);


static void
draw_screenshot_paper (ScreenatorData *data, cairo_t *cr)
{
  if (!data->screenshot_paper)
    return;

  cairo_save (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface (cr, data->screenshot_paper, 0, 0);
  cairo_paint (cr);
  cairo_restore (cr);
}

gboolean on_expose (GtkWidget *widget,
		    cairo_t* cr,
		    gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  GHashTableIter it;
  gpointer value;

  if(data->debug)
    g_printerr("DEBUG: got draw event\n");

  /* The visibility toggle hides the drawings but keeps the screen paper. */
  if (data->hide_drawings)
    {
      if (data->screenshot_paper)
	draw_screenshot_paper (data, cr);
      else if (data->paper != SCREENATOR_PAPER_NONE)
	draw_screen_paper (data, cr);

      return TRUE;
    }

  /* The screen paper is drawn first, so it stays below the annotations and is
   * not part of the backbuffer: clearing the screen leaves it in place. */
  if (data->screenshot_paper)
    {
      draw_screenshot_paper (data, cr);

      cairo_save (cr);
      cairo_set_source_surface (cr, data->backbuffer, 0, 0);
      cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
      cairo_paint (cr);
      cairo_restore (cr);
    }
  else if (data->paper != SCREENATOR_PAPER_NONE)
    {
      draw_screen_paper (data, cr);

      cairo_save (cr);
      cairo_set_source_surface (cr, data->backbuffer, 0, 0);
      cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
      cairo_paint (cr);
      cairo_restore (cr);
    }
  else
    {
      cairo_save (cr);
      cairo_set_source_surface (cr, data->backbuffer, 0, 0);
      cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
      cairo_paint (cr);
      cairo_restore (cr);
    }

  /* Draw translucent strokes that are still in progress on top of the
   * backbuffer, so they look the same as after they are committed. */
  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      ScreenatorDeviceData *devdata = value;

      if (!devdata->strokebuffer)
	continue;

      cairo_save (cr);
      cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
      cairo_set_source_surface (cr, devdata->strokebuffer, 0, 0);
      cairo_paint_with_alpha (cr, devdata->stroke_alpha);
      cairo_restore (cr);
    }

  /* Draw the shape that is currently being dragged on top of the
   * backbuffer.  It is only committed on button release. */
  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      ScreenatorDeviceData *devdata = value;

      if (!devdata->shape_active && !devdata->star_active &&
	  !devdata->triangle_active && !devdata->frame_active &&
	  !devdata->line_active)
	continue;

      cairo_save (cr);
      cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
      if (data->shape_type == SCREENATOR_SHAPE_STAR && devdata->star_active)
	{
	  draw_star (data, cr, &devdata->star);
	  star_draw_handles (data, cr, &devdata->star);
	}
      else if (data->shape_type == SCREENATOR_SHAPE_TRIANGLE &&
	       devdata->triangle_active)
	{
	  draw_triangle (data, cr, &devdata->triangle,
			 devdata->triangle_vertices);
	  triangle_draw_handles (data, cr, &devdata->triangle,
				 devdata->triangle_vertices);
	}
      else if (devdata->line_active)
	{
	  draw_line_shape (data, cr, &devdata->line);
	  line_draw_handles (data, cr, &devdata->line);
	}
      else if (devdata->frame_active)
	{
	  draw_frame_shape (data, cr, &devdata->frame_shape);
	  frame_draw_handles (data, cr, &devdata->frame_shape);
	}
      else if (data->shape_type == SCREENATOR_SHAPE_SPLINE)
	{
	  GArray *points = spline_preview_points (devdata);
	  guint count = devdata->shape_points ? devdata->shape_points->len : 0;

	  draw_spline (data, cr, points);
	  spline_draw_handles (data, cr, points, count);
	  g_array_free (points, TRUE);
	}
      else if (devdata->shape_active)
	{
	  if (data->shape_type == SCREENATOR_SHAPE_MATH ||
	      data->shape_type == SCREENATOR_SHAPE_BARS ||
	      data->shape_type == SCREENATOR_SHAPE_RANDOM)
	    {
	      /* The math plot, parallel lines and random signal have no plain
	       * outline, so preview the frame with its real contents. */
	      ScreenatorFrameShape preview;

	      memset (&preview, 0, sizeof (preview));
	      preview.type = data->shape_type;
	      preview.extra = 5.0;
	      preview.seed = devdata->frame_seed;
	      preview.polar = data->math_polar;
	      preview.expression = data->math_polar
		? data->math_polar_expression : data->math_expression;
	      preview.xmin = data->math_polar
		? data->math_theta_min : data->math_xmin;
	      preview.xmax = data->math_polar
		? data->math_theta_max : data->math_xmax;
	      preview.count = 5;
	      preview.frame.cx = (devdata->shape_x0 + devdata->shape_x1) / 2.0;
	      preview.frame.cy = (devdata->shape_y0 + devdata->shape_y1) / 2.0;
	      preview.frame.w = MAX (ABS (devdata->shape_x1 - devdata->shape_x0), 4);
	      preview.frame.h = MAX (ABS (devdata->shape_y1 - devdata->shape_y0), 4);

	      draw_frame_shape (data, cr, &preview);
	    }
	  else
	    draw_shape (data, cr, data->shape_type,
			devdata->shape_x0, devdata->shape_y0,
			devdata->shape_x1, devdata->shape_y1);
	}
      cairo_restore (cr);
    }

  /* The text being typed is previewed above the annotations, so it stays
   * visible even inside a filled shape.  The editor window only supplies the
   * input; the caret is drawn here as well so drawings and the screen paper
   * never hide it. */
  if (data->text_preview_valid)
    {
      ScreenatorFrameShape shape;

      if (text_preview_shape (data, &shape))
	{
	  cairo_save (cr);
	  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	  draw_frame_shape (data, cr, &shape);
	  cairo_restore (cr);
	}

      frame_shape_clear (&shape);
    }

  if (data->text_editor && data->text_caret_visible)
    {
      GdkRectangle caret;

      if (text_caret_bounds (data, &caret))
	{
	  cairo_save (cr);
	  cairo_set_source_rgba (cr, data->pen_color.red,
				 data->pen_color.green,
				 data->pen_color.blue,
				 data->opacity);
	  cairo_rectangle (cr, caret.x, caret.y, caret.width, caret.height);
	  cairo_fill (cr);
	  cairo_restore (cr);
	}
    }

  return TRUE;
}




gboolean on_configure (GtkWidget *widget,
		       GdkEventExpose *event,
		       gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if(data->debug)
    g_printerr("DEBUG: got configure event\n");

  return TRUE;
}



void on_screen_changed(GtkWidget *widget,
		       GdkScreen *previous_screen,
		       gpointer   user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if(data->debug)
    g_printerr("DEBUG: got screen-changed event\n");

  GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET (widget));
  GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
  if (visual == NULL)
    visual = gdk_screen_get_system_visual (screen);

  gtk_widget_set_visual (widget, visual);
}



void on_monitors_changed ( GdkScreen *screen,
			   gpointer   user_data) 
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  // get new sizes
  data->width = gdk_screen_get_width (data->screen);
  data->height = gdk_screen_get_height (data->screen);

  if(data->debug)
    g_printerr("DEBUG: screen size changed to %d x %d!\n", data->width, data->height);

  // change size
  gtk_widget_set_size_request(GTK_WIDGET(data->win), data->width, data->height);
  // try to set transparent for input
  cairo_region_t* r =  cairo_region_create();
  gtk_widget_input_shape_combine_region(data->win, r);
  cairo_region_destroy(r);

  /* recreate the shape surface */
  cairo_surface_t *new_shape = cairo_image_surface_create(CAIRO_FORMAT_ARGB32 ,data->width, data->height);
  cairo_t *cr = cairo_create (new_shape);
  cairo_set_source_surface (cr, data->backbuffer, 0, 0);
  cairo_paint (cr);
  cairo_destroy (cr);
  cairo_surface_destroy(data->backbuffer);
  data->backbuffer = new_shape;
 
  /*
     these depend on the shape surface
  */
  GHashTableIter it;
  gpointer value;
  g_hash_table_iter_init (&it, data->tool_config);
  while (g_hash_table_iter_next (&it, NULL, &value)) 
    paint_context_free(value);
  g_hash_table_remove_all(data->tool_config);


  parse_config(data); // also calls paint_context_new() :-(


  data->default_pen = paint_context_new (data, SCREENATOR_PEN,
					 data->red, 7, 0, 1);
  data->default_eraser = paint_context_new (data, SCREENATOR_ERASER,
					    data->red, data->eraser_width, 0, 1);
  data->default_text = paint_context_new (data, SCREENATOR_TEXT,
					  data->red, 7, 0, 1);
  data->default_shape = paint_context_new (data, SCREENATOR_SHAPE,
					   data->red, 7, 0, 1);
  data->default_marker = paint_context_new (data, SCREENATOR_PEN,
					    &data->marker_color, 32, 0, 32);

  /* The pen was recreated on the new surface: re-apply the settings chosen
   * in the icon menu and point the selected tool at the new context. */
  apply_pen_settings (data);

  update_window_shape (data);

  /* Keep the floating icon at the configured corner of its monitor after a
   * resolution change. */
  screenator_position_icon (data);

  setup_input_devices(data);

  gtk_widget_show_all (data->win);

  /* Showing the annotation window again puts it above the floating icon. */
  raise_icon_window (data);
}



static void
recreate_icon_windows_for_compositing (ScreenatorData *data)
{
  GtkWidget *icon = data->icon_win;
  GtkWidget *settings = data->settings_win;

  if (!icon || data->icon_pointer_grabbed || data->icon_menu_pending ||
      data->top_dialog)
    return;

  data->icon_win = NULL;
  data->icon_image = NULL;
  data->settings_win = NULL;

  if (settings)
    gtk_widget_destroy (settings);
  gtk_widget_destroy (icon);

  create_icon_window (data);
}

void on_composited_changed ( GdkScreen *screen,
			   gpointer   user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if(data->debug)
    g_printerr("DEBUG: got composited-changed event\n");

  data->composited = gdk_screen_is_composited (data->screen);

  if (data->icon_win &&
      ((data->composited && data->icon_shaped) ||
       (!data->composited && !data->icon_shaped)))
    recreate_icon_windows_for_compositing (data);

  if(data->composited)
    {
      // undo shape
      gtk_widget_shape_combine_region(data->win, NULL);
      // the window itself stays opaque; only the strokes carry the opacity
      gtk_widget_set_opacity(data->win, 1.0);
    }

  /* The icon window keeps the visual it was created with.  Refresh its
   * pixbuf and shape so a compositor that starts after login cannot leave a
   * stale shape mask around the old icon size. */
  update_icon_image (data);

  // set anti-aliasing
  GHashTableIter it;
  gpointer value;
  g_hash_table_iter_init (&it, data->tool_config);
  while (g_hash_table_iter_next (&it, NULL, &value)) 
    {
      ScreenatorPaintContext *context = value;
      cairo_set_antialias(context->paint_ctx, data->composited ? CAIRO_ANTIALIAS_DEFAULT : CAIRO_ANTIALIAS_NONE);
    }
      

  GdkRectangle rect = {0, 0, data->width, data->height};
  gdk_window_invalidate_rect(gtk_widget_get_window(data->win), &rect, 0); 
}



void on_clientapp_selection_get (GtkWidget          *widget,
				 GtkSelectionData   *selection_data,
				 guint               info,
				 guint               time,
				 gpointer            user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  
  gchar *ans = "";

  if(data->debug)
    g_printerr("DEBUG: clientapp received request.\n");  

  if (gtk_selection_data_get_target(selection_data) == GA_TOGGLEDATA)
    {
      ans = data->clientdata;
    }
    
  gtk_selection_data_set (selection_data,
                          gtk_selection_data_get_target(selection_data),
                          8, (guchar*)ans, strlen (ans));
}


void on_clientapp_selection_received (GtkWidget *widget,
				      GtkSelectionData *selection_data,
				      guint time,
				      gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  /* If someone has a selection for us, Gromit is already running. */

  if(gtk_selection_data_get_data_type(selection_data) == GDK_NONE)
    data->client = 0;
  else
    data->client = 1;

  gtk_main_quit ();
}





/* Inline text editor
 *
 * The Text tool writes directly on screen: a small, borderless entry is
 * placed at the click position, using the selected font and pen colour.
 * Return commits the text, Escape cancels it, and moving the focus away
 * commits what has been typed so far.  The text is previewed in the
 * annotation window, above the annotations, because the editor window itself
 * sits below the fullscreen annotation window and a filled shape would hide
 * it; the entry supplies the input and the caret.  After it is committed the
 * string is rendered into the annotation layer.
 */

/* Root position of the editor's text layout.  The committed text is placed
 * there, so the preview uses the same point. */
static void
text_editor_origin (ScreenatorData *data, gint *x, gint *y)
{
  GtkAllocation allocation;
  gint layout_x = 0, layout_y = 0;

  *x = data->text_x;
  *y = data->text_y;

  if (!data->text_entry)
    return;

  /* Right-to-left text ends at the click.  Computing the origin from the
   * text ink avoids the first character being drawn at the left edge before
   * the widened RTL entry has been allocated. */
  if (data->text_editor_rtl)
    {
      PangoLayout *layout = gtk_entry_get_layout (GTK_ENTRY (data->text_entry));

      if (layout)
	{
	  PangoRectangle ink;

	  pango_layout_get_pixel_extents (layout, &ink, NULL);
	  *x = data->text_x - ink.x - MAX (ink.width, 1);
	  *y = data->text_y - text_editor_line_height (data) / 2;
	  return;
	}
    }

  gtk_widget_get_allocation (data->text_entry, &allocation);
  gtk_entry_get_layout_offsets (GTK_ENTRY (data->text_entry),
				&layout_x, &layout_y);
  /* Use the position the editor was asked to take, not the window manager's
   * current position: the latter lags behind, so the first caret was drawn at
   * the click and left a static line below the centred one. */
  if (data->text_editor_positioned)
    {
      *x = data->text_editor_x + allocation.x + layout_x;
      *y = data->text_editor_y + allocation.y + layout_y;
    }
  else
    {
      *x = data->text_x;
      *y = data->text_y;
    }
}

/* Build the frame that the text in the editor will become.  The preview is
 * drawn with the same code as a committed text frame, so it lines up exactly
 * when the text is committed.  Returns FALSE for empty text. */
static gboolean
text_preview_shape (ScreenatorData *data, ScreenatorFrameShape *shape)
{
  const gchar *text = NULL;
  gint x, y;

  memset (shape, 0, sizeof (*shape));

  if (data->text_entry)
    text = gtk_entry_get_text (GTK_ENTRY (data->text_entry));

  if (!text || !*text)
    return FALSE;

  shape->type = SCREENATOR_SHAPE_TEXT;
  shape->text = g_strdup (text);
  shape->font = g_strdup (data->font ? data->font : KEY_DFLT_FONT);
  shape->font_size = frame_font_pixel_size (shape->font);
  frame_shape_measure_text (shape);

  text_editor_origin (data, &x, &y);
  shape->frame.cx = x + shape->text_off_x + shape->frame.w / 2.0;
  shape->frame.cy = y + shape->text_off_y + shape->frame.h / 2.0;
  shape->frame.rotation = 0.0;

  shape->color = data->pen_color;
  shape->color.alpha = 1.0;

  return TRUE;
}

/* Repaint the area of the old and the new typing preview. */
static void
text_preview_refresh (ScreenatorData *data)
{
  GdkRectangle old_bounds = data->text_preview_bounds;
  GdkRectangle new_bounds;
  ScreenatorFrameShape shape;
  GdkWindow *window;
  gboolean has_new;

  has_new = text_preview_shape (data, &shape);

  if (has_new)
    frame_shape_get_bounds (&shape, 2, &new_bounds);

  frame_shape_clear (&shape);

  window = data->win ? gtk_widget_get_window (data->win) : NULL;

  if (!window)
    {
      data->text_preview_valid = FALSE;
      return;
    }

  if (data->text_preview_valid)
    gdk_window_invalidate_rect (window, &old_bounds, 0);

  if (has_new)
    gdk_window_invalidate_rect (window, &new_bounds, 0);

  /* The caret moves with the text. */
  {
    GdkRectangle caret;

    if (text_caret_bounds (data, &caret))
      gdk_window_invalidate_rect (window, &caret, 0);
  }

  data->text_preview_valid = has_new;
  if (has_new)
    data->text_preview_bounds = new_bounds;

  /* On a shaped (non-composited) window the preview has to be part of the
   * window shape to stay visible. */
  update_window_shape (data);
}

/* Remove the typing preview from the screen. */
static void
text_preview_clear (ScreenatorData *data)
{
  GdkWindow *window = data->win ? gtk_widget_get_window (data->win) : NULL;

  if (data->text_preview_valid && window)
    gdk_window_invalidate_rect (window, &data->text_preview_bounds, 0);

  data->text_preview_valid = FALSE;
}

/* Area of the typing caret in the annotation window.  The editor is placed so
 * that the layout origin of its entry is the click position, so the caret
 * position from the entry's own layout can be drawn on the annotation window
 * as well.  That keeps the caret visible above drawings and the screen
 * paper, which would otherwise cover the editor window. */
static gboolean
text_caret_bounds (ScreenatorData *data, GdkRectangle *rect)
{
  GtkEntry *entry;
  PangoLayout *layout;
  PangoRectangle pos;
  const gchar *text;
  gint cursor, byte_index;
  gint origin_x, origin_y;
  gint line_height = 0;

  if (!data->text_entry)
    return FALSE;

  entry = GTK_ENTRY (data->text_entry);
  layout = gtk_entry_get_layout (entry);
  if (!layout)
    return FALSE;

  cursor = gtk_editable_get_position (GTK_EDITABLE (entry));
  text = gtk_entry_get_text (entry);
  if (text && cursor > 0)
    byte_index = (gint) (g_utf8_offset_to_pointer (text, cursor) - text);
  else
    byte_index = 0;

  pango_layout_index_to_pos (layout, byte_index, &pos);

  /* The layout origin is the click position for left-to-right text and the
   * right edge of the editor for right-to-left text, exactly like the text
   * preview.  An empty layout still has a line height, so the caret is
   * visible before the first character is typed. */
  text_editor_origin (data, &origin_x, &origin_y);
  pango_layout_get_pixel_size (layout, NULL, &line_height);

  if (data->text_editor_rtl)
    {
      /* Pango reports the position from the left of the layout.  In RTL text
       * the insertion point grows to the left, so mirror it inside the text
       * width: the end of the text is the left edge, not the right one. */
      gint lw = 0;

      pango_layout_get_pixel_size (layout, &lw, NULL);
      rect->x = origin_x + lw - pos.x / PANGO_SCALE;
    }
  else
    rect->x = origin_x + pos.x / PANGO_SCALE;
  rect->y = origin_y + pos.y / PANGO_SCALE;
  rect->width = 2;
  rect->height = MAX (MAX (pos.height / PANGO_SCALE, line_height), 1);

  return TRUE;
}

/* Blink the caret drawn in the annotation window. */
static gboolean
text_caret_blink (gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  GdkWindow *window;
  GdkRectangle caret;

  if (!data->text_editor)
    {
      data->text_caret_timeout = 0;
      return G_SOURCE_REMOVE;
    }

  data->text_caret_visible = !data->text_caret_visible;

  window = data->win ? gtk_widget_get_window (data->win) : NULL;
  if (window && text_caret_bounds (data, &caret))
    gdk_window_invalidate_rect (window, &caret, 0);

  /* On a screen without a compositor the annotation window is shaped, so the
   * caret area has to enter and leave the shape together with the blink. */
  update_window_shape (data);

  return G_SOURCE_CONTINUE;
}

/* Add the pixels of the typing preview and its caret to the annotation
 * window shape.  Without a compositor the window is only visible where this
 * region says, and the preview is drawn on the window but is not part of the
 * backbuffer.  Adding the glyph pixels (not the whole text rectangle) keeps
 * the desktop visible around the text. */
void
text_preview_add_shape (ScreenatorData *data, cairo_region_t *region)
{
  ScreenatorFrameShape shape;
  GdkRectangle bounds;
  cairo_surface_t *surface;
  cairo_region_t *ink;
  cairo_t *cr;

  if (!data->text_editor)
    return;

  if (text_preview_shape (data, &shape))
    {
      frame_shape_get_bounds (&shape, 2, &bounds);

      if (bounds.width > 0 && bounds.height > 0)
	{
	  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
						bounds.width, bounds.height);
	  cr = cairo_create (surface);
	  cairo_translate (cr, -bounds.x, -bounds.y);
	  draw_frame_shape (data, cr, &shape);
	  cairo_destroy (cr);

	  ink = gdk_cairo_region_create_from_surface (surface);
	  cairo_region_translate (ink, bounds.x, bounds.y);
	  cairo_region_union (region, ink);
	  cairo_region_destroy (ink);
	  cairo_surface_destroy (surface);
	}

      frame_shape_clear (&shape);
    }

  if (data->text_caret_visible)
    {
      GdkRectangle caret;

      if (text_caret_bounds (data, &caret))
	{
	  cairo_rectangle_int_t rect;

	  rect.x = caret.x;
	  rect.y = caret.y;
	  rect.width = caret.width;
	  rect.height = caret.height;
	  cairo_region_union_rectangle (region, &rect);
	}
    }
}

/* Add the ink of an active frame (text, ellipse, rounded rectangle, math plot
 * or parallel lines) to the window shape.  Such a frame is drawn as a preview
 * until it is committed, so on a screen without a compositor it would be
 * clipped away without this. */
static void
frame_preview_add_shape (ScreenatorData *data, cairo_region_t *region,
			 const ScreenatorFrameShape *shape)
{
  GdkRectangle bounds;
  cairo_surface_t *surface;
  cairo_region_t *ink;
  cairo_t *cr;

  frame_shape_get_bounds (shape, 2, &bounds);
  if (bounds.width <= 0 || bounds.height <= 0)
    return;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
					bounds.width, bounds.height);
  cr = cairo_create (surface);
  cairo_translate (cr, -bounds.x, -bounds.y);
  draw_frame_shape (data, cr, shape);
  frame_draw_handles (data, cr, shape);
  cairo_destroy (cr);

  ink = gdk_cairo_region_create_from_surface (surface);
  cairo_region_translate (ink, bounds.x, bounds.y);
  cairo_region_union (region, ink);
  cairo_region_destroy (ink);
  cairo_surface_destroy (surface);
}

void
shape_previews_add_shape (ScreenatorData *data, cairo_region_t *region)
{
  GHashTableIter it;
  gpointer value;

  if (!data->devdatatable)
    return;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      ScreenatorDeviceData *devdata = value;

      if (devdata->frame_active)
	frame_preview_add_shape (data, region, &devdata->frame_shape);
    }
}


static gboolean
on_text_editor_draw (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
  cairo_save (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba (cr, 0, 0, 0, 0);
  cairo_paint (cr);
  cairo_restore (cr);

  return FALSE;
}

/* Height of one line of text in the editor.  The text is centred vertically on
 * the click position, so the click lands in the middle of the caret. */
static gint
text_editor_line_height (ScreenatorData *data)
{
  gint height = 0;

  if (data->text_entry)
    pango_layout_get_pixel_size (gtk_entry_get_layout (GTK_ENTRY (data->text_entry)),
				 NULL, &height);

  return height;
}

/* Repaint the area around the click, clearing any caret that was drawn
 * before the editor reached its final position. */
static void
invalidate_text_area (ScreenatorData *data, gint x, gint y)
{
  GdkRectangle area;

  if (!data->win || !gtk_widget_get_window (data->win))
    return;

  area.x = MAX (0, x - 20);
  area.y = MAX (0, y - 80);
  area.width = 340;
  area.height = 180;
  gdk_window_invalidate_rect (gtk_widget_get_window (data->win), &area, 0);
}

/* Place the small empty editor at the press position.  The entry keeps a
 * little padding and centres the text vertically, so offset the window by the
 * layout inset instead of putting its corner on the pointer.  The width is
 * fixed later, once the first characters reveal the script direction. */
static void
position_text_editor (ScreenatorData *data)
{
  GtkRequisition req;
  GtkAllocation allocation;
  gint layout_x = 0, layout_y = 0;
  gint x, y;

  if (!data->text_editor || !data->text_entry)
    return;

  gtk_widget_get_preferred_size (data->text_editor, NULL, &req);
  gtk_widget_get_allocation (data->text_entry, &allocation);
  gtk_entry_get_layout_offsets (GTK_ENTRY (data->text_entry),
				&layout_x, &layout_y);

  x = data->text_x - allocation.x - layout_x;
  y = data->text_y - text_editor_line_height (data) / 2
      - allocation.y - layout_y;

  if (x + req.width > (gint) data->width)
    x = (gint) data->width - req.width;
  if (y + req.height > (gint) data->height)
    y = (gint) data->height - req.height;
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;

  data->text_editor_x = x;
  data->text_editor_y = y;
  gtk_window_move (GTK_WINDOW (data->text_editor), x, y);
  data->text_editor_positioned = TRUE;

  /* The annotation window has to be visible where the preview is drawn. */
  update_window_shape (data);
}

/* Choose the editor geometry once the script direction is known.  The entry
 * is stretched from the click position to the screen edge the text grows
 * towards: leftwards for RTL, rightwards for LTR.  The editor then keeps
 * that size for the rest of the session.  Growing the window on every
 * keystroke makes it move while the text is typed, which makes right-to-left
 * text jitter visibly. */
static void
size_text_editor (ScreenatorData *data, PangoDirection direction)
{
  gint available;
  gint layout_x = 0, layout_y = 0;

  if (!data->text_editor || !data->text_entry)
    return;

  if (direction == PANGO_DIRECTION_RTL)
    available = data->text_x;
  else
    available = (gint) data->width - data->text_x;
  available = MAX (available, 1);

  gtk_widget_set_size_request (data->text_entry, available, -1);

  gtk_entry_get_layout_offsets (GTK_ENTRY (data->text_entry),
				&layout_x, &layout_y);

  /* Remember the position that was requested: text_editor_origin() uses it
   * instead of the window manager's lagging position. */
  data->text_editor_x = direction == PANGO_DIRECTION_RTL
    ? data->text_x - available
    : data->text_x;
  data->text_editor_y = data->text_y - text_editor_line_height (data) / 2
    - layout_y;

  gtk_window_move (GTK_WINDOW (data->text_editor),
		   data->text_editor_x, data->text_editor_y);

  data->text_editor_sized = TRUE;
  data->text_editor_rtl = (direction == PANGO_DIRECTION_RTL);

  /* The annotation window has to be visible where the preview is drawn. */
  update_window_shape (data);
}

static void
on_text_editor_changed (GtkEditable *editable, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  const gchar *text;
  PangoDirection direction;

  if (!data->text_entry)
    return;

  text = gtk_entry_get_text (GTK_ENTRY (data->text_entry));
  if (!text || !*text)
    {
      /* Remove the preview of the text that was just deleted. */
      text_preview_refresh (data);
      return;
    }

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  direction = pango_find_base_dir (text, -1);
G_GNUC_END_IGNORE_DEPRECATIONS
  if (direction != PANGO_DIRECTION_RTL)
    direction = PANGO_DIRECTION_LTR;

  if (!data->text_editor_sized ||
      (direction == PANGO_DIRECTION_RTL) != data->text_editor_rtl)
    size_text_editor (data, direction);

  text_preview_refresh (data);
}

/* Turn a committed string into an editable text frame.  The click position is
 * the layout origin, so the text stays where the editor showed it; the frame
 * is centred on the ink box, so the handles rotate it around its middle. */
static void
begin_text_frame (ScreenatorData *data, GdkDevice *device,
		  gint x, gint y, const gchar *text)
{
  ScreenatorDeviceData *devdata =
    g_hash_table_lookup (data->devdatatable, device);
  ScreenatorPaintContext *context = devdata ? devdata->cur_context : NULL;
  ScreenatorFrameShape *shape;

  if (!devdata || !text || !*text)
    return;

  if (!context || context->type != SCREENATOR_TEXT)
    context = data->default_text;

  /* A text that was still editable is committed before the next one. */
  frame_finish (data, devdata);

  shape = &devdata->frame_shape;
  frame_shape_clear (shape);

  shape->type = SCREENATOR_SHAPE_TEXT;
  shape->text = g_strdup (text);
  shape->font = g_strdup (data->font ? data->font : KEY_DFLT_FONT);
  shape->font_size = frame_font_pixel_size (shape->font);
  frame_shape_measure_text (shape);

  shape->frame.cx = x + shape->text_off_x + shape->frame.w / 2.0;
  shape->frame.cy = y + shape->text_off_y + shape->frame.h / 2.0;
  shape->frame.rotation = 0.0;

  if (context && context->paint_color)
    shape->color = *context->paint_color;
  else
    shape->color = data->pen_color;

  /* The context already carries the current opacity; the text frame applies
   * the global opacity when it is drawn, so keep the colour itself opaque. */
  shape->color.alpha = 1.0;

  devdata->frame_active = TRUE;
  devdata->frame_drag = SCREENATOR_FRAME_HANDLE_NONE;

  frame_invalidate (data, shape);
}

static void
close_text_editor_ex (ScreenatorData *data, gboolean commit, gboolean reacquire)
{
  GtkWidget *editor = data->text_editor;

  GtkWidget *entry = data->text_entry;
  GdkDevice *device = data->text_device;
  gchar *text = NULL;
  gint x = data->text_x;
  gint y = data->text_y;

  if (!editor)
    return;

  /* Remove the blinking caret from the annotation window before the entry is
   * gone, so no stale caret is left behind. */
  if (data->text_caret_timeout)
    {
      g_source_remove (data->text_caret_timeout);
      data->text_caret_timeout = 0;
    }

  if (data->text_caret_visible)
    {
      GdkRectangle caret;
      GdkWindow *window = data->win ? gtk_widget_get_window (data->win) : NULL;

      data->text_caret_visible = FALSE;
      if (window && text_caret_bounds (data, &caret))
	gdk_window_invalidate_rect (window, &caret, 0);
    }

  if (data->text_focus_id)
    {
      g_source_remove (data->text_focus_id);
      data->text_focus_id = 0;
    }

  data->text_editor = NULL;
  data->text_entry = NULL;
  data->text_editor_active = FALSE;
  data->text_device = NULL;

  if (commit && entry)
    {
      GtkAllocation allocation;
      gint layout_x = 0, layout_y = 0;
      GdkWindow *window;

      text = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));

      /* Line the committed text up with the previewed text. */
      gtk_widget_get_allocation (entry, &allocation);
      gtk_entry_get_layout_offsets (GTK_ENTRY (entry), &layout_x, &layout_y);
      window = gtk_widget_get_window (entry);
      if (window)
	gdk_window_get_root_coords (window,
				    allocation.x + layout_x,
				    allocation.y + layout_y,
				    &x, &y);
    }

  invalidate_text_area (data, x, y);
  gtk_widget_destroy (editor);

  /* The editor area no longer has to stay visible on the annotation window. */
  update_window_shape (data);

  /* The committed frame replaces the preview; drop it first so the frame is
   * what gets drawn on the next expose. */
  text_preview_clear (data);

  if (text && *text)
    begin_text_frame (data, device, x, y, text);
  g_free (text);

  if (reacquire)
    acquire_grab (data, NULL);

  /* A paper is the active window again after the editor closes. */
  if (data->paper != SCREENATOR_PAPER_NONE)
    paper_take_focus (data);
}

static void
close_text_editor (ScreenatorData *data, gboolean commit)
{
  close_text_editor_ex (data, commit, TRUE);
}

/* Commit a text that is still being typed when painting mode stops.  Called
 * from release_grab(), so the grab must not be acquired again here. */
void
text_editor_finish (ScreenatorData *data)
{
  if (data->text_editor)
    close_text_editor_ex (data, TRUE, FALSE);
}

static void
on_text_editor_activate (GtkEntry *entry, gpointer user_data)
{
  close_text_editor ((ScreenatorData *) user_data, TRUE);
}

static gboolean
on_text_editor_key_press (GtkWidget *widget, GdkEventKey *event,
			  gpointer user_data)
{
  if (event->keyval == GDK_KEY_Escape)
    {
      close_text_editor ((ScreenatorData *) user_data, FALSE);
      return TRUE;
    }

  return FALSE;
}

static gboolean
on_text_editor_focus_in (GtkWidget *widget, GdkEventFocus *event,
			 gpointer user_data)
{
  ((ScreenatorData *) user_data)->text_editor_active = TRUE;
  return FALSE;
}

static void
on_text_editor_destroy (GtkWidget *widget, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if (data->text_editor == widget)
    {
      if (data->text_caret_timeout)
	{
	  g_source_remove (data->text_caret_timeout);
	  data->text_caret_timeout = 0;
	}
      data->text_caret_visible = FALSE;
      if (data->text_focus_id)
	{
	  g_source_remove (data->text_focus_id);
	  data->text_focus_id = 0;
	}
      data->text_editor = NULL;
      data->text_entry = NULL;
      data->text_editor_active = FALSE;
      data->text_device = NULL;
      text_preview_clear (data);
    }
}

static gboolean
on_text_editor_focus_out (GtkWidget *widget, GdkEventFocus *event,
			  gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if (data->text_editor_active && data->text_editor)
    close_text_editor (data, TRUE);

  return FALSE;
}

static gboolean
text_editor_focus_retry (gpointer user_data)
{
  ScreenatorData *data = user_data;

  data->text_focus_id = 0;

  if (!data->text_editor || data->text_editor_active ||
      data->text_focus_attempts >= 10)
    return G_SOURCE_REMOVE;

  data->text_focus_attempts++;

  gtk_window_present (GTK_WINDOW (data->text_editor));
  if (data->text_entry)
    gtk_widget_grab_focus (data->text_entry);

#ifdef GDK_WINDOWING_X11
  {
    GdkWindow *window = gtk_widget_get_window (data->text_editor);

    if (window && GDK_IS_X11_WINDOW (window))
      {
	GdkDisplay *display = gdk_window_get_display (window);
	Display *xdisplay = GDK_DISPLAY_XDISPLAY (display);

	gdk_x11_display_error_trap_push (display);
	XSetInputFocus (xdisplay, GDK_WINDOW_XID (window),
			RevertToParent, CurrentTime);
	XFlush (xdisplay);
	gdk_x11_display_error_trap_pop_ignored (display);
      }
  }
#endif

  data->text_focus_id = g_timeout_add (100, text_editor_focus_retry, data);
  return G_SOURCE_REMOVE;
}

static void
show_text_editor (ScreenatorData *data, GdkDevice *device, gint x, gint y)
{
  GtkWidget *editor;
  GtkWidget *entry;
  GtkCssProvider *provider;
  PangoFontDescription *font;
  GdkScreen *screen;
  GdkVisual *visual;
  gchar *css;

  /* Keep painting mode and its pointer grab alive while the editor is open.
   * The editor only takes the keyboard focus; mouse buttons are disabled
   * until Enter or Escape closes it, so a click can neither commit the text
   * nor fall through to the application below. */
  data->text_device = device;
  data->text_x = x;
  data->text_y = y;
  data->text_editor_x = x;
  data->text_editor_y = y;
  data->text_editor_active = FALSE;
  data->text_editor_positioned = FALSE;
  data->text_editor_sized = FALSE;
  data->text_editor_rtl = FALSE;
  data->text_preview_valid = FALSE;
  memset (&data->text_preview_bounds, 0, sizeof (data->text_preview_bounds));

  /* The caret is drawn on the annotation window and blinks like a normal
   * text cursor. */
  /* The caret is shown only after the editor has been positioned, so an
   * early expose cannot leave a stale non-blinking caret at the click. */
  data->text_caret_visible = FALSE;
  if (data->text_caret_timeout)
    {
      g_source_remove (data->text_caret_timeout);
      data->text_caret_timeout = 0;
    }

  editor = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_decorated (GTK_WINDOW (editor), FALSE);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (editor), TRUE);
  gtk_window_set_skip_pager_hint (GTK_WINDOW (editor), TRUE);
  gtk_window_set_keep_above (GTK_WINDOW (editor), TRUE);
  gtk_window_set_resizable (GTK_WINDOW (editor), FALSE);
  gtk_window_set_type_hint (GTK_WINDOW (editor), GDK_WINDOW_TYPE_HINT_UTILITY);
  gtk_window_set_accept_focus (GTK_WINDOW (editor), TRUE);
  gtk_window_set_focus_on_map (GTK_WINDOW (editor), TRUE);
  gtk_window_set_gravity (GTK_WINDOW (editor), GDK_GRAVITY_STATIC);

  /* Make the editor background transparent on a composited screen. */
  screen = gtk_widget_get_screen (editor);
  visual = gdk_screen_get_rgba_visual (screen);
  if (visual && gdk_screen_is_composited (screen))
    {
      gtk_widget_set_visual (editor, visual);
      gtk_widget_set_app_paintable (editor, TRUE);
      g_signal_connect (editor, "draw",
			G_CALLBACK (on_text_editor_draw), data);
    }

  entry = gtk_entry_new ();
  gtk_entry_set_has_frame (GTK_ENTRY (entry), FALSE);
  /* Start with a tiny editor.  Once the first character shows the script
   * direction, size_text_editor() fixes the width for the whole session. */
  gtk_entry_set_width_chars (GTK_ENTRY (entry), 1);

  font = pango_font_description_from_string (data->font ? data->font
							: KEY_DFLT_FONT);
  gtk_widget_override_font (entry, font);
  pango_font_description_free (font);

  /* The entry is invisible: the text and the caret are drawn on the
   * annotation window, where they cannot be hidden by a drawing or the screen
   * paper. */
  css = g_strdup ("entry {"
		  " background: transparent;"
		  " background-image: none;"
		  " border: none;"
		  " box-shadow: none;"
		  " padding: 0;"
		  " margin: 0;"
		  " color: transparent;"
		  " caret-color: transparent;"
		  " }");
  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_data (provider, css, -1, NULL);
  gtk_style_context_add_provider (gtk_widget_get_style_context (entry),
				  GTK_STYLE_PROVIDER (provider),
				  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
  g_free (css);

  gtk_container_add (GTK_CONTAINER (editor), entry);

  data->text_editor = editor;
  data->text_entry = entry;

  g_signal_connect (entry, "changed",
		    G_CALLBACK (on_text_editor_changed), data);
  g_signal_connect (entry, "activate",
		    G_CALLBACK (on_text_editor_activate), data);
  g_signal_connect (entry, "key-press-event",
		    G_CALLBACK (on_text_editor_key_press), data);
  g_signal_connect (entry, "focus-out-event",
		    G_CALLBACK (on_text_editor_focus_out), data);
  g_signal_connect (entry, "focus-in-event",
		    G_CALLBACK (on_text_editor_focus_in), data);
  g_signal_connect (editor, "destroy",
		    G_CALLBACK (on_text_editor_destroy), data);

  gtk_window_move (GTK_WINDOW (editor), x, y);
  gtk_widget_show_all (editor);
  position_text_editor (data);

  /* The editor window itself is invisible: the typed text and the caret are
   * drawn on the annotation window, above the drawings and the paper.  An
   * empty shape also stops the window from covering them when the screen is
   * not composited or when it is stacked above the annotation window. */
  {
    cairo_region_t *empty = cairo_region_create ();

    gtk_widget_shape_combine_region (editor, empty);
    cairo_region_destroy (empty);
  }

  /* Re-assert painting mode's pointer grab while the editor is open.  The
   * button handlers swallow every mouse event, so a click can neither commit
   * the text nor reach the application below. */
  acquire_grab (data, NULL);

  gtk_widget_grab_focus (entry);
  gtk_window_present (GTK_WINDOW (editor));

  data->text_focus_attempts = 0;
  if (data->text_focus_id)
    g_source_remove (data->text_focus_id);
  data->text_focus_id = g_timeout_add (50, text_editor_focus_retry, data);

  invalidate_text_area (data, x, y);

  data->text_caret_visible = TRUE;
  data->text_caret_timeout = g_timeout_add (500, text_caret_blink, data);

  /* Draw the caret right away, before the first blink. */
  {
    GdkRectangle caret;
    GdkWindow *window = data->win ? gtk_widget_get_window (data->win) : NULL;

    if (window && text_caret_bounds (data, &caret))
      gdk_window_invalidate_rect (window, &caret, 0);
  }
}


void
screenator_show_text (ScreenatorData *data, GdkDevice *device, gint x, gint y)
{
  if (!data->text_editor)
    show_text_editor (data, device, x, y);
}

/* The spline tool places nodes with single clicks.  Every node carries two
 * Bezier handles that can be dragged to shape the curve; the handles mirror
 * each other until one is dragged with Ctrl held.  The curve is committed when
 * the user double-clicks or right-clicks.  While the spline is being placed,
 * the pointer position is appended as a temporary last node so the next
 * segment is previewed. */
static GArray *
spline_preview_points (ScreenatorDeviceData *devdata)
{
  GArray *nodes = g_array_new (FALSE, FALSE, sizeof (ScreenatorSplineNode));
  ScreenatorSplineNode cursor;

  if (devdata->shape_points && devdata->shape_points->len > 0)
    g_array_append_vals (nodes, devdata->shape_points->data,
			 devdata->shape_points->len);

  memset (&cursor, 0, sizeof (cursor));
  cursor.point.x = devdata->shape_x1;
  cursor.point.y = devdata->shape_y1;

  if (nodes->len == 0)
    g_array_append_val (nodes, cursor);
  else
    {
      ScreenatorSplineNode last =
	g_array_index (nodes, ScreenatorSplineNode, nodes->len - 1);

      if (last.point.x != cursor.point.x ||
	  last.point.y != cursor.point.y)
	g_array_append_val (nodes, cursor);
    }

  /* Recompute the automatic handles on the copy so the preview matches what
   * will be committed when the next node is clicked. */
  spline_update_auto (nodes);

  return nodes;
}


static void
spline_preview_bounds (ScreenatorData *data, ScreenatorDeviceData *devdata,
		       GdkRectangle *bounds)
{
  GArray *points = spline_preview_points (devdata);

  spline_get_bounds (data, points, bounds);
  g_array_free (points, TRUE);
}


static void
spline_invalidate (ScreenatorData *data, const GdkRectangle *bounds)
{
  if (bounds->width > 0 && bounds->height > 0)
    {
      /* Let reshape() update the window shape on a non-composited screen. */
      data->modified = 1;
      gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
				  bounds, 0);
    }
}


/* Add a node at the click position and redraw the affected area. */
static void
spline_add_node (ScreenatorData *data, ScreenatorDeviceData *devdata,
		 gint x, gint y)
{
  GdkRectangle old_bounds, new_bounds, dirty;
  ScreenatorSplineNode node;

  spline_preview_bounds (data, devdata, &old_bounds);

  if (!devdata->shape_points)
    devdata->shape_points = g_array_new (FALSE, FALSE,
					 sizeof (ScreenatorSplineNode));
  else if (devdata->shape_points->len > 0)
    {
      ScreenatorSplineNode last =
	g_array_index (devdata->shape_points, ScreenatorSplineNode,
		       devdata->shape_points->len - 1);

      /* Clicking the same spot again must not add a duplicate node. */
      if (last.point.x == x && last.point.y == y)
	return;
    }

  memset (&node, 0, sizeof (node));
  node.point.x = x;
  node.point.y = y;
  node.in = node.point;
  node.out = node.point;
  g_array_append_val (devdata->shape_points, node);

  spline_update_auto (devdata->shape_points);

  devdata->shape_x1 = x;
  devdata->shape_y1 = y;
  devdata->shape_active = TRUE;

  spline_preview_bounds (data, devdata, &new_bounds);
  gdk_rectangle_union (&old_bounds, &new_bounds, &dirty);
  spline_invalidate (data, &dirty);
}


/* Return the index of the node under (x, y), or FALSE when the pointer is not
 * close enough to a node. */
static gboolean
spline_node_at (ScreenatorDeviceData *devdata, gint x, gint y, gint *index)
{
  const gint radius = 8;
  guint i;

  if (!devdata->shape_points)
    return FALSE;

  for (i = 0; i < devdata->shape_points->len; i++)
    {
      ScreenatorSplineNode *node =
	&g_array_index (devdata->shape_points, ScreenatorSplineNode, i);
      gint dx = node->point.x - x;
      gint dy = node->point.y - y;

      if (dx * dx + dy * dy <= radius * radius)
	{
	  *index = (gint) i;
	  return TRUE;
	}
    }

  return FALSE;
}


/* Return the handle under (x, y).  "outgoing" tells which side of the node
 * the handle belongs to.  The closest handle wins so overlapping handles of
 * neighbouring nodes stay usable. */
static gboolean
spline_handle_at (GArray *nodes, gint x, gint y,
		  gint *index, gboolean *outgoing)
{
  const gint radius = 9;
  gint best = radius * radius + 1;
  gboolean found = FALSE;
  guint i;

  if (!nodes)
    return FALSE;

  for (i = 0; i < nodes->len; i++)
    {
      ScreenatorSplineNode *node =
	&g_array_index (nodes, ScreenatorSplineNode, i);
      GdkPoint handles[2];
      guint h;

      handles[0] = node->in;
      handles[1] = node->out;

      for (h = 0; h < 2; h++)
	{
	  gint dx = handles[h].x - x;
	  gint dy = handles[h].y - y;
	  gint dist = dx * dx + dy * dy;

	  /* A zero-length handle sits on the node and cannot be grabbed. */
	  if (handles[h].x == node->point.x &&
	      handles[h].y == node->point.y)
	    continue;

	  if (dist < best)
	    {
	      best = dist;
	      *index = (gint) i;
	      *outgoing = (h == 1);
	      found = TRUE;
	    }
	}
    }

  return found;
}


/* Park the preview cursor on the last node, so dragging an earlier node does
 * not leave a rubber-band segment behind. */
static void
spline_park_cursor (ScreenatorData *data, ScreenatorDeviceData *devdata)
{
  GdkRectangle old_bounds, new_bounds, dirty;
  ScreenatorSplineNode last;

  if (!devdata->shape_points || devdata->shape_points->len == 0)
    return;

  last = g_array_index (devdata->shape_points, ScreenatorSplineNode,
			devdata->shape_points->len - 1);
  if (devdata->shape_x1 == last.point.x && devdata->shape_y1 == last.point.y)
    return;

  spline_preview_bounds (data, devdata, &old_bounds);
  devdata->shape_x1 = last.point.x;
  devdata->shape_y1 = last.point.y;
  spline_preview_bounds (data, devdata, &new_bounds);
  gdk_rectangle_union (&old_bounds, &new_bounds, &dirty);
  spline_invalidate (data, &dirty);
}


/* Move one of the placed nodes together with its handles, then refresh the
 * automatic handles of the other nodes, which depend on their neighbours. */
static void
spline_move_node (ScreenatorData *data, ScreenatorDeviceData *devdata,
		  gint index, gint x, gint y)
{
  GdkRectangle old_bounds, new_bounds, dirty;
  ScreenatorSplineNode *nodes, *node;
  ScreenatorSplineNode last;
  gint dx, dy;

  if (!devdata->shape_points || index < 0 ||
      index >= (gint) devdata->shape_points->len)
    return;

  nodes = (ScreenatorSplineNode *) devdata->shape_points->data;
  node = &nodes[index];
  if (node->point.x == x && node->point.y == y)
    return;

  spline_preview_bounds (data, devdata, &old_bounds);

  dx = x - node->point.x;
  dy = y - node->point.y;
  node->point.x = x;
  node->point.y = y;
  node->in.x += dx;
  node->in.y += dy;
  node->out.x += dx;
  node->out.y += dy;

  spline_update_auto (devdata->shape_points);

  /* If the last node moved, the parked cursor follows it. */
  last = nodes[devdata->shape_points->len - 1];
  devdata->shape_x1 = last.point.x;
  devdata->shape_y1 = last.point.y;

  spline_preview_bounds (data, devdata, &new_bounds);
  gdk_rectangle_union (&old_bounds, &new_bounds, &dirty);
  spline_invalidate (data, &dirty);
}


/* Move one handle of a node.  Without "independent" the opposite handle is
 * mirrored through the node, so the curve stays smooth; holding Ctrl while
 * dragging keeps the other side in place and gives the node a corner. */
static void
spline_move_handle (ScreenatorData *data, ScreenatorDeviceData *devdata,
		    gint index, gboolean outgoing, gint x, gint y,
		    gboolean independent)
{
  GdkRectangle old_bounds, new_bounds, dirty;
  ScreenatorSplineNode *nodes, *node;
  GdkPoint *side, *other;

  if (!devdata->shape_points || index < 0 ||
      index >= (gint) devdata->shape_points->len)
    return;

  nodes = (ScreenatorSplineNode *) devdata->shape_points->data;
  node = &nodes[index];
  side = outgoing ? &node->out : &node->in;
  other = outgoing ? &node->in : &node->out;

  if (side->x == x && side->y == y)
    return;

  spline_preview_bounds (data, devdata, &old_bounds);

  node->manual = TRUE;
  side->x = x;
  side->y = y;

  if (!independent)
    {
      other->x = 2 * node->point.x - x;
      other->y = 2 * node->point.y - y;
    }

  spline_preview_bounds (data, devdata, &new_bounds);
  gdk_rectangle_union (&old_bounds, &new_bounds, &dirty);
  spline_invalidate (data, &dirty);
}


/* Draw the Bezier handles of every placed node while the spline is being
 * edited.  The handles are only part of the preview and are not committed. */
static void
spline_draw_handles (ScreenatorData *data, cairo_t *cr, GArray *nodes,
		     guint count)
{
  GdkRGBA color = data->pen_color;
  guint i;

  if (!nodes)
    return;

  count = MIN (count, nodes->len);

  color.alpha = 1.0;

  for (i = 0; i < count; i++)
    {
      ScreenatorSplineNode *node =
	&g_array_index (nodes, ScreenatorSplineNode, i);
      GdkPoint handles[2];
      guint h;

      handles[0] = node->in;
      handles[1] = node->out;

      for (h = 0; h < 2; h++)
	{
	  if (handles[h].x == node->point.x &&
	      handles[h].y == node->point.y)
	    continue;

	  /* A white halo under a dark line keeps the handle visible on both
	   * light and dark backgrounds. */
	  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.8);
	  cairo_set_line_width (cr, 3.0);
	  cairo_move_to (cr, node->point.x, node->point.y);
	  cairo_line_to (cr, handles[h].x, handles[h].y);
	  cairo_stroke (cr);

	  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.7);
	  cairo_set_line_width (cr, 1.0);
	  cairo_move_to (cr, node->point.x, node->point.y);
	  cairo_line_to (cr, handles[h].x, handles[h].y);
	  cairo_stroke (cr);

	  /* Handle grip: a white disc with a dark ring. */
	  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.95);
	  cairo_arc (cr, handles[h].x, handles[h].y, 5.0, 0, 2 * G_PI);
	  cairo_fill (cr);

	  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.85);
	  cairo_set_line_width (cr, 1.5);
	  cairo_arc (cr, handles[h].x, handles[h].y, 5.0, 0, 2 * G_PI);
	  cairo_stroke (cr);
	}
    }

  /* Square node markers are drawn last so they stay on top of the handles. */
  for (i = 0; i < count; i++)
    {
      ScreenatorSplineNode *node =
	&g_array_index (nodes, ScreenatorSplineNode, i);

      /* Dark border, white ring and a pen-coloured core: the marker stays
       * visible whatever the pen colour and the screen behind it. */
      cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.85);
      cairo_rectangle (cr, node->point.x - 6.0, node->point.y - 6.0,
		       12.0, 12.0);
      cairo_fill (cr);

      cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.95);
      cairo_rectangle (cr, node->point.x - 5.0, node->point.y - 5.0,
		       10.0, 10.0);
      cairo_fill (cr);

      gdk_cairo_set_source_rgba (cr, &color);
      cairo_rectangle (cr, node->point.x - 3.0, node->point.y - 3.0,
		       6.0, 6.0);
      cairo_fill (cr);
    }
}


/* Commit the placed nodes to the backbuffer.  A spline with fewer than two
 * nodes is discarded. */
static void
spline_finish (ScreenatorData *data, ScreenatorDeviceData *devdata)
{
  GdkRectangle preview_bounds, curve_bounds, dirty;

  if (!devdata->shape_active)
    return;

  spline_preview_bounds (data, devdata, &preview_bounds);
  devdata->shape_active = FALSE;
  devdata->spline_node_drag = FALSE;
  devdata->spline_handle_drag = FALSE;
  devdata->spline_finish_pending = FALSE;
  curve_bounds = preview_bounds;

  if (devdata->shape_points && devdata->shape_points->len >= 2)
    {
      cairo_t *cr;

      spline_update_auto (devdata->shape_points);

      snap_undo_state (data);
      cr = cairo_create (data->backbuffer);
      draw_spline (data, cr, devdata->shape_points);
      cairo_destroy (cr);

      data->modified = 1;
      data->painted = 1;

      spline_get_bounds (data, devdata->shape_points, &curve_bounds);
    }

  if (devdata->shape_points)
    g_array_set_size (devdata->shape_points, 0);

  gdk_rectangle_union (&preview_bounds, &curve_bounds, &dirty);
  spline_invalidate (data, &dirty);
}


/* The Polygon tool uses the first click as the shape's centre; dragging away
 * from it sets the radius and rotation.  The star stays editable until it is
 * committed.  Four handles control the number of sides, the radius, the
 * rotation and the inner radius, which turns the star into a regular polygon
 * as it grows.  Clicking empty space commits the current shape and places a
 * new one; a double-click or a right-click commits it and stays in the
 * tool. */

#define STAR_DEFAULT_RADIUS 64.0
#define STAR_DEFAULT_INNER  0.45
#define STAR_MIN_RADIUS     8.0
#define STAR_POINTS_HANDLE_OFFSET 28.0
#define POLYGON_SIDES_PER_PIXEL 12.0

/* Position of one star handle in screen coordinates.  The points handle sits
 * outside the star so it never covers the shape; the rotation handle sits at
 * half the radius, between two vertices. */
static void
star_handle_position (const ScreenatorStar *star, ScreenatorStarHandle handle,
		      GdkPoint *pos)
{
  gdouble angle;
  gdouble r;

  switch (handle)
    {
    case SCREENATOR_STAR_HANDLE_RADIUS:
      angle = star->rotation;
      r = star->radius;
      break;
    case SCREENATOR_STAR_HANDLE_INNER:
      angle = star->rotation + G_PI / star->points;
      r = star->radius * CLAMP (star->inner, 0.0, 1.0);
      break;
    case SCREENATOR_STAR_HANDLE_ROTATION:
      angle = star->rotation + G_PI / 2.0;
      r = star->radius * 0.5;
      break;
    case SCREENATOR_STAR_HANDLE_POINTS:
      angle = star->rotation + G_PI;
      r = star->radius + STAR_POINTS_HANDLE_OFFSET;
      break;
    default:
      angle = 0.0;
      r = 0.0;
      break;
    }

  pos->x = (gint) round (star->cx + r * cos (angle));
  pos->y = (gint) round (star->cy + r * sin (angle));
}

/* Repaint the area covered by the shape and its handles. */
static void
star_invalidate (ScreenatorData *data, const ScreenatorStar *star)
{
  GdkRectangle bounds;
  gint pad = (gint) data->pen_width * 2 + 64;

  if (!data->win)
    return;

  star_get_bounds (star, pad, &bounds);
  data->modified = 1;
  gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
			      &bounds, 0);
}

/* Return the star handle under (x, y), or NONE.  The square points handle is
 * the outermost one and is tested first so it wins when handles overlap. */
static ScreenatorStarHandle
star_handle_at (const ScreenatorStar *star, gint x, gint y)
{
  static const ScreenatorStarHandle order[] =
    {
      SCREENATOR_STAR_HANDLE_POINTS,
      SCREENATOR_STAR_HANDLE_ROTATION,
      SCREENATOR_STAR_HANDLE_INNER,
      SCREENATOR_STAR_HANDLE_RADIUS
    };
  const gint radius = 11;
  guint i;

  for (i = 0; i < G_N_ELEMENTS (order); i++)
    {
      GdkPoint pos;
      gint dx, dy;

      star_handle_position (star, order[i], &pos);
      dx = pos.x - x;
      dy = pos.y - y;

      if (dx * dx + dy * dy <= radius * radius)
	return order[i];
    }

  return SCREENATOR_STAR_HANDLE_NONE;
}

/* One vertex of the star polygon: even indices are on the outer radius, odd
 * indices on the inner radius. */
static void
star_vertex (const ScreenatorStar *star, gint index, gdouble *x, gdouble *y)
{
  gdouble angle = star->rotation + index * G_PI / star->points;
  gdouble r = (index % 2 == 0)
    ? star->radius : star->radius * CLAMP (star->inner, 0.0, 1.0);

  *x = star->cx + r * cos (angle);
  *y = star->cy + r * sin (angle);
}

/* Point-in-polygon test, used to decide whether a press grabs the star and
 * moves it or places a new one. */
static gboolean
star_contains (const ScreenatorStar *star, gint x, gint y)
{
  gint i, j, n;
  gboolean inside = FALSE;

  if (!star || star->points < 3 || star->radius < 1.0)
    return FALSE;

  n = star->points * 2;

  for (i = 0, j = n - 1; i < n; j = i++)
    {
      gdouble xi, yi, xj, yj;

      star_vertex (star, i, &xi, &yi);
      star_vertex (star, j, &xj, &yj);

      if (((yi > y) != (yj > y)) &&
	  (x < (xj - xi) * (y - yi) / (yj - yi) + xi))
	inside = !inside;
    }

  return inside;
}

/* Start a new shape at the click, which becomes its centre.  Dragging away
 * from that centre sets the radius and rotation; a plain click leaves the
 * default radius. */
static void
star_begin (ScreenatorData *data, ScreenatorDeviceData *devdata,
	    gint x, gint y)
{
  devdata->star.cx = x;
  devdata->star.cy = y;
  devdata->star.radius = 0.0;
  devdata->star.inner = STAR_DEFAULT_INNER;
  devdata->star.rotation = -G_PI / 2.0;
  devdata->star.points = 5;

  devdata->star_active = TRUE;

  devdata->star_drag = SCREENATOR_STAR_HANDLE_PLACEMENT;
  devdata->star_press_x = x;
  devdata->star_press_y = y;
  devdata->star_press_cx = x;
  devdata->star_press_cy = y;

  star_invalidate (data, &devdata->star);
}

/* Finish the initial radius/rotation drag.  A click that never moved keeps a
 * useful default-size shape, while a real drag keeps the size and angle that
 * the pointer selected. */
static void
star_finish_placement (ScreenatorData *data, ScreenatorDeviceData *devdata)
{
  if (devdata->star_drag != SCREENATOR_STAR_HANDLE_PLACEMENT)
    return;

  devdata->star_drag = SCREENATOR_STAR_HANDLE_NONE;

  if (devdata->star.radius < STAR_MIN_RADIUS)
    {
      devdata->star.radius = STAR_DEFAULT_RADIUS;
      devdata->star.rotation = -G_PI / 2.0;
    }

  star_invalidate (data, &devdata->star);
}

/* Quantize an angle to the nearest multiple of 30 or 45 degrees, the angles
 * that are easy to aim for: 0, 30, 45, 60, 90 and so on.  Snapping is only
 * applied while Ctrl is held; without it the angle follows the pointer. */
static gdouble
snap_common_rotation (gdouble rotation)
{
  static const gdouble steps[] = { 30.0, 45.0 };
  gdouble degrees = rotation * 180.0 / G_PI;
  gdouble best = rotation;
  gdouble best_diff = G_MAXDOUBLE;
  guint i;

  for (i = 0; i < G_N_ELEMENTS (steps); i++)
    {
      gdouble candidate = round (degrees / steps[i]) * steps[i];
      gdouble diff = fabs (degrees - candidate);

      if (diff < best_diff)
	{
	  best_diff = diff;
	  best = candidate;
	}
    }

  return best * G_PI / 180.0;
}

/* Move the grabbed handle to (x, y).  The side-count handle always starts at
 * three when it is pressed; dragging away from the press point in any
 * direction adds sides.  Rotation snaps to the common angles only while Ctrl
 * is held. */
static void
star_move_handle (ScreenatorData *data, ScreenatorDeviceData *devdata,
		  gint x, gint y, gboolean snap)
{
  ScreenatorStar *star = &devdata->star;
  gdouble dx = x - star->cx;
  gdouble dy = y - star->cy;
  gdouble dist = sqrt (dx * dx + dy * dy);

  if (!devdata->star_active ||
      devdata->star_drag == SCREENATOR_STAR_HANDLE_NONE)
    return;

  star_invalidate (data, star);

  switch (devdata->star_drag)
    {
    case SCREENATOR_STAR_HANDLE_PLACEMENT:
      /* The first drag sets both the radius and the rotation around the
       * clicked centre. */
      star->radius = CLAMP (dist, 0.0, 100000.0);
      if (dist > 0.01)
	{
	  star->rotation = atan2 (dy, dx);

	  if (snap)
	    star->rotation = snap_common_rotation (star->rotation);
	}
      break;
    case SCREENATOR_STAR_HANDLE_RADIUS:
      star->radius = CLAMP (dist, STAR_MIN_RADIUS, 100000.0);
      break;
    case SCREENATOR_STAR_HANDLE_INNER:
      if (star->radius > 0.0)
	{
	  gdouble inner = CLAMP (dist / star->radius, 0.0, 1.0);
	  gdouble tolerance = CLAMP (star->radius * 0.08, 3.0, 10.0);
	  /* The sides are exactly flat when the inner vertices sit on the
	   * apothem, at cos(pi / points) of the outer radius.  The inner
	   * vertices are then the midpoints of the sides, so the star is a
	   * regular polygon with the chosen number of sides.  Snap to that
	   * ratio when the handle is close, so a perfect hexagon or pentagon
	   * is easy to hit. */
	  gdouble flat = cos (G_PI / star->points);

	  if (fabs (dist - star->radius * flat) <= tolerance)
	    inner = flat;

	  star->inner = inner;
	}
      break;
    case SCREENATOR_STAR_HANDLE_ROTATION:
      if (dist > 0.0)
	{
	  star->rotation = atan2 (dy, dx) - G_PI / 2.0;

	  if (snap)
	    star->rotation = snap_common_rotation (star->rotation);
	}
      break;
    case SCREENATOR_STAR_HANDLE_POINTS:
      {
	/* The distance from the handle sets the number of points: at the
	 * handle the star has three points, and moving away in any
	 * direction adds more.  Moving back towards the handle removes
	 * them again. */
	gdouble px = x - devdata->star_press_x;
	gdouble py = y - devdata->star_press_y;
	gdouble moved = sqrt (px * px + py * py);

	star->points = CLAMP (3 + (gint) (moved / POLYGON_SIDES_PER_PIXEL),
			      3, 16);
      }
      break;
    case SCREENATOR_STAR_HANDLE_CENTRE:
      star->cx = devdata->star_press_cx + (x - devdata->star_press_x);
      star->cy = devdata->star_press_cy + (y - devdata->star_press_y);
      break;
    default:
      break;
    }

  star_invalidate (data, star);
}

/* Draw one round handle grip with a dark ring and a white fill so it stays
 * visible on any background. */
static void
star_draw_grip (cairo_t *cr, gint x, gint y)
{
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.85);
  cairo_arc (cr, x, y, 6.0, 0, 2 * G_PI);
  cairo_fill (cr);

  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.95);
  cairo_arc (cr, x, y, 5.0, 0, 2 * G_PI);
  cairo_fill (cr);
}

/* Draw a line from the centre to a handle, with a light halo so it does not
 * disappear over dark or busy content. */
static void
star_draw_spoke (cairo_t *cr, const ScreenatorStar *star, gint x, gint y)
{
  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.8);
  cairo_set_line_width (cr, 3.0);
  cairo_move_to (cr, star->cx, star->cy);
  cairo_line_to (cr, x, y);
  cairo_stroke (cr);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.7);
  cairo_set_line_width (cr, 1.0);
  cairo_move_to (cr, star->cx, star->cy);
  cairo_line_to (cr, x, y);
  cairo_stroke (cr);
}

/* Draw the four star handles while the star is editable.  The number of
 * points is written next to its square handle. */
static void
star_draw_handles (ScreenatorData *data, cairo_t *cr,
		   const ScreenatorStar *star)
{
  static const ScreenatorStarHandle spokes[] =
    {
      SCREENATOR_STAR_HANDLE_RADIUS,
      SCREENATOR_STAR_HANDLE_INNER,
      SCREENATOR_STAR_HANDLE_ROTATION
    };
  GdkPoint pos;
  guint i;

  if (!star)
    return;

  for (i = 0; i < G_N_ELEMENTS (spokes); i++)
    {
      star_handle_position (star, spokes[i], &pos);
      star_draw_spoke (cr, star, pos.x, pos.y);
      star_draw_grip (cr, pos.x, pos.y);
    }

  /* The points handle is square and labelled with the current count. */
  star_handle_position (star, SCREENATOR_STAR_HANDLE_POINTS, &pos);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.85);
  cairo_rectangle (cr, pos.x - 7.0, pos.y - 7.0, 14.0, 14.0);
  cairo_fill (cr);

  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.95);
  cairo_rectangle (cr, pos.x - 6.0, pos.y - 6.0, 12.0, 12.0);
  cairo_fill (cr);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.9);
  cairo_set_line_width (cr, 1.0);
  cairo_rectangle (cr, pos.x - 6.0, pos.y - 6.0, 12.0, 12.0);
  cairo_stroke (cr);

  {
    gchar label[8];
    cairo_text_extents_t ext;
    gdouble tx, ty;

    g_snprintf (label, sizeof (label), "%d", star->points);

    cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
			    CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size (cr, 11.0);
    cairo_text_extents (cr, label, &ext);

    /* Right of the handle, vertically centred on it. */
    tx = pos.x + 11.0;
    ty = pos.y - ext.y_bearing - ext.height / 2.0;

    cairo_move_to (cr, tx, ty);
    cairo_text_path (cr, label);

    cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.9);
    cairo_set_line_width (cr, 3.0);
    cairo_stroke_preserve (cr);

    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.95);
    cairo_fill (cr);
  }
}


/* Repaint the area covered by the triangle and its handles. */
static void
triangle_invalidate (ScreenatorData *data, const ScreenatorTriangle *triangle)
{
  GdkRectangle bounds;
  gint pad = (gint) data->pen_width * 2 + 64;

  if (!data->win)
    return;

  triangle_get_bounds (triangle, pad, &bounds);
  data->modified = 1;
  gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
			      &bounds, 0);
}

/* Return the vertex handle under (x, y), or NONE. */
static ScreenatorTriangleHandle
triangle_handle_at (const ScreenatorTriangle *triangle, gint x, gint y)
{
  const gint radius = 11;
  gint i;

  for (i = 0; i < 3; i++)
    {
      gint dx = triangle->v[i].x - x;
      gint dy = triangle->v[i].y - y;

      if (dx * dx + dy * dy <= radius * radius)
	return (ScreenatorTriangleHandle) (SCREENATOR_TRIANGLE_HANDLE_V0 + i);
    }

  return SCREENATOR_TRIANGLE_HANDLE_NONE;
}

/* Point-in-triangle test, used to decide whether a press grabs the triangle
 * and moves it. */
static gboolean
triangle_contains (const ScreenatorTriangle *triangle, gint x, gint y)
{
  gint i, j;
  gboolean inside = FALSE;

  for (i = 0, j = 2; i < 3; j = i++)
    {
      gint xi = triangle->v[i].x, yi = triangle->v[i].y;
      gint xj = triangle->v[j].x, yj = triangle->v[j].y;

      if (((yi > y) != (yj > y)) &&
	  (x < (gdouble) (xj - xi) * (y - yi) / (yj - yi) + xi))
	inside = !inside;
    }

  return inside;
}

/* Start a new triangle by placing its first vertex at the click.  The next
 * two clicks place the other vertices; until then, handles are not active. */
static void
triangle_begin (ScreenatorData *data, ScreenatorDeviceData *devdata,
		gint x, gint y)
{
  gint i;

  for (i = 0; i < 3; i++)
    {
      devdata->triangle.v[i].x = x;
      devdata->triangle.v[i].y = y;
      devdata->triangle_press_v[i] = devdata->triangle.v[i];
    }

  devdata->triangle_active = TRUE;
  devdata->triangle_drag = SCREENATOR_TRIANGLE_HANDLE_NONE;
  devdata->triangle_vertices = 1;
  devdata->triangle_placed_time = 0;
  devdata->triangle_press_x = x;
  devdata->triangle_press_y = y;

  triangle_invalidate (data, &devdata->triangle);
}

/* Place the next vertex at the click.  Returns TRUE while the triangle is
 * still being placed. */
static gboolean
triangle_place_next_vertex (ScreenatorData *data,
			    ScreenatorDeviceData *devdata,
			    gint x, gint y, guint32 time)
{
  if (!devdata->triangle_active || devdata->triangle_vertices >= 3)
    return FALSE;

  triangle_invalidate (data, &devdata->triangle);

  if (devdata->triangle_vertices == 1)
    {
      devdata->triangle.v[1].x = x;
      devdata->triangle.v[1].y = y;
      devdata->triangle.v[2].x = x;
      devdata->triangle.v[2].y = y;
      devdata->triangle_vertices = 2;
    }
  else
    {
      devdata->triangle.v[2].x = x;
      devdata->triangle.v[2].y = y;
      devdata->triangle_vertices = 3;
      devdata->triangle_placed_time = time;
    }

  triangle_invalidate (data, &devdata->triangle);
  return TRUE;
}

/* Interior angle at P between the rays to A and B, in degrees.  A vertex that
 * sits on top of an end point is treated as a straight angle. */
static gdouble
triangle_angle_at (gdouble ax, gdouble ay, gdouble bx, gdouble by,
		   gdouble px, gdouble py)
{
  gdouble ux = ax - px;
  gdouble uy = ay - py;
  gdouble vx = bx - px;
  gdouble vy = by - py;
  gdouble nu = hypot (ux, uy);
  gdouble nv = hypot (vx, vy);
  gdouble cosine;

  if (nu < 1e-9 || nv < 1e-9)
    return 180.0;

  cosine = CLAMP ((ux * vx + uy * vy) / (nu * nv), -1.0, 1.0);

  return acos (cosine) * 180.0 / G_PI;
}

/* Move one vertex to (x, y).  While Ctrl is held the interior angle at that
 * vertex snaps to 30, 45, 60 or 90 degrees: the opposite edge stays fixed and
 * the vertex slides along its median to the distance that gives the snapped
 * angle.  The angle shrinks steadily as the vertex moves away from the middle
 * of the opposite edge, so a short bisection finds the distance that matches
 * the target even when the triangle is not isosceles. */
static void
triangle_place_vertex (ScreenatorTriangle *triangle, gint index,
		       gint x, gint y, gboolean snap)
{
  if (snap)
    {
      static const gdouble angles[] = { 30.0, 45.0, 60.0, 90.0 };
      gint a = (index + 1) % 3;
      gint b = (index + 2) % 3;
      gdouble ax = triangle->v[a].x;
      gdouble ay = triangle->v[a].y;
      gdouble bx = triangle->v[b].x;
      gdouble by = triangle->v[b].y;
      gdouble mx = (ax + bx) / 2.0;
      gdouble my = (ay + by) / 2.0;
      gdouble c = hypot (bx - ax, by - ay) / 2.0;
      gdouble dx = x - mx;
      gdouble dy = y - my;
      gdouble d = hypot (dx, dy);

      if (c > 0.5 && d > 0.5)
	{
	  gdouble theta = triangle_angle_at (ax, ay, bx, by, x, y);
	  gdouble target = theta;
	  gdouble best_diff = G_MAXDOUBLE;
	  gdouble ux = dx / d;
	  gdouble uy = dy / d;
	  gdouble lo = 0.0;
	  gdouble hi = MAX (d, c);
	  gdouble snapped_d;
	  guint i;

	  for (i = 0; i < G_N_ELEMENTS (angles); i++)
	    {
	      gdouble diff = fabs (theta - angles[i]);

	      if (diff < best_diff)
		{
		  best_diff = diff;
		  target = angles[i];
		}
	    }

	  /* Grow the upper bound until the vertex is far enough away for the
	   * angle to fall below the target. */
	  while (hi < 1e7 &&
		 triangle_angle_at (ax, ay, bx, by,
				    mx + ux * hi, my + uy * hi) > target)
	    hi *= 2.0;

	  for (i = 0; i < 60; i++)
	    {
	      gdouble mid = (lo + hi) / 2.0;

	      if (triangle_angle_at (ax, ay, bx, by,
				     mx + ux * mid, my + uy * mid) > target)
		lo = mid;
	      else
		hi = mid;
	    }

	  snapped_d = (lo + hi) / 2.0;
	  x = (gint) round (mx + ux * snapped_d);
	  y = (gint) round (my + uy * snapped_d);
	}
    }

  triangle->v[index].x = x;
  triangle->v[index].y = y;
}

/* Move one vertex of the triangle, or the whole triangle, to (x, y). */
static void
triangle_move_handle (ScreenatorData *data, ScreenatorDeviceData *devdata,
		      gint x, gint y, gboolean snap)
{
  ScreenatorTriangle *triangle = &devdata->triangle;

  if (!devdata->triangle_active ||
      devdata->triangle_drag == SCREENATOR_TRIANGLE_HANDLE_NONE)
    return;

  triangle_invalidate (data, triangle);

  switch (devdata->triangle_drag)
    {
    case SCREENATOR_TRIANGLE_HANDLE_V0:
      triangle_place_vertex (triangle, 0, x, y, snap);
      break;
    case SCREENATOR_TRIANGLE_HANDLE_V1:
      triangle_place_vertex (triangle, 1, x, y, snap);
      break;
    case SCREENATOR_TRIANGLE_HANDLE_V2:
      triangle_place_vertex (triangle, 2, x, y, snap);
      break;
    case SCREENATOR_TRIANGLE_HANDLE_CENTRE:
      {
	gint dx = x - devdata->triangle_press_x;
	gint dy = y - devdata->triangle_press_y;
	gint i;

	for (i = 0; i < 3; i++)
	  {
	    triangle->v[i].x = devdata->triangle_press_v[i].x + dx;
	    triangle->v[i].y = devdata->triangle_press_v[i].y + dy;
	  }
      }
      break;
    default:
      break;
    }

  triangle_invalidate (data, triangle);
}

/* Draw the handles of the triangle.  As on the Windows version, handles
 * appear only after the third vertex has been placed. */
static void
triangle_draw_handles (ScreenatorData *data, cairo_t *cr,
		       const ScreenatorTriangle *triangle, guint vertices)
{
  guint i;

  (void) data;

  if (vertices < 3)
    return;

  for (i = 0; i < 3; i++)
    star_draw_grip (cr, triangle->v[i].x, triangle->v[i].y);
}


/* --- Editable frame shapes ------------------------------------------------
 *
 * The rectangle, ellipse, rounded rectangle, text, math plot and parallel
 * lines share one frame.  A placed shape keeps a square size handle on each
 * of its four corners, a round rotation handle above it and, for the rounded
 * rectangle and the parallel lines, an extra handle.  Dragging inside the
 * frame moves it.
 */

#define FRAME_GRIP_RADIUS 11

/* A single click with the Random Signal tool creates this default-sized
 * signal centred on the click; dragging still gives an arbitrary frame. */
#define RANDOM_SIGNAL_DEFAULT_W 360
#define RANDOM_SIGNAL_DEFAULT_H 180

/* Repaint the area covered by the frame and its handles. */
static void
frame_invalidate (ScreenatorData *data, const ScreenatorFrameShape *shape)
{
  GdkRectangle bounds;
  gint pad = (gint) data->pen_width * 2 + 64;

  if (!data->win)
    return;

  frame_shape_get_bounds (shape, pad, &bounds);
  data->modified = 1;
  gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
			      &bounds, 0);
}

static gboolean
frame_handle_is_corner (ScreenatorFrameHandle handle)
{
  return handle == SCREENATOR_FRAME_HANDLE_SIZE_TL ||
	 handle == SCREENATOR_FRAME_HANDLE_SIZE_TR ||
	 handle == SCREENATOR_FRAME_HANDLE_SIZE_BL ||
	 handle == SCREENATOR_FRAME_HANDLE_SIZE_BR;
}

/* Signs of the dragged corner in the frame's local coordinates.  The opposite
 * corner is the fixed one while resizing. */
static void
frame_corner_signs (ScreenatorFrameHandle handle,
		    gdouble *sign_x, gdouble *sign_y)
{
  *sign_x = (handle == SCREENATOR_FRAME_HANDLE_SIZE_TR ||
	     handle == SCREENATOR_FRAME_HANDLE_SIZE_BR) ? 1.0 : -1.0;
  *sign_y = (handle == SCREENATOR_FRAME_HANDLE_SIZE_BL ||
	     handle == SCREENATOR_FRAME_HANDLE_SIZE_BR) ? 1.0 : -1.0;
}

/* Return the frame handle under (x, y), or NONE.  The extra handle is tested
 * first so it wins when it overlaps a corner handle. */
static ScreenatorFrameHandle
frame_handle_at (const ScreenatorFrameShape *shape, gint x, gint y)
{
  ScreenatorFrameHandle order[8];
  guint count = 0;
  guint i;

  if (shape->type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE ||
      shape->type == SCREENATOR_SHAPE_RANDOM)
    order[count++] = SCREENATOR_FRAME_HANDLE_EXTRA;
  else if (shape->type == SCREENATOR_SHAPE_BARS)
    {
      order[count++] = SCREENATOR_FRAME_HANDLE_EXTRA;
      order[count++] = SCREENATOR_FRAME_HANDLE_ARROWS;
    }
  order[count++] = SCREENATOR_FRAME_HANDLE_ROTATE;
  order[count++] = SCREENATOR_FRAME_HANDLE_SIZE_TL;
  order[count++] = SCREENATOR_FRAME_HANDLE_SIZE_TR;
  order[count++] = SCREENATOR_FRAME_HANDLE_SIZE_BL;
  order[count++] = SCREENATOR_FRAME_HANDLE_SIZE_BR;

  for (i = 0; i < count; i++)
    {
      GdkPoint pos;
      gint dx, dy;

      frame_handle_position (shape, order[i], &pos);
      dx = pos.x - x;
      dy = pos.y - y;

      if (dx * dx + dy * dy <= FRAME_GRIP_RADIUS * FRAME_GRIP_RADIUS)
	return order[i];
    }

  return SCREENATOR_FRAME_HANDLE_NONE;
}

/* Point-in-frame test, used to decide whether a press moves the shape.  The
 * point is rotated into the local frame first, so rotated shapes still feel
 * right. */
static gboolean
frame_contains (const ScreenatorFrameShape *shape, gint x, gint y)
{
  gdouble lx, ly;

  frame_screen_to_local (&shape->frame, x, y, &lx, &ly);

  return fabs (lx) <= shape->frame.w / 2.0 &&
	 fabs (ly) <= shape->frame.h / 2.0;
}

/* Remember the state the drag started from, so every handle can work from a
 * fixed reference instead of accumulating rounding errors. */
static void
frame_begin_drag (ScreenatorData *data, ScreenatorDeviceData *devdata,
		  ScreenatorFrameHandle handle, gint x, gint y)
{
  ScreenatorFrameShape *shape = &devdata->frame_shape;
  gdouble lx, ly;

  devdata->frame_drag = handle;
  devdata->frame_press_x = x;
  devdata->frame_press_y = y;
  devdata->frame_press_cx = shape->frame.cx;
  devdata->frame_press_cy = shape->frame.cy;
  devdata->frame_press_w = shape->frame.w;
  devdata->frame_press_h = shape->frame.h;
  devdata->frame_press_rotation = shape->frame.rotation;
  devdata->frame_press_font_size = shape->font_size;

  /* Text is scaled from the distance to the fixed corner, so a corner drag
   * keeps the opposite corner in place. */
  if (shape->type == SCREENATOR_SHAPE_TEXT && frame_handle_is_corner (handle))
    {
      gdouble sx, sy, fx, fy;

      frame_screen_to_local (&shape->frame, x, y, &lx, &ly);
      frame_corner_signs (handle, &sx, &sy);
      fx = -sx * shape->frame.w / 2.0;
      fy = -sy * shape->frame.h / 2.0;
      devdata->frame_press_distance =
	MAX (sqrt ((lx - fx) * (lx - fx) + (ly - fy) * (ly - fy)), 1.0);
    }
  else
    devdata->frame_press_distance = 0.0;

  (void) data;
}

/* Start a frame shape from the rectangle that was just dragged out. */
static void
frame_begin_from_drag (ScreenatorData *data, ScreenatorDeviceData *devdata,
		      ScreenatorShapeType type,
		      gint x0, gint y0, gint x1, gint y1)
{
  ScreenatorFrameShape *shape = &devdata->frame_shape;
  gint w = ABS (x1 - x0);
  gint h = ABS (y1 - y0);

  frame_shape_clear (shape);

  shape->type = type;
  shape->frame.cx = (x0 + x1) / 2.0;
  shape->frame.cy = (y0 + y1) / 2.0;
  shape->frame.w = MAX (w, 4);
  shape->frame.h = MAX (h, 4);
  shape->frame.rotation = 0.0;
  shape->extra = MIN (w, h) / 4.0;
  shape->count = 5;
  shape->arrows = SCREENATOR_BARS_ARROWS_NONE;
  if (type == SCREENATOR_SHAPE_RANDOM)
    {
      shape->extra = 5.0;
      shape->seed = devdata->frame_seed ? devdata->frame_seed : 1u;
    }
  shape->polar = data->math_polar;
  if (shape->polar)
    {
      shape->expression = g_strdup (data->math_polar_expression);
      shape->xmin = data->math_theta_min;
      shape->xmax = data->math_theta_max;
    }
  else
    {
      shape->expression = g_strdup (data->math_expression);
      shape->xmin = data->math_xmin;
      shape->xmax = data->math_xmax;
    }

  devdata->frame_active = TRUE;
  devdata->frame_drag = SCREENATOR_FRAME_HANDLE_NONE;

  frame_invalidate (data, shape);
}

/* Move the grabbed handle, or the whole shape, to (x, y).  Rotation follows
 * the pointer freely; while Ctrl is held it snaps to 0, 30, 45, 60, 90 and
 * so on. */
static void
frame_move_handle (ScreenatorData *data, ScreenatorDeviceData *devdata,
		   gint x, gint y, gboolean snap_rotation)
{
  ScreenatorFrameShape *shape = &devdata->frame_shape;
  ScreenatorFrame *frame = &shape->frame;
  gdouble lx, ly;

  if (!devdata->frame_active ||
      devdata->frame_drag == SCREENATOR_FRAME_HANDLE_NONE)
    return;

  frame_invalidate (data, shape);

  switch (devdata->frame_drag)
    {
    case SCREENATOR_FRAME_HANDLE_MOVE:
      frame->cx = devdata->frame_press_cx + (x - devdata->frame_press_x);
      frame->cy = devdata->frame_press_cy + (y - devdata->frame_press_y);
      break;
    case SCREENATOR_FRAME_HANDLE_SIZE_TL:
    case SCREENATOR_FRAME_HANDLE_SIZE_TR:
    case SCREENATOR_FRAME_HANDLE_SIZE_BL:
    case SCREENATOR_FRAME_HANDLE_SIZE_BR:
      {
	ScreenatorFrame press;
	gdouble sx, sy, fx, fy;

	/* Work in the frame as it was when the drag started, so the opposite
	 * corner stays fixed. */
	press.cx = devdata->frame_press_cx;
	press.cy = devdata->frame_press_cy;
	press.w = devdata->frame_press_w;
	press.h = devdata->frame_press_h;
	press.rotation = devdata->frame_press_rotation;
	frame_screen_to_local (&press, x, y, &lx, &ly);

	frame_corner_signs (devdata->frame_drag, &sx, &sy);
	fx = -sx * press.w / 2.0;
	fy = -sy * press.h / 2.0;

	if (shape->type == SCREENATOR_SHAPE_TEXT)
	  {
	    gdouble dist = sqrt ((lx - fx) * (lx - fx) +
				 (ly - fy) * (ly - fy));
	    gdouble fixed_x, fixed_y, nfx, nfy;
	    gdouble c, s;

	    if (devdata->frame_press_distance > 1.0 &&
		devdata->frame_press_font_size > 0.0)
	      shape->font_size =
		CLAMP (devdata->frame_press_font_size * dist /
		       devdata->frame_press_distance, 6.0, 300.0);

	    frame_shape_measure_text (shape);

	    /* Keep the fixed corner where it was on screen. */
	    frame_local_to_screen (&press, fx, fy, &fixed_x, &fixed_y);
	    nfx = -sx * frame->w / 2.0;
	    nfy = -sy * frame->h / 2.0;
	    c = cos (frame->rotation);
	    s = sin (frame->rotation);
	    frame->cx = fixed_x - (nfx * c - nfy * s);
	    frame->cy = fixed_y - (nfx * s + nfy * c);
	  }
	else
	  {
	    gdouble ncx, ncy, c, s;

	    if (sx < 0.0)
	      lx = MIN (lx, fx - FRAME_MIN_SIZE);
	    else
	      lx = MAX (lx, fx + FRAME_MIN_SIZE);
	    if (sy < 0.0)
	      ly = MIN (ly, fy - FRAME_MIN_SIZE);
	    else
	      ly = MAX (ly, fy + FRAME_MIN_SIZE);

	    frame->w = fabs (fx - lx);
	    frame->h = fabs (fy - ly);
	    ncx = (fx + lx) / 2.0;
	    ncy = (fy + ly) / 2.0;
	    c = cos (press.rotation);
	    s = sin (press.rotation);
	    frame->cx = press.cx + ncx * c - ncy * s;
	    frame->cy = press.cy + ncx * s + ncy * c;
	  }
      }
      break;
    case SCREENATOR_FRAME_HANDLE_ROTATE:
      {
	gdouble dx = x - frame->cx;
	gdouble dy = y - frame->cy;

	if (dx * dx + dy * dy > 1.0)
	  {
	    frame->rotation = atan2 (dy, dx) + G_PI / 2.0;

	    if (snap_rotation)
	      frame->rotation = snap_common_rotation (frame->rotation);
	  }
      }
      break;
    case SCREENATOR_FRAME_HANDLE_EXTRA:
      if (shape->type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE)
	{
	  frame_screen_to_local (frame, x, y, &lx, &ly);
	  shape->extra = CLAMP (lx + frame->w / 2.0, 0.0,
				MIN (frame->w, frame->h) / 2.0);
	}
      else if (shape->type == SCREENATOR_SHAPE_BARS)
	{
	  gdouble px = x - devdata->frame_press_x;
	  gdouble py = y - devdata->frame_press_y;
	  gdouble moved = sqrt (px * px + py * py);

	  shape->count = CLAMP (2 + (gint) (moved / 12.0), 2, 40);
	}
      else if (shape->type == SCREENATOR_SHAPE_RANDOM)
	{
	  /* Like the parallel-lines count handle: dragging in any direction
	   * away from the handle raises the smoothness. */
	  gdouble px = x - devdata->frame_press_x;
	  gdouble py = y - devdata->frame_press_y;
	  gdouble moved = sqrt (px * px + py * py);

	  shape->extra = CLAMP (1.0 + moved / 10.0, 1.0, 16.0);
	}
      break;
    case SCREENATOR_FRAME_HANDLE_ARROWS:
      if (shape->type == SCREENATOR_SHAPE_BARS)
	{
	  gdouble px = x - devdata->frame_press_x;
	  gdouble py = y - devdata->frame_press_y;
	  gdouble moved = sqrt (px * px + py * py);
	  gint mode = ((gint) (moved / 18.0)) % 4;

	  shape->arrows = (ScreenatorBarsArrows) mode;
	}
      break;
    default:
      break;
    }

  frame_invalidate (data, shape);
}

/* Draw a square handle grip with a dark border, like the polygon's side
 * handle. */
static void
frame_draw_square (cairo_t *cr, gint x, gint y)
{
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.85);
  cairo_rectangle (cr, x - 7.0, y - 7.0, 14.0, 14.0);
  cairo_fill (cr);

  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.95);
  cairo_rectangle (cr, x - 6.0, y - 6.0, 12.0, 12.0);
  cairo_fill (cr);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.9);
  cairo_set_line_width (cr, 1.0);
  cairo_rectangle (cr, x - 6.0, y - 6.0, 12.0, 12.0);
  cairo_stroke (cr);
}

/* Draw a number next to a handle. */
static void
frame_draw_label (cairo_t *cr, gint x, gint y, gint value)
{
  gchar label[8];
  cairo_text_extents_t ext;
  gdouble tx, ty;

  g_snprintf (label, sizeof (label), "%d", value);

  cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
			  CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size (cr, 11.0);
  cairo_text_extents (cr, label, &ext);

  tx = x + 11.0;
  ty = y - ext.y_bearing - ext.height / 2.0;

  cairo_move_to (cr, tx, ty);
  cairo_text_path (cr, label);

  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.9);
  cairo_set_line_width (cr, 3.0);
  cairo_stroke_preserve (cr);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.95);
  cairo_fill (cr);
}

/* A small arrow icon inside the parallel-lines arrow handle, showing the
 * current mode: a dash for none, an up arrow, a down arrow or both. */
static void
frame_draw_bars_arrow_icon (cairo_t *cr, gint x, gint y,
			    ScreenatorBarsArrows arrows)
{
  gboolean top = arrows == SCREENATOR_BARS_ARROWS_TOP ||
		 arrows == SCREENATOR_BARS_ARROWS_BOTH;
  gboolean bottom = arrows == SCREENATOR_BARS_ARROWS_BOTTOM ||
		    arrows == SCREENATOR_BARS_ARROWS_BOTH;

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.9);
  cairo_set_line_width (cr, 1.2);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);

  if (arrows == SCREENATOR_BARS_ARROWS_NONE)
    {
      cairo_move_to (cr, x - 3.0, y);
      cairo_line_to (cr, x + 3.0, y);
      cairo_stroke (cr);
      return;
    }

  cairo_move_to (cr, x, y - 4.0);
  cairo_line_to (cr, x, y + 4.0);
  cairo_stroke (cr);

  if (top)
    {
      cairo_move_to (cr, x, y - 5.5);
      cairo_line_to (cr, x - 2.5, y - 2.0);
      cairo_line_to (cr, x + 2.5, y - 2.0);
      cairo_close_path (cr);
      cairo_fill (cr);
    }

  if (bottom)
    {
      cairo_move_to (cr, x, y + 5.5);
      cairo_line_to (cr, x - 2.5, y + 2.0);
      cairo_line_to (cr, x + 2.5, y + 2.0);
      cairo_close_path (cr);
      cairo_fill (cr);
    }
}

/* Draw the handles of an editable frame shape. */
static void
frame_draw_handles (ScreenatorData *data, cairo_t *cr,
		    const ScreenatorFrameShape *shape)
{
  GdkPoint pos;

  (void) data;

  /* A light spoke connects the centre to the rotation handle. */
  frame_handle_position (shape, SCREENATOR_FRAME_HANDLE_ROTATE, &pos);

  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.8);
  cairo_set_line_width (cr, 3.0);
  cairo_move_to (cr, shape->frame.cx, shape->frame.cy);
  cairo_line_to (cr, pos.x, pos.y);
  cairo_stroke (cr);

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.7);
  cairo_set_line_width (cr, 1.0);
  cairo_move_to (cr, shape->frame.cx, shape->frame.cy);
  cairo_line_to (cr, pos.x, pos.y);
  cairo_stroke (cr);

  star_draw_grip (cr, pos.x, pos.y);

  /* A square size handle on every corner. */
  {
    static const ScreenatorFrameHandle corners[] =
      {
	SCREENATOR_FRAME_HANDLE_SIZE_TL,
	SCREENATOR_FRAME_HANDLE_SIZE_TR,
	SCREENATOR_FRAME_HANDLE_SIZE_BL,
	SCREENATOR_FRAME_HANDLE_SIZE_BR
      };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (corners); i++)
      {
	frame_handle_position (shape, corners[i], &pos);
	frame_draw_square (cr, pos.x, pos.y);
      }
  }

  /* The extra handle is the roundness of the rounded rectangle or the line
   * count of the parallel lines. */
  if (shape->type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE)
    {
      frame_handle_position (shape, SCREENATOR_FRAME_HANDLE_EXTRA, &pos);
      star_draw_grip (cr, pos.x, pos.y);
    }
  else if (shape->type == SCREENATOR_SHAPE_BARS)
    {
      frame_handle_position (shape, SCREENATOR_FRAME_HANDLE_EXTRA, &pos);
      frame_draw_square (cr, pos.x, pos.y);
      frame_draw_label (cr, pos.x, pos.y, CLAMP (shape->count, 1, 40));

      frame_handle_position (shape, SCREENATOR_FRAME_HANDLE_ARROWS, &pos);
      frame_draw_square (cr, pos.x, pos.y);
      frame_draw_bars_arrow_icon (cr, pos.x, pos.y, shape->arrows);
    }
  else if (shape->type == SCREENATOR_SHAPE_RANDOM)
    {
      frame_handle_position (shape, SCREENATOR_FRAME_HANDLE_EXTRA, &pos);
      frame_draw_square (cr, pos.x, pos.y);
      frame_draw_label (cr, pos.x, pos.y,
			CLAMP ((gint) round (shape->extra), 1, 16));
    }
}


/* --- Editable line and arrow -------------------------------------------- */

#define LINE_GRIP_RADIUS 11

/* Repaint the area covered by the line and its handles. */
static void
line_invalidate (ScreenatorData *data, const ScreenatorLine *line)
{
  GdkRectangle bounds;

  if (!data->win)
    return;

  line_get_bounds (data, line, &bounds);
  data->modified = 1;
  gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
			      &bounds, 0);
}

/* Position of the arrow-heads handle: halfway along the line, offset to one
 * side so it never covers the shaft. */
static void
line_heads_handle_position (const ScreenatorLine *line, GdkPoint *pos)
{
  gdouble dx = line->x1 - line->x0;
  gdouble dy = line->y1 - line->y0;
  gdouble length = hypot (dx, dy);
  gdouble mx = (line->x0 + line->x1) / 2.0;
  gdouble my = (line->y0 + line->y1) / 2.0;

  if (length < 1.0)
    {
      pos->x = (gint) round (mx);
      pos->y = (gint) round (my - LINE_HEADS_HANDLE_OFFSET);
      return;
    }

  pos->x = (gint) round (mx - dy / length * LINE_HEADS_HANDLE_OFFSET);
  pos->y = (gint) round (my + dx / length * LINE_HEADS_HANDLE_OFFSET);
}

/* A small horizontal icon in the heads handle: a dash for none, a left arrow
 * for start, a right arrow for end, or both. */
static void
line_draw_heads_icon (cairo_t *cr, gint x, gint y,
		      ScreenatorArrowHeads heads)
{
  gboolean start = heads == SCREENATOR_ARROW_HEADS_START ||
		   heads == SCREENATOR_ARROW_HEADS_BOTH;
  gboolean end = heads == SCREENATOR_ARROW_HEADS_END ||
		 heads == SCREENATOR_ARROW_HEADS_BOTH;

  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.9);
  cairo_set_line_width (cr, 1.2);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);

  if (heads == SCREENATOR_ARROW_HEADS_NONE)
    {
      cairo_move_to (cr, x - 3.0, y);
      cairo_line_to (cr, x + 3.0, y);
      cairo_stroke (cr);
      return;
    }

  cairo_move_to (cr, x - 4.0, y);
  cairo_line_to (cr, x + 4.0, y);
  cairo_stroke (cr);

  if (start)
    {
      cairo_move_to (cr, x - 5.5, y);
      cairo_line_to (cr, x - 2.0, y - 2.5);
      cairo_line_to (cr, x - 2.0, y + 2.5);
      cairo_close_path (cr);
      cairo_fill (cr);
    }

  if (end)
    {
      cairo_move_to (cr, x + 5.5, y);
      cairo_line_to (cr, x + 2.0, y - 2.5);
      cairo_line_to (cr, x + 2.0, y + 2.5);
      cairo_close_path (cr);
      cairo_fill (cr);
    }
}

/* Draw the round grip on each end point and, for an arrow, the heads handle. */
static void
line_draw_handles (ScreenatorData *data, cairo_t *cr,
		   const ScreenatorLine *line)
{
  (void) data;

  star_draw_grip (cr, line->x0, line->y0);
  star_draw_grip (cr, line->x1, line->y1);

  if (line->type == SCREENATOR_SHAPE_ARROW)
    {
      GdkPoint pos;

      line_heads_handle_position (line, &pos);
      frame_draw_square (cr, pos.x, pos.y);
      line_draw_heads_icon (cr, pos.x, pos.y, line->heads);
    }
}

/* The end handle under (x, y), if any.  The line itself is tested later, so
 * the handles win when they overlap it. */
static ScreenatorLineHandle
line_handle_at (const ScreenatorLine *line, gint x, gint y)
{
  gint dx, dy;

  if (line->type == SCREENATOR_SHAPE_ARROW)
    {
      GdkPoint pos;

      line_heads_handle_position (line, &pos);
      dx = pos.x - x;
      dy = pos.y - y;
      if (dx * dx + dy * dy <= LINE_GRIP_RADIUS * LINE_GRIP_RADIUS)
	return SCREENATOR_LINE_HANDLE_HEADS;
    }

  dx = x - line->x0;
  dy = y - line->y0;
  if (dx * dx + dy * dy <= LINE_GRIP_RADIUS * LINE_GRIP_RADIUS)
    return SCREENATOR_LINE_HANDLE_START;

  dx = x - line->x1;
  dy = y - line->y1;
  if (dx * dx + dy * dy <= LINE_GRIP_RADIUS * LINE_GRIP_RADIUS)
    return SCREENATOR_LINE_HANDLE_END;

  return SCREENATOR_LINE_HANDLE_NONE;
}

/* Distance from (x, y) to the line segment. */
static gdouble
line_distance (const ScreenatorLine *line, gint x, gint y)
{
  gdouble dx = line->x1 - line->x0;
  gdouble dy = line->y1 - line->y0;
  gdouble length2 = dx * dx + dy * dy;
  gdouble t;

  if (length2 < 1.0)
    return hypot (x - line->x0, y - line->y0);

  t = ((x - line->x0) * dx + (y - line->y0) * dy) / length2;
  t = CLAMP (t, 0.0, 1.0);

  return hypot (x - (line->x0 + t * dx), y - (line->y0 + t * dy));
}

static gboolean
line_contains (const ScreenatorLine *line, gint x, gint y, gint width)
{
  return line_distance (line, x, y) <= MAX (8.0, width / 2.0 + 4.0);
}

/* Start a new line at the click position; dragging sets the other end. */
static void
line_begin (ScreenatorDeviceData *devdata, ScreenatorShapeType type,
	    gint x, gint y)
{
  devdata->line_active = TRUE;
  devdata->line.type = type;
  devdata->line.x0 = x;
  devdata->line.y0 = y;
  devdata->line.x1 = x;
  devdata->line.y1 = y;
  devdata->line.heads = type == SCREENATOR_SHAPE_ARROW
    ? SCREENATOR_ARROW_HEADS_END : SCREENATOR_ARROW_HEADS_NONE;
  devdata->line_drag = SCREENATOR_LINE_HANDLE_END;
  devdata->line_press_x = x;
  devdata->line_press_y = y;
  devdata->line_press_x0 = x;
  devdata->line_press_y0 = y;
  devdata->line_press_x1 = x;
  devdata->line_press_y1 = y;
}

/* Remember the state at the start of a handle drag. */
static void
line_begin_drag (ScreenatorDeviceData *devdata, ScreenatorLineHandle handle,
		 gint x, gint y)
{
  devdata->line_drag = handle;
  devdata->line_press_x = x;
  devdata->line_press_y = y;
  devdata->line_press_x0 = devdata->line.x0;
  devdata->line_press_y0 = devdata->line.y0;
  devdata->line_press_x1 = devdata->line.x1;
  devdata->line_press_y1 = devdata->line.y1;
}

/* Place one end point at (x, y) and keep the other end fixed.  The direction
 * follows the pointer; while Ctrl is held it snaps to 0, 30, 45, 60, 90 and
 * so on. */
static void
line_place_endpoint (ScreenatorLine *line, gboolean start,
		     gint x, gint y, gboolean snap)
{
  gint ax = start ? line->x1 : line->x0;
  gint ay = start ? line->y1 : line->y0;
  gdouble angle = atan2 (y - ay, x - ax);
  gdouble length = hypot (x - ax, y - ay);

  if (snap)
    angle = snap_common_rotation (angle);

  x = ax + (gint) round (length * cos (angle));
  y = ay + (gint) round (length * sin (angle));

  if (start)
    {
      line->x0 = x;
      line->y0 = y;
    }
  else
    {
      line->x1 = x;
      line->y1 = y;
    }
}

/* Move the grabbed end point or the whole line to (x, y). */
static void
line_move_handle (ScreenatorData *data, ScreenatorDeviceData *devdata,
		  gint x, gint y, gboolean snap)
{
  ScreenatorLine *line = &devdata->line;

  if (!devdata->line_active ||
      devdata->line_drag == SCREENATOR_LINE_HANDLE_NONE)
    return;

  line_invalidate (data, line);

  switch (devdata->line_drag)
    {
    case SCREENATOR_LINE_HANDLE_START:
      line_place_endpoint (line, TRUE, x, y, snap);
      break;
    case SCREENATOR_LINE_HANDLE_END:
      line_place_endpoint (line, FALSE, x, y, snap);
      break;
    case SCREENATOR_LINE_HANDLE_HEADS:
      {
	gdouble px = x - devdata->line_press_x;
	gdouble py = y - devdata->line_press_y;
	gdouble moved = hypot (px, py);

	line->heads = (ScreenatorArrowHeads) (((gint) (moved / 18.0)) % 4);
      }
      break;
    case SCREENATOR_LINE_HANDLE_MOVE:
      line->x0 = devdata->line_press_x0 + (x - devdata->line_press_x);
      line->y0 = devdata->line_press_y0 + (y - devdata->line_press_y);
      line->x1 = devdata->line_press_x1 + (x - devdata->line_press_x);
      line->y1 = devdata->line_press_y1 + (y - devdata->line_press_y);
      break;
    default:
      break;
    }

  line_invalidate (data, line);
}

/* Finish what the active tool is doing and/or leave painting mode.  This is
 * the action of a right click on the canvas, shared by the mouse and by the
 * Escape key so both behave the same. */
static void send_escape_to_close_menu (ScreenatorData *data);

static gboolean
close_menu_timeout (gpointer user_data)
{
  ScreenatorData *data = user_data;

  data->screenshot_close_menu_id = 0;

  if (!data->screenshot_paper || data->top_dialog ||
      screenator_any_grabbed (data) ||
      data->screenshot_escape_attempts >= 3)
    return G_SOURCE_REMOVE;

  data->screenshot_escape_attempts++;
  send_escape_to_close_menu (data);

  /* A menu with a submenu needs more than one Escape.  Give the grab retry a
   * chance to succeed after each one before sending the next. */
  data->screenshot_close_menu_id =
    g_timeout_add (350, close_menu_timeout, data);

  return G_SOURCE_REMOVE;
}

void
screenator_clear_screenshot_paper (ScreenatorData *data)
{
  if (data->screenshot_close_menu_id)
    {
      g_source_remove (data->screenshot_close_menu_id);
      data->screenshot_close_menu_id = 0;
    }
  data->screenshot_escape_attempts = 0;

  if (!data->screenshot_paper)
    return;

  cairo_surface_destroy (data->screenshot_paper);
  data->screenshot_paper = NULL;

  if (data->win && gtk_widget_get_window (data->win))
    gdk_window_invalidate_rect (gtk_widget_get_window (data->win), NULL, 0);
}

static GdkPixbuf *
flatten_screenshot_pixbuf (GdkPixbuf *pixbuf)
{
  GdkPixbuf *opaque;
  gint width, height;

  if (!pixbuf || !gdk_pixbuf_get_has_alpha (pixbuf))
    return pixbuf;

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  opaque = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, width, height);
  if (!opaque)
    {
      g_object_unref (pixbuf);
      return NULL;
    }

  gdk_pixbuf_fill (opaque, 0x000000ff);
  gdk_pixbuf_composite (pixbuf, opaque, 0, 0, width, height,
			0, 0, 1.0, 1.0, GDK_INTERP_NEAREST, 255);
  g_object_unref (pixbuf);

  return opaque;
}

static cairo_surface_t *
load_screenshot_file (const gchar *path)
{
  GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file (path, NULL);
  cairo_surface_t *surface;

  pixbuf = flatten_screenshot_pixbuf (pixbuf);
  surface = pixbuf
    ? gdk_cairo_surface_create_from_pixbuf (pixbuf, 1, NULL) : NULL;

  if (pixbuf)
    g_object_unref (pixbuf);

  return surface;
}

/* Prefer a real screenshot tool: on a compositor the root window can return
 * the desktop background without the composited application windows. */
static cairo_surface_t *
capture_screen_external (void)
{
  static const gchar * const programs[] = {
    "spectacle", "gnome-screenshot", "import", "grim"
  };
  gchar *path = g_strdup_printf ("%s/screenator-shot-%d.png",
				 g_get_tmp_dir (), (gint) getpid ());
  cairo_surface_t *surface = NULL;
  guint i;

  g_unlink (path);

  for (i = 0; i < G_N_ELEMENTS (programs) && !surface; i++)
    {
      gchar *program = g_find_program_in_path (programs[i]);
      gchar *argv[8];
      gint argc = 0;
      gint status = 0;
      GError *error = NULL;

      if (!program)
	continue;

      argv[argc++] = program;
      if (g_strcmp0 (programs[i], "import") == 0)
	{
	  argv[argc++] = "-window";
	  argv[argc++] = "root";
	}
      else if (g_strcmp0 (programs[i], "spectacle") == 0)
	{
	  argv[argc++] = "-b";
	  argv[argc++] = "-n";
	  argv[argc++] = "-o";
	}
      else if (g_strcmp0 (programs[i], "gnome-screenshot") == 0)
	argv[argc++] = "-f";
      argv[argc++] = path;
      argv[argc] = NULL;

      if (g_spawn_sync (NULL, argv, NULL,
			G_SPAWN_SEARCH_PATH |
			G_SPAWN_STDERR_TO_DEV_NULL |
			G_SPAWN_STDOUT_TO_DEV_NULL,
			NULL, NULL, NULL, NULL, &status, &error))
	{
	  gint attempt;

	  /* A background screenshot tool can exit just before its output file
	   * is written.  Give it a short moment to appear. */
	  for (attempt = 0; attempt < 20 && !surface; attempt++)
	    {
	      GStatBuf st;

	      if (g_stat (path, &st) == 0 && st.st_size > 0)
		{
		  surface = load_screenshot_file (path);
		  if (surface)
		    g_printerr ("Screenshot captured with %s.\n", programs[i]);
		  break;
		}
	      g_usleep (50000);
	    }
	}

      g_clear_error (&error);
      g_free (program);
      g_unlink (path);
    }

  g_free (path);
  return surface;
}

static void
send_escape_to_close_menu (ScreenatorData *data)
{
#ifdef HAVE_XTEST
  Display *xdisplay;
  KeyCode keycode;
  int event_base, error_base, major, minor;

  if (!data->display || !GDK_IS_X11_DISPLAY (data->display))
    return;

  xdisplay = GDK_DISPLAY_XDISPLAY (data->display);
  if (!XTestQueryExtension (xdisplay, &event_base, &error_base,
			    &major, &minor))
    return;

  keycode = XKeysymToKeycode (xdisplay, XK_Escape);
  if (!keycode)
    return;

  /* Escape closes the context menu that still owns the pointer grab, while
   * the frozen screenshot keeps its picture on screen. */
  XTestFakeKeyEvent (xdisplay, keycode, True, CurrentTime);
  XTestFakeKeyEvent (xdisplay, keycode, False, CurrentTime);
  XFlush (xdisplay);
#endif
}

/* Freeze the current screen into a temporary background.  Hiding the overlay
 * and icon first keeps the screenshot free of Screenator's own windows. */
void
screenator_take_screenshot_paper (ScreenatorData *data)
{
  GdkWindow *root;
  GdkPixbuf *pixbuf;
  cairo_surface_t *surface;
  gboolean win_visible, icon_visible, settings_visible;

  screenator_clear_screenshot_paper (data);

  if (!data->win || !data->display || !data->width || !data->height)
    return;

  win_visible = gtk_widget_get_visible (data->win);
  icon_visible = data->icon_win && gtk_widget_get_visible (data->icon_win);
  settings_visible = data->settings_win &&
    gtk_widget_get_visible (data->settings_win);

  if (settings_visible)
    gtk_widget_hide (data->settings_win);
  if (icon_visible)
    gtk_widget_hide (data->icon_win);
  if (win_visible)
    gtk_widget_hide (data->win);

  gdk_display_sync (gdk_display_get_default ());
  gdk_window_process_all_updates ();

  surface = capture_screen_external ();

  if (!surface)
    {
      root = gdk_get_default_root_window ();
      pixbuf = root ? gdk_pixbuf_get_from_window (root, 0, 0,
						  data->width, data->height)
		    : NULL;
      pixbuf = flatten_screenshot_pixbuf (pixbuf);
      surface = pixbuf
	? gdk_cairo_surface_create_from_pixbuf (pixbuf, 1, NULL) : NULL;
      if (pixbuf)
	g_object_unref (pixbuf);
    }

  if (surface)
    data->screenshot_paper = surface;
  else
    g_printerr ("Screenshot capture failed; using the live screen.\n");

  if (win_visible)
    gtk_widget_show (data->win);
  if (icon_visible)
    gtk_widget_show (data->icon_win);
  if (settings_visible)
    gtk_widget_show (data->settings_win);

  if (data->win && gtk_widget_get_window (data->win))
    {
      gdk_window_raise (gtk_widget_get_window (data->win));
      gdk_window_invalidate_rect (gtk_widget_get_window (data->win), NULL, 0);
    }
  raise_icon_window (data);
  update_settings_indicator (data);

  /* Let the frozen picture become visible before dismissing the real menu.
   * Otherwise the menu can disappear for one frame while the overlay is still
   * being mapped. */
  if (data->screenshot_paper)
    {
      data->screenshot_escape_attempts = 0;
      data->screenshot_close_menu_id =
	g_timeout_add (150, close_menu_timeout, data);
    }
}

gboolean
screenator_exit_tool (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;
  gboolean handled = FALSE;

  if (!data->devdatatable)
    return FALSE;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      ScreenatorDeviceData *devdata = value;

      if (devdata->frame_active)
	{
	  frame_finish (data, devdata);
	  handled = TRUE;
	}
      else if (devdata->line_active)
	{
	  line_finish (data, devdata);
	  handled = TRUE;
	}
      else if (devdata->cur_context &&
	       devdata->cur_context->type == SCREENATOR_SHAPE &&
	       data->shape_type == SCREENATOR_SHAPE_SPLINE &&
	       devdata->shape_active)
	{
	  spline_finish (data, devdata);
	  handled = TRUE;
	}
      else if (devdata->cur_context &&
	       devdata->cur_context->type == SCREENATOR_SHAPE &&
	       data->shape_type == SCREENATOR_SHAPE_STAR &&
	       devdata->star_active)
	{
	  star_finish (data, devdata);
	  handled = TRUE;
	}
      else if (devdata->cur_context &&
	       devdata->cur_context->type == SCREENATOR_SHAPE &&
	       data->shape_type == SCREENATOR_SHAPE_TRIANGLE &&
	       devdata->triangle_active)
	{
	  triangle_finish (data, devdata);
	  handled = TRUE;
	}
    }

  if (!handled && screenator_any_grabbed (data))
    {
      screenator_marker_leave (data);
      screenator_clear_screenshot_paper (data);
      release_grab (data, NULL);
      handled = TRUE;
    }

  return handled;
}

/* Some XWayland setups report a pen as a generic pointer with a mouse-like
 * source.  The device tool and the device name still identify it. */
static gboolean
device_name_looks_like_stylus (GdkDevice *device)
{
  const gchar *name;
  gchar *lower;
  gboolean match;

  if (!device)
    return FALSE;

  name = gdk_device_get_name (device);
  if (!name)
    return FALSE;

  lower = g_ascii_strdown (name, -1);
  match = g_strstr_len (lower, -1, "pen") != NULL ||
	  g_strstr_len (lower, -1, "stylus") != NULL ||
	  g_strstr_len (lower, -1, "tablet") != NULL ||
	  g_strstr_len (lower, -1, "wacom") != NULL ||
	  g_strstr_len (lower, -1, "eraser") != NULL ||
	  g_strstr_len (lower, -1, "huion") != NULL ||
	  g_strstr_len (lower, -1, "xp-pen") != NULL ||
	  g_strstr_len (lower, -1, "xp pen") != NULL;
  g_free (lower);
  return match;
}

/* TRUE when the event comes from a pen or the eraser end of a pen rather
 * than a mouse or touchpad. */
static gboolean
event_source_is_stylus (const GdkEvent *event)
{
  GdkDevice *source = gdk_event_get_source_device (event);
  GdkDevice *device = gdk_event_get_device (event);
  GdkDeviceTool *tool = gdk_event_get_device_tool (event);
  GdkInputSource input_source;

  if (tool)
    {
      switch (gdk_device_tool_get_tool_type (tool))
	{
	case GDK_DEVICE_TOOL_TYPE_PEN:
	case GDK_DEVICE_TOOL_TYPE_ERASER:
	case GDK_DEVICE_TOOL_TYPE_BRUSH:
	case GDK_DEVICE_TOOL_TYPE_PENCIL:
	case GDK_DEVICE_TOOL_TYPE_AIRBRUSH:
	  return TRUE;
	default:
	  break;
	}
    }

  if (source)
    {
      input_source = gdk_device_get_source (source);
      if (input_source == GDK_SOURCE_PEN ||
	  input_source == GDK_SOURCE_ERASER)
	return TRUE;
    }

  return device_name_looks_like_stylus (source) ||
	 device_name_looks_like_stylus (device);
}

typedef struct
{
  ScreenatorData       *data;
  ScreenatorDeviceData *devdata;
  gboolean              was_grabbed;
} StylusMenu;

static void
stylus_menu_color (GtkMenuItem *item, gpointer user_data)
{
  ((StylusMenu *) user_data)->data->stylus_mode =
    SCREENATOR_STYLUS_COLOR;
}

static void
stylus_menu_thickness (GtkMenuItem *item, gpointer user_data)
{
  ((StylusMenu *) user_data)->data->stylus_mode =
    SCREENATOR_STYLUS_THICKNESS;
}

/* A bold, non-selectable heading for a section of the stylus menu. */
static GtkWidget *
stylus_menu_heading (const gchar *text)
{
  GtkWidget *item = gtk_menu_item_new_with_label (text);
  GtkWidget *label = gtk_bin_get_child (GTK_BIN (item));
  gchar *markup = g_markup_printf_escaped ("<b>%s</b>", text);

  if (label)
    gtk_label_set_markup (GTK_LABEL (label), markup);
  g_free (markup);
  gtk_widget_set_sensitive (item, FALSE);
  return item;
}

static void
stylus_menu_done (GtkMenuShell *menu, gpointer user_data)
{
  StylusMenu *ctx = user_data;

  if (ctx->data->stylus_menu == GTK_WIDGET (menu))
    ctx->data->stylus_menu = NULL;

  if (ctx->devdata)
    ctx->devdata->menu_button_down = FALSE;

  /* The menu only selects a Button 1 Mode, so painting always resumes. */
  if (ctx->was_grabbed)
    acquire_grab (ctx->data, NULL);

  g_free (ctx);
}

/* Show the stylus side-button menu at the pen position.  The painting grab is
 * released while the menu is open so the menu can take the pointer and
 * keyboard, and it is taken back when the menu closes. */
static void
show_stylus_menu (ScreenatorData *data, ScreenatorDeviceData *devdata,
		  gint x, gint y)
{
  StylusMenu *ctx;
  GtkWidget *menu, *mode_heading, *color, *thickness;
  GdkWindow *root;
  GdkRectangle rect;
  gboolean was_grabbed;

  if (data->stylus_menu)
    return;

  /* A side-button action may still be in progress when the menu opens. */
  devdata->stylus_eraser_active = FALSE;
  devdata->menu_button_down = FALSE;
  devdata->stylus_side_button_down = FALSE;

  if (data->stylus_eraser_hold_id)
    {
      g_source_remove (data->stylus_eraser_hold_id);
      data->stylus_eraser_hold_id = 0;
      data->stylus_eraser_hold_devdata = NULL;
    }

  was_grabbed = screenator_any_grabbed (data);
  if (was_grabbed)
    release_grab (data, NULL);

  ctx = g_new0 (StylusMenu, 1);
  ctx->data = data;
  ctx->devdata = devdata;
  ctx->was_grabbed = was_grabbed;

  menu = gtk_menu_new ();
  mode_heading = stylus_menu_heading ("Button 1 Mode");
  color = gtk_radio_menu_item_new_with_label (NULL, "Color");
  thickness = gtk_radio_menu_item_new_with_label_from_widget (
    GTK_RADIO_MENU_ITEM (color), "Thickness");

  if (data->stylus_mode == SCREENATOR_STYLUS_THICKNESS)
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (thickness), TRUE);
  else
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (color), TRUE);

  g_signal_connect (color, "activate",
		    G_CALLBACK (stylus_menu_color), ctx);
  g_signal_connect (thickness, "activate",
		    G_CALLBACK (stylus_menu_thickness), ctx);

  gtk_menu_shell_append (GTK_MENU_SHELL (menu), mode_heading);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), color);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), thickness);
  gtk_widget_show_all (menu);

  data->stylus_menu = menu;
  g_signal_connect (menu, "selection-done",
		    G_CALLBACK (stylus_menu_done), ctx);

  root = gdk_screen_get_root_window (gdk_screen_get_default ());
  rect.x = x;
  rect.y = y;
  rect.width = 1;
  rect.height = 1;
  gtk_menu_popup_at_rect (GTK_MENU (menu), root, &rect,
			  GDK_GRAVITY_NORTH_WEST,
			  GDK_GRAVITY_NORTH_WEST, NULL);

  /* GTK's menu popup can silently fail when another application holds the
   * keyboard grab.  Recover instead of leaving painting mode released. */
  if (!gtk_widget_get_visible (menu))
    {
      data->stylus_menu = NULL;
      g_signal_handlers_disconnect_by_data (menu, ctx);
      gtk_widget_destroy (menu);
      if (was_grabbed)
	acquire_grab (data, NULL);
      g_free (ctx);
    }
}

typedef struct
{
  ScreenatorData       *data;
  ScreenatorDeviceData *devdata;
  gboolean              was_grabbed;
} WheelMenu;

static void
wheel_menu_color (GtkMenuItem *item, gpointer user_data)
{
  ((WheelMenu *) user_data)->data->wheel_mode = SCREENATOR_WHEEL_COLOR;
}

static void
wheel_menu_thickness (GtkMenuItem *item, gpointer user_data)
{
  ((WheelMenu *) user_data)->data->wheel_mode =
    SCREENATOR_WHEEL_THICKNESS;
}

static void
wheel_menu_done (GtkMenuShell *menu, gpointer user_data)
{
  WheelMenu *ctx = user_data;

  if (ctx->data->wheel_menu == GTK_WIDGET (menu))
    ctx->data->wheel_menu = NULL;

  if (ctx->devdata)
    ctx->devdata->menu_button_down = FALSE;

  if (ctx->was_grabbed)
    acquire_grab (ctx->data, NULL);

  g_free (ctx);
}

/* Long right-click menu for the mouse: what the wheel changes. */
static void
show_wheel_menu (ScreenatorData *data, ScreenatorDeviceData *devdata,
		 gint x, gint y)
{
  WheelMenu *ctx;
  GtkWidget *menu, *heading, *color, *thickness;
  GdkWindow *root;
  GdkRectangle rect;
  gboolean was_grabbed;

  if (data->wheel_menu)
    return;

  devdata->menu_button_down = FALSE;

  was_grabbed = screenator_any_grabbed (data);
  if (was_grabbed)
    release_grab (data, NULL);

  ctx = g_new0 (WheelMenu, 1);
  ctx->data = data;
  ctx->devdata = devdata;
  ctx->was_grabbed = was_grabbed;

  menu = gtk_menu_new ();
  heading = stylus_menu_heading ("Scroll Mode");
  color = gtk_radio_menu_item_new_with_label (NULL, "Color");
  thickness = gtk_radio_menu_item_new_with_label_from_widget (
    GTK_RADIO_MENU_ITEM (color), "Thickness");

  if (data->wheel_mode == SCREENATOR_WHEEL_THICKNESS)
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (thickness), TRUE);
  else
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (color), TRUE);

  g_signal_connect (color, "activate",
		    G_CALLBACK (wheel_menu_color), ctx);
  g_signal_connect (thickness, "activate",
		    G_CALLBACK (wheel_menu_thickness), ctx);

  gtk_menu_shell_append (GTK_MENU_SHELL (menu), heading);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), color);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), thickness);
  gtk_widget_show_all (menu);

  data->wheel_menu = menu;
  g_signal_connect (menu, "selection-done",
		    G_CALLBACK (wheel_menu_done), ctx);

  root = gdk_screen_get_root_window (gdk_screen_get_default ());
  rect.x = x;
  rect.y = y;
  rect.width = 1;
  rect.height = 1;
  gtk_menu_popup_at_rect (GTK_MENU (menu), root, &rect,
			  GDK_GRAVITY_NORTH_WEST,
			  GDK_GRAVITY_NORTH_WEST, NULL);

  if (!gtk_widget_get_visible (menu))
    {
      data->wheel_menu = NULL;
      g_signal_handlers_disconnect_by_data (menu, ctx);
      gtk_widget_destroy (menu);
      if (was_grabbed)
	acquire_grab (data, NULL);
      g_free (ctx);
    }
}

typedef struct
{
  ScreenatorData       *data;
  ScreenatorDeviceData *devdata;
  gboolean              was_grabbed;
  gboolean              first_release_ignored;
} MarkerMenu;

/* The menu opens while the button that opened it is still held.  Swallow that
 * first release so it cannot activate the item under the pointer. */
static gboolean
marker_menu_button_release (GtkWidget *widget, GdkEventButton *event,
			    gpointer user_data)
{
  MarkerMenu *ctx = user_data;

  if (ctx->first_release_ignored)
    return FALSE;

  ctx->first_release_ignored = TRUE;
  return TRUE;
}

static void
marker_menu_opacity (GtkMenuItem *item, gpointer user_data)
{
  MarkerMenu *ctx = user_data;
  guint percent = GPOINTER_TO_UINT (
    g_object_get_data (G_OBJECT (item), "marker-opacity"));

  ctx->data->marker_opacity = percent / 100.0;
  ctx->data->marker_color.alpha = ctx->data->marker_opacity;
  if (ctx->data->default_marker)
    gdk_cairo_set_source_rgba (ctx->data->default_marker->paint_ctx,
			       &ctx->data->marker_color);

  write_keyfile (ctx->data);
}

static void
marker_menu_done (GtkMenuShell *menu, gpointer user_data)
{
  MarkerMenu *ctx = user_data;

  if (ctx->data->marker_menu == GTK_WIDGET (menu))
    ctx->data->marker_menu = NULL;

  if (ctx->devdata)
    ctx->devdata->menu_button_down = FALSE;

  if (ctx->was_grabbed)
    acquire_grab (ctx->data, NULL);

  g_free (ctx);
}

/* Long right-click menu for the Marker: its own opacity, independent of the
 * global pen opacity. */
static void
show_marker_menu (ScreenatorData *data, ScreenatorDeviceData *devdata,
		  gint x, gint y)
{
  static const guint percents[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90 };
  MarkerMenu *ctx;
  GtkWidget *menu, *heading, *item, *first = NULL;
  GtkWidget *items[G_N_ELEMENTS (percents)];
  GdkWindow *root;
  GdkRectangle rect;
  gboolean was_grabbed;
  guint i;

  if (data->marker_menu)
    return;

  devdata->menu_button_down = FALSE;

  was_grabbed = screenator_any_grabbed (data);
  if (was_grabbed)
    release_grab (data, NULL);

  ctx = g_new0 (MarkerMenu, 1);
  ctx->data = data;
  ctx->devdata = devdata;
  ctx->was_grabbed = was_grabbed;

  menu = gtk_menu_new ();
  heading = stylus_menu_heading ("Marker Opacity");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), heading);

  for (i = 0; i < G_N_ELEMENTS (percents); i++)
    {
      gchar *label = g_strdup_printf ("%u%%", percents[i]);

      if (!first)
	{
	  first = gtk_radio_menu_item_new_with_label (NULL, label);
	  item = first;
	}
      else
	item = gtk_radio_menu_item_new_with_label_from_widget (
	  GTK_RADIO_MENU_ITEM (first), label);
      g_free (label);

      g_object_set_data (G_OBJECT (item), "marker-opacity",
			 GINT_TO_POINTER ((gint) percents[i]));

      if (fabs (data->marker_opacity - percents[i] / 100.0) < 0.001)
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);

      items[i] = item;
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    }

  /* Connect after showing the menu, so any activation during construction or
   * the initial radio selection cannot change the stored opacity. */
  gtk_widget_show_all (menu);
  for (i = 0; i < G_N_ELEMENTS (percents); i++)
    g_signal_connect (items[i], "activate",
		      G_CALLBACK (marker_menu_opacity), ctx);

  data->marker_menu = menu;
  g_signal_connect (menu, "button-release-event",
		    G_CALLBACK (marker_menu_button_release), ctx);
  g_signal_connect (menu, "selection-done",
		    G_CALLBACK (marker_menu_done), ctx);

  root = gdk_screen_get_root_window (gdk_screen_get_default ());
  rect.x = x;
  rect.y = y;
  rect.width = 1;
  rect.height = 1;
  gtk_menu_popup_at_rect (GTK_MENU (menu), root, &rect,
			  GDK_GRAVITY_NORTH_WEST,
			  GDK_GRAVITY_NORTH_WEST, NULL);

  if (!gtk_widget_get_visible (menu))
    {
      data->marker_menu = NULL;
      g_signal_handlers_disconnect_by_data (menu, ctx);
      gtk_widget_destroy (menu);
      if (was_grabbed)
	acquire_grab (data, NULL);
      g_free (ctx);
    }
}

/* A long press on the second side button opens the menu; a short click on the
 * release performs Escape instead. */
#define MENU_HOLD_MS 500

static gboolean
menu_hold_timeout (gpointer user_data)
{
  ScreenatorData *data = user_data;
  ScreenatorDeviceData *devdata = data->menu_hold_devdata;

  data->menu_hold_id = 0;
  data->menu_hold_devdata = NULL;

  if (devdata)
    {
      /* The Marker opens its own opacity menu; the Eraser uses the first
       * side button for its size and has no menu.  A short click still
       * performs Escape when it is released. */
      if (marker_tool_active (data))
	show_marker_menu (data, devdata,
			  data->menu_hold_x, data->menu_hold_y);
      else if (data->menu_hold_is_stylus && eraser_tool_active (data))
	{
	  /* No menu for the Eraser tool. */
	}
      else if (data->menu_hold_is_stylus)
	show_stylus_menu (data, devdata,
			  data->menu_hold_x, data->menu_hold_y);
      else
	show_wheel_menu (data, devdata,
			 data->menu_hold_x, data->menu_hold_y);
    }

  return G_SOURCE_REMOVE;
}

/* A long press of the first side button temporarily activates the eraser,
 * even while the pen hovers above the tablet. */
#define STYLUS_ERASER_HOLD_MS 500

static gboolean
stylus_eraser_hold_timeout (gpointer user_data)
{
  ScreenatorData *data = user_data;
  ScreenatorDeviceData *devdata = data->stylus_eraser_hold_devdata;

  data->stylus_eraser_hold_id = 0;
  data->stylus_eraser_hold_devdata = NULL;

  if (!devdata || !devdata->is_grabbed ||
      !devdata->stylus_side_button_down)
    return G_SOURCE_REMOVE;

  devdata->stylus_eraser_active = TRUE;

  if (devdata->cur_context != data->default_eraser)
    {
      devdata->saved_context = devdata->cur_context;
      devdata->cur_context = data->default_eraser;
    }

  set_device_cursor (data, devdata->device, data->default_eraser);

  data->maxwidth = data->default_eraser->width;
  stroke_begin (data, devdata->device);
  draw_line (data, devdata->device, devdata->lastx, devdata->lasty,
	     devdata->lastx, devdata->lasty);

  return G_SOURCE_REMOVE;
}

gboolean on_buttonpress (GtkWidget *win, 
			 GdkEventButton *ev,
			 gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  gdouble pressure = 1;

  /* A dialog is modal: clicks outside it must not paint on the annotation
   * window that is visible behind it. */
  if (data->top_dialog)
    return TRUE;

  /* get the data for this device */
  ScreenatorDeviceData *devdata = g_hash_table_lookup(data->devdatatable, ev->device);

  if(data->debug)
    g_printerr("DEBUG: Device '%s': Button %i Down State %d at (x,y)=(%.2f : %.2f)\n",
	       gdk_device_get_name(ev->device), ev->button, ev->state, ev->x, ev->y);

  /* The text editor disables the mouse: every button is swallowed until the
   * typed text is accepted with Enter (or cancelled with Escape).  The click
   * must not commit the text and must not reach the application below. */
  if (data->text_editor)
    {
      data->text_pressed = FALSE;
      return TRUE;
    }

  if (!devdata->is_grabbed)
    return FALSE;

  /* The right button is a dual-action button: a short click acts like Escape
   * / right click and a long press opens the mouse wheel menu.  On a stylus
   * the same applies to the second side button (button 3), which opens the
   * Button 1 Mode menu instead. */
  if (ev->button == 3)
    {
      devdata->menu_button_down = TRUE;

      if (data->menu_hold_id)
	g_source_remove (data->menu_hold_id);

      data->menu_hold_devdata = devdata;
      data->menu_hold_is_stylus =
	event_source_is_stylus ((GdkEvent *) ev);
      data->menu_hold_x = ev->x_root;
      data->menu_hold_y = ev->y_root;
      data->menu_hold_id = g_timeout_add (MENU_HOLD_MS,
					  menu_hold_timeout,
					  data);
      return TRUE;
    }

  /* The stylus first side button (button 2): a short press steps through
   * colors or thicknesses, a long press activates the eraser. */
  if (event_source_is_stylus ((GdkEvent *) ev) && ev->button == 2)
    {
      devdata->stylus_side_button_down = TRUE;
      devdata->lastx = ev->x;
      devdata->lasty = ev->y;
      devdata->motion_time = ev->time;

      if (data->stylus_eraser_hold_id)
	g_source_remove (data->stylus_eraser_hold_id);

      data->stylus_eraser_hold_devdata = devdata;
      data->stylus_eraser_hold_id =
	g_timeout_add (STYLUS_ERASER_HOLD_MS,
		       stylus_eraser_hold_timeout, data);
      return TRUE;
    }

  /* Do not drop unprocessed motion events.  Compressed events make fast
   * pointer movements look like a sequence of straight segments. */
  gdk_window_set_event_compression(gtk_widget_get_window(data->win), FALSE);

  /* See GdkModifierType. Am I fixing a Gtk misbehaviour???  */
  ev->state |= 1 << (ev->button + 7);


  /* A long press of the stylus first side button keeps the eraser active even
   * though the button is held; do not let the tool lookup replace it. */
  if (!devdata->stylus_eraser_active &&
      (ev->state != devdata->state ||
       devdata->lastslave != gdk_event_get_source_device ((GdkEvent *) ev)))
    select_tool (data, ev->device, gdk_event_get_source_device ((GdkEvent *) ev), ev->state);

  /* The Text tool writes on screen instead of painting a stroke.  Remember
   * where the button was pressed; the text is placed there when the button
   * is released, even if the pointer moved slightly in between.  A text that
   * was just placed stays editable with its handles until the next click
   * outside it. */
  if (devdata->cur_context &&
      devdata->cur_context->type == SCREENATOR_TEXT)
    {
      if (ev->button == 1)
	{
	  if (devdata->frame_active &&
	      devdata->frame_shape.type == SCREENATOR_SHAPE_TEXT)
	    {
	      ScreenatorFrameHandle handle =
		frame_handle_at (&devdata->frame_shape, ev->x, ev->y);

	      if (handle != SCREENATOR_FRAME_HANDLE_NONE)
		{
		  frame_begin_drag (data, devdata, handle, ev->x, ev->y);
		  return TRUE;
		}

	      if (frame_contains (&devdata->frame_shape, ev->x, ev->y))
		{
		  frame_begin_drag (data, devdata,
				    SCREENATOR_FRAME_HANDLE_MOVE,
				    ev->x, ev->y);
		  return TRUE;
		}

	      /* A click outside commits the text; a new editor opens at the
	       * click position. */
	      frame_finish (data, devdata);
	    }

	  data->text_x = ev->x;
	  data->text_y = ev->y;
	  data->text_pressed = TRUE;
	}

      /* The middle button erases while it is held, even with the Text tool
       * selected.  Any other button is swallowed. */
      if (ev->button != 2)
	return TRUE;
    }

  /* Middle mouse button temporarily activates eraser while pressed */
  if (ev->button == 2 &&
      devdata->cur_context != data->default_eraser)
    {
      /* Finish a translucent pen stroke first, so the eraser does not paint
       * into the stroke's layer. */
      stroke_finish (data, ev->device);
      devdata->saved_context = devdata->cur_context;
      devdata->cur_context = data->default_eraser;

      /* Show the eraser cursor (at the selected eraser size) while the
       * middle button erases. */
      set_device_cursor (data, ev->device, data->default_eraser);
    }

  /* Shapes are drawn by dragging.  Only remember the anchor here and show a
   * preview until the button is released.  The spline is different: every
   * click places a node and the curve is finished with a double-click or a
   * right-click. */
  if (devdata->cur_context &&
      devdata->cur_context->type == SCREENATOR_SHAPE)
    {
      if (ev->button == 1)
	{
	  /* The paint bucket acts on a single click: it floods the enclosed
	   * region under the pointer and leaves no preview behind. */
	  if (data->shape_type == SCREENATOR_SHAPE_BUCKET)
	    {
	      fill_bucket (data, (gint) ev->x, (gint) ev->y);
	      return TRUE;
	    }

	  if (data->shape_type == SCREENATOR_SHAPE_SPLINE)
	    {
	      gint index;
	      gboolean outgoing;
	      GArray *preview;

	      if (ev->type == GDK_2BUTTON_PRESS)
		{
		  spline_finish (data, devdata);
		  return TRUE;
		}

	      /* Test the handles against the preview that is on screen, so
	       * the handle the user aimed at is the one that gets grabbed. */
	      preview = spline_preview_points (devdata);
	      if (spline_handle_at (preview, ev->x, ev->y,
				    &index, &outgoing))
		{
		  g_array_free (preview, TRUE);

		  /* Pressing a Bezier handle starts a handle drag.  The
		   * rubber-band segment is dropped first so later previews
		   * keep the handles in place. */
		  spline_park_cursor (data, devdata);
		  devdata->spline_handle_drag = TRUE;
		  devdata->spline_drag_index = index;
		  devdata->spline_drag_out = outgoing;
		  return TRUE;
		}
	      g_array_free (preview, TRUE);

	      spline_park_cursor (data, devdata);

	      if (spline_node_at (devdata, ev->x, ev->y, &index))
		{
		  /* Pressing a node starts a node drag.  Clicking the last
		   * node again (without moving) finishes the curve. */
		  devdata->spline_node_drag = TRUE;
		  devdata->spline_drag_index = index;
		  devdata->spline_finish_pending =
		    (index == (gint) devdata->shape_points->len - 1);
		  devdata->spline_press_x = ev->x;
		  devdata->spline_press_y = ev->y;
		}
	      else
		{
		  spline_add_node (data, devdata, ev->x, ev->y);
		}

	      return TRUE;
	    }

	  if (data->shape_type == SCREENATOR_SHAPE_STAR)
	    {
	      ScreenatorStarHandle handle;

	      if (ev->type == GDK_2BUTTON_PRESS)
		{
		  star_finish (data, devdata);
		  return TRUE;
		}

	      if (devdata->star_active)
		{
		  handle = star_handle_at (&devdata->star, ev->x, ev->y);

		  if (handle != SCREENATOR_STAR_HANDLE_NONE)
		    {
		      devdata->star_drag = handle;
		      devdata->star_press_x = ev->x;
		      devdata->star_press_y = ev->y;

		      /* The side handle starts at three sides; moving the
		       * pointer away from here adds more. */
		      if (handle == SCREENATOR_STAR_HANDLE_POINTS)
			{
			  devdata->star.points = 3;
			  star_invalidate (data, &devdata->star);
			}
		      return TRUE;
		    }

		  /* A press inside the star grabs it and moves it. */
		  if (star_contains (&devdata->star, ev->x, ev->y))
		    {
		      devdata->star_drag = SCREENATOR_STAR_HANDLE_CENTRE;
		      devdata->star_press_x = ev->x;
		      devdata->star_press_y = ev->y;
		      devdata->star_press_cx = devdata->star.cx;
		      devdata->star_press_cy = devdata->star.cy;
		      return TRUE;
		    }

		  /* A press outside commits the current star and places the
		   * next one at the click position. */
		  star_finish (data, devdata);
		}

	      star_begin (data, devdata, ev->x, ev->y);
	      return TRUE;
	    }

	  if (data->shape_type == SCREENATOR_SHAPE_TRIANGLE)
	    {
	      ScreenatorTriangleHandle handle;

	      if (ev->type == GDK_2BUTTON_PRESS ||
		  ev->type == GDK_3BUTTON_PRESS)
		{
		  /* The clicks that place the vertices can also look like a
		   * double click.  Only commit a finished triangle that was
		   * not placed by this same click sequence. */
		  if (devdata->triangle_active &&
		      devdata->triangle_vertices >= 3 &&
		      (guint32) (ev->time - devdata->triangle_placed_time) > 300)
		    triangle_finish (data, devdata);
		  return TRUE;
		}

	      if (devdata->triangle_active &&
		  devdata->triangle_vertices < 3)
		{
		  triangle_place_next_vertex (data, devdata, ev->x, ev->y,
					      ev->time);
		  return TRUE;
		}

	      if (devdata->triangle_active)
		{
		  handle = triangle_handle_at (&devdata->triangle,
					       ev->x, ev->y);

		  if (handle != SCREENATOR_TRIANGLE_HANDLE_NONE)
		    {
		      devdata->triangle_drag = handle;
		      return TRUE;
		    }

		  /* A press inside the triangle grabs it and moves it. */
		  if (triangle_contains (&devdata->triangle, ev->x, ev->y))
		    {
		      gint i;

		      devdata->triangle_drag =
			SCREENATOR_TRIANGLE_HANDLE_CENTRE;
		      devdata->triangle_press_x = ev->x;
		      devdata->triangle_press_y = ev->y;

		      for (i = 0; i < 3; i++)
			devdata->triangle_press_v[i] = devdata->triangle.v[i];

		      return TRUE;
		    }

		  /* A press outside commits the current triangle and places
		   * the next one at the click position. */
		  triangle_finish (data, devdata);
		}

	      triangle_begin (data, devdata, ev->x, ev->y);
	      return TRUE;
	    }

	  /* The line and arrow stay editable after the drag: each end
	   * point has a round handle, and pressing the line itself moves
	   * it. */
	  if (data->shape_type == SCREENATOR_SHAPE_LINE ||
	      data->shape_type == SCREENATOR_SHAPE_ARROW)
	    {
	      ScreenatorLineHandle handle;

	      if (ev->type == GDK_2BUTTON_PRESS)
		{
		  line_finish (data, devdata);
		  return TRUE;
		}

	      if (devdata->line_active)
		{
		  handle = line_handle_at (&devdata->line, ev->x, ev->y);

		  if (handle != SCREENATOR_LINE_HANDLE_NONE)
		    {
		      line_begin_drag (devdata, handle, ev->x, ev->y);

		      if (handle == SCREENATOR_LINE_HANDLE_HEADS)
			{
			  devdata->line.heads =
			    SCREENATOR_ARROW_HEADS_NONE;
			  line_invalidate (data, &devdata->line);
			}
		      return TRUE;
		    }

		  if (line_contains (&devdata->line, ev->x, ev->y,
				     (gint) data->pen_width))
		    {
		      line_begin_drag (devdata,
				       SCREENATOR_LINE_HANDLE_MOVE,
				       ev->x, ev->y);
		      return TRUE;
		    }

		  /* A press outside commits it; the code below starts a new
		   * one at the click position. */
		  line_finish (data, devdata);
		}

	      line_begin (devdata, data->shape_type, ev->x, ev->y);
	      return TRUE;
	    }

	  /* The ellipse, rounded rectangle, math plot and parallel lines are
	   * dragged out once and then stay editable with their handles until
	   * the user presses outside them. */
	  if (data->shape_type == SCREENATOR_SHAPE_ELLIPSE ||
	      data->shape_type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE ||
	      data->shape_type == SCREENATOR_SHAPE_MATH ||
	      data->shape_type == SCREENATOR_SHAPE_BARS ||
	      data->shape_type == SCREENATOR_SHAPE_RANDOM)
	    {
	      ScreenatorFrameHandle handle;

	      if (ev->type == GDK_2BUTTON_PRESS)
		{
		  frame_finish (data, devdata);
		  return TRUE;
		}

	      if (devdata->frame_active)
		{
		  handle = frame_handle_at (&devdata->frame_shape,
					    ev->x, ev->y);

		  if (handle != SCREENATOR_FRAME_HANDLE_NONE)
		    {
		      frame_begin_drag (data, devdata, handle, ev->x, ev->y);

		      /* The line-count handle starts at two lines and the
		       * arrow handle at no arrows; moving the pointer away
		       * adds more. */
		      if (handle == SCREENATOR_FRAME_HANDLE_EXTRA &&
			  devdata->frame_shape.type == SCREENATOR_SHAPE_BARS)
			{
			  devdata->frame_shape.count = 2;
			  frame_invalidate (data, &devdata->frame_shape);
			}
		      else if (handle == SCREENATOR_FRAME_HANDLE_ARROWS &&
			       devdata->frame_shape.type == SCREENATOR_SHAPE_BARS)
			{
			  devdata->frame_shape.arrows =
			    SCREENATOR_BARS_ARROWS_NONE;
			  frame_invalidate (data, &devdata->frame_shape);
			}
		      else if (handle == SCREENATOR_FRAME_HANDLE_EXTRA &&
			       devdata->frame_shape.type == SCREENATOR_SHAPE_RANDOM)
			{
			  devdata->frame_shape.extra = 1.0;
			  frame_invalidate (data, &devdata->frame_shape);
			}
		      return TRUE;
		    }

		  /* A press inside the frame grabs it and moves it. */
		  if (frame_contains (&devdata->frame_shape, ev->x, ev->y))
		    {
		      frame_begin_drag (data, devdata,
					SCREENATOR_FRAME_HANDLE_MOVE,
					ev->x, ev->y);
		      return TRUE;
		    }

		  /* A press outside commits it; the code below starts a new
		   * shape at the click position. */
		  frame_finish (data, devdata);
		}

	      devdata->shape_active = TRUE;
	      devdata->shape_x0 = devdata->shape_x1 = ev->x;
	      devdata->shape_y0 = devdata->shape_y1 = ev->y;
	      if (data->shape_type == SCREENATOR_SHAPE_RANDOM)
		devdata->frame_seed = (guint) g_random_int () | 1u;
	      return TRUE;
	    }

	  devdata->shape_active = TRUE;
	  devdata->shape_x0 = devdata->shape_x1 = ev->x;
	  devdata->shape_y0 = devdata->shape_y1 = ev->y;
	}
      return TRUE;
    }

  devdata->lastx = ev->x;
  devdata->lasty = ev->y;
  devdata->motion_time = ev->time;
  devdata->smooth_active = FALSE;

  snap_undo_state (data);

  gdk_event_get_axis ((GdkEvent *) ev, GDK_AXIS_PRESSURE, &pressure);

  /* The stylus draws with the selected line width, exactly like the mouse.
   * Pressure is ignored so 1, 2 and 3 px stay visibly different; the side
   * button eraser also uses the full eraser width while hovering. */
  if (devdata->stylus_eraser_active ||
      event_source_is_stylus ((GdkEvent *) ev))
    pressure = 1.0;

  data->maxwidth = (CLAMP (pressure, 0, 1) *
		    (double) (devdata->cur_context->width -
			      devdata->cur_context->minwidth) +
		    devdata->cur_context->minwidth);

  if (ev->button <= 5)
    {
      stroke_begin (data, ev->device);
      draw_line (data, ev->device, ev->x, ev->y, ev->x, ev->y);
    }

  coord_list_prepend (data, ev->device, ev->x, ev->y, data->maxwidth);

  return TRUE;
}


gboolean on_motion (GtkWidget *win,
		    GdkEventMotion *ev,
		    gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  GdkTimeCoord **coords = NULL;
  gint nevents;
  int i;
  gdouble pressure = 1;
  /* get the data for this device */
  ScreenatorDeviceData *devdata = g_hash_table_lookup(data->devdatatable, ev->device);
  gboolean stylus = event_source_is_stylus ((GdkEvent *) ev);

  if (!devdata->is_grabbed)
    return FALSE;

  /* The second side button is the Escape/menu button; moving the pen while it
   * is held must not draw a line. */
  if (devdata->menu_button_down ||
      (ev->state & GDK_BUTTON3_MASK) != 0)
    {
      devdata->lastx = ev->x;
      devdata->lasty = ev->y;
      devdata->motion_time = ev->time;
      return TRUE;
    }

  /* While the first side button is waiting for its long press, keep the
   * pointer position fresh but do not draw. */
  if (devdata->stylus_side_button_down && !devdata->stylus_eraser_active)
    {
      devdata->lastx = ev->x;
      devdata->lasty = ev->y;
      devdata->motion_time = ev->time;
      return TRUE;
    }

  if(data->debug)
      g_printerr("DEBUG: Device '%s': motion to (x,y)=(%.2f : %.2f)\n", gdk_device_get_name(ev->device), ev->x, ev->y);

  /* Keep the eraser selected for as long as the first side button is held,
   * and keep the middle-button eraser for as long as that button is held. */
  if (!devdata->stylus_eraser_active && !devdata->saved_context &&
      (ev->state != devdata->state ||
       devdata->lastslave != gdk_event_get_source_device ((GdkEvent *) ev)))
    select_tool (data, ev->device, gdk_event_get_source_device ((GdkEvent *) ev), ev->state);

  /* Moving the pointer with the Text tool selected does not draw, but the
   * handles of a text that is still editable follow the pointer. */
  if (devdata->cur_context &&
      devdata->cur_context->type == SCREENATOR_TEXT)
    {
      if (devdata->frame_active &&
	  devdata->frame_drag != SCREENATOR_FRAME_HANDLE_NONE)
	frame_move_handle (data, devdata, ev->x, ev->y,
			   (ev->state & GDK_CONTROL_MASK) != 0);

      return TRUE;
    }

  /* Update the shape preview while dragging and repaint both the old and the
   * new preview area so the previous outline disappears. */
  if (devdata->cur_context &&
      devdata->cur_context->type == SCREENATOR_SHAPE)
    {
      if (data->shape_type == SCREENATOR_SHAPE_BUCKET)
	return TRUE;

      if (data->shape_type == SCREENATOR_SHAPE_STAR)
	{
	  if (devdata->star_active &&
	      devdata->star_drag != SCREENATOR_STAR_HANDLE_NONE)
	    star_move_handle (data, devdata, ev->x, ev->y,
			      (ev->state & GDK_CONTROL_MASK) != 0);
	  return TRUE;
	}

      if (data->shape_type == SCREENATOR_SHAPE_TRIANGLE)
	{
	  /* After the first click, the first edge follows the pointer; after
	   * the second click, the second edge follows it until the third
	   * click fixes the last vertex. */
	  if (devdata->triangle_active &&
	      devdata->triangle_vertices == 1)
	    {
	      if (devdata->triangle.v[1].x != ev->x ||
		  devdata->triangle.v[1].y != ev->y ||
		  devdata->triangle.v[2].x != ev->x ||
		  devdata->triangle.v[2].y != ev->y)
		{
		  triangle_invalidate (data, &devdata->triangle);
		  devdata->triangle.v[1].x = ev->x;
		  devdata->triangle.v[1].y = ev->y;
		  devdata->triangle.v[2].x = ev->x;
		  devdata->triangle.v[2].y = ev->y;
		  triangle_invalidate (data, &devdata->triangle);
		}
	      return TRUE;
	    }

	  if (devdata->triangle_active &&
	      devdata->triangle_vertices == 2)
	    {
	      if (devdata->triangle.v[2].x != ev->x ||
		  devdata->triangle.v[2].y != ev->y)
		{
		  triangle_invalidate (data, &devdata->triangle);
		  devdata->triangle.v[2].x = ev->x;
		  devdata->triangle.v[2].y = ev->y;
		  triangle_invalidate (data, &devdata->triangle);
		}
	      return TRUE;
	    }

	  if (devdata->triangle_active &&
	      devdata->triangle_drag != SCREENATOR_TRIANGLE_HANDLE_NONE)
	    triangle_move_handle (data, devdata, ev->x, ev->y,
				  (ev->state & GDK_CONTROL_MASK) != 0);
	  return TRUE;
	}

      if (data->shape_type == SCREENATOR_SHAPE_LINE ||
	  data->shape_type == SCREENATOR_SHAPE_ARROW)
	{
	  line_move_handle (data, devdata, ev->x, ev->y,
			    (ev->state & GDK_CONTROL_MASK) != 0);
	  return TRUE;
	}

      if (data->shape_type == SCREENATOR_SHAPE_ELLIPSE ||
	  data->shape_type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE ||
	  data->shape_type == SCREENATOR_SHAPE_MATH ||
	  data->shape_type == SCREENATOR_SHAPE_BARS ||
	  data->shape_type == SCREENATOR_SHAPE_RANDOM)
	{
	  if (devdata->frame_active &&
	      devdata->frame_drag != SCREENATOR_FRAME_HANDLE_NONE)
	    {
	      frame_move_handle (data, devdata, ev->x, ev->y,
				 (ev->state & GDK_CONTROL_MASK) != 0);
	      return TRUE;
	    }
	  /* Without a grabbed handle the generic preview below tracks the
	   * drag that creates the shape. */
	}

      if (data->shape_type == SCREENATOR_SHAPE_SPLINE)
	{
	  if (devdata->shape_active)
	    {
	      GdkRectangle old_bounds, new_bounds, dirty;

	      if (devdata->spline_handle_drag)
		{
		  spline_move_handle (data, devdata,
				      devdata->spline_drag_index,
				      devdata->spline_drag_out,
				      ev->x, ev->y,
				      (ev->state & GDK_CONTROL_MASK) != 0);
		  return TRUE;
		}

	      if (devdata->spline_node_drag)
		{
		  if (devdata->spline_finish_pending)
		    {
		      gint dx = ev->x - devdata->spline_press_x;
		      gint dy = ev->y - devdata->spline_press_y;

		      /* A tiny movement is still a click on the last node,
		       * which finishes the spline on release. */
		      if (dx * dx + dy * dy < 16)
			return TRUE;

		      devdata->spline_finish_pending = FALSE;
		    }

		  spline_move_node (data, devdata,
				    devdata->spline_drag_index,
				    ev->x, ev->y);
		  return TRUE;
		}

	      spline_preview_bounds (data, devdata, &old_bounds);
	      devdata->shape_x1 = ev->x;
	      devdata->shape_y1 = ev->y;
	      spline_preview_bounds (data, devdata, &new_bounds);

	      gdk_rectangle_union (&old_bounds, &new_bounds, &dirty);
	      spline_invalidate (data, &dirty);
	    }
	  return TRUE;
	}

      if (devdata->shape_active)
	{
	  GdkRectangle old_bounds, new_bounds, dirty;

	  shape_get_bounds (data, data->shape_type,
			    devdata->shape_x0, devdata->shape_y0,
			    devdata->shape_x1, devdata->shape_y1,
			    &old_bounds);

	  devdata->shape_x1 = ev->x;
	  devdata->shape_y1 = ev->y;

	  shape_get_bounds (data, data->shape_type,
			    devdata->shape_x0, devdata->shape_y0,
			    devdata->shape_x1, devdata->shape_y1,
			    &new_bounds);

	  gdk_rectangle_union (&old_bounds, &new_bounds, &dirty);
	  gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
				      &dirty, 0);
	}
      return TRUE;
    }

  /* Pointer motion is now also delivered while no button is held, so the
   * triangle and other shape previews can follow the pointer after a click.
   * Outside those previews, hovering must never paint. */
  if ((ev->state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK |
		    GDK_BUTTON3_MASK | GDK_BUTTON4_MASK |
		    GDK_BUTTON5_MASK)) == 0 &&
      !devdata->stylus_eraser_active)
    {
      devdata->lastx = ev->x;
      devdata->lasty = ev->y;
      devdata->motion_time = ev->time;
      return TRUE;
    }

  gdk_device_get_history (ev->device, ev->window,
			  devdata->motion_time, ev->time,
			  &coords, &nevents);

  if(!data->xinerama && nevents > 0)
    {
      for (i=0; i < nevents; i++)
        {
          gdouble x, y;

          gdk_device_get_axis (ev->device, coords[i]->axes,
                               GDK_AXIS_PRESSURE, &pressure);
          if (pressure > 0 || devdata->stylus_eraser_active)
            {
	      if (stylus || devdata->stylus_eraser_active)
		data->maxwidth = devdata->cur_context->width;
	      else
		data->maxwidth = (CLAMP (pressure, 0, 1) *
				  (double) (devdata->cur_context->width -
					    devdata->cur_context->minwidth) +
				  devdata->cur_context->minwidth);

              gdk_device_get_axis(ev->device, coords[i]->axes,
                                  GDK_AXIS_X, &x);
              gdk_device_get_axis(ev->device, coords[i]->axes,
                                  GDK_AXIS_Y, &y);

	      draw_line (data, ev->device, devdata->lastx, devdata->lasty, x, y);

              coord_list_prepend (data, ev->device, x, y, data->maxwidth);
              devdata->lastx = x;
              devdata->lasty = y;
            }
        }

      devdata->motion_time = coords[nevents-1]->time;
      g_free (coords);
    }

  /* always paint to the current event coordinate. */
  gdk_event_get_axis ((GdkEvent *) ev, GDK_AXIS_PRESSURE, &pressure);

  if (pressure > 0 || devdata->stylus_eraser_active)
    {
      if (stylus || devdata->stylus_eraser_active)
	data->maxwidth = devdata->cur_context->width;
      else
	data->maxwidth = (CLAMP (pressure, 0, 1) *
			  (double) (devdata->cur_context->width -
				    devdata->cur_context->minwidth) +
			  devdata->cur_context->minwidth);

      if(devdata->motion_time > 0)
	{
	  draw_line (data, ev->device, devdata->lastx, devdata->lasty, ev->x, ev->y);
	  coord_list_prepend (data, ev->device, ev->x, ev->y, data->maxwidth);
	}
    }

  devdata->lastx = ev->x;
  devdata->lasty = ev->y;
  devdata->motion_time = ev->time;

  return TRUE;
}


gboolean on_buttonrelease (GtkWidget *win, 
			   GdkEventButton *ev, 
			   gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  /* get the device data for this event */
  ScreenatorDeviceData *devdata = g_hash_table_lookup(data->devdatatable, ev->device);

  gfloat direction = 0;
  gint width = 0;
  if(devdata->cur_context)
    width = devdata->cur_context->arrowsize * devdata->cur_context->width / 2;
   

  /* The text editor disables the mouse; ignore every button until the typed
   * text is accepted with Enter. */
  if (data->text_editor)
    {
      data->text_pressed = FALSE;
      return TRUE;
    }

  /* Right button: a short click performs Escape; a long press has opened the
   * mode menu and consumes its own release. */
  if (ev->button == 3)
    {
      devdata->menu_button_down = FALSE;

      if (data->menu_hold_id)
	{
	  g_source_remove (data->menu_hold_id);
	  data->menu_hold_id = 0;
	  data->menu_hold_devdata = NULL;
	  screenator_exit_tool (data);
	}
      return TRUE;
    }

  if (event_source_is_stylus ((GdkEvent *) ev) && ev->button == 2 &&
      data->stylus_eraser_hold_id)
    {
      /* Short press: step through the selected colours or thicknesses. */
      g_source_remove (data->stylus_eraser_hold_id);
      data->stylus_eraser_hold_id = 0;
      data->stylus_eraser_hold_devdata = NULL;
      devdata->stylus_side_button_down = FALSE;
      apply_stylus_step (data, TRUE);
      return TRUE;
    }

  if ((ev->x != devdata->lastx) ||
      (ev->y != devdata->lasty))
    on_motion(win, (GdkEventMotion *) ev, user_data);

  if (!devdata->is_grabbed)
    return FALSE;

  if (ev->button == 2)
    devdata->stylus_side_button_down = FALSE;

  if (ev->button == 2 && devdata->stylus_eraser_active)
    devdata->stylus_eraser_active = FALSE;

  /* Open the inline text editor at the position where the button was
   * pressed, not where it happened to be released.  Releasing a text handle
   * only ends the drag. */
  if (devdata->cur_context &&
      devdata->cur_context->type == SCREENATOR_TEXT)
    {
      if (devdata->frame_drag != SCREENATOR_FRAME_HANDLE_NONE)
	{
	  devdata->frame_drag = SCREENATOR_FRAME_HANDLE_NONE;
	  data->text_pressed = FALSE;
	  return TRUE;
	}

      if (ev->button == 1)
	{
	  gint x = data->text_pressed ? data->text_x : ev->x;
	  gint y = data->text_pressed ? data->text_y : ev->y;

	  data->text_pressed = FALSE;
	  show_text_editor (data, ev->device, x, y);
	}
      return TRUE;
    }

  /* Restore previous tool after middle mouse button release */
  if (ev->button == 2 && devdata->saved_context)
    {
      devdata->cur_context = devdata->saved_context;
      devdata->saved_context = NULL;

      /* Return to the cursor of the tool that was active before. */
      set_device_cursor (data, ev->device, devdata->cur_context);
    }

  /* Commit the dragged shape to the backbuffer.  A click without any drag
   * does not create an annotation.  The spline is finished by a double-click
   * or a right-click instead, so releasing its button does nothing. */
  if (devdata->cur_context &&
      devdata->cur_context->type == SCREENATOR_SHAPE)
    {
      if (data->shape_type == SCREENATOR_SHAPE_BUCKET)
	return TRUE;

      if (data->shape_type == SCREENATOR_SHAPE_STAR)
	{
	  /* Releasing a handle ends the drag; the star stays editable. */
	  if (devdata->star_drag != SCREENATOR_STAR_HANDLE_NONE)
	    {
	      star_finish_placement (data, devdata);
	      devdata->star_drag = SCREENATOR_STAR_HANDLE_NONE;
	      return TRUE;
	    }
	  return TRUE;
	}

      if (data->shape_type == SCREENATOR_SHAPE_SPLINE)
	{
	  if (devdata->spline_handle_drag)
	    {
	      devdata->spline_handle_drag = FALSE;
	      return TRUE;
	    }

	  if (devdata->spline_node_drag)
	    {
	      gboolean finish = devdata->spline_finish_pending;

	      devdata->spline_node_drag = FALSE;
	      devdata->spline_finish_pending = FALSE;

	      /* A click on the last node, without dragging it, finishes. */
	      if (finish)
		spline_finish (data, devdata);
	    }
	  return TRUE;
	}

      if (data->shape_type == SCREENATOR_SHAPE_TRIANGLE)
	{
	  /* Releasing a placement drag does not place a point; the line keeps
	   * following the pointer until the next click.  Releasing a vertex
	   * handle ends the drag; the triangle stays editable. */
	  devdata->triangle_drag = SCREENATOR_TRIANGLE_HANDLE_NONE;
	  return TRUE;
	}

      if (data->shape_type == SCREENATOR_SHAPE_LINE ||
	  data->shape_type == SCREENATOR_SHAPE_ARROW)
	{
	  /* Releasing a handle ends the drag; the line stays editable until
	   * it is committed. */
	  devdata->line_drag = SCREENATOR_LINE_HANDLE_NONE;
	  return TRUE;
	}

      if (data->shape_type == SCREENATOR_SHAPE_ELLIPSE ||
	  data->shape_type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE ||
	  data->shape_type == SCREENATOR_SHAPE_MATH ||
	  data->shape_type == SCREENATOR_SHAPE_BARS ||
	  data->shape_type == SCREENATOR_SHAPE_RANDOM)
	{
	  /* A click inside a random signal (no drag) rolls a new one. */
	  if (data->shape_type == SCREENATOR_SHAPE_RANDOM &&
	      devdata->frame_shape.type == SCREENATOR_SHAPE_RANDOM &&
	      devdata->frame_drag == SCREENATOR_FRAME_HANDLE_MOVE)
	    {
	      gint dx = ABS (ev->x - devdata->frame_press_x);
	      gint dy = ABS (ev->y - devdata->frame_press_y);

	      devdata->frame_drag = SCREENATOR_FRAME_HANDLE_NONE;

	      if (dx < 3 && dy < 3)
		{
		  devdata->frame_shape.frame.cx = devdata->frame_press_cx;
		  devdata->frame_shape.frame.cy = devdata->frame_press_cy;
		  devdata->frame_shape.seed = (guint) g_random_int () | 1u;
		  frame_invalidate (data, &devdata->frame_shape);
		}

	      return TRUE;
	    }

	  /* Releasing a handle ends the drag; the shape stays editable until
	   * it is committed. */
	  if (devdata->frame_drag != SCREENATOR_FRAME_HANDLE_NONE)
	    {
	      devdata->frame_drag = SCREENATOR_FRAME_HANDLE_NONE;
	      return TRUE;
	    }

	  if (devdata->shape_active)
	    {
	      gint dx = ABS (devdata->shape_x1 - devdata->shape_x0);
	      gint dy = ABS (devdata->shape_y1 - devdata->shape_y0);

	      devdata->shape_active = FALSE;

	      /* The dragged-out rectangle becomes the editable frame, so
	       * the handles can still move, resize and rotate it.  A plain
	       * click with the Random Signal tool creates a default-size
	       * signal centred on the click. */
	      if (dx > 0 || dy > 0)
		frame_begin_from_drag (data, devdata, data->shape_type,
				       devdata->shape_x0, devdata->shape_y0,
				       devdata->shape_x1, devdata->shape_y1);
	      else if (data->shape_type == SCREENATOR_SHAPE_RANDOM)
		frame_begin_from_drag (data, devdata, data->shape_type,
				       devdata->shape_x0 -
				       RANDOM_SIGNAL_DEFAULT_W / 2,
				       devdata->shape_y0 -
				       RANDOM_SIGNAL_DEFAULT_H / 2,
				       devdata->shape_x0 +
				       RANDOM_SIGNAL_DEFAULT_W / 2,
				       devdata->shape_y0 +
				       RANDOM_SIGNAL_DEFAULT_H / 2);

	      return TRUE;
	    }
	  return TRUE;
	}

      if (devdata->shape_active)
	{
	  GdkRectangle bounds;
	  gint dx = ABS (devdata->shape_x1 - devdata->shape_x0);
	  gint dy = ABS (devdata->shape_y1 - devdata->shape_y0);

	  devdata->shape_active = FALSE;

	  if (dx > 0 || dy > 0)
	    {
	      cairo_t *cr;

	      snap_undo_state (data);
	      cr = cairo_create (data->backbuffer);
	      draw_shape (data, cr, data->shape_type,
			  devdata->shape_x0, devdata->shape_y0,
			  devdata->shape_x1, devdata->shape_y1);
	      cairo_destroy (cr);

	      data->modified = 1;
	      data->painted = 1;
	    }

	  shape_get_bounds (data, data->shape_type,
			    devdata->shape_x0, devdata->shape_y0,
			    devdata->shape_x1, devdata->shape_y1,
			    &bounds);
	  if (bounds.width > 0 && bounds.height > 0)
	    gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
					&bounds, 0);
	}
      return TRUE;
    }

  /* Finish the smoothed pen curve before the (possibly translucent) stroke is
   * composited onto the backbuffer. */
  draw_line_finish (data, ev->device);

  /* Commit a translucent stroke before the arrow head is drawn on top. */
  stroke_finish (data, ev->device);
  
  if (devdata->cur_context->arrowsize != 0 &&
      coord_list_get_arrow_param (data, ev->device, width * 3,
				  &width, &direction))
    draw_arrow (data, ev->device, ev->x, ev->y, width, direction);

  coord_list_free (data, ev->device);

  return TRUE;
}

/* Remote control */
void on_mainapp_selection_get (GtkWidget          *widget,
			       GtkSelectionData   *selection_data,
			       guint               info,
			       guint               time,
			       gpointer            user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  
  gchar *uri = "OK";
  GdkAtom action = gtk_selection_data_get_target(selection_data);

  if(action == GA_TOGGLE)
    {
      /* ask back client for device id */
      gtk_selection_convert (data->win, GA_DATA,
                             GA_TOGGLEDATA, time);
      gtk_main(); /* Wait for the response */
    }
  else if (action == GA_TOGGLEICON)
    set_floating_icon_visible (data, !data->show_floating_icon);
  else if (action == GA_VISIBILITY)
    toggle_visibility (data);
  else if (action == GA_CLEAR)
    clear_screen (data);
  else if (action == GA_RELOAD)
    setup_input_devices(data);
  else if (action == GA_QUIT)
    gtk_main_quit ();
  else if (action == GA_UNDO)
    undo_drawing (data);
  else if (action == GA_REDO)
    redo_drawing (data);
  else
    uri = "NOK";

   
  gtk_selection_data_set (selection_data,
                          gtk_selection_data_get_target(selection_data),
                          8, (guchar*)uri, strlen (uri));
}


void on_mainapp_selection_received (GtkWidget *widget,
				    GtkSelectionData *selection_data,
				    guint time,
				    gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if(gtk_selection_data_get_length(selection_data) < 0)
    {
      if(data->debug)
        g_printerr("DEBUG: mainapp got no answer back from client.\n");
    }
  else
    {
      if(gtk_selection_data_get_target(selection_data) == GA_TOGGLEDATA )
        {
	  intptr_t dev_nr = strtoull((gchar*)gtk_selection_data_get_data(selection_data), NULL, 10);
	  
          if(data->debug)
	    g_printerr("DEBUG: mainapp got toggle id '%ld' back from client.\n", (long)dev_nr);

	  if(dev_nr < 0)
	    toggle_grab(data, NULL); /* toggle all */
	  else 
	    {
	      /* find dev numbered dev_nr */
	      GHashTableIter it;
	      gpointer value;
	      ScreenatorDeviceData* devdata = NULL; 
	      g_hash_table_iter_init (&it, data->devdatatable);
	      while (g_hash_table_iter_next (&it, NULL, &value)) 
		{
		  devdata = value;
		  if(devdata->index == dev_nr)
		    break;
		  else
		    devdata = NULL;
		}
	      
	      if(devdata)
		toggle_grab(data, devdata->device);
	      else
		g_printerr("ERROR: No device at index %ld.\n", (long)dev_nr);
	    }
        }
    }
 
  gtk_main_quit ();
}


void on_device_removed (GdkDeviceManager *device_manager,
			GdkDevice        *device,
			gpointer          user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
    
  if(!gdk_device_get_device_type(device) == GDK_DEVICE_TYPE_MASTER
     || gdk_device_get_n_axes(device) < 2)
    return;
  
  if(data->debug)
    g_printerr("DEBUG: device '%s' removed\n", gdk_device_get_name(device));

  setup_input_devices(data);
}

void on_device_added (GdkDeviceManager *device_manager,
		      GdkDevice        *device,
		      gpointer          user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if(!gdk_device_get_device_type(device) == GDK_DEVICE_TYPE_MASTER
     || gdk_device_get_n_axes(device) < 2)
    return;

  if(data->debug)
    g_printerr("DEBUG: device '%s' added\n", gdk_device_get_name(device));

  setup_input_devices(data);
}



gboolean on_toggle_paint(GtkWidget *widget,
			 GdkEventButton  *ev,
			 gpointer   user_data)
{
    ScreenatorData *data = (ScreenatorData *) user_data;

    if(data->debug)
	g_printerr("DEBUG: Device '%s': Button %i on_toggle_paint at (x,y)=(%.2f : %.2f)\n",
		   gdk_device_get_name(ev->device), ev->button, ev->x, ev->y);

    toggle_grab(data, ev->device);

    return TRUE;
}

void on_clear (GtkMenuItem *menuitem,
	       gpointer     user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  clear_screen(data);
}


void on_toggle_vis(GtkMenuItem *menuitem,
		   gpointer     user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  toggle_drawings_visibility(data);
}

static void
on_show_hints (GtkMenuItem *menuitem, gpointer user_data)
{
  show_hints_dialog ((ScreenatorData *) user_data, FALSE);
}


void on_opacity_bigger(GtkMenuItem *menuitem,
		       gpointer     user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  set_pen_opacity (data, data->opacity + 0.1);
  schedule_tray_menu_refresh (data);
}

void on_opacity_lesser(GtkMenuItem *menuitem,
		       gpointer     user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  set_pen_opacity (data, data->opacity - 0.1);
  schedule_tray_menu_refresh (data);
}


void on_undo(GtkMenuItem *menuitem,
	     gpointer     user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  undo_drawing (data);
}

void on_redo(GtkMenuItem *menuitem,
	     gpointer     user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  redo_drawing (data);
}


/* About is built here rather than with GtkAboutDialog: the old dialog brought
 * a lot of Gromit-MPX/GtkAboutDialog machinery (Credits, links and a
 * screen-wide link CSS provider) that this simple information window does not
 * need.  Like the other dialogs it is a borderless popup above the drawings,
 * but it deliberately does not take the input focus when it maps: taking focus
 * from the panel's closing tray menu left the whole panel unresponsive for a
 * while.  Focus is given only when the pointer enters it or the user clicks
 * it, i.e. after the tray menu has closed.  For the same reason the focus is
 * never stored or restored on close. */

static GtkWidget *
build_about_dialog (void)
{
  GtkWidget *dialog;
  GtkWidget *content;
  GtkWidget *box;
  GtkWidget *image;
  GtkWidget *label;
  gchar *markup;

  dialog = gtk_dialog_new_with_buttons ("About Screenator", NULL, 0,
					"_Close", GTK_RESPONSE_CLOSE,
					NULL);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);

  content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width (GTK_CONTAINER (box), 24);
  gtk_widget_set_size_request (box, 420, -1);
  gtk_container_add (GTK_CONTAINER (content), box);

  image = gtk_image_new_from_icon_name ("screenator", GTK_ICON_SIZE_DIALOG);
  gtk_image_set_pixel_size (GTK_IMAGE (image), 96);
  gtk_widget_set_halign (image, GTK_ALIGN_CENTER);
  gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 0);

  markup = g_strdup_printf ("<span size=\"x-large\" weight=\"bold\">"
			    "Screenator %s</span>", VERSION);
  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label), markup);
  g_free (markup);
  gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  label = gtk_label_new ("A screen annotation tool for X11, developed by "
			 "Mahmoud Alnaanah (malnaanah@gmail.com).");
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 52);
  gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  label = gtk_label_new ("Based on Gromit-MPX 1.4.2\n"
			 "Copyright 2009-2022 Christian Beier\n"
			 "Copyright 2000 Simon Budig");
  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
  gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  label = gtk_label_new ("Copyright 2026 Mahmoud Alnaanah\n\n"
			 "This program comes with absolutely no warranty.\n"
			 "See the GNU General Public License, version 2 or "
			 "later for details.");
  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
  gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  return dialog;
}

static void
about_take_focus (GtkWidget *dialog)
{
#ifdef GDK_WINDOWING_X11
  GdkWindow *window = gtk_widget_get_window (dialog);
  GdkDisplay *display;
  Display *xdisplay;

  if (!window || !GDK_IS_X11_WINDOW (window))
    return;

  display = gdk_window_get_display (window);
  xdisplay = GDK_DISPLAY_XDISPLAY (display);

  gdk_x11_display_error_trap_push (display);
  XSetInputFocus (xdisplay, GDK_WINDOW_XID (window), RevertToParent,
		  CurrentTime);
  XFlush (xdisplay);
  gdk_x11_display_error_trap_pop_ignored (display);
#endif
}

static gboolean
on_about_pointer (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if (data->about_dialog)
    about_take_focus (data->about_dialog);

  return FALSE;
}

static gboolean
on_about_map (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
  GdkWindow *window = gtk_widget_get_window (widget);
  GtkRequisition req;

  if (window)
    {
      /* No window manager places an override-redirect window: centre it. */
      gtk_widget_get_preferred_size (widget, NULL, &req);
      gtk_window_move (GTK_WINDOW (widget),
		       MAX (0, (gdk_screen_width () - req.width) / 2),
		       MAX (0, (gdk_screen_height () - req.height) / 2));

      /* The annotation window and the floating icon are above the other
       * windows, so raise the dialog above them. */
      gdk_window_raise (window);
    }

  /* Deliberately no gtk_window_present() and no input focus here. */
  return FALSE;
}

void on_about(GtkMenuItem *menuitem,
	      gpointer     user_data)
{
    ScreenatorData *data = (ScreenatorData *) user_data;
    GtkWidget *dialog;
    gboolean was_grabbed;

    if (data->about_dialog)
      {
	GdkWindow *window = gtk_widget_get_window (data->about_dialog);

	if (window)
	  gdk_window_raise (window);
	return;
      }

    dialog = build_about_dialog ();

    /* A borderless popup, like the other dialogs, but without their focus
     * stealing when it maps. */
    make_dialog_popup (dialog);
    gtk_widget_add_events (dialog, GDK_ENTER_NOTIFY_MASK |
				   GDK_BUTTON_PRESS_MASK);
    g_signal_connect (dialog, "map-event",
		      G_CALLBACK (on_about_map), data);
    g_signal_connect (dialog, "enter-notify-event",
		      G_CALLBACK (on_about_pointer), data);
    g_signal_connect (dialog, "button-press-event",
		      G_CALLBACK (on_about_pointer), data);

    /* About must be clickable while painting mode is active.  The drawings
     * stay visible; only the pointer grab is released for the dialog. */
    was_grabbed = screenator_any_grabbed (data);
    if (was_grabbed)
      release_grab (data, NULL);

    data->about_dialog = dialog;
    gtk_widget_show_all (dialog);
    gtk_dialog_run (GTK_DIALOG (dialog));
    data->about_dialog = NULL;
    gtk_widget_destroy (dialog);

    if (was_grabbed)
      acquire_grab (data, NULL);
}

/* ------------------------------------------------------------------
* Movable on-screen icon
 * ------------------------------------------------------------------ */

#define MENU_ICON_SIZE 16

/* The .deb installs the icons below /usr/share.  The AppImage is mounted at
 * a temporary path, so its AppRun exports SCREENATOR_DATADIR with the bundled
 * /usr/share directory.  linuxdeploy may replace that AppRun, but it always
 * leaves APPDIR pointing at the mount, so use it as a fallback. */
static const gchar *icon_data_dir (void)
{
  const gchar *dir = g_getenv ("SCREENATOR_DATADIR");

  if (dir && *dir && g_file_test (dir, G_FILE_TEST_IS_DIR))
    return dir;

  dir = g_getenv ("APPDIR");
  if (dir && *dir)
    {
      static gchar *appdir_share = NULL;

      if (!appdir_share)
	appdir_share = g_build_filename (dir, "usr", "share", NULL);
      if (g_file_test (appdir_share, G_FILE_TEST_IS_DIR))
	return appdir_share;
    }

  return NULL;
}

/* Path of a file installed below the data directory (usually /usr/share). */
static gchar *icon_path (const gchar *relative)
{
  const gchar *dir = icon_data_dir ();

  if (dir)
    return g_build_filename (dir, relative, NULL);

  return g_build_filename ("/usr/share", relative, NULL);
}

/* The application icon has two variants: the normal one and the active one,
 * which is shown in the floating icon while a painting mode is grabbed. */
static gchar *main_icon_svg_path (gboolean active)
{
  return icon_path (active
		    ? "icons/hicolor/scalable/apps/screenator-active.svg"
		    : "icons/hicolor/scalable/apps/screenator.svg");
}

static gchar *main_icon_png_path (void)
{
  return icon_path ("pixmaps/screenator.png");
}

/* The tray indicator resolves icons through the icon theme.  When the
 * application runs from its AppImage (or from an unpacked build) the icon is
 * not part of the system theme and the panel shows a blank icon.  Passing the
 * absolute file instead lets libayatana-appindicator hand the file to the
 * panel, so the tray icon also works without installing the package. */
void set_tray_icon (ScreenatorData *data)
{
  gchar *path;

  if (!data->trayicon)
    return;

  path = main_icon_png_path ();
  if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
    app_indicator_set_icon (data->trayicon, path);
  else
    app_indicator_set_icon (data->trayicon, "screenator");

  g_free (path);
}

static GdkPixbuf *load_menu_icon (const gchar *name)
{
  const gchar *dir = icon_data_dir ();
  gchar *path = dir ? g_build_filename (dir, "screenator", name, NULL)
                    : g_build_filename ("/usr/share/screenator", name, NULL);
  GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale (path, MENU_ICON_SIZE,
                                                         MENU_ICON_SIZE, FALSE,
                                                         NULL);
  g_free (path);

  return pixbuf;
}

static GdkPixbuf *load_icon_pixbuf (ScreenatorData *data, gboolean active)
{
  static const gchar * const normal_svg =
    "icons/hicolor/scalable/apps/screenator.svg";
  GdkPixbuf *pixbuf;
  gchar *path;

  /* Prefer the SVG so the icon stays sharp at every floating icon size.
   * Fall back to the high-resolution PNG if no SVG loader is installed. */
  path = main_icon_svg_path (active);
  pixbuf = gdk_pixbuf_new_from_file_at_scale (path, data->icon_size,
					      data->icon_size, TRUE, NULL);
  g_free (path);

  /* An older installation may not have the active variant yet. */
  if (!pixbuf && active)
    {
      path = icon_path (normal_svg);
      pixbuf = gdk_pixbuf_new_from_file_at_scale (path, data->icon_size,
						  data->icon_size, TRUE, NULL);
      g_free (path);
    }

  if (!pixbuf)
    {
      path = main_icon_png_path ();
      pixbuf = gdk_pixbuf_new_from_file_at_scale (path, data->icon_size,
						  data->icon_size, TRUE, NULL);
      g_free (path);
    }

  return pixbuf;
}

/* Without a compositor the icon window has no alpha channel, so shape it to
 * the icon's circle.  That lets the settings ring show through the square
 * corners and keeps clicks outside the circle away from the icon. */
static void update_icon_shape (ScreenatorData *data);

static gboolean
on_icon_configure (GtkWidget *widget, GdkEventConfigure *event, gpointer user_data)
{
  ScreenatorData *data = user_data;

  if (event->width != (gint) data->icon_size ||
      event->height != (gint) data->icon_size)
    {
      gtk_widget_set_size_request (widget,
				   (gint) data->icon_size,
				   (gint) data->icon_size);
      return FALSE;
    }

  update_icon_shape (data);
  gtk_widget_queue_draw (widget);
  return FALSE;
}

static void
update_icon_shape (ScreenatorData *data)
{
  cairo_surface_t *surface;
  cairo_region_t *region;
  cairo_t *cr;
  gint size;
  gdouble radius;

  if (!data->icon_win || !data->icon_shaped)
    return;

  size = MAX (1, (gint) data->icon_size);
  radius = MAX (1.0, size / 2.0 - 1.0);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, size, size);
  cr = cairo_create (surface);
  cairo_arc (cr, size / 2.0, size / 2.0, radius, 0, 2.0 * G_PI);
  cairo_set_source_rgba (cr, 1, 1, 1, 1);
  cairo_fill (cr);
  cairo_destroy (cr);
  cairo_surface_flush (surface);

  region = gdk_cairo_region_create_from_surface (surface);
  gtk_widget_shape_combine_region (data->icon_win, region);
  gtk_widget_input_shape_combine_region (data->icon_win, region);
  cairo_region_destroy (region);
  cairo_surface_destroy (surface);
}

void update_icon_image (ScreenatorData *data)
{
  GdkPixbuf *pixbuf;

  if (!data->icon_image)
    return;

  pixbuf = load_icon_pixbuf (data,
			     data->force_active_icon ||
			     screenator_any_grabbed (data));
  gtk_image_set_from_pixbuf (GTK_IMAGE (data->icon_image),
			     pixbuf ? pixbuf : NULL);
  if (pixbuf)
    g_object_unref (pixbuf);

  if (data->icon_win)
    gtk_widget_set_size_request (data->icon_win,
				 (gint) data->icon_size,
				 (gint) data->icon_size);

  update_icon_shape (data);
}

/* Colours offered by the Line Color menu; the mouse wheel cycles through them
 * while a tool is active. */
static const struct { const gchar *label; const gchar *name; } line_colors[] =
{
  { "Red", "#FF0000" },
  { "Green", "#00AA00" },
  { "Blue", "#0000FF" },
  { "Yellow", "#FFFF00" },
  { "Black", "#000000" },
  { "White", "#FFFFFF" },
  { "Orange", "#FF8000" },
  { "Gray", "#808080" }
};

/* Colours offered by the Shapes Fill Color menu; the mouse wheel cycles through
 * them while the Paint Bucket tool is active. */
static const struct { const gchar *label; const gchar *name; } fill_colors[] =
{
  { "None", "none" },
  { "Green", "#00AA00" },
  { "Blue", "#0000FF" },
  { "Yellow", "#FFFF00" },
  { "Black", "#000000" },
  { "White", "#FFFFFF" },
  { "Red", "#FF0000" },
  { "Orange", "#FF8000" },
  { "Gray", "#808080" }
};

/* Recolour the frames that are still being edited (a text that was just
 * placed with Enter, an ellipse, a math plot, ...), so a colour chosen from
 * the menu or the mouse wheel applies to them before they are committed. */
static void
update_active_frame_colors (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;

  if (!data->devdatatable)
    return;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      ScreenatorDeviceData *devdata = value;

      if (devdata->frame_active)
	{
	  devdata->frame_shape.color = data->pen_color;
	  devdata->frame_shape.color.alpha = 1.0;
	}
    }
}

/* Redraw the annotation window, so a shape that is still being edited picks
 * up a colour changed from the menu or the mouse wheel immediately. */
static void
invalidate_annotation_window (ScreenatorData *data)
{
  if (data->win && gtk_widget_get_window (data->win))
    gdk_window_invalidate_rect (gtk_widget_get_window (data->win), NULL, 0);
}

/* Eraser sizes offered by the Eraser Size menu; the mouse wheel cycles through
 * them while the eraser is active. */
static const guint eraser_widths[] = { 20, 40, 60, 120 };

/* Apply an eraser size exactly like the Eraser Size menu does. */
static void
apply_eraser_size (ScreenatorData *data, guint width)
{
  GHashTableIter it;
  gpointer value;

  data->eraser_width = width;

  apply_pen_settings (data);
  update_eraser_cursor (data);

  /* The cursor is attached to the device grab, so grabbing again is the only
   * way to replace it.  Do that right away, otherwise the new eraser cursor
   * would only appear after the next event. */
  if (data->devdatatable)
    {
      g_hash_table_iter_init (&it, data->devdatatable);
      while (g_hash_table_iter_next (&it, NULL, &value))
	{
	  ScreenatorDeviceData *devdata = value;

	  devdata->state = 0;

	  if (devdata->is_grabbed && devdata->cur_context &&
	      devdata->cur_context->type == SCREENATOR_ERASER)
	    set_device_cursor (data, devdata->device, devdata->cur_context);
	}
    }

  schedule_tray_menu_refresh (data);
}

/* Step through the eraser sizes, wrapping around at both ends. */
static void
cycle_eraser_size (ScreenatorData *data, gboolean down)
{
  guint count = G_N_ELEMENTS (eraser_widths);
  guint current = count;
  guint i;

  for (i = 0; i < count; i++)
    if (data->eraser_width == eraser_widths[i])
      {
	current = i;
	break;
      }

  if (current >= count)
    current = down ? 0 : count - 1;
  else if (down)
    current = (current + 1) % count;
  else
    current = (current + count - 1) % count;

  apply_eraser_size (data, eraser_widths[current]);
}

/* Apply a line colour exactly like the Line Color menu does. */
static void
apply_line_color (ScreenatorData *data, const gchar *name)
{
  GdkRGBA color;

  if (!name || !gdk_rgba_parse (&color, name))
    return;

  /* Remember the hue (opacity is a separate setting). */
  data->pen_color.red = color.red;
  data->pen_color.green = color.green;
  data->pen_color.blue = color.blue;
  data->pen_color.alpha = 1.0;

  apply_pen_settings (data);
  update_settings_indicator (data);
  update_active_frame_colors (data);
  invalidate_annotation_window (data);
  schedule_tray_menu_refresh (data);
}

/* Apply a shape fill colour exactly like the Shapes Fill Color menu does. */
static void
apply_shape_fill (ScreenatorData *data, const gchar *name)
{
  GdkRGBA color;

  if (!name)
    return;

  if (g_ascii_strcasecmp (name, "none") == 0)
    {
      data->shape_fill_enabled = FALSE;
    }
  else if (gdk_rgba_parse (&color, name))
    {
      color.alpha = 1.0;
      data->shape_fill_color = color;
      data->shape_fill_enabled = TRUE;
    }

  update_settings_indicator (data);
  invalidate_annotation_window (data);
  schedule_tray_menu_refresh (data);
}

static gchar *
rgba_to_hex (const GdkRGBA *color)
{
  return g_strdup_printf ("#%02X%02X%02X",
			  (guint) (CLAMP (color->red, 0.0, 1.0) * 255.0 + 0.5),
			  (guint) (CLAMP (color->green, 0.0, 1.0) * 255.0 + 0.5),
			  (guint) (CLAMP (color->blue, 0.0, 1.0) * 255.0 + 0.5));
}

typedef struct
{
  ScreenatorData *data;
  gboolean        fill;
} CustomColorRequest;

/* Show the colour chooser after the menu has closed, so the menu cannot stay
 * open behind the modal dialog. */
static gboolean
custom_color_idle (gpointer user_data)
{
  CustomColorRequest *request = user_data;
  ScreenatorData *data = request->data;
  gboolean fill = request->fill;
  GtkWidget *dialog;
  GdkRGBA color = fill ? data->custom_fill_color : data->custom_line_color;
  gint response;

  dialog = gtk_color_chooser_dialog_new (fill ? "Shapes Fill Color"
					 : "Line Color", NULL);
  gtk_color_chooser_set_use_alpha (GTK_COLOR_CHOOSER (dialog), FALSE);
  gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (dialog), &color);
  gtk_window_set_keep_above (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  show_dialog_above_drawings (data, dialog);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response == GTK_RESPONSE_OK)
    {
      gchar *hex;

      gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (dialog), &color);
      color.alpha = 1.0;
      hex = rgba_to_hex (&color);

      if (fill)
	{
	  data->custom_fill_color = color;
	  apply_shape_fill (data, hex);
	}
      else
	{
	  data->custom_line_color = color;
	  apply_line_color (data, hex);
	}

      g_free (hex);
      write_keyfile (data);
    }

  gtk_widget_destroy (dialog);
  g_free (request);
  schedule_tray_menu_refresh (data);

  return G_SOURCE_REMOVE;
}

static void
on_icon_custom_color (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  const gchar *target =
    g_object_get_data (G_OBJECT (menuitem), "icon-custom-target");
  CustomColorRequest *request = g_new0 (CustomColorRequest, 1);

  request->data = data;
  request->fill = g_strcmp0 (target, "fill") == 0;
  g_idle_add (custom_color_idle, request);
}

/* Index of the current colour in a table, or the table size when the current
 * colour is not one of the presets. */
static guint
color_table_index (const GdkRGBA *current, gboolean include_none)
{
  guint i, count = include_none ? G_N_ELEMENTS (fill_colors)
				: G_N_ELEMENTS (line_colors);

  for (i = include_none ? 1 : 0; i < count; i++)
    {
      const gchar *name = include_none ? fill_colors[i].name
				       : line_colors[i].name;
      GdkRGBA color;

      if (gdk_rgba_parse (&color, name) &&
	  current->red == color.red &&
	  current->green == color.green &&
	  current->blue == color.blue)
	return i;
    }

  return count;
}

/* Step through the line colours, wrapping around at both ends. */
static void
cycle_line_color (ScreenatorData *data, gboolean down)
{
  guint presets = G_N_ELEMENTS (line_colors);
  guint count = presets + 1;   /* the last entry is the custom colour */
  guint current = color_table_index (&data->pen_color, FALSE);

  if (current > presets)
    current = presets;

  if (down)
    current = (current + 1) % count;
  else
    current = (current + count - 1) % count;

  if (current == presets)
    {
      gchar *hex = rgba_to_hex (&data->custom_line_color);

      apply_line_color (data, hex);
      g_free (hex);
    }
  else
    apply_line_color (data, line_colors[current].name);
}

/* Step through the shape fill colours, including None. */
static void
cycle_shape_fill_color (ScreenatorData *data, gboolean down)
{
  guint presets = G_N_ELEMENTS (fill_colors);
  guint count = presets + 1;   /* the last entry is the custom colour */
  guint current;

  if (!data->shape_fill_enabled)
    current = 0;
  else
    current = color_table_index (&data->shape_fill_color, TRUE);

  if (current > presets)
    current = presets;

  if (down)
    current = (current + 1) % count;
  else
    current = (current + count - 1) % count;

  if (current == presets)
    {
      gchar *hex = rgba_to_hex (&data->custom_fill_color);

      apply_shape_fill (data, hex);
      g_free (hex);
    }
  else
    apply_shape_fill (data, fill_colors[current].name);
}

/* TRUE while the Eraser tool is the selected tool. */
static gboolean
eraser_tool_active (ScreenatorData *data)
{
  return (data->forced_context &&
	  data->forced_context->type == SCREENATOR_ERASER) ||
	 data->pen_tool == 3;
}

/* TRUE while the Marker tool is the selected tool. */
static gboolean
marker_tool_active (ScreenatorData *data)
{
  return data->pen_tool == 11 ||
	 (data->default_marker &&
	  data->forced_context == data->default_marker);
}

/* Apply one wheel notch to the setting that belongs to the active tool: the
 * eraser size for the eraser, the shape fill colour over the Paint Bucket and
 * the line colour for every other tool.  Shared by the mouse wheel and the
 * stylus Scroll mode. */
static void
apply_scroll_step (ScreenatorData *data, gboolean down, gboolean colors_only)
{
  gboolean bucket = data->forced_context &&
		    data->forced_context->type == SCREENATOR_SHAPE &&
		    data->shape_type == SCREENATOR_SHAPE_BUCKET;

  if (!colors_only && eraser_tool_active (data))
    cycle_eraser_size (data, down);
  else if (bucket)
    cycle_shape_fill_color (data, down);
  else
    cycle_line_color (data, down);
}

/* Step through the Line Thickness entries, wrapping at both ends. */
static void
cycle_line_thickness (ScreenatorData *data, gboolean down)
{
  static const guint widths[] = { 1, 2, 3, 4, 5, 6, 12, 24, 48 };
  guint current = 0;
  guint best = G_MAXUINT;
  guint i;

  for (i = 0; i < G_N_ELEMENTS (widths); i++)
    {
      guint diff = ABS ((gint) data->pen_width - (gint) widths[i]);

      if (diff < best)
	{
	  best = diff;
	  current = i;
	}
    }

  if (down)
    current = (current + 1) % G_N_ELEMENTS (widths);
  else
    current = (current + G_N_ELEMENTS (widths) - 1) %
	      G_N_ELEMENTS (widths);

  data->pen_width = widths[current];
  apply_pen_settings (data);
  schedule_tray_menu_refresh (data);
}

/* One step of the stylus first-side-button action: eraser size for the
 * Eraser, colours for the Marker, colours in Color mode and line thickness in
 * Thickness mode. */
static void
apply_stylus_step (ScreenatorData *data, gboolean down)
{
  if (eraser_tool_active (data))
    cycle_eraser_size (data, down);
  else if (marker_tool_active (data))
    apply_scroll_step (data, down, TRUE);
  else if (data->stylus_mode == SCREENATOR_STYLUS_THICKNESS)
    cycle_line_thickness (data, down);
  else
    apply_scroll_step (data, down, TRUE);
}

/* The mouse wheel changes the line colour while a tool is active; over the
 * Paint Bucket it changes the shape fill colour instead.  The change is the
 * same as picking the colour from the menu: it is applied to the pen, saved
 * with the other settings and shown in the settings indicator and menus. */
gboolean
on_scroll (GtkWidget *win, GdkEventScroll *ev, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  static gdouble smooth_accumulator = 0.0;
  static gint64 last_step_time = 0;
  gint64 now;
  gboolean down;

  if (data->top_dialog)
    return TRUE;

  if (!screenator_any_grabbed (data))
    return FALSE;

  if (ev->direction == GDK_SCROLL_UP)
    down = FALSE;
  else if (ev->direction == GDK_SCROLL_DOWN)
    down = TRUE;
  else if (ev->direction == GDK_SCROLL_SMOOTH)
    {
      /* Touchpads report a stream of small deltas; collect a wheel notch
       * before changing the colour so one gesture is one step. */
      smooth_accumulator += ev->delta_y;

      if (smooth_accumulator > -1.0 && smooth_accumulator < 1.0)
	return TRUE;

      down = smooth_accumulator > 0;
      smooth_accumulator = 0.0;
    }
  else
    return FALSE;

  /* Some wheels and drivers report one physical notch as two events.  Taking
   * only one step per short interval keeps the colours in menu order instead
   * of skipping every other one. */
  now = g_get_monotonic_time ();
  if (last_step_time != 0 && now - last_step_time < 40000)
    return TRUE;
  last_step_time = now;

  /* The Eraser tool always uses the wheel for its size, whichever scroll
   * setting is selected. */
  if (eraser_tool_active (data))
    cycle_eraser_size (data, down);
  else if (marker_tool_active (data))
    apply_scroll_step (data, down, TRUE);
  else if (data->wheel_mode == SCREENATOR_WHEEL_THICKNESS)
    cycle_line_thickness (data, down);
  else
    apply_scroll_step (data, down, TRUE);

  return TRUE;
}

static void on_icon_color (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  const gchar *name = g_object_get_data (G_OBJECT (menuitem), "icon-color-name");

  apply_line_color (data, name);
}

static void on_icon_shape_fill (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  const gchar *name = g_object_get_data (G_OBJECT (menuitem),
					 "icon-shape-fill-name");

  apply_shape_fill (data, name);
}

/* Give the annotation window the keyboard focus while a paper is shown, so
 * typing does not reach the application below.  The previous focus window is
 * remembered and restored when the paper is turned off. */
void
paper_take_focus (ScreenatorData *data)
{
#ifdef GDK_WINDOWING_X11
  GdkWindow *window;
  GdkDisplay *display;
  Display *xdisplay;
  Window focus = None;
  int revert = 0;

  if (!data->win)
    return;

  window = gtk_widget_get_window (data->win);
  if (!window || !GDK_IS_X11_WINDOW (window))
    return;

  display = gdk_window_get_display (window);
  xdisplay = GDK_DISPLAY_XDISPLAY (display);

  if (data->paper_prev_focus == 0)
    {
      XGetInputFocus (xdisplay, &focus, &revert);
      if (focus != None && focus != PointerRoot &&
	  focus != GDK_WINDOW_XID (window))
	data->paper_prev_focus = focus;
    }

  gdk_x11_display_error_trap_push (display);
  XSetInputFocus (xdisplay, GDK_WINDOW_XID (window), RevertToParent,
		  CurrentTime);
  XFlush (xdisplay);
  gdk_x11_display_error_trap_pop_ignored (display);
#endif
}

void
paper_restore_focus (ScreenatorData *data)
{
#ifdef GDK_WINDOWING_X11
  GdkWindow *window;
  GdkDisplay *display;
  Display *xdisplay;

  if (!data->win || data->paper_prev_focus == 0)
    return;

  window = gtk_widget_get_window (data->win);
  if (!window || !GDK_IS_X11_WINDOW (window))
    return;

  display = gdk_window_get_display (window);
  xdisplay = GDK_DISPLAY_XDISPLAY (display);

  gdk_x11_display_error_trap_push (display);
  XSetInputFocus (xdisplay, data->paper_prev_focus, RevertToParent,
		  CurrentTime);
  XFlush (xdisplay);
  gdk_x11_display_error_trap_pop_ignored (display);

  data->paper_prev_focus = 0;
#endif
}

/* Show one of the screen papers, or turn it off again.  The paper is drawn on
 * the annotation window below the annotations, so the window has to stay
 * visible and above the other windows even when painting mode is off. */
static void
set_screen_paper (ScreenatorData *data, ScreenatorPaperType paper)
{
  if (paper != SCREENATOR_PAPER_NONE)
    screenator_clear_screenshot_paper (data);

  data->hide_drawings = FALSE;
  data->paper = paper;
  update_paper_shortcuts (data, paper != SCREENATOR_PAPER_NONE);

  update_window_shape (data);

  if (gtk_widget_get_window (data->win))
    {
      GdkRectangle rect = { 0, 0, data->width, data->height };

      gdk_window_invalidate_rect (gtk_widget_get_window (data->win), &rect, 0);
    }

  if (paper != SCREENATOR_PAPER_NONE)
    {
      if (data->hidden)
	show_window (data);
      else
	{
	  if (gtk_widget_get_window (data->win))
	    gdk_window_raise (gtk_widget_get_window (data->win));
	  raise_icon_window (data);
	}

      /* The paper is the active window: it keeps the keyboard, so typing does
       * not reach the application below. */
      paper_take_focus (data);
    }
  else
    {
      paper_restore_focus (data);

      if (!data->painted && !screenator_any_grabbed (data))
	hide_window (data);
    }

  write_keyfile (data);
  schedule_tray_menu_refresh (data);
}

static void on_icon_paper (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  ScreenatorPaperType paper =
    (ScreenatorPaperType) GPOINTER_TO_INT (
      g_object_get_data (G_OBJECT (menuitem), "icon-paper-type"));

  set_screen_paper (data, paper);
}

/* Leave the screen paper from the Ctrl+Q shortcut. */
void
screenator_exit_screen_paper (ScreenatorData *data)
{
  if (data->paper != SCREENATOR_PAPER_NONE)
    set_screen_paper (data, SCREENATOR_PAPER_NONE);
}

static void on_icon_thickness (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  guint width = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (menuitem),
						     "icon-thickness-width"));

  data->pen_width = width;

  apply_pen_settings (data);

  schedule_tray_menu_refresh (data);
}

static void on_icon_eraser_size (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  guint width = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (menuitem),
						     "icon-eraser-width"));

  apply_eraser_size (data, width);
}

static void on_icon_change_font (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  gboolean was_grabbed = screenator_any_grabbed (data);
  GtkWidget *dialog;
  gint response;

  /* Release the pointer grab while the dialog is open, so it can be used
   * while screening is active, and restore the grab afterwards. */
  if (was_grabbed)
    release_grab (data, NULL);

  dialog = gtk_font_chooser_dialog_new ("Select Text Font", NULL);

  gtk_window_set_keep_above (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  gtk_font_chooser_set_preview_text (GTK_FONT_CHOOSER (dialog),
				     "Screenator text annotation");
  if (data->font && *data->font)
    gtk_font_chooser_set_font (GTK_FONT_CHOOSER (dialog), data->font);

  /* Keep the dialog above the annotation window and its drawings. */
  show_dialog_above_drawings (data, dialog);

  /* The font chooser dialog reports GTK_RESPONSE_OK on older and newer GTK3
   * releases, while some builds used GTK_RESPONSE_ACCEPT.  Accept both so the
   * chosen font is always applied to the Text tool. */
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_ACCEPT)
    {
      gchar *font = gtk_font_chooser_get_font (GTK_FONT_CHOOSER (dialog));

      if (font && *font)
	{
	  g_free (data->font);
	  data->font = font;
	}
      else
	g_free (font);
    }

  gtk_widget_destroy (dialog);

  if (was_grabbed)
    acquire_grab (data, NULL);

  schedule_tray_menu_refresh (data);
}

static void on_icon_opacity (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  gint opacity_int = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (menuitem),
							 "icon-opacity"));

  set_pen_opacity (data, opacity_int / 100.0);

  schedule_tray_menu_refresh (data);
}

static gboolean
icon_reposition_timeout (gpointer user_data)
{
  ScreenatorData *data = user_data;

  /* The first update can run before the X window has processed its new size.
   * Reapply the pixbuf, shape and size request, then position the window. */
  update_icon_image (data);
  gtk_widget_queue_draw (data->icon_win);
  screenator_position_icon (data);
  return G_SOURCE_REMOVE;
}

/* A login-time start can run before the window manager and panel have settled
 * the monitor work area.  Recheck once shortly after startup. */
static gboolean
icon_startup_settle (gpointer user_data)
{
  ScreenatorData *data = user_data;
  GdkScreen *screen = data->screen;

  if (data->show_floating_icon && data->icon_win &&
      !gtk_widget_get_visible (data->icon_win))
    gtk_widget_show (data->icon_win);

  /* A login-time start can see the compositor before its RGBA visual is
   * available.  Rebuild the icon window once the visual appears, so later
   * size changes use an alpha window instead of a shaped one. */
  if (data->icon_shaped && data->icon_win)
    {
      GdkScreen *icon_screen = gtk_widget_get_screen (data->icon_win);
      GdkVisual *visual =
	icon_screen ? gdk_screen_get_rgba_visual (icon_screen) : NULL;

      if (visual && gdk_screen_is_composited (icon_screen))
	{
	  recreate_icon_windows_for_compositing (data);

	  /* The menu may still be open; try again when it closes. */
	  if (data->icon_shaped)
	    g_timeout_add (2000, icon_startup_settle, data);

	  return G_SOURCE_REMOVE;
	}
    }

  if (screen &&
      (gdk_screen_get_width (screen) != (gint) data->width ||
       gdk_screen_get_height (screen) != (gint) data->height))
    on_monitors_changed (screen, data);
  else
    screenator_position_icon (data);

  return G_SOURCE_REMOVE;
}

static void on_icon_tool_delay (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  guint delay = GPOINTER_TO_UINT (
    g_object_get_data (G_OBJECT (menuitem), "tool-delay-seconds"));

  data->tool_delay_seconds = delay;
  write_keyfile (data);
  schedule_tray_menu_refresh (data);
}

static void
icon_corner_position (ScreenatorData *data, gint current_size,
		      gint target_size, gint *x, gint *y)
{
  GdkRectangle area;
  gint monitor;
  const gint margin = 20;

  if (!data->screen)
    return;

  if (data->icon_win)
    {
      gint icon_x, icon_y;

      gtk_window_get_position (GTK_WINDOW (data->icon_win),
			       &icon_x, &icon_y);
      monitor = gdk_screen_get_monitor_at_point (data->screen,
						 icon_x + current_size / 2,
						 icon_y + current_size / 2);
    }
  else
    monitor = gdk_screen_get_primary_monitor (data->screen);

  if (monitor < 0)
    monitor = 0;
  gdk_screen_get_monitor_workarea (data->screen, monitor, &area);

  switch (data->icon_position)
    {
    case SCREENATOR_ICON_TOP_LEFT:
      *x = area.x + margin;
      *y = area.y + margin;
      break;
    case SCREENATOR_ICON_BOTTOM_RIGHT:
      *x = area.x + area.width - target_size - margin;
      *y = area.y + area.height - target_size - margin;
      break;
    case SCREENATOR_ICON_BOTTOM_LEFT:
      *x = area.x + margin;
      *y = area.y + area.height - target_size - margin;
      break;
    case SCREENATOR_ICON_TOP_RIGHT:
    default:
      *x = area.x + area.width - target_size - margin;
      *y = area.y + margin;
      break;
    }

  *x = CLAMP (*x, area.x,
	      MAX (area.x, area.x + area.width - target_size));
  *y = CLAMP (*y, area.y,
	      MAX (area.y, area.y + area.height - target_size));
}

void
screenator_position_icon (ScreenatorData *data)
{
  gint x = 0, y = 0;

  if (!data->icon_win)
    return;

  icon_corner_position (data, (gint) data->icon_size,
			(gint) data->icon_size, &x, &y);
  gtk_window_move (GTK_WINDOW (data->icon_win), x, y);
  update_settings_indicator (data);
  raise_icon_window (data);
}

static void on_icon_size (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  guint new_size = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (menuitem),
                                                        "icon-size-value"));
  data->icon_size = new_size;

  /* Redraw at the new size first, as before.  Move the window afterwards so
   * the resize and the redraw are not interleaved. */
  gtk_widget_set_size_request (data->icon_win,
			       (gint) new_size, (gint) new_size);
  gtk_window_resize (GTK_WINDOW (data->icon_win), new_size, new_size);
  {
    GdkWindow *window = gtk_widget_get_window (data->icon_win);

    if (window)
      {
	gdk_window_resize (window, (gint) new_size, (gint) new_size);
	gdk_display_sync (gdk_display_get_default ());
      }
  }
  update_icon_image (data);
  update_settings_indicator (data);
  raise_icon_window (data);

  if (data->icon_win)
    g_timeout_add (50, icon_reposition_timeout, data);

  /* Save immediately: logging out can terminate the app before the normal
   * exit-time keyfile write. */
  write_keyfile (data);
  schedule_tray_menu_refresh (data);
}

void
set_floating_icon_visible (ScreenatorData *data, gboolean show)
{
  data->show_floating_icon = show;

  if (show)
    {
      gtk_widget_show (data->icon_win);
      screenator_position_icon (data);
      update_settings_indicator (data);
      raise_icon_window (data);
    }
  else
    {
      icon_cancel_menu_retry (data);
      icon_ungrab_pointer (data);
      gtk_widget_hide (data->icon_win);
      if (data->settings_win)
	gtk_widget_hide (data->settings_win);
    }

  write_keyfile (data);
  schedule_tray_menu_refresh (data);
}

static void on_icon_toggle_floating (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  set_floating_icon_visible (data,
			     gtk_check_menu_item_get_active (
			       GTK_CHECK_MENU_ITEM (menuitem)));
}

static void on_icon_toggle_save_annotations (GtkMenuItem *menuitem,
					      gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  data->save_annotations_on_exit =
    gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem));

  /* Do not bring back older drawings after the option has been turned off. */
  if (!data->save_annotations_on_exit)
    remove_saved_annotations ();

  write_keyfile (data);
  schedule_tray_menu_refresh (data);
}

static void on_icon_toggle_settings_indicator (GtkMenuItem *menuitem,
					       gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  data->show_settings_indicator =
    gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem));

  update_settings_indicator (data);
  write_keyfile (data);
  schedule_tray_menu_refresh (data);
}

static void on_icon_position (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  static const gchar * const labels[] = {
    "Top Right", "Top Left", "Bottom Right", "Bottom Left"
  };
  GtkWidget *dialog, *content, *box, *label;
  GtkWidget *buttons[G_N_ELEMENTS (labels)];
  gint response, selected = 0;
  guint i;

  dialog = gtk_dialog_new_with_buttons ("Floating Icon Position", NULL,
					GTK_DIALOG_MODAL |
					GTK_DIALOG_DESTROY_WITH_PARENT,
					"Cancel", GTK_RESPONSE_CANCEL,
					"OK", GTK_RESPONSE_OK,
					NULL);
  gtk_window_set_keep_above (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  show_dialog_above_drawings (data, dialog);

  content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width (GTK_CONTAINER (box), 12);
  gtk_container_add (GTK_CONTAINER (content), box);

  label = gtk_label_new ("Choose where the floating icon is placed:");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  for (i = 0; i < G_N_ELEMENTS (labels); i++)
    {
      buttons[i] = gtk_radio_button_new_with_label (
	i == 0 ? NULL : gtk_radio_button_get_group (GTK_RADIO_BUTTON (buttons[0])),
	labels[i]);
      gtk_box_pack_start (GTK_BOX (box), buttons[i], FALSE, FALSE, 0);
      if ((guint) data->icon_position == i)
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (buttons[i]), TRUE);
    }

  gtk_widget_show_all (dialog);
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response == GTK_RESPONSE_OK)
    {
      for (i = 0; i < G_N_ELEMENTS (buttons); i++)
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (buttons[i])))
	  {
	    selected = (gint) i;
	    break;
	  }

      data->icon_position = (ScreenatorIconPosition) selected;
      write_keyfile (data);
      screenator_position_icon (data);
      schedule_tray_menu_refresh (data);
    }

  gtk_widget_destroy (dialog);
}

static void on_icon_reset_settings (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  GHashTableIter it;
  gpointer value;

  data->pen_width = KEY_DFLT_PEN_WIDTH;
  data->eraser_width = KEY_DFLT_ERASER_WIDTH;
  gdk_rgba_parse (&data->pen_color, KEY_DFLT_PEN_COLOR);
  data->pen_color.alpha = 1.0;
  data->opacity = DEFAULT_OPACITY;
  data->marker_opacity = 0.40;
  data->icon_size = KEY_DFLT_ICON_SIZE;
  data->icon_position = SCREENATOR_ICON_TOP_RIGHT;
  data->tool_delay_seconds = 0;
  data->pen_tool = 1;
  g_free (data->font);
  data->font = g_strdup (KEY_DFLT_FONT);
  data->shape_fill_enabled = FALSE;
  gdk_rgba_parse (&data->shape_fill_color, KEY_DFLT_SHAPE_FILL_COLOR);
  data->shape_fill_color.alpha = 1.0;

  /* Reset Settings also puts the normal desktop back. */
  data->paper = SCREENATOR_PAPER_NONE;
  update_window_shape (data);
  if (!data->painted && !screenator_any_grabbed (data))
    hide_window (data);

  apply_pen_settings (data);
  update_eraser_cursor (data);

  /* Let select_tool() pick up the reset tool on the next event. */
  if (data->devdatatable)
    {
      g_hash_table_iter_init (&it, data->devdatatable);
      while (g_hash_table_iter_next (&it, NULL, &value))
	((ScreenatorDeviceData *) value)->state = 0;
    }

  gtk_window_resize (GTK_WINDOW (data->icon_win), data->icon_size,
		     data->icon_size);
  update_icon_image (data);
  screenator_position_icon (data);
  write_keyfile (data);
  schedule_tray_menu_refresh (data);
}

static gboolean on_icon_draw (GtkWidget *widget,
			      cairo_t *cr,
			      gpointer user_data)
{
  /* Clear the icon window to fully transparent.  The window is marked
   * app-paintable so GTK does not paint an opaque theme background behind
   * the transparent icon pixbuf. */
  cairo_save (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba (cr, 0, 0, 0, 0);
  cairo_paint (cr);
  cairo_restore (cr);

  return FALSE;
}

/* ------------------------------------------------------------------
 * Settings indicator
 * ------------------------------------------------------------------ */

/* A small settings indicator to the left of the floating icon.  It is a plain
 * rectangle as tall as the icon, divided into three equal-height pieces that
 * are centred horizontally: the upper piece is the current line width in the
 * line colour, the middle piece is 24 pixels of the line colour and the lower
 * piece is 24 pixels of the shape fill colour (a black and white checkerboard
 * when shapes are not filled).  A one pixel black outline surrounds the whole
 * indicator.  It is a separate popup window so it stays above the annotation
 * window together with the icon, and it lets clicks through to the screen
 * below. */
#define THICKNESS_RING_WIDTH 6.0
#define OUTER_RING_WIDTH 6.0
#define THICKNESS_SECTORS 9

static const guint thickness_values[THICKNESS_SECTORS] =
  { 1, 2, 3, 4, 5, 6, 12, 24, 48 };

/* The rings grow with the largest icon sizes so they stay readable: twice the
 * original width at 128x128 and three times at 256x256. */
static gdouble
settings_indicator_ring_scale (ScreenatorData *data)
{
  if (data->icon_size >= 256)
    return 3.0;
  if (data->icon_size >= 128)
    return 2.0;
  return 1.0;
}

static gdouble
settings_indicator_thickness_width (ScreenatorData *data)
{
  return THICKNESS_RING_WIDTH * settings_indicator_ring_scale (data);
}

static gdouble
settings_indicator_outer_width (ScreenatorData *data)
{
  return OUTER_RING_WIDTH * settings_indicator_ring_scale (data);
}

static gint
settings_indicator_size (ScreenatorData *data)
{
  return MAX (1, (gint) data->icon_size +
	      2 * (gint) (settings_indicator_thickness_width (data) +
			  settings_indicator_outer_width (data)) + 4);
}

static gdouble
settings_indicator_inner_radius (ScreenatorData *data)
{
  return MAX (1.0, data->icon_size / 2.0 - 2.0);
}

/* An annular sector.  ccw selects the direction from a0 to a1: the thickness
 * sectors run counter clockwise from angle zero. */
static void
ring_sector_path (cairo_t *cr, gdouble cx, gdouble cy,
		  gdouble r_in, gdouble r_out, gdouble a0, gdouble a1,
		  gboolean ccw)
{
  cairo_new_path (cr);

  if (ccw)
    {
      cairo_arc_negative (cr, cx, cy, r_out, a0, a1);
      cairo_arc (cr, cx, cy, r_in, a1, a0);
    }
  else
    {
      cairo_arc (cr, cx, cy, r_out, a0, a1);
      cairo_arc_negative (cr, cx, cy, r_in, a1, a0);
    }

  cairo_close_path (cr);
}

/* Fill one sector and give it a one pixel outline. */
static void
paint_ring_sector (cairo_t *cr, gdouble cx, gdouble cy,
		   gdouble r_in, gdouble r_out, gdouble a0, gdouble a1,
		   gboolean ccw, const GdkRGBA *color, gboolean mask)
{
  ring_sector_path (cr, cx, cy, r_in, r_out, a0, a1, ccw);

  if (mask)
    cairo_set_source_rgba (cr, 1, 1, 1, 1);
  else
    cairo_set_source_rgba (cr, color->red, color->green, color->blue,
			   color->alpha ? color->alpha : 1.0);
  cairo_fill_preserve (cr);

  if (mask)
    cairo_set_source_rgba (cr, 1, 1, 1, 1);
  else
    cairo_set_source_rgba (cr, 0, 0, 0, 1);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);
}

/* Outline a sector without filling it. */
static void
stroke_ring_sector (cairo_t *cr, gdouble cx, gdouble cy,
		    gdouble r_in, gdouble r_out, gdouble a0, gdouble a1,
		    gboolean ccw, gboolean mask)
{
  ring_sector_path (cr, cx, cy, r_in, r_out, a0, a1, ccw);

  if (mask)
    cairo_set_source_rgba (cr, 1, 1, 1, 1);
  else
    cairo_set_source_rgba (cr, 0, 0, 0, 1);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);
}

/* How many thickness sectors are yellow for the current line width.  The
 * width is matched to the closest entry of the Line Thickness menu. */
static guint
thickness_sector_count (ScreenatorData *data)
{
  guint best = 0;
  guint best_diff = ABS ((gint) data->pen_width -
			 (gint) thickness_values[0]);
  guint i;

  for (i = 1; i < THICKNESS_SECTORS; i++)
    {
      guint diff = ABS ((gint) data->pen_width - (gint) thickness_values[i]);

      if (diff < best_diff)
	{
	  best = i;
	  best_diff = diff;
	}
    }

  return best + 1;
}

/* Draw the indicator: a white thickness ring around the icon, made of one
 * sector per menu thickness and filled with yellow up to the selected one,
 * and an outer ring whose upper half is the line colour and whose lower half
 * is the fill colour (or twelve black and white radial stripes).  Both rings
 * grow with the largest icon sizes.  With mask TRUE everything is opaque
 * white, so the same geometry can be used for the window shape and its input
 * region. */
static void
draw_settings_indicator (ScreenatorData *data, cairo_t *cr,
			 gdouble width, gdouble height, gboolean mask)
{
  /* Magenta for the selected thickness sectors. */
  static const GdkRGBA magenta = { 1.0, 0.0, 1.0, 1.0 };
  static const GdkRGBA white = { 1.0, 1.0, 1.0, 1.0 };
  gdouble cx = width / 2.0;
  gdouble cy = height / 2.0;
  gdouble r0 = settings_indicator_inner_radius (data);
  gdouble r1 = r0 + settings_indicator_thickness_width (data);
  gdouble r2 = r1 + settings_indicator_outer_width (data);
  gdouble step = 2.0 * G_PI / THICKNESS_SECTORS;
  guint filled = thickness_sector_count (data);
  gint i;

  cairo_save (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba (cr, 0, 0, 0, 0);
  cairo_paint (cr);
  cairo_restore (cr);

  /* Thickness ring: white sectors, yellow up to the selected thickness. */
  for (i = 0; i < THICKNESS_SECTORS; i++)
    {
      gdouble a0 = -i * step;
      gdouble a1 = -(i + 1) * step;

      paint_ring_sector (cr, cx, cy, r0, r1, a0, a1, TRUE,
			 (guint) i < filled ? &magenta : &white, mask);
    }

  /* Outer ring, upper half: line colour. */
  paint_ring_sector (cr, cx, cy, r1, r2, G_PI, 2.0 * G_PI, FALSE,
		     &data->pen_color, mask);

  /* Outer ring, lower half: fill colour or twelve stripes. */
  if (mask || data->shape_fill_enabled)
    paint_ring_sector (cr, cx, cy, r1, r2, 0.0, G_PI, FALSE,
		       mask ? &white : &data->shape_fill_color, mask);
  else
    {
      gdouble stripe = G_PI / 12.0;

      for (i = 0; i < 12; i++)
	{
	  ring_sector_path (cr, cx, cy, r1, r2, i * stripe,
			    (i + 1) * stripe, FALSE);
	  if ((i & 1) == 0)
	    cairo_set_source_rgba (cr, 1, 1, 1, 1);
	  else
	    cairo_set_source_rgba (cr, 0, 0, 0, 1);
	  cairo_fill (cr);
	}

      stroke_ring_sector (cr, cx, cy, r1, r2, 0.0, G_PI, FALSE, FALSE);
    }
}

/* The ring geometry as a region, used both for the input shape (only the ring
 * catches clicks) and for the visual shape on screens without a compositor. */
static cairo_region_t *
settings_indicator_region (ScreenatorData *data, gint width, gint height)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  cairo_region_t *region;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create (surface);
  draw_settings_indicator (data, cr, width, height, TRUE);
  cairo_destroy (cr);
  cairo_surface_flush (surface);
  region = gdk_cairo_region_create_from_surface (surface);
  cairo_surface_destroy (surface);

  return region;
}

static void
position_settings_indicator (ScreenatorData *data)
{
  gint icon_x, icon_y, size, x, y;

  if (!data->settings_win || !data->icon_win)
    return;

  gtk_window_get_position (GTK_WINDOW (data->icon_win), &icon_x, &icon_y);
  size = settings_indicator_size (data);
  x = icon_x + (gint) data->icon_size / 2 - size / 2;
  y = icon_y + (gint) data->icon_size / 2 - size / 2;

  gtk_window_move (GTK_WINDOW (data->settings_win), x, y);
}

static gboolean
on_settings_indicator_draw (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  draw_settings_indicator (data, cr,
			   gtk_widget_get_allocated_width (widget),
			   gtk_widget_get_allocated_height (widget), FALSE);
  return FALSE;
}

static void
raise_settings_indicator (ScreenatorData *data)
{
  GdkWindow *window;

  if (!data->settings_win || !gtk_widget_get_visible (data->settings_win))
    return;

  window = gtk_widget_get_window (data->settings_win);
  if (window)
    gdk_window_raise (window);
}

void
update_settings_indicator (ScreenatorData *data)
{
  gint size;
  cairo_region_t *region;

  if (!data->settings_win)
    return;

  if (!data->show_settings_indicator || !data->icon_win ||
      !gtk_widget_get_visible (data->icon_win))
    {
      gtk_widget_hide (data->settings_win);
      return;
    }

  size = settings_indicator_size (data);

  gtk_widget_set_size_request (data->settings_win, size, size);
  gtk_window_resize (GTK_WINDOW (data->settings_win), size, size);

  region = settings_indicator_region (data, size, size);

  /* Only the ring itself catches clicks, so the transparent corners and the
   * area around the icon stay clickable. */
  gtk_widget_input_shape_combine_region (data->settings_win, region);
  if (!data->composited)
    gtk_widget_shape_combine_region (data->settings_win, region);
  cairo_region_destroy (region);

  position_settings_indicator (data);

  if (!gtk_widget_get_visible (data->settings_win))
    gtk_widget_show (data->settings_win);

  gtk_widget_queue_draw (data->settings_win);
  raise_settings_indicator (data);
}

/* Left and right clicks on the ring open the same menu as the floating icon.
 * Other buttons and scrolling are only swallowed, so they do not reach the
 * desktop or the application below. */
static gboolean
on_settings_indicator_button (GtkWidget *widget, GdkEventButton *event,
			      gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if (event->type == GDK_BUTTON_RELEASE &&
      (event->button == 1 || event->button == 3))
    {
      icon_cancel_menu_retry (data);
      icon_ungrab_pointer (data);
      popup_icon_menu (data);
    }

  return TRUE;
}

static gboolean
on_settings_indicator_scroll (GtkWidget *widget, GdkEventScroll *event,
			      gpointer user_data)
{
  return TRUE;
}

static void
create_settings_indicator (ScreenatorData *data)
{
  GdkScreen *screen;
  GdkVisual *visual;

  data->settings_win = gtk_window_new (GTK_WINDOW_POPUP);
  gtk_window_set_decorated (GTK_WINDOW (data->settings_win), FALSE);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (data->settings_win), TRUE);
  gtk_window_set_skip_pager_hint (GTK_WINDOW (data->settings_win), TRUE);
  gtk_window_set_keep_above (GTK_WINDOW (data->settings_win), TRUE);
  gtk_window_set_resizable (GTK_WINDOW (data->settings_win), FALSE);
  gtk_window_set_type_hint (GTK_WINDOW (data->settings_win),
			    GDK_WINDOW_TYPE_HINT_DOCK);

  /* Paint the window ourselves so the rounded rectangle is not drawn on the
   * theme's opaque window background. */
  gtk_widget_set_app_paintable (data->settings_win, TRUE);

  screen = gtk_widget_get_screen (data->settings_win);
  visual = gdk_screen_get_rgba_visual (screen);
  if (visual && gdk_screen_is_composited (screen))
    gtk_widget_set_visual (data->settings_win, visual);

  g_signal_connect (data->settings_win, "draw",
		    G_CALLBACK (on_settings_indicator_draw), data);

  gtk_widget_realize (data->settings_win);

  /* The indicator is not interactive, but it must not let clicks reach the
   * desktop or the application below it.  Keeping the default input shape
   * (the whole window) and swallowing the events does that. */
  gtk_widget_add_events (data->settings_win,
			 GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
			 GDK_SCROLL_MASK);
  g_signal_connect (data->settings_win, "button-press-event",
		    G_CALLBACK (on_settings_indicator_button), data);
  g_signal_connect (data->settings_win, "button-release-event",
		    G_CALLBACK (on_settings_indicator_button), data);
  g_signal_connect (data->settings_win, "scroll-event",
		    G_CALLBACK (on_settings_indicator_scroll), data);

  update_settings_indicator (data);
}

void create_icon_window (ScreenatorData *data)
{
  GdkPixbuf *pixbuf = load_icon_pixbuf (data, FALSE);
  GtkWidget *image;

  data->icon_win = gtk_window_new (GTK_WINDOW_POPUP);
  gtk_window_set_decorated (GTK_WINDOW (data->icon_win), FALSE);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (data->icon_win), TRUE);
  gtk_window_set_keep_above (GTK_WINDOW (data->icon_win), TRUE);
  gtk_window_set_resizable (GTK_WINDOW (data->icon_win), FALSE);
  gtk_window_set_type_hint (GTK_WINDOW (data->icon_win),
			    GDK_WINDOW_TYPE_HINT_DOCK);

  /* Use an RGBA visual so the transparent SVG icon is not shown on an
   * opaque (white) window background. */
  {
    GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (data->icon_win));
    GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
    gboolean use_alpha = visual && gdk_screen_is_composited (screen);

    data->icon_shaped = !use_alpha;

    if (use_alpha)
      {
	gtk_widget_set_visual (GTK_WIDGET (data->icon_win), visual);
	/* Paint the window background ourselves, so GTK does not fill it
	 * with the theme's opaque window colour. */
	gtk_widget_set_app_paintable (GTK_WIDGET (data->icon_win), TRUE);
	g_signal_connect (data->icon_win, "draw",
			  G_CALLBACK (on_icon_draw), data);
      }
  }

  image = gtk_image_new_from_pixbuf (pixbuf ? pixbuf : NULL);
  if (pixbuf)
    g_object_unref (pixbuf);
  data->icon_image = image;
  gtk_container_add (GTK_CONTAINER (data->icon_win), image);

  gtk_widget_add_events (data->icon_win,
			 GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
			 GDK_POINTER_MOTION_MASK);

  g_signal_connect (data->icon_win, "button-press-event",
		    G_CALLBACK (on_icon_button_press), data);
  g_signal_connect (data->icon_win, "motion-notify-event",
		    G_CALLBACK (on_icon_motion), data);
  g_signal_connect (data->icon_win, "button-release-event",
		    G_CALLBACK (on_icon_button_release), data);
  g_signal_connect (data->icon_win, "grab-broken-event",
		    G_CALLBACK (on_icon_grab_broken), data);
  g_signal_connect (data->icon_win, "configure-event",
		    G_CALLBACK (on_icon_configure), data);

  data->icon_dragging = FALSE;
  data->icon_pointer_grabbed = FALSE;
  data->icon_focus_taken = FALSE;
  data->icon_prev_focus = 0;
  data->icon_menu_pending = NULL;
  data->icon_menu_retry_id = 0;
  data->icon_menu_retry_count = 0;
  data->icon_drag_start_x = -1;
  data->icon_drag_start_y = -1;
  data->icon_press_x = -1;
  data->icon_press_y = -1;
  data->icon_menu_x = -1;
  data->icon_menu_y = -1;

  gtk_widget_show_all (data->icon_win);
  update_icon_shape (data);

  /* Place the icon at the configured corner of its monitor.  At login the
   * panel/window manager may not have settled the work area yet, so recheck
   * once shortly after startup. */
  screenator_position_icon (data);
  g_timeout_add (1500, icon_startup_settle, data);
  g_timeout_add (4000, icon_startup_settle, data);
  g_timeout_add (8000, icon_startup_settle, data);

  /* The annotation window is created first; make sure the icon starts above
   * it, otherwise the annotation window takes the clicks for the icon. */
  raise_icon_window (data);

  /* The settings indicator is a ring around the icon. */
  create_settings_indicator (data);

  /* A hidden floating menu stays hidden after a restart. */
  if (!data->show_floating_icon)
    {
      gtk_widget_hide (data->icon_win);
      if (data->settings_win)
	gtk_widget_hide (data->settings_win);
    }
}

static void icon_menu_position_func (GtkMenu  *menu,
				     gint     *x,
				     gint     *y,
				     gboolean *push_in,
				     gpointer  user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  *x = data->icon_menu_x;
  *y = data->icon_menu_y;
  *push_in = TRUE;
}

static void append_icon_tool_items (ScreenatorData *data, GtkWidget *menu)
{
  GtkWidget *item;
  GtkWidget *submenu;

  item = gtk_image_menu_item_new_with_label ("Pen");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("pen.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_icon_pen), data);

  item = gtk_image_menu_item_new_with_label ("Text");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("text.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_icon_text), data);

  item = gtk_image_menu_item_new_with_label ("Eraser");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("eraser.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_icon_eraser), data);

  /* --- shapes submenu --- */
  item = gtk_image_menu_item_new_with_label ("Shapes");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("shapes.png")));
  submenu = gtk_menu_new ();
  {
    static const struct
    {
      const gchar *label;
      const gchar *icon;
      GCallback    callback;
    } shapes[] =
      {
	{ "Marker",             "marker.png",             G_CALLBACK (on_icon_marker) },
	{ "Line",               "free-line.png",          G_CALLBACK (on_icon_line) },
	{ "Spline",             "spline.png",             G_CALLBACK (on_icon_spline) },
	{ "Polygon",            "polygon.png",            G_CALLBACK (on_icon_polygon) },
	{ "Triangle",           "triangle.png",           G_CALLBACK (on_icon_triangle) },
	{ "Arrow",              "arrow.png",              G_CALLBACK (on_icon_arrow) },
	{ "Ellipse",            "ellipse.png",            G_CALLBACK (on_icon_ellipse) },
	{ "Rounded Rectangle",  "rounded-rectangle.png",  G_CALLBACK (on_icon_rounded_rectangle) },
	{ "Paint Bucket",       "bucket.png",             G_CALLBACK (on_icon_bucket) },
	{ "Math Function",      "math.png",               G_CALLBACK (on_icon_math) },
	{ "Parallel Lines",     "bars.png",               G_CALLBACK (on_icon_bars) },
	{ "Random Signal",      "random-signal.png",      G_CALLBACK (on_icon_random) }
      };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (shapes); i++)
      {
	GtkWidget *shape_item =
	  gtk_image_menu_item_new_with_label (shapes[i].label);

	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (shape_item),
	                               gtk_image_new_from_pixbuf (
	                                 load_menu_icon (shapes[i].icon)));
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), shape_item);
	g_signal_connect (shape_item, "activate", shapes[i].callback, data);
	gtk_widget_show (shape_item);
      }
  }
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  /* --- delayed tool activation submenu --- */
  item = gtk_menu_item_new_with_label ("Tool Activation Delay");
  submenu = gtk_menu_new ();
  {
    GSList *group = NULL;
    static const guint delays[] = { 0, 1, 2, 3, 4, 5, 7, 9, 11 };
    static const gchar * const labels[] = {
      "0 s", "1 s", "2 s", "3 s", "4 s", "5 s", "7 s", "9 s", "11 s"
    };
    GtkWidget *items[G_N_ELEMENTS (delays)];
    GtkWidget *active_item = NULL;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (delays); i++)
      {
	GtkWidget *subitem = gtk_radio_menu_item_new_with_label (group,
								 labels[i]);
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), subitem);
	gtk_widget_show (subitem);
	group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (subitem));
	g_object_set_data (G_OBJECT (subitem), "tool-delay-seconds",
			   GUINT_TO_POINTER (delays[i]));
	items[i] = subitem;
	if (data->tool_delay_seconds == delays[i])
	  active_item = subitem;
      }
    if (active_item)
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item), TRUE);
    for (i = 0; i < G_N_ELEMENTS (delays); i++)
      g_signal_connect (items[i], "activate",
			G_CALLBACK (on_icon_tool_delay), data);
  }
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  /* --- line thickness submenu --- */
  item = gtk_menu_item_new_with_label ("Line Thickness");
  submenu = gtk_menu_new ();
  {
    GSList *group = NULL;
    static const guint widths[] = { 1, 2, 3, 4, 5, 6, 12, 24, 48 };
    static const gchar * const labels[] = { "1 px", "2 px", "3 px", "4 px",
					    "5 px", "6 px", "12 px", "24 px",
					    "48 px" };
    GtkWidget *items[G_N_ELEMENTS (widths)];
    GtkWidget *active_item = NULL;
    guint i;
    guint closest = 0;
    guint closest_diff = ABS ((gint) data->pen_width - (gint) widths[0]);

    for (i = 1; i < G_N_ELEMENTS (widths); i++)
      {
	guint diff = ABS ((gint) data->pen_width - (gint) widths[i]);

	if (diff < closest_diff)
	  {
	    closest = i;
	    closest_diff = diff;
	  }
      }

    for (i = 0; i < G_N_ELEMENTS (widths); i++)
      {
	GtkWidget *subitem = gtk_radio_menu_item_new_with_label (group, labels[i]);
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), subitem);
	gtk_widget_show (subitem);
	group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (subitem));
	g_object_set_data (G_OBJECT (subitem), "icon-thickness-width",
			   GUINT_TO_POINTER (widths[i]));
	items[i] = subitem;
	if (i == closest)
	  active_item = subitem;
      }
    /* Activating a radio item programmatically emits "activate", so mark the
     * current entry first and connect the handlers afterwards. */
    if (active_item)
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item), TRUE);
    for (i = 0; i < G_N_ELEMENTS (widths); i++)
      g_signal_connect (items[i], "activate", G_CALLBACK (on_icon_thickness), data);
  }
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  /* --- line color submenu --- */
  item = gtk_menu_item_new_with_label ("Line Color");
  submenu = gtk_menu_new ();
  {
    GSList *group = NULL;
    GtkWidget *items[G_N_ELEMENTS (line_colors)];
    GtkWidget *active_item = NULL;
    GtkWidget *custom_item = NULL;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (line_colors); i++)
      {
	GtkWidget *subitem;
	GdkRGBA color;

	subitem = gtk_radio_menu_item_new_with_label (group, line_colors[i].label);
	group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (subitem));
	if (gdk_rgba_parse (&color, line_colors[i].name))
	  {
	    GdkRGBA *current = data->default_pen->paint_color;

	    /* Opacity is a setting of its own, so compare the hue only. */
	    if (current->red == color.red &&
		current->green == color.green &&
		current->blue == color.blue)
	      active_item = subitem;
	  }
	g_object_set_data (G_OBJECT (subitem), "icon-color-name",
			   (gpointer) line_colors[i].name);
	items[i] = subitem;
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), subitem);
	gtk_widget_show (subitem);
      }

    {
      GtkWidget *sep = gtk_separator_menu_item_new ();
      GtkWidget *custom =
	gtk_radio_menu_item_new_with_label (group, "Custom...");

      group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (custom));
      g_object_set_data (G_OBJECT (custom), "icon-custom-target",
			 (gpointer) "line");
      if (!active_item)
	active_item = custom;
      custom_item = custom;
      gtk_menu_shell_append (GTK_MENU_SHELL (submenu), sep);
      gtk_widget_show (sep);
      gtk_menu_shell_append (GTK_MENU_SHELL (submenu), custom);
      gtk_widget_show (custom);
    }

    if (active_item)
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item), TRUE);
    for (i = 0; i < G_N_ELEMENTS (line_colors); i++)
      g_signal_connect (items[i], "activate", G_CALLBACK (on_icon_color), data);
    if (custom_item)
      g_signal_connect (custom_item, "activate",
			G_CALLBACK (on_icon_custom_color), data);
  }
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  /* --- shape fill colour submenu --- */
  item = gtk_menu_item_new_with_label ("Shapes Fill Color");
  submenu = gtk_menu_new ();
  {
    GSList *group = NULL;
    GtkWidget *items[G_N_ELEMENTS (fill_colors)];
    GtkWidget *active_item = NULL;
    GtkWidget *custom_item = NULL;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (fill_colors); i++)
      {
	GtkWidget *subitem;

	subitem = gtk_radio_menu_item_new_with_label (group, fill_colors[i].label);
	group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (subitem));

	if (g_ascii_strcasecmp (fill_colors[i].name, "none") == 0)
	  {
	    if (!data->shape_fill_enabled)
	      active_item = subitem;
	  }
	else
	  {
	    GdkRGBA color;

	    if (data->shape_fill_enabled &&
		gdk_rgba_parse (&color, fill_colors[i].name) &&
		data->shape_fill_color.red == color.red &&
		data->shape_fill_color.green == color.green &&
		data->shape_fill_color.blue == color.blue)
	      active_item = subitem;
	  }

	g_object_set_data (G_OBJECT (subitem), "icon-shape-fill-name",
			   (gpointer) fill_colors[i].name);
	items[i] = subitem;
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), subitem);
	gtk_widget_show (subitem);
      }

    {
      GtkWidget *sep = gtk_separator_menu_item_new ();
      GtkWidget *custom =
	gtk_radio_menu_item_new_with_label (group, "Custom...");

      group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (custom));
      g_object_set_data (G_OBJECT (custom), "icon-custom-target",
			 (gpointer) "fill");
      if (!active_item)
	active_item = custom;
      custom_item = custom;
      gtk_menu_shell_append (GTK_MENU_SHELL (submenu), sep);
      gtk_widget_show (sep);
      gtk_menu_shell_append (GTK_MENU_SHELL (submenu), custom);
      gtk_widget_show (custom);
    }

    if (active_item)
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item), TRUE);
    for (i = 0; i < G_N_ELEMENTS (fill_colors); i++)
      g_signal_connect (items[i], "activate",
			G_CALLBACK (on_icon_shape_fill), data);
    if (custom_item)
      g_signal_connect (custom_item, "activate",
			G_CALLBACK (on_icon_custom_color), data);
  }
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  /* --- screen paper submenu --- */
  item = gtk_image_menu_item_new_with_label ("Screen Paper");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("paper.png")));
  submenu = gtk_menu_new ();
  {
    GSList *group = NULL;
    static const struct
    {
      const gchar        *label;
      ScreenatorPaperType paper;
    } papers[] =
      {
	{ "None",  SCREENATOR_PAPER_NONE },
	{ "Plain", SCREENATOR_PAPER_PLAIN },
	{ "Ruled", SCREENATOR_PAPER_RULED },
	{ "Graph", SCREENATOR_PAPER_GRAPH }
      };
    GtkWidget *items[G_N_ELEMENTS (papers)];
    guint i;

    for (i = 0; i < G_N_ELEMENTS (papers); i++)
      {
	GtkWidget *subitem =
	  gtk_radio_menu_item_new_with_label (group, papers[i].label);

	group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (subitem));
	g_object_set_data (G_OBJECT (subitem), "icon-paper-type",
			   GINT_TO_POINTER (papers[i].paper));
	items[i] = subitem;
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), subitem);
	gtk_widget_show (subitem);

	if (papers[i].paper == data->paper)
	  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (subitem), TRUE);
      }
    for (i = 0; i < G_N_ELEMENTS (papers); i++)
      g_signal_connect (items[i], "activate",
			G_CALLBACK (on_icon_paper), data);
  }
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  /* --- eraser size submenu (above Opacity) --- */
  item = gtk_menu_item_new_with_label ("Eraser Size");
  submenu = gtk_menu_new ();
  {
    GSList *group = NULL;
    static const guint widths[] = { 20, 40, 60, 120 };
    static const gchar * const labels[] = { "20 px", "40 px", "60 px", "120 px" };
    GtkWidget *items[G_N_ELEMENTS (widths)];
    GtkWidget *active_item = NULL;
    guint i;
    guint closest = 0;
    guint closest_diff = ABS ((gint) data->eraser_width - (gint) widths[0]);

    for (i = 1; i < G_N_ELEMENTS (widths); i++)
      {
	guint diff = ABS ((gint) data->eraser_width - (gint) widths[i]);

	if (diff < closest_diff)
	  {
	    closest = i;
	    closest_diff = diff;
	  }
      }

    for (i = 0; i < G_N_ELEMENTS (widths); i++)
      {
	GtkWidget *subitem = gtk_radio_menu_item_new_with_label (group, labels[i]);
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), subitem);
	gtk_widget_show (subitem);
	group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (subitem));
	g_object_set_data (G_OBJECT (subitem), "icon-eraser-width",
			   GUINT_TO_POINTER (widths[i]));
	items[i] = subitem;
	if (i == closest)
	  active_item = subitem;
      }
    if (active_item)
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item), TRUE);
    for (i = 0; i < G_N_ELEMENTS (widths); i++)
      g_signal_connect (items[i], "activate",
			G_CALLBACK (on_icon_eraser_size), data);
  }
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  /* --- opacity submenu --- */
  item = gtk_menu_item_new_with_label ("Opacity");
  submenu = gtk_menu_new ();
  {
    GSList *group = NULL;
    static const gdouble opacities[] =
      { 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9 };
    static const gchar * const labels[] =
      { "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%" };
    GtkWidget *items[G_N_ELEMENTS (opacities)];
    GtkWidget *active_item = NULL;
    guint i;
    guint closest = 0;
    gdouble closest_diff = ABS (data->opacity - opacities[0]);

    for (i = 1; i < G_N_ELEMENTS (opacities); i++)
      {
	gdouble diff = ABS (data->opacity - opacities[i]);

	if (diff < closest_diff)
	  {
	    closest = i;
	    closest_diff = diff;
	  }
      }

    for (i = 0; i < G_N_ELEMENTS (opacities); i++)
      {
	GtkWidget *subitem = gtk_radio_menu_item_new_with_label (group, labels[i]);
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), subitem);
	gtk_widget_show (subitem);
	group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (subitem));
	g_object_set_data (G_OBJECT (subitem), "icon-opacity",
			   GINT_TO_POINTER ((gint) (opacities[i] * 100)));
	items[i] = subitem;
	if (i == closest)
	  active_item = subitem;
      }
    if (active_item)
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item), TRUE);
    for (i = 0; i < G_N_ELEMENTS (opacities); i++)
      g_signal_connect (items[i], "activate", G_CALLBACK (on_icon_opacity), data);
  }
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  /* --- floating icon size submenu --- */
  item = gtk_menu_item_new_with_label ("Floating Icon Size");
  submenu = gtk_menu_new ();
  {
    GSList *group = NULL;
    static const guint sizes[] = { 16, 32, 48, 64, 128, 256 };
    static const gchar * const labels[] =
      { "16x16", "32x32", "48x48", "64x64", "128x128", "256x256" };
    GtkWidget *items[G_N_ELEMENTS (sizes)];
    GtkWidget *active_item = NULL;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (sizes); i++)
      {
	GtkWidget *subitem = gtk_radio_menu_item_new_with_label (group, labels[i]);
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), subitem);
	gtk_widget_show (subitem);
	group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (subitem));
	g_object_set_data (G_OBJECT (subitem), "icon-size-value",
			   GUINT_TO_POINTER (sizes[i]));
	items[i] = subitem;
	if (data->icon_size == sizes[i])
	  active_item = subitem;
      }
    if (active_item)
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item), TRUE);
    for (i = 0; i < G_N_ELEMENTS (sizes); i++)
      g_signal_connect (items[i], "activate", G_CALLBACK (on_icon_size), data);
  }
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  /* --- undo / redo (above Change Font) --- */
  item = gtk_image_menu_item_new_with_label ("Undo");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("undo.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_undo), data);

  item = gtk_image_menu_item_new_with_label ("Redo");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("redo.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_redo), data);

}

/* The settings items, either in a submenu of the floating icon menu or
 * directly in the tray menu. */
static void
append_settings_items (ScreenatorData *data, GtkWidget *shell)
{
  GtkWidget *item;

  item = gtk_check_menu_item_new_with_label ("Save annotations on exit");
  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item),
				  data->save_annotations_on_exit);
  gtk_menu_shell_append (GTK_MENU_SHELL (shell), item);
  gtk_widget_show (item);
  g_signal_connect (item, "activate",
		    G_CALLBACK (on_icon_toggle_save_annotations), data);

  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (shell), item);
  gtk_widget_show (item);

  item = gtk_image_menu_item_new_with_label ("Change Font...");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("font-settings.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (shell), item);
  gtk_widget_show (item);
  g_signal_connect (item, "activate", G_CALLBACK (on_icon_change_font), data);

  item = gtk_check_menu_item_new_with_label ("Show Settings Indicator");
  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item),
				  data->show_settings_indicator);
  /* The indicator sits next to the icon, so it can only be shown with it. */
  gtk_widget_set_sensitive (item, data->show_floating_icon);
  gtk_menu_shell_append (GTK_MENU_SHELL (shell), item);
  gtk_widget_show (item);
  g_signal_connect (item, "activate",
		    G_CALLBACK (on_icon_toggle_settings_indicator), data);

  item = gtk_image_menu_item_new_with_label ("Floating Icon Position...");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("screenator.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (shell), item);
  gtk_widget_show (item);
  g_signal_connect (item, "activate",
		    G_CALLBACK (on_icon_position), data);

  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (shell), item);
  gtk_widget_show (item);

  item = gtk_image_menu_item_new_with_label ("Reset Settings");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("reset-settings.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (shell), item);
  gtk_widget_show (item);
  g_signal_connect (item, "activate",
		    G_CALLBACK (on_icon_reset_settings), data);
}

/* A menu that appears on the right half of the screen opens its submenus to
 * the left, towards the centre, so they stay fully visible.  The submenu
 * direction is set when the menu is mapped: at build time the tray menu has no
 * position yet, which made the first one open towards the screen edge. */
static void
set_settings_submenu_direction (GtkWidget *menu, GtkWidget *submenu)
{
  GdkWindow *window = gtk_widget_get_window (menu);
  GdkScreen *screen = gdk_screen_get_default ();
  GtkTextDirection direction = GTK_TEXT_DIR_LTR;
  GList *children;
  gint x = 0, y = 0;

  if (!submenu)
    return;

  if (window)
    gdk_window_get_origin (window, &x, &y);

  if (screen)
    {
      GdkRectangle geometry;
      gint monitor = gdk_screen_get_monitor_at_point (screen, x, y);

      gdk_screen_get_monitor_geometry (screen, monitor, &geometry);
      if (x > geometry.x + geometry.width / 2)
	direction = GTK_TEXT_DIR_RTL;
    }

  gtk_widget_set_direction (submenu, direction);
  for (children = gtk_container_get_children (GTK_CONTAINER (submenu));
       children; children = children->next)
    gtk_widget_set_direction (GTK_WIDGET (children->data),
			      GTK_TEXT_DIR_LTR);
  g_list_free (children);
}

/* The submenu is shown just before GTK places it, and the parent menu already
 * has its final position by then.  Deciding the direction here works for the
 * first tray menu as well. */
static void
on_settings_submenu_show (GtkWidget *submenu, gpointer user_data)
{
  set_settings_submenu_direction (GTK_WIDGET (user_data), submenu);
}

static gboolean
set_settings_direction_idle (gpointer user_data)
{
  GtkWidget *menu = GTK_WIDGET (user_data);
  GtkWidget *submenu = g_object_get_data (G_OBJECT (menu),
					  "screenator-settings-submenu");

  set_settings_submenu_direction (menu, submenu);
  return G_SOURCE_REMOVE;
}

static gboolean
on_menu_map (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
  GtkWidget *submenu = g_object_get_data (G_OBJECT (widget),
					  "screenator-settings-submenu");

  set_settings_submenu_direction (widget, submenu);

  /* Some menus are mapped before their final position is known.  Repeat the
   * choice once the position has settled, which is still before the user can
   * open the submenu. */
  g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, set_settings_direction_idle,
		   g_object_ref (widget), g_object_unref);
  return FALSE;
}

GtkWidget *build_icon_menu (ScreenatorData *data)
{
  GtkWidget *menu = gtk_menu_new ();
  GtkWidget *item;
  GtkWidget *submenu;

  append_icon_tool_items (data, menu);

  item = gtk_image_menu_item_new_with_label ("Clear Screen");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("trash.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_clear), data);

  item = gtk_image_menu_item_new_with_label ("Toggle Visibility");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("hide.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_toggle_vis), data);

  {
    gchar *label;

    if (data->hot_keyval && *data->hot_keyval &&
	g_ascii_strcasecmp (data->hot_keyval, "none") != 0)
      label = g_strdup_printf ("Show Floating Icon (%s)", data->hot_keyval);
    else
      label = g_strdup ("Show Floating Icon");

    item = gtk_check_menu_item_new_with_label (label);
    g_free (label);
  }
  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item),
				  data->show_floating_icon);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate",
		    G_CALLBACK (on_icon_toggle_floating), data);

  item = gtk_image_menu_item_new_with_label ("Show Hints");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("hint.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_show_hints), data);

  item = gtk_menu_item_new_with_label ("Settings");
  submenu = gtk_menu_new ();
  append_settings_items (data, submenu);

  /* The direction is chosen when the menu is mapped, from where it actually
   * appears on the screen, and the items always keep their normal
   * left-to-right layout. */
  {
    GList *children;

    gtk_widget_set_direction (submenu, GTK_TEXT_DIR_LTR);
    for (children = gtk_container_get_children (GTK_CONTAINER (submenu));
	 children; children = children->next)
      gtk_widget_set_direction (GTK_WIDGET (children->data),
				GTK_TEXT_DIR_LTR);
    g_list_free (children);
  }

  g_object_set_data (G_OBJECT (menu), "screenator-settings-submenu",
		     submenu);
  g_signal_connect (menu, "map-event", G_CALLBACK (on_menu_map), NULL);
  g_signal_connect (submenu, "show", G_CALLBACK (on_settings_submenu_show),
		    menu);

  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  {
    GtkWidget *sep = gtk_separator_menu_item_new ();
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), sep);
  }

  item = gtk_menu_item_new_with_label ("About");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_about), data);

  item = gtk_menu_item_new_with_label ("Quit");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (gtk_main_quit), NULL);

  return menu;
}

/* GTK refuses to pop up a menu when it cannot grab the keyboard and gives no
 * error: the menu simply stays hidden.  A virtual machine with automatic
 * keyboard capture holds the keyboard grab for as long as it has the focus,
 * which is exactly the case for the floating icon: it never takes the focus
 * itself, so the virtual machine never lets go.  Borrowing the focus for a
 * moment makes such an application release the keyboard, and the menu can be
 * shown afterwards.  The previous focus is restored when the menu is done. */
#ifdef GDK_WINDOWING_X11
static gboolean
icon_take_focus (ScreenatorData *data)
{
  GdkWindow *window;
  Display *display;
  Window focus = None;
  int revert = 0;

  if (data->icon_focus_taken || !GDK_IS_X11_DISPLAY (data->display))
    return FALSE;

  window = data->icon_win ? gtk_widget_get_window (data->icon_win) : NULL;
  if (!window)
    return FALSE;

  display = GDK_DISPLAY_XDISPLAY (data->display);
  XGetInputFocus (display, &focus, &revert);

  if (focus == GDK_WINDOW_XID (window))
    return FALSE;

  data->icon_prev_focus = (gulong) focus;
  data->icon_focus_taken = TRUE;

  gdk_x11_display_error_trap_push (data->display);
  XSetInputFocus (display, GDK_WINDOW_XID (window), RevertToParent,
		  CurrentTime);
  gdk_x11_display_error_trap_pop_ignored (data->display);

  return TRUE;
}

static void
icon_restore_focus (ScreenatorData *data)
{
  GdkWindow *window;
  Display *display;
  Window focus;
  Window current = None;
  int revert = 0;

  if (!data->icon_focus_taken)
    return;

  data->icon_focus_taken = FALSE;

  if (!GDK_IS_X11_DISPLAY (data->display))
    return;

  display = GDK_DISPLAY_XDISPLAY (data->display);
  focus = (Window) data->icon_prev_focus;
  data->icon_prev_focus = None;

  /* Only give the focus back if the icon still has it.  A dialog opened from
   * the menu may have taken it over in the meantime. */
  window = data->icon_win ? gtk_widget_get_window (data->icon_win) : NULL;
  if (!window)
    return;

  gdk_x11_display_error_trap_push (data->display);
  XGetInputFocus (display, &current, &revert);
  if (current == GDK_WINDOW_XID (window))
    {
      if (focus == None)
	XSetInputFocus (display, None, RevertToNone, CurrentTime);
      else if (focus == PointerRoot)
	XSetInputFocus (display, PointerRoot, RevertToPointerRoot, CurrentTime);
      else
	XSetInputFocus (display, focus, RevertToParent, CurrentTime);
    }
  gdk_x11_display_error_trap_pop_ignored (data->display);
}
#else
static gboolean
icon_take_focus (ScreenatorData *data)
{
  return FALSE;
}

static void
icon_restore_focus (ScreenatorData *data)
{
}
#endif

/* Free the popup menu once it is dismissed.  The flag keeps a repeated
 * selection-done (a submenu chain) from destroying the menu twice. */
static void
on_icon_menu_done (GtkMenuShell *menu, gpointer user_data)
{
  if (g_object_get_data (G_OBJECT (menu), "screenator-menu-done"))
    return;

  g_object_set_data (G_OBJECT (menu), "screenator-menu-done",
		     GINT_TO_POINTER (1));
  gtk_widget_destroy (GTK_WIDGET (menu));

  /* The menu borrowed the focus when another application held the keyboard;
   * give it back. */
  icon_restore_focus ((ScreenatorData *) user_data);
}

/* How long to wait for the focus change to make the other application release
 * the keyboard, and how often to retry. */
#define ICON_MENU_RETRY_MS 25
#define ICON_MENU_RETRIES 12

/* Pop the menu up.  GTK leaves the menu hidden when it cannot grab the
 * keyboard, which is the only way to notice the failure. */
static gboolean
icon_menu_show (ScreenatorData *data, GtkWidget *menu)
{
  gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
		  icon_menu_position_func, data,
		  0, GDK_CURRENT_TIME);

  if (!gtk_widget_get_visible (menu))
    return FALSE;

  /* The annotation window covers the whole screen, so make sure the menu ends
   * up above it (and above the icon). */
  if (gtk_widget_get_window (menu))
    gdk_window_raise (gtk_widget_get_window (menu));

  return TRUE;
}

static void
quick_menu_position_func (GtkMenu *menu, gint *x, gint *y,
			  gboolean *push_in, gpointer user_data)
{
  ScreenatorData *data = user_data;
  GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (menu));
  GdkRectangle area;
  GtkRequisition req;
  gint monitor;

  if (data->icon_win)
    {
      gint icon_x, icon_y;

      gtk_window_get_position (GTK_WINDOW (data->icon_win),
			       &icon_x, &icon_y);
      monitor = gdk_screen_get_monitor_at_point (screen,
						 icon_x + data->icon_size / 2,
						 icon_y + data->icon_size / 2);
    }
  else
    monitor = gdk_screen_get_primary_monitor (screen);

  if (monitor < 0)
    monitor = 0;

  gdk_screen_get_monitor_workarea (screen, monitor, &area);
  gtk_widget_get_preferred_size (GTK_WIDGET (menu), NULL, &req);

  *x = area.x + MAX (0, (area.width - req.width) / 2);
  *y = area.y + MAX (0, (area.height - req.height) / 2);
  *push_in = TRUE;
}

static void
quick_menu_done (GtkMenuShell *menu, gpointer user_data)
{
  ScreenatorData *data = user_data;
  gboolean selected = data->quick_menu_select;
  gboolean restore_grab = data->quick_menu_was_grabbed && !selected;
  gint pen_tool = data->pending_pen_tool;
  gint shape_type = data->pending_shape_type;

  if (data->quick_menu == GTK_WIDGET (menu))
    data->quick_menu = NULL;

  data->quick_menu_select = FALSE;
  data->quick_menu_was_grabbed = FALSE;

  gtk_widget_destroy (GTK_WIDGET (menu));

  if (selected)
    activate_tool_now (data, pen_tool, shape_type);
  else if (restore_grab)
    acquire_grab (data, NULL);
}

/* F10 quick tool menu: every tool in one flat list with its icon.  GTK menu
 * keyboard navigation provides the arrow keys and Enter. */
void
screenator_show_quick_menu (ScreenatorData *data)
{
  static const struct
  {
    const gchar *label;
    const gchar *icon;
    GCallback    callback;
  } tools[] =
    {
      { "Pen",               "pen.png",              G_CALLBACK (on_icon_pen) },
      { "Text",              "text.png",             G_CALLBACK (on_icon_text) },
      { "Eraser",            "eraser.png",           G_CALLBACK (on_icon_eraser) },
      { "Marker",            "marker.png",           G_CALLBACK (on_icon_marker) },
      { "Line",              "free-line.png",        G_CALLBACK (on_icon_line) },
      { "Spline",            "spline.png",           G_CALLBACK (on_icon_spline) },
      { "Polygon",           "polygon.png",          G_CALLBACK (on_icon_polygon) },
      { "Triangle",          "triangle.png",         G_CALLBACK (on_icon_triangle) },
      { "Arrow",             "arrow.png",            G_CALLBACK (on_icon_arrow) },
      { "Ellipse",           "ellipse.png",          G_CALLBACK (on_icon_ellipse) },
      { "Rounded Rectangle", "rounded-rectangle.png", G_CALLBACK (on_icon_rounded_rectangle) },
      { "Paint Bucket",      "bucket.png",           G_CALLBACK (on_icon_bucket) },
      { "Math Function",     "math.png",             G_CALLBACK (on_icon_math) },
      { "Parallel Lines",    "bars.png",             G_CALLBACK (on_icon_bars) },
      { "Random Signal",     "random-signal.png",    G_CALLBACK (on_icon_random) }
    };
  GtkWidget *menu, *item, *heading;
  guint i;

  if (data->top_dialog)
    return;

  if (data->quick_menu)
    return;

  /* The quick menu is an immediate selection, so drop any pending delayed
   * activation or blocked-grab fallback first. */
  if (data->tool_delay_id)
    {
      g_source_remove (data->tool_delay_id);
      data->tool_delay_id = 0;
    }
  if (data->tool_grab_retry_id)
    {
      g_source_remove (data->tool_grab_retry_id);
      data->tool_grab_retry_id = 0;
    }
  screenator_raw_input_disable (data);

  data->quick_menu_select = FALSE;
  data->quick_menu_was_grabbed = screenator_any_grabbed (data);
  if (data->quick_menu_was_grabbed)
    release_grab (data, NULL);

  menu = gtk_menu_new ();
  heading = stylus_menu_heading ("Quick Tools");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), heading);

  for (i = 0; i < G_N_ELEMENTS (tools); i++)
    {
      item = gtk_image_menu_item_new_with_label (tools[i].label);
      gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
				     gtk_image_new_from_pixbuf (
				       load_menu_icon (tools[i].icon)));
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
      g_signal_connect (item, "activate", tools[i].callback, data);
    }

  gtk_widget_show_all (menu);
  data->quick_menu = menu;
  g_signal_connect (menu, "selection-done",
		    G_CALLBACK (quick_menu_done), data);

  gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
		  quick_menu_position_func, data, 0, GDK_CURRENT_TIME);

  if (!gtk_widget_get_visible (menu))
    {
      gboolean restore_grab = data->quick_menu_was_grabbed;

      data->quick_menu = NULL;
      data->quick_menu_was_grabbed = FALSE;
      data->quick_menu_select = FALSE;
      g_signal_handlers_disconnect_by_data (menu, data);
      gtk_widget_destroy (menu);
      if (restore_grab)
	acquire_grab (data, NULL);
      return;
    }

  if (gtk_widget_get_window (menu))
    gdk_window_raise (gtk_widget_get_window (menu));

  /* Start keyboard navigation on the first selectable tool. */
  gtk_menu_shell_select_first (GTK_MENU_SHELL (menu), FALSE);
}

static gboolean
icon_menu_retry (gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if (icon_menu_show (data, data->icon_menu_pending))
    {
      data->icon_menu_pending = NULL;
      data->icon_menu_retry_id = 0;
      return G_SOURCE_REMOVE;
    }

  if (++data->icon_menu_retry_count >= ICON_MENU_RETRIES)
    {
      GtkWidget *menu = data->icon_menu_pending;

      data->icon_menu_pending = NULL;
      data->icon_menu_retry_id = 0;
      gtk_widget_destroy (menu);
      icon_restore_focus (data);
      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

/* Drop a menu that is still waiting for the keyboard and hand the focus back. */
static void
icon_cancel_menu_retry (ScreenatorData *data)
{
  if (data->icon_menu_retry_id)
    {
      g_source_remove (data->icon_menu_retry_id);
      data->icon_menu_retry_id = 0;
    }

  if (data->icon_menu_pending)
    {
      gtk_widget_destroy (data->icon_menu_pending);
      data->icon_menu_pending = NULL;
    }

  icon_restore_focus (data);
}

void popup_icon_menu (ScreenatorData *data)
{
  GtkWidget *menu = build_icon_menu (data);
  GtkRequisition req;
  GList     *l;
  gint      ix, iy, mx, my;

  if (!data->icon_win)
    {
      gtk_widget_destroy (menu);
      return;
    }

  /* position the menu below the icon so it does not cover it */
  gtk_window_get_position (GTK_WINDOW (data->icon_win), &ix, &iy);

  for (l = gtk_container_get_children (GTK_CONTAINER (menu)); l; l = l->next)
    gtk_widget_show (GTK_WIDGET (l->data));
  g_list_free (gtk_container_get_children (GTK_CONTAINER (menu)));

  /* Below the icon by default; flip above it and stay inside the screen when
   * the icon sits at the bottom or the right edge. */
  gtk_widget_get_preferred_size (menu, NULL, &req);

  mx = ix;
  my = iy + (gint) data->icon_size + 6;

  if (my + req.height > (gint) data->height)
    my = iy - req.height - 6;
  if (my < 0)
    my = 0;
  if (mx + req.width > (gint) data->width)
    mx = (gint) data->width - req.width;
  if (mx < 0)
    mx = 0;

  data->icon_menu_x = mx;
  data->icon_menu_y = my;

  /* The popup is a separate window; free it when it is dismissed. */
  g_signal_connect (menu, "selection-done",
		    G_CALLBACK (on_icon_menu_done), data);

  if (icon_menu_show (data, menu))
    return;

  /* Some other application holds the keyboard grab, so GTK refused to show
   * the menu.  Borrow the focus until it lets go, then try again. */
  if (!icon_take_focus (data))
    {
      gtk_widget_destroy (menu);
      return;
    }

  data->icon_menu_pending = menu;
  data->icon_menu_retry_count = 0;
  data->icon_menu_retry_id = g_timeout_add (ICON_MENU_RETRY_MS,
					    icon_menu_retry, data);
}

/* How far the pointer has to move before a click on the icon becomes a drag.
 * A small threshold keeps a slightly shaky click from moving the icon and
 * cancelling the menu. */
#define ICON_DRAG_THRESHOLD 4

/* Movement beyond this is not a drag but a pointer warp (a desktop tool or a
 * virtual machine moving the pointer).  Following it would throw the icon
 * across the screen and lose the click. */
static gint
icon_drag_limit (ScreenatorData *data)
{
  return MAX ((gint) data->icon_size * 2, 64);
}

/* Hold the pointer while a button is down on the icon.
 *
 * GTK 3 does not grab the pointer implicitly, so a release that happens
 * outside the small icon window is delivered to whatever window is below it
 * and the menu never opens.  Keeping the pointer grabbed makes the click
 * survive a small movement or a pointer warp.  The grab is best effort: a
 * compositor may refuse it, in which case the icon behaves as before. */
static void
icon_grab_pointer (ScreenatorData *data)
{
  GdkWindow *window;
  GdkSeat *seat;

  if (data->icon_pointer_grabbed || !data->icon_win)
    return;

  window = gtk_widget_get_window (data->icon_win);
  if (!window)
    return;

  seat = gdk_display_get_default_seat (gdk_window_get_display (window));
  if (!seat)
    return;

  if (gdk_seat_grab (seat, window, GDK_SEAT_CAPABILITY_POINTER, FALSE,
		     NULL, NULL, NULL, NULL) == GDK_GRAB_SUCCESS)
    data->icon_pointer_grabbed = TRUE;
}

static void
icon_ungrab_pointer (ScreenatorData *data)
{
  GdkWindow *window;
  GdkDisplay *display;
  GdkSeat *seat;

  if (!data->icon_pointer_grabbed)
    return;

  data->icon_pointer_grabbed = FALSE;

  window = data->icon_win ? gtk_widget_get_window (data->icon_win) : NULL;
  display = window ? gdk_window_get_display (window)
		   : gdk_display_get_default ();

  seat = display ? gdk_display_get_default_seat (display) : NULL;
  if (seat)
    gdk_seat_ungrab (seat);
}

/* Another client grabbed the pointer; the click cannot continue. */
static gboolean
on_icon_grab_broken (GtkWidget *widget, GdkEventGrabBroken *event,
		     gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  data->icon_pointer_grabbed = FALSE;
  data->icon_dragging = FALSE;
  data->icon_drag_start_x = -1;
  data->icon_drag_start_y = -1;

  return FALSE;
}

gboolean on_icon_button_press (GtkWidget   *win,
			       GdkEventButton *ev,
			       gpointer   user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  gint wx, wy;

  /* Both left and right click open the icon menu; dragging with either
   * button moves the icon. */
  if (ev->button != 1 && ev->button != 3)
    return FALSE;

  /* A press that arrives while a previous click is still open (its release
   * was lost) starts over. */
  icon_cancel_menu_retry (data);
  icon_ungrab_pointer (data);

  data->icon_dragging = FALSE;
  gtk_window_get_position (GTK_WINDOW (win), &wx, &wy);
  data->icon_drag_start_x = ev->x_root - wx;
  data->icon_drag_start_y = ev->y_root - wy;
  data->icon_press_x = ev->x_root;
  data->icon_press_y = ev->y_root;

  /* Keep the icon above the annotations and make sure the release comes back
   * to it, even when the pointer moves a little or is warped away. */
  raise_icon_window (data);
  icon_grab_pointer (data);

  return TRUE;
}

gboolean on_icon_motion (GtkWidget    *win,
			 GdkEventMotion *ev,
			 gpointer   user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  gint dx, dy, limit;

  if (!(ev->state & (GDK_BUTTON1_MASK | GDK_BUTTON3_MASK)))
    return FALSE;

  if (data->icon_drag_start_x < 0 || data->icon_drag_start_y < 0)
    return FALSE;

  dx = ev->x_root - data->icon_press_x;
  dy = ev->y_root - data->icon_press_y;

  if (!data->icon_dragging)
    {
      /* A jump that is too large to be a drag is a pointer warp.  Leave the
       * icon where it is and keep the click alive. */
      limit = icon_drag_limit (data);
      if (ABS (dx) > limit || ABS (dy) > limit)
	return TRUE;

      if (ABS (dx) < ICON_DRAG_THRESHOLD && ABS (dy) < ICON_DRAG_THRESHOLD)
	return TRUE;

      /* The offset was recorded on the press and must not change here: it is
       * the distance from the icon corner to the pointer, and using the
       * current pointer position instead would cancel the movement. */
      data->icon_dragging = TRUE;
    }

  {
    gint x = ev->x_root - data->icon_drag_start_x;
    gint y = ev->y_root - data->icon_drag_start_y;
    gint max_x = (gint) data->width - (gint) data->icon_size;
    gint max_y = (gint) data->height - (gint) data->icon_size;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > max_x) x = MAX (0, max_x);
    if (y > max_y) y = MAX (0, max_y);
    gtk_window_move (GTK_WINDOW (win), x, y);
    position_settings_indicator (data);
    raise_icon_window (data);
  }

  return TRUE;
}

gboolean on_icon_button_release (GtkWidget   *win,
				 GdkEventButton *ev,
				 gpointer   user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  gboolean clicked;

  clicked = (ev->button == 1 || ev->button == 3) && !data->icon_dragging;

  /* Release the pointer before the menu grabs it. */
  icon_ungrab_pointer (data);

  if (clicked)
    popup_icon_menu (data);

  data->icon_dragging = FALSE;
  data->icon_drag_start_x = -1;
  data->icon_drag_start_y = -1;

  return TRUE;
}

/* A coloured section heading for the hints dialog. */
static GtkWidget *
hints_heading (const gchar *color, const gchar *text)
{
  GtkWidget *label = gtk_label_new (NULL);
  gchar *markup = g_markup_printf_escaped (
    "<span foreground=\"%s\" weight=\"bold\" size=\"large\">%s</span>",
    color, text);

  gtk_label_set_markup (GTK_LABEL (label), markup);
  g_free (markup);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_widget_set_margin_top (label, 8);
  return label;
}

/* One bullet line, with the bullet in the section's colour.  The line may
 * contain its own markup for emphasis. */
static GtkWidget *
hints_bullet (const gchar *color, const gchar *markup)
{
  GtkWidget *label = gtk_label_new (NULL);
  gchar *full = g_strdup_printf (
    "<span foreground=\"%s\">•</span> %s", color, markup);

  gtk_label_set_markup (GTK_LABEL (label), full);
  g_free (full);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 90);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  return label;
}

/* Make the hints scrollbar easy to see: a solid trough and a wide, coloured
 * slider instead of the thin overlay indicator. */
static void
install_hints_scroll_style (GtkWidget *scroll)
{
  static GtkCssProvider *provider = NULL;
  GtkStyleContext *context;

  if (!provider)
    {
      provider = gtk_css_provider_new ();
      gtk_css_provider_load_from_data (provider,
	".hints-scroll scrollbar { background-color: #ececec; }\n"
	".hints-scroll scrollbar trough { min-width: 18px; "
	"background-color: #d8d8d8; border: 1px solid #a0a0a0; "
	"border-radius: 9px; }\n"
	".hints-scroll scrollbar slider { min-width: 14px; "
	"min-height: 48px; background-color: #3d6fa5; "
	"border: 1px solid #244b73; border-radius: 7px; }\n"
	".hints-scroll scrollbar slider:hover { background-color: #2f5b8a; }\n"
	".hints-scroll scrollbar slider:active { background-color: #1f4063; }\n",
	-1, NULL);
      gtk_style_context_add_provider_for_screen (
	gtk_widget_get_screen (scroll),
	GTK_STYLE_PROVIDER (provider),
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

  context = gtk_widget_get_style_context (scroll);
  gtk_style_context_add_class (context, "hints-scroll");
}

/* Opening the hints must always show their beginning rather than a scrolled
 * position. */
static gboolean
hints_scroll_to_top (gpointer user_data)
{
  GtkWidget *scroll = user_data;
  GtkAdjustment *adjustment =
    gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroll));

  if (adjustment)
    gtk_adjustment_set_value (adjustment,
			      gtk_adjustment_get_lower (adjustment));

  g_object_unref (scroll);
  return G_SOURCE_REMOVE;
}

/* The single, colourful hints dialog.  It replaces the per-tool messages and
 * states the common controls once. */
static void
show_hints_dialog (ScreenatorData *data, gboolean startup)
{
  GtkWidget *dialog, *content, *box, *column;
  GtkWidget *label, *check, *scroll;
  gboolean was_grabbed;
  gboolean changed = FALSE;
  gboolean restore_window = FALSE;

  if (data->hints_id)
    {
      g_source_remove (data->hints_id);
      data->hints_id = 0;
    }

  data->hints_shown = TRUE;

  /* A modal dialog cannot be used while the pointer is grabbed. */
  was_grabbed = screenator_any_grabbed (data);
  if (was_grabbed)
    release_grab (data, NULL);

  /* A managed startup hint would otherwise sit below the override-redirect
   * annotation window (and its saved paper or drawings). */
  if (startup && data->win && gtk_widget_get_visible (data->win))
    {
      hide_window (data);
      restore_window = TRUE;
    }

  dialog = gtk_dialog_new_with_buttons ("Screenator Hints", NULL,
					GTK_DIALOG_MODAL |
					GTK_DIALOG_DESTROY_WITH_PARENT,
					"Close", GTK_RESPONSE_CLOSE,
					NULL);
  gtk_window_set_keep_above (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);

  /* At startup the hints are a managed window; from the menu they stay above
   * the drawings like the other dialogs. */
  if (startup)
    show_dialog_common (data, dialog, FALSE);
  else
    show_dialog_above_drawings (data, dialog);

  content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
  gtk_container_set_border_width (GTK_CONTAINER (box), 14);

  scroll = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
				  GTK_POLICY_NEVER,
				  GTK_POLICY_ALWAYS);
  gtk_scrolled_window_set_overlay_scrolling (GTK_SCROLLED_WINDOW (scroll),
					     FALSE);
  install_hints_scroll_style (scroll);
  gtk_widget_set_hexpand (scroll, TRUE);
  gtk_widget_set_vexpand (scroll, TRUE);
  gtk_widget_set_hexpand (box, TRUE);
  gtk_widget_set_vexpand (box, TRUE);

  /* A non-resizable window sizes itself from its content, so request the
   * intended size explicitly. */
  gtk_widget_set_size_request (scroll, 750, 550);
  gtk_window_set_default_size (GTK_WINDOW (dialog), 750, 550);

  gtk_container_add (GTK_CONTAINER (content), scroll);
  gtk_container_add (GTK_CONTAINER (scroll), box);

  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label),
			"<span foreground=\"#2c3e50\" weight=\"bold\" "
			"size=\"xx-large\">Screenator Hints</span>\n"
			"<span foreground=\"#7f8c8d\">The important "
			"controls at a glance.</span>");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  /* One column inside the scrollable area. */
  column = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
  gtk_box_pack_start (GTK_BOX (box), column, FALSE, FALSE, 4);

  /* Mouse and stylus. */
  gtk_box_pack_start (GTK_BOX (column),
		      hints_heading ("#c0392b", "Mouse"), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#c0392b",
    "<span foreground=\"#d35400\" weight=\"bold\">Long right click</span> "
    "opens the Scroll Mode menu and chooses what the <b>mouse wheel</b> "
    "changes: <b>Color</b> or <b>Thickness</b>."), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#c0392b",
    "Left click and drag to draw with the selected tool."), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#c0392b",
    "Middle click and drag to erase."), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#c0392b",
    "With the Eraser tool, the wheel changes the eraser size."),
    FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#c0392b",
    "Short right click finishes the shape or exits the tool."),
    FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (column),
		      hints_heading ("#8e44ad", "Stylus"), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#8e44ad",
    "<span foreground=\"#c0392b\" weight=\"bold\">IMPORTANT:</span> In your "
    "stylus configuration, set button 1 (near the tip) to <b>Middle Click</b> "
    "and button 2 to <b>Right Click</b>."), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#8e44ad",
    "<span foreground=\"#d35400\" weight=\"bold\">Long press side button "
    "1</span> activates the eraser while it is held, even while "
    "hovering."), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#8e44ad",
    "<span foreground=\"#d35400\" weight=\"bold\">Long press side button "
    "2</span> opens the Scroll Mode menu and chooses what scrolling "
    "changes: <b>Color</b> or <b>Thickness</b>."), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#8e44ad",
    "Short press side button 1: step through the selected "
    "<b>Color</b> or <b>Thickness</b>, or the eraser size with the "
    "Eraser tool."), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#8e44ad",
    "Short click side button 2: finish the shape or exit the tool. With "
    "the Eraser tool, a long press does not open the menu."),
    FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (column),
		      hints_heading ("#d35400", "Global Shortcut"),
		      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#d35400",
    "<span foreground=\"#d35400\" weight=\"bold\">F9</span> shows or "
    "hides the floating icon at any time."), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#d35400",
    "<span foreground=\"#d35400\" weight=\"bold\">F8</span> selects the "
    "Pen, <span foreground=\"#d35400\" weight=\"bold\">F6</span> the "
    "Arrow and <span foreground=\"#d35400\" weight=\"bold\">F7</span> the "
    "Text tool."), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#d35400",
    "<span foreground=\"#d35400\" weight=\"bold\">F10</span> opens the "
    "centered quick tool menu with all tools."), FALSE, FALSE, 0);

  /* Shape editing, tool tips and shortcuts. */
  gtk_box_pack_start (GTK_BOX (column),
		      hints_heading ("#2980b9", "Shape Editing"),
		      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#2980b9",
    "Hold <b>Ctrl</b> while rotating a shape to snap to 0, 30, 45, 60, "
    "90 degrees, and so on."), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#2980b9",
    "Spline: hold <b>Ctrl</b> while dragging a handle to move each control "
    "point separately."), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#2980b9",
    "Polygon: the first click sets the centre; drag to set the radius and "
    "rotation."), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#2980b9",
    "Triangle: each click places the next vertex; lines follow the pointer, "
    "even while dragging. Dragging does not set a point; the next click does."),
    FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (column),
		      hints_heading ("#e67e22", "Text Tool"),
		      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#e67e22",
    "After typing, press <b>Enter</b> to accept the text and re-enable the "
    "mouse."), FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (column),
		      hints_heading ("#b9770e", "Marker Tool"),
		      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#b9770e",
    "The wheel or stylus side button 1 changes its color."), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#b9770e",
    "<span foreground=\"#d35400\" weight=\"bold\">Long right click</span> "
    "or <span foreground=\"#d35400\" weight=\"bold\">Long press side "
    "button 2</span> opens the Marker Opacity menu (10% to 90% in steps of "
    "10%)."), FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (column),
		      hints_heading ("#16a085", "Drawing Mode Shortcuts"),
		      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#16a085",
    "<b>Ctrl+Z</b> undo, <b>Ctrl+Shift+Z</b> redo."),
    FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#16a085",
    "<b>Escape</b> finishes the shape or exits the tool."),
    FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (column),
		      hints_heading ("#2980b9", "Screen Paper Shortcuts"),
		      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#2980b9",
    "<b>Ctrl+Z</b> undo, <b>Ctrl+Shift+Z</b> redo."),
    FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#2980b9",
    "<b>Ctrl+T</b> shows or hides the drawings."), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#2980b9",
    "<b>Ctrl+Q</b> exits the screen paper."), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (column), hints_bullet ("#2980b9",
    "<b>Alt+F4</b> quits."), FALSE, FALSE, 0);

  check = gtk_check_button_new_with_label ("Never show hints again");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), data->hide_hints);
  {
    GtkWidget *action_area;
    GtkWidget *close_button;

    /* A check button must not be added with gtk_dialog_add_action_widget(),
     * because that would close the dialog when it is toggled.  Pack it into
     * the action area directly, then put the check at the left edge and the
     * Close button at the right edge. */
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    action_area = gtk_dialog_get_action_area (GTK_DIALOG (dialog));
    G_GNUC_END_IGNORE_DEPRECATIONS
    close_button =
      gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog),
					  GTK_RESPONSE_CLOSE);

    gtk_box_pack_start (GTK_BOX (action_area), check, FALSE, FALSE, 0);
    gtk_box_reorder_child (GTK_BOX (action_area), check, 0);
    if (close_button)
      gtk_box_reorder_child (GTK_BOX (action_area), close_button, 1);

    gtk_button_box_set_layout (GTK_BUTTON_BOX (action_area),
			       GTK_BUTTONBOX_EDGE);
  }

  if (startup)
    data->hints_dialog = dialog;

  gtk_widget_show_all (dialog);
  {
    GtkAdjustment *adjustment =
      gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroll));
    GtkWidget *close_button =
      gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog),
					  GTK_RESPONSE_CLOSE);

    /* Keep the focus outside the scroll area. */
    if (close_button)
      gtk_widget_grab_focus (close_button);

    if (adjustment)
      gtk_adjustment_set_value (adjustment,
				gtk_adjustment_get_lower (adjustment));

    /* Focus is assigned after the dialog is shown; put the viewport back at
     * the top afterwards. */
    g_timeout_add (50, hints_scroll_to_top, g_object_ref (scroll));
  }
  gtk_dialog_run (GTK_DIALOG (dialog));

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check)) !=
      data->hide_hints)
    {
      data->hide_hints =
	gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check));
      changed = TRUE;
    }

  data->hints_dialog = NULL;
  gtk_widget_destroy (dialog);

  if (changed)
    write_keyfile (data);

  if (restore_window)
    show_window (data);

  if (was_grabbed)
    acquire_grab (data, NULL);
}

static gboolean
show_hints_idle (gpointer user_data)
{
  show_hints_dialog ((ScreenatorData *) user_data, TRUE);
  return G_SOURCE_REMOVE;
}

/* Offer the hints once per run, after the input devices are set up. */
void
schedule_hints (ScreenatorData *data)
{
  if (data->hide_hints || data->hints_shown || data->hints_id)
    return;

  data->hints_id = g_idle_add (show_hints_idle, data);
}

/* Prepare the Marker when painting starts: save the current line colour and
 * switch to yellow.  Does nothing if the Marker is not selected or its colour
 * is already prepared. */
void
screenator_marker_prepare (ScreenatorData *data)
{
  if (data->pen_tool != 11 || data->marker_saved_color_valid)
    return;

  data->marker_saved_color = data->pen_color;
  data->marker_saved_color_valid = TRUE;
  data->pen_color.red = 1.0;
  data->pen_color.green = 1.0;
  data->pen_color.blue = 0.0;
  data->pen_color.alpha = 1.0;

  apply_pen_settings (data);
  update_settings_indicator (data);
  schedule_tray_menu_refresh (data);
}

/* Leaving the Marker restores the line colour that was active before it. */
void
screenator_marker_leave (ScreenatorData *data)
{
  if (data->pen_tool != 11 || !data->marker_saved_color_valid)
    return;

  data->pen_color = data->marker_saved_color;
  data->marker_saved_color_valid = FALSE;
  apply_pen_settings (data);
  update_settings_indicator (data);
  schedule_tray_menu_refresh (data);
}

static gboolean
tool_grab_retry (gpointer user_data)
{
  ScreenatorData *data = user_data;

  data->tool_grab_retry_id = 0;

  if (data->top_dialog || screenator_any_grabbed (data))
    {
      screenator_raw_input_disable (data);
      return G_SOURCE_REMOVE;
    }

  acquire_grab (data, NULL);

  if (!screenator_any_grabbed (data))
    data->tool_grab_retry_id = g_timeout_add (250, tool_grab_retry, data);

  return G_SOURCE_REMOVE;
}

/* A delayed tool activation can fire while another application holds a menu
 * or a pointer grab.  Try again until the grab becomes available. */
static void
ensure_tool_grab (ScreenatorData *data)
{
  if (data->tool_grab_retry_id)
    {
      g_source_remove (data->tool_grab_retry_id);
      data->tool_grab_retry_id = 0;
    }
  screenator_raw_input_disable (data);

  if (!data->top_dialog && !screenator_any_grabbed (data))
    {
      /* A menu or another application may hold the pointer grab.  Freeze the
       * current screen and use raw events so the selected tool can draw over
       * the frozen picture until the real grab becomes available. */
      screenator_take_screenshot_paper (data);
      screenator_raw_input_enable (data);
      data->tool_grab_retry_id = g_timeout_add (250, tool_grab_retry, data);
    }
  else
    screenator_raw_input_disable (data);
}

static void
activate_tool_now (ScreenatorData *data, gint pen_tool, gint shape_type)
{
  GHashTableIter it;
  gpointer value;
  ScreenatorDeviceData *devdata = NULL;

  text_editor_finish (data);

  if (pen_tool != 11)
    screenator_marker_leave (data);

  star_finish_all (data);
  triangle_finish_all (data);
  frame_finish_all (data);
  line_finish_all (data);
  spline_cancel_all (data);

  if (pen_tool == 1)
    {
      data->forced_context = data->default_pen;
      data->default_pen->arrowsize = 0;
    }
  else if (pen_tool == 3)
    data->forced_context = data->default_eraser;
  else if (pen_tool == 4)
    data->forced_context = data->default_text;
  else if (pen_tool == 11)
    data->forced_context = data->default_marker;
  else
    {
      data->forced_context = data->default_shape;
      data->shape_type = (ScreenatorShapeType) shape_type;
    }

  data->pen_tool = pen_tool;

  if (pen_tool == 11)
    screenator_marker_prepare (data);

  /* Reset tracked button state so select_tool() is re-invoked on the next
   * button press and the newly selected tool takes effect immediately. */
  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      devdata = value;
      devdata->state = 0;

      /* The device is already grabbed while a tool is active, so
       * acquire_grab() will skip it.  Update its context and cursor here so
       * the new tool's cursor appears immediately. */
      if (devdata->is_grabbed && data->forced_context)
	{
	  devdata->cur_context = data->forced_context;
	  set_device_cursor (data, devdata->device, devdata->cur_context);
	}
    }

  acquire_grab (data, NULL);
  ensure_tool_grab (data);
}

static gboolean
tool_activation_timeout (gpointer user_data)
{
  ScreenatorData *data = user_data;

  data->tool_delay_id = 0;
  activate_tool_now (data, data->pending_pen_tool, data->pending_shape_type);
  return G_SOURCE_REMOVE;
}

/* Apply a tool selection now, or after the configured delay. */
static void
schedule_tool_activation (ScreenatorData *data, gint pen_tool, gint shape_type)
{
  if (data->tool_delay_id)
    {
      g_source_remove (data->tool_delay_id);
      data->tool_delay_id = 0;
    }

  if (data->tool_grab_retry_id)
    {
      g_source_remove (data->tool_grab_retry_id);
      data->tool_grab_retry_id = 0;
    }

  if (data->quick_menu)
    {
      /* The menu is still closing; remember the choice and activate it from
       * quick_menu_done() without applying the activation delay. */
      data->quick_menu_select = TRUE;
      data->pending_pen_tool = pen_tool;
      data->pending_shape_type = shape_type;
      return;
    }

  if (data->tool_delay_seconds == 0)
    {
      activate_tool_now (data, pen_tool, shape_type);
      return;
    }

  data->pending_pen_tool = pen_tool;
  data->pending_shape_type = shape_type;
  data->tool_delay_id = g_timeout_add_seconds (data->tool_delay_seconds,
					       tool_activation_timeout, data);
}

void
screenator_schedule_tool (ScreenatorData *data, gint pen_tool, gint shape_type)
{
  /* Keyboard tool shortcuts switch immediately while a tool is already
   * active; the delay only applies when starting from the normal desktop. */
  if (screenator_any_grabbed (data) || data->raw_input_enabled)
    {
      if (data->tool_delay_id)
	{
	  g_source_remove (data->tool_delay_id);
	  data->tool_delay_id = 0;
	}
      if (data->tool_grab_retry_id)
	{
	  g_source_remove (data->tool_grab_retry_id);
	  data->tool_grab_retry_id = 0;
	}
      activate_tool_now (data, pen_tool, shape_type);
    }
  else
    schedule_tool_activation (data, pen_tool, shape_type);
}

void on_icon_pen (GtkMenuItem *menuitem,
		  gpointer     user_data)
{
  schedule_tool_activation ((ScreenatorData *) user_data, 1, -1);
}

/* --- Math Function dialog -------------------------------------------------
 *
 * The expression and the range are entered in a small dialog.  Rectangular
 * plots use y = f(x) over an x range; polar plots use r = f(theta) over a
 * theta range.  "pi" and "e" are the constants, and the expression is checked
 * before it is stored.  Each mode keeps its own expression and range.
 */

/* Copy the current expression and range into every editable math plot. */
static void
math_dialog_update_frames (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;

  if (!data->devdatatable)
    return;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      ScreenatorDeviceData *devdata = value;

      if (devdata->frame_active &&
	  devdata->frame_shape.type == SCREENATOR_SHAPE_MATH)
	{
	  GdkRectangle bounds;
	  gint pad = (gint) data->pen_width * 2 + 64;

	  frame_shape_get_bounds (&devdata->frame_shape, pad, &bounds);

	  g_free (devdata->frame_shape.expression);
	  devdata->frame_shape.polar = data->math_polar;
	  if (data->math_polar)
	    {
	      devdata->frame_shape.expression =
		g_strdup (data->math_polar_expression);
	      devdata->frame_shape.xmin = data->math_theta_min;
	      devdata->frame_shape.xmax = data->math_theta_max;
	    }
	  else
	    {
	      devdata->frame_shape.expression =
		g_strdup (data->math_expression);
	      devdata->frame_shape.xmin = data->math_xmin;
	      devdata->frame_shape.xmax = data->math_xmax;
	    }

	  if (data->win)
	    gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
					&bounds, 0);
	}
    }
}

/* Widgets shared with the coordinate-system toggle. */
typedef struct
{
  GtkWidget *rect_grid;
  GtkWidget *polar_grid;
  GtkWidget *help;
  GtkWidget *rect_entry;
  GtkWidget *polar_entry;
  GtkWidget *preset_combo;
  gboolean   polar;
  gboolean   updating;
} MathDialogWidgets;

/* Ready-made functions offered by the preset drop-down.  The polar variant
 * uses theta in place of x. */
typedef struct
{
  const gchar *rect;
  const gchar *polar;
} MathPreset;

static const MathPreset math_presets[] =
{
  { "cos(2*pi*x)", "cos(2*pi*theta)" },
  { "sin(2*pi*x)", "sin(2*pi*theta)" },
  { "unit(x)", "unit(theta)" },
  { "delta(x)", "delta(theta)" },
  { "rect(x)", "rect(theta)" },
  { "e^(x)", "e^(theta)" },
  { "log(x)", "log(theta)" },
  { "1/x", "1/theta" },
  { "sin(2*pi*x)/(2*pi*x)", "sin(2*pi*theta)/(2*pi*theta)" },
  { "(cos(2*pi*x)+2)*cos(20*pi*x)", "(cos(2*pi*theta)+2)*cos(20*pi*theta)" }
};

/* Fill the preset drop-down with the expressions for the active mode. */
static void
math_dialog_fill_presets (MathDialogWidgets *widgets)
{
  guint i;

  widgets->updating = TRUE;
  gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (widgets->preset_combo));

  for (i = 0; i < G_N_ELEMENTS (math_presets); i++)
    {
      const gchar *text = widgets->polar
	? math_presets[i].polar : math_presets[i].rect;

      gtk_combo_box_text_append_text (
	GTK_COMBO_BOX_TEXT (widgets->preset_combo), text);
    }

  gtk_combo_box_set_active (GTK_COMBO_BOX (widgets->preset_combo), -1);
  widgets->updating = FALSE;
}

/* Put the chosen preset into the active expression entry. */
static void
math_dialog_preset_changed (GtkComboBox *combo, gpointer user_data)
{
  MathDialogWidgets *widgets = user_data;
  GtkWidget *entry;
  gchar *text;

  if (widgets->updating)
    return;

  text = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (combo));

  if (!text)
    return;

  entry = widgets->polar ? widgets->polar_entry : widgets->rect_entry;
  gtk_entry_set_text (GTK_ENTRY (entry), text);
  gtk_editable_set_position (GTK_EDITABLE (entry), -1);
  g_free (text);

  /* Clear the choice so the same preset can be picked again. */
  widgets->updating = TRUE;
  gtk_combo_box_set_active (combo, -1);
  widgets->updating = FALSE;
}

static void
math_dialog_mode_changed (GtkToggleButton *button, gpointer user_data)
{
  MathDialogWidgets *widgets = user_data;
  gboolean polar = gtk_toggle_button_get_active (button);

  widgets->polar = polar;
  gtk_widget_set_visible (widgets->rect_grid, !polar);
  gtk_widget_set_visible (widgets->polar_grid, polar);
  gtk_label_set_text (GTK_LABEL (widgets->help),
		      polar
		      ? "Use theta for the angle and pi and e for the "
			"constants, for example cos(3*theta). Functions: "
			"sin, cos, tan, sqrt, abs, exp, log, unit, delta, "
			"rect."
		      : "Use pi and e for the constants, for example "
			"cos(2*pi*x). Functions: sin, cos, tan, sqrt, abs, "
			"exp, log, unit, delta, rect.");
  math_dialog_fill_presets (widgets);
}

/* Build one mode's fields: the left-hand side label, the expression entry and
 * the two range spin buttons. */
static GtkWidget *
math_dialog_build_grid (const gchar *lhs, const gchar *range_label,
			const gchar *expression, gdouble min, gdouble max,
			GtkWidget **entry_out, GtkWidget **min_out,
			GtkWidget **max_out)
{
  GtkWidget *grid = gtk_grid_new ();
  GtkWidget *label;
  GtkWidget *entry;
  GtkWidget *min_spin;
  GtkWidget *max_spin;

  gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 8);

  label = gtk_label_new (lhs);
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

  entry = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (entry), expression ? expression : "");
  gtk_entry_set_width_chars (GTK_ENTRY (entry), 30);
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  gtk_grid_attach (GTK_GRID (grid), entry, 1, 0, 3, 1);

  label = gtk_label_new (range_label);
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);

  min_spin = gtk_spin_button_new_with_range (-1e6, 1e6, 0.1);
  gtk_spin_button_set_digits (GTK_SPIN_BUTTON (min_spin), 4);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (min_spin), min);
  gtk_grid_attach (GTK_GRID (grid), min_spin, 1, 1, 1, 1);

  label = gtk_label_new ("to");
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), label, 2, 1, 1, 1);

  max_spin = gtk_spin_button_new_with_range (-1e6, 1e6, 0.1);
  gtk_spin_button_set_digits (GTK_SPIN_BUTTON (max_spin), 4);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (max_spin), max);
  gtk_grid_attach (GTK_GRID (grid), max_spin, 3, 1, 1, 1);

  *entry_out = entry;
  *min_out = min_spin;
  *max_out = max_spin;

  return grid;
}

static gboolean
show_math_dialog (ScreenatorData *data)
{
  gboolean was_grabbed = screenator_any_grabbed (data);
  gboolean old_suspend = data->suspend_frame_finish;
  GtkWidget *dialog;
  GtkWidget *content;
  GtkWidget *box;
  GtkWidget *mode_box;
  GtkWidget *rect_radio;
  GtkWidget *polar_radio;
  GtkWidget *preset_box;
  GtkWidget *preset_label;
  GtkWidget *preset_combo;
  GtkWidget *rect_grid;
  GtkWidget *polar_grid;
  GtkWidget *rect_entry, *rect_min, *rect_max;
  GtkWidget *polar_entry, *polar_min, *polar_max;
  GtkWidget *help;
  MathDialogWidgets widgets;
  gboolean accepted = FALSE;
  gint response;

  /* Opening the dialog releases the pointer grab.  Keep editable math plots
   * alive so the new settings can be applied to them when the dialog closes. */
  data->suspend_frame_finish = TRUE;
  if (was_grabbed)
    release_grab (data, NULL);

  dialog = gtk_dialog_new_with_buttons ("Math Function",
					NULL,
					GTK_DIALOG_MODAL |
					GTK_DIALOG_DESTROY_WITH_PARENT,
					"Cancel", GTK_RESPONSE_CANCEL,
					"OK", GTK_RESPONSE_OK,
					NULL);
  gtk_window_set_keep_above (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  /* The dialog must stay above the drawings it edits. */
  show_dialog_above_drawings (data, dialog);

  content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
  gtk_container_set_border_width (GTK_CONTAINER (box), 12);
  gtk_container_add (GTK_CONTAINER (content), box);

  mode_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  rect_radio = gtk_radio_button_new_with_label (NULL,
						"Rectangular  y = f(x)");
  polar_radio = gtk_radio_button_new_with_label_from_widget (
    GTK_RADIO_BUTTON (rect_radio), "Polar  r = f(theta)");
  gtk_box_pack_start (GTK_BOX (mode_box), rect_radio, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (mode_box), polar_radio, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box), mode_box, FALSE, FALSE, 0);

  preset_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  preset_label = gtk_label_new ("Preset:");
  gtk_widget_set_halign (preset_label, GTK_ALIGN_START);
  preset_combo = gtk_combo_box_text_new ();
  gtk_widget_set_hexpand (preset_combo, TRUE);
  gtk_widget_set_size_request (preset_combo, 240, -1);
  gtk_box_pack_start (GTK_BOX (preset_box), preset_label, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (preset_box), preset_combo, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), preset_box, FALSE, FALSE, 0);

  rect_grid = math_dialog_build_grid ("y =", "x from",
				      data->math_expression,
				      data->math_xmin, data->math_xmax,
				      &rect_entry, &rect_min, &rect_max);
  gtk_box_pack_start (GTK_BOX (box), rect_grid, FALSE, FALSE, 0);

  polar_grid = math_dialog_build_grid ("r =", "theta from",
				       data->math_polar_expression,
				       data->math_theta_min,
				       data->math_theta_max,
				       &polar_entry, &polar_min, &polar_max);
  gtk_box_pack_start (GTK_BOX (box), polar_grid, FALSE, FALSE, 0);

  help = gtk_label_new ("");
  gtk_label_set_xalign (GTK_LABEL (help), 0.0);
  gtk_label_set_line_wrap (GTK_LABEL (help), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (help), 44);
  gtk_box_pack_start (GTK_BOX (box), help, FALSE, FALSE, 0);

  if (data->math_polar)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (polar_radio), TRUE);

  widgets.rect_grid = rect_grid;
  widgets.polar_grid = polar_grid;
  widgets.help = help;
  widgets.rect_entry = rect_entry;
  widgets.polar_entry = polar_entry;
  widgets.preset_combo = preset_combo;
  widgets.polar = data->math_polar;
  widgets.updating = FALSE;
  g_signal_connect (polar_radio, "toggled",
		    G_CALLBACK (math_dialog_mode_changed), &widgets);
  g_signal_connect (preset_combo, "changed",
		    G_CALLBACK (math_dialog_preset_changed), &widgets);

  gtk_widget_show_all (dialog);
  math_dialog_mode_changed (GTK_TOGGLE_BUTTON (polar_radio), &widgets);

  while ((response = gtk_dialog_run (GTK_DIALOG (dialog))) == GTK_RESPONSE_OK)
    {
      gboolean polar =
	gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (polar_radio));
      const gchar *expression =
	gtk_entry_get_text (GTK_ENTRY (polar ? polar_entry : rect_entry));
      gdouble lo = gtk_spin_button_get_value (
	GTK_SPIN_BUTTON (polar ? polar_min : rect_min));
      gdouble hi = gtk_spin_button_get_value (
	GTK_SPIN_BUTTON (polar ? polar_max : rect_max));
      gchar *error = NULL;

      if (!expression || !*expression)
	error = g_strdup (polar
			  ? "Enter a function of theta, for example "
			    "cos(3*theta)."
			  : "Enter a function of x, for example "
			    "cos(2*pi*x).");
      else if (!math_expression_is_valid (expression, &error))
	{
	  /* math_expression_is_valid() filled in the message. */
	}
      else if (!(hi > lo))
	error = g_strdup (polar
			  ? "The ending theta value must be greater than the "
			    "starting value."
			  : "The ending x value must be greater than the "
			    "starting value.");

      if (!error)
	{
	  /* Keep both modes' values so switching between them does not lose
	   * the other plot. */
	  g_free (data->math_expression);
	  data->math_expression =
	    g_strdup (gtk_entry_get_text (GTK_ENTRY (rect_entry)));
	  data->math_xmin =
	    gtk_spin_button_get_value (GTK_SPIN_BUTTON (rect_min));
	  data->math_xmax =
	    gtk_spin_button_get_value (GTK_SPIN_BUTTON (rect_max));

	  g_free (data->math_polar_expression);
	  data->math_polar_expression =
	    g_strdup (gtk_entry_get_text (GTK_ENTRY (polar_entry)));
	  data->math_theta_min =
	    gtk_spin_button_get_value (GTK_SPIN_BUTTON (polar_min));
	  data->math_theta_max =
	    gtk_spin_button_get_value (GTK_SPIN_BUTTON (polar_max));

	  data->math_polar = polar;
	  accepted = TRUE;
	  break;
	}

      {
	GtkWidget *message =
	  gtk_message_dialog_new (GTK_WINDOW (dialog),
				  GTK_DIALOG_MODAL |
				  GTK_DIALOG_DESTROY_WITH_PARENT,
				  GTK_MESSAGE_ERROR,
				  GTK_BUTTONS_OK,
				  "%s", error);

	gtk_window_set_keep_above (GTK_WINDOW (message), TRUE);
	gtk_window_set_position (GTK_WINDOW (message), GTK_WIN_POS_CENTER);
	show_dialog_above_drawings (data, message);
	gtk_dialog_run (GTK_DIALOG (message));
	gtk_widget_destroy (message);
      }

      g_free (error);
    }

  gtk_widget_destroy (dialog);

  data->suspend_frame_finish = old_suspend;
  if (was_grabbed)
    acquire_grab (data, NULL);

  if (accepted)
    {
      math_dialog_update_frames (data);
      write_keyfile (data);
    }

  return accepted;
}

/* Select one of the shape tools.  All of them share the default_shape
 * context; only the shape type differs. */
static void
select_shape_tool (ScreenatorData *data, gint pen_tool, ScreenatorShapeType shape)
{
  schedule_tool_activation (data, pen_tool, (gint) shape);
}

void on_icon_arrow (GtkMenuItem *menuitem,
		    gpointer     user_data)
{
  select_shape_tool ((ScreenatorData *) user_data, 2, SCREENATOR_SHAPE_ARROW);
}

void on_icon_line (GtkMenuItem *menuitem,
		   gpointer     user_data)
{
  select_shape_tool ((ScreenatorData *) user_data, 9, SCREENATOR_SHAPE_LINE);
}

void on_icon_spline (GtkMenuItem *menuitem,
		     gpointer     user_data)
{
  select_shape_tool ((ScreenatorData *) user_data, 12, SCREENATOR_SHAPE_SPLINE);
}

void on_icon_polygon (GtkMenuItem *menuitem,
		      gpointer     user_data)
{
  select_shape_tool ((ScreenatorData *) user_data, 14, SCREENATOR_SHAPE_STAR);
}

void on_icon_triangle (GtkMenuItem *menuitem,
		       gpointer     user_data)
{
  select_shape_tool ((ScreenatorData *) user_data, 15,
		     SCREENATOR_SHAPE_TRIANGLE);
}

void on_icon_ellipse (GtkMenuItem *menuitem,
		      gpointer     user_data)
{
  select_shape_tool ((ScreenatorData *) user_data, 6,
		     SCREENATOR_SHAPE_ELLIPSE);
}

void on_icon_rounded_rectangle (GtkMenuItem *menuitem,
				gpointer     user_data)
{
  select_shape_tool ((ScreenatorData *) user_data, 7,
		     SCREENATOR_SHAPE_ROUNDED_RECTANGLE);
}

void on_icon_bucket (GtkMenuItem *menuitem,
		     gpointer     user_data)
{
  select_shape_tool ((ScreenatorData *) user_data, 13,
		     SCREENATOR_SHAPE_BUCKET);
}

void on_icon_math (GtkMenuItem *menuitem,
		   gpointer     user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  /* Ask for the expression and range, then start the tool. */
  if (show_math_dialog (data))
    select_shape_tool (data, 16, SCREENATOR_SHAPE_MATH);
}

void on_icon_bars (GtkMenuItem *menuitem,
		   gpointer     user_data)
{
  select_shape_tool ((ScreenatorData *) user_data, 17,
		     SCREENATOR_SHAPE_BARS);
}

void on_icon_random (GtkMenuItem *menuitem,
		     gpointer     user_data)
{
  select_shape_tool ((ScreenatorData *) user_data, 18,
		     SCREENATOR_SHAPE_RANDOM);
}

void on_icon_text (GtkMenuItem *menuitem,
		   gpointer     user_data)
{
  schedule_tool_activation ((ScreenatorData *) user_data, 4, -1);
}

void on_icon_eraser (GtkMenuItem *menuitem,
		     gpointer     user_data)
{
  schedule_tool_activation ((ScreenatorData *) user_data, 3, -1);
}

void on_icon_marker (GtkMenuItem *menuitem,
		     gpointer     user_data)
{
  schedule_tool_activation ((ScreenatorData *) user_data, 11, -1);
}
