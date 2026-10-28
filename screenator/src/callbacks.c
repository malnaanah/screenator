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
  refresh_tray_menu ((ScreenatorData *) user_data);
  return G_SOURCE_REMOVE;
}

static void
schedule_tray_menu_refresh (ScreenatorData *data)
{
  g_idle_add (refresh_tray_menu_idle, data);
}

static void on_icon_eraser_size (GtkMenuItem *menuitem, gpointer user_data);
static GArray *spline_preview_points (ScreenatorDeviceData *devdata);
static void spline_draw_handles (ScreenatorData *data, cairo_t *cr,
				 GArray *nodes, guint count);
static void star_draw_handles (ScreenatorData *data, cairo_t *cr,
			       const ScreenatorStar *star);
static void triangle_draw_handles (ScreenatorData *data, cairo_t *cr,
				   const ScreenatorTriangle *triangle);
static void rounded_draw_handle (ScreenatorData *data, cairo_t *cr,
				 const ScreenatorRoundedRect *rect);


gboolean on_expose (GtkWidget *widget,
		    cairo_t* cr,
		    gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  GHashTableIter it;
  gpointer value;

  if(data->debug)
    g_printerr("DEBUG: got draw event\n");

  cairo_save (cr);
  cairo_set_source_surface (cr, data->backbuffer, 0, 0);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);
  cairo_restore (cr);

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
	  !devdata->triangle_active && !devdata->rounded_active)
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
	  draw_triangle (data, cr, &devdata->triangle);
	  triangle_draw_handles (data, cr, &devdata->triangle);
	}
      else if (data->shape_type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE &&
	       devdata->rounded_active)
	{
	  draw_rounded_rect (data, cr,
			     devdata->rounded.x0, devdata->rounded.y0,
			     devdata->rounded.x1, devdata->rounded.y1,
			     devdata->rounded.radius);
	  rounded_draw_handle (data, cr, &devdata->rounded);
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
	draw_shape (data, cr, data->shape_type,
		    devdata->shape_x0, devdata->shape_y0,
		    devdata->shape_x1, devdata->shape_y1);
      cairo_restore (cr);
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

  if(!data->composited) // set shape
    {
      cairo_region_t* r = gdk_cairo_region_create_from_surface(data->backbuffer);
      gtk_widget_shape_combine_region(data->win, r);
      cairo_region_destroy(r);
    }

  setup_input_devices(data);


  gtk_widget_show_all (data->win);
}



void on_composited_changed ( GdkScreen *screen,
			   gpointer   user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if(data->debug)
    g_printerr("DEBUG: got composited-changed event\n");

  data->composited = gdk_screen_is_composited (data->screen);

  if(data->composited)
    {
      // undo shape
      gtk_widget_shape_combine_region(data->win, NULL);
      // the window itself stays opaque; only the strokes carry the opacity
      gtk_widget_set_opacity(data->win, 1.0);
    }

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
 * commits what has been typed so far.  The entry only previews the text;
 * after it is committed the string is rendered into the annotation layer.
 */

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
  y = data->text_y - allocation.y - layout_y;

  if (x + req.width > (gint) data->width)
    x = (gint) data->width - req.width;
  if (y + req.height > (gint) data->height)
    y = (gint) data->height - req.height;
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;

  gtk_window_move (GTK_WINDOW (data->text_editor), x, y);
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

  gtk_window_move (GTK_WINDOW (data->text_editor),
		   direction == PANGO_DIRECTION_RTL
		     ? data->text_x - available
		     : data->text_x,
		   data->text_y - layout_y);

  data->text_editor_sized = TRUE;
  data->text_editor_rtl = (direction == PANGO_DIRECTION_RTL);
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
    return;

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  direction = pango_find_base_dir (text, -1);
G_GNUC_END_IGNORE_DEPRECATIONS
  if (direction != PANGO_DIRECTION_RTL)
    direction = PANGO_DIRECTION_LTR;

  if (!data->text_editor_sized ||
      (direction == PANGO_DIRECTION_RTL) != data->text_editor_rtl)
    size_text_editor (data, direction);
}

