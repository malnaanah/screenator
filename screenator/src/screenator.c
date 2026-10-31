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
#include <stdarg.h>
#include <math.h>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <pango/pangocairo.h>

#include "callbacks.h"
#include "config.h"
#include "input.h"
#include "screenator.h"
#include "build-config.h"

#include "../data/paint_cursor.xpm"
#include "../data/text_cursor.xpm"
#include "../data/bucket_cursor.xpm"



/* Build the eraser cursor from the configured eraser width.  The ring is a
 * little larger than the erasing circle so it stays visible, and the hotspot
 * sits in its centre.  Recreating the cursor whenever the size changes keeps
 * the visible ring and the erased area identical. */
void update_eraser_cursor (ScreenatorData *data)
{
  guint width = CLAMP (data->eraser_width, 4, 128);
  gint dim = (gint) width + 2;
  cairo_surface_t *surface;
  cairo_t *cr;
  GdkPixbuf *pixbuf;
  GdkCursor *cursor;

  if (!data->display)
    return;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, dim, dim);
  cr = cairo_create (surface);

  /* White ring below a thin black ring, so the cursor is visible on both
   * light and dark backgrounds. */
  cairo_set_line_width (cr, 2.0);
  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.9);
  cairo_arc (cr, dim / 2.0, dim / 2.0, width / 2.0, 0, 2 * G_PI);
  cairo_stroke (cr);
  cairo_set_line_width (cr, 1.0);
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.9);
  cairo_arc (cr, dim / 2.0, dim / 2.0, width / 2.0, 0, 2 * G_PI);
  cairo_stroke (cr);

  cairo_destroy (cr);
  pixbuf = gdk_pixbuf_get_from_surface (surface, 0, 0, dim, dim);
  cairo_surface_destroy (surface);

  if (!pixbuf)
    return;

  cursor = gdk_cursor_new_from_pixbuf (data->display, pixbuf,
				       dim / 2, dim / 2);
  g_object_unref (pixbuf);

  if (data->erase_cursor)
    g_object_unref (data->erase_cursor);
  data->erase_cursor = cursor;
}



ScreenatorPaintContext *paint_context_new (ScreenatorData *data, 
				       ScreenatorPaintType type,
				       GdkRGBA *paint_color, 
				       guint width,
				       guint arrowsize,
				       guint minwidth)
{
  ScreenatorPaintContext *context;

  context = g_malloc (sizeof (ScreenatorPaintContext));

  context->type = type;
  context->width = width;
  context->arrowsize = arrowsize;
  context->minwidth = minwidth;
  context->paint_color = paint_color;

  
  context->paint_ctx = cairo_create (data->backbuffer);

  gdk_cairo_set_source_rgba(context->paint_ctx, paint_color);
  if(!data->composited)
    cairo_set_antialias(context->paint_ctx, CAIRO_ANTIALIAS_NONE);
  cairo_set_line_width(context->paint_ctx, width);
  cairo_set_line_cap(context->paint_ctx, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(context->paint_ctx, CAIRO_LINE_JOIN_ROUND);
  
  if (type == SCREENATOR_ERASER)
    cairo_set_operator(context->paint_ctx, CAIRO_OPERATOR_CLEAR);
  else
    if (type == SCREENATOR_RECOLOR)
      cairo_set_operator(context->paint_ctx, CAIRO_OPERATOR_ATOP);
    else /* SCREENATOR_PEN, SCREENATOR_TEXT or SCREENATOR_SHAPE */
      cairo_set_operator(context->paint_ctx, CAIRO_OPERATOR_OVER);

  return context;
}


/*
 * Apply the pen settings chosen in the icon menu (thickness, colour and
 * opacity) to the default pen.  Called after the pen has been created and
 * whenever it is recreated, so the settings survive a restart and a screen
 * size change.
 */
static void
set_context_opacity (ScreenatorPaintContext *context, gdouble opacity)
{
  if (!context ||
      (context->type != SCREENATOR_PEN &&
       context->type != SCREENATOR_TEXT &&
       context->type != SCREENATOR_SHAPE))
    return;

  context->paint_color->alpha = opacity;
  gdk_cairo_set_source_rgba (context->paint_ctx, context->paint_color);
}


/* The width, colour and opacity chosen in the icon menu are global drawing
 * settings and apply to every pen tool, including the ones defined in
 * screenator.cfg, and to the Text and Shape tools.  Only the pen type and arrow
 * size stay per tool.  The Marker keeps its own fixed style and is therefore
 * not part of this list. */
static void
apply_pen_style (ScreenatorData *data, ScreenatorPaintContext *context)
{
  if (!context ||
      (context->type != SCREENATOR_PEN &&
       context->type != SCREENATOR_TEXT &&
       context->type != SCREENATOR_SHAPE))
    return;

  context->width = data->pen_width;
  context->paint_color->red = data->pen_color.red;
  context->paint_color->green = data->pen_color.green;
  context->paint_color->blue = data->pen_color.blue;
  set_context_opacity (context, data->opacity);
  if (context->type == SCREENATOR_PEN || context->type == SCREENATOR_SHAPE)
    cairo_set_line_width (context->paint_ctx, context->width);
}


static void
apply_pen_style_to_pens (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;

  apply_pen_style (data, data->default_pen);
  apply_pen_style (data, data->default_text);
  apply_pen_style (data, data->default_shape);

  if (!data->tool_config)
    return;

  g_hash_table_iter_init (&it, data->tool_config);
  while (g_hash_table_iter_next (&it, NULL, &value))
    apply_pen_style (data, (ScreenatorPaintContext *) value);
}


void apply_pen_settings (ScreenatorData *data)
{
  if (!data->default_pen)
    return;

  apply_pen_style_to_pens (data);

  /* The eraser size is a setting of its own and does not follow the pen
   * width. */
  if (data->default_eraser)
    {
      data->default_eraser->width = data->eraser_width;
      cairo_set_line_width (data->default_eraser->paint_ctx,
			    data->eraser_width);
    }

  /* Restore the tool that was selected from the icon menu. */
  switch (data->pen_tool)
    {
    case 1:
      data->forced_context = data->default_pen;
      data->default_pen->arrowsize = 0;
      break;
    case 2:
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_ARROW;
      break;
    case 3:
      data->forced_context = data->default_eraser;
      break;
    case 4:
      data->forced_context = data->default_text;
      break;
    case 5:
      /* The rectangle tool was removed; a saved rectangle selection falls
       * back to the rounded rectangle, whose roundness can be zero. */
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_ROUNDED_RECTANGLE;
      break;
    case 6:
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_ELLIPSE;
      break;
    case 7:
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_ROUNDED_RECTANGLE;
      break;
    case 8:
      /* The angled line was merged into the line tool. */
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_LINE;
      break;
    case 9:
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_LINE;
      break;
    case 10:
      /* The angled arrow was merged into the arrow tool. */
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_ARROW;
      break;
    case 11:
      data->forced_context = data->default_marker;
      break;
    case 12:
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_SPLINE;
      break;
    case 13:
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_BUCKET;
      break;
    case 14:
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_STAR;
      break;
    case 15:
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_TRIANGLE;
      break;
    case 16:
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_MATH;
      break;
    case 17:
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_BARS;
      break;
    default:
      data->forced_context = NULL;
      break;
    }

  /* The indicator shows the line colour, thickness and opacity. */
  update_settings_indicator (data);
}


/* Change the drawing opacity and make it take effect immediately. */
void set_pen_opacity (ScreenatorData *data, gdouble opacity)
{
  if (opacity < 0.0)
    opacity = 0.0;
  if (opacity > 1.0)
    opacity = 1.0;

  data->opacity = opacity;

  if (data->default_pen)
    apply_pen_style_to_pens (data);

  update_settings_indicator (data);
}


void paint_context_print (gchar *name, 
			  ScreenatorPaintContext *context)
{
  g_printerr ("Tool name: \"%-20s\": ", name);
  switch (context->type)
  {
    case SCREENATOR_PEN:
      g_printerr ("Pen,     "); break;
    case SCREENATOR_ERASER:
      g_printerr ("Eraser,  "); break;
    case SCREENATOR_RECOLOR:
      g_printerr ("Recolor, "); break;
    case SCREENATOR_TEXT:
      g_printerr ("Text,    "); break;
    case SCREENATOR_SHAPE:
      g_printerr ("Shape,   "); break;
    default:
      g_printerr ("UNKNOWN, "); break;
  }

  g_printerr ("width: %3d, ", context->width);
  g_printerr ("minwidth: %3d, ", context->minwidth);
  g_printerr ("arrowsize: %.2f, ", context->arrowsize);
  g_printerr ("color: %s\n", gdk_rgba_to_string(context->paint_color));
}


void paint_context_free (ScreenatorPaintContext *context)
{
  cairo_destroy(context->paint_ctx);
  g_free (context);
}


void coord_list_prepend (ScreenatorData *data, 
			 GdkDevice* dev, 
			 gint x, 
			 gint y, 
			 gint width)
{
  /* get the data for this device */
  ScreenatorDeviceData *devdata = g_hash_table_lookup(data->devdatatable, dev);

  ScreenatorStrokeCoordinate *point;

  point = g_malloc (sizeof (ScreenatorStrokeCoordinate));
  point->x = x;
  point->y = y;
  point->width = width;

  devdata->coordlist = g_list_prepend (devdata->coordlist, point);
}


void coord_list_free (ScreenatorData *data, 
		      GdkDevice* dev)
{
  /* get the data for this device */
  ScreenatorDeviceData *devdata = g_hash_table_lookup(data->devdatatable, dev);

  GList *ptr;
  ptr = devdata->coordlist;

  while (ptr)
    {
      g_free (ptr->data);
      ptr = ptr->next;
    }

  g_list_free (devdata->coordlist);

  devdata->coordlist = NULL;
}


gboolean coord_list_get_arrow_param (ScreenatorData *data,
				     GdkDevice  *dev,
				     gint        search_radius,
				     gint       *ret_width,
				     gfloat     *ret_direction)
{
  gint x0, y0, r2, dist;
  gboolean success = FALSE;
  ScreenatorStrokeCoordinate  *cur_point, *valid_point;
  /* get the data for this device */
  ScreenatorDeviceData *devdata = g_hash_table_lookup(data->devdatatable, dev);
  GList *ptr = devdata->coordlist;
  gfloat width;

  valid_point = NULL;

  if (ptr)
    {
      cur_point = ptr->data;
      x0 = cur_point->x;
      y0 = cur_point->y;
      r2 = search_radius * search_radius;
      dist = 0;

      while (ptr && dist < r2)
        {
          ptr = ptr->next;
          if (ptr)
            {
              cur_point = ptr->data;
              dist = (cur_point->x - x0) * (cur_point->x - x0) +
                     (cur_point->y - y0) * (cur_point->y - y0);
              width = cur_point->width * devdata->cur_context->arrowsize;
              if (width * 2 <= dist &&
                  (!valid_point || valid_point->width < cur_point->width))
                valid_point = cur_point;
            }
        }

      if (valid_point)
        {
          *ret_width = MAX (valid_point->width * devdata->cur_context->arrowsize,
                            2);
          *ret_direction = atan2 (y0 - valid_point->y, x0 - valid_point->x);
          success = TRUE;
        }
    }

  return success;
}


void hide_window (ScreenatorData *data)
{
  if (!data->hidden)
    {
      /* save grab state of each device */
      GHashTableIter it;
      gpointer value;
      ScreenatorDeviceData* devdata; 
      g_hash_table_iter_init (&it, data->devdatatable);
      while (g_hash_table_iter_next (&it, NULL, &value)) 
        {
          devdata = value;
          devdata->was_grabbed = devdata->is_grabbed;
        }
      data->hidden = 1;
      release_grab (data, NULL); /* release all */
      gtk_widget_hide (data->win);
      
      if(data->debug)
        g_printerr ("DEBUG: Hiding window.\n");
    }
}


/* Keep the floating icon above the annotation window.
 *
 * The annotation window covers the whole screen and, on X11, it takes the
 * clicks for every pixel it covers.  It is transparent, so an icon below it
 * still looks visible but cannot be clicked any more.  Whenever the
 * annotation window is raised, the icon has to be raised again. */
void raise_icon_window (ScreenatorData *data)
{
  GdkWindow *icon_window;

  /* The settings indicator sits to the left of the icon and has to stay above
   * the annotation window as well. */
  if (data->settings_win && gtk_widget_get_visible (data->settings_win))
    {
      GdkWindow *settings_window = gtk_widget_get_window (data->settings_win);

      if (settings_window)
	gdk_window_raise (settings_window);
    }

  if (!data->icon_win || !gtk_widget_get_visible (data->icon_win))
    return;

  icon_window = gtk_widget_get_window (data->icon_win);
  if (icon_window)
    gdk_window_raise (icon_window);
}


void show_window (ScreenatorData *data)
{
  if (data->hidden)
    {
      gtk_widget_show (data->win);
      data->hidden = 0;

      /* restore grab state of each device */
      GHashTableIter it;
      gpointer value;
      ScreenatorDeviceData* devdata; 
      g_hash_table_iter_init (&it, data->devdatatable);
      while (g_hash_table_iter_next (&it, NULL, &value)) 
        {
          devdata = value;
          if(devdata->was_grabbed)
            acquire_grab (data, devdata->device);
        }
      if(data->debug)
        g_printerr ("DEBUG: Showing window.\n");
    }
  gdk_window_raise (gtk_widget_get_window(data->win));

  /* The icon has to stay on top of the annotation window, otherwise the
   * annotation window swallows the clicks meant for the icon. */
  raise_icon_window (data);

  /* A paper is the active window and keeps the keyboard focus. */
  if (data->paper != SCREENATOR_PAPER_NONE)
    paper_take_focus (data);
}



void toggle_visibility (ScreenatorData *data)
{
  if (data->hidden)
    show_window (data);
  else
    hide_window (data);
}


/* ------------------------------------------------------------------
 * Screen paper
 * ------------------------------------------------------------------ */

/* The colours and the spacing come from assets/ruled-page.pdf and
 * assets/graph-page.pdf: blue ruled lines about 8.5 mm apart and a grey 5 mm
 * grid, which is 32 px and 20 px on a 96 dpi screen.  The first line is one
 * spacing away from the edge, like the margin of a paper page. */
#define PAPER_RULED_SPACING 32
#define PAPER_GRAPH_SPACING 20

/* Draw the paper below the annotations.  The caller composites the backbuffer
 * on top afterwards, so erasing a stroke shows the paper again. */
void
draw_screen_paper (ScreenatorData *data, cairo_t *cr)
{
  gint x, y;

  if (data->paper == SCREENATOR_PAPER_NONE)
    return;

  cairo_save (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
  cairo_paint (cr);

  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
  cairo_set_line_width (cr, 1.0);

  if (data->paper == SCREENATOR_PAPER_RULED)
    {
      /* 0.25098 0.627451 1 in the ruled page. */
      cairo_set_source_rgb (cr, 0.25098, 0.627451, 1.0);
      for (y = PAPER_RULED_SPACING; y < (gint) data->height;
	   y += PAPER_RULED_SPACING)
	{
	  cairo_move_to (cr, 0, y + 0.5);
	  cairo_line_to (cr, data->width, y + 0.5);
	}
      cairo_stroke (cr);
    }
  else if (data->paper == SCREENATOR_PAPER_GRAPH)
    {
      /* 0.741176 grey in the graph page. */
      cairo_set_source_rgb (cr, 0.741176, 0.741176, 0.741176);
      for (x = PAPER_GRAPH_SPACING; x < (gint) data->width;
	   x += PAPER_GRAPH_SPACING)
	{
	  cairo_move_to (cr, x + 0.5, 0);
	  cairo_line_to (cr, x + 0.5, data->height);
	}
      for (y = PAPER_GRAPH_SPACING; y < (gint) data->height;
	   y += PAPER_GRAPH_SPACING)
	{
	  cairo_move_to (cr, 0, y + 0.5);
	  cairo_line_to (cr, data->width, y + 0.5);
	}
      cairo_stroke (cr);
    }

  cairo_restore (cr);
}

/* The annotation window is only visible where it has content when the screen is
 * not composited, and it is transparent to input everywhere.  A paper covers
 * the whole screen, so the window has to be visible everywhere; clicks still
 * pass through to the application below, and painting mode receives them
 * through its pointer grab. */
void
update_window_shape (ScreenatorData *data)
{
  cairo_region_t *region;

  if (!data->win || !gtk_widget_get_window (data->win))
    return;

  if (data->paper != SCREENATOR_PAPER_NONE || data->composited)
    gtk_widget_shape_combine_region (data->win, NULL);
  else
    {
      region = gdk_cairo_region_create_from_surface (data->backbuffer);

      /* The text preview and its caret are drawn on the annotation window but
       * are not part of the backbuffer.  Add their pixels to the shape so they
       * are visible while the editor is open. */
      text_preview_add_shape (data, region);

      /* Active frame previews are drawn on the window as well, until they are
       * committed to the backbuffer. */
      shape_previews_add_shape (data, region);

      gtk_widget_shape_combine_region (data->win, region);
      cairo_region_destroy (region);
    }

  /* With a screen paper the annotation window is the active window: it takes
   * the pointer input, so clicks and scrolling do not reach the desktop or the
   * applications below.  Without a paper the window stays click-through. */
  if (data->paper != SCREENATOR_PAPER_NONE)
    {
      cairo_rectangle_int_t rect;

      rect.x = 0;
      rect.y = 0;
      rect.width = data->width;
      rect.height = data->height;
      region = cairo_region_create_rectangle (&rect);
    }
  else
    region = cairo_region_create ();

  gtk_widget_input_shape_combine_region (data->win, region);
  cairo_region_destroy (region);
}


/* ------------------------------------------------------------------
 * Saved annotations
 *
 * The drawing layer is saved as a PNG when Screenator quits and painted back
 * into the backbuffer on the next start.  The screen paper is not part of it:
 * it is drawn separately and follows its own setting.
 * ------------------------------------------------------------------ */

static gchar *
annotations_path (void)
{
  return g_build_filename (g_get_user_config_dir (),
			   "screenator-annotations.png", NULL);
}

/* TRUE if the surface has at least one visible pixel. */
static gboolean
surface_has_annotations (cairo_surface_t *surface)
{
  gint width, height, stride, x, y;
  guchar *pixels;

  if (!surface ||
      cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS ||
      cairo_image_surface_get_format (surface) != CAIRO_FORMAT_ARGB32)
    return FALSE;

  cairo_surface_flush (surface);
  width = cairo_image_surface_get_width (surface);
  height = cairo_image_surface_get_height (surface);
  stride = cairo_image_surface_get_stride (surface);
  pixels = cairo_image_surface_get_data (surface);
  if (!pixels)
    return FALSE;

  for (y = 0; y < height; y++)
    {
      guint32 *row = (guint32 *) (pixels + y * stride);

      for (x = 0; x < width; x++)
	if ((row[x] >> 24) != 0)
	  return TRUE;
    }

  return FALSE;
}

static void
save_annotations (ScreenatorData *data)
{
  gchar *path;

  if (!data->backbuffer)
    return;

  path = annotations_path ();

  /* No drawings: remove an older file instead of saving a blank one. */
  if (!surface_has_annotations (data->backbuffer))
    {
      g_unlink (path);
      g_free (path);
      return;
    }

  cairo_surface_flush (data->backbuffer);
  if (cairo_surface_write_to_png (data->backbuffer, path) != CAIRO_STATUS_SUCCESS)
    g_printerr ("Could not save the annotations to %s\n", path);

  g_free (path);
}

/* Paint the saved drawing layer back into the backbuffer.  Returns TRUE if
 * there was something to restore. */
static gboolean
load_annotations (ScreenatorData *data)
{
  gchar *path;
  cairo_surface_t *saved;
  cairo_t *cr;
  gboolean loaded = FALSE;
  gint width, height;

  if (!data->save_annotations_on_exit || !data->backbuffer)
    return FALSE;

  path = annotations_path ();
  saved = cairo_image_surface_create_from_png (path);
  g_free (path);

  if (cairo_surface_status (saved) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (saved);
      return FALSE;
    }

  if (surface_has_annotations (saved))
    {
      width = cairo_image_surface_get_width (saved);
      height = cairo_image_surface_get_height (saved);

      cr = cairo_create (data->backbuffer);
      if (width > 0 && height > 0)
	cairo_scale (cr, (gdouble) data->width / width,
		     (gdouble) data->height / height);
      cairo_set_source_surface (cr, saved, 0, 0);
      cairo_paint (cr);
      cairo_destroy (cr);
      loaded = TRUE;
    }

  cairo_surface_destroy (saved);
  return loaded;
}

void remove_saved_annotations (void)
{
  gchar *path = annotations_path ();

  g_unlink (path);
  g_free (path);
}


void clear_screen (ScreenatorData *data)
{
  cairo_t *cr = cairo_create(data->backbuffer);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);
  cairo_destroy(cr);

  GdkRectangle rect = {0, 0, data->width, data->height};
  gdk_window_invalidate_rect(gtk_widget_get_window(data->win), &rect, 0); 

  update_window_shape (data);

  data->painted = 0;
  remove_saved_annotations ();

  if(data->debug)
    g_printerr ("DEBUG: Cleared screen.\n");
}


gint reshape (gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if (data->modified && !data->composited)
    {
      if (gtk_events_pending () && data->delayed < 5)
        {
          data->delayed++ ;
        }
      else
        {
	  update_window_shape (data);

	  /*
	    this is not needed when there is user input, i.e. when drawing,
	    but needed when uing the undo functionality.
	  */
	  gtk_widget_queue_draw(data->win);

          data->modified = 0;
          data->delayed = 0;
        }
    }
  return 1;
}


/* Cursor that belongs to a paint context.  The paint bucket shares the shape
 * context with the other shapes, so its type is checked separately. */
GdkCursor *get_tool_cursor (ScreenatorData *data,
			    ScreenatorPaintContext *context)
{
  if (context && context->type == SCREENATOR_ERASER)
    return data->erase_cursor;
  if (context && context->type == SCREENATOR_TEXT)
    return data->text_cursor;
  if (context && context->type == SCREENATOR_SHAPE &&
      data->shape_type == SCREENATOR_SHAPE_BUCKET)
    return data->bucket_cursor;

  return data->paint_cursor;
}


/* Point the device grab at the cursor that belongs to the given tool.  The
 * grab cursor has to be replaced by grabbing again; gdk_window_set_cursor()
 * has no effect while the device is grabbed. */
void set_device_cursor (ScreenatorData *data,
			GdkDevice *device,
			ScreenatorPaintContext *context)
{
  GdkCursor *cursor;

  if (!device || !gtk_widget_get_window (data->win))
    return;

  cursor = get_tool_cursor (data, context);

  gdk_device_grab (device,
		   gtk_widget_get_window (data->win),
		   GDK_OWNERSHIP_NONE,
		   FALSE,
		   SCREENATOR_MOUSE_EVENTS,
		   cursor,
		   GDK_CURRENT_TIME);
}


void select_tool (ScreenatorData *data, 
		  GdkDevice *device, 
		  GdkDevice *slave_device,
		  guint state)
{
  guint buttons = 0, modifier = 0, slave_len = 0, len = 0, default_len = 0;
  guint req_buttons = 0, req_modifier = 0;
  guint i, j, success = 0;
  ScreenatorPaintContext *context = NULL;
  guchar *slave_name;
  guchar *name;
  guchar *default_name;

  /* get the data for this device */
  ScreenatorDeviceData *devdata = g_hash_table_lookup(data->devdatatable, device);
 
  if (device)
    {
      slave_len = strlen (gdk_device_get_name(slave_device));
      slave_name = (guchar*) g_strndup (gdk_device_get_name(slave_device), slave_len + 3);
      len = strlen (gdk_device_get_name(device));
      name = (guchar*) g_strndup (gdk_device_get_name(device), len + 3);
      default_len = strlen(DEFAULT_DEVICE_NAME);
      default_name = (guchar*) g_strndup (DEFAULT_DEVICE_NAME, default_len + 3);
      
      
      /* Extract Button/Modifiers from state (see GdkModifierType) */
      req_buttons = (state >> 8) & 31;

      req_modifier = (state >> 1) & 7;
      if (state & GDK_SHIFT_MASK) req_modifier |= 1;

      slave_name [slave_len] = 124;
      slave_name [slave_len+3] = 0;
      name [len] = 124;
      name [len+3] = 0;
      default_name [default_len] = 124;
      default_name [default_len+3] = 0;

      /*
	Iterate i up until <= req_buttons.
	For each i, find out if bits of i are _all_ in `req_buttons`.
	- If yes, lookup if there is tool and select if there is.
	- If no, try next i, no tool lookup.
      */
      context = NULL;
      if (data->forced_context)
	{
	  devdata->cur_context = data->forced_context;
	  success = 1;
	}
      i=-1;
      do
        {
	  i++;
	  if (success)
	    break;

	  /*
	    For all i > 0, find out if _all_ bits representing the iterator 'i'
	    are present in req_buttons as well. If not (none or only some are),
	    then go on.
	    The condition i==0 handles the config cases where no button is given.
	  */
	  buttons = i & req_buttons;
	  if(i > 0 && (buttons == 0 || buttons != i))
	      continue;

          j=-1;
          do
            {
              j++;
              modifier = req_modifier & ((1 << j)-1);
              slave_name [slave_len+1] = buttons + 64;
              slave_name [slave_len+2] = modifier + 48;
              name [len+1] = buttons + 64;
              name [len+2] = modifier + 48;
              default_name [default_len+1] = buttons + 64;
              default_name [default_len+2] = modifier + 48;

	            if(data->debug)
                g_printerr("DEBUG: select_tool looking up context for '%s' attached to '%s'\n", slave_name, name);

              context = g_hash_table_lookup (data->tool_config, slave_name);
              if(context) {
                  if(data->debug)
                    g_printerr("DEBUG: select_tool set context for '%s'\n", slave_name);
                  devdata->cur_context = context;
                  success = 1;
              }
              else /* try master name */
              if ((context = g_hash_table_lookup (data->tool_config, name)))
                {
                  if(data->debug)
                    g_printerr("DEBUG: select_tool set context for '%s'\n", name);
                  devdata->cur_context = context;
                  success = 1;
                }
              else /* try default_name */
                if((context = g_hash_table_lookup (data->tool_config, default_name)))
                  {
                    if(data->debug)
                      g_printerr("DEBUG: select_tool set default context '%s' for '%s'\n", default_name, name);
                    devdata->cur_context = context;
                    success = 1;
                  }
                
            }
          while (j<=3 && req_modifier >= (1u << j));
        }
      while (i < req_buttons);

      g_free (name);
      g_free (default_name);

      if (!success)
        {
          if (gdk_device_get_source(device) == GDK_SOURCE_ERASER)
            devdata->cur_context = data->default_eraser;
          else
            devdata->cur_context = data->default_pen;

	  if(data->debug)
	      g_printerr("DEBUG: select_tool set fallback context for '%s'\n", name);
        }
    }
  else
    g_printerr ("ERROR: select_tool attempted to select nonexistent device!\n");

  if(data->debug)
    g_printerr("DEBUG: select_tool setting cursor for tool type %d\n",
	       devdata->cur_context ? (gint) devdata->cur_context->type : -1);

  set_device_cursor (data, device, devdata->cur_context);

  devdata->state = state;
  devdata->lastslave = slave_device;
}



void snap_undo_state (ScreenatorData *data)
{
  if(data->debug)
    g_printerr ("DEBUG: Snapping undo buffer %d.\n", data->undo_head);

  copy_surface(data->undobuffer[data->undo_head], data->backbuffer);

  // Increment head position
  data->undo_head++;
  if(data->undo_head >= SCREENATOR_MAX_UNDO)
    data->undo_head -= SCREENATOR_MAX_UNDO;
  data->undo_depth++;
  // See if we ran out of undo levels with oldest undo overwritten
  if(data->undo_depth > SCREENATOR_MAX_UNDO)
    data->undo_depth = SCREENATOR_MAX_UNDO;
  // Invalidate any redo from this position
  data->redo_depth = 0;
}



void copy_surface (cairo_surface_t *dst, cairo_surface_t *src)
{
  cairo_t *cr = cairo_create(dst);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);
  cairo_destroy(cr);
}



