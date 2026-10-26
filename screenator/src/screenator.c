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
#include <math.h>
#include <stdlib.h>
#include <pango/pangocairo.h>

#include "callbacks.h"
#include "config.h"
#include "input.h"
#include "screenator.h"
#include "build-config.h"

#include "paint_cursor.xpm"
#include "text_cursor.xpm"



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
      data->shape_type = SCREENATOR_SHAPE_FREE_ARROW;
      break;
    case 3:
      data->forced_context = data->default_eraser;
      break;
    case 4:
      data->forced_context = data->default_text;
      break;
    case 5:
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_RECTANGLE;
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
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_ANGLED_LINE;
      break;
    case 9:
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_FREE_LINE;
      break;
    case 10:
      data->forced_context = data->default_shape;
      data->shape_type = SCREENATOR_SHAPE_ANGLED_ARROW;
      break;
    case 11:
      data->forced_context = data->default_marker;
      break;
    default:
      data->forced_context = NULL;
      break;
    }
}


/* Change the drawing opacity and make it take effect immediately. */
void set_pen_opacity (ScreenatorData *data, gdouble opacity)
{
  if (opacity < 0.0)
    opacity = 0.0;
  if (opacity > 1.0)
    opacity = 1.0;

  data->opacity = opacity;

  if (!data->default_pen)
    return;

  apply_pen_style_to_pens (data);
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
}



