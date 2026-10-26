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

      if (!devdata->shape_active)
	continue;

      cairo_save (cr);
      cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
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

  /* Right-click exits painting mode and returns to normal. */
  if (ev->button == 3)
    {
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
   * preview until the button is released. */
  if (devdata->cur_context &&
      devdata->cur_context->type == SCREENATOR_SHAPE)
    {
      if (ev->button == 1)
	{
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
   * does not create an annotation. */
  if (devdata->cur_context &&
      devdata->cur_context->type == SCREENATOR_SHAPE)
    {
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


void on_about(GtkMenuItem *menuitem,
	      gpointer     user_data)
{
    const gchar *authors [] = { "Mahmoud Alnaanah <malnaanah@gmail.com>",
                                "Christian Beier (Gromit-MPX 1.4.2)",
                                "Simon Budig (Gromit)",
                                 NULL };
    gtk_show_about_dialog (NULL,
			   "program-name", "Screenator",
			   "logo-icon-name", "screenator",
			   "title", "About Screenator",
			   "comments", "Screenator is a vibe-coded screen annotation tool for X11, developed by Mahmoud Alnaanah, based on Gromit-MPX 1.4.2.",
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
  gdk_rgba_parse (&data->shape_fill_color, KEY_DFLT_PEN_COLOR);
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
	{ "Arrow",              "arrow.png",              G_CALLBACK (on_icon_arrow) },
	{ "Angled Line",        "angled-line.png",        G_CALLBACK (on_icon_angled_line) },
	{ "Angled Arrow",       "angled-arrow.png",       G_CALLBACK (on_icon_angled_arrow) },
	{ "Rectangle",          "rectangle.png",          G_CALLBACK (on_icon_rectangle) },
	{ "Ellipse",            "ellipse.png",            G_CALLBACK (on_icon_ellipse) },
	{ "Rounded Rectangle",  "rounded-rectangle.png",  G_CALLBACK (on_icon_rounded_rectangle) }
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
    static const guint widths[] = { 20, 40, 60 };
    static const gchar * const labels[] = { "20 px", "40 px", "60 px" };
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

void on_icon_pen (GtkMenuItem *menuitem,
		  gpointer     user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  GHashTableIter it;
  gpointer value;
  ScreenatorDeviceData *devdata = NULL;

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

  data->forced_context = data->default_shape;
  data->shape_type = shape;
  data->pen_tool = pen_tool;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      devdata = value;
      devdata->state = 0;
    }

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

  data->forced_context = data->default_text;
  data->pen_tool = 4;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      devdata = value;
      devdata->state = 0;
    }

  acquire_grab (data, NULL);
}

void on_icon_eraser (GtkMenuItem *menuitem,
		     gpointer     user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  GHashTableIter it;
  gpointer value;
  ScreenatorDeviceData *devdata = NULL;

  data->forced_context = data->default_eraser;
  data->pen_tool = 3;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      devdata = value;
      devdata->state = 0;
    }

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
  data->forced_context = data->default_marker;
  data->pen_tool = 11;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      devdata = value;
      devdata->state = 0;
    }

  acquire_grab (data, NULL);
}