void swap_surfaces (cairo_surface_t *a, cairo_surface_t *b)
{
  int width = cairo_image_surface_get_width(a);
  int height = cairo_image_surface_get_height(a);
  cairo_surface_t *temp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  copy_surface(temp, a);
  copy_surface(a, b);
  copy_surface(b, temp);
  cairo_surface_destroy(temp);
}



void undo_drawing (ScreenatorData *data)
{
  /* Swap undobuffer[head-1]->backbuffer */
  if(data->undo_depth <= 0)
    return;
  data->undo_depth--;
  data->redo_depth++;
  if(data->redo_depth > SCREENATOR_MAX_UNDO)
    data->redo_depth -= SCREENATOR_MAX_UNDO;
  data->undo_head--;
  if(data->undo_head < 0)
    data->undo_head += SCREENATOR_MAX_UNDO;

  swap_surfaces(data->backbuffer, data->undobuffer[data->undo_head]);

  GdkRectangle rect = {0, 0, data->width, data->height};
  gdk_window_invalidate_rect(gtk_widget_get_window(data->win), &rect, 0); 

  data->modified = 1;

  if(data->debug)
    g_printerr ("DEBUG: Undo drawing %d.\n", data->undo_head);
}



void redo_drawing (ScreenatorData *data)
{
  if(data->redo_depth <= 0)
    return;

  swap_surfaces(data->backbuffer, data->undobuffer[data->undo_head]);

  data->redo_depth--;
  data->undo_depth++;
  data->undo_head++;
  if(data->undo_head >= SCREENATOR_MAX_UNDO)
    data->undo_head -= SCREENATOR_MAX_UNDO;
  
  GdkRectangle rect = {0, 0, data->width, data->height};
  gdk_window_invalidate_rect(gtk_widget_get_window(data->win), &rect, 0); 

  data->modified = 1;

  if(data->debug)
    g_printerr("DEBUG: Redo drawing.\n");
}



/*
 * Translucent strokes
 *
 * Every motion event draws one short segment. Drawing those segments directly
 * onto the backbuffer with a translucent colour composites the areas where
 * segments and their round caps overlap again and again, so the stroke gets
 * darker spots at the segment joints and looks uneven.
 *
 * A translucent pen stroke is therefore collected in a layer of its own,
 * drawn there at full opacity, and composited onto the backbuffer exactly
 * once when the stroke is finished.
 */

void devdata_free (ScreenatorDeviceData *devdata)
{
  if (!devdata)
    return;

  if (devdata->stroke_ctx)
    cairo_destroy (devdata->stroke_ctx);
  if (devdata->strokebuffer)
    cairo_surface_destroy (devdata->strokebuffer);
  if (devdata->shape_points)
    g_array_free (devdata->shape_points, TRUE);
  frame_shape_clear (&devdata->frame_shape);

  g_free (devdata);
}

static gboolean
context_needs_stroke_layer (ScreenatorData *data,
			    ScreenatorPaintContext *context)
{
  return data->composited &&
	 context &&
	 context->type == SCREENATOR_PEN &&
	 context->paint_color->alpha < 1.0;
}

void stroke_begin (ScreenatorData *data, GdkDevice *dev)
{
  ScreenatorDeviceData *devdata = g_hash_table_lookup (data->devdatatable, dev);
  GdkRGBA color;

  if (!devdata || devdata->strokebuffer ||
      !context_needs_stroke_layer (data, devdata->cur_context))
    return;

  devdata->strokebuffer = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
						      data->width, data->height);
  devdata->stroke_ctx = cairo_create (devdata->strokebuffer);
  devdata->stroke_alpha = devdata->cur_context->paint_color->alpha;

  /* The layer itself is opaque; its alpha is applied when compositing. */
  color = *devdata->cur_context->paint_color;
  color.alpha = 1.0;
  gdk_cairo_set_source_rgba (devdata->stroke_ctx, &color);
  cairo_set_operator (devdata->stroke_ctx, CAIRO_OPERATOR_OVER);
  cairo_set_antialias (devdata->stroke_ctx, CAIRO_ANTIALIAS_DEFAULT);

  devdata->stroke_bounds.width = 0;
  devdata->stroke_bounds.height = 0;
}

