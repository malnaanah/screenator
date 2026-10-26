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

#ifndef CALLBACKS_H
#define CALLBACKS_H

#include <glib.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "screenator.h"


gboolean on_expose (GtkWidget *widget,
		    cairo_t* cr,
		    gpointer user_data);

gboolean on_configure (GtkWidget *widget,
		       GdkEventExpose *event,
		       gpointer user_data);


void on_screen_changed(GtkWidget *widget,
		       GdkScreen *previous_screen,
		       gpointer   user_data);

void on_monitors_changed(GdkScreen *screen,
			 gpointer   user_data);

void on_composited_changed(GdkScreen *screen,
			   gpointer   user_data);

void on_clientapp_selection_get (GtkWidget          *widget,
				 GtkSelectionData   *selection_data,
				 guint               info,
				 guint               time,
				 gpointer            user_data);

void on_clientapp_selection_received (GtkWidget *widget,
				      GtkSelectionData *selection_data,
				      guint time,
				      gpointer user_data);

gboolean on_buttonpress (GtkWidget *win, GdkEventButton *ev, gpointer user_data);

gboolean on_motion (GtkWidget *win, GdkEventMotion *ev, gpointer user_data);

gboolean on_buttonrelease (GtkWidget *win, GdkEventButton *ev, gpointer user_data);

void on_mainapp_selection_get (GtkWidget          *widget,
			       GtkSelectionData   *selection_data,
			       guint               info,
			       guint               time,
			       gpointer            user_data);


void on_mainapp_selection_received (GtkWidget *widget,
				    GtkSelectionData *selection_data,
				    guint time,
				    gpointer user_data);


void on_device_removed (GdkDeviceManager *device_manager,
			GdkDevice        *device,
			gpointer          user_data);

void on_device_added (GdkDeviceManager *device_manager,
		      GdkDevice        *device,
		      gpointer          user_data);

/*
  menu callbacks
 */
gboolean on_toggle_paint(GtkWidget *widget,
			 GdkEventButton  *ev,
			 gpointer   user_data);

void on_clear (GtkMenuItem *menuitem,
	       gpointer     user_data);

void on_toggle_vis(GtkMenuItem *menuitem,
		   gpointer     user_data);

void on_opacity_bigger(GtkMenuItem *menuitem,
		       gpointer     user_data);

void on_opacity_lesser(GtkMenuItem *menuitem,
		       gpointer     user_data);

void on_undo(GtkMenuItem *menuitem,
	     gpointer     user_data);

void on_redo(GtkMenuItem *menuitem,
	     gpointer     user_data);

void on_about(GtkMenuItem *menuitem,
	      gpointer     user_data);

/* movable icon window */
void create_icon_window (ScreenatorData *data);
void update_icon_image (ScreenatorData *data);
GtkWidget *build_icon_menu (ScreenatorData *data);

gboolean on_icon_button_press (GtkWidget *win,
			       GdkEventButton  *ev,
			       gpointer   user_data);

gboolean on_icon_motion (GtkWidget *win,
			 GdkEventMotion *ev,
			 gpointer   user_data);

gboolean on_icon_button_release (GtkWidget *win,
				 GdkEventButton  *ev,
				 gpointer   user_data);

void on_icon_pen (GtkMenuItem *menuitem,
		  gpointer     user_data);

void on_icon_arrow (GtkMenuItem *menuitem,
		    gpointer     user_data);

void on_icon_free_line (GtkMenuItem *menuitem,
			gpointer     user_data);

void on_icon_angled_arrow (GtkMenuItem *menuitem,
			   gpointer     user_data);

void on_icon_rectangle (GtkMenuItem *menuitem,
			gpointer     user_data);

void on_icon_ellipse (GtkMenuItem *menuitem,
		      gpointer     user_data);

void on_icon_rounded_rectangle (GtkMenuItem *menuitem,
				gpointer     user_data);

void on_icon_angled_line (GtkMenuItem *menuitem,
			  gpointer     user_data);

void on_icon_text (GtkMenuItem *menuitem,
		   gpointer     user_data);

void on_icon_eraser (GtkMenuItem *menuitem,
		     gpointer     user_data);

void on_icon_marker (GtkMenuItem *menuitem,
		     gpointer     user_data);

void popup_icon_menu (ScreenatorData *data, gint x, gint y);

#endif