static void
close_text_editor (ScreenatorData *data, gboolean commit)
{
  GtkWidget *editor = data->text_editor;
  GtkWidget *entry = data->text_entry;
  GdkDevice *device = data->text_device;
  gchar *text = NULL;
  gint x = data->text_x;
  gint y = data->text_y;

  if (!editor)
    return;

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

  gtk_widget_destroy (editor);

  if (text && *text)
    {
      snap_undo_state (data);
      draw_text (data, device, x, y, text);
    }
  g_free (text);

  acquire_grab (data, NULL);
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
      data->text_editor = NULL;
      data->text_entry = NULL;
      data->text_editor_active = FALSE;
      data->text_device = NULL;
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

static void
show_text_editor (ScreenatorData *data, GdkDevice *device, gint x, gint y)
{
  GtkWidget *editor;
  GtkWidget *entry;
  GtkCssProvider *provider;
  PangoFontDescription *font;
  GdkScreen *screen;
  GdkVisual *visual;
  GdkRGBA color;
  gchar *color_string;
  gchar *css;

  release_grab (data, NULL);

  data->text_device = device;
  data->text_x = x;
  data->text_y = y;
  data->text_editor_active = FALSE;
  data->text_editor_sized = FALSE;
  data->text_editor_rtl = FALSE;

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

  color = data->pen_color;
  color.alpha = data->opacity;
  color_string = gdk_rgba_to_string (&color);
  css = g_strdup_printf ("entry {"
			 " background: transparent;"
			 " background-image: none;"
			 " border: none;"
			 " box-shadow: none;"
			 " padding: 0;"
			 " margin: 0;"
			 " color: %s;"
			 " caret-color: %s;"
			 " }",
			 color_string, color_string);
  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_data (provider, css, -1, NULL);
  gtk_style_context_add_provider (gtk_widget_get_style_context (entry),
				  GTK_STYLE_PROVIDER (provider),
				  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
  g_free (css);
  g_free (color_string);

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
  gtk_widget_grab_focus (entry);
  gtk_window_present (GTK_WINDOW (editor));
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
    gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
				bounds, 0);
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


/* The Polygon tool places a star shape with a single click and keeps it
 * editable until it is committed.  Four handles control the number of sides,
 * the radius, the rotation and the inner radius, which turns the star into a
 * regular polygon as it grows.  Clicking empty space commits the current shape
 * and places a new one; a double-click or a right-click commits it and stays
 * in the tool. */

#define STAR_DEFAULT_RADIUS 64.0
#define STAR_DEFAULT_INNER  0.45
#define STAR_MIN_RADIUS     8.0
#define STAR_POINTS_HANDLE_OFFSET 28.0
#define POLYGON_SIDES_PER_PIXEL 12.0

/* Radius of the equilateral triangle placed by a single click. */
#define TRIANGLE_DEFAULT_RADIUS 64.0

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

/* Start a new shape centred on the click.  A default radius makes a single
 * click useful; the handles refine it afterwards. */
static void
star_begin (ScreenatorData *data, ScreenatorDeviceData *devdata,
	    gint x, gint y)
{
  devdata->star.cx = x;
  devdata->star.cy = y;
  devdata->star.radius = STAR_DEFAULT_RADIUS;
  devdata->star.inner = STAR_DEFAULT_INNER;
  devdata->star.rotation = -G_PI / 2.0;
  devdata->star.points = 5;

  devdata->star_active = TRUE;

  /* Pressing and dragging on the freshly placed star moves it, so it can be
   * positioned without a separate centre handle. */
  devdata->star_drag = SCREENATOR_STAR_HANDLE_CENTRE;
  devdata->star_press_x = x;
  devdata->star_press_y = y;
  devdata->star_press_cx = x;
  devdata->star_press_cy = y;

  star_invalidate (data, &devdata->star);
}

/* Move the grabbed handle to (x, y).  The side-count handle always starts at
 * three when it is pressed; dragging away from the press point in any
 * direction adds sides. */
static void
star_move_handle (ScreenatorData *data, ScreenatorDeviceData *devdata,
		  gint x, gint y)
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
    case SCREENATOR_STAR_HANDLE_RADIUS:
      star->radius = CLAMP (dist, STAR_MIN_RADIUS, 100000.0);
      break;
    case SCREENATOR_STAR_HANDLE_INNER:
      if (star->radius > 0.0)
	star->inner = CLAMP (dist / star->radius, 0.0, 1.0);
      break;
    case SCREENATOR_STAR_HANDLE_ROTATION:
      if (dist > 0.0)
	star->rotation = atan2 (dy, dx) - G_PI / 2.0;
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

/* Start a new equilateral triangle centred on the click.  The three vertex
 * handles refine it afterwards. */
static void
triangle_begin (ScreenatorData *data, ScreenatorDeviceData *devdata,
		gint x, gint y)
{
  static const gdouble angles[3] =
    { -G_PI / 2.0, G_PI / 6.0, 5.0 * G_PI / 6.0 };
  gint i;

  for (i = 0; i < 3; i++)
    {
      devdata->triangle.v[i].x =
	(gint) round (x + TRIANGLE_DEFAULT_RADIUS * cos (angles[i]));
      devdata->triangle.v[i].y =
	(gint) round (y + TRIANGLE_DEFAULT_RADIUS * sin (angles[i]));
      devdata->triangle_press_v[i] = devdata->triangle.v[i];
    }

  devdata->triangle_active = TRUE;

  /* Pressing and dragging on the fresh triangle moves it, so it can be
   * positioned without a separate centre handle. */
  devdata->triangle_drag = SCREENATOR_TRIANGLE_HANDLE_CENTRE;
  devdata->triangle_press_x = x;
  devdata->triangle_press_y = y;

  triangle_invalidate (data, &devdata->triangle);
}

/* Move one vertex of the triangle, or the whole triangle, to (x, y). */
static void
triangle_move_handle (ScreenatorData *data, ScreenatorDeviceData *devdata,
		      gint x, gint y)
{
  ScreenatorTriangle *triangle = &devdata->triangle;

  if (!devdata->triangle_active ||
      devdata->triangle_drag == SCREENATOR_TRIANGLE_HANDLE_NONE)
    return;

  triangle_invalidate (data, triangle);

  switch (devdata->triangle_drag)
    {
    case SCREENATOR_TRIANGLE_HANDLE_V0:
      triangle->v[0].x = x;
      triangle->v[0].y = y;
      break;
    case SCREENATOR_TRIANGLE_HANDLE_V1:
      triangle->v[1].x = x;
      triangle->v[1].y = y;
      break;
    case SCREENATOR_TRIANGLE_HANDLE_V2:
      triangle->v[2].x = x;
      triangle->v[2].y = y;
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

/* Draw the three vertex handles of the editable triangle. */
static void
triangle_draw_handles (ScreenatorData *data, cairo_t *cr,
		       const ScreenatorTriangle *triangle)
{
  gint i;

  (void) data;

  for (i = 0; i < 3; i++)
    star_draw_grip (cr, triangle->v[i].x, triangle->v[i].y);
}


/* Position of the roundness handle: on the top edge, radius pixels from the
 * top-left corner. */
static void
rounded_handle_position (const ScreenatorRoundedRect *rect, GdkPoint *pos)
{
  gint x = MIN (rect->x0, rect->x1);
  gint y = MIN (rect->y0, rect->y1);
  gint w = ABS (rect->x1 - rect->x0);
  gint h = ABS (rect->y1 - rect->y0);
  gdouble max_radius = MIN (w, h) / 2.0;

  pos->x = (gint) round (x + CLAMP (rect->radius, 0.0, max_radius));
  pos->y = y;
}

/* Return TRUE when (x, y) is on the roundness handle. */
static gboolean
rounded_handle_at (const ScreenatorRoundedRect *rect, gint x, gint y)
{
  const gint radius = 11;
  GdkPoint pos;
  gint dx, dy;

  rounded_handle_position (rect, &pos);
  dx = pos.x - x;
  dy = pos.y - y;

  return dx * dx + dy * dy <= radius * radius;
}

/* Point-in-rectangle test, used to decide whether a press moves the rounded
 * rectangle. */
static gboolean
rounded_contains (const ScreenatorRoundedRect *rect, gint x, gint y)
{
  return x >= MIN (rect->x0, rect->x1) &&
	 x <= MAX (rect->x0, rect->x1) &&
	 y >= MIN (rect->y0, rect->y1) &&
	 y <= MAX (rect->y0, rect->y1);
}

/* Repaint the area covered by the rounded rectangle and its handle. */
static void
rounded_invalidate (ScreenatorData *data, const ScreenatorRoundedRect *rect)
{
  GdkRectangle bounds;
  gint pad = (gint) data->pen_width * 2 + 64;

  if (!data->win)
    return;

  rounded_rect_get_bounds (rect, pad, &bounds);
  gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
			      &bounds, 0);
}

/* Draw the roundness handle on the top edge.  Its distance from the top-left
 * corner is the corner radius. */
static void
rounded_draw_handle (ScreenatorData *data, cairo_t *cr,
		     const ScreenatorRoundedRect *rect)
{
  GdkPoint pos;

  (void) data;

  rounded_handle_position (rect, &pos);
  star_draw_grip (cr, pos.x, pos.y);
}


gboolean on_buttonpress (GtkWidget *win, 
			 GdkEventButton *ev,
			 gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  gdouble pressure = 1;

  /* get the data for this device */
  ScreenatorDeviceData *devdata = g_hash_table_lookup(data->devdatatable, ev->device);

  if(data->debug)
    g_printerr("DEBUG: Device '%s': Button %i Down State %d at (x,y)=(%.2f : %.2f)\n",
	       gdk_device_get_name(ev->device), ev->button, ev->state, ev->x, ev->y);

  if (!devdata->is_grabbed)
    return FALSE;

  /* Right-click finishes a spline or a star that is being edited.  Otherwise
   * it exits painting mode and returns to normal. */
  if (ev->button == 3)
    {
      if (devdata->cur_context &&
	  devdata->cur_context->type == SCREENATOR_SHAPE &&
	  data->shape_type == SCREENATOR_SHAPE_SPLINE &&
	  devdata->shape_active)
	{
	  spline_finish (data, devdata);
	  return TRUE;
	}

      if (devdata->cur_context &&
	  devdata->cur_context->type == SCREENATOR_SHAPE &&
	  data->shape_type == SCREENATOR_SHAPE_STAR &&
	  devdata->star_active)
	{
	  star_finish (data, devdata);
	  return TRUE;
	}

      if (devdata->cur_context &&
	  devdata->cur_context->type == SCREENATOR_SHAPE &&
	  data->shape_type == SCREENATOR_SHAPE_TRIANGLE &&
	  devdata->triangle_active)
	{
	  triangle_finish (data, devdata);
	  return TRUE;
	}

      if (devdata->cur_context &&
	  devdata->cur_context->type == SCREENATOR_SHAPE &&
	  data->shape_type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE &&
	  devdata->rounded_active)
	{
	  rounded_rect_finish (data, devdata);
	  return TRUE;
	}

      release_grab (data, NULL);
      return TRUE;
    }

  /* Do not drop unprocessed motion events.  Compressed events make fast
   * pointer movements look like a sequence of straight segments. */
  gdk_window_set_event_compression(gtk_widget_get_window(data->win), FALSE);

  /* See GdkModifierType. Am I fixing a Gtk misbehaviour???  */
  ev->state |= 1 << (ev->button + 7);


  if (ev->state != devdata->state ||
      devdata->lastslave != gdk_event_get_source_device ((GdkEvent *) ev))
    select_tool (data, ev->device, gdk_event_get_source_device ((GdkEvent *) ev), ev->state);

  /* The Text tool writes on screen instead of painting a stroke.  Remember
   * where the button was pressed; the text is placed there when the button
   * is released, even if the pointer moved slightly in between. */
  if (devdata->cur_context &&
      devdata->cur_context->type == SCREENATOR_TEXT)
    {
      if (ev->button == 1)
	{
	  data->text_x = ev->x;
	  data->text_y = ev->y;
	  data->text_pressed = TRUE;
	}
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

	      if (ev->type == GDK_2BUTTON_PRESS)
		{
		  triangle_finish (data, devdata);
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

	  /* A rounded rectangle that is already placed keeps its roundness
	   * handle until the user presses outside it. */
	  if (data->shape_type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE &&
	      devdata->rounded_active)
	    {
	      if (ev->type == GDK_2BUTTON_PRESS)
		{
		  rounded_rect_finish (data, devdata);
		  return TRUE;
		}

	      if (rounded_handle_at (&devdata->rounded, ev->x, ev->y))
		{
		  devdata->rounded_radius_drag = TRUE;
		  return TRUE;
		}

	      /* A press inside the rectangle grabs it and moves it. */
	      if (rounded_contains (&devdata->rounded, ev->x, ev->y))
		{
		  devdata->rounded_move_drag = TRUE;
		  devdata->rounded_press_x = ev->x;
		  devdata->rounded_press_y = ev->y;
		  devdata->rounded_press = devdata->rounded;
		  return TRUE;
		}

	      /* A press outside commits it; the code below starts a new
	       * rectangle at the click position. */
	      rounded_rect_finish (data, devdata);
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

  if (!devdata->is_grabbed)
    return FALSE;

  if(data->debug)
      g_printerr("DEBUG: Device '%s': motion to (x,y)=(%.2f : %.2f)\n", gdk_device_get_name(ev->device), ev->x, ev->y);

  if (ev->state != devdata->state ||
      devdata->lastslave != gdk_event_get_source_device ((GdkEvent *) ev))
    select_tool (data, ev->device, gdk_event_get_source_device ((GdkEvent *) ev), ev->state);

  /* Moving the pointer with the Text tool selected does not draw. */
  if (devdata->cur_context &&
      devdata->cur_context->type == SCREENATOR_TEXT)
    return TRUE;

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
	    star_move_handle (data, devdata, ev->x, ev->y);
	  return TRUE;
	}

      if (data->shape_type == SCREENATOR_SHAPE_TRIANGLE)
	{
	  if (devdata->triangle_active &&
	      devdata->triangle_drag != SCREENATOR_TRIANGLE_HANDLE_NONE)
	    triangle_move_handle (data, devdata, ev->x, ev->y);
	  return TRUE;
	}

      if (data->shape_type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE &&
	  devdata->rounded_active)
	{
	  if (devdata->rounded_radius_drag)
	    {
	      gint x0 = MIN (devdata->rounded.x0, devdata->rounded.x1);
	      gint w = ABS (devdata->rounded.x1 - devdata->rounded.x0);
	      gint h = ABS (devdata->rounded.y1 - devdata->rounded.y0);
	      gdouble max_radius = MIN (w, h) / 2.0;

	      rounded_invalidate (data, &devdata->rounded);
	      devdata->rounded.radius =
		CLAMP ((gdouble) (ev->x - x0), 0.0, max_radius);
	      rounded_invalidate (data, &devdata->rounded);
	      return TRUE;
	    }

	  if (devdata->rounded_move_drag)
	    {
	      gint dx = ev->x - devdata->rounded_press_x;
	      gint dy = ev->y - devdata->rounded_press_y;

	      rounded_invalidate (data, &devdata->rounded);
	      devdata->rounded.x0 = devdata->rounded_press.x0 + dx;
	      devdata->rounded.y0 = devdata->rounded_press.y0 + dy;
	      devdata->rounded.x1 = devdata->rounded_press.x1 + dx;
	      devdata->rounded.y1 = devdata->rounded_press.y1 + dy;
	      rounded_invalidate (data, &devdata->rounded);
	      return TRUE;
	    }

	  return TRUE;
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
          if (pressure > 0)
            {
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

  if (pressure > 0)
    {
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
   

  if ((ev->x != devdata->lastx) ||
      (ev->y != devdata->lasty))
    on_motion(win, (GdkEventMotion *) ev, user_data);

  if (!devdata->is_grabbed)
    return FALSE;

  /* Open the inline text editor at the position where the button was
   * pressed, not where it happened to be released. */
  if (devdata->cur_context &&
      devdata->cur_context->type == SCREENATOR_TEXT)
    {
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
	  /* Releasing a vertex handle ends the drag; the triangle stays
	   * editable until it is committed. */
	  devdata->triangle_drag = SCREENATOR_TRIANGLE_HANDLE_NONE;
	  return TRUE;
	}

      if (data->shape_type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE &&
	  devdata->rounded_active)
	{
	  /* Releasing the roundness handle or the body ends the drag; the
	   * rectangle stays editable until it is committed. */
	  if (devdata->rounded_radius_drag || devdata->rounded_move_drag)
	    {
	      devdata->rounded_radius_drag = FALSE;
	      devdata->rounded_move_drag = FALSE;
	      return TRUE;
	    }
	}

      if (devdata->shape_active)
	{
	  GdkRectangle bounds;
	  gint dx = ABS (devdata->shape_x1 - devdata->shape_x0);
	  gint dy = ABS (devdata->shape_y1 - devdata->shape_y0);

	  devdata->shape_active = FALSE;

	  if (dx > 0 || dy > 0)
	    {
	      /* A rounded rectangle stays editable after the drag so its
	       * roundness handle can adjust the corners. */
	      if (data->shape_type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE)
		{
		  gint w = ABS (devdata->shape_x1 - devdata->shape_x0);
		  gint h = ABS (devdata->shape_y1 - devdata->shape_y0);

		  devdata->rounded.x0 = devdata->shape_x0;
		  devdata->rounded.y0 = devdata->shape_y0;
		  devdata->rounded.x1 = devdata->shape_x1;
		  devdata->rounded.y1 = devdata->shape_y1;
		  devdata->rounded.radius = MIN (w, h) / 4.0;
		  devdata->rounded_active = TRUE;

		  rounded_invalidate (data, &devdata->rounded);
		  return TRUE;
		}

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
  toggle_visibility(data);
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


/* Some themes use a very dim colour for links.  The About dialog is the only
 * place where Screenator shows a link, so use the normal text colour there to
 * keep the licence line readable. */
static void
install_about_link_style (void)
{
  static gboolean installed = FALSE;
  GtkCssProvider *provider;

  if (installed)
    return;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_data (provider,
				   "*:link, *:visited { color: @theme_fg_color; }",
				   -1, NULL);
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
					     GTK_STYLE_PROVIDER (provider),
					     GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (provider);
  installed = TRUE;
}


void on_about(GtkMenuItem *menuitem,
	      gpointer     user_data)
{
    const gchar *authors [] = {
        "Mahmoud Alnaanah <a href=\"mailto:malnaanah@gmail.com\">malnaanah@gmail.com</a>",
        "Christian Beier (Gromit-MPX 1.4.2)",
        "Simon Budig (Gromit)",
        NULL };

    install_about_link_style ();

    gtk_show_about_dialog (NULL,
			   "program-name", "Screenator",
			   "logo-icon-name", "screenator",
			   "title", "About Screenator",
			   "comments", "Screenator is a vibe-coded screen annotation tool for X11, developed by Mahmoud Alnaanah (malnaanah@gmail.com), based on Gromit-MPX 1.4.2.",
			   "version", VERSION,
			   "authors", authors,
			   "copyright", "2026 Mahmoud Alnaanah\nBased on Gromit-MPX 1.4.2, Copyright 2009-2022 Christian Beier, Copyright 2000 Simon Budig",
			   "license-type", GTK_LICENSE_GPL_2_0,
			   NULL);
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

static gchar *main_icon_path (gboolean svg)
{
  const gchar *dir = icon_data_dir ();

  if (dir)
    return g_build_filename (dir, svg ? "icons/hicolor/scalable/apps/screenator.svg"
                                      : "pixmaps/screenator.png", NULL);

  return g_strdup (svg ? "/usr/share/icons/hicolor/scalable/apps/screenator.svg"
                       : "/usr/share/pixmaps/screenator.png");
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

  path = main_icon_path (FALSE);
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
  gchar *svg_path;
  gchar *png_path;
  GdkPixbuf *base;
  GdkPixbuf *pixbuf;
  cairo_surface_t *surf;
  cairo_t *cr;

  /* Prefer the SVG so the icon stays sharp at every floating icon size.
   * Fall back to the high-resolution PNG if no SVG loader is installed. */
  svg_path = main_icon_path (TRUE);
  png_path = main_icon_path (FALSE);
  base = gdk_pixbuf_new_from_file_at_scale (svg_path,
					    data->icon_size, data->icon_size,
					    TRUE, NULL);
  if (!base)
    base = gdk_pixbuf_new_from_file_at_scale (png_path,
					      data->icon_size, data->icon_size,
					      TRUE, NULL);
  g_free (svg_path);
  g_free (png_path);

  if (!base)
    return NULL;

  if (!active)
    return base;

  surf = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, data->icon_size, data->icon_size);
  cr = cairo_create (surf);

  cairo_set_source_rgba (cr, 0, 0, 0, 0);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr); /* transparent background */

  gdk_cairo_set_source_pixbuf (cr, base, 0, 0);
  cairo_paint (cr);

  /* red ring marks the active (painting) state, SVG transparency preserved */
  cairo_set_source_rgb (cr, 1.0, 0.0, 0.0);
  cairo_set_line_width (cr, 2.0);
  cairo_arc (cr, data->icon_size / 2.0, data->icon_size / 2.0, data->icon_size / 2.0 - 2,
             0, 2 * G_PI);
  cairo_stroke (cr);

  cairo_destroy (cr);
  pixbuf = gdk_pixbuf_get_from_surface (surf, 0, 0, data->icon_size, data->icon_size);
  cairo_surface_destroy (surf);
  g_object_unref (base);
  return pixbuf;
}

void update_icon_image (ScreenatorData *data)
{
  GdkPixbuf *pixbuf;

  if (!data->icon_image)
    return;

  pixbuf = load_icon_pixbuf (data, screenator_any_grabbed (data));
  gtk_image_set_from_pixbuf (GTK_IMAGE (data->icon_image),
			     pixbuf ? pixbuf : NULL);
  if (pixbuf)
    g_object_unref (pixbuf);
}

static void on_icon_color (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  const gchar *name = g_object_get_data (G_OBJECT (menuitem), "icon-color-name");
  GdkRGBA color;

  if (name && gdk_rgba_parse (&color, name))
    {
      /* Remember the hue (opacity is a separate setting). */
      data->pen_color.red = color.red;
      data->pen_color.green = color.green;
      data->pen_color.blue = color.blue;
      data->pen_color.alpha = 1.0;

      apply_pen_settings (data);

      schedule_tray_menu_refresh (data);
    }
}

static void on_icon_shape_fill (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  const gchar *name = g_object_get_data (G_OBJECT (menuitem),
					 "icon-shape-fill-name");
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

  schedule_tray_menu_refresh (data);
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
  GHashTableIter it;
  gpointer value;
  guint width = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (menuitem),
						     "icon-eraser-width"));

  data->eraser_width = width;

  apply_pen_settings (data);
  update_eraser_cursor (data);

  /* The cursor is attached to the device grab.  Resetting the tracked state
   * makes the next event select the tool again and pick up the new cursor. */
  if (data->devdatatable)
    {
      g_hash_table_iter_init (&it, data->devdatatable);
      while (g_hash_table_iter_next (&it, NULL, &value))
	((ScreenatorDeviceData *) value)->state = 0;
    }

  schedule_tray_menu_refresh (data);
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
  gtk_font_chooser_set_preview_text (GTK_FONT_CHOOSER (dialog),
				     "Screenator text annotation");
  if (data->font && *data->font)
    gtk_font_chooser_set_font (GTK_FONT_CHOOSER (dialog), data->font);

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

static void on_icon_size (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  guint new_size = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (menuitem),
                                                        "icon-size-value"));

  data->icon_size = new_size;

  /* Resize the icon window and reload the icon pixbuf */
  gtk_window_resize (GTK_WINDOW (data->icon_win), new_size, new_size);
  update_icon_image (data);

  schedule_tray_menu_refresh (data);
}

static void on_icon_toggle_floating (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if (gtk_widget_get_visible (data->icon_win))
    gtk_widget_hide (data->icon_win);
  else
    gtk_widget_show (data->icon_win);
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
  data->icon_size = KEY_DFLT_ICON_SIZE;
  data->pen_tool = 1;
  g_free (data->font);
  data->font = g_strdup (KEY_DFLT_FONT);
  data->shape_fill_enabled = FALSE;
  gdk_rgba_parse (&data->shape_fill_color, KEY_DFLT_SHAPE_FILL_COLOR);
  data->shape_fill_color.alpha = 1.0;

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

    if (visual && gdk_screen_is_composited (screen))
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

  data->icon_dragging = FALSE;
  data->icon_drag_start_x = -1;
  data->icon_drag_start_y = -1;
  data->icon_menu_x = -1;
  data->icon_menu_y = -1;

  gtk_widget_show_all (data->icon_win);

  /* place at the top-right corner of the screen (icon is data->icon_size) */
  {
    gint x = data->width - data->icon_size - 20;
    gint y = 20;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    gtk_window_move (GTK_WINDOW (data->icon_win), x, y);
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
	{ "Free Line",          "free-line.png",          G_CALLBACK (on_icon_free_line) },
	{ "Spline",             "spline.png",             G_CALLBACK (on_icon_spline) },
	{ "Polygon",            "polygon.png",            G_CALLBACK (on_icon_polygon) },
	{ "Triangle",           "triangle.png",           G_CALLBACK (on_icon_triangle) },
	{ "Arrow",              "arrow.png",              G_CALLBACK (on_icon_arrow) },
	{ "Angled Line",        "angled-line.png",        G_CALLBACK (on_icon_angled_line) },
	{ "Angled Arrow",       "angled-arrow.png",       G_CALLBACK (on_icon_angled_arrow) },
	{ "Rectangle",          "rectangle.png",          G_CALLBACK (on_icon_rectangle) },
	{ "Ellipse",            "ellipse.png",            G_CALLBACK (on_icon_ellipse) },
	{ "Rounded Rectangle",  "rounded-rectangle.png",  G_CALLBACK (on_icon_rounded_rectangle) },
	{ "Paint Bucket",       "bucket.png",             G_CALLBACK (on_icon_bucket) }
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

  /* --- line thickness submenu --- */
  item = gtk_menu_item_new_with_label ("Line Thickness (Pixels)");
  submenu = gtk_menu_new ();
  {
    GSList *group = NULL;
    static const guint widths[] = { 3, 6, 12, 24, 48 };
    static const gchar * const labels[] = { "3 px", "6 px", "12 px", "24 px", "48 px" };
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
    static const struct { const gchar *label; const gchar *name; } colors[] =
      {
	{ "Red", "#FF0000" },
	{ "Green", "#00AA00" },
	{ "Blue", "#0000FF" },
	{ "Yellow", "#FFFF00" },
	{ "Black", "#000000" },
	{ "White", "#FFFFFF" }
      };
    GtkWidget *items[G_N_ELEMENTS (colors)];
    GtkWidget *active_item = NULL;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (colors); i++)
      {
	GtkWidget *subitem;
	GdkRGBA color;

	subitem = gtk_radio_menu_item_new_with_label (group, colors[i].label);
	group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (subitem));
	if (gdk_rgba_parse (&color, colors[i].name))
	  {
	    GdkRGBA *current = data->default_pen->paint_color;

	    /* Opacity is a setting of its own, so compare the hue only. */
	    if (current->red == color.red &&
		current->green == color.green &&
		current->blue == color.blue)
	      active_item = subitem;
	  }
	g_object_set_data (G_OBJECT (subitem), "icon-color-name",
			   (gpointer) colors[i].name);
	items[i] = subitem;
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), subitem);
	gtk_widget_show (subitem);
      }
    if (active_item)
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item), TRUE);
    for (i = 0; i < G_N_ELEMENTS (colors); i++)
      g_signal_connect (items[i], "activate", G_CALLBACK (on_icon_color), data);
  }
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  /* --- shape fill colour submenu --- */
  item = gtk_menu_item_new_with_label ("Shapes Fill Color");
  submenu = gtk_menu_new ();
  {
    GSList *group = NULL;
    static const struct { const gchar *label; const gchar *name; } colors[] =
      {
	{ "None", "none" },
	{ "Green", "#00AA00" },
	{ "Blue", "#0000FF" },
	{ "Yellow", "#FFFF00" },
	{ "Black", "#000000" },
	{ "White", "#FFFFFF" },
	{ "Red", "#FF0000" }
      };
    GtkWidget *items[G_N_ELEMENTS (colors)];
    GtkWidget *active_item = NULL;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (colors); i++)
      {
	GtkWidget *subitem;

	subitem = gtk_radio_menu_item_new_with_label (group, colors[i].label);
	group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (subitem));

	if (g_ascii_strcasecmp (colors[i].name, "none") == 0)
	  {
	    if (!data->shape_fill_enabled)
	      active_item = subitem;
	  }
	else
	  {
	    GdkRGBA color;

	    if (data->shape_fill_enabled &&
		gdk_rgba_parse (&color, colors[i].name) &&
		data->shape_fill_color.red == color.red &&
		data->shape_fill_color.green == color.green &&
		data->shape_fill_color.blue == color.blue)
	      active_item = subitem;
	  }

	g_object_set_data (G_OBJECT (subitem), "icon-shape-fill-name",
			   (gpointer) colors[i].name);
	items[i] = subitem;
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), subitem);
	gtk_widget_show (subitem);
      }
    if (active_item)
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item), TRUE);
    for (i = 0; i < G_N_ELEMENTS (colors); i++)
      g_signal_connect (items[i], "activate",
			G_CALLBACK (on_icon_shape_fill), data);
  }
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  /* --- eraser size submenu (above Opacity) --- */
  item = gtk_menu_item_new_with_label ("Eraser Size (Pixels)");
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
    static const gdouble opacities[] = { 1.0, 0.75, 0.5, 0.25 };
    static const gchar * const labels[] = { "100%", "75%", "50%", "25%" };
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
    static const guint sizes[] = { 32, 48, 64 };
    static const gchar * const labels[] = { "32x32", "48x48", "64x64" };
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

  /* --- text font --- */
  item = gtk_image_menu_item_new_with_label ("Change Font...");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("font-settings.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_icon_change_font), data);
}