void stroke_finish (ScreenatorData *data, GdkDevice *dev)
{
  ScreenatorDeviceData *devdata = g_hash_table_lookup (data->devdatatable, dev);
  cairo_t *cr;

  if (!devdata || !devdata->strokebuffer)
    return;

  cr = cairo_create (data->backbuffer);
  if (devdata->stroke_bounds.width > 0 && devdata->stroke_bounds.height > 0)
    {
      cairo_rectangle (cr, devdata->stroke_bounds.x, devdata->stroke_bounds.y,
		       devdata->stroke_bounds.width, devdata->stroke_bounds.height);
      cairo_clip (cr);
    }
  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
  cairo_set_source_surface (cr, devdata->strokebuffer, 0, 0);
  cairo_paint_with_alpha (cr, devdata->stroke_alpha);
  cairo_destroy (cr);

  cairo_destroy (devdata->stroke_ctx);
  cairo_surface_destroy (devdata->strokebuffer);
  devdata->strokebuffer = NULL;
  devdata->stroke_ctx = NULL;

  data->modified = 1;

  if (devdata->stroke_bounds.width > 0 && devdata->stroke_bounds.height > 0)
    gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
				&devdata->stroke_bounds, 0);
}

void stroke_finish_all (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;

  if (!data->devdatatable)
    return;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    stroke_finish (data, ((ScreenatorDeviceData *) value)->device);
}



void draw_line (ScreenatorData *data,
		GdkDevice *dev,
		gint x1, gint y1,
		gint x2, gint y2)
{
  GdkRectangle rect;
  ScreenatorDeviceData *devdata = g_hash_table_lookup(data->devdatatable, dev);
  /* Pen strokes are smoothed with quadratic curves between the midpoints of
   * consecutive motion samples.  The eraser and the shapes keep the plain
   * line drawing. */
  gboolean smooth = devdata && devdata->cur_context &&
		    devdata->cur_context->type == SCREENATOR_PEN &&
		    (x1 != x2 || y1 != y2);
  gint minx = MIN (x1, x2);
  gint miny = MIN (y1, y2);
  gint maxx = MAX (x1, x2);
  gint maxy = MAX (y1, y2);

  if (smooth && devdata->smooth_active)
    {
      minx = MIN (minx, (gint) devdata->smooth_x);
      miny = MIN (miny, (gint) devdata->smooth_y);
      maxx = MAX (maxx, (gint) devdata->smooth_x);
      maxy = MAX (maxy, (gint) devdata->smooth_y);
    }

  rect.x = minx - data->maxwidth / 2;
  rect.y = miny - data->maxwidth / 2;
  rect.width = maxx - minx + data->maxwidth;
  rect.height = maxy - miny + data->maxwidth;

  if(data->debug)
    g_printerr("DEBUG: draw line from %d %d to %d %d\n", x1, y1, x2, y2);

  if (devdata->cur_context->paint_ctx)
    {
      cairo_t *cr = devdata->stroke_ctx ? devdata->stroke_ctx
					: devdata->cur_context->paint_ctx;

      cairo_set_line_width(cr, data->maxwidth);
      cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
      cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
 
      if (smooth)
	{
	  gdouble midx = (x1 + x2) / 2.0;
	  gdouble midy = (y1 + y2) / 2.0;

	  if (!devdata->smooth_active)
	    {
	      devdata->smooth_x = x1;
	      devdata->smooth_y = y1;
	      devdata->smooth_active = TRUE;
	    }

	  cairo_move_to (cr, devdata->smooth_x, devdata->smooth_y);
	  cairo_curve_to (cr, x1, y1, x1, y1, midx, midy);
	  cairo_stroke (cr);

	  devdata->smooth_x = midx;
	  devdata->smooth_y = midy;
	  devdata->smooth_target_x = x2;
	  devdata->smooth_target_y = y2;
	}
      else
	{
	  cairo_move_to(cr, x1, y1);
	  cairo_line_to(cr, x2, y2);
	  cairo_stroke(cr);
	}

      if (devdata->stroke_ctx)
	{
	  /* Remember the area so it can be repainted once the layer has been
	   * composited onto the backbuffer. */
	  if (devdata->stroke_bounds.width > 0)
	    gdk_rectangle_union (&devdata->stroke_bounds, &rect,
				 &devdata->stroke_bounds);
	  else
	    devdata->stroke_bounds = rect;
	}

      data->modified = 1;

      gdk_window_invalidate_rect(gtk_widget_get_window(data->win), &rect, 0); 
    }

  data->painted = 1;
}


/* Finish the last half of a smoothed pen stroke.  The curve segments end at
 * the midpoint before the last sample, so draw the remaining piece as a
 * short straight line when the button is released. */
void draw_line_finish (ScreenatorData *data,
		       GdkDevice *dev)
{
  ScreenatorDeviceData *devdata = g_hash_table_lookup(data->devdatatable, dev);

  if (!devdata || !devdata->smooth_active)
    return;

  devdata->smooth_active = FALSE;

  if (!devdata->cur_context || !devdata->cur_context->paint_ctx)
    return;

  {
    cairo_t *cr = devdata->stroke_ctx ? devdata->stroke_ctx
				      : devdata->cur_context->paint_ctx;
    gint x1 = (gint) devdata->smooth_x;
    gint y1 = (gint) devdata->smooth_y;
    gint x2 = (gint) devdata->smooth_target_x;
    gint y2 = (gint) devdata->smooth_target_y;
    GdkRectangle rect;

    cairo_set_line_width (cr, data->maxwidth);
    cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);
    cairo_move_to (cr, x1, y1);
    cairo_line_to (cr, x2, y2);
    cairo_stroke (cr);

    rect.x = MIN (x1, x2) - data->maxwidth / 2;
    rect.y = MIN (y1, y2) - data->maxwidth / 2;
    rect.width = ABS (x1 - x2) + data->maxwidth;
    rect.height = ABS (y1 - y2) + data->maxwidth;

    if (devdata->stroke_ctx)
      {
	if (devdata->stroke_bounds.width > 0)
	  gdk_rectangle_union (&devdata->stroke_bounds, &rect,
			       &devdata->stroke_bounds);
	else
	  devdata->stroke_bounds = rect;
      }

    data->modified = 1;
    gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
				&rect, 0);
  }
}


void draw_arrow (ScreenatorData *data, 
		 GdkDevice *dev,
		 gint x1, gint y1,
		 gint width,
		 gfloat direction)
{
  GdkRectangle rect;
  GdkPoint arrowhead [4];

  /* get the data for this device */
  ScreenatorDeviceData *devdata = g_hash_table_lookup(data->devdatatable, dev);

  width = width / 2;

  /* I doubt that calculating the boundary box more exact is very useful */
  rect.x = x1 - 4 * width - 1;
  rect.y = y1 - 4 * width - 1;
  rect.width = 8 * width + 2;
  rect.height = 8 * width + 2;

  arrowhead [0].x = x1 + 4 * width * cos (direction);
  arrowhead [0].y = y1 + 4 * width * sin (direction);

  arrowhead [1].x = x1 - 3 * width * cos (direction)
                       + 3 * width * sin (direction);
  arrowhead [1].y = y1 - 3 * width * cos (direction)
                       - 3 * width * sin (direction);

  arrowhead [2].x = x1 - 2 * width * cos (direction);
  arrowhead [2].y = y1 - 2 * width * sin (direction);

  arrowhead [3].x = x1 - 3 * width * cos (direction)
                       - 3 * width * sin (direction);
  arrowhead [3].y = y1 + 3 * width * cos (direction)
                       - 3 * width * sin (direction);

  if (devdata->cur_context->paint_ctx)
    {
      cairo_set_line_width(devdata->cur_context->paint_ctx, 1);
      cairo_set_line_cap(devdata->cur_context->paint_ctx, CAIRO_LINE_CAP_ROUND);
      cairo_set_line_join(devdata->cur_context->paint_ctx, CAIRO_LINE_JOIN_ROUND);
 
      cairo_move_to(devdata->cur_context->paint_ctx, arrowhead[0].x, arrowhead[0].y);
      cairo_line_to(devdata->cur_context->paint_ctx, arrowhead[1].x, arrowhead[1].y);
      cairo_line_to(devdata->cur_context->paint_ctx, arrowhead[2].x, arrowhead[2].y);
      cairo_line_to(devdata->cur_context->paint_ctx, arrowhead[3].x, arrowhead[3].y);
      cairo_fill(devdata->cur_context->paint_ctx);

      gdk_cairo_set_source_rgba(devdata->cur_context->paint_ctx, data->black);

      cairo_move_to(devdata->cur_context->paint_ctx, arrowhead[0].x, arrowhead[0].y);
      cairo_line_to(devdata->cur_context->paint_ctx, arrowhead[1].x, arrowhead[1].y);
      cairo_line_to(devdata->cur_context->paint_ctx, arrowhead[2].x, arrowhead[2].y);
      cairo_line_to(devdata->cur_context->paint_ctx, arrowhead[3].x, arrowhead[3].y);
      cairo_line_to(devdata->cur_context->paint_ctx, arrowhead[0].x, arrowhead[0].y);
      cairo_stroke(devdata->cur_context->paint_ctx);

      gdk_cairo_set_source_rgba(devdata->cur_context->paint_ctx, devdata->cur_context->paint_color);
    
      data->modified = 1;

      gdk_window_invalidate_rect(gtk_widget_get_window(data->win), &rect, 0); 
    }

  data->painted = 1;
}




/* The line and the arrow share the same drawing code; the arrow additionally
 * gets a head at the end point. */
static gboolean
shape_is_line (ScreenatorShapeType type)
{
  return type == SCREENATOR_SHAPE_LINE ||
	 type == SCREENATOR_SHAPE_ARROW;
}


static gboolean
shape_has_arrow (ScreenatorShapeType type)
{
  return type == SCREENATOR_SHAPE_ARROW;
}


/* Arrow proportions, relative to w = pen width * 0.5 + ARROW_HEAD_EXTRA_WIDTH.
 * The extra width keeps a thin line's head easy to see, while the head still
 * grows with the line thickness. */
#define ARROW_HEAD_LENGTH 6.3
#define ARROW_HEAD_HALF_WIDTH 2.3
#define ARROW_HEAD_EXTRA_WIDTH 2.0

/* Draw an arrow as one filled polygon that contains both the shaft and the
 * head.  The head is a plain triangle: the shaft joins the middle of its base,
 * with no notch.  Building it as a single path keeps translucent strokes at
 * one opacity, avoids a seam between shaft and head, and leaves the tip as a
 * plain sharp corner instead of the round cap a stroked line would show. */
static void
shape_draw_arrow (cairo_t *cr, gint line_width,
		  gint x0, gint y0, gint x1, gint y1,
		  const GdkRGBA *color)
{
  gdouble direction = atan2 (y1 - y0, x1 - x0);
  gdouble w = MAX (1, line_width) * 0.5 + ARROW_HEAD_EXTRA_WIDTH;
  gdouble half = line_width / 2.0;
  gdouble ca = cos (direction);
  gdouble sa = sin (direction);
  gdouble length = hypot (x1 - x0, y1 - y0);
  gdouble base_x = x1 - ARROW_HEAD_LENGTH * w * ca;
  gdouble base_y = y1 - ARROW_HEAD_LENGTH * w * sa;
  gdouble barb = ARROW_HEAD_HALF_WIDTH * w;
  gdouble start_x = x0;
  gdouble start_y = y0;

  /* A very short drag still has to give a clean head: clamp the shaft to the
   * base of the triangle so the polygon does not turn inside out. */
  if (length <= ARROW_HEAD_LENGTH * w)
    {
      start_x = base_x;
      start_y = base_y;
    }

  /* The unit vector along the arrow is (ca, sa); the one across it is
   * (sa, -ca). */
  cairo_save (cr);
  cairo_new_path (cr);
  cairo_move_to (cr, start_x + half * sa, start_y - half * ca);
  cairo_line_to (cr, base_x + half * sa, base_y - half * ca);
  cairo_line_to (cr, base_x + barb * sa, base_y - barb * ca);
  cairo_line_to (cr, x1, y1);
  cairo_line_to (cr, base_x - barb * sa, base_y + barb * ca);
  cairo_line_to (cr, base_x - half * sa, base_y + half * ca);
  cairo_line_to (cr, start_x - half * sa, start_y + half * ca);
  cairo_close_path (cr);

  gdk_cairo_set_source_rgba (cr, color);
  cairo_fill (cr);
  cairo_restore (cr);
}


/* Draw a shape with the current pen width, colour and opacity on the given
 * cairo context.  Used both for the live preview and for committing the
 * shape to the backbuffer. */
void
draw_shape (ScreenatorData *data, cairo_t *cr, ScreenatorShapeType type,
	    gint x0, gint y0, gint x1, gint y1)
{
  GdkRGBA color = data->pen_color;
  gint line_width = MAX (1, (gint) data->pen_width);
  gint x, y, w, h;

  color.alpha = data->opacity;
  gdk_cairo_set_source_rgba (cr, &color);
  cairo_set_line_width (cr, line_width);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);

  if (shape_is_line (type))
    {
      if (x0 == x1 && y0 == y1)
	return;

      if (shape_has_arrow (type))
	{
	  shape_draw_arrow (cr, line_width, x0, y0, x1, y1, &color);
	  return;
	}

      cairo_move_to (cr, x0, y0);
      cairo_line_to (cr, x1, y1);
      cairo_stroke (cr);
      return;
    }

  x = MIN (x0, x1);
  y = MIN (y0, y1);
  w = ABS (x1 - x0);
  h = ABS (y1 - y0);

  /* The rounded rectangle has a corner radius the user can adjust after the
   * drag, so it is drawn by its own function. */
  if (type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE)
    {
      draw_rounded_rect (data, cr, x0, y0, x1, y1, MIN (w, h) / 4.0);
      return;
    }

  if (type == SCREENATOR_SHAPE_ELLIPSE)
    {
      if (w < 1 || h < 1)
	return;
      cairo_save (cr);
      cairo_translate (cr, x + w / 2.0, y + h / 2.0);
      cairo_scale (cr, w / 2.0, h / 2.0);
      cairo_arc (cr, 0, 0, 1, 0, 2 * G_PI);
      cairo_restore (cr);
    }
  else
    return;

  if (data->shape_fill_enabled)
    {
      GdkRGBA fill = data->shape_fill_color;

      fill.alpha = data->opacity;
      gdk_cairo_set_source_rgba (cr, &fill);
      cairo_fill_preserve (cr);
      gdk_cairo_set_source_rgba (cr, &color);
    }

  cairo_stroke (cr);
}


/* Bounding box of a shape, padded for the stroke width and, for arrows, for
 * the head at the end point. */
void
shape_get_bounds (ScreenatorData *data, ScreenatorShapeType type,
		  gint x0, gint y0, gint x1, gint y1,
		  GdkRectangle *bounds)
{
  gint pad = (gint) data->pen_width * 2 + 4;

  bounds->x = MIN (x0, x1) - pad;
  bounds->y = MIN (y0, y1) - pad;
  bounds->width = ABS (x1 - x0) + 2 * pad;
  bounds->height = ABS (y1 - y0) + 2 * pad;
}

/* Bounding box of an editable line or arrow, padded for the stroke, the arrow
 * head and the two end handles. */
void
line_get_bounds (ScreenatorData *data, const ScreenatorLine *line,
		 GdkRectangle *bounds)
{
  gint pad = (gint) data->pen_width * 6 + 16;

  shape_get_bounds (data, line->type, line->x0, line->y0,
		    line->x1, line->y1, bounds);

  bounds->x -= pad;
  bounds->y -= pad;
  bounds->width += 2 * pad;
  bounds->height += 2 * pad;
}

/* Commit one editable line or arrow to the backbuffer.  A click without a drag
 * leaves nothing behind. */
void
line_finish (ScreenatorData *data, ScreenatorDeviceData *devdata)
{
  GdkRectangle bounds;
  gboolean drawable;

  if (!devdata || !devdata->line_active)
    return;

  line_get_bounds (data, &devdata->line, &bounds);

  drawable = devdata->line.x0 != devdata->line.x1 ||
	     devdata->line.y0 != devdata->line.y1;

  devdata->line_active = FALSE;
  devdata->line_drag = SCREENATOR_LINE_HANDLE_NONE;

  if (drawable)
    {
      cairo_t *cr;

      snap_undo_state (data);
      cr = cairo_create (data->backbuffer);
      draw_shape (data, cr, devdata->line.type,
		  devdata->line.x0, devdata->line.y0,
		  devdata->line.x1, devdata->line.y1);
      cairo_destroy (cr);

      data->modified = 1;
      data->painted = 1;
    }

  if (data->win && bounds.width > 0 && bounds.height > 0)
    gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
				&bounds, 0);
}

/* Commit every editable line and arrow, e.g. when painting mode ends or
 * another tool is chosen. */
void
line_finish_all (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;

  if (!data->devdatatable)
    return;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    line_finish (data, value);
}