void toggle_visibility (ScreenatorData *data)
{
  if (data->hidden)
    show_window (data);
  else
    hide_window (data);
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
  
  if(!data->composited)
    {
      cairo_region_t* r = gdk_cairo_region_create_from_surface(data->backbuffer);
      gtk_widget_shape_combine_region(data->win, r);
      cairo_region_destroy(r);
      // try to set transparent for input
      r =  cairo_region_create();
      gtk_widget_input_shape_combine_region(data->win, r);
      cairo_region_destroy(r);
    }

  data->painted = 0;

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
	  cairo_region_t* r = gdk_cairo_region_create_from_surface(data->backbuffer);
	  gtk_widget_shape_combine_region(data->win, r);
	  cairo_region_destroy(r);
	  // try to set transparent for input
	  r =  cairo_region_create();
	  gtk_widget_input_shape_combine_region(data->win, r);
	  cairo_region_destroy(r);

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

  if (context && context->type == SCREENATOR_ERASER)
    cursor = data->erase_cursor;
  else if (context && context->type == SCREENATOR_TEXT)
    cursor = data->text_cursor;
  else
    cursor = data->paint_cursor;

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




/*
 * Render a text annotation at (x, y) with the font chosen in the menu and
 * the same colour and opacity as the pen.
 */

void draw_text (ScreenatorData *data,
		GdkDevice *dev,
		gint x, gint y,
		const gchar *text)
{
  ScreenatorDeviceData *devdata = g_hash_table_lookup (data->devdatatable, dev);
  ScreenatorPaintContext *context = devdata ? devdata->cur_context : NULL;
  cairo_t *cr;
  PangoLayout *layout;
  PangoFontDescription *desc;
  PangoRectangle ink;
  GdkRectangle rect;

  if (!text || !*text)
    return;

  if (!context || context->type != SCREENATOR_TEXT)
    context = data->default_text;
  if (!context)
    return;

  cr = cairo_create (data->backbuffer);
  layout = pango_cairo_create_layout (cr);
  pango_layout_set_text (layout, text, -1);

  desc = pango_font_description_from_string (data->font ? data->font
						       : KEY_DFLT_FONT);
  pango_layout_set_font_description (layout, desc);
  pango_font_description_free (desc);

  gdk_cairo_set_source_rgba (cr, context->paint_color);
  cairo_move_to (cr, x, y);
  pango_cairo_show_layout (cr, layout);

  pango_layout_get_pixel_extents (layout, &ink, NULL);
  rect.x = x + ink.x - 2;
  rect.y = y + ink.y - 2;
  rect.width = ink.width + 4;
  rect.height = ink.height + 4;

  g_object_unref (layout);
  cairo_destroy (cr);

  data->modified = 1;
  data->painted = 1;

  if (rect.width > 4 && rect.height > 4)
    gdk_window_invalidate_rect (gtk_widget_get_window (data->win), &rect, 0);

  if (data->debug)
    g_printerr ("DEBUG: drew text \"%s\" at %d %d\n", text, x, y);
}


/* Snap the end of an angled line to the nearest supported angle.  Both
 * directions of each angle are allowed, so the set effectively covers 0, 30,
 * 45, 60, 90, 120, 135 and 150 degrees plus their opposites. */
static void
shape_angled_endpoint (gint x0, gint y0, gint x1, gint y1,
		       gint *end_x, gint *end_y)
{
  static const gdouble angles[] = { 0.0, 30.0, 45.0, 60.0,
				    90.0, 120.0, 135.0, 150.0 };
  gdouble dx = x1 - x0;
  gdouble dy = y1 - y0;
  gdouble phi = atan2 (dy, dx) * 180.0 / G_PI;
  gdouble best_diff = 360.0;
  gdouble theta = 0.0;
  guint i, half;

  if (phi < 0.0)
    phi += 360.0;

  for (i = 0; i < G_N_ELEMENTS (angles); i++)
    for (half = 0; half < 2; half++)
      {
	gdouble candidate = angles[i] + 180.0 * half;
	gdouble diff = fabs (phi - candidate);

	if (diff > 180.0)
	  diff = 360.0 - diff;
	if (diff < best_diff)
	  {
	    best_diff = diff;
	    theta = candidate;
	  }
      }

  {
    gdouble rad = theta * G_PI / 180.0;
    gdouble length = dx * cos (rad) + dy * sin (rad);

    if (length < 0.0)
      length = 0.0;
    *end_x = x0 + (gint) (length * cos (rad) + 0.5);
    *end_y = y0 + (gint) (length * sin (rad) + 0.5);
  }
}


/* The four line tools share the same drawing code; the arrows additionally
 * get a head at the end point.  Angled lines and arrows snap to fixed
 * angles, the free line and the arrow do not. */
static gboolean
shape_is_line (ScreenatorShapeType type)
{
  return type == SCREENATOR_SHAPE_FREE_LINE ||
	 type == SCREENATOR_SHAPE_ANGLED_LINE ||
	 type == SCREENATOR_SHAPE_FREE_ARROW ||
	 type == SCREENATOR_SHAPE_ANGLED_ARROW;
}


static gboolean
shape_is_angled (ScreenatorShapeType type)
{
  return type == SCREENATOR_SHAPE_ANGLED_LINE ||
	 type == SCREENATOR_SHAPE_ANGLED_ARROW;
}


static gboolean
shape_has_arrow (ScreenatorShapeType type)
{
  return type == SCREENATOR_SHAPE_FREE_ARROW ||
	 type == SCREENATOR_SHAPE_ANGLED_ARROW;
}


/* Arrow proportions, relative to w = pen width * 0.375.  The head is longer
 * than it is wide so the point looks sharp, and the barbs sweep back behind
 * the notch to make the head clearly visible. */
#define ARROW_HEAD_LENGTH 8.0
#define ARROW_HEAD_HALF_WIDTH 3.0
#define ARROW_HEAD_NOTCH 6.0

/* Draw an arrow as one filled polygon that contains both the shaft and the
 * head.  Building it as a single path keeps translucent strokes at one
 * opacity, avoids a seam between shaft and head, and leaves the tip as a
 * plain sharp corner instead of the round cap a stroked line would show. */
static void
shape_draw_arrow (cairo_t *cr, gint line_width,
		  gint x0, gint y0, gint x1, gint y1,
		  const GdkRGBA *color)
{
  gdouble direction = atan2 (y1 - y0, x1 - x0);
  gdouble w = MAX (1, line_width) * 0.5;
  gdouble half = line_width / 2.0;
  gdouble ca = cos (direction);
  gdouble sa = sin (direction);
  gdouble length = hypot (x1 - x0, y1 - y0);
  gdouble notch = ARROW_HEAD_NOTCH * w;
  gdouble notch_x = x1 - notch * ca;
  gdouble notch_y = y1 - notch * sa;
  gdouble base_x = x1 - ARROW_HEAD_LENGTH * w * ca;
  gdouble base_y = y1 - ARROW_HEAD_LENGTH * w * sa;
  gdouble barb = ARROW_HEAD_HALF_WIDTH * w;
  gdouble start_x = x0;
  gdouble start_y = y0;

  /* A very short drag still has to give a clean head: clamp the shaft to the
   * notch so the polygon does not turn inside out. */
  if (length <= notch)
    {
      start_x = notch_x;
      start_y = notch_y;
    }

  /* The unit vector along the arrow is (ca, sa); the one across it is
   * (sa, -ca). */
  cairo_save (cr);
  cairo_new_path (cr);
  cairo_move_to (cr, start_x + half * sa, start_y - half * ca);
  cairo_line_to (cr, notch_x + half * sa, notch_y - half * ca);
  cairo_line_to (cr, base_x + barb * sa, base_y - barb * ca);
  cairo_line_to (cr, x1, y1);
  cairo_line_to (cr, base_x - barb * sa, base_y + barb * ca);
  cairo_line_to (cr, notch_x - half * sa, notch_y + half * ca);
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
      if (shape_is_angled (type))
	shape_angled_endpoint (x0, y0, x1, y1, &x1, &y1);
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

  if (type == SCREENATOR_SHAPE_RECTANGLE)
    cairo_rectangle (cr, x, y, w, h);
  else if (type == SCREENATOR_SHAPE_ELLIPSE)
    {
      if (w < 1 || h < 1)
	return;
      cairo_save (cr);
      cairo_translate (cr, x + w / 2.0, y + h / 2.0);
      cairo_scale (cr, w / 2.0, h / 2.0);
      cairo_arc (cr, 0, 0, 1, 0, 2 * G_PI);
      cairo_restore (cr);
    }
  else if (type == SCREENATOR_SHAPE_ROUNDED_RECTANGLE)
    {
      gdouble radius = MIN (w, h) / 4.0;

      if (w < 1 || h < 1)
	return;
      if (radius < 2.0)
	radius = 2.0;
      if (radius > 60.0)
	radius = 60.0;
      if (radius > w / 2.0)
	radius = w / 2.0;
      if (radius > h / 2.0)
	radius = h / 2.0;
      cairo_new_sub_path (cr);
      cairo_arc (cr, x + w - radius, y + radius, radius, -G_PI_2, 0);
      cairo_arc (cr, x + w - radius, y + h - radius, radius, 0, G_PI_2);
      cairo_arc (cr, x + radius, y + h - radius, radius, G_PI_2, G_PI);
      cairo_arc (cr, x + radius, y + radius, radius, G_PI, 3 * G_PI_2);
      cairo_close_path (cr);
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

  if (shape_is_angled (type))
    shape_angled_endpoint (x0, y0, x1, y1, &x1, &y1);

  bounds->x = MIN (x0, x1) - pad;
  bounds->y = MIN (y0, y1) - pad;
  bounds->width = ABS (x1 - x0) + 2 * pad;
  bounds->height = ABS (y1 - y0) + 2 * pad;
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
      (keycode == data->hot_keycode || keycode == data->undo_keycode))
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


  if(!data->composited) // set initial shape
    {
      cairo_region_t* r = gdk_cairo_region_create_from_surface(data->backbuffer);
      gtk_widget_shape_combine_region(data->win, r);
      cairo_region_destroy(r);
    }
  

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

  data->painted = 0;
  hide_window (data);

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