GtkWidget *build_icon_menu (ScreenatorData *data)
{
  GtkWidget *menu = gtk_menu_new ();
  GtkWidget *item;

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

  item = gtk_image_menu_item_new_with_label ("Show/Hide Floating Icon");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("hide-menu.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate",
		    G_CALLBACK (on_icon_toggle_floating), data);

  item = gtk_image_menu_item_new_with_label ("Reset Settings");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("reset-settings.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate",
		    G_CALLBACK (on_icon_reset_settings), data);

  {
    GtkWidget *sep = gtk_separator_menu_item_new ();
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), sep);
  }

  item = gtk_menu_item_new_with_label ("About");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_about), NULL);

  item = gtk_menu_item_new_with_label ("Quit");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (gtk_main_quit), NULL);

  return menu;
}

void popup_icon_menu (ScreenatorData *data, gint x, gint y)
{
  GtkWidget *menu = build_icon_menu (data);
  GList     *l;
  gint      ix, iy;

  /* position the menu below the icon so it does not cover it */
  gtk_window_get_position (GTK_WINDOW (data->icon_win), &ix, &iy);
  data->icon_menu_x = ix;
  data->icon_menu_y = iy + data->icon_size + 6;

  for (l = gtk_container_get_children (GTK_CONTAINER (menu)); l; l = l->next)
    gtk_widget_show (GTK_WIDGET (l->data));
  g_list_free (gtk_container_get_children (GTK_CONTAINER (menu)));

  gtk_menu_popup (GTK_MENU (menu), NULL, NULL,
		  icon_menu_position_func, data,
		  0, GDK_CURRENT_TIME);
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

  data->icon_dragging = FALSE;
  gtk_window_get_position (GTK_WINDOW (win), &wx, &wy);
  data->icon_drag_start_x = ev->x_root - wx;
  data->icon_drag_start_y = ev->y_root - wy;

  return TRUE;
}