/* Build the rounded-rectangle path.  A radius of zero gives a sharp
 * rectangle; the radius is clamped to half the shorter side, where the shape
 * becomes a stadium. */
static void
rounded_rect_path (cairo_t *cr, gint x0, gint y0, gint x1, gint y1,
		   gdouble radius)
{
  gint x = MIN (x0, x1);
  gint y = MIN (y0, y1);
  gint w = ABS (x1 - x0);
  gint h = ABS (y1 - y0);
  gdouble max_radius = MIN (w, h) / 2.0;

  radius = CLAMP (radius, 0.0, max_radius);

  if (radius < 0.5)
    {
      cairo_rectangle (cr, x, y, w, h);
      return;
    }

  cairo_new_sub_path (cr);
  cairo_arc (cr, x + w - radius, y + radius, radius, -G_PI_2, 0);
  cairo_arc (cr, x + w - radius, y + h - radius, radius, 0, G_PI_2);
  cairo_arc (cr, x + radius, y + h - radius, radius, G_PI_2, G_PI);
  cairo_arc (cr, x + radius, y + radius, radius, G_PI, 3 * G_PI_2);
  cairo_close_path (cr);
}


/* Draw a rounded rectangle with an explicit corner radius.  Used for the
 * editable shape whose roundness handle changes the radius. */
void
draw_rounded_rect (ScreenatorData *data, cairo_t *cr,
		   gint x0, gint y0, gint x1, gint y1, gdouble radius)
{
  GdkRGBA color = data->pen_color;
  gint line_width = MAX (1, (gint) data->pen_width);
  gint w = ABS (x1 - x0);
  gint h = ABS (y1 - y0);

  if (w < 1 || h < 1)
    return;

  color.alpha = data->opacity;
  gdk_cairo_set_source_rgba (cr, &color);
  cairo_set_line_width (cr, line_width);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);

  rounded_rect_path (cr, x0, y0, x1, y1, radius);

  if (data->shape_fill_enabled)
    {
      GdkRGBA fill = data->shape_fill_color;

      fill.alpha = data->opacity;
      gdk_cairo_set_source_rgba (cr, &fill);
      cairo_fill_preserve (cr);
      gdk_cairo_set_source_rgba (cr, &color);
    }

  cairo_stroke (cr);
}


/* --- Editable frame shapes ------------------------------------------------
 *
 * The rectangle, ellipse, rounded rectangle, text, math plot and parallel
 * lines all share one frame: a centre, a width, a height and a rotation.  A
 * placed shape stays editable until it is committed, so it can still be
 * moved, resized and rotated with its handles.
 */

/* Distance between the top edge and the rotation handle. */
#define FRAME_HANDLE_OFFSET 32.0
#define FRAME_HANDLE_MARGIN 64

/* Release the strings owned by a frame shape and reset it. */
void
frame_shape_clear (ScreenatorFrameShape *shape)
{
  if (!shape)
    return;

  g_free (shape->expression);
  g_free (shape->text);
  g_free (shape->font);
  memset (shape, 0, sizeof (*shape));
}

/* Local (unrotated) frame coordinates to screen coordinates. */
void
frame_local_to_screen (const ScreenatorFrame *frame, gdouble lx, gdouble ly,
		       gdouble *x, gdouble *y)
{
  gdouble c = cos (frame->rotation);
  gdouble s = sin (frame->rotation);

  *x = frame->cx + lx * c - ly * s;
  *y = frame->cy + lx * s + ly * c;
}

/* Screen coordinates to local frame coordinates. */
void
frame_screen_to_local (const ScreenatorFrame *frame, gdouble x, gdouble y,
		       gdouble *lx, gdouble *ly)
{
  gdouble c = cos (frame->rotation);
  gdouble s = sin (frame->rotation);
  gdouble dx = x - frame->cx;
  gdouble dy = y - frame->cy;

  *lx = dx * c + dy * s;
  *ly = -dx * s + dy * c;
}

/* Font size of a Pango description in pixels, so text can be scaled by the
 * size handle.  Pango reports point sizes unless they are marked absolute. */
gdouble
frame_font_pixel_size (const gchar *font)
{
  PangoFontDescription *desc =
    pango_font_description_from_string (font ? font : KEY_DFLT_FONT);
  gdouble size;

  if (pango_font_description_get_size_is_absolute (desc))
    size = pango_font_description_get_size (desc) / (gdouble) PANGO_SCALE;
  else
    size = pango_font_description_get_size (desc) / (gdouble) PANGO_SCALE *
      96.0 / 72.0;

  pango_font_description_free (desc);

  return MAX (size, 1.0);
}

/* Build the Pango layout of a text frame at its current font size. */
static PangoLayout *
frame_text_layout (cairo_t *cr, const ScreenatorFrameShape *shape)
{
  PangoLayout *layout = pango_cairo_create_layout (cr);
  PangoFontDescription *desc =
    pango_font_description_from_string (shape->font ? shape->font
						   : KEY_DFLT_FONT);

  if (shape->font_size > 0.0)
    pango_font_description_set_absolute_size (desc,
					      shape->font_size * PANGO_SCALE);

  pango_layout_set_text (layout, shape->text ? shape->text : "", -1);
  pango_layout_set_font_description (layout, desc);
  pango_font_description_free (desc);

  return layout;
}

/* Measure a text frame and update its width and height.  The ink extents are
 * used so the frame hugs the glyphs; the ink offset is applied when the text
 * is drawn. */
void
frame_shape_measure_text (ScreenatorFrameShape *shape)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  PangoLayout *layout;
  PangoRectangle ink;

  if (!shape || shape->type != SCREENATOR_SHAPE_TEXT)
    return;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 1, 1);
  cr = cairo_create (surface);
  layout = frame_text_layout (cr, shape);
  pango_layout_get_pixel_extents (layout, &ink, NULL);

  shape->frame.w = MAX (ink.width, 1);
  shape->frame.h = MAX (ink.height, 1);
  shape->text_off_x = ink.x;
  shape->text_off_y = ink.y;

  g_object_unref (layout);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);
}

/* Position of a frame handle in screen coordinates. */
void
frame_handle_position (const ScreenatorFrameShape *shape,
		       ScreenatorFrameHandle handle, GdkPoint *pos)
{
  const ScreenatorFrame *frame = &shape->frame;
  gdouble lx = 0.0;
  gdouble ly = 0.0;
  gdouble x, y;

  switch (handle)
    {
    case SCREENATOR_FRAME_HANDLE_SIZE_TL:
      lx = -frame->w / 2.0;
      ly = -frame->h / 2.0;
      break;
    case SCREENATOR_FRAME_HANDLE_SIZE_TR:
      lx = frame->w / 2.0;
      ly = -frame->h / 2.0;
      break;
    case SCREENATOR_FRAME_HANDLE_SIZE_BL:
      lx = -frame->w / 2.0;
      ly = frame->h / 2.0;
      break;
    case SCREENATOR_FRAME_HANDLE_SIZE_BR:
      lx = frame->w / 2.0;
      ly = frame->h / 2.0;
      break;
    case SCREENATOR_FRAME_HANDLE_ROTATE:
      ly = -(frame->h / 2.0 + FRAME_HANDLE_OFFSET);
      break;
    case SCREENATOR_FRAME_HANDLE_EXTRA:
      if (shape->type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE)
	{
	  lx = -frame->w / 2.0 +
	    CLAMP (shape->extra, 0.0, MIN (frame->w, frame->h) / 2.0);
	  ly = -frame->h / 2.0;
	}
      else if (shape->type == SCREENATOR_SHAPE_BARS)
	lx = -(frame->w / 2.0 + FRAME_HANDLE_OFFSET);
      break;
    default:
      break;
    }

  frame_local_to_screen (frame, lx, ly, &x, &y);
  pos->x = (gint) round (x);
  pos->y = (gint) round (y);
}

/* --- Math expression parser ----------------------------------------------
 *
 * The Math tool takes a small expression, e.g. "cos(2*pi*x)" in rectangular
 * mode or "cos(3*theta)" in polar mode.  The parser below understands
 * numbers, the variables x and theta, the constants pi and e, the operators
 * + - * / ^, parentheses and a set of common functions.  Multiplication can
 * be written explicitly or implied ("2*pi*x", "2pi" or "2(x+1)").
 */

#ifndef M_E
#define M_E 2.7182818284590452354
#endif

typedef enum
{
  MATH_NODE_NUMBER,
  MATH_NODE_VARIABLE,   /* x, the rectangular plot's input */
  MATH_NODE_THETA,      /* theta, the polar plot's angle */
  MATH_NODE_ADD,
  MATH_NODE_SUB,
  MATH_NODE_MUL,
  MATH_NODE_DIV,
  MATH_NODE_POW,
  MATH_NODE_NEG,
  MATH_NODE_FUNCTION
} MathNodeType;

typedef struct MathNode
{
  MathNodeType     type;
  gdouble          number;
  gint             function;
  struct MathNode *left;
  struct MathNode *right;
} MathNode;

typedef struct
{
  const gchar *name;
  gdouble    (*fn) (gdouble);
} MathFunctionDef;

/* The unit step is 0 for negative values and 1 from zero upwards.  The Dirac
 * delta is zero everywhere in ordinary sampling; the plot code finds where
 * its argument vanishes and draws the spike as an arrow. */
static gdouble
math_unit (gdouble value)
{
  return value < 0.0 ? 0.0 : 1.0;
}

static gdouble
math_delta (gdouble value)
{
  (void) value;
  return 0.0;
}

/* Rectangle pulse: one for |value| <= 1/2 and zero elsewhere. */
static gdouble
math_rect (gdouble value)
{
  return (value >= -0.5 && value <= 0.5) ? 1.0 : 0.0;
}

static const MathFunctionDef math_functions[] =
{
  { "sin", sin }, { "cos", cos }, { "tan", tan },
  { "asin", asin }, { "acos", acos }, { "atan", atan },
  { "sinh", sinh }, { "cosh", cosh }, { "tanh", tanh },
  { "sqrt", sqrt }, { "abs", fabs }, { "exp", exp },
  { "log", log }, { "ln", log }, { "log10", log10 },
  { "floor", floor }, { "ceil", ceil },
  { "unit", math_unit }, { "delta", math_delta },
  { "rect", math_rect }
};

typedef struct
{
  const gchar *text;
  const gchar *pos;
  gchar       *error;
} MathParser;

static MathNode *math_parse_expression (MathParser *parser);
static MathNode *math_parse_unary (MathParser *parser);

static MathNode *
math_node_new (MathNodeType type, MathNode *left, MathNode *right)
{
  MathNode *node = g_new0 (MathNode, 1);

  node->type = type;
  node->left = left;
  node->right = right;

  return node;
}

static void
math_node_free (MathNode *node)
{
  if (!node)
    return;

  math_node_free (node->left);
  math_node_free (node->right);
  g_free (node);
}

static void
math_parser_error (MathParser *parser, const gchar *format, ...)
{
  va_list args;

  if (parser->error)
    return;

  va_start (args, format);
  parser->error = g_strdup_vprintf (format, args);
  va_end (args);
}

static void
math_skip_space (MathParser *parser)
{
  while (g_ascii_isspace (*parser->pos))
    parser->pos++;
}

static gboolean
math_starts_primary (gchar c)
{
  return g_ascii_isdigit (c) || c == '.' || c == '(' ||
	 g_ascii_isalpha (c) || c == '_';
}

static MathNode *
math_parse_primary (MathParser *parser)
{
  MathNode *node;

  math_skip_space (parser);

  if (*parser->pos == '(')
    {
      parser->pos++;
      node = math_parse_expression (parser);
      math_skip_space (parser);

      if (*parser->pos != ')')
	{
	  math_parser_error (parser, "Expected ')'.");
	  math_node_free (node);
	  return NULL;
	}
      parser->pos++;
      return node;
    }

  if (g_ascii_isdigit (*parser->pos) || *parser->pos == '.')
    {
      gchar *end;

      node = math_node_new (MATH_NODE_NUMBER, NULL, NULL);
      node->number = g_ascii_strtod (parser->pos, &end);

      if (end == parser->pos)
	{
	  math_parser_error (parser, "Invalid number.");
	  math_node_free (node);
	  return NULL;
	}

      parser->pos = end;
      return node;
    }

  if (g_ascii_isalpha (*parser->pos) || *parser->pos == '_')
    {
      const gchar *start = parser->pos;
      gchar *name;
      guint i;

      while (g_ascii_isalnum (*parser->pos) || *parser->pos == '_')
	parser->pos++;

      name = g_ascii_strdown (start, parser->pos - start);

      if (strcmp (name, "x") == 0)
	{
	  g_free (name);
	  return math_node_new (MATH_NODE_VARIABLE, NULL, NULL);
	}
      if (strcmp (name, "theta") == 0)
	{
	  g_free (name);
	  return math_node_new (MATH_NODE_THETA, NULL, NULL);
	}
      if (strcmp (name, "pi") == 0)
	{
	  g_free (name);
	  node = math_node_new (MATH_NODE_NUMBER, NULL, NULL);
	  node->number = G_PI;
	  return node;
	}
      if (strcmp (name, "e") == 0)
	{
	  g_free (name);
	  node = math_node_new (MATH_NODE_NUMBER, NULL, NULL);
	  node->number = M_E;
	  return node;
	}

      for (i = 0; i < G_N_ELEMENTS (math_functions); i++)
	if (strcmp (name, math_functions[i].name) == 0)
	  break;

      if (i == G_N_ELEMENTS (math_functions))
	{
	  math_parser_error (parser, "Unknown name \"%s\".", name);
	  g_free (name);
	  return NULL;
	}

      g_free (name);

      math_skip_space (parser);
      if (*parser->pos != '(')
	{
	  math_parser_error (parser, "Expected '(' after %s.",
			     math_functions[i].name);
	  return NULL;
	}
      parser->pos++;

      node = math_parse_expression (parser);
      math_skip_space (parser);

      if (*parser->pos != ')')
	{
	  math_parser_error (parser, "Expected ')'.");
	  math_node_free (node);
	  return NULL;
	}
      parser->pos++;

      {
	MathNode *call = math_node_new (MATH_NODE_FUNCTION, node, NULL);

	call->function = i;
	return call;
      }
    }

  if (*parser->pos == '\0')
    math_parser_error (parser, "The expression is incomplete.");
  else
    math_parser_error (parser, "Unexpected \"%c\".", *parser->pos);

  return NULL;
}

static MathNode *
math_parse_power (MathParser *parser)
{
  MathNode *left = math_parse_primary (parser);
  MathNode *right;

  if (!left)
    return NULL;

  math_skip_space (parser);
  if (*parser->pos != '^')
    return left;

  parser->pos++;
  right = math_parse_unary (parser);
  if (!right)
    {
      math_node_free (left);
      return NULL;
    }

  return math_node_new (MATH_NODE_POW, left, right);
}

static MathNode *
math_parse_unary (MathParser *parser)
{
  MathNode *node;

  math_skip_space (parser);

  if (*parser->pos == '+')
    {
      parser->pos++;
      return math_parse_unary (parser);
    }
  if (*parser->pos == '-')
    {
      parser->pos++;
      node = math_parse_unary (parser);
      if (!node)
	return NULL;
      return math_node_new (MATH_NODE_NEG, node, NULL);
    }

  return math_parse_power (parser);
}

static MathNode *
math_parse_term (MathParser *parser)
{
  MathNode *left = math_parse_unary (parser);
  MathNode *right;

  if (!left)
    return NULL;

  for (;;)
    {
      math_skip_space (parser);

      if (*parser->pos == '*')
	{
	  parser->pos++;
	  right = math_parse_unary (parser);
	  if (!right)
	    {
	      math_node_free (left);
	      return NULL;
	    }
	  left = math_node_new (MATH_NODE_MUL, left, right);
	}
      else if (*parser->pos == '/')
	{
	  parser->pos++;
	  right = math_parse_unary (parser);
	  if (!right)
	    {
	      math_node_free (left);
	      return NULL;
	    }
	  left = math_node_new (MATH_NODE_DIV, left, right);
	}
      else if (math_starts_primary (*parser->pos))
	{
	  /* Implied multiplication: 2pi, 2(x+1), x sin(x). */
	  right = math_parse_unary (parser);
	  if (!right)
	    {
	      math_node_free (left);
	      return NULL;
	    }
	  left = math_node_new (MATH_NODE_MUL, left, right);
	}
      else
	break;
    }

  return left;
}

static MathNode *
math_parse_expression (MathParser *parser)
{
  MathNode *left = math_parse_term (parser);
  MathNode *right;

  if (!left)
    return NULL;

  for (;;)
    {
      math_skip_space (parser);

      if (*parser->pos == '+')
	{
	  parser->pos++;
	  right = math_parse_term (parser);
	  if (!right)
	    {
	      math_node_free (left);
	      return NULL;
	    }
	  left = math_node_new (MATH_NODE_ADD, left, right);
	}
      else if (*parser->pos == '-')
	{
	  parser->pos++;
	  right = math_parse_term (parser);
	  if (!right)
	    {
	      math_node_free (left);
	      return NULL;
	    }
	  left = math_node_new (MATH_NODE_SUB, left, right);
	}
      else
	break;
    }

  return left;
}

/* Parse an expression into a tree.  On failure NULL is returned and, when
 * "error" is not NULL, it receives a newly allocated message. */
static MathNode *
math_parse (const gchar *expression, gchar **error)
{
  MathParser parser;
  MathNode *root;

  if (error)
    *error = NULL;

  if (!expression || !*expression)
    {
      if (error)
	*error = g_strdup ("Enter a function of x.");
      return NULL;
    }

  parser.text = expression;
  parser.pos = expression;
  parser.error = NULL;

  root = math_parse_expression (&parser);

  if (root && !parser.error)
    {
      math_skip_space (&parser);
      if (*parser.pos != '\0')
	math_parser_error (&parser, "Unexpected \"%c\".", *parser.pos);
    }

  if (parser.error)
    {
      math_node_free (root);
      root = NULL;
      if (error)
	*error = parser.error;
      else
	g_free (parser.error);
    }

  return root;
}

/* Check an expression for the Math Function dialog. */
gboolean
math_expression_is_valid (const gchar *expression, gchar **error)
{
  MathNode *root = math_parse (expression, error);

  if (!root)
    return FALSE;

  math_node_free (root);
  return TRUE;
}

static gdouble
math_node_eval (const MathNode *node, gdouble x, gdouble theta)
{
  switch (node->type)
    {
    case MATH_NODE_NUMBER:
      return node->number;
    case MATH_NODE_VARIABLE:
      return x;
    case MATH_NODE_THETA:
      return theta;
    case MATH_NODE_ADD:
      return math_node_eval (node->left, x, theta) +
	math_node_eval (node->right, x, theta);
    case MATH_NODE_SUB:
      return math_node_eval (node->left, x, theta) -
	math_node_eval (node->right, x, theta);
    case MATH_NODE_MUL:
      return math_node_eval (node->left, x, theta) *
	math_node_eval (node->right, x, theta);
    case MATH_NODE_DIV:
      return math_node_eval (node->left, x, theta) /
	math_node_eval (node->right, x, theta);
    case MATH_NODE_POW:
      return pow (math_node_eval (node->left, x, theta),
		  math_node_eval (node->right, x, theta));
    case MATH_NODE_NEG:
      return -math_node_eval (node->left, x, theta);
    case MATH_NODE_FUNCTION:
      return math_functions[node->function].fn (
	math_node_eval (node->left, x, theta));
    }

  return NAN;
}

/* Evaluate the expression for one sample of the active variable. */
static gdouble
math_eval_variable (const MathNode *root, gdouble value, gboolean polar)
{
  return polar ? math_node_eval (root, 0.0, value)
	       : math_node_eval (root, value, 0.0);
}

/* Evaluate the expression at x.  A point where it is undefined may still have
 * a limit: sin(x)/x is 0/0 at x = 0 but tends to 1.  In that case the value is
 * sampled on both sides at shrinking distances and the value they agree on is
 * used.  A real pole, such as 1/x or tan(x), does not settle on one value, so
 * NaN is returned and the curve is broken there. */
static gdouble
math_eval_at (const MathNode *root, gdouble value, gdouble span,
	      gboolean polar)
{
  gdouble y = math_eval_variable (root, value, polar);
  gdouble h;
  gint i;

  if (isfinite (y))
    return y;

  h = span * 1e-4;

  for (i = 0; i < 8; i++)
    {
      gdouble left = math_eval_variable (root, value - h, polar);
      gdouble right = math_eval_variable (root, value + h, polar);

      if (isfinite (left) && isfinite (right))
	{
	  gdouble scale = MAX (1.0, MAX (fabs (left), fabs (right)));

	  /* Both sides meet, so the hole can be filled in. */
	  if (fabs (left - right) <= 1e-6 * scale)
	    return (left + right) / 2.0;
	}

      h *= 0.1;
    }

  return NAN;
}

static gint
compare_doubles (gconstpointer a, gconstpointer b)
{
  gdouble da = *(const gdouble *) a;
  gdouble db = *(const gdouble *) b;

  if (da < db)
    return -1;
  if (da > db)
    return 1;
  return 0;
}

/* Number of samples used to draw a math plot. */
#define MATH_SAMPLES 320

/* Sample the active function over the stored range.  out[] receives
 * MATH_SAMPLES + 1 values, with NaN where the function is undefined, and the
 * return value is the largest magnitude.  A pole can be orders of magnitude
 * larger than the rest of the curve, so the range is capped near the median
 * to keep the shape readable.  A point that is undefined but has a limit,
 * such as sin(x)/x at zero, is filled in by math_eval_at(). */
static gdouble
math_sample (const ScreenatorFrameShape *shape, gdouble *out, gboolean polar)
{
  gdouble values[MATH_SAMPLES + 1];
  gdouble lo = shape->xmin;
  gdouble hi = shape->xmax;
  gdouble span, maxabs = 0.0, median_abs;
  MathNode *root;
  gchar *error = NULL;
  gint n = 0;
  gint i;

  if (!(hi > lo))
    return 0.0;

  root = math_parse (shape->expression, &error);
  g_free (error);

  if (!root)
    return 0.0;

  span = hi - lo;

  for (i = 0; i <= MATH_SAMPLES; i++)
    {
      gdouble value = lo + span * i / MATH_SAMPLES;
      gdouble y = math_eval_at (root, value, span, polar);

      out[i] = y;

      if (isfinite (y))
	{
	  values[n++] = fabs (y);
	  maxabs = MAX (maxabs, fabs (y));
	}
    }

  math_node_free (root);

  /* A constant zero still draws a line in the middle of the frame. */
  if (maxabs < 1e-9)
    maxabs = 1.0;

  if (n > 1)
    {
      gdouble *sorted = g_new (gdouble, n);

      memcpy (sorted, values, n * sizeof (gdouble));
      qsort (sorted, n, sizeof (gdouble), compare_doubles);
      median_abs = sorted[n / 2];
      g_free (sorted);

      if (median_abs > 1e-9 && maxabs > 20.0 * median_abs)
	maxabs = 20.0 * median_abs;
    }

  return maxabs;
}

/* TRUE when the node is a call to the named function. */
static gboolean
math_node_is_function (const MathNode *node, const gchar *name)
{
  return node && node->type == MATH_NODE_FUNCTION &&
	 strcmp (math_functions[node->function].name, name) == 0;
}

/* Add a delta root once, so a constant argument or two identical deltas do
 * not draw several arrows in the same place. */
static void
math_add_delta_root (GArray *roots, gdouble root, gdouble tolerance)
{
  guint i;

  for (i = 0; i < roots->len; i++)
    if (fabs (g_array_index (roots, gdouble, i) - root) <= tolerance)
      return;

  g_array_append_val (roots, root);
}

/* Collect the values of the active variable where a delta() argument is
 * zero.  A sign change between two samples is interpolated; a run of exact
 * zeros, such as delta(0), contributes one root at the middle of the run. */
static void
math_collect_delta_roots (const MathNode *node, gdouble lo, gdouble span,
			  gboolean polar, GArray *roots)
{
  if (!node)
    return;

  if (math_node_is_function (node, "delta"))
    {
      gdouble value = lo;
      gdouble arg = math_eval_variable (node->left, value, polar);
      gdouble zero_start = lo;
      gboolean in_zero = isfinite (arg) && arg == 0.0;
      gint i;

      for (i = 1; i <= MATH_SAMPLES; i++)
	{
	  gdouble next_value = lo + span * i / MATH_SAMPLES;
	  gdouble next_arg =
	    math_eval_variable (node->left, next_value, polar);

	  if (isfinite (next_arg) && next_arg == 0.0)
	    {
	      if (!in_zero)
		{
		  in_zero = TRUE;
		  zero_start = next_value;
		}
	    }
	  else
	    {
	      if (in_zero)
		{
		  gdouble root = (zero_start + value) / 2.0;

		  math_add_delta_root (roots, root, span * 1e-6);
		  in_zero = FALSE;
		}

	      if (isfinite (arg) && isfinite (next_arg) &&
		  arg != 0.0 && next_arg != 0.0 &&
		  ((arg < 0.0 && next_arg > 0.0) ||
		   (arg > 0.0 && next_arg < 0.0)))
		{
		  gdouble root = value +
		    (0.0 - arg) / (next_arg - arg) *
		    (next_value - value);

		  math_add_delta_root (roots, root, span * 1e-6);
		}
	    }

	  value = next_value;
	  arg = next_arg;
	}

      if (in_zero)
	{
	  gdouble root = (zero_start + value) / 2.0;

	  math_add_delta_root (roots, root, span * 1e-6);
	}

      /* A delta inside a delta is not meaningful; the argument has already
       * been sampled above. */
      return;
    }

  math_collect_delta_roots (node->left, lo, span, polar, roots);
  math_collect_delta_roots (node->right, lo, span, polar, roots);
}

/* Draw a V-shaped arrow head at the tip, pointing along (ux, uy). */
static void
math_draw_arrow_head (cairo_t *cr, gdouble tip_x, gdouble tip_y,
		      gdouble ux, gdouble uy, gdouble size)
{
  gdouble px = -uy;
  gdouble py = ux;
  gdouble base_x = tip_x - ux * size;
  gdouble base_y = tip_y - uy * size;
  gdouble half = size * 0.55;

  cairo_move_to (cr, tip_x, tip_y);
  cairo_line_to (cr, base_x + px * half, base_y + py * half);
  cairo_move_to (cr, tip_x, tip_y);
  cairo_line_to (cr, base_x - px * half, base_y - py * half);
}

/* Draw the spikes of every delta() in the expression.  A rectangular plot
 * gets a vertical arrow at the zero of the argument; a polar plot gets a
 * radial arrow at that angle. */
static void
frame_draw_math_deltas (ScreenatorData *data, cairo_t *cr,
			const ScreenatorFrameShape *shape)
{
  GArray *roots;
  MathNode *root;
  gchar *error = NULL;
  GdkRGBA color = data->pen_color;
  gdouble lo = shape->xmin;
  gdouble span = shape->xmax - shape->xmin;
  gdouble line_width;
  gdouble head;
  gdouble max_head;
  guint i;

  if (!(span > 0.0))
    return;

  root = math_parse (shape->expression, &error);
  g_free (error);

  if (!root)
    return;

  roots = g_array_new (FALSE, FALSE, sizeof (gdouble));
  math_collect_delta_roots (root, lo, span, shape->polar, roots);
  math_node_free (root);

  if (roots->len == 0)
    {
      g_array_free (roots, TRUE);
      return;
    }

  color.alpha = data->opacity;
  gdk_cairo_set_source_rgba (cr, &color);
  line_width = MAX (1, (gint) data->pen_width);
  cairo_set_line_width (cr, line_width);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);

  head = CLAMP (line_width * 4.0, 8.0, 18.0);
  max_head = shape->polar
    ? MIN (shape->frame.w, shape->frame.h) * 0.25
    : shape->frame.h * 0.25;
  head = MIN (head, max_head);

  for (i = 0; i < roots->len; i++)
    {
      gdouble value = g_array_index (roots, gdouble, i);

      if (shape->polar)
	{
	  gdouble radius = MIN (shape->frame.w, shape->frame.h) / 2.0 * 0.9;
	  gdouble ux = cos (value);
	  gdouble uy = sin (value);
	  gdouble tip_x = radius * ux;
	  gdouble tip_y = radius * uy;

	  cairo_move_to (cr, 0.0, 0.0);
	  cairo_line_to (cr, tip_x, tip_y);
	  math_draw_arrow_head (cr, tip_x, tip_y, ux, uy, head);
	}
      else
	{
	  gdouble x = -shape->frame.w / 2.0 +
	    ((value - lo) / span) * shape->frame.w;
	  gdouble tip_y = -shape->frame.h / 2.0 * 0.9;

	  cairo_move_to (cr, x, 0.0);
	  cairo_line_to (cr, x, tip_y);
	  math_draw_arrow_head (cr, x, tip_y, 0.0, -1.0, head);
	}
    }

  cairo_stroke (cr);
  g_array_free (roots, TRUE);
}

/* Plot y = f(x).  The x range comes from the Math Function dialog and the y
 * range is scaled to fill the frame. */
static void
frame_draw_rect_math (ScreenatorData *data, cairo_t *cr,
		      const ScreenatorFrameShape *shape)
{
  gdouble ys[MATH_SAMPLES + 1];
  gdouble maxabs = math_sample (shape, ys, FALSE);
  gdouble sy;
  GdkRGBA color = data->pen_color;
  gboolean pen_down = FALSE;
  gint i;

  if (shape->frame.w < 1.0 || shape->frame.h < 1.0 || maxabs <= 0.0)
    return;

  sy = (shape->frame.h / 2.0) / maxabs;

  color.alpha = data->opacity;
  gdk_cairo_set_source_rgba (cr, &color);
  cairo_set_line_width (cr, MAX (1, (gint) data->pen_width));
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);

  for (i = 0; i <= MATH_SAMPLES; i++)
    {
      gdouble lx = -shape->frame.w / 2.0 +
	(i / (gdouble) MATH_SAMPLES) * shape->frame.w;
      gdouble ly = -ys[i] * sy;

      /* Break the path at undefined points and at poles. */
      if (!isfinite (ys[i]) || fabs (ys[i]) > maxabs * 1.02)
	{
	  pen_down = FALSE;
	  continue;
	}

      if (!pen_down)
	{
	  cairo_move_to (cr, lx, ly);
	  pen_down = TRUE;
	}
      else
	cairo_line_to (cr, lx, ly);
    }

  cairo_stroke (cr);
  frame_draw_math_deltas (data, cr, shape);
}

/* Plot r = f(theta) in polar coordinates.  The angle runs over the stored
 * theta range, and one scale is used for both axes so the curve stays round
 * even when the frame is not square. */
static void
frame_draw_polar_math (ScreenatorData *data, cairo_t *cr,
		       const ScreenatorFrameShape *shape)
{
  gdouble rs[MATH_SAMPLES + 1];
  gdouble maxabs = math_sample (shape, rs, TRUE);
  gdouble lo = shape->xmin;
  gdouble span = shape->xmax - shape->xmin;
  gdouble scale;
  GdkRGBA color = data->pen_color;
  gboolean pen_down = FALSE;
  gint i;

  if (shape->frame.w < 1.0 || shape->frame.h < 1.0 || maxabs <= 0.0)
    return;

  scale = MIN (shape->frame.w, shape->frame.h) / 2.0 / maxabs;

  color.alpha = data->opacity;
  gdk_cairo_set_source_rgba (cr, &color);
  cairo_set_line_width (cr, MAX (1, (gint) data->pen_width));
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);

  for (i = 0; i <= MATH_SAMPLES; i++)
    {
      gdouble theta = lo + span * i / MATH_SAMPLES;
      gdouble r = rs[i];
      gdouble lx, ly;

      /* Break the path at undefined points and at poles. */
      if (!isfinite (r) || fabs (r) > maxabs * 1.02)
	{
	  pen_down = FALSE;
	  continue;
	}

      lx = r * cos (theta) * scale;
      ly = r * sin (theta) * scale;

      if (!pen_down)
	{
	  cairo_move_to (cr, lx, ly);
	  pen_down = TRUE;
	}
      else
	cairo_line_to (cr, lx, ly);
    }

  cairo_stroke (cr);
  frame_draw_math_deltas (data, cr, shape);
}

/* Draw the Math tool's plot in the selected coordinate system. */
static void
frame_draw_math (ScreenatorData *data, cairo_t *cr,
		 const ScreenatorFrameShape *shape)
{
  if (shape->polar)
    frame_draw_polar_math (data, cr, shape);
  else
    frame_draw_rect_math (data, cr, shape);
}

/* Draw the parallel lines: the frame width is divided evenly between the
 * lines, which run vertically, and the current line thickness is used for
 * every one of them. */