gboolean on_icon_motion (GtkWidget    *win,
			 GdkEventMotion *ev,
			 gpointer   user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if (!(ev->state & (GDK_BUTTON1_MASK | GDK_BUTTON3_MASK)))
    return FALSE;

  data->icon_dragging = TRUE;
  {
    gint x = ev->x_root - data->icon_drag_start_x;
    gint y = ev->y_root - data->icon_drag_start_y;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    gtk_window_move (GTK_WINDOW (win), x, y);
  }

  return TRUE;
}

gboolean on_icon_button_release (GtkWidget   *win,
				 GdkEventButton *ev,
				 gpointer   user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if ((ev->button == 1 || ev->button == 3) && !data->icon_dragging)
    {
      gint x, y;
      gtk_window_get_position (GTK_WINDOW (win), &x, &y);
      popup_icon_menu (data, x, y);
    }

  data->icon_dragging = FALSE;
  data->icon_drag_start_x = -1;
  data->icon_drag_start_y = -1;

  return TRUE;
}

/* Show the tool hints.  The general hint appears when any tool is selected;
 * the spline hint is added for the Spline tool.  Each hint has its own
 * "Never show this message again" check box.  Reset Settings deliberately
 * leaves both flags alone, so a hint the user turned off stays off. */
static void
show_tool_hint (ScreenatorData *data, gboolean spline)
{
  gboolean show_general = !data->hide_tool_hint;
  gboolean show_spline = spline && !data->hide_spline_hint;
  gboolean was_grabbed;
  gboolean changed = FALSE;
  GtkWidget *dialog, *content, *box, *label;
  GtkWidget *general_check = NULL;
  GtkWidget *spline_check = NULL;

  if (!show_general && !show_spline)
    return;

  /* A modal dialog cannot be used while the pointer is grabbed. */
  was_grabbed = screenator_any_grabbed (data);
  if (was_grabbed)
    release_grab (data, NULL);

  dialog = gtk_dialog_new_with_buttons ("Screenator",
					NULL,
					GTK_DIALOG_MODAL |
					GTK_DIALOG_DESTROY_WITH_PARENT,
					"OK", GTK_RESPONSE_OK,
					NULL);
  gtk_window_set_keep_above (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width (GTK_CONTAINER (box), 12);
  gtk_container_add (GTK_CONTAINER (content), box);

  if (show_general)
    {
      label = gtk_label_new ("Left click to draw, middle click to erase, "
			     "and right click to exit tool");
      gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
      gtk_label_set_max_width_chars (GTK_LABEL (label), 56);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

      general_check =
	gtk_check_button_new_with_label ("Never show this message again");
      gtk_box_pack_start (GTK_BOX (box), general_check, FALSE, FALSE, 0);
    }

  if (show_general && show_spline)
    {
      GtkWidget *separator =
	gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
      gtk_box_pack_start (GTK_BOX (box), separator, FALSE, FALSE, 4);
    }

  if (show_spline)
    {
      label = gtk_label_new ("Move handles using left mouse button, and hold "
			     "CTRL to move each handle control point "
			     "separately.");
      gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
      gtk_label_set_max_width_chars (GTK_LABEL (label), 56);
      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

      spline_check =
	gtk_check_button_new_with_label ("Never show this message again");
      gtk_box_pack_start (GTK_BOX (box), spline_check, FALSE, FALSE, 0);
    }

  gtk_widget_show_all (dialog);
  gtk_dialog_run (GTK_DIALOG (dialog));

  if (general_check &&
      gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (general_check)))
    {
      data->hide_tool_hint = TRUE;
      changed = TRUE;
    }

  if (spline_check &&
      gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (spline_check)))
    {
      data->hide_spline_hint = TRUE;
      changed = TRUE;
    }

  gtk_widget_destroy (dialog);

  if (changed)
    write_keyfile (data);

  if (was_grabbed)
    acquire_grab (data, NULL);
}