static void
frame_draw_bars (ScreenatorData *data, cairo_t *cr,
		 const ScreenatorFrameShape *shape)
{
  GdkRGBA color = data->pen_color;
  gint count = CLAMP (shape->count, 1, 40);
  gint i;

  if (shape->frame.w < 1.0 || shape->frame.h < 1.0)
    return;

  color.alpha = data->opacity;
  gdk_cairo_set_source_rgba (cr, &color);
  cairo_set_line_width (cr, MAX (1, (gint) data->pen_width));
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);

  for (i = 0; i < count; i++)
    {
      gdouble lx = -shape->frame.w / 2.0 +
	(i + 0.5) * shape->frame.w / count;

      cairo_move_to (cr, lx, -shape->frame.h / 2.0);
      cairo_line_to (cr, lx, shape->frame.h / 2.0);
    }

  cairo_stroke (cr);
}

/* Draw a text frame at its rotation and font size. */
static void
frame_draw_text (ScreenatorData *data, cairo_t *cr,
		 const ScreenatorFrameShape *shape)
{
  PangoLayout *layout;
  GdkRGBA color = shape->color;

  if (!shape->text || !*shape->text)
    return;

  layout = frame_text_layout (cr, shape);

  color.alpha *= data->opacity;
  gdk_cairo_set_source_rgba (cr, &color);

  cairo_move_to (cr,
		 -shape->frame.w / 2.0 - shape->text_off_x,
		 -shape->frame.h / 2.0 - shape->text_off_y);
  pango_cairo_show_layout (cr, layout);

  g_object_unref (layout);
}

/* Draw a placed frame shape.  The cairo context is rotated around the frame
 * centre, so every shape is drawn in local coordinates. */
void
draw_frame_shape (ScreenatorData *data, cairo_t *cr,
		  const ScreenatorFrameShape *shape)
{
  const ScreenatorFrame *frame;
  GdkRGBA color = data->pen_color;
  gint line_width = MAX (1, (gint) data->pen_width);

  if (!shape)
    return;

  frame = &shape->frame;

  if ((shape->type == SCREENATOR_SHAPE_ELLIPSE ||
       shape->type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE) &&
      (frame->w < 1.0 || frame->h < 1.0))
    return;

  cairo_save (cr);
  cairo_translate (cr, frame->cx, frame->cy);
  cairo_rotate (cr, frame->rotation);

  switch (shape->type)
    {
    case SCREENATOR_SHAPE_MATH:
      frame_draw_math (data, cr, shape);
      cairo_restore (cr);
      return;
    case SCREENATOR_SHAPE_BARS:
      frame_draw_bars (data, cr, shape);
      cairo_restore (cr);
      return;
    case SCREENATOR_SHAPE_TEXT:
      frame_draw_text (data, cr, shape);
      cairo_restore (cr);
      return;
    default:
      break;
    }

  color.alpha = data->opacity;
  gdk_cairo_set_source_rgba (cr, &color);
  cairo_set_line_width (cr, line_width);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);

  if (shape->type == SCREENATOR_SHAPE_ELLIPSE)
    {
      cairo_save (cr);
      cairo_scale (cr, frame->w / 2.0, frame->h / 2.0);
      cairo_arc (cr, 0, 0, 1, 0, 2 * G_PI);
      cairo_restore (cr);
    }
  else if (shape->type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE)
    rounded_rect_path (cr,
		       (gint) round (-frame->w / 2.0),
		       (gint) round (-frame->h / 2.0),
		       (gint) round (frame->w / 2.0),
		       (gint) round (frame->h / 2.0),
		       shape->extra);
  else
    {
      cairo_restore (cr);
      return;
    }

  if (data->shape_fill_enabled)
    {
      GdkRGBA fill = data->shape_fill_color;

      fill.alpha = data->opacity;
      gdk_cairo_set_source_rgba (cr, &fill);
      cairo_fill_preserve (cr);
      gdk_cairo_set_source_rgba (cr, &color);
    }

  cairo_stroke (cr);
  cairo_restore (cr);
}

/* Bounding box of a frame shape, padded for the stroke width and for the
 * handles.  A circle around the frame keeps the maths simple and only makes
 * the invalidated area a little larger. */
void
frame_shape_get_bounds (const ScreenatorFrameShape *shape, gint pad,
			GdkRectangle *bounds)
{
  gdouble r;

  if (!shape)
    {
      bounds->x = bounds->y = bounds->width = bounds->height = 0;
      return;
    }

  r = sqrt (shape->frame.w * shape->frame.w +
	    shape->frame.h * shape->frame.h) / 2.0;
  r += FRAME_HANDLE_OFFSET + pad;

  bounds->x = (gint) floor (shape->frame.cx - r);
  bounds->y = (gint) floor (shape->frame.cy - r);
  bounds->width = (gint) ceil (2.0 * r);
  bounds->height = (gint) ceil (2.0 * r);
}

/* Commit one editable frame shape to the backbuffer.  An empty text or a
 * frame that was never given a size is dropped without drawing anything. */
void
frame_finish (ScreenatorData *data, ScreenatorDeviceData *devdata)
{
  GdkRectangle bounds;
  gint pad = (gint) data->pen_width * 2 + FRAME_HANDLE_MARGIN;
  gboolean drawable;

  if (!devdata || !devdata->frame_active)
    return;

  frame_shape_get_bounds (&devdata->frame_shape, pad, &bounds);

  drawable = devdata->frame_shape.frame.w >= 4.0 &&
	     devdata->frame_shape.frame.h >= 4.0;

  if (devdata->frame_shape.type == SCREENATOR_SHAPE_TEXT)
    drawable = drawable && devdata->frame_shape.text &&
	       *devdata->frame_shape.text;

  devdata->frame_active = FALSE;
  devdata->frame_drag = SCREENATOR_FRAME_HANDLE_NONE;

  if (drawable)
    {
      cairo_t *cr;

      snap_undo_state (data);
      cr = cairo_create (data->backbuffer);
      draw_frame_shape (data, cr, &devdata->frame_shape);
      cairo_destroy (cr);

      data->modified = 1;
      data->painted = 1;
    }

  frame_shape_clear (&devdata->frame_shape);

  if (data->win && bounds.width > 0 && bounds.height > 0)
    gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
				&bounds, 0);
}

/* Commit every editable frame shape, e.g. when painting mode ends or another
 * tool is chosen. */
void
frame_finish_all (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;

  if (!data->devdatatable)
    return;

  if (data->suspend_frame_finish)
    return;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    frame_finish (data, value);
}


/* Recompute the automatic handles of every node the user has not edited.
 * The tangent matches the Catmull-Rom spline that Screenator used before the
 * handles were editable, so a freshly placed spline looks the same as it
 * always did.  A short tangent is stretched to a minimum length so the handle
 * stays big enough to grab when nodes are close together. */
void
spline_update_auto (GArray *nodes)
{
  const gdouble min_handle = 12.0;
  guint i, n;

  if (!nodes)
    return;

  n = nodes->len;

  for (i = 0; i < n; i++)
    {
      ScreenatorSplineNode *node =
	&g_array_index (nodes, ScreenatorSplineNode, i);
      GdkPoint prev, next;
      gdouble tx, ty, len;

      if (node->manual)
	continue;

      if (n == 1)
	{
	  node->in = node->point;
	  node->out = node->point;
	  continue;
	}

      prev = g_array_index (nodes, ScreenatorSplineNode,
			    i > 0 ? i - 1 : i).point;
      next = g_array_index (nodes, ScreenatorSplineNode,
			    i + 1 < n ? i + 1 : i).point;

      tx = (next.x - prev.x) / 6.0;
      ty = (next.y - prev.y) / 6.0;
      len = sqrt (tx * tx + ty * ty);

      /* The endpoints of a two node spline stay on the chord, so stretching
       * their handles does not bend the straight line between the nodes. */
      if (len > 0.0 && len < min_handle)
	{
	  tx *= min_handle / len;
	  ty *= min_handle / len;
	}
      else if (len == 0.0)
	{
	  GdkPoint other = g_array_index (nodes, ScreenatorSplineNode,
					  i + 1 < n ? i + 1 : i - 1).point;
	  gdouble dx = other.x - node->point.x;
	  gdouble dy = other.y - node->point.y;

	  len = sqrt (dx * dx + dy * dy);
	  if (len > 0.0)
	    {
	      tx = dx * min_handle / len;
	      ty = dy * min_handle / len;
	    }
	}

      node->out.x = (gint) round (node->point.x + tx);
      node->out.y = (gint) round (node->point.y + ty);
      node->in.x = (gint) round (node->point.x - tx);
      node->in.y = (gint) round (node->point.y - ty);
    }
}


/* Draw the spline through the given nodes.  Every node is an on-curve point
 * and its two handles are the Bezier control points of the neighbouring
 * segments, so the curve always passes exactly through the placed nodes. */
void
draw_spline (ScreenatorData *data, cairo_t *cr, GArray *nodes)
{
  GdkRGBA color = data->pen_color;
  gint line_width = MAX (1, (gint) data->pen_width);
  guint i, n;

  if (!nodes || nodes->len == 0)
    return;

  color.alpha = data->opacity;
  gdk_cairo_set_source_rgba (cr, &color);
  cairo_set_line_width (cr, line_width);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);

  n = nodes->len;

  if (n == 1)
    {
      GdkPoint p = g_array_index (nodes, ScreenatorSplineNode, 0).point;

      cairo_arc (cr, p.x, p.y, line_width / 2.0, 0, 2 * G_PI);
      cairo_fill (cr);
      return;
    }

  {
    GdkPoint p0 = g_array_index (nodes, ScreenatorSplineNode, 0).point;

    cairo_move_to (cr, p0.x, p0.y);
  }

  for (i = 0; i < n - 1; i++)
    {
      ScreenatorSplineNode *a =
	&g_array_index (nodes, ScreenatorSplineNode, i);
      ScreenatorSplineNode *b =
	&g_array_index (nodes, ScreenatorSplineNode, i + 1);

      cairo_curve_to (cr,
		      a->out.x, a->out.y,
		      b->in.x, b->in.y,
		      b->point.x, b->point.y);
    }

  cairo_stroke (cr);
}


/* Bounding box of the spline, padded for the stroke width.  The control
 * polygon of every segment is a superset of the curve, so its box covers the
 * whole stroke and the editable handles. */
void
spline_get_bounds (ScreenatorData *data, GArray *nodes, GdkRectangle *bounds)
{
  gint pad = (gint) data->pen_width * 2 + 4;
  gdouble minx, miny, maxx, maxy;
  guint i, n;

  if (!nodes || nodes->len == 0)
    {
      bounds->x = bounds->y = bounds->width = bounds->height = 0;
      return;
    }

  n = nodes->len;
  {
    GdkPoint p0 = g_array_index (nodes, ScreenatorSplineNode, 0).point;

    minx = maxx = p0.x;
    miny = maxy = p0.y;
  }

  for (i = 0; i < n; i++)
    {
      ScreenatorSplineNode *node =
	&g_array_index (nodes, ScreenatorSplineNode, i);

      minx = MIN (minx, MIN (node->point.x, MIN (node->in.x, node->out.x)));
      miny = MIN (miny, MIN (node->point.y, MIN (node->in.y, node->out.y)));
      maxx = MAX (maxx, MAX (node->point.x, MAX (node->in.x, node->out.x)));
      maxy = MAX (maxy, MAX (node->point.y, MAX (node->in.y, node->out.y)));
    }

  bounds->x = (gint) floor (minx) - pad;
  bounds->y = (gint) floor (miny) - pad;
  bounds->width = (gint) ceil (maxx) - (gint) floor (minx) + 2 * pad;
  bounds->height = (gint) ceil (maxy) - (gint) floor (miny) + 2 * pad;
}


/* Drop a spline that is still being placed, e.g. when painting mode ends or
 * another tool is chosen.  The placed nodes are discarded; nothing is
 * committed to the backbuffer. */
void
spline_cancel_all (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;
  gboolean had_preview = FALSE;

  if (!data->devdatatable)
    return;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      ScreenatorDeviceData *devdata = value;

      if (devdata->shape_active)
	had_preview = TRUE;
      devdata->shape_active = FALSE;
      devdata->spline_node_drag = FALSE;
      devdata->spline_handle_drag = FALSE;
      devdata->spline_finish_pending = FALSE;
      if (devdata->shape_points)
	g_array_set_size (devdata->shape_points, 0);
    }

  if (had_preview && data->win)
    gdk_window_invalidate_rect (gtk_widget_get_window (data->win), NULL, 0);
}


/* Margin around the star that covers the outer handles and their grips.  It is
 * added to the radius whenever the preview has to be invalidated. */
#define STAR_HANDLE_MARGIN 64

/* Build the closed star polygon on the current cairo path.  The vertices
 * alternate between the outer and inner radius, so a full turn covers
 * 2 * points vertices. */
static void
star_build_path (cairo_t *cr, const ScreenatorStar *star)
{
  gint i, n;
  gdouble inner;

  n = star->points * 2;
  inner = CLAMP (star->inner, 0.0, 1.0);

  for (i = 0; i < n; i++)
    {
      gdouble angle = star->rotation + i * G_PI / star->points;
      gdouble r = (i % 2 == 0) ? star->radius : star->radius * inner;
      gdouble x = star->cx + r * cos (angle);
      gdouble y = star->cy + r * sin (angle);

      if (i == 0)
	cairo_move_to (cr, x, y);
      else
	cairo_line_to (cr, x, y);
    }

  cairo_close_path (cr);
}


/* Draw the star with the current pen colour and, when shape filling is on,
 * the shape fill colour. */
void
draw_star (ScreenatorData *data, cairo_t *cr, const ScreenatorStar *star)
{
  GdkRGBA color = data->pen_color;
  gint line_width = MAX (1, (gint) data->pen_width);

  if (!star || star->points < 3 || star->radius < 1.0)
    return;

  color.alpha = data->opacity;
  gdk_cairo_set_source_rgba (cr, &color);
  cairo_set_line_width (cr, line_width);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);

  star_build_path (cr, star);

  if (data->shape_fill_enabled)
    {
      GdkRGBA fill = data->shape_fill_color;

      fill.alpha = data->opacity;
      gdk_cairo_set_source_rgba (cr, &fill);
      cairo_fill_preserve (cr);
      gdk_cairo_set_source_rgba (cr, &color);
    }

  cairo_stroke (cr);
}


/* Bounding box of the star, padded for the stroke width and for the handles
 * when "pad" includes them. */
void
star_get_bounds (const ScreenatorStar *star, gint pad, GdkRectangle *bounds)
{
  gdouble r;

  if (!star)
    {
      bounds->x = bounds->y = bounds->width = bounds->height = 0;
      return;
    }

  /* A star that is still being dragged out has no radius yet; keep a small
   * box around the centre so its handles are erased when it is dropped. */
  r = MAX (star->radius, 8.0) + pad;

  bounds->x = (gint) floor (star->cx - r);
  bounds->y = (gint) floor (star->cy - r);
  bounds->width = (gint) ceil (2.0 * r);
  bounds->height = (gint) ceil (2.0 * r);
}


/* Commit one editable star to the backbuffer.  A star whose radius never grew
 * past the click threshold is dropped without drawing anything. */
void
star_finish (ScreenatorData *data, ScreenatorDeviceData *devdata)
{
  GdkRectangle bounds;
  gint pad = (gint) data->pen_width * 2 + STAR_HANDLE_MARGIN;

  if (!devdata || !devdata->star_active)
    return;

  star_get_bounds (&devdata->star, pad, &bounds);
  devdata->star_active = FALSE;
  devdata->star_drag = SCREENATOR_STAR_HANDLE_NONE;

  if (devdata->star.radius >= 8.0)
    {
      cairo_t *cr;

      snap_undo_state (data);
      cr = cairo_create (data->backbuffer);
      draw_star (data, cr, &devdata->star);
      cairo_destroy (cr);

      data->modified = 1;
      data->painted = 1;
    }

  if (data->win && bounds.width > 0 && bounds.height > 0)
    gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
				&bounds, 0);
}


/* Commit every editable star, e.g. when painting mode ends or another tool is
 * chosen. */
void
star_finish_all (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;

  if (!data->devdatatable)
    return;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    star_finish (data, value);
}


/* Build the triangle path from its three vertices. */
static void
triangle_build_path (cairo_t *cr, const ScreenatorTriangle *triangle)
{
  cairo_move_to (cr, triangle->v[0].x, triangle->v[0].y);
  cairo_line_to (cr, triangle->v[1].x, triangle->v[1].y);
  cairo_line_to (cr, triangle->v[2].x, triangle->v[2].y);
  cairo_close_path (cr);
}