void on_icon_pen (GtkMenuItem *menuitem,
		  gpointer     user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  GHashTableIter it;
  gpointer value;
  ScreenatorDeviceData *devdata = NULL;

  star_finish_all (data);
  triangle_finish_all (data);
  rounded_rect_finish_all (data);
  spline_cancel_all (data);
  data->forced_context = data->default_pen;
  data->pen_tool = 1;
  data->default_pen->arrowsize = 0;

  /* Reset tracked button state so select_tool() is re-invoked on the next
   * button press and the newly selected tool takes effect immediately. */
  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      devdata = value;
      devdata->state = 0;
    }

  show_tool_hint (data, FALSE);
  acquire_grab (data, NULL);
}

/* Select one of the shape tools.  All of them share the default_shape
 * context; only the shape type differs. */
static void
select_shape_tool (ScreenatorData *data, gint pen_tool, ScreenatorShapeType shape)
{
  GHashTableIter it;
  gpointer value;
  ScreenatorDeviceData *devdata = NULL;

  star_finish_all (data);
  triangle_finish_all (data);
  rounded_rect_finish_all (data);
  spline_cancel_all (data);
  data->forced_context = data->default_shape;
  data->shape_type = shape;
  data->pen_tool = pen_tool;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      devdata = value;
      devdata->state = 0;
    }

  show_tool_hint (data, shape == SCREENATOR_SHAPE_SPLINE);
  acquire_grab (data, NULL);
}