/* Draw the triangle with the current pen colour and, when shape filling is
 * on, the shape fill colour. */
void
draw_triangle (ScreenatorData *data, cairo_t *cr,
	       const ScreenatorTriangle *triangle)
{
  GdkRGBA color = data->pen_color;
  gint line_width = MAX (1, (gint) data->pen_width);

  if (!triangle)
    return;

  color.alpha = data->opacity;
  gdk_cairo_set_source_rgba (cr, &color);
  cairo_set_line_width (cr, line_width);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);

  triangle_build_path (cr, triangle);

  if (data->shape_fill_enabled)
    {
      GdkRGBA fill = data->shape_fill_color;

      fill.alpha = data->opacity;
      gdk_cairo_set_source_rgba (cr, &fill);
      cairo_fill_preserve (cr);
      gdk_cairo_set_source_rgba (cr, &color);
    }

  cairo_stroke (cr);
}


/* Bounding box of the triangle, padded for the stroke width and for the
 * vertex handles. */
void
triangle_get_bounds (const ScreenatorTriangle *triangle, gint pad,
		     GdkRectangle *bounds)
{
  gint minx, miny, maxx, maxy;
  guint i;

  if (!triangle)
    {
      bounds->x = bounds->y = bounds->width = bounds->height = 0;
      return;
    }

  minx = maxx = triangle->v[0].x;
  miny = maxy = triangle->v[0].y;

  for (i = 1; i < 3; i++)
    {
      minx = MIN (minx, triangle->v[i].x);
      miny = MIN (miny, triangle->v[i].y);
      maxx = MAX (maxx, triangle->v[i].x);
      maxy = MAX (maxy, triangle->v[i].y);
    }

  bounds->x = minx - pad;
  bounds->y = miny - pad;
  bounds->width = maxx - minx + 2 * pad;
  bounds->height = maxy - miny + 2 * pad;
}


/* Twice the signed area of the triangle; a collapsed triangle has none. */
static gdouble
triangle_double_area (const ScreenatorTriangle *triangle)
{
  gdouble ax = triangle->v[1].x - triangle->v[0].x;
  gdouble ay = triangle->v[1].y - triangle->v[0].y;
  gdouble bx = triangle->v[2].x - triangle->v[0].x;
  gdouble by = triangle->v[2].y - triangle->v[0].y;

  return fabs (ax * by - ay * bx);
}


/* Commit one editable triangle to the backbuffer.  A triangle whose vertices
 * were dragged onto one line is dropped without drawing anything. */
void
triangle_finish (ScreenatorData *data, ScreenatorDeviceData *devdata)
{
  GdkRectangle bounds;
  gint pad = (gint) data->pen_width * 2 + 64;

  if (!devdata || !devdata->triangle_active)
    return;

  triangle_get_bounds (&devdata->triangle, pad, &bounds);
  devdata->triangle_active = FALSE;
  devdata->triangle_drag = SCREENATOR_TRIANGLE_HANDLE_NONE;

  if (triangle_double_area (&devdata->triangle) >= 16.0)
    {
      cairo_t *cr;

      snap_undo_state (data);
      cr = cairo_create (data->backbuffer);
      draw_triangle (data, cr, &devdata->triangle);
      cairo_destroy (cr);

      data->modified = 1;
      data->painted = 1;
    }

  if (data->win && bounds.width > 0 && bounds.height > 0)
    gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
				&bounds, 0);
}


/* Commit every editable triangle, e.g. when painting mode ends or another
 * tool is chosen. */
void
triangle_finish_all (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;

  if (!data->devdatatable)
    return;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    triangle_finish (data, value);
}


/* A pixel this transparent is treated as empty canvas by the bucket.  The
 * limit is high enough to swallow the anti-aliased inner edge of a stroke but
 * still stops at its solid core. */
#define BUCKET_ALPHA_LIMIT 200

/* Tolerance for recolouring a region that is already painted. */
#define BUCKET_COLOR_TOLERANCE 48

typedef struct
{
  gint x;
  gint y;
} BucketSeed;

static gboolean
bucket_match (guint32 pixel, guint32 seed, gboolean seed_clear)
{
  guint32 a, r, g, b, sa, sr, sg, sb;

  if (seed_clear)
    return ((pixel >> 24) & 0xff) < BUCKET_ALPHA_LIMIT;

  a = (pixel >> 24) & 0xff;
  r = (pixel >> 16) & 0xff;
  g = (pixel >> 8) & 0xff;
  b = pixel & 0xff;
  sa = (seed >> 24) & 0xff;
  sr = (seed >> 16) & 0xff;
  sg = (seed >> 8) & 0xff;
  sb = seed & 0xff;

  return ABS ((gint) a - (gint) sa) <= BUCKET_COLOR_TOLERANCE &&
	 ABS ((gint) r - (gint) sr) <= BUCKET_COLOR_TOLERANCE &&
	 ABS ((gint) g - (gint) sg) <= BUCKET_COLOR_TOLERANCE &&
	 ABS ((gint) b - (gint) sb) <= BUCKET_COLOR_TOLERANCE;
}


/* Fill the enclosed region containing (x, y) with the shape fill colour.
 * A region that reaches the screen edge is treated as not closed and left
 * alone, so clicking outside a shape cannot flood the whole screen. */
gboolean
fill_bucket (ScreenatorData *data, gint x, gint y)
{
  cairo_surface_t *surface = data->backbuffer;
  GdkRGBA color;
  guint32 *pixels;
  guint8 *mask;
  GArray *stack;
  guint32 seed, fill_pixel;
  guint32 fa, fr, fg, fb;
  gboolean seed_clear, touches_edge = FALSE;
  gboolean erase;
  gint width, height, stride, row_pixels;
  gint minx, miny, maxx, maxy;
  guint i;

  if (!surface)
    return FALSE;

  width = cairo_image_surface_get_width (surface);
  height = cairo_image_surface_get_height (surface);
  stride = cairo_image_surface_get_stride (surface);

  if (x < 0 || y < 0 || x >= width || y >= height)
    return FALSE;

  /* None means no fill colour: the bucket erases the enclosed area instead of
   * filling it, so an area filled with a colour becomes transparent again. */
  erase = !data->shape_fill_enabled;

  color = data->shape_fill_color;
  color.alpha = data->opacity;

  if (erase)
    {
      /* The target pixel is fully transparent, so the flood matches the seed
       * colour and the matched pixels are cleared. */
      fa = fr = fg = fb = 0;
    }
  else
    {
      fa = (guint32) (CLAMP (color.alpha, 0.0, 1.0) * 255.0 + 0.5);
      if (fa == 0)
	return FALSE;

      fr = (guint32) (CLAMP (color.red, 0.0, 1.0) * fa + 0.5);
      fg = (guint32) (CLAMP (color.green, 0.0, 1.0) * fa + 0.5);
      fb = (guint32) (CLAMP (color.blue, 0.0, 1.0) * fa + 0.5);
    }
  fill_pixel = (fa << 24) | (fr << 16) | (fg << 8) | fb;

  cairo_surface_flush (surface);
  pixels = (guint32 *) cairo_image_surface_get_data (surface);
  if (!pixels)
    return FALSE;

  row_pixels = stride / 4;
  seed = pixels[(gsize) y * row_pixels + x];
  if (seed == fill_pixel)
    return FALSE;

  seed_clear = (((seed >> 24) & 0xff) == 0);

  mask = g_malloc0 ((gsize) width * height);
  stack = g_array_new (FALSE, FALSE, sizeof (BucketSeed));

  minx = width;
  miny = height;
  maxx = -1;
  maxy = -1;

  {
    BucketSeed start;

    start.x = x;
    start.y = y;
    g_array_append_val (stack, start);
  }

  while (stack->len > 0)
    {
      BucketSeed s = g_array_index (stack, BucketSeed, stack->len - 1);
      guint32 *row;
      gint left, right, i;

      g_array_set_size (stack, stack->len - 1);

      if (mask[(gsize) s.y * width + s.x])
	continue;

      row = pixels + (gsize) s.y * row_pixels;
      if (!bucket_match (row[s.x], seed, seed_clear))
	continue;

      left = s.x;
      while (left > 0 &&
	     !mask[(gsize) s.y * width + (left - 1)] &&
	     bucket_match (row[left - 1], seed, seed_clear))
	left--;

      right = s.x;
      while (right < width - 1 &&
	     !mask[(gsize) s.y * width + (right + 1)] &&
	     bucket_match (row[right + 1], seed, seed_clear))
	right++;

      for (i = left; i <= right; i++)
	mask[(gsize) s.y * width + i] = 1;

      if (left == 0 || right == width - 1 ||
	  s.y == 0 || s.y == height - 1)
	touches_edge = TRUE;

      minx = MIN (minx, left);
      maxx = MAX (maxx, right);
      miny = MIN (miny, s.y);
      maxy = MAX (maxy, s.y);

      /* Push the start of every matching run on the rows above and below. */
      for (i = left; i <= right; i++)
	{
	  gint ny;

	  for (ny = s.y - 1; ny <= s.y + 1; ny += 2)
	    {
	      gsize idx;
	      gboolean in_run;

	      if (ny < 0 || ny >= height)
		continue;

	      idx = (gsize) ny * width + i;
	      in_run = (i > left &&
			!mask[idx - 1] &&
			bucket_match (pixels[(gsize) ny * row_pixels + (i - 1)],
				      seed, seed_clear));

	      if (!mask[idx] && !in_run &&
		  bucket_match (pixels[(gsize) ny * row_pixels + i],
				seed, seed_clear))
		{
		  BucketSeed next;

		  next.x = i;
		  next.y = ny;
		  g_array_append_val (stack, next);
		}
	    }
	}
    }

  if (maxx < 0 || touches_edge)
    {
      g_array_free (stack, TRUE);
      g_free (mask);
      return FALSE;
    }

  snap_undo_state (data);

  for (i = 0; i < (guint) height; i++)
    {
      guint32 *row = pixels + (gsize) i * row_pixels;
      guint8 *row_mask = mask + (gsize) i * width;
      gint j;

      for (j = 0; j < width; j++)
	{
	  guint32 dst, da, dr, dg, db, ia, oa, or_, og, ob;

	  if (!row_mask[j])
	    continue;

	  if (erase)
	    {
	      row[j] = 0;
	      continue;
	    }

	  dst = row[j];
	  da = (dst >> 24) & 0xff;
	  dr = (dst >> 16) & 0xff;
	  dg = (dst >> 8) & 0xff;
	  db = dst & 0xff;

	  /* Source-over with premultiplied components.  Anti-aliased stroke
	   * pixels stay on top of the new fill. */
	  ia = 255 - fa;
	  oa = fa + (da * ia + 127) / 255;
	  or_ = fr + (dr * ia + 127) / 255;
	  og = fg + (dg * ia + 127) / 255;
	  ob = fb + (db * ia + 127) / 255;

	  row[j] = (oa << 24) | (or_ << 16) | (og << 8) | ob;
	}
    }

  g_array_free (stack, TRUE);
  g_free (mask);

  cairo_surface_mark_dirty_rectangle (surface,
				      minx, miny,
				      maxx - minx + 1, maxy - miny + 1);

  data->modified = 1;
  data->painted = 1;

  if (data->win)
    {
      GdkRectangle bounds;

      bounds.x = minx;
      bounds.y = miny;
      bounds.width = maxx - minx + 1;
      bounds.height = maxy - miny + 1;
      gdk_window_invalidate_rect (gtk_widget_get_window (data->win),
				  &bounds, 0);
    }

  return TRUE;
}




/*
 * Functions for handling various (GTK+)-Events
 */


void main_do_event (GdkEventAny *event,
		    ScreenatorData  *data)
{
  guint keycode = ((GdkEventKey *) event)->hardware_keycode;
  if ((event->type == GDK_KEY_PRESS ||
       event->type == GDK_KEY_RELEASE) &&
      event->window == data->root &&
      (keycode == data->hot_keycode || keycode == data->undo_keycode ||
       keycode == data->undo_ctrl_keycode))
    {
      /* redirect the event to our main window, so that GTK+ doesn't
       * throw it away (there is no GtkWidget for the root window...)
       */
      event->window = gtk_widget_get_window(data->win);
      g_object_ref (gtk_widget_get_window(data->win));
    }

  gtk_main_do_event ((GdkEvent *) event);
}