void on_icon_arrow (GtkMenuItem *menuitem,
		    gpointer     user_data)
{
  select_shape_tool ((ScreenatorData *) user_data, 2, SCREENATOR_SHAPE_FREE_ARROW);
}

void on_icon_free_line (GtkMenuItem *menuitem,
			gpointer     user_data)
{
  select_shape_tool ((ScreenatorData *) user_data, 9, SCREENATOR_SHAPE_FREE_LINE);
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

void on_icon_angled_arrow (GtkMenuItem *menuitem,
			   gpointer     user_data)
{
  select_shape_tool ((ScreenatorData *) user_data, 10,
		     SCREENATOR_SHAPE_ANGLED_ARROW);
}

void on_icon_rectangle (GtkMenuItem *menuitem,
			gpointer     user_data)
{
  select_shape_tool ((ScreenatorData *) user_data, 5,
		     SCREENATOR_SHAPE_RECTANGLE);
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

void on_icon_angled_line (GtkMenuItem *menuitem,
			  gpointer     user_data)
{
  select_shape_tool ((ScreenatorData *) user_data, 8,
		     SCREENATOR_SHAPE_ANGLED_LINE);
}

void on_icon_text (GtkMenuItem *menuitem,
		   gpointer     user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  GHashTableIter it;
  gpointer value;
  ScreenatorDeviceData *devdata = NULL;

  star_finish_all (data);
  triangle_finish_all (data);
  rounded_rect_finish_all (data);
  spline_cancel_all (data);
  data->forced_context = data->default_text;
  data->pen_tool = 4;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      devdata = value;
      devdata->state = 0;
    }

  show_tool_hint (data, FALSE);
  acquire_grab (data, NULL);
}

void on_icon_eraser (GtkMenuItem *menuitem,
		     gpointer     user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  GHashTableIter it;
  gpointer value;
  ScreenatorDeviceData *devdata = NULL;

  star_finish_all (data);
  triangle_finish_all (data);
  rounded_rect_finish_all (data);
  spline_cancel_all (data);
  data->forced_context = data->default_eraser;
  data->pen_tool = 3;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      devdata = value;
      devdata->state = 0;
    }

  show_tool_hint (data, FALSE);
  acquire_grab (data, NULL);
}

void on_icon_marker (GtkMenuItem *menuitem,
		     gpointer     user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  GHashTableIter it;
  gpointer value;
  ScreenatorDeviceData *devdata = NULL;

  /* The marker keeps its own fixed colour, opacity and width.  Selecting it
   * must not change the global pen settings. */
  star_finish_all (data);
  triangle_finish_all (data);
  rounded_rect_finish_all (data);
  spline_cancel_all (data);
  data->forced_context = data->default_marker;
  data->pen_tool = 11;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      devdata = value;
      devdata->state = 0;
    }

  show_tool_hint (data, FALSE);
  acquire_grab (data, NULL);
}