void setup_main_app (ScreenatorData *data, int argc, char ** argv)
{
  gboolean activate;

  if(getenv("GDK_CORE_DEVICE_EVENTS")) {
      g_printerr("GDK is set to not use the XInput extension, Screenator can not work this way.\n"
		 "Probably the GDK_CORE_DEVICE_EVENTS environment variable is set, try to start Screenator with this variable unset.\n");
      exit(1);
  }

  /*
    HOT KEYS
  */
  data->hot_keyval = DEFAULT_HOTKEY;
  data->hot_keycode = 0;

  data->undo_keyval = DEFAULT_UNDOKEY;
  data->undo_keycode = 0;

  char *xdg_current_desktop = getenv("XDG_CURRENT_DESKTOP");
  if (xdg_current_desktop && strcmp(xdg_current_desktop, "XFCE") == 0) {
      /*
	XFCE per default grabs Ctrl-F1 to Ctrl-F12 (switch to workspace 1-12)
	and Alt-F9 (minimize window) which renders Screenator's default hotkey
	mapping unusable. Provide different defaults for that desktop environment.
      */
      data->hot_keyval = DEFAULT_HOTKEY_XFCE;
      data->undo_keyval = DEFAULT_UNDOKEY_XFCE;
      g_print("Detected XFCE, changing default hot keys to '" DEFAULT_HOTKEY_XFCE "' and '" DEFAULT_UNDOKEY_XFCE "'\n");
  }

  /* COLOURS */
  g_free(data->white);
  g_free(data->black);
  g_free(data->red);
  data->white = g_malloc (sizeof (GdkRGBA));
  data->black = g_malloc (sizeof (GdkRGBA));
  data->red   = g_malloc (sizeof (GdkRGBA));
  gdk_rgba_parse(data->white, "#FFFFFF");
  gdk_rgba_parse(data->black, "#000000");
  gdk_rgba_parse(data->red, "#FF0000");

  /* The Marker has a fixed style: yellow, half transparent and 32 px wide.
   * Its colour lives in ScreenatorData so the context stays valid when the
   * screen size changes and the contexts are recreated. */
  gdk_rgba_parse (&data->marker_color, "#FFFF00");
  data->marker_color.alpha = 0.5;


  /* 
     CURSORS
  */
  GdkPixbuf* paint_cursor_pixbuf = gdk_pixbuf_new_from_xpm_data(paint_cursor_xpm);
  data->paint_cursor = gdk_cursor_new_from_pixbuf(data->display,
						  paint_cursor_pixbuf,
						  paint_cursor_x_hot,
						  paint_cursor_y_hot);
  g_object_unref (paint_cursor_pixbuf);

  GdkPixbuf* text_cursor_pixbuf = gdk_pixbuf_new_from_xpm_data(text_cursor_xpm);
  data->text_cursor = gdk_cursor_new_from_pixbuf(data->display,
						 text_cursor_pixbuf,
						 text_cursor_x_hot,
						 text_cursor_y_hot);
  g_object_unref (text_cursor_pixbuf);

  GdkPixbuf* bucket_cursor_pixbuf = gdk_pixbuf_new_from_xpm_data(bucket_cursor_xpm);
  data->bucket_cursor = gdk_cursor_new_from_pixbuf(data->display,
						   bucket_cursor_pixbuf,
						   bucket_cursor_x_hot,
						   bucket_cursor_y_hot);
  g_object_unref (bucket_cursor_pixbuf);


  /*
    DRAWING AREA
  */
  /* SHAPE SURFACE*/
  cairo_surface_destroy(data->backbuffer);
  data->backbuffer = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, data->width, data->height);
  
  /*
    UNDO STATE
  */
  data->undo_head = 0;
  data->undo_depth = 0;
  data->redo_depth = 0;
  int i;
  for (i = 0; i < SCREENATOR_MAX_UNDO; i++)
    {
      cairo_surface_destroy(data->undobuffer[i]);
      data->undobuffer[i] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, data->width, data->height);
    }
  


  /* EVENTS */
  gtk_widget_add_events (data->win, SCREENATOR_WINDOW_EVENTS);
  g_signal_connect (data->win, "draw",
		    G_CALLBACK (on_expose), data);
  g_signal_connect (data->win,"configure_event",
		    G_CALLBACK (on_configure), data);
  g_signal_connect (data->win,"screen-changed",
		    G_CALLBACK (on_screen_changed), data);
  g_signal_connect (data->screen,"monitors_changed",
		    G_CALLBACK (on_monitors_changed), data);
  g_signal_connect (data->screen,"composited-changed",
		    G_CALLBACK (on_composited_changed), data);
  g_signal_connect (gdk_display_get_device_manager (data->display), "device-added",
                    G_CALLBACK (on_device_added), data);
  g_signal_connect (gdk_display_get_device_manager (data->display), "device-removed",
                    G_CALLBACK (on_device_removed), data);
  g_signal_connect (data->win, "motion_notify_event",
		    G_CALLBACK (on_motion), data);
  g_signal_connect (data->win, "button_press_event", 
		    G_CALLBACK (on_buttonpress), data);
  g_signal_connect (data->win, "button_release_event",
		    G_CALLBACK (on_buttonrelease), data);
  g_signal_connect (data->win, "scroll_event",
		    G_CALLBACK (on_scroll), data);
  /* disconnect previously defined selection handlers */
  g_signal_handlers_disconnect_by_func (data->win, 
					G_CALLBACK (on_clientapp_selection_get),
					data);
  g_signal_handlers_disconnect_by_func (data->win, 
					G_CALLBACK (on_clientapp_selection_received),
					data);
  /* and re-connect them to mainapp functions */
  g_signal_connect (data->win, "selection_get",
		    G_CALLBACK (on_mainapp_selection_get), data);
  g_signal_connect (data->win, "selection_received",
		    G_CALLBACK (on_mainapp_selection_received), data);


  /* The initial shape also depends on the saved screen paper. */
  update_window_shape (data);
  

  /* reset settings from client setup */
  gtk_selection_remove_all (data->win);
  gtk_selection_owner_set (data->win, GA_CONTROL, GDK_CURRENT_TIME);

  gtk_selection_add_target (data->win, GA_CONTROL, GA_STATUS, 0);
  gtk_selection_add_target (data->win, GA_CONTROL, GA_QUIT, 1);
  gtk_selection_add_target (data->win, GA_CONTROL, GA_ACTIVATE, 2);
  gtk_selection_add_target (data->win, GA_CONTROL, GA_DEACTIVATE, 3);
  gtk_selection_add_target (data->win, GA_CONTROL, GA_TOGGLE, 4);
  gtk_selection_add_target (data->win, GA_CONTROL, GA_VISIBILITY, 5);
  gtk_selection_add_target (data->win, GA_CONTROL, GA_CLEAR, 6);
  gtk_selection_add_target (data->win, GA_CONTROL, GA_RELOAD, 7);
  gtk_selection_add_target (data->win, GA_CONTROL, GA_UNDO, 8);
  gtk_selection_add_target (data->win, GA_CONTROL, GA_REDO, 9);


 

  /*
   * Parse Config file
   */
  data->tool_config = g_hash_table_new (g_str_hash, g_str_equal);
  parse_config (data);
  g_hash_table_foreach (data->tool_config, parse_print_help, NULL);

  /*
    parse key file
  */
  read_keyfile(data);

  /* The saved screen paper changes how much of the window is visible. */
  update_window_shape (data);

  /*
    parse cmdline
  */
  activate = app_parse_args (argc, argv, data);

  /* The eraser width is only known after the config file and the command
   * line have been read, so build the cursor here.  Creating it earlier
   * would use the zero-initialised width and give a tiny cursor. */
  update_eraser_cursor (data);

  /* 
     FIND HOTKEY KEYCODE 
  */
  if (data->hot_keyval)
    {
      GdkKeymap    *keymap;
      GdkKeymapKey *keys;
      gint          n_keys;
      guint         keyval;

      if (strlen (data->hot_keyval) > 0 &&
          strcasecmp (data->hot_keyval, "none") != 0)
        {
          keymap = gdk_keymap_get_for_display (data->display);
          keyval = gdk_keyval_from_name (data->hot_keyval);

          if (!keyval || !gdk_keymap_get_entries_for_keyval (keymap, keyval,
                                                             &keys, &n_keys))
            {
              g_printerr ("cannot find the key \"%s\"\n", data->hot_keyval);
              exit (1);
            }

          data->hot_keycode = keys[0].keycode;
          g_free (keys);
        }
    }

  /*
     FIND UNDOKEY KEYCODE 
  */
  if (data->undo_keyval)
    {
      GdkKeymap    *keymap;
      GdkKeymapKey *keys;
      gint          n_keys;
      guint         keyval;

      if (strlen (data->undo_keyval) > 0 &&
          strcasecmp (data->undo_keyval, "none") != 0)
        {
          keymap = gdk_keymap_get_for_display (data->display);
          keyval = gdk_keyval_from_name (data->undo_keyval);

          if (!keyval || !gdk_keymap_get_entries_for_keyval (keymap, keyval,
                                                             &keys, &n_keys))
            {
              g_printerr ("cannot find the key \"%s\"\n", data->undo_keyval);
              exit (1);
            }

          data->undo_keycode = keys[0].keycode;
          g_free (keys);
        }
    }


  /* 
     INPUT DEVICES
  */
  data->devdatatable = g_hash_table_new(NULL, NULL);
  setup_input_devices (data);



  gtk_widget_show_all (data->win);

  /* Restore the drawings saved when Screenator last quit. */
  data->painted = load_annotations (data) ? 1 : 0;
  if (data->painted)
    update_window_shape (data);

  hide_window (data);

  /* A saved screen paper or some restored annotations are shown right away. */
  if (data->paper != SCREENATOR_PAPER_NONE || data->painted)
    show_window (data);

  data->timeout_id = g_timeout_add (20, reshape, data);
 
  data->modified = 0;

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

  /* Apply the saved (or command line) pen settings and restore the tool
   * that was selected from the icon menu before the last quit. */
  apply_pen_settings (data);

  

  gdk_event_handler_set ((GdkEventFunc) main_do_event, data, NULL);
  gtk_key_snooper_install (snoop_key_press, data);

  if (activate)
    acquire_grab (data, NULL); /* grab all */

  /* 
     TRAY ICON
  */
  data->trayicon = app_indicator_new (PACKAGE_NAME,
				      "screenator",
				      APP_INDICATOR_CATEGORY_APPLICATION_STATUS);

  /* Use the packaged icon file when available: the theme name alone fails
   * for the AppImage and for unpacked builds. */
  set_tray_icon (data);

  app_indicator_set_status (data->trayicon, APP_INDICATOR_STATUS_ACTIVE);

  /* The tray icon shows the same menu as the floating icon. */
  refresh_tray_menu (data);

  /* movable on-screen icon */
  create_icon_window (data);

}


void parse_print_help (gpointer key, gpointer value, gpointer user_data)
{
  paint_context_print ((gchar *) key, (ScreenatorPaintContext *) value);
}


int app_parse_args (int argc, char **argv, ScreenatorData *data)
{
   gint      i;
   gchar    *arg;
   gboolean  wrong_arg = FALSE;
   gboolean  activate = FALSE;

   for (i=1; i < argc ; i++)
     {
       arg = argv[i];
       if (strcmp (arg, "-a") == 0 ||
           strcmp (arg, "--active") == 0)
         {
           activate = TRUE;
         }
       else if (strcmp (arg, "-d") == 0 ||
                strcmp (arg, "--debug") == 0)
         {
           data->debug = 1;
         }
       else if (strcmp (arg, "-k") == 0 ||
                strcmp (arg, "--key") == 0)
         {
           if (i+1 < argc)
             {
               data->hot_keyval = argv[i+1];
               data->hot_keycode = 0;
               i++;
             }
           else
             {
               g_printerr ("-k requires an Key-Name as argument\n");
               wrong_arg = TRUE;
             }
         }
       else if (strcmp (arg, "-K") == 0 ||
                strcmp (arg, "--keycode") == 0)
         {
           if (i+1 < argc && atoi (argv[i+1]) > 0)
             {
               data->hot_keyval = NULL;
               data->hot_keycode = atoi (argv[i+1]);
               i++;
             }
           else
             {
               g_printerr ("-K requires an keycode > 0 as argument\n");
               wrong_arg = TRUE;
             }
         }
      else if (strcmp (arg, "-o") == 0 ||
                strcmp (arg, "--opacity") == 0)
         {
           if (i+1 < argc && strtod (argv[i+1], NULL) >= 0.0 && strtod (argv[i+1], NULL) <= 1.0)
             {
               data->opacity = strtod (argv[i+1], NULL);
               g_printerr ("Opacity set to: %.2f\n", data->opacity);
               i++;
             }
           else
             {
               g_printerr ("-o requires an opacity >=0 and <=1 as argument\n");
               wrong_arg = TRUE;
             }
         }
       else if (strcmp (arg, "-u") == 0 ||
                strcmp (arg, "--undo-key") == 0)
         {
           if (i+1 < argc)
             {
               data->undo_keyval = argv[i+1];
               data->undo_keycode = 0;
               i++;
             }
           else
             {
               g_printerr ("-u requires an Key-Name as argument\n");
               wrong_arg = TRUE;
             }
         }
       else if (strcmp (arg, "-U") == 0 ||
                strcmp (arg, "--undo-keycode") == 0)
         {
           if (i+1 < argc && atoi (argv[i+1]) > 0)
             {
               data->undo_keyval = NULL;
               data->undo_keycode = atoi (argv[i+1]);
               i++;
             }
           else
             {
               g_printerr ("-U requires an keycode > 0 as argument\n");
               wrong_arg = TRUE;
             }
         }
       else if (strcmp (arg, "-V") == 0 ||
		strcmp (arg, "--version") == 0)
         {
	     g_print("Screenator " VERSION "\n");
	     exit(0);
         }
       else
         {
           g_printerr ("Unknown Option for Screenator startup: \"%s\"\n", arg);
           wrong_arg = TRUE;
         }

       if (wrong_arg)
         {
           g_printerr ("Please see the Screenator manpage for the correct usage\n");
           exit (1);
         }
     }

   return activate;
}


/*
 * Tray menu
 *
 * The tray icon shows exactly the same menu as the floating icon.
 */

void
refresh_tray_menu (ScreenatorData *data)
{
  GtkWidget *menu;

  if (!data->trayicon)
    return;

  menu = build_icon_menu (data);
  gtk_widget_show_all (menu);
  app_indicator_set_menu (data->trayicon, GTK_MENU (menu));

  /* The indicator does not take ownership of the menu. */
  if (data->tray_menu)
    gtk_widget_destroy (data->tray_menu);
  data->tray_menu = menu;
}


/*
 * Main programs
 */

int main_client (int argc, char **argv, ScreenatorData *data)
{
   GdkAtom   action = GDK_NONE;
   gint      i;
   gchar    *arg;
   gboolean  wrong_arg = FALSE;

   for (i=1; i < argc ; i++)
     {
       arg = argv[i];
       action = GDK_NONE;
       if (strcmp (arg, "-d") == 0 ||
           strcmp (arg, "--debug") == 0)
         {
           data->debug = 1;
         }
       else if (strcmp (arg, "-t") == 0 ||
           strcmp (arg, "--toggle") == 0)
         {
           action = GA_TOGGLE;
           if (i+1 < argc && argv[i+1][0] != '-') /* there is an id supplied */
             {
               data->clientdata  = argv[i+1];
               ++i;
             }
           else
             data->clientdata = "-1"; /* default to grab all */
         }
       else if (strcmp (arg, "-v") == 0 ||
                strcmp (arg, "--visibility") == 0)
         {
           action = GA_VISIBILITY;
         }
       else if (strcmp (arg, "-q") == 0 ||
                strcmp (arg, "--quit") == 0)
         {
           action = GA_QUIT;
         }
       else if (strcmp (arg, "-c") == 0 ||
                strcmp (arg, "--clear") == 0)
         {
           action = GA_CLEAR;
         }
       else if (strcmp (arg, "-r") == 0 ||
                strcmp (arg, "--reload") == 0)
         {
           action = GA_RELOAD;
         }
       else if (strcmp (arg, "-z") == 0 ||
                strcmp (arg, "--undo") == 0)
         {
           action = GA_UNDO;
         }
       else if (strcmp (arg, "-y") == 0 ||
                strcmp (arg, "--redo") == 0)
         {
           action = GA_REDO;
         }
       else if (strcmp (arg, "-V") == 0 ||
                strcmp (arg, "--version") == 0)
         {
           g_print ("Screenator " VERSION "\n");
           return 0;
         }
       else
         {
           g_printerr ("Unknown Option to control a running Screenator process: \"%s\"\n", arg);
           wrong_arg = TRUE;
         }

       if (!wrong_arg && action != GDK_NONE)
         {
           gtk_selection_convert (data->win, GA_CONTROL,
                                  action, GDK_CURRENT_TIME);
           gtk_main ();  /* Wait for the response */
         }
       else if(wrong_arg)
         {
           g_printerr ("Please see the Screenator manpage for the correct usage\n");
           return 1;
         }
     }

   if (argc <= 1)
     g_printerr ("Screenator is already running.\n");

   return 0;
}

int main (int argc, char **argv)
{
  ScreenatorData *data;

  /*
      we run okay under XWayland, but not native Wayland
  */
  gdk_set_allowed_backends ("x11");

  gtk_init (&argc, &argv);
  data = g_malloc0(sizeof (ScreenatorData));

  /* 
     init basic stuff
  */
  data->display = gdk_display_get_default ();
  data->screen = gdk_display_get_default_screen (data->display);
  data->xinerama = gdk_screen_get_n_monitors (data->screen) > 1;
  data->composited = gdk_screen_is_composited (data->screen);
  data->root = gdk_screen_get_root_window (data->screen);
  data->width = gdk_screen_get_width (data->screen);
  data->height = gdk_screen_get_height (data->screen);
  data->opacity = DEFAULT_OPACITY;
  data->pen_width = KEY_DFLT_PEN_WIDTH;
  gdk_rgba_parse (&data->pen_color, KEY_DFLT_PEN_COLOR);
  data->pen_tool = 0;
  data->icon_size = KEY_DFLT_ICON_SIZE;

  /*
    init our window
  */
  data->win = gtk_window_new (GTK_WINDOW_POPUP);
  // this trys to set an alpha channel
  on_screen_changed(data->win, NULL, data);

  gtk_window_fullscreen(GTK_WINDOW(data->win)); 
  gtk_window_set_skip_taskbar_hint(GTK_WINDOW(data->win), TRUE);
  gtk_widget_set_app_paintable (data->win, TRUE);
  gtk_window_set_decorated (GTK_WINDOW (data->win), FALSE);

  gtk_widget_set_size_request (GTK_WIDGET (data->win), data->width, data->height-1);

  gtk_widget_add_events (data->win, SCREENATOR_WINDOW_EVENTS);

  g_signal_connect (data->win, "delete-event", gtk_main_quit, NULL);
  g_signal_connect (data->win, "destroy", gtk_main_quit, NULL);
  /* the selection event handlers will be overwritten if we become a mainapp */
  g_signal_connect (data->win, "selection_received", 
		    G_CALLBACK (on_clientapp_selection_received), data);
  g_signal_connect (data->win, "selection_get",
		    G_CALLBACK (on_clientapp_selection_get), data);
  
  gtk_widget_realize (data->win);

  // try to set transparent for input
  cairo_region_t* r =  cairo_region_create();
  gtk_widget_input_shape_combine_region(data->win, r);
  cairo_region_destroy(r);

  gtk_selection_owner_set (data->win, GA_DATA, GDK_CURRENT_TIME);
  gtk_selection_add_target (data->win, GA_DATA, GA_TOGGLEDATA, 1007);



  /* Try to get a status message. If there is a response screenator
   * is already active.
   */

  gtk_selection_convert (data->win, GA_CONTROL, GA_STATUS,
                         GDK_CURRENT_TIME);
  gtk_main ();  /* Wait for the response */

  if (data->client)
    return main_client (argc, argv, data);

  /* Main application */
  setup_main_app (data, argc, argv);
  gtk_main ();
  shutdown_input_devices(data);
  if (data->save_annotations_on_exit)
    save_annotations (data);
  else
    remove_saved_annotations ();
  write_keyfile(data); // save keyfile config
  g_free (data);
  return 0;
}

void indicate_active(ScreenatorData *data, gboolean YESNO)
{
    /* During startup the input devices are set up before the tray icon is
     * created, so this can be called while there is no indicator yet. */
    set_tray_icon (data);

    update_icon_image (data);
}
