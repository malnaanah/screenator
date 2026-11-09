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
#include <cairo-pdf.h>
#ifdef HAVE_POPPLER_GLIB
#include <poppler.h>
#endif
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

void screenator_paper_pan_start (ScreenatorData *data, guint button,
				 gint x, gint y);
void screenator_paper_pan_motion (ScreenatorData *data, gint x, gint y);
void screenator_paper_pan_end (ScreenatorData *data);
static ScreenatorPaperDocument *paper_document (ScreenatorData *data,
						ScreenatorPaperType type);
static ScreenatorPaperDocument *paper_active_document (ScreenatorData *data);
static ScreenatorPaperPage *paper_current_page (ScreenatorPaperDocument *doc);
static gboolean paper_continuous (ScreenatorData *data);
static gint paper_pdf_page_at (ScreenatorPaperDocument *doc, gint content_y);
static gint paper_view_height (ScreenatorData *data);
static void paper_window_ensure_buffer (ScreenatorData *data);
static void paper_trim_page_backgrounds (ScreenatorPaperDocument *doc);
static void paper_load_page (ScreenatorData *data, ScreenatorPaperPage *page);
static void paper_page_origin (ScreenatorData *data, ScreenatorPaperPage *page,
			       gint *x, gint *y);
static void paper_pdf_pick_page (ScreenatorData *data, gint screen_y);
static void paper_pdf_prepare_active (ScreenatorData *data,
				      ScreenatorPaperDocument *doc);
static void paper_commit_pending (ScreenatorData *data);
static void paper_pdf_build_layout (ScreenatorData *data,
				    ScreenatorPaperDocument *doc);
static void paper_build_continuous_layout (ScreenatorData *data,
					   ScreenatorPaperDocument *doc);
static void paper_ensure_background (ScreenatorData *data,
				     ScreenatorPaperDocument *doc,
				     guint index, ScreenatorPaperPage *page);
static void set_paper_pan_cursor (ScreenatorData *data);
static void restore_paper_cursor (ScreenatorData *data);
static void paper_update_hover_cursor (ScreenatorData *data);
static void paper_refresh_active_cursor (ScreenatorData *data);
static void screenator_activate_cursor_state (ScreenatorData *data);
static void paper_refresh_controls_hover (ScreenatorData *data);
static void paper_controls_attach_hover (ScreenatorData *data,
					 GtkWidget *widget);
static void paper_finish_temporary_eraser (ScreenatorData *data);
static void screenator_timer_cancel (ScreenatorData *data);
static void paper_top_bar_rebuild (ScreenatorData *data);

/* ------------------------------------------------------------------
 * Dialogs and message boxes
 * ------------------------------------------------------------------ */

/* The annotation window is an override-redirect popup, and the window manager
 * keeps such windows above the dialogs it manages.  Make the dialogs
 * override-redirect too, so they live in the same layer as the annotation
 * window and can be raised above it.  The drawings and the paper page then
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

/* Give popup dialogs a visible border.  They are override-redirect windows,
 * so the window manager does not draw a frame around them. */
static void
dialog_install_border (GtkWidget *dialog)
{
  static GtkCssProvider *provider = NULL;
  GtkStyleContext *context;
  GdkScreen *screen;

  if (!provider)
    {
      provider = gtk_css_provider_new ();
      gtk_css_provider_load_from_data (provider,
	"window.screenator-dialog { border: 2px solid #5f6368; }\n",
	-1, NULL);
      screen = gtk_widget_get_screen (dialog);
      gtk_style_context_add_provider_for_screen (
	screen ? screen : gdk_screen_get_default (),
	GTK_STYLE_PROVIDER (provider),
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

  context = gtk_widget_get_style_context (dialog);
  gtk_style_context_add_class (context, "screenator-dialog");
}

/* Every real dialog gets a title bar as well as the border above.  A
 * GtkHeaderBar works for both override-redirect dialogs (above the drawings)
 * and managed dialogs (the startup hints), so their appearance is the same. */
static void
dialog_install_titlebar (GtkWidget *dialog)
{
  GtkWidget *header;
  const gchar *title;

  if (!GTK_IS_WINDOW (dialog))
    return;

  if (gtk_window_get_titlebar (GTK_WINDOW (dialog)))
    return;

  header = gtk_header_bar_new ();
  gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (header), TRUE);
  gtk_header_bar_set_decoration_layout (GTK_HEADER_BAR (header), ":close");

  title = gtk_window_get_title (GTK_WINDOW (dialog));
  gtk_header_bar_set_title (GTK_HEADER_BAR (header), title ? title : "");

  gtk_window_set_titlebar (GTK_WINDOW (dialog), header);
  gtk_widget_show (header);
}

static void
make_dialog_popup (GtkWidget *dialog)
{
  /* Centring is done when the dialog is mapped, once its content has its
   * final size. */
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_NONE);
  /* Every dialog is a popup above the drawings and the paper page. */
  gtk_window_set_keep_above (GTK_WINDOW (dialog), TRUE);
  dialog_install_border (dialog);
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
   * paper page), which is restored on its own when the dialog closes. */
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
      /* Paper Drawing is the active window again. */
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

/* A fullscreen managed window can lose the window-manager "active" state
 * when an override-redirect dialog/popup takes focus, which makes some panels
 * appear.  Keep the paper above the panel while it is fullscreen. */
static void
paper_update_panel_raise (ScreenatorData *data)
{
  gboolean keep;

  if (!data || !data->paper_win)
    return;

  keep = data->paper_window_fullscreen && !data->paper_window_mode &&
    (data->text_editor != NULL || data->top_dialog != NULL);

  gtk_window_set_keep_above (GTK_WINDOW (data->paper_win), keep);
}

static void
paper_raise_over_panel (ScreenatorData *data)
{
  GdkWindow *window;

  if (!data || !data->paper_win || !data->paper_window_fullscreen ||
      data->paper_window_mode)
    return;

  paper_update_panel_raise (data);
  window = gtk_widget_get_window (data->paper_win);
  if (window)
    gdk_window_raise (window);
}

/* Selecting a tool while the Paper window is behind another application must
 * make the paper usable immediately, with the tool bar still above it. */
static void
paper_bring_to_front (ScreenatorData *data)
{
  GdkWindow *window;

  if (!data || data->paper == SCREENATOR_PAPER_NONE || !data->paper_win)
    return;

  gtk_window_deiconify (GTK_WINDOW (data->paper_win));
  gtk_window_present_with_time (GTK_WINDOW (data->paper_win),
				gtk_get_current_event_time ());
  paper_raise_over_panel (data);

  window = gtk_widget_get_window (data->paper_win);
  if (window)
    gdk_window_raise (window);

  paper_take_focus (data);
  raise_icon_window (data);
}

static gboolean
on_dialog_map (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  GdkWindow *window = gtk_widget_get_window (widget);
  gint width, height;

  if (window)
    {
      GdkScreen *screen = gtk_widget_get_screen (widget);
      GdkRectangle area;
      gint monitor = 0;

      /* No window manager places an override-redirect window: centre it at
       * its real size on the primary monitor. */
      gtk_window_get_size (GTK_WINDOW (widget), &width, &height);

      if (screen)
	{
	  monitor = gdk_screen_get_primary_monitor (screen);
	  if (monitor < 0)
	    monitor = 0;
	  gdk_screen_get_monitor_workarea (screen, monitor, &area);
	}
      else
	{
	  area.x = 0;
	  area.y = 0;
	  area.width = gdk_screen_width ();
	  area.height = gdk_screen_height ();
	}

      gtk_window_move (GTK_WINDOW (widget),
		       area.x + MAX (0, (area.width - width) / 2),
		       area.y + MAX (0, (area.height - height) / 2));

      /* The annotation window and the floating icon are raised above the
       * other windows, so the dialog has to be raised again to stay in front
       * of them. */
      paper_raise_over_panel (data);
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

  if (data->paper != SCREENATOR_PAPER_NONE)
    update_paper_shortcuts (data, TRUE);

  if (data->dialog_released_grab)
    {
      data->dialog_released_grab = FALSE;
      paper_refresh_controls_hover (data);
      acquire_grab (data, NULL);
      if (data->paper != SCREENATOR_PAPER_NONE &&
	  data->paper_hand_tool)
	restore_paper_cursor (data);
    }

  paper_update_panel_raise (data);
}

/* A managed dialog is placed by the window manager.  Do not move or resize it
 * again after mapping: repeated corrections made the startup hints visibly
 * jump around. */
static gboolean
on_managed_dialog_map (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  (void) event;
  dialog_take_focus (data, widget);
  return FALSE;
}

static void
show_dialog_common (ScreenatorData *data, GtkWidget *dialog, gboolean popup)
{
  /* Dialogs belong above the drawings, the paper page and the floating icon,
   * including the startup hints dialog that is a normal managed window. */
  gtk_window_set_keep_above (GTK_WINDOW (dialog), TRUE);
  dialog_install_titlebar (dialog);
  dialog_install_border (dialog);
  screenator_timer_cancel (data);
  paper_finish_temporary_eraser (data);
  screenator_paper_pan_end (data);

  if (popup)
    {
      /* The dialog lives in the same layer as the annotation window, so it
       * can be raised above the drawings and the paper page. */
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
   * entries can be used.  The Paper Drawing shortcuts are released too, so
   * Ctrl+A, Ctrl+S and the other keys keep their normal meaning inside the
   * dialog.  The drawings stay visible behind it. */
  if (!data->top_dialog)
    {
      data->dialog_released_grab = screenator_any_grabbed (data);
      if (data->dialog_released_grab)
	release_grab (data, NULL);
      if (data->paper != SCREENATOR_PAPER_NONE)
	update_paper_shortcuts (data, FALSE);
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
static void paper_store_active_page (ScreenatorData *data);
static void draw_paper_page (ScreenatorData *data, cairo_t *cr);

/* Keep only a few rendered PDF pages alive.  Every page rendered at screen
 * width can take tens of megabytes, so an unbounded cache makes scrolling a
 * long PDF consume huge amounts of memory and eventually lag or swap. */
#define PAPER_PDF_BACKGROUND_CACHE_LIMIT 6
static guint32 paper_page_background_clock;
static void paper_background_invalidate (ScreenatorData *data);
static void paper_background_cache_free (ScreenatorData *data);
static void paper_repaint_window_only (ScreenatorData *data);
void screenator_paper_pan_start (ScreenatorData *data, guint button,
				 gint x, gint y);
void screenator_paper_pan_motion (ScreenatorData *data, gint x, gint y);
void screenator_paper_pan_end (ScreenatorData *data);
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
static gboolean editable_handles_active (ScreenatorData *data);
static void apply_stylus_step (ScreenatorData *data, gboolean down);
static void paper_cycle_setting (ScreenatorData *data, gboolean down);
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
static void on_action_screenshot (GtkMenuItem *menuitem, gpointer user_data);
static void activate_tool_now (ScreenatorData *data, gint pen_tool,
			       gint shape_type);
static gboolean show_math_dialog (ScreenatorData *data);
static void quick_menu_cancel (ScreenatorData *data);
static gboolean eraser_tool_active (ScreenatorData *data);
static gboolean marker_tool_active (ScreenatorData *data);
static void apply_icon_size (ScreenatorData *data, guint new_size);
static void on_icon_timer (GtkMenuItem *menuitem, gpointer user_data);
static void on_icon_switch_mode (GtkMenuItem *menuitem, gpointer user_data);
static void on_icon_change_font (GtkMenuItem *menuitem, gpointer user_data);
static void on_icon_reset_settings (GtkMenuItem *menuitem, gpointer user_data);
static void paper_top_bar_update_images (ScreenatorData *data);
static void paper_top_bar_update_active_tool (ScreenatorData *data);
static void paper_top_bar_reposition (ScreenatorData *data);
static void paper_top_bar_restore (ScreenatorData *data);
static void paper_repaint (ScreenatorData *data);
static void paper_top_bar_destroy (ScreenatorData *data);
static void paper_top_bar_rebuild (ScreenatorData *data);
static gboolean paper_load_pdf_file (ScreenatorData *data, const gchar *path);
static gint paper_pdf_memory_get (ScreenatorData *data, const gchar *path);
static void paper_pdf_memory_set (ScreenatorData *data, const gchar *path,
				  gint page);
static void paper_pdf_remember_current (ScreenatorData *data);
static void schedule_tool_activation (ScreenatorData *data, gint pen_tool,
				      gint shape_type);
static void paper_top_bar_menu_button_clicked (GtkButton *button,
					       gpointer user_data);
static GtkWidget *paper_top_bar_build_opacity_menu (ScreenatorData *data);
static GtkWidget *paper_top_bar_build_marker_opacity_menu (
  ScreenatorData *data);
static void paper_toolbar_set_visible (GtkWidget *button, gboolean show);
static void paper_window_transform_values (ScreenatorData *data,
					   gdouble *scale, gint *origin_x,
					   gint *origin_y);
static void paper_window_transform_event (ScreenatorData *data,
					  gdouble *x, gdouble *y);
static void paper_window_to_root (ScreenatorData *data, gint *x, gint *y);
static gboolean paper_window_event_inside (ScreenatorData *data,
					   gdouble x, gdouble y);
static const gchar *toolbar_item_id (const gchar *id);
static GdkPixbuf *paper_color_swatch (const GdkRGBA *color, gboolean none,
				      gint size);
static GdkPixbuf *paper_thickness_sample (guint width, gint size);
static GdkPixbuf *paper_eraser_size_sample (guint width, gint size);

static gchar *
shortcut_label (ScreenatorData *data, ScreenatorShortcutId id,
		const gchar *base)
{
  const gchar *key = data->shortcuts[id].text;

  if (!key || !*key || g_ascii_strcasecmp (key, "none") == 0)
    return g_strdup (base);

  return g_strdup_printf ("%s (%s)", base, key);
}

static GtkWidget *
shortcut_menu_item (ScreenatorData *data, ScreenatorShortcutId id,
		    const gchar *base)
{
  gchar *label = shortcut_label (data, id, base);
  GtkWidget *item = gtk_image_menu_item_new_with_label (label);

  g_free (label);
  return item;
}


typedef struct
{
  const gchar *id;
  const gchar *label;
  const gchar *icon;
} ToolbarItemInfo;

static const ToolbarItemInfo toolbar_item_info[] =
{
  { "switch_mode",   "Switch Mode",         "switch_mode.png" },
  { "cursor",        "Cursor",              "cursor.png" },
  { "pen",           "Pen",                 "pen.png" },
  { "text",          "Text",                "text.png" },
  { "eraser",        "Eraser",              "eraser.png" },
  { "arrow",         "Arrow",               "arrow.png" },
  { "timer",         "Timer",               "timer.png" },
  { "shapes",        "Shapes",              "shapes.png" },
  { "undo",          "Undo",                "undo.png" },
  { "redo",          "Redo",                "redo.png" },
  { "save",          "Save / Screenshot",   "save.png" },
  { "paper",         "Paper Type",          "paper.png" },
  { "insert_before", "Insert Page Before",  "add_before.png" },
  { "delete_page",   "Delete Page",         "delete-page.png" },
  { "insert_after",  "Insert Page After",   "add_after.png" },
  { "page_number",   "Current Page",        NULL },
  { "goto_page",     "Go to Page",          "goto-page.png" },
  { "clear",         "Clear Screen",        "trash.png" },
  { "visibility",    "Toggle Visibility",   "hide.png" },
  { "eraser_size",   "Eraser Size",         NULL },
  { "line_color",    "Line Color",          NULL },
  { "thickness",     "Thickness",           NULL },
  { "fill_color",    "Fill Color",          NULL },
  { "opacity",       "Opacity",             "opacity.png" },
  { "marker_opacity","Marker Opacity",      "marker_opacity.png" },
  { "settings",      "Settings",            "settings.png" },
  { "hints",         "Show Hints",          "hint.png" },
  { "quit",          "Quit",                "quit.png" }
};


/* Use bilinear filtering for screen-sized image surfaces.  Cairo's default
 * GOOD filter is dramatically slower when downscaling the full backbuffer or
 * paper in Window mode (tens of milliseconds per frame), while bilinear is a
 * few milliseconds and looks the same at these scale factors. */
static void
use_bilinear_filter (cairo_t *cr)
{
  cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_BILINEAR);
}

/* Optional diagnostics: SCREENATOR_PROFILE=1 prints frame and scroll timings
 * to stderr.  Disabled by default with no measurable overhead. */
static gboolean
screenator_profile_enabled (void)
{
  static gint enabled = -1;

  if (enabled < 0)
    enabled = g_getenv ("SCREENATOR_PROFILE") != NULL ? 1 : 0;

  return enabled == 1;
}

static void
profile_expose (ScreenatorData *data, cairo_t *cr, gint64 start)
{
  ScreenatorPaperDocument *doc;
  gdouble x1, y1, x2, y2;
  guint pages = 0;

  cairo_clip_extents (cr, &x1, &y1, &x2, &y2);
  doc = paper_active_document (data);
  if (doc && doc->pages)
    pages = doc->pages->len;

  g_printerr ("PROFILE expose %.2f ms clip %.0fx%.0f window=%d paper=%d "
	      "continuous=%d pages=%u\n",
	      (g_get_monotonic_time () - start) / 1000.0,
	      x2 - x1, y2 - y1,
	      data->paper_window_mode ? 1 : 0, (gint) data->paper,
	      paper_continuous (data) ? 1 : 0, pages);
}

static void
draw_screenshot_paper (ScreenatorData *data, cairo_t *cr)
{
  if (!data->screenshot_paper)
    return;

  cairo_save (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface (cr, data->screenshot_paper, 0, 0);
  use_bilinear_filter (cr);
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
  gboolean profile = screenator_profile_enabled ();
  gint64 profile_start = profile ? g_get_monotonic_time () : 0;

  if(data->debug)
    g_printerr("DEBUG: got draw event\n");

  /* Window mode scales the paper page so its width matches the Paper
   * window.  All drawing still happens in screen/backbuffer coordinates. */
  if (data->paper_window_mode && data->win == data->paper_win)
    {
      /* A tall or tiled window can be higher than the scaled screen content.
       * Paint the whole window with the paper surround first; otherwise the
       * uncovered lower area contains stale/garbage pixels. */
      cairo_save (cr);
      cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
      cairo_set_source_rgb (cr, 0.92, 0.92, 0.92);
      cairo_paint (cr);
      cairo_restore (cr);
    }

  if (data->paper_window_mode && data->win == data->paper_win)
    {
      gdouble scale;
      gint origin_x, origin_y;

      paper_window_transform_values (data, &scale, &origin_x, &origin_y);
      cairo_save (cr);
      cairo_scale (cr, scale, scale);
      cairo_translate (cr, -origin_x, -origin_y);
    }

  /* The visibility toggle hides the drawings but keeps the paper page. */
  if (data->hide_drawings)
    {
      if (data->screenshot_paper)
	draw_screenshot_paper (data, cr);
      else if (data->paper != SCREENATOR_PAPER_NONE)
	draw_paper_page (data, cr);

      if (data->paper_window_mode && data->win == data->paper_win)
	cairo_restore (cr);

      if (profile)
	profile_expose (data, cr, profile_start);
      return TRUE;
    }

  /* The paper page is drawn first, so it stays below the annotations and is
   * not part of the backbuffer: clearing the screen leaves it in place. */
  if (data->screenshot_paper)
    {
      draw_screenshot_paper (data, cr);

      cairo_save (cr);
      cairo_set_source_surface (cr, data->backbuffer, 0, 0);
      use_bilinear_filter (cr);
      cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
      cairo_paint (cr);
      cairo_restore (cr);
    }
  else if (data->paper != SCREENATOR_PAPER_NONE)
    {
      draw_paper_page (data, cr);

      cairo_save (cr);
      cairo_set_source_surface (cr, data->backbuffer, 0, 0);
      use_bilinear_filter (cr);
      cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
      cairo_paint (cr);
      cairo_restore (cr);
    }
  else
    {
      cairo_save (cr);
      cairo_set_source_surface (cr, data->backbuffer, 0, 0);
      use_bilinear_filter (cr);
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
      use_bilinear_filter (cr);
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
   * input; the caret is drawn here as well so drawings and the paper page
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

  if (data->paper_window_mode && data->win == data->paper_win)
    cairo_restore (cr);

  if (profile)
    profile_expose (data, cr, profile_start);

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



/* Copy a surface into one of a different size, scaling its contents to fit.
 * Used to keep paper annotations proportional when a page is refitted. */
static cairo_surface_t *
resize_surface_scale (cairo_surface_t *source, gint width, gint height)
{
  cairo_surface_t *surface =
    cairo_image_surface_create (CAIRO_FORMAT_ARGB32, MAX (1, width),
				MAX (1, height));

  if (source)
    {
      gint source_width = cairo_image_surface_get_width (source);
      gint source_height = cairo_image_surface_get_height (source);
      cairo_t *cr = cairo_create (surface);

      if (source_width > 0 && source_height > 0)
	cairo_scale (cr, (gdouble) MAX (1, width) / source_width,
		     (gdouble) MAX (1, height) / source_height);
      cairo_set_source_surface (cr, source, 0, 0);
      cairo_paint (cr);
      cairo_destroy (cr);
    }

  return surface;
}

/* Scale surface uniformly to fit inside width x height, centring it. */
static cairo_surface_t *
resize_surface_contain (cairo_surface_t *source, gint width, gint height)
{
  cairo_surface_t *surface =
    cairo_image_surface_create (CAIRO_FORMAT_ARGB32, MAX (1, width),
				MAX (1, height));

  if (source)
    {
      gint sw = cairo_image_surface_get_width (source);
      gint sh = cairo_image_surface_get_height (source);

      if (sw > 0 && sh > 0)
	{
	  gdouble scale = MIN ((gdouble) width / sw, (gdouble) height / sh);
	  cairo_t *cr = cairo_create (surface);

	  cairo_translate (cr, (width - sw * scale) / 2.0,
			   (height - sh * scale) / 2.0);
	  cairo_scale (cr, scale, scale);
	  cairo_set_source_surface (cr, source, 0, 0);
	  cairo_paint (cr);
	  cairo_destroy (cr);
	}
    }

  return surface;
}

/* Scale a page's recorded vector layer to a new page size while keeping it a
 * recording surface.  Strokes stay vector and text stays text; the recording
 * is only replayed under a scale transform. */
static void
paper_scale_vector_surface (ScreenatorPaperPage *page, gint width, gint height,
			    gboolean contain)
{
  cairo_rectangle_t extents;
  cairo_surface_t *scaled;
  cairo_t *cr;

  if (!page || !page->vector || !page->vector_valid ||
      width <= 0 || height <= 0)
    return;

  if (!cairo_recording_surface_get_extents (page->vector, &extents) ||
      extents.width <= 0 || extents.height <= 0)
    return;

  if ((gint) round (extents.width) == width &&
      (gint) round (extents.height) == height)
    return;

  {
    cairo_rectangle_t target = { 0, 0, width, height };

    scaled = cairo_recording_surface_create (CAIRO_CONTENT_COLOR_ALPHA,
					     &target);
  }

  if (cairo_surface_status (scaled) != CAIRO_STATUS_SUCCESS)
    {
      cairo_surface_destroy (scaled);
      return;
    }

  cr = cairo_create (scaled);
  if (contain)
    {
      gdouble scale = MIN ((gdouble) width / extents.width,
			   (gdouble) height / extents.height);

      cairo_translate (cr, (width - extents.width * scale) / 2.0,
		       (height - extents.height * scale) / 2.0);
      cairo_scale (cr, scale, scale);
    }
  else
    cairo_scale (cr, (gdouble) width / extents.width,
		 (gdouble) height / extents.height);
  cairo_set_source_surface (cr, page->vector, 0, 0);
  cairo_paint (cr);
  cairo_destroy (cr);

  cairo_surface_destroy (page->vector);
  page->vector = scaled;
  page->vector_valid = TRUE;
}

void on_monitors_changed ( GdkScreen *screen,
			   gpointer   user_data) 
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  gint new_width, new_height;

  (void) screen;

  /* A resolution change invalidates the current tool; leave Cursor selected. */
  screenator_exit_tool (data);

  new_width = gdk_screen_get_width (data->screen);
  new_height = gdk_screen_get_height (data->screen);

  /* Store the active page with the old screen size before changing
   * data->width/height. */
  if (data->paper != SCREENATOR_PAPER_NONE)
    paper_store_active_page (data);

  data->width = new_width;
  data->height = new_height;
  data->paper_view_height_current = new_height;

  if(data->debug)
    g_printerr("DEBUG: screen size changed to %d x %d!\n", data->width, data->height);

  // change size
  gtk_widget_set_size_request(GTK_WIDGET(data->win), data->width, data->height);
  // try to set transparent for input
  cairo_region_t* r =  cairo_region_create();
  gtk_widget_input_shape_combine_region(data->win, r);
  cairo_region_destroy(r);

  {
    cairo_surface_t *new_shape =
      resize_surface_scale (data->backbuffer, data->width, data->height);
    gint i;

    cairo_surface_destroy (data->backbuffer);
    data->backbuffer = new_shape;

    /* Undo snapshots must match the new drawing-buffer size. */
    for (i = 0; i < SCREENATOR_MAX_UNDO; i++)
      {
	if (data->undobuffer[i])
	  cairo_surface_destroy (data->undobuffer[i]);
	data->undobuffer[i] =
	  cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
				      data->width, data->height);
      }
    data->undo_head = 0;
    data->undo_depth = 0;
    data->redo_depth = 0;
    reset_undo_history (data);
  }

  screenator_page_history_clear (data);

  {
    cairo_surface_t *surface;
    ScreenatorPaperType type;
    guint j;

    if (data->paper != SCREENATOR_PAPER_NONE && data->screen_annotations)
      {
	surface = resize_surface_scale (data->screen_annotations,
					data->width, data->height);
	cairo_surface_destroy (data->screen_annotations);
	data->screen_annotations = surface;
      }

    /* Refit every stored non-PDF page, so a paper activated later also fits
     * the new screen. */
    for (type = SCREENATOR_PAPER_PLAIN;
	 type <= SCREENATOR_PAPER_PDF; type++)
      {
	ScreenatorPaperDocument *doc = data->paper_docs[type];

	if (!doc || !doc->pages)
	  continue;

	for (j = 0; j < doc->pages->len; j++)
	  {
	    ScreenatorPaperPage *page =
	      &g_array_index (doc->pages, ScreenatorPaperPage, j);

	    if (page->style != SCREENATOR_PAPER_PDF)
	      {
		if (data->paper_size == SCREENATOR_PAPER_SIZE_SCREEN)
		  {
		    if (page->annotations)
		      {
			/* Scale the drawing with the paper, exactly like a PDF page.
			 * Copying it at the old size crops/pads instead. */
			surface = resize_surface_contain (page->annotations,
							data->width,
							data->height);
			cairo_surface_destroy (page->annotations);
			page->annotations = surface;
		      }

		    /* Keep the vector layer vector by replaying it with a scale. */
		    paper_scale_vector_surface (page, data->width, data->height,
						TRUE);

		    page->canvas_width = data->width;
		    page->canvas_height = data->height;
		    page->pdf_width = data->width;
		    page->pdf_height = data->height;
		    page->view_x = 0;
		    page->view_y = 0;
		  }
		else
		  {
		    page->view_x = CLAMP (page->view_x, 0,
					  MAX (0, page->canvas_width -
					       (gint) data->width));
		    page->view_y = CLAMP (page->view_y, 0,
					  MAX (0, page->canvas_height -
					       (gint) data->height));
		  }
	      }
	  }
      }

    /* Refit a loaded PDF to the new screen width, even when Paper Drawing is
     * currently off. */
    {
      ScreenatorPaperDocument *pdf =
	paper_document (data, SCREENATOR_PAPER_PDF);

      if (pdf && pdf->pdf_loaded && pdf->pages && pdf->pages->len > 0)
	{
	  gint old_page = CLAMP ((gint) pdf->page, 0,
				 (gint) pdf->pages->len - 1);
	  gint old_top = 0, old_height = 0;
	  gdouble relative = 0.0;

	  if (pdf->pdf_page_tops && pdf->pdf_page_heights &&
	      pdf->pdf_page_tops->len > (guint) old_page &&
	      pdf->pdf_page_heights->len > (guint) old_page)
	    {
	      old_top = g_array_index (pdf->pdf_page_tops, gint, old_page);
	      old_height = g_array_index (pdf->pdf_page_heights, gint,
					  old_page);
	      if (old_height > 0)
		relative = ((gdouble) pdf->pdf_scroll_y - old_top) /
			   old_height;
	    }

	  /* This scales each page's annotations and invalidates its rendered
	   * background. */
	  paper_pdf_build_layout (data, pdf);
	  pdf->page = MIN ((guint) old_page, pdf->pages->len - 1);

	  if (data->paper == SCREENATOR_PAPER_PDF &&
	      data->paper_continuous_scroll &&
	      pdf->pdf_page_tops &&
	      pdf->pdf_page_tops->len > pdf->page)
	    {
	      gint new_top = g_array_index (pdf->pdf_page_tops, gint,
					    pdf->page);
	      gint new_height = g_array_index (pdf->pdf_page_heights,
					       gint, pdf->page);

	      pdf->pdf_scroll_y =
		new_top + (gint) round (relative * new_height);
	      pdf->pdf_scroll_y =
		CLAMP (pdf->pdf_scroll_y, 0,
		       MAX (0, pdf->pdf_stack_height - (gint) data->height));
	      paper_pdf_prepare_active (data, pdf);
	    }
	  else if (data->paper == SCREENATOR_PAPER_PDF)
	    {
	      ScreenatorPaperPage *page = paper_current_page (pdf);

	      if (page)
		{
		  page->view_x = 0;
		  page->view_y = 0;
		  paper_ensure_background (data, pdf, pdf->page, page);
		  paper_load_page (data, page);
		}
	    }
	  else
	    {
	      pdf->pdf_scroll_y =
		CLAMP (pdf->pdf_scroll_y, 0,
		       MAX (0, pdf->pdf_stack_height - (gint) data->height));
	    }

	  data->paper_pdf_page = (gint) pdf->page;
	}
    }

    if (data->paper != SCREENATOR_PAPER_NONE &&
	data->paper != SCREENATOR_PAPER_PDF)
      {
	ScreenatorPaperDocument *doc = paper_active_document (data);
	ScreenatorPaperPage *page = paper_current_page (doc);

	if (paper_continuous (data))
	  {
	    paper_build_continuous_layout (data, doc);
	    doc->pdf_scroll_y = CLAMP (
	      (doc->pdf_page_tops && doc->page < doc->pdf_page_tops->len)
	      ? g_array_index (doc->pdf_page_tops, gint, doc->page) : 0,
	      0, MAX (0, doc->pdf_stack_height - (gint) data->height));
	    paper_pdf_prepare_active (data, doc);
	  }
	else
	  paper_load_page (data, page);
      }

    if (data->paper != SCREENATOR_PAPER_NONE)
      reset_undo_history (data);
  }

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

  /* Keep the floating icon and the paper tool bar in place after a
   * resolution change. */
  screenator_position_icon (data);
  paper_top_bar_reposition (data);

  setup_input_devices(data);

  /* parse_config() above may have restored a saved tool.  Force the Cursor
   * tool and refresh its toolbar icon after rebuilding the input devices. */
  screenator_activate_cursor_state (data);

  gtk_widget_show_all (data->win);

  /* Showing the annotation window again puts it above the floating icon. */
  raise_icon_window (data);

  paper_refresh_active_cursor (data);

  if (data->paper != SCREENATOR_PAPER_NONE)
    paper_repaint (data);
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
  screenator_invalidate_rect (data, &rect);
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
    screenator_invalidate_rect (data, &old_bounds);

  if (has_new)
    screenator_invalidate_rect (data, &new_bounds);

  /* The caret moves with the text. */
  {
    GdkRectangle caret;

    if (text_caret_bounds (data, &caret))
      screenator_invalidate_rect (data, &caret);
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
    screenator_invalidate_rect (data, &data->text_preview_bounds);

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
    screenator_invalidate_rect (data, &caret);

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
  screenator_invalidate_rect (data, &area);
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
  paper_window_to_root (data, &x, &y);
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
	screenator_invalidate_rect (data, &caret);
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

  /* Move focus back before destroying the focused popup so the window manager
   * never sees a frame with no active window (which reveals the panel). */
  if (data->paper != SCREENATOR_PAPER_NONE)
    paper_take_focus (data);

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

  /* A paper is the active window again after the editor closes.  Release
   * the temporary keep-above only after focus is back on the paper, otherwise
   * the desktop panel flashes for one frame. */
  if (data->paper != SCREENATOR_PAPER_NONE)
    paper_take_focus (data);

  paper_update_panel_raise (data);
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
clear_text_focus_commit (gpointer user_data)
{
  ScreenatorData *data = user_data;

  data->text_focus_commit_id = 0;
  data->text_focus_committed = FALSE;
  return G_SOURCE_REMOVE;
}

static void
mark_text_focus_commit (ScreenatorData *data)
{
  data->text_focus_committed = TRUE;
  if (data->text_focus_commit_id)
    g_source_remove (data->text_focus_commit_id);
  data->text_focus_commit_id =
    g_timeout_add (500, clear_text_focus_commit, data);
}

static gboolean
on_text_editor_focus_out (GtkWidget *widget, GdkEventFocus *event,
			  gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  if (data->text_editor_active && data->text_editor)
    {
      /* The click that removed focus must not also act as Escape or as the
       * temporary eraser.  Consume it once it reaches the paper window. */
      mark_text_focus_commit (data);
      close_text_editor (data, TRUE);
    }

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

  if (data->text_entry)
    gtk_widget_grab_focus (data->text_entry);

  paper_raise_over_panel (data);

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
  data->text_focus_committed = FALSE;
  data->text_commit_suppress_button = 0;
  if (data->text_focus_commit_id)
    {
      g_source_remove (data->text_focus_commit_id);
      data->text_focus_commit_id = 0;
    }
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

  editor = gtk_window_new (GTK_WINDOW_POPUP);
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
  /* A popup is override-redirect, so it must be made override-redirect before
   * it is mapped on every backend. */
  g_signal_connect (editor, "realize",
		    G_CALLBACK (on_dialog_realize), data);

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
  paper_raise_over_panel (data);

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
      screenator_invalidate_rect (data, &caret);
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

  /* While a node or one of its Bezier handles is being dragged, the curve
   * must end at the real last node.  Appending the pointer node here would
   * draw a stray segment that is not part of the spline. */
  if (!devdata->spline_node_drag && !devdata->spline_handle_drag &&
      !devdata->spline_preview_paused)
    {
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
      screenator_mark_modified (data);
      screenator_invalidate_rect (data, bounds);
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
  devdata->spline_preview_paused = FALSE;

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
  devdata->spline_preview_paused = FALSE;
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
      screenator_paper_record_spline (data, devdata->shape_points);

      screenator_mark_modified (data);
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
  screenator_mark_modified (data);
  screenator_invalidate_rect (data, &bounds);
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
  screenator_mark_modified (data);
  screenator_invalidate_rect (data, &bounds);
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
  screenator_mark_modified (data);
  screenator_invalidate_rect (data, &bounds);
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
  screenator_mark_modified (data);
  screenator_invalidate_rect (data, &bounds);
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

/* Grab the screen without Screenator's floating controls.  When
 * hide_annotation is TRUE the drawing layer is hidden as well, which is what
 * the frozen background for a delayed tool needs.  A screenshot key press
 * keeps the drawings visible. */
static cairo_surface_t *
capture_screen_content (ScreenatorData *data, gboolean hide_annotation)
{
  GdkWindow *root;
  GdkPixbuf *pixbuf;
  cairo_surface_t *surface;
  gboolean win_visible, icon_visible, settings_visible, bar_visible;

  if (!data->win || !data->display || !data->width || !data->height)
    return NULL;

  win_visible = gtk_widget_get_visible (data->win);
  icon_visible = data->icon_win && gtk_widget_get_visible (data->icon_win);
  settings_visible = data->settings_win &&
    gtk_widget_get_visible (data->settings_win);
  bar_visible = data->paper_top_bar &&
    gtk_widget_get_visible (data->paper_top_bar);

  if (bar_visible)
    gtk_widget_hide (data->paper_top_bar);
  if (settings_visible)
    gtk_widget_hide (data->settings_win);
  if (icon_visible)
    gtk_widget_hide (data->icon_win);
  if (win_visible && hide_annotation)
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

  if (win_visible)
    gtk_widget_show (data->win);
  if (icon_visible)
    gtk_widget_show (data->icon_win);
  if (settings_visible)
    gtk_widget_show (data->settings_win);
  if (bar_visible)
    gtk_widget_show (data->paper_top_bar);

  if (data->win && gtk_widget_get_window (data->win))
    {
      gdk_window_raise (gtk_widget_get_window (data->win));
      gdk_window_invalidate_rect (gtk_widget_get_window (data->win), NULL, 0);
    }
  raise_icon_window (data);
  update_settings_indicator (data);

  return surface;
}

/* Freeze the current screen into a temporary background.  Hiding the overlay
 * and icon first keeps the screenshot free of Screenator's own windows. */
void
screenator_take_screenshot_paper (ScreenatorData *data)
{
  cairo_surface_t *surface;

  screenator_clear_screenshot_paper (data);

  surface = capture_screen_content (data, TRUE);

  if (surface)
    data->screenshot_paper = surface;
  else
    g_printerr ("Screenshot capture failed; using the live screen.\n");

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

static gchar *
screenshot_directory (void)
{
  const gchar *pictures =
    g_get_user_special_dir (G_USER_DIRECTORY_PICTURES);

  if (pictures && *pictures)
    return g_strdup (pictures);

  return g_build_filename (g_get_home_dir (), "Pictures", NULL);
}

/* F4 in Screen or Paper mode: save the current screen, including the
 * annotations or paper page and without the floating controls, to the
 * Pictures directory as Screenator-YYYYMMDD-HHMMSS.png. */
void
screenator_save_screenshot (ScreenatorData *data)
{
  cairo_surface_t *surface;
  GDateTime *now;
  gchar *directory;
  gchar *name;
  gchar *path;

  surface = capture_screen_content (data, FALSE);
  if (!surface)
    {
      g_printerr ("Screenshot capture failed; no file was saved.\n");
      return;
    }

  directory = screenshot_directory ();
  g_mkdir_with_parents (directory, 0755);

  now = g_date_time_new_now_local ();
  name = g_date_time_format (now, "Screenator-%Y%m%d-%H%M%S.png");
  path = g_build_filename (directory, name, NULL);

  cairo_surface_flush (surface);
  {
    gboolean saved =
      cairo_surface_write_to_png (surface, path) == CAIRO_STATUS_SUCCESS;
    GtkWidget *dialog = gtk_message_dialog_new (
      NULL, GTK_DIALOG_MODAL,
      saved ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR,
      GTK_BUTTONS_OK,
      saved ? "Screenshot saved to:\n%s" : "Could not save the screenshot to:\n%s",
      path);

    gtk_window_set_title (GTK_WINDOW (dialog),
			  saved ? "Screenshot saved" : "Screenshot failed");
    show_dialog_above_drawings (data, dialog);
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
  }

  cairo_surface_destroy (surface);
  g_free (path);
  g_free (name);
  g_date_time_unref (now);
  g_free (directory);
}

/* Clear the selected drawing tool and make Cursor the active tool again.
 * Cursor is represented by an empty forced_context and pen_tool 0. */
static void
screenator_activate_cursor_state (ScreenatorData *data)
{
  screenator_timer_cancel (data);

  if (data->text_editor)
    close_text_editor (data, FALSE);

  screenator_paper_pan_end (data);
  paper_finish_temporary_eraser (data);

  if (screenator_any_grabbed (data))
    release_grab (data, NULL);

  data->paper_hand_tool = FALSE;
  data->forced_context = NULL;
  data->pen_tool = 0;

  /* Drop cached per-device tool contexts as well, otherwise re-grabbing the
   * pointer could show the previous tool's cursor while Cursor is active. */
  if (data->devdatatable)
    {
      GHashTableIter it;
      gpointer value;

      g_hash_table_iter_init (&it, data->devdatatable);
      while (g_hash_table_iter_next (&it, NULL, &value))
	{
	  ScreenatorDeviceData *devdata = value;

	  devdata->cur_context = NULL;
	  devdata->saved_context = NULL;
	}
    }

  restore_paper_cursor (data);
  paper_top_bar_update_active_tool (data);

  if (data->paper != SCREENATOR_PAPER_NONE)
    paper_take_focus (data);
}

gboolean
screenator_exit_tool (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;
  gboolean handled = FALSE;
  gboolean finished_handles = FALSE;
  gboolean had_tool;

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
	  finished_handles = TRUE;
	}
      else if (devdata->line_active)
	{
	  line_finish (data, devdata);
	  handled = TRUE;
	  finished_handles = TRUE;
	}
      else if (devdata->cur_context &&
	       devdata->cur_context->type == SCREENATOR_SHAPE &&
	       data->shape_type == SCREENATOR_SHAPE_SPLINE &&
	       devdata->shape_active)
	{
	  spline_finish (data, devdata);
	  handled = TRUE;
	  finished_handles = TRUE;
	}
      else if (devdata->cur_context &&
	       devdata->cur_context->type == SCREENATOR_SHAPE &&
	       data->shape_type == SCREENATOR_SHAPE_STAR &&
	       devdata->star_active)
	{
	  star_finish (data, devdata);
	  handled = TRUE;
	  finished_handles = TRUE;
	}
      else if (devdata->cur_context &&
	       devdata->cur_context->type == SCREENATOR_SHAPE &&
	       data->shape_type == SCREENATOR_SHAPE_TRIANGLE &&
	       devdata->triangle_active)
	{
	  triangle_finish (data, devdata);
	  handled = TRUE;
	  finished_handles = TRUE;
	}
    }

  /* The first Escape/right click commits the handles but keeps the tool
   * selected.  Another Escape then exits the tool and selects Cursor. */
  if (finished_handles)
    return TRUE;

  if (screenator_any_grabbed (data))
    {
      screenator_marker_leave (data);
      screenator_clear_screenshot_paper (data);
      release_grab (data, NULL);
      handled = TRUE;
    }

  /* Exiting a tool always leaves the Cursor tool selected, so the next click
   * does not start another stroke and the Cursor icon is highlighted. */
  had_tool = data->forced_context != NULL || data->pen_tool != 0 ||
    data->timer_active || data->paper_hand_tool;

  if (handled || had_tool)
    {
      screenator_activate_cursor_state (data);
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
  if (data->top_dialog || data->native_dialog_open)
    return TRUE;

  if (!paper_window_event_inside (data, ev->x, ev->y))
    return TRUE;

  paper_window_transform_event (data, &ev->x, &ev->y);

  /* A click on the drawing area outside the quick tool grid dismisses the
   * grid and is consumed. */
  if (data->quick_menu)
    {
      quick_menu_cancel (data);
      return TRUE;
    }

  /* Committing the text closed the editor on focus-out.  The same click must
   * not close the new text handles or start the paper eraser, in either
   * mode.  Button 1 is allowed through so it can start a new text. */
  if (data->text_focus_committed)
    {
      if (data->text_focus_commit_id)
	{
	  g_source_remove (data->text_focus_commit_id);
	  data->text_focus_commit_id = 0;
	}
      data->text_focus_committed = FALSE;

      if (ev->button == 2 || ev->button == 3)
	{
	  data->text_commit_suppress_button = ev->button;
	  data->text_pressed = FALSE;
	  return TRUE;
	}
    }

  /* Crossing events can be stale after using the controls; pick the correct
   * pointer for this press before an eraser cursor is chosen. */
  paper_refresh_controls_hover (data);

  /* get the data for this device */
  ScreenatorDeviceData *devdata = g_hash_table_lookup(data->devdatatable, ev->device);

  if(data->debug)
    g_printerr("DEBUG: Device '%s': Button %i Down State %d at (x,y)=(%.2f : %.2f)\n",
	       gdk_device_get_name(ev->device), ev->button, ev->state, ev->x, ev->y);

  /* The text editor disables the mouse: every button is swallowed until the
   * typed text is accepted.  Right click, stylus side button 2 and mouse
   * button 2 accept the text exactly like Enter; Escape still cancels it.
   * The stylus first side button keeps changing the text colour. */
  if (data->text_editor)
    {
      if (event_source_is_stylus ((GdkEvent *) ev) && ev->button == 2)
	{
	  apply_scroll_step (data, TRUE, TRUE);
	  return TRUE;
	}

      if (ev->button == 2 || ev->button == 3)
	{
	  data->text_commit_suppress_button = ev->button;
	  close_text_editor (data, TRUE);
	  return TRUE;
	}

      data->text_pressed = FALSE;
      return TRUE;
    }

  /* In Paper Drawing the middle mouse button and stylus side button 1 pan
   * the paper, and the right mouse button and stylus side button 2 erase
   * only while they are held.  This works with or without an active tool. */
  if (data->paper != SCREENATOR_PAPER_NONE)
    {
      if (ev->button == 2)
	{
	  /* With a placed shape or text selected, the stylus first side
	   * button recolours it instead of starting a pan. */
	  if (event_source_is_stylus ((GdkEvent *) ev) &&
	      editable_handles_active (data))
	    {
	      apply_stylus_step (data, TRUE);
	      return TRUE;
	    }

	  paper_commit_pending (data);
	  screenator_paper_pan_start (data, 2, ev->x, ev->y);
	  return TRUE;
	}

      if (ev->button == 3)
	{
	  /* While a shape is still being dragged or a placed shape or text
	   * still shows its handles, the right button finishes it like
	   * Escape.  Only after it is committed does it become the
	   * temporary eraser, which keeps the selected tool. */
	  if (editable_handles_active (data))
	    {
	      screenator_exit_tool (data);
	      return TRUE;
	    }

	  if (!devdata || data->paper_eraser_active)
	    return TRUE;

	  data->paper_eraser_was_grabbed = devdata->is_grabbed;

	  /* Without an active tool the window is not grabbed; take the
	   * pointer for the duration of the eraser stroke. */
	  if (!devdata->is_grabbed)
	    acquire_grab (data, ev->device);

	  if (!devdata->is_grabbed)
	    return TRUE;

	  paper_commit_pending (data);
	  stroke_finish (data, ev->device);

	  if (devdata->cur_context != data->default_eraser)
	    {
	      devdata->saved_context = devdata->cur_context;
	      devdata->cur_context = data->default_eraser;
	    }

	  data->paper_eraser_active = TRUE;
	  data->paper_eraser_device = ev->device;

	  /* `stylus_eraser_active` tells the motion handler that the eraser is
	   * held even when the device does not report the side button in the
	   * motion state.  Without it a stylus would erase only at the press. */
	  devdata->stylus_eraser_active = TRUE;
	  set_device_cursor (data, ev->device, data->default_eraser);

	  /* Fall through to the normal drawing code so the eraser starts on
	   * this press. */
	}
    }

  /* In the continuous view, route the stroke to the page under the
   * pointer. */
  if (devdata->is_grabbed && paper_continuous (data) && ev->button == 1)
    paper_pdf_pick_page (data, ev->y);

  if (!devdata->is_grabbed)
    return FALSE;

  /* The right button is a dual-action button: a short click acts like Escape
   * / right click and a long press opens the mouse wheel menu.  On a stylus
   * the same applies to the second side button (button 3), which opens the
   * Button 1 Mode menu instead.  In Paper Drawing the right button is the
   * temporary eraser instead. */
  if (ev->button == 3 && data->paper == SCREENATOR_PAPER_NONE)
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
   * colors or thicknesses, a long press activates the eraser.  In Paper
   * Drawing this button pans instead. */
  if (event_source_is_stylus ((GdkEvent *) ev) && ev->button == 2 &&
      data->paper == SCREENATOR_PAPER_NONE)
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
  if (!devdata->stylus_eraser_active && !data->paper_eraser_active &&
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
		  devdata->spline_preview_paused = TRUE;
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
		  devdata->spline_preview_paused = TRUE;
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

  if (data->top_dialog || data->native_dialog_open)
    return TRUE;

  if (!paper_window_event_inside (data, ev->x, ev->y))
    return TRUE;

  paper_window_transform_event (data, &ev->x, &ev->y);

  /* get the data for this device */
  ScreenatorDeviceData *devdata = g_hash_table_lookup(data->devdatatable, ev->device);
  gboolean stylus = event_source_is_stylus ((GdkEvent *) ev);

  /* Leaving the tool bar can be reported only as a motion event over the
   * canvas.  Refresh the stale control-hover state here so the newly selected
   * tool cursor appears as soon as the pointer moves back to the drawing
   * area, without waiting for a click. */
  if (data->controls_hover > 0)
    {
      paper_refresh_controls_hover (data);
      paper_update_hover_cursor (data);
    }

  if (data->paper_panning)
    {
      screenator_paper_pan_motion (data, ev->x, ev->y);
      return TRUE;
    }

  if (!devdata->is_grabbed)
    return FALSE;

  /* The second side button is the Escape/menu button; moving the pen while it
   * is held must not draw a line.  In Paper Drawing the right button is the
   * temporary eraser and must keep drawing. */
  if (!data->paper_eraser_active &&
      (devdata->menu_button_down ||
       (ev->state & GDK_BUTTON3_MASK) != 0))
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
  if (!devdata->stylus_eraser_active && !data->paper_eraser_active &&
      !data->paper_hand_tool && !devdata->saved_context &&
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
	  screenator_invalidate_rect (data, &dirty);
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

	      paper_window_transform_event (data, &x, &y);

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
  ScreenatorDeviceData *devdata;
  gdouble raw_x, raw_y;

  if (data->top_dialog || data->native_dialog_open)
    return TRUE;

  raw_x = ev->x;
  raw_y = ev->y;
  paper_window_transform_event (data, &ev->x, &ev->y);

  if (data->paper != SCREENATOR_PAPER_NONE && data->paper_eraser_active &&
      ev->button == 3 && data->paper_eraser_device != ev->device)
    return TRUE;

  devdata = g_hash_table_lookup(data->devdatatable, ev->device);

  gfloat direction = 0;
  gint width = 0;
  if(devdata->cur_context)
    width = devdata->cur_context->arrowsize * devdata->cur_context->width / 2;
   

  /* The press that committed the text is consumed here as well, so the
   * paper never treats it as a finished handle or a temporary eraser. */
  if (data->text_commit_suppress_button == ev->button)
    {
      data->text_commit_suppress_button = 0;
      data->text_pressed = FALSE;
      return TRUE;
    }

  /* The text editor disables the mouse; ignore every button until the typed
   * text is accepted with Enter. */
  if (data->text_editor)
    {
      data->text_pressed = FALSE;
      return TRUE;
    }

  if (data->paper_panning && ev->button == data->paper_pan_button)
    {
      screenator_paper_pan_end (data);
      return TRUE;
    }

  /* Right button: a short click performs Escape; a long press has opened the
   * mode menu and consumes its own release.  In Paper Drawing it ends the
   * temporary eraser stroke instead. */
  if (ev->button == 3 && data->paper == SCREENATOR_PAPER_NONE)
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

  if (paper_window_event_inside (data, raw_x, raw_y) &&
      ((ev->x != devdata->lastx) ||
       (ev->y != devdata->lasty)))
    {
      /* on_motion() transforms window coordinates itself; give it the raw
       * release position so it is not transformed a second time. */
      gdouble transformed_x = ev->x;
      gdouble transformed_y = ev->y;

      ev->x = raw_x;
      ev->y = raw_y;
      on_motion(win, (GdkEventMotion *) ev, user_data);
      ev->x = transformed_x;
      ev->y = transformed_y;
    }

  if (!devdata->is_grabbed)
    {
      /* Recover even if the grab was broken while the eraser was held. */
      if (data->paper_eraser_active && data->paper_eraser_device == ev->device)
	paper_finish_temporary_eraser (data);
      return FALSE;
    }

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

	      screenator_mark_modified (data);
	      data->painted = 1;
	    }

	  shape_get_bounds (data, data->shape_type,
			    devdata->shape_x0, devdata->shape_y0,
			    devdata->shape_x1, devdata->shape_y1,
			    &bounds);
	  if (bounds.width > 0 && bounds.height > 0)
	    {
	      screenator_paper_mark_ink (data, &bounds);
	      screenator_invalidate_rect (data, &bounds);
	    }
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

  if (data->paper_eraser_active && data->paper_eraser_device == ev->device)
    paper_finish_temporary_eraser (data);

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
    screenator_request_quit (data);
  else if (action == GA_UNDO)
    screenator_undo (data);
  else if (action == GA_REDO)
    screenator_redo (data);
  else if (action == GA_OPENPDF)
    {
      gchar *handoff =
	g_build_filename (g_get_user_runtime_dir (),
			  "screenator-open-pdf", NULL);
      gchar *path = NULL;

      if (g_file_get_contents (handoff, &path, NULL, NULL))
	{
	  g_strstrip (path);
	  if (*path)
	    screenator_open_pdf_path (data, path);
	  g_free (path);
	  g_remove (handoff);
	}
      g_free (handoff);
    }
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

static void
on_quit (GtkMenuItem *menuitem, gpointer user_data)
{
  (void) menuitem;
  screenator_request_quit ((ScreenatorData *) user_data);
}

static void
on_action_screenshot (GtkMenuItem *menuitem, gpointer user_data)
{
  (void) menuitem;
  screenator_save_screenshot ((ScreenatorData *) user_data);
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
  screenator_undo (data);
}

void on_redo(GtkMenuItem *menuitem,
	     gpointer     user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  screenator_redo (data);
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

    /* A popup above the drawings, without the usual focus stealing when it
     * maps, but with the same title bar and border as the other dialogs. */
    make_dialog_popup (dialog);
    dialog_install_titlebar (dialog);
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

static GdkPixbuf *load_menu_icon_at_size (const gchar *name, gint size)
{
  const gchar *dir = icon_data_dir ();
  gchar *path = dir ? g_build_filename (dir, "screenator", name, NULL)
                    : g_build_filename ("/usr/share/screenator", name, NULL);
  GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale (path, size, size,
                                                         FALSE, NULL);
  g_free (path);

  return pixbuf;
}

static GdkPixbuf *load_menu_icon (const gchar *name)
{
  return load_menu_icon_at_size (name, MENU_ICON_SIZE);
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
  paper_top_bar_update_images (data);

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

/* Drop a Custom colour dialog that is still waiting for the menu to close.
 * Applying another colour first means the chooser must not appear. */
static void
cancel_pending_custom_color (ScreenatorData *data)
{
  if (data->custom_color_idle_id)
    {
      g_source_remove (data->custom_color_idle_id);
      data->custom_color_idle_id = 0;
      data->custom_color_pending = FALSE;
    }
}

/* Apply a line colour exactly like the Line Color menu does. */
static void
apply_line_color (ScreenatorData *data, const gchar *name)
{
  GdkRGBA color;

  if (!name || !gdk_rgba_parse (&color, name))
    return;

  cancel_pending_custom_color (data);

  /* Remember the hue (opacity is a separate setting). */
  data->pen_color.red = color.red;
  data->pen_color.green = color.green;
  data->pen_color.blue = color.blue;
  data->pen_color.alpha = 1.0;

  apply_pen_settings (data);
  update_settings_indicator (data);
  update_active_frame_colors (data);
  invalidate_annotation_window (data);
  paper_top_bar_update_images (data);
  schedule_tray_menu_refresh (data);
}

/* Apply a shape fill colour exactly like the Shapes Fill Color menu does. */
static void
apply_shape_fill (ScreenatorData *data, const gchar *name)
{
  GdkRGBA color;

  if (!name)
    return;

  cancel_pending_custom_color (data);

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
  paper_top_bar_update_images (data);
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
  GtkWidget      *custom_item;
} CustomColorRequest;

/* Put the colour radio group back on the colour that is actually selected.
 * The custom entry is part of the group, so this also covers a cancelled
 * chooser and keeps a preset from looking selected for a custom colour. */
static void
restore_color_menu_selection (gboolean fill, GtkWidget *custom_item,
			      ScreenatorData *data)
{
  GSList *group, *l;
  GtkWidget *match = NULL;
  guint i;

  if (!custom_item || !gtk_widget_get_parent (custom_item))
    return;

  group = gtk_radio_menu_item_get_group (
    GTK_RADIO_MENU_ITEM (custom_item));

  if (fill)
    {
      if (!data->shape_fill_enabled)
	{
	  for (l = group; l; l = l->next)
	    {
	      const gchar *name = g_object_get_data (G_OBJECT (l->data),
						     "icon-shape-fill-name");

	      if (name && g_ascii_strcasecmp (name, "none") == 0)
		{
		  match = GTK_WIDGET (l->data);
		  break;
		}
	    }
	}
      else
	{
	  for (i = 0; i < G_N_ELEMENTS (fill_colors) && !match; i++)
	    {
	      GdkRGBA color;

	      if (g_ascii_strcasecmp (fill_colors[i].name, "none") == 0 ||
		  !gdk_rgba_parse (&color, fill_colors[i].name) ||
		  data->shape_fill_color.red != color.red ||
		  data->shape_fill_color.green != color.green ||
		  data->shape_fill_color.blue != color.blue)
		continue;

	      for (l = group; l; l = l->next)
		{
		  const gchar *name = g_object_get_data (G_OBJECT (l->data),
							 "icon-shape-fill-name");

		  if (g_strcmp0 (name, fill_colors[i].name) == 0)
		    {
		      match = GTK_WIDGET (l->data);
		      break;
		    }
		}
	    }
	}
    }
  else
    {
      for (i = 0; i < G_N_ELEMENTS (line_colors) && !match; i++)
	{
	  GdkRGBA color;

	  if (!gdk_rgba_parse (&color, line_colors[i].name) ||
	      data->pen_color.red != color.red ||
	      data->pen_color.green != color.green ||
	      data->pen_color.blue != color.blue)
	    continue;

	  for (l = group; l; l = l->next)
	    {
	      const gchar *name = g_object_get_data (G_OBJECT (l->data),
						     "icon-color-name");

	      if (g_strcmp0 (name, line_colors[i].name) == 0)
		{
		  match = GTK_WIDGET (l->data);
		  break;
		}
	    }
	}
    }

  if (!match)
    match = custom_item;

  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (match), TRUE);
}

static gboolean
clear_custom_color_pending (gpointer user_data)
{
  ((ScreenatorData *) user_data)->custom_color_pending = FALSE;
  return G_SOURCE_REMOVE;
}

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

  data->custom_color_idle_id = 0;

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
  schedule_tray_menu_refresh (data);

  restore_color_menu_selection (fill, request->custom_item, data);
  g_object_unref (request->custom_item);
  request->custom_item = NULL;

  /* Keep ignoring activations until the event that closed the dialog has been
   * fully processed, so a duplicate radio activation cannot reopen it. */
  g_idle_add (clear_custom_color_pending, data);

  return G_SOURCE_REMOVE;
}

static void
on_icon_custom_color (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  const gchar *target =
    g_object_get_data (G_OBJECT (menuitem), "icon-custom-target");
  CustomColorRequest *request;

  if (data->custom_color_pending)
    return;

  data->custom_color_pending = TRUE;
  request = g_new0 (CustomColorRequest, 1);

  request->data = data;
  request->fill = g_strcmp0 (target, "fill") == 0;
  request->custom_item = g_object_ref (GTK_WIDGET (menuitem));
  data->custom_color_idle_id =
    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, custom_color_idle, request,
		     g_free);
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

/* TRUE while a placed shape, text or spline still shows its editing handles.
 * In that state the wheel and stylus button 1 recolour the object instead of
 * scrolling the paper. */
static gboolean
editable_handles_active (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;

  if (!data->devdatatable)
    return FALSE;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      ScreenatorDeviceData *devdata = value;

      if (devdata->shape_active || devdata->star_active ||
	  devdata->triangle_active || devdata->frame_active ||
	  devdata->line_active)
	return TRUE;
    }

  return FALSE;
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
  paper_top_bar_update_images (data);
  schedule_tray_menu_refresh (data);
}

/* One step of the stylus first-side-button action: eraser size for the
 * Eraser, colours for the Marker, colours in Color mode and line thickness in
 * Thickness mode. */
static void
apply_stylus_step (ScreenatorData *data, gboolean down)
{
  if (editable_handles_active (data))
    {
      apply_scroll_step (data, down, TRUE);
      return;
    }

  if (eraser_tool_active (data))
    cycle_eraser_size (data, down);
  else if (marker_tool_active (data))
    apply_scroll_step (data, down, TRUE);
  else if (data->stylus_mode == SCREENATOR_STYLUS_THICKNESS)
    cycle_line_thickness (data, down);
  else
    apply_scroll_step (data, down, TRUE);
}

/* Outside Paper Drawing the mouse wheel keeps its setting action: colour,
 * thickness, or eraser size. */
static void
paper_cycle_setting (ScreenatorData *data, gboolean down)
{
  if (eraser_tool_active (data))
    cycle_eraser_size (data, down);
  else if (marker_tool_active (data))
    apply_scroll_step (data, down, TRUE);
  else if (data->wheel_mode == SCREENATOR_WHEEL_THICKNESS)
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

  if (data->top_dialog || data->native_dialog_open)
    return TRUE;

  if (data->quick_menu)
    return TRUE;

  /* In Paper Drawing the wheel scrolls the pages.  Outside Paper Drawing it
   * keeps the old tool-only behaviour.  While text is being typed, the wheel
   * changes the text colour instead. */
  if (data->paper == SCREENATOR_PAPER_NONE && !data->text_editor_active &&
      !screenator_any_grabbed (data))
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

  if (data->text_editor_active)
    apply_scroll_step (data, down, TRUE);
  else if (data->paper != SCREENATOR_PAPER_NONE)
    {
      if (editable_handles_active (data))
	apply_scroll_step (data, down, TRUE);
      else
	screenator_paper_scroll (data, down ? 1 : -1);
    }
  else
    paper_cycle_setting (data, down);

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

/* ------------------------------------------------------------------
 * Paper Drawing
 *
 * Plain, Ruled, Graph and PDF are separate documents.  Selecting a type
 * switches to Paper Drawing, and switching away discards the old drawings
 * after the save prompt.  A PDF document is rendered with
 * poppler and can be scrolled, panned and annotated page by page.
 * ------------------------------------------------------------------ */

static void paper_switch_to (ScreenatorData *data, ScreenatorPaperType type);
static void paper_ensure_background (ScreenatorData *data,
				     ScreenatorPaperDocument *doc,
				     guint index, ScreenatorPaperPage *page);
static void draw_paper_page (ScreenatorData *data, cairo_t *cr);

static ScreenatorPaperDocument *
paper_document (ScreenatorData *data, ScreenatorPaperType type)
{
  if (type <= SCREENATOR_PAPER_NONE || type > SCREENATOR_PAPER_PDF)
    return NULL;

  return data->paper_docs[type];
}

static ScreenatorPaperDocument *
paper_ensure_document (ScreenatorData *data, ScreenatorPaperType type)
{
  ScreenatorPaperDocument *doc;

  if (type <= SCREENATOR_PAPER_NONE || type > SCREENATOR_PAPER_PDF)
    return NULL;

  doc = data->paper_docs[type];
  if (!doc)
    {
      doc = g_new0 (ScreenatorPaperDocument, 1);
      doc->type = type;
      doc->pages = g_array_new (FALSE, FALSE, sizeof (ScreenatorPaperPage));
      doc->pdf_page_tops = g_array_new (FALSE, FALSE, sizeof (gint));
      doc->pdf_page_heights = g_array_new (FALSE, FALSE, sizeof (gint));
      doc->page = 0;
      data->paper_docs[type] = doc;
    }

  return doc;
}

static ScreenatorPaperDocument *
paper_active_document (ScreenatorData *data)
{
  if (data->paper == SCREENATOR_PAPER_NONE)
    return NULL;

  return data->paper_docs[data->paper];
}

/* TRUE when the active paper is shown as one continuous vertical document.
 * All paper types use the same page-top/height layout; only PDF pages have a
 * rendered background. */
static gboolean
paper_continuous (ScreenatorData *data)
{
  ScreenatorPaperDocument *doc = paper_active_document (data);

  if (!doc || data->paper == SCREENATOR_PAPER_NONE ||
      !data->paper_continuous_scroll || doc->pdf_stack_height <= 0)
    return FALSE;

  if (doc->type == SCREENATOR_PAPER_PDF)
    return doc->pdf_loaded;

  return doc->pages && doc->pages->len > 0;
}

static ScreenatorPaperPage *
paper_current_page (ScreenatorPaperDocument *doc)
{
  if (!doc || !doc->pages || doc->pages->len == 0)
    return NULL;

  if (doc->page >= doc->pages->len)
    doc->page = doc->pages->len - 1;

  return &g_array_index (doc->pages, ScreenatorPaperPage, doc->page);
}

/* Convert the paper size setting to canvas pixels and PDF points. */
void
paper_size_dimensions (ScreenatorData *data, gint *canvas_width,
		       gint *canvas_height, gdouble *pdf_width,
		       gdouble *pdf_height)
{
  gdouble mm_width = 0, mm_height = 0;

  if (data->paper_size == SCREENATOR_PAPER_SIZE_A4_PORTRAIT)
    {
      mm_width = 210.0;
      mm_height = 297.0;
    }
  else if (data->paper_size == SCREENATOR_PAPER_SIZE_A4_LANDSCAPE)
    {
      mm_width = 297.0;
      mm_height = 210.0;
    }
  else if (data->paper_size == SCREENATOR_PAPER_SIZE_CUSTOM)
    {
      mm_width = CLAMP (data->paper_custom_width_mm, 10.0, 1000.0);
      mm_height = CLAMP (data->paper_custom_height_mm, 10.0, 1000.0);
    }
  else
    {
      /* Screen: use the screen pixels and keep the old PDF behaviour where
       * one screen pixel is one PDF point. */
      *canvas_width = MAX (1, (gint) data->width);
      *canvas_height = MAX (1, (gint) data->height);
      *pdf_width = *canvas_width;
      *pdf_height = *canvas_height;
      return;
    }

  *canvas_width = MAX (1, (gint) round (mm_width * 96.0 / 25.4));
  *canvas_height = MAX (1, (gint) round (mm_height * 96.0 / 25.4));
  *pdf_width = mm_width * 72.0 / 25.4;
  *pdf_height = mm_height * 72.0 / 25.4;
}

static ScreenatorPaperPage
paper_new_page (ScreenatorData *data, ScreenatorPaperType type)
{
  ScreenatorPaperPage page;

  memset (&page, 0, sizeof (page));
  page.style = type;
  page.vector_valid = TRUE;

  if (type != SCREENATOR_PAPER_PDF)
    paper_size_dimensions (data, &page.canvas_width, &page.canvas_height,
			   &page.pdf_width, &page.pdf_height);

  return page;
}

static void
paper_page_clear (ScreenatorPaperPage *page)
{
  if (!page)
    return;

  if (page->annotations)
    cairo_surface_destroy (page->annotations);
  if (page->background)
    cairo_surface_destroy (page->background);
  if (page->vector)
    cairo_surface_destroy (page->vector);

  memset (page, 0, sizeof (*page));
}

/* Free the pages held by an undo or redo stack. */
static void
paper_page_action_stack_clear (GArray *stack)
{
  guint i;

  if (!stack)
    return;

  for (i = 0; i < stack->len; i++)
    paper_page_clear (
      &g_array_index (stack, ScreenatorPageAction, i).page);

  g_array_set_size (stack, 0);
}

/* Page history belongs to one paper document.  It is discarded whenever the
 * active document is replaced, reset or resized. */
void
screenator_page_history_clear (ScreenatorData *data)
{
  if (!data)
    return;

  paper_page_action_stack_clear (data->page_undo_stack);
  paper_page_action_stack_clear (data->page_redo_stack);
}

/* A new drawing action branches the timeline, so a pending page redo can no
 * longer be replayed. */
void
screenator_page_redo_clear (ScreenatorData *data)
{
  if (data)
    paper_page_action_stack_clear (data->page_redo_stack);
}

static void
paper_document_clear_pages (ScreenatorPaperDocument *doc)
{
  guint i;

  if (!doc || !doc->pages)
    return;

  for (i = 0; i < doc->pages->len; i++)
    paper_page_clear (&g_array_index (doc->pages, ScreenatorPaperPage, i));

  g_array_set_size (doc->pages, 0);
  if (doc->pdf_page_tops)
    g_array_set_size (doc->pdf_page_tops, 0);
  if (doc->pdf_page_heights)
    g_array_set_size (doc->pdf_page_heights, 0);
  doc->pdf_stack_width = 0;
  doc->pdf_stack_height = 0;
  doc->pdf_scroll_y = 0;
  doc->page = 0;
  doc->modified = FALSE;
}

/* Commit an editable shape or a stroke that is still in progress before the
 * active drawing layer is swapped, so it is not painted onto the next page. */
static void
paper_commit_pending (ScreenatorData *data)
{
  text_editor_finish (data);
  stroke_finish_all (data);
  star_finish_all (data);
  triangle_finish_all (data);
  frame_finish_all (data);
  line_finish_all (data);
  spline_cancel_all (data);
}

static void
paper_ensure_annotations (ScreenatorData *data, ScreenatorPaperDocument *doc,
			  guint index, ScreenatorPaperPage *page)
{
  gint width, height;

  if (!page || page->annotations)
    return;

  if (page->style == SCREENATOR_PAPER_PDF)
    {
      paper_ensure_background (data, doc, index, page);

      if (page->background)
	{
	  width = page->background_width;
	  height = page->background_height;
	  page->canvas_width = width;
	  page->canvas_height = height;
	}
      else
	{
	  /* The rendered background is still loading.  Use the page size
	   * computed by the layout, not the screen size. */
	  width = page->canvas_width > 0 ? page->canvas_width
	    : (gint) data->width;
	  height = page->canvas_height > 0 ? page->canvas_height
	    : (gint) data->height;
	}
    }
  else
    {
      if (!page->canvas_width || !page->canvas_height)
	paper_size_dimensions (data, &page->canvas_width,
			       &page->canvas_height,
			       &page->pdf_width, &page->pdf_height);
      width = page->canvas_width;
      height = page->canvas_height;
    }

  page->annotations = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
						  width, height);
}

static void
paper_store_continuous_page (ScreenatorData *data, ScreenatorPaperDocument *doc,
			     guint index, ScreenatorPaperPage *page,
			     const GdkRectangle *area)
{
  gint top, height, page_y, origin_x, sx0, sx1, sy0, sy1;
  cairo_operator_t op;
  cairo_t *cr;

  if (!page->canvas_width || !page->canvas_height || !page->painted)
    return;

  top = g_array_index (doc->pdf_page_tops, gint, index);
  height = g_array_index (doc->pdf_page_heights, gint, index);
  page_y = top - doc->pdf_scroll_y;
  origin_x = page->style == SCREENATOR_PAPER_PDF
    ? 0 : MAX (0, ((gint) data->width - page->canvas_width) / 2);

  sx0 = MAX (0, origin_x);
  sx1 = MIN ((gint) data->width, origin_x + page->canvas_width);
  sy0 = MAX (0, page_y);
  sy1 = MIN (paper_view_height (data), page_y + height);

  if (area)
    {
      sx0 = MAX (sx0, area->x);
      sy0 = MAX (sy0, area->y);
      sx1 = MIN (sx1, area->x + area->width);
      sy1 = MIN (sy1, area->y + area->height);
    }

  if (sx1 <= sx0 || sy1 <= sy0)
    return;

  paper_ensure_annotations (data, doc, index, page);

  op = CAIRO_OPERATOR_SOURCE;

  cr = cairo_create (page->annotations);
  cairo_rectangle (cr, sx0 - origin_x, sy0 - page_y,
		   sx1 - sx0, sy1 - sy0);
  cairo_clip (cr);
  cairo_set_operator (cr, op);
  cairo_set_source_surface (cr, data->backbuffer, -origin_x, -page_y);
  cairo_paint (cr);
  cairo_destroy (cr);

  page->painted = TRUE;
}

static void
paper_store_continuous_pages (ScreenatorData *data,
			      ScreenatorPaperDocument *doc)
{
  gint first, last, i;

  if (!doc || !doc->pages || !doc->pdf_page_tops ||
      !doc->pdf_page_heights)
    return;

  /* Store the whole visible region of every visible page.  The drawing
   * backbuffer is a composite of all visible pages, so copying only one
   * recorded stroke rectangle can otherwise leave holes where another
   * element (text, shape, bucket fill) was drawn in the same session. */
  first = paper_pdf_page_at (doc, doc->pdf_scroll_y);
  last = paper_pdf_page_at (doc, doc->pdf_scroll_y +
			    MAX (0, paper_view_height (data) - 1));

  if (first > last)
    {
      i = first;
      first = last;
      last = i;
    }

  for (i = first; i <= last && i < (gint) doc->pages->len; i++)
    {
      ScreenatorPaperPage *page =
	&g_array_index (doc->pages, ScreenatorPaperPage, i);

      paper_store_continuous_page (data, doc, (guint) i, page, NULL);
    }

  data->paper_pending_ink_valid = FALSE;
}

static void
paper_store_active_page (ScreenatorData *data)
{
  ScreenatorPaperDocument *doc = paper_active_document (data);
  ScreenatorPaperPage *page = paper_current_page (doc);
  gboolean profile = screenator_profile_enabled ();
  gint64 profile_start = profile ? g_get_monotonic_time () : 0;

  if (!page)
    return;

  /* Nothing has been drawn since the last store.  Scrolling or switching
   * pages must not copy the whole active layer in that case. */
  if (!data->paper_page_dirty && !data->paper_pending_ink_valid)
    return;

  if (paper_continuous (data))
    {
      paper_store_continuous_pages (data, doc);
      data->paper_page_dirty = FALSE;
      return;
    }

  paper_ensure_annotations (data, doc, doc->page, page);

  /* Replace the visible part of the full-page annotation layer.  Source over
   * the clipped rectangle keeps drawings outside the viewport. */
  {
    gint origin_x, origin_y;
    cairo_t *cr;

    paper_page_origin (data, page, &origin_x, &origin_y);
    cr = cairo_create (page->annotations);
    cairo_rectangle (cr, page->view_x - origin_x, page->view_y - origin_y,
		     data->width, data->height);
    cairo_clip (cr);
    cairo_set_source_surface (cr, data->backbuffer,
			      page->view_x - origin_x,
			      page->view_y - origin_y);
    cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint (cr);
    cairo_destroy (cr);
  }

  page->painted = data->painted;
  data->paper_page_dirty = FALSE;

  if (profile)
    g_printerr ("PROFILE store %.2f ms continuous=%d dirty=%d\n",
		(g_get_monotonic_time () - profile_start) / 1000.0,
		paper_continuous (data) ? 1 : 0,
		data->paper_page_dirty ? 1 : 0);
}

#ifdef HAVE_POPPLER_GLIB
/* PDF pages are rendered on a worker thread, like Xournal++'s render jobs, so
 * scrolling into a page that has not been rendered yet does not freeze the
 * window.  The page shows a loading placeholder until the job finishes. */
typedef struct
{
  ScreenatorData *data;
  PopplerDocument *document;
  guint index;
  guint serial;
  gint screen_width;
  gint pixel_width;
  gint pixel_height;
  cairo_surface_t *surface;
} PaperPdfRenderJob;

static GThreadPool *paper_pdf_render_pool;
static GMutex paper_pdf_render_mutex;
static gsize paper_pdf_pool_ready;

static gboolean
paper_pdf_page_visible (ScreenatorData *data, ScreenatorPaperDocument *doc,
			guint index)
{
  gint top, height, y;

  if (!data || !doc || data->paper != SCREENATOR_PAPER_PDF ||
      index >= doc->pages->len)
    return FALSE;

  if (!paper_continuous (data))
    return (gint) doc->page == (gint) index;

  if (!doc->pdf_page_tops || !doc->pdf_page_heights ||
      index >= doc->pdf_page_tops->len)
    return FALSE;

  top = g_array_index (doc->pdf_page_tops, gint, index);
  height = g_array_index (doc->pdf_page_heights, gint, index);
  y = top - doc->pdf_scroll_y;

  return y + height > 0 && y < paper_view_height (data);
}

static gboolean
paper_pdf_render_apply (gpointer user_data)
{
  PaperPdfRenderJob *job = user_data;
  ScreenatorData *data = job->data;
  ScreenatorPaperDocument *doc = NULL;
  ScreenatorPaperPage *page;

  if (data && data->paper == SCREENATOR_PAPER_PDF)
    doc = data->paper_docs[SCREENATOR_PAPER_PDF];

  if (doc && doc->pdf_document == job->document &&
      job->index < doc->pages->len)
    {
      page = &g_array_index (doc->pages, ScreenatorPaperPage, job->index);

      if (data->paper_saving)
	{
	  /* The exporter renders pages synchronously.  Do not let a background
	   * result modify/evict page data while the PDF is being written. */
	  if (page->pdf_render_serial == job->serial)
	    page->pdf_render_pending = FALSE;
	  if (job->surface)
	    {
	      cairo_surface_destroy (job->surface);
	      job->surface = NULL;
	    }
	  g_object_unref (job->document);
	  g_free (job);
	  return G_SOURCE_REMOVE;
	}

      if (page->pdf_render_serial == job->serial)
	{
	  page->pdf_render_pending = FALSE;

	  if (!page->background &&
	      data->pdf_render_serial != (gint) job->serial &&
	      paper_pdf_page_visible (data, doc, job->index))
	    {
	      /* Serial changed while the page was visible.  Trigger a repaint so
	       * the page schedules a fresh render. */
	      paper_background_invalidate (data);
	      paper_repaint (data);
	    }
	}

      if (job->surface && !page->background &&
	  page->pdf_render_serial == job->serial &&
	  data->pdf_render_serial == (gint) job->serial)
	{
	  gint old_width = page->canvas_width;
	  gint old_height = page->canvas_height;

	  /* Annotations may have been recorded before the first background
	   * render established the final page size.  Keep their vector form
	   * and scale it to the new canvas instead of leaving it behind. */
	  if (old_width > 0 && old_height > 0 &&
	      (old_width != job->pixel_width ||
	       old_height != job->pixel_height))
	    {
	      paper_scale_vector_surface (page, job->pixel_width,
					  job->pixel_height, FALSE);
	      if (page->annotations)
		{
		  cairo_surface_t *scaled =
		    resize_surface_scale (page->annotations,
					  job->pixel_width,
					  job->pixel_height);

		  cairo_surface_destroy (page->annotations);
		  page->annotations = scaled;
		}

	      page->view_x = (gint) round ((gdouble) page->view_x *
					   job->pixel_width / old_width);
	      page->view_y = (gint) round ((gdouble) page->view_y *
					   job->pixel_height / old_height);
	    }

	  page->background = job->surface;
	  job->surface = NULL;
	  page->background_width = job->pixel_width;
	  page->background_height = job->pixel_height;
	  page->canvas_width = job->pixel_width;
	  page->canvas_height = job->pixel_height;
	  page->pdf_width = job->pixel_width;
	  page->pdf_height = job->pixel_height;
	  page->background_stamp = ++paper_page_background_clock;
	  paper_trim_page_backgrounds (doc);

	  if (paper_pdf_page_visible (data, doc, job->index))
	    {
	      paper_background_invalidate (data);
	      paper_repaint (data);
	    }
	}
    }

  if (job->surface)
    cairo_surface_destroy (job->surface);
  g_object_unref (job->document);
  g_free (job);
  return G_SOURCE_REMOVE;
}

static void
paper_pdf_render_worker (gpointer user_data, gpointer pool_data)
{
  PaperPdfRenderJob *job = user_data;
  PopplerPage *pdf_page = NULL;
  cairo_t *cr;
  gdouble width = 0, height = 0, scale;
  cairo_surface_t *surface = NULL;

  (void) pool_data;

  /* A newer view or document has superseded this request.  Do not spend time
   * rendering a page that will only be discarded. */
  if ((guint) g_atomic_int_get (&job->data->pdf_render_serial) != job->serial)
    {
      job->surface = NULL;
      g_idle_add (paper_pdf_render_apply, job);
      return;
    }

  g_mutex_lock (&paper_pdf_render_mutex);
  pdf_page = poppler_document_get_page (job->document, job->index);
  if (pdf_page)
    {
      poppler_page_get_size (pdf_page, &width, &height);
      scale = width > 0 ? (gdouble) job->screen_width / width : 1.0;
      if (scale <= 0)
	scale = 1.0;
      scale = MIN (scale, 4.0);

      job->pixel_width = MAX (1, (gint) ceil (width * scale));
      job->pixel_height = MAX (1, (gint) ceil (height * scale));

      surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
					    job->pixel_width, job->pixel_height);
      if (cairo_surface_status (surface) == CAIRO_STATUS_SUCCESS)
	{
	  cr = cairo_create (surface);
	  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
	  cairo_paint (cr);
	  cairo_scale (cr, scale, scale);
	  poppler_page_render (pdf_page, cr);
	  cairo_destroy (cr);
	}
      else
	{
	  cairo_surface_destroy (surface);
	  surface = NULL;
	}

      g_object_unref (pdf_page);
    }
  g_mutex_unlock (&paper_pdf_render_mutex);

  job->surface = surface;
  g_idle_add (paper_pdf_render_apply, job);
}

static void
paper_pdf_render_init (void)
{
  if (g_once_init_enter (&paper_pdf_pool_ready))
    {
      g_mutex_init (&paper_pdf_render_mutex);
      paper_pdf_render_pool = g_thread_pool_new (paper_pdf_render_worker,
						 NULL, 1, FALSE, NULL);
      g_once_init_leave (&paper_pdf_pool_ready, 1);
    }
}

static void
paper_render_pdf_page (ScreenatorData *data, ScreenatorPaperDocument *doc,
		       guint index, ScreenatorPaperPage *page)
{
  PaperPdfRenderJob *job;

  if (!doc || !doc->pdf_document || !page || page->background ||
      page->pdf_render_pending || data->paper_saving)
    return;

  if (index >= (guint) doc->pdf_page_count)
    return;

  paper_pdf_render_init ();

  if (!paper_pdf_render_pool)
    return;

  job = g_new0 (PaperPdfRenderJob, 1);
  job->data = data;
  job->document = g_object_ref ((PopplerDocument *) doc->pdf_document);
  job->index = index;
  job->serial = (guint) g_atomic_int_get (&data->pdf_render_serial);
  job->screen_width = (gint) data->width;

  page->pdf_render_pending = TRUE;
  page->pdf_render_serial = job->serial;

  if (!g_thread_pool_push (paper_pdf_render_pool, job, NULL))
    {
      page->pdf_render_pending = FALSE;
      g_object_unref (job->document);
      g_free (job);
    }
}
#else
static void
paper_render_pdf_page (ScreenatorData *data, ScreenatorPaperDocument *doc,
		       guint index, ScreenatorPaperPage *page)
{
}
#endif

static void
paper_trim_page_backgrounds (ScreenatorPaperDocument *doc)
{
  guint i, count = 0;
  ScreenatorPaperPage *victim;
  guint32 oldest;

  if (!doc || doc->type != SCREENATOR_PAPER_PDF || !doc->pages)
    return;

  for (i = 0; i < doc->pages->len; i++)
    {
      ScreenatorPaperPage *page =
	&g_array_index (doc->pages, ScreenatorPaperPage, i);

      if (page->background)
	count++;
    }

  while (count > PAPER_PDF_BACKGROUND_CACHE_LIMIT)
    {
      victim = NULL;
      oldest = G_MAXUINT32;

      for (i = 0; i < doc->pages->len; i++)
	{
	  ScreenatorPaperPage *page =
	    &g_array_index (doc->pages, ScreenatorPaperPage, i);

	  if (page->background && page->background_stamp <= oldest)
	    {
	      oldest = page->background_stamp;
	      victim = page;
	    }
	}

      if (!victim)
	break;

      cairo_surface_destroy (victim->background);
      victim->background = NULL;
      victim->background_width = 0;
      victim->background_height = 0;
      count--;
    }
}

static void
paper_ensure_background (ScreenatorData *data, ScreenatorPaperDocument *doc,
			 guint index, ScreenatorPaperPage *page)
{
  if (!page || page->style != SCREENATOR_PAPER_PDF)
    return;

  if (page->background)
    {
      page->background_stamp = ++paper_page_background_clock;
      return;
    }

  paper_render_pdf_page (data, doc, index, page);

  if (page->background)
    {
      page->background_stamp = ++paper_page_background_clock;
      paper_trim_page_backgrounds (doc);
    }
}

/* Space between two PDF pages in the continuous view.  The grey paper
 * surround shows through it, so the page edges stay visible. */
#define PAPER_PDF_PAGE_GAP 16

/* Build the vertical layout of a PDF: every page is rendered at the screen
 * width, and the pages are stacked with a grey gap between them for
 * continuous scrolling. */
static void
paper_pdf_build_layout (ScreenatorData *data, ScreenatorPaperDocument *doc)
{
#ifdef HAVE_POPPLER_GLIB
  gint i, total = 0, stack_width = 0;

  paper_pdf_render_init ();
  g_atomic_int_inc (&data->pdf_render_serial);

  if (!doc || !doc->pdf_document || !doc->pdf_page_tops ||
      !doc->pdf_page_heights)
    return;

  g_array_set_size (doc->pdf_page_tops, 0);
  g_array_set_size (doc->pdf_page_heights, 0);

  for (i = 0; i < doc->pdf_page_count; i++)
    {
      PopplerPage *pdf_page;
      gdouble width = 0, height = 0, scale;
      gint pixel_width, pixel_height;

      g_mutex_lock (&paper_pdf_render_mutex);
      pdf_page = poppler_document_get_page ((PopplerDocument *) doc->pdf_document,
					    i);
      if (pdf_page)
	poppler_page_get_size (pdf_page, &width, &height);
      g_mutex_unlock (&paper_pdf_render_mutex);

      if (!pdf_page)
	break;

      scale = width > 0 ? (gdouble) data->width / width : 1.0;
      if (scale <= 0)
	scale = 1.0;
      scale = MIN (scale, 4.0);

      pixel_width = MAX (1, (gint) ceil (width * scale));
      pixel_height = MAX (1, (gint) ceil (height * scale));

      if (doc->pages && i < (gint) doc->pages->len)
	{
	  ScreenatorPaperPage *page =
	    &g_array_index (doc->pages, ScreenatorPaperPage, i);
	  gint old_width = page->canvas_width;
	  gint old_height = page->canvas_height;

	  if (old_width > 0 && old_height > 0 &&
	      (old_width != pixel_width || old_height != pixel_height))
	    {
	      paper_scale_vector_surface (page, pixel_width, pixel_height,
					  FALSE);
	      if (page->annotations)
		{
		  cairo_surface_t *scaled =
		    resize_surface_scale (page->annotations, pixel_width,
					  pixel_height);

		  cairo_surface_destroy (page->annotations);
		  page->annotations = scaled;
		}

	      page->view_x = (gint) round ((gdouble) page->view_x *
					   pixel_width / old_width);
	      page->view_y = (gint) round ((gdouble) page->view_y *
					   pixel_height / old_height);
	    }

	  page->canvas_width = pixel_width;
	  page->canvas_height = pixel_height;
	  page->pdf_width = pixel_width;
	  page->pdf_height = pixel_height;

	  /* Re-render the PDF page at the new zoom. */
	  if (page->background &&
	      (page->background_width != pixel_width ||
	       page->background_height != pixel_height))
	    {
	      cairo_surface_destroy (page->background);
	      page->background = NULL;
	      page->background_width = 0;
	      page->background_height = 0;
	    }
	}

      if (i > 0)
	total += PAPER_PDF_PAGE_GAP;

      g_array_append_val (doc->pdf_page_tops, total);
      g_array_append_val (doc->pdf_page_heights, pixel_height);
      total += pixel_height;
      stack_width = MAX (stack_width, pixel_width);
      g_object_unref (pdf_page);
    }

  doc->pdf_stack_width = stack_width;
  doc->pdf_stack_height = total;
#else
  (void) data;
  (void) doc;
#endif
}

/* Build the same vertical stack for Plain, Ruled and Graph pages.  PDF pages
 * use their rendered heights, other papers use their configured page size. */
static void
paper_build_continuous_layout (ScreenatorData *data,
			       ScreenatorPaperDocument *doc)
{
  guint i;
  gint total = 0, stack_width = 0;

  if (!doc)
    return;

  if (doc->type == SCREENATOR_PAPER_PDF)
    {
      paper_pdf_build_layout (data, doc);
      return;
    }

  if (!doc->pages || !doc->pdf_page_tops || !doc->pdf_page_heights)
    return;

  g_array_set_size (doc->pdf_page_tops, 0);
  g_array_set_size (doc->pdf_page_heights, 0);

  for (i = 0; i < doc->pages->len; i++)
    {
      ScreenatorPaperPage *page =
	&g_array_index (doc->pages, ScreenatorPaperPage, i);

      if (!page->canvas_width || !page->canvas_height)
	paper_size_dimensions (data, &page->canvas_width,
			       &page->canvas_height,
			       &page->pdf_width, &page->pdf_height);

      if (i > 0)
	total += PAPER_PDF_PAGE_GAP;

      g_array_append_val (doc->pdf_page_tops, total);
      g_array_append_val (doc->pdf_page_heights, page->canvas_height);
      total += page->canvas_height;
      stack_width = MAX (stack_width, page->canvas_width);
    }

  doc->pdf_stack_width = stack_width;
  doc->pdf_stack_height = total;
}

static void
paper_load_page (ScreenatorData *data, ScreenatorPaperPage *page)
{
  cairo_t *cr;

  if (!page)
    return;

  /* Continuous PDF pages use a virtual, sometimes negative, view offset.
   * Every other view is clamped to the page. */
  if (!paper_continuous (data))
    {
      gint max_x = MAX (0, page->canvas_width - (gint) data->width);
      gint max_y = MAX (0, page->canvas_height - (gint) data->height);

      page->view_x = CLAMP (page->view_x, 0, max_x);
      page->view_y = CLAMP (page->view_y, 0, max_y);
    }

  cr = cairo_create (data->backbuffer);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba (cr, 0, 0, 0, 0);
  cairo_paint (cr);

  if (page->annotations)
    {
      gint origin_x, origin_y;

      paper_page_origin (data, page, &origin_x, &origin_y);
      cairo_set_source_surface (cr, page->annotations,
				origin_x - page->view_x,
				origin_y - page->view_y);
      cairo_paint (cr);
    }

  cairo_destroy (cr);

  data->painted = page->painted ? 1 : 0;
  data->paper_page_dirty = FALSE;
}

static void
paper_repaint_window_only (ScreenatorData *data)
{
  update_window_shape (data);

  if (data->win && gtk_widget_get_window (data->win))
    {
      GdkRectangle rect = { 0, 0, data->width,
			    paper_view_height (data) };

      screenator_invalidate_rect (data, &rect);
    }
}

static void
paper_repaint (ScreenatorData *data)
{
  /* Page, view, paper style or layout changed.  Window resizing must not
   * invalidate the background cache: it is in screen coordinates. */
  paper_background_invalidate (data);
  paper_repaint_window_only (data);
}

static gint
paper_pdf_page_at (ScreenatorPaperDocument *doc, gint content_y)
{
  guint lo, hi;

  if (!doc || !doc->pdf_page_tops || !doc->pdf_page_heights ||
      doc->pdf_page_tops->len == 0)
    return 0;

  /* Page tops are sorted, so a long PDF does not need a linear scan for
   * every scroll or stroke. */
  lo = 0;
  hi = doc->pdf_page_tops->len;
  while (lo < hi)
    {
      guint mid = lo + (hi - lo) / 2;
      gint top = g_array_index (doc->pdf_page_tops, gint, mid);
      gint height = g_array_index (doc->pdf_page_heights, gint, mid);

      if (content_y < top + height)
	hi = mid;
      else
	lo = mid + 1;
    }

  if (lo >= doc->pdf_page_tops->len)
    lo = doc->pdf_page_tops->len - 1;

  return (gint) lo;
}

/* Mark the page or pages covered by a committed drawing rectangle as having
 * annotations.  The continuous view uses this to avoid allocating a full page
 * annotation surface for every blank page that is merely scrolled past. */
void
screenator_paper_mark_ink (ScreenatorData *data, const GdkRectangle *bounds)
{
  ScreenatorPaperDocument *doc;
  ScreenatorPaperPage *page;
  gint first, last, i;

  if (!data || data->paper == SCREENATOR_PAPER_NONE)
    return;

  data->paper_page_dirty = TRUE;

  doc = paper_active_document (data);
  if (!doc || !doc->pages || doc->pages->len == 0)
    return;

  if (!paper_continuous (data) || !bounds ||
      !doc->pdf_page_tops || !doc->pdf_page_heights)
    {
      page = paper_current_page (doc);
      if (page)
	page->painted = TRUE;
      return;
    }

  if (data->paper_pending_ink_valid)
    gdk_rectangle_union (&data->paper_pending_ink_bounds, bounds,
			 &data->paper_pending_ink_bounds);
  else
    {
      data->paper_pending_ink_bounds = *bounds;
      data->paper_pending_ink_valid = TRUE;
    }

  first = paper_pdf_page_at (doc, doc->pdf_scroll_y + bounds->y);
  last = paper_pdf_page_at (doc, doc->pdf_scroll_y + bounds->y +
			    MAX (0, bounds->height - 1));

  if (first > last)
    {
      i = first;
      first = last;
      last = i;
    }

  for (i = first; i <= last && i < (gint) doc->pages->len; i++)
    {
      page = &g_array_index (doc->pages, ScreenatorPaperPage, i);
      page->painted = TRUE;
    }
}

/* Build the screen backbuffer from every visible page.  This is the same
 * model as the original continuous view: drawing happens in one screen-sized
 * layer, and storing it back splits the visible content into per-page layers. */
static void
paper_load_continuous_view (ScreenatorData *data,
			    ScreenatorPaperDocument *doc)
{
  cairo_t *cr;
  guint i;
  gint first = 0, last = -1;

  if (!doc || !doc->pages || !doc->pdf_page_tops)
    return;

  cr = cairo_create (data->backbuffer);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba (cr, 0, 0, 0, 0);
  cairo_paint (cr);

  if (doc->pdf_page_tops->len > 0)
    {
      first = paper_pdf_page_at (doc, doc->pdf_scroll_y);
      last = paper_pdf_page_at (doc, doc->pdf_scroll_y +
				MAX (0, paper_view_height (data) - 1));
    }

  data->painted = 0;

  for (i = (guint) first;
       i <= (guint) last && i < doc->pages->len; i++)
    {
      ScreenatorPaperPage *page =
	&g_array_index (doc->pages, ScreenatorPaperPage, i);
      gint top, page_x, page_y;

      if (!page->annotations)
	continue;

      top = g_array_index (doc->pdf_page_tops, gint, i);
      page_y = top - doc->pdf_scroll_y;
      page_x = page->style == SCREENATOR_PAPER_PDF
	? 0 : MAX (0, ((gint) data->width - page->canvas_width) / 2);

      if (page_y + page->canvas_height <= 0 ||
	  page_y >= paper_view_height (data))
	continue;

      cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
      cairo_set_source_surface (cr, page->annotations, page_x, page_y);
      cairo_paint (cr);

      if (page->painted)
	data->painted = 1;
    }

  cairo_destroy (cr);
  data->paper_page_dirty = FALSE;
}

/* Shift the screen-sized drawing backbuffer vertically.  Scrolling then only
 * needs to redraw the newly exposed strip instead of clearing and rebuilding
 * the whole composite. */
static gboolean
paper_shift_backbuffer (ScreenatorData *data, gint rows)
{
  cairo_surface_t *surface = data->backbuffer;
  gint height, stride, move;
  guchar *pixels;

  if (!surface || cairo_surface_get_type (surface) != CAIRO_SURFACE_TYPE_IMAGE)
    return FALSE;

  height = cairo_image_surface_get_height (surface);
  if (rows == 0)
    return TRUE;
  if (ABS (rows) >= height)
    return FALSE;

  cairo_surface_flush (surface);
  stride = cairo_image_surface_get_stride (surface);
  pixels = cairo_image_surface_get_data (surface);
  if (!pixels)
    return FALSE;

  move = height - ABS (rows);
  if (rows > 0)
    memmove (pixels, pixels + rows * stride, (gsize) move * stride);
  else
    memmove (pixels + (-rows) * stride, pixels, (gsize) move * stride);

  cairo_surface_mark_dirty (surface);
  return TRUE;
}

/* Clear one horizontal strip of the backbuffer and composite the visible page
 * annotations into it. */
static void
paper_fill_continuous_strip (ScreenatorData *data, ScreenatorPaperDocument *doc,
			     gint y, gint height)
{
  cairo_t *cr;
  gint first, last, i;

  if (!doc || height <= 0 || y >= paper_view_height (data))
    return;

  if (y < 0)
    {
      height += y;
      y = 0;
    }
  if (y + height > paper_view_height (data))
    height = paper_view_height (data) - y;
  if (height <= 0 || !doc->pdf_page_tops || doc->pdf_page_tops->len == 0)
    return;

  cr = cairo_create (data->backbuffer);
  cairo_rectangle (cr, 0, y, data->width, height);
  cairo_clip (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba (cr, 0, 0, 0, 0);
  cairo_paint (cr);

  first = paper_pdf_page_at (doc, doc->pdf_scroll_y + y);
  last = paper_pdf_page_at (doc, doc->pdf_scroll_y + y + height - 1);

  for (i = first; i <= last && i < (gint) doc->pages->len; i++)
    {
      ScreenatorPaperPage *page =
	&g_array_index (doc->pages, ScreenatorPaperPage, i);
      gint top, page_x, page_y;

      if (!page->annotations)
	continue;

      top = g_array_index (doc->pdf_page_tops, gint, i);
      page_y = top - doc->pdf_scroll_y;
      page_x = page->style == SCREENATOR_PAPER_PDF
	? 0 : MAX (0, ((gint) data->width - page->canvas_width) / 2);

      cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
      cairo_set_source_surface (cr, page->annotations, page_x, page_y);
      cairo_paint (cr);

      if (page->painted)
	data->painted = 1;
    }

  cairo_destroy (cr);
  data->paper_page_dirty = FALSE;
}

/* Put the page under the viewport centre into the backbuffer at its position
 * in the continuous stack. */
static void
paper_pdf_prepare_active (ScreenatorData *data, ScreenatorPaperDocument *doc)
{
  ScreenatorPaperPage *page;
  gint index, top;

  if (!doc || !paper_continuous (data))
    return;

  /* The page at the top of the viewport is the current one; the page under
   * the pointer is picked separately when a stroke starts. */
  index = paper_pdf_page_at (doc, doc->pdf_scroll_y + 1);
  doc->page = index;
  data->paper_pdf_page = (gint) index;
  page = paper_current_page (doc);
  if (!page)
    return;

  top = g_array_index (doc->pdf_page_tops, gint, index);
  page->view_x = 0;
  page->view_y = -(top - doc->pdf_scroll_y);

  paper_ensure_background (data, doc, index, page);
  paper_load_continuous_view (data, doc);
  paper_top_bar_update_active_tool (data);
}

static void
paper_pdf_set_scroll (ScreenatorData *data, gint scroll)
{
  ScreenatorPaperDocument *doc = paper_active_document (data);
  gint max_scroll;
  gboolean profile = screenator_profile_enabled ();
  gint64 profile_start = profile ? g_get_monotonic_time () : 0;
  gint old_scroll;

  if (!doc || !paper_continuous (data))
    return;

  max_scroll = MAX (0, doc->pdf_stack_height - paper_view_height (data));
  scroll = CLAMP (scroll, 0, max_scroll);
  if (scroll == doc->pdf_scroll_y)
    return;

  old_scroll = doc->pdf_scroll_y;
  paper_store_active_page (data);

  {
    gint old_first = paper_pdf_page_at (doc, doc->pdf_scroll_y);
    gint old_last = paper_pdf_page_at (doc, doc->pdf_scroll_y +
				       MAX (0, paper_view_height (data) - 1));
    gint new_first, new_last;
    gint delta = scroll - doc->pdf_scroll_y;
    ScreenatorPaperPage *page;
    gint top;

    doc->pdf_scroll_y = scroll;
    new_first = paper_pdf_page_at (doc, doc->pdf_scroll_y);
    new_last = paper_pdf_page_at (doc, doc->pdf_scroll_y +
				  MAX (0, paper_view_height (data) - 1));

    if (new_first != old_first || new_last != old_last)
      g_atomic_int_inc (&data->pdf_render_serial);

    doc->page = (guint) new_first;
    data->paper_pdf_page = new_first;
    page = paper_current_page (doc);
    if (page)
      {
	top = g_array_index (doc->pdf_page_tops, gint, new_first);
	page->view_x = 0;
	page->view_y = -(top - doc->pdf_scroll_y);
	paper_ensure_background (data, doc, new_first, page);
      }
    paper_top_bar_update_active_tool (data);

    if (delta != 0 && paper_shift_backbuffer (data, delta))
      {
	if (delta > 0)
	  paper_fill_continuous_strip (data, doc,
				       paper_view_height (data) - delta, delta);
	else
	  paper_fill_continuous_strip (data, doc, 0, -delta);
      }
    else
      paper_load_continuous_view (data, doc);

    data->paper_pending_ink_valid = FALSE;
  }

  reset_undo_history (data);
  paper_repaint (data);
  schedule_tray_menu_refresh (data);

  if (profile)
    g_printerr ("PROFILE scroll %.2f ms delta=%d\n",
		(g_get_monotonic_time () - profile_start) / 1000.0,
		scroll - old_scroll);
}

/* Jump to the top of one PDF page in the continuous view. */
static void
paper_pdf_show_page (ScreenatorData *data, ScreenatorPaperDocument *doc,
		     guint index)
{
  ScreenatorPaperPage *page;
  gint max_scroll, top;

  if (!doc || index >= doc->pdf_page_tops->len)
    return;

  paper_store_active_page (data);
  g_atomic_int_inc (&data->pdf_render_serial);
  max_scroll = MAX (0, doc->pdf_stack_height - paper_view_height (data));
  top = g_array_index (doc->pdf_page_tops, gint, index);
  doc->pdf_scroll_y = CLAMP (top, 0, max_scroll);
  doc->page = index;
  data->paper_pdf_page = (gint) index;

  page = paper_current_page (doc);
  if (!page)
    return;

  page->view_x = 0;
  page->view_y = -(top - doc->pdf_scroll_y);
  paper_ensure_background (data, doc, index, page);
  paper_load_continuous_view (data, doc);
  reset_undo_history (data);
  paper_top_bar_update_active_tool (data);
  paper_repaint (data);
  schedule_tray_menu_refresh (data);
}

/* Erase a stroke segment directly on every page it crosses.  The tools
 * normally draw into the screen-sized backbuffer, which only holds the
 * active page; in the continuous view the pages above and below the viewport
 * are painted straight from their own annotation layers.  Drawing the
 * segment into each page's layer, offset by that page's position, lets cairo
 * crop it to the page it belongs to. */
void
screenator_paper_erase_line (ScreenatorData *data,
			     gint x1, gint y1, gint x2, gint y2,
			     gint width)
{
  ScreenatorPaperDocument *doc;
  guint i, count;

  if (!data || !paper_continuous (data))
    return;

  doc = paper_active_document (data);
  if (!doc || !doc->pages || !doc->pdf_page_tops ||
      !doc->pdf_page_heights)
    return;

  count = MIN (doc->pages->len, doc->pdf_page_tops->len);

  for (i = 0; i < count; i++)
    {
      ScreenatorPaperPage *page =
	&g_array_index (doc->pages, ScreenatorPaperPage, i);
      gint top;
      cairo_t *cr;

      if (!page->annotations)
	continue;

      top = g_array_index (doc->pdf_page_tops, gint, i);

      {
	gint page_x = page->style == SCREENATOR_PAPER_PDF
	  ? 0 : MAX (0, ((gint) data->width - page->canvas_width) / 2);

	cr = cairo_create (page->annotations);
	if (!data->composited)
	  cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);

	cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
	cairo_set_line_width (cr, MAX (1, width));
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);
	cairo_move_to (cr, x1 - page_x, y1 - (top - doc->pdf_scroll_y));
	cairo_line_to (cr, x2 - page_x, y2 - (top - doc->pdf_scroll_y));
      }
      cairo_stroke (cr);
      cairo_destroy (cr);
    }
}

/* Before a stroke starts, make the page under the pointer the active page. */
static void
paper_pdf_pick_page (ScreenatorData *data, gint screen_y)
{
  ScreenatorPaperDocument *doc = paper_active_document (data);
  ScreenatorPaperPage *page;
  gint content_y, index, top;

  if (!doc || !paper_continuous (data))
    return;

  content_y = doc->pdf_scroll_y + screen_y;
  index = paper_pdf_page_at (doc, content_y);
  if (index == (gint) doc->page)
    return;

  paper_commit_pending (data);

  /* The backbuffer already contains every visible page, so selecting the page
   * under the pointer is only bookkeeping. */
  doc->page = index;
  data->paper_pdf_page = (gint) index;
  page = paper_current_page (doc);
  if (!page)
    return;

  top = g_array_index (doc->pdf_page_tops, gint, index);
  page->view_x = 0;
  page->view_y = -(top - doc->pdf_scroll_y);
  paper_top_bar_update_active_tool (data);
  schedule_tray_menu_refresh (data);
}

/* Switch between page-by-page and continuous PDF views. */
static void
paper_set_continuous_scrolling (ScreenatorData *data, gboolean enable)
{
  ScreenatorPaperDocument *doc = paper_active_document (data);
  ScreenatorPaperPage *page;

  if (data->paper_continuous_scroll == enable)
    return;

  if (!doc || !doc->pages || doc->pages->len == 0 ||
      (doc->type == SCREENATOR_PAPER_PDF &&
       (!doc->pdf_loaded || doc->pdf_stack_height <= 0)))
    {
      data->paper_continuous_scroll = enable;
      return;
    }

  page = paper_current_page (doc);
  if (!page)
    {
      data->paper_continuous_scroll = enable;
      return;
    }

  paper_store_active_page (data);

  if (!enable)
    {
      gint max_y = MAX (0, page->canvas_height - (gint) data->height);

      data->paper_continuous_scroll = FALSE;

      if (doc->pdf_page_tops && doc->page < doc->pdf_page_tops->len)
	{
	  gint top = g_array_index (doc->pdf_page_tops, gint, doc->page);

	  page->view_y = CLAMP (doc->pdf_scroll_y - top, 0, max_y);
	}
      else
	page->view_y = CLAMP (page->view_y, 0, max_y);

      page->view_x = CLAMP (page->view_x, 0,
			    MAX (0, page->canvas_width - (gint) data->width));
      paper_load_page (data, page);
    }
  else
    {
      gint max_scroll, top;

      paper_build_continuous_layout (data, doc);
      max_scroll = MAX (0, doc->pdf_stack_height - paper_view_height (data));
      top = (doc->pdf_page_tops && doc->page < doc->pdf_page_tops->len)
	? g_array_index (doc->pdf_page_tops, gint, doc->page) : 0;

      data->paper_continuous_scroll = TRUE;
      doc->pdf_scroll_y = CLAMP (top + page->view_y, 0, max_scroll);
      paper_pdf_prepare_active (data, doc);
    }

  reset_undo_history (data);
  paper_repaint (data);
  schedule_tray_menu_refresh (data);
}

/* Draw the paper below the annotations.  The caller composites the backbuffer
 * on top afterwards, so erasing a stroke shows the paper again. */
static void
draw_pdf_loading_page (cairo_t *cr, ScreenatorPaperPage *page,
		       gint x, gint y)
{
  const gchar *text = page->pdf_render_pending
    ? "Loading..." : "PDF page unavailable";
  cairo_text_extents_t extents;
  gdouble font_size;

  cairo_save (cr);
  cairo_translate (cr, x, y);
  cairo_rectangle (cr, 0, 0, page->canvas_width, page->canvas_height);
  cairo_clip (cr);

  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_paint (cr);

  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
  cairo_set_source_rgb (cr, 0.55, 0.55, 0.55);
  cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
			  CAIRO_FONT_WEIGHT_NORMAL);
  font_size = MAX (14.0, page->canvas_width / 40.0);
  cairo_set_font_size (cr, font_size);
  cairo_text_extents (cr, text, &extents);
  cairo_move_to (cr,
		 (page->canvas_width - extents.width) / 2.0,
		 page->canvas_height / 2.0);
  cairo_show_text (cr, text);
  cairo_restore (cr);
}

static void
draw_paper_page (ScreenatorData *data, cairo_t *cr)
{
  ScreenatorPaperDocument *doc = paper_active_document (data);
  ScreenatorPaperPage *page = paper_current_page (doc);

  if (!page)
    return;

  cairo_save (cr);

  /* A light grey surround makes the paper edge visible when the page is
   * smaller than the screen. */
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgb (cr, 0.92, 0.92, 0.92);
  cairo_paint (cr);

  if (paper_continuous (data))
    {
      guint i;
      guint count = MIN (doc->pages->len, doc->pdf_page_tops->len);

      for (i = 0; i < count; i++)
	{
	  ScreenatorPaperPage *pdf_page =
	    &g_array_index (doc->pages, ScreenatorPaperPage, i);
	  gint top = g_array_index (doc->pdf_page_tops, gint, i);
	  gint height = g_array_index (doc->pdf_page_heights, gint, i);
	  gint y = top - doc->pdf_scroll_y;

	  if (top + height <= doc->pdf_scroll_y ||
	      top >= doc->pdf_scroll_y + paper_view_height (data))
	    continue;

	  gint page_x = pdf_page->style == SCREENATOR_PAPER_PDF
	    ? 0 : MAX (0, ((gint) data->width - pdf_page->canvas_width) / 2);

	  paper_ensure_background (data, doc, i, pdf_page);

	  if (pdf_page->style == SCREENATOR_PAPER_PDF)
	    {
	      if (pdf_page->background)
		{
		  pdf_page->background_stamp = ++paper_page_background_clock;
		  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		  cairo_set_source_surface (cr, pdf_page->background, 0, y);
		  use_bilinear_filter (cr);
		  cairo_paint (cr);
		}
	      else
		draw_pdf_loading_page (cr, pdf_page, 0, y);
	    }
	  else
	    {
	      cairo_save (cr);
	      cairo_translate (cr, page_x, y);
	      cairo_rectangle (cr, 0, 0,
			       pdf_page->canvas_width,
			       pdf_page->canvas_height);
	      cairo_clip (cr);
	      cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
	      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
	      cairo_paint (cr);
	      draw_paper_pattern (cr, pdf_page->style,
				  pdf_page->canvas_width,
				  pdf_page->canvas_height);
	      cairo_restore (cr);
	    }

	}

      cairo_restore (cr);
      return;
    }

  if (page->style == SCREENATOR_PAPER_PDF)
    {
      if (page->background)
	{
	  gint origin_x, origin_y;

	  page->background_stamp = ++paper_page_background_clock;
	  paper_page_origin (data, page, &origin_x, &origin_y);
	  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	  cairo_set_source_surface (cr, page->background,
				    origin_x - page->view_x,
				    origin_y - page->view_y);
	  use_bilinear_filter (cr);
	  cairo_paint (cr);
	}
      else
	{
	  gint origin_x, origin_y;

	  paper_page_origin (data, page, &origin_x, &origin_y);
	  draw_pdf_loading_page (cr, page,
				 origin_x - page->view_x,
				 origin_y - page->view_y);
	}
    }
  else
    {
      gint origin_x, origin_y;

      paper_page_origin (data, page, &origin_x, &origin_y);
      cairo_save (cr);
      cairo_translate (cr, origin_x - page->view_x,
		       origin_y - page->view_y);
      cairo_rectangle (cr, 0, 0,
		       page->canvas_width, page->canvas_height);
      cairo_clip (cr);
      cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
      cairo_paint (cr);
      draw_paper_pattern (cr, page->style,
			  page->canvas_width, page->canvas_height);
      cairo_restore (cr);
    }

  cairo_restore (cr);
}

static void
paper_background_cache_free (ScreenatorData *data)
{
  if (data->paper_background_cache)
    cairo_surface_destroy (data->paper_background_cache);

  data->paper_background_cache = NULL;
  data->paper_background_cache_valid = FALSE;
}

static void
paper_background_invalidate (ScreenatorData *data)
{
  data->paper_background_cache_valid = FALSE;
}

/* Centre a page that is smaller than the screen.  Pages larger than the
 * screen use zero and are panned instead. */
static void
paper_page_origin (ScreenatorData *data, ScreenatorPaperPage *page,
		   gint *x, gint *y)
{
  if (!page)
    {
      *x = *y = 0;
      return;
    }

  if (paper_continuous (data))
    {
      *x = page->style == SCREENATOR_PAPER_PDF
	? 0 : MAX (0, ((gint) data->width - page->canvas_width) / 2);
      *y = 0;
      return;
    }

  *x = MAX (0, ((gint) data->width - page->canvas_width) / 2);
  *y = MAX (0, ((gint) data->height - page->canvas_height) / 2);
}

/* Scale and page origin used by Window mode.  The scale makes the active
 * paper page exactly as wide as the Paper window.  At scale 1 and origin 0
 * the normal fullscreen presentation is unchanged. */
static void
paper_window_transform_values (ScreenatorData *data, gdouble *scale,
			       gint *origin_x, gint *origin_y)
{
  ScreenatorPaperDocument *doc;
  ScreenatorPaperPage *page;
  gint page_origin_x, page_origin_y, window_width, canvas_width;

  *scale = 1.0;
  *origin_x = 0;
  *origin_y = 0;

  if (!data || !data->paper_window_mode || !data->paper_win)
    return;

  doc = paper_active_document (data);
  page = paper_current_page (doc);
  if (!page)
    return;

  paper_page_origin (data, page, &page_origin_x, &page_origin_y);
  *origin_x = page_origin_x - page->view_x;

  /* In the continuous view the screen coordinate system is the viewport,
   * whose top-left is the window origin.  Using the current page's view_y
   * here shifts the whole window whenever a lower page is selected, which
   * corrupts drawing on every page except the top one. */
  *origin_y = paper_continuous (data) ? 0 : page_origin_y - page->view_y;

  window_width = gtk_widget_get_allocated_width (data->paper_win);
  if (window_width <= 0)
    window_width = (gint) data->width;

  canvas_width = page->canvas_width > 0
    ? page->canvas_width : (gint) data->width;
  if (canvas_width <= 0)
    return;

  *scale = (gdouble) window_width / (gdouble) canvas_width;
  if (*scale < 0.02)
    *scale = 0.02;
  if (*scale > 10.0)
    *scale = 10.0;
}

/* Desired content height for the current Window-mode geometry. */
static gint
paper_compute_view_height (ScreenatorData *data)
{
  gdouble scale;
  gint origin_x, origin_y, window_height, view_height;

  if (!data || !data->paper_window_mode || data->win != data->paper_win)
    return data->height;

  /* Extending the viewport is meaningful for the continuous page stack. */
  if (!paper_continuous (data))
    return data->height;

  paper_window_transform_values (data, &scale, &origin_x, &origin_y);
  if (scale <= 0.0)
    return data->height;

  window_height = gtk_widget_get_allocated_height (data->paper_win);
  if (window_height <= 0)
    return data->height;

  view_height = (gint) ceil ((gdouble) window_height / scale);
  view_height = MAX (view_height, (gint) data->height);

  /* Avoid pathological allocations in extremely narrow/tall windows. */
  view_height = MIN (view_height, (gint) data->height * 3);

  return view_height;
}

/* Actual content height currently backed by the drawing buffer. */
static gint
paper_view_height (ScreenatorData *data)
{
  if (!data || !data->paper_window_mode || data->win != data->paper_win)
    return data->height;

  return MAX ((gint) data->height, data->paper_view_height_current);
}

/* Make the drawing buffer and paint contexts match the current Window-mode
 * viewport. */
static void
paper_window_ensure_buffer (ScreenatorData *data)
{
  ScreenatorPaperDocument *doc;
  ScreenatorPaperPage *page;
  gint view_height;

  if (!data || !data->paper_window_mode || data->win != data->paper_win ||
      !data->backbuffer)
    return;

  view_height = paper_compute_view_height (data);
  if (cairo_image_surface_get_width (data->backbuffer) == (gint) data->width &&
      cairo_image_surface_get_height (data->backbuffer) == view_height)
    {
      data->paper_view_height_current = view_height;
      return;
    }

  /* Store with the old buffer height before changing it. */
  paper_commit_pending (data);
  paper_store_active_page (data);

  screenator_resize_drawing_buffer (data, (gint) data->width, view_height);
  data->paper_view_height_current = view_height;
  doc = paper_active_document (data);

  if (paper_continuous (data) && doc)
    paper_load_continuous_view (data, doc);
  else
    {
      page = paper_current_page (doc);
      if (page)
	paper_load_page (data, page);
    }

  update_window_shape (data);
}

/* Restore the normal screen-sized drawing buffer when leaving Window mode. */
static void
paper_window_restore_buffer (ScreenatorData *data)
{
  ScreenatorPaperDocument *doc;
  ScreenatorPaperPage *page;

  if (!data || !data->backbuffer)
    return;

  if (cairo_image_surface_get_width (data->backbuffer) == (gint) data->width &&
      cairo_image_surface_get_height (data->backbuffer) == (gint) data->height)
    return;

  /* Store while the tall viewport height is still the active one. */
  {
    gboolean was_mode = data->paper_window_mode;

    if (!was_mode && data->win == data->paper_win)
      data->paper_window_mode = TRUE;

    paper_commit_pending (data);
    paper_store_active_page (data);
    data->paper_window_mode = was_mode;
  }

  screenator_resize_drawing_buffer (data, (gint) data->width,
				    (gint) data->height);
  data->paper_view_height_current = (gint) data->height;
  doc = paper_active_document (data);
  if (paper_continuous (data) && doc)
    paper_load_continuous_view (data, doc);
  else
    {
      page = paper_current_page (doc);
      if (page)
	paper_load_page (data, page);
    }

  update_window_shape (data);
}

/* Scale applied to the paper in Window mode; 1.0 for fullscreen/Ctrl+D. */
gdouble
screenator_paper_window_scale (ScreenatorData *data)
{
  gdouble scale = 1.0;
  gint origin_x, origin_y;

  if (!data || !data->paper_window_mode || data->win != data->paper_win)
    return 1.0;

  paper_window_transform_values (data, &scale, &origin_x, &origin_y);
  return scale > 0.0 ? scale : 1.0;
}

static void
paper_window_transform_event (ScreenatorData *data, gdouble *x, gdouble *y)
{
  gdouble scale;
  gint origin_x, origin_y;

  if (!data || !data->paper_window_mode || data->win != data->paper_win)
    return;

  paper_window_transform_values (data, &scale, &origin_x, &origin_y);
  if (scale <= 0.0)
    return;

  *x = *x / scale + origin_x;
  *y = *y / scale + origin_y;
}

/* Convert a backbuffer coordinate to a root-window coordinate.  Used for
 * windows that are positioned independently, such as the inline text editor. */
static void
paper_window_to_root (ScreenatorData *data, gint *x, gint *y)
{
  gdouble scale;
  gint origin_x, origin_y, window_x, window_y;

  if (!data || !data->paper_window_mode || data->win != data->paper_win ||
      !data->paper_win)
    return;

  paper_window_transform_values (data, &scale, &origin_x, &origin_y);

  {
    GdkWindow *window = gtk_widget_get_window (data->paper_win);

    if (window)
      gdk_window_get_root_coords (window, 0, 0, &window_x, &window_y);
    else
      gtk_window_get_position (GTK_WINDOW (data->paper_win),
			       &window_x, &window_y);
  }

  *x = window_x + (gint) round ((*x - origin_x) * scale);
  *y = window_y + (gint) round ((*y - origin_y) * scale);
}

/* Invalidate the on-screen part that corresponds to a backbuffer rectangle.
 * Window mode scales and translates the paper, so using the raw backbuffer
 * rectangle would miss the area being drawn and make drawing look laggy. */
void
screenator_invalidate_rect (ScreenatorData *data, const GdkRectangle *rect)
{
  GdkWindow *window;
  GdkRectangle scaled;

  if (!data || !data->win)
    return;

  window = gtk_widget_get_window (data->win);
  if (!window)
    return;

  if (!rect)
    {
      gdk_window_invalidate_rect (window, NULL, 0);
      return;
    }

  if (data->paper_window_mode && data->win == data->paper_win)
    {
      gdouble scale;
      gint origin_x, origin_y;

      paper_window_transform_values (data, &scale, &origin_x, &origin_y);
      scaled.x = (gint) floor ((rect->x - origin_x) * scale) - 2;
      scaled.y = (gint) floor ((rect->y - origin_y) * scale) - 2;
      scaled.width = (gint) ceil (rect->width * scale) + 4;
      scaled.height = (gint) ceil (rect->height * scale) + 4;
      gdk_window_invalidate_rect (window, &scaled, 0);
    }
  else
    gdk_window_invalidate_rect (window, rect, 0);
}

/* TRUE when a pointer coordinate is over the visible Paper window.  Window
 * mode must not draw or show tool feedback outside its window; button drags
 * can still deliver coordinates outside during the X implicit grab. */
static gboolean
paper_window_event_inside (ScreenatorData *data, gdouble x, gdouble y)
{
  gdouble scale;
  gint origin_x, origin_y;
  gdouble screen_x, screen_y;

  if (!screenator_paper_local_input (data))
    return TRUE;

  if (!data->paper_window_mode)
    {
      gint width = gtk_widget_get_allocated_width (data->paper_win);
      gint height = gtk_widget_get_allocated_height (data->paper_win);

      if (width <= 0 || height <= 0)
	return TRUE;

      return x >= 0 && y >= 0 && x < width && y < height;
    }

  paper_window_transform_values (data, &scale, &origin_x, &origin_y);
  if (scale <= 0.0)
    return TRUE;

  /* A tall window extends beyond the scaled screen content.  Pointer events
   * there must not draw outside the backbuffer. */
  screen_x = x / scale + origin_x;
  screen_y = y / scale + origin_y;

  return screen_x >= 0 && screen_y >= 0 &&
    screen_x < (gdouble) data->width &&
    screen_y < (gdouble) paper_view_height (data);
}

static void
on_paper_window_size_allocate (GtkWidget *widget, GdkRectangle *allocation,
			       gpointer user_data)
{
  ScreenatorData *data = user_data;

  (void) widget;
  (void) allocation;

  if (data->paper_window_mode && data->win == data->paper_win)
    {
      paper_window_ensure_buffer (data);
      paper_repaint_window_only (data);

      if (ABS (screenator_paper_window_scale (data) -
	       data->eraser_cursor_scale) > 0.001)
	{
	  update_eraser_cursor (data);
	  paper_update_hover_cursor (data);
	}
    }
}

/* Largest viewport offsets for a page.  PDF pages use their rendered size;
 * other papers use their configured canvas size. */
static void
paper_page_view_limits (ScreenatorData *data, ScreenatorPaperDocument *doc,
			guint index, ScreenatorPaperPage *page,
			gint *max_x, gint *max_y)
{
  if (!page)
    {
      *max_x = *max_y = 0;
      return;
    }

  if (!page->canvas_width || !page->canvas_height)
    {
      if (page->style == SCREENATOR_PAPER_PDF)
	paper_ensure_background (data, doc, index, page);

      if (page->style == SCREENATOR_PAPER_PDF && page->background)
	{
	  page->canvas_width = page->background_width;
	  page->canvas_height = page->background_height;
	}
      else if (page->style != SCREENATOR_PAPER_PDF)
	paper_size_dimensions (data, &page->canvas_width,
			       &page->canvas_height,
			       &page->pdf_width, &page->pdf_height);
    }

  *max_x = MAX (0, page->canvas_width - (gint) data->width);
  *max_y = MAX (0, page->canvas_height - (gint) data->height);
}

/* Move the visible part of a page.  The old viewport is stored into the
 * full-page annotation layer, then the new viewport is loaded from it, so
 * drawings outside the screen are not lost. */
static void
paper_set_view (ScreenatorData *data, gint x, gint y)
{
  ScreenatorPaperDocument *doc = paper_active_document (data);
  ScreenatorPaperPage *page = paper_current_page (doc);
  gint max_x, max_y;

  if (!page)
    return;

  paper_page_view_limits (data, doc, doc->page, page, &max_x, &max_y);
  x = CLAMP (x, 0, max_x);
  y = CLAMP (y, 0, max_y);

  if (x == page->view_x && y == page->view_y)
    return;

  paper_store_active_page (data);
  page->view_x = x;
  page->view_y = y;
  paper_load_page (data, page);
  reset_undo_history (data);
  paper_repaint (data);
}

static gboolean
hover_settle_idle (gpointer user_data)
{
  ScreenatorData *data = user_data;

  data->controls_hover_idle = 0;
  paper_refresh_controls_hover (data);
  paper_update_hover_cursor (data);
  return G_SOURCE_REMOVE;
}

static gboolean
on_controls_enter (GtkWidget *widget, GdkEventCrossing *event,
		   gpointer user_data)
{
  ScreenatorData *data = user_data;

  (void) widget;
  (void) event;

  data->controls_hover++;
  if (data->controls_hover == 1)
    paper_update_hover_cursor (data);

  return FALSE;
}

static gboolean
on_controls_leave (GtkWidget *widget, GdkEventCrossing *event,
		   gpointer user_data)
{
  ScreenatorData *data = user_data;

  (void) widget;
  (void) event;

  if (data->controls_hover > 0)
    data->controls_hover--;

  if (data->controls_hover == 0)
    {
      /* Moving between child windows of the toolbar can produce a leave on
       * the toplevel before the next child's enter.  Check the real pointer
       * now, then once more after the crossing events have settled. */
      paper_refresh_controls_hover (data);
      paper_update_hover_cursor (data);

      if (!data->controls_hover_idle)
	data->controls_hover_idle = g_idle_add (hover_settle_idle, data);
    }

  return FALSE;
}

static void
paper_controls_attach_hover (ScreenatorData *data, GtkWidget *widget)
{
  gtk_widget_add_events (widget, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
  g_signal_connect (widget, "enter-notify-event",
		    G_CALLBACK (on_controls_enter), data);
  g_signal_connect (widget, "leave-notify-event",
		    G_CALLBACK (on_controls_leave), data);
}

/* Re-grab every active device with the normal cursor while the pointer is
 * over one of Screenator's control windows, and with the tool cursor
 * otherwise. */
static void
paper_update_hover_cursor (ScreenatorData *data)
{
  GdkWindow *window = data->win ? gtk_widget_get_window (data->win) : NULL;
  GHashTableIter it;
  gpointer value;

  if (!window)
    return;

  /* Crossing events can be lost after a menu, a toolbar click, or a window
   * change.  Recompute the hover state from the real pointer position before
   * choosing a cursor. */
  paper_refresh_controls_hover (data);

  /* Grabbed devices use their own cursor.  The window cursor matters while
   * no device is grabbed, for example in raw-input fallback mode, so keep it
   * in step with the selected tool as well. */
  if (data->paper_hand_tool || data->paper_panning)
    gdk_window_set_cursor (window, data->hand_cursor);
  else if (data->forced_context)
    gdk_window_set_cursor (window,
			   get_tool_cursor (data, data->forced_context));
  else
    gdk_window_set_cursor (window, NULL);

  /* In every Paper presentation the window cursor already follows the
   * pointer only over the Paper window.  Re-grabbing devices here would make
   * the tool cursor appear over other applications again. */
  if (screenator_paper_local_input (data))
    return;

  if (!data->devdatatable)
    return;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      ScreenatorDeviceData *devdata = value;
      GdkCursor *cursor = NULL;

      if (!devdata->is_grabbed)
	continue;

      if (data->controls_hover <= 0)
	{
	  /* Use the tool that is actually selected, not a cursor cached on
	   * this device.  After a resolution or mode change the cached context
	   * may belong to a tool that is no longer active. */
	  if (data->paper_panning || data->paper_hand_tool)
	    cursor = data->hand_cursor;
	  else if (devdata->saved_context)
	    cursor = get_tool_cursor (data, devdata->cur_context);
	  else if (data->forced_context)
	    cursor = get_tool_cursor (data, data->forced_context);
	  else
	    cursor = NULL;
	}

      gdk_device_grab (devdata->device, window, GDK_OWNERSHIP_NONE,
		       screenator_paper_grab_owner_events (data),
		       SCREENATOR_MOUSE_EVENTS, cursor, GDK_CURRENT_TIME);
    }
}

/* Re-apply the cursor that belongs to the tool which is actually selected
 * now.  Used after mode changes, resolution changes and paper/PDF loading,
 * where cached cursors or scaled eraser sizes may be stale. */
static void
paper_refresh_active_cursor (ScreenatorData *data)
{
  if (!data)
    return;

  update_eraser_cursor (data);
  paper_update_hover_cursor (data);
}

/* Recompute the control-window hover state from the real pointer position.
 * Crossing events can be missing after a menu, dialog or toolbar drag. */
static void
paper_refresh_controls_hover (ScreenatorData *data)
{
  GdkWindow *pointer_window = gdk_window_at_pointer (NULL, NULL);
  GdkWindow *icon_window =
    data->icon_win ? gtk_widget_get_window (data->icon_win) : NULL;
  GdkWindow *settings_window =
    data->settings_win ? gtk_widget_get_window (data->settings_win) : NULL;
  GdkWindow *bar_window =
    data->paper_top_bar ? gtk_widget_get_window (data->paper_top_bar) : NULL;
  GdkWindow *window;
  gboolean over = FALSE;

  /* The pointer can be over a child X window of the toolbar or icon, so walk
   * the window tree up to the control toplevels. */
  for (window = pointer_window; window && !over;
       window = gdk_window_get_parent (window))
    {
      if (window == icon_window || window == settings_window ||
	  window == bar_window)
	over = TRUE;
    }

  data->controls_hover = over ? 1 : 0;
}

static void
set_paper_pan_cursor (ScreenatorData *data)
{
  GdkWindow *window = data->win ? gtk_widget_get_window (data->win) : NULL;
  GHashTableIter it;
  gpointer value;

  if (!window || !data->hand_cursor)
    return;

  gdk_window_set_cursor (window, data->hand_cursor);

  if (screenator_paper_local_input (data))
    return;

  if (!data->devdatatable)
    return;

  /* A grabbed device uses the cursor from its grab, not the window cursor. */
  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      ScreenatorDeviceData *devdata = value;

      if (devdata->is_grabbed)
	gdk_device_grab (devdata->device, window, GDK_OWNERSHIP_NONE,
			 screenator_paper_grab_owner_events (data),
			 SCREENATOR_MOUSE_EVENTS,
			 data->controls_hover > 0 ? NULL : data->hand_cursor,
			 GDK_CURRENT_TIME);
    }
}

static void
restore_paper_cursor (ScreenatorData *data)
{
  GdkWindow *window = data->win ? gtk_widget_get_window (data->win) : NULL;
  GHashTableIter it;
  gpointer value;

  /* While panning, the pointer stays a hand between drags. */
  if (data->paper_hand_tool)
    {
      set_paper_pan_cursor (data);
      return;
    }

  if (window)
    gdk_window_set_cursor (window,
			   data->forced_context
			   ? get_tool_cursor (data, data->forced_context)
			   : NULL);

  if (screenator_paper_local_input (data))
    return;

  if (!data->devdatatable)
    return;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      ScreenatorDeviceData *devdata = value;

      if (devdata->is_grabbed)
	set_device_cursor (data, devdata->device,
			   devdata->saved_context
			   ? devdata->cur_context : data->forced_context);
    }
}

void
screenator_paper_pan_start (ScreenatorData *data, guint button, gint x, gint y)
{
  if (data->paper == SCREENATOR_PAPER_NONE)
    return;

  data->paper_panning = TRUE;
  data->paper_pan_button = button;
  data->paper_pan_x = x;
  data->paper_pan_y = y;
  set_paper_pan_cursor (data);
}

void
screenator_paper_pan_motion (ScreenatorData *data, gint x, gint y)
{
  ScreenatorPaperDocument *doc;
  ScreenatorPaperPage *page;
  gint dx, dy;

  if (!data->paper_panning)
    return;

  dx = x - data->paper_pan_x;
  dy = y - data->paper_pan_y;
  data->paper_pan_x = x;
  data->paper_pan_y = y;

  doc = paper_active_document (data);
  page = paper_current_page (doc);
  if (!page)
    return;

  if (paper_continuous (data))
    {
      paper_pdf_set_scroll (data, doc->pdf_scroll_y - dy);
      return;
    }

  paper_set_view (data, page->view_x - dx, page->view_y - dy);
}

void
screenator_paper_pan_end (ScreenatorData *data)
{
  gboolean was_panning = data->paper_panning;

  data->paper_panning = FALSE;
  data->paper_pan_button = 0;

  if (was_panning)
    restore_paper_cursor (data);
}

/* The right mouse button and stylus side button 2 erase only while they are
 * pressed.  Restore the tool that was active before and drop the grab that
 * was taken just for the temporary eraser. */
static void
paper_finish_temporary_eraser (ScreenatorData *data)
{
  ScreenatorDeviceData *devdata = NULL;
  GdkDevice *device;

  if (!data->paper_eraser_active)
    return;

  device = data->paper_eraser_device;
  data->paper_eraser_active = FALSE;
  data->paper_eraser_device = NULL;

  if (device && data->devdatatable)
    devdata = g_hash_table_lookup (data->devdatatable, device);

  if (devdata)
    {
      devdata->stylus_eraser_active = FALSE;

      if (devdata->saved_context)
	{
	  devdata->cur_context = devdata->saved_context;
	  devdata->saved_context = NULL;
	}

      /* The pointer may still be over the window when a dialog opens. */
      if (devdata->is_grabbed)
	set_device_cursor (data, device, devdata->cur_context);
    }

  if (data->paper_hand_tool)
    set_paper_pan_cursor (data);

  /* If the eraser itself grabbed the device, let it go again so the paper
   * becomes idle as soon as the button is released. */
  if (!data->paper_eraser_was_grabbed && device)
    release_grab (data, device);

  data->paper_eraser_was_grabbed = FALSE;
}

/* Write every page of a document to a PDF.  For a PDF source the rendered
 * page is the background and the annotations are placed where they were on
 * screen. */
static gboolean
paper_write_pdf_document (ScreenatorData *data, ScreenatorPaperDocument *doc,
			  const gchar *path)
{
  cairo_surface_t *pdf;
  cairo_status_t status;
  guint i;

  if (!doc || !doc->pages || doc->pages->len == 0)
    return FALSE;

  pdf = cairo_pdf_surface_create (path, data->width, data->height);
  status = cairo_surface_status (pdf);

  if (status != CAIRO_STATUS_SUCCESS)
    {
      g_printerr ("Could not create the PDF %s: %s\n", path,
		  cairo_status_to_string (status));
      cairo_surface_destroy (pdf);
      return FALSE;
    }

  for (i = 0; i < doc->pages->len; i++)
    {
      ScreenatorPaperPage *page =
	&g_array_index (doc->pages, ScreenatorPaperPage, i);
      gint canvas_width = page->canvas_width;
      gint canvas_height = page->canvas_height;
      gdouble pdf_width = page->pdf_width;
      gdouble pdf_height = page->pdf_height;
      cairo_t *cr;

      if (page->style == SCREENATOR_PAPER_PDF)
	{
	  paper_ensure_background (data, doc, i, page);
	  if (page->background)
	    {
	      canvas_width = page->background_width;
	      canvas_height = page->background_height;
	    }
	  pdf_width = canvas_width;
	  pdf_height = canvas_height;
	}
      else if (!canvas_width || !canvas_height)
	paper_size_dimensions (data, &canvas_width, &canvas_height,
			       &pdf_width, &pdf_height);

      if (!canvas_width || !canvas_height)
	{
	  canvas_width = data->width;
	  canvas_height = data->height;
	}
      if (pdf_width <= 0 || pdf_height <= 0)
	{
	  pdf_width = canvas_width;
	  pdf_height = canvas_height;
	}

      cairo_pdf_surface_set_size (pdf, pdf_width, pdf_height);
      cr = cairo_create (pdf);

      if (page->style == SCREENATOR_PAPER_PDF)
	{
	  gboolean rendered = FALSE;

#ifdef HAVE_POPPLER_GLIB
	  if (doc->pdf_document)
	    {
	      PopplerPage *pdf_page;

	      g_mutex_lock (&paper_pdf_render_mutex);
	      pdf_page = poppler_document_get_page (
		(PopplerDocument *) doc->pdf_document, i);

	      if (pdf_page)
		{
		  gdouble source_width = 0.0, source_height = 0.0;

		  poppler_page_get_size (pdf_page, &source_width,
					 &source_height);
		  cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
		  cairo_paint (cr);

		  if (source_width > 0.0 && source_height > 0.0)
		    {
		      cairo_save (cr);
		      cairo_scale (cr, pdf_width / source_width,
				   pdf_height / source_height);
		      poppler_page_render (pdf_page, cr);
		      cairo_restore (cr);
		      rendered = TRUE;
		    }

		  g_object_unref (pdf_page);
		}
	      g_mutex_unlock (&paper_pdf_render_mutex);
	    }
#endif

	  /* Vector-only export: never fall back to the rendered bitmap of the
	   * source PDF page. */
	  if (!rendered)
	    {
	      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
	      cairo_paint (cr);
	    }
	}
      else
	{
	  cairo_scale (cr, pdf_width / canvas_width,
		       pdf_height / canvas_height);
	  draw_paper_background (cr, page->style,
				 canvas_width, canvas_height);
	}

      cairo_save (cr);
      if (page->style != SCREENATOR_PAPER_PDF && canvas_width > 0 &&
	  canvas_height > 0)
	cairo_scale (cr, pdf_width / canvas_width,
		     pdf_height / canvas_height);

      cairo_rectangle (cr, 0, 0, canvas_width, canvas_height);
      cairo_clip (cr);

      if (g_getenv ("SCREENATOR_VECTOR_DEBUG"))
	g_printerr ("VECTOR: save page %u style %d valid=%d vector=%p "
		    "type=%d annotations=%p\n", i, page->style,
		    page->vector_valid, (void *) page->vector,
		    page->vector ? (gint) cairo_surface_get_type (page->vector)
		    : -1, (void *) page->annotations);

      /* Vector-only export.  The annotation raster is never written to the
       * PDF; text and graphics must remain text and vector paths. */
      if (page->vector)
	{
	  cairo_set_source_surface (cr, page->vector, 0, 0);
	  cairo_paint (cr);
	}
      cairo_restore (cr);

      cairo_show_page (cr);
      cairo_destroy (cr);
    }

  cairo_surface_finish (pdf);
  status = cairo_surface_status (pdf);
  cairo_surface_destroy (pdf);

  if (status != CAIRO_STATUS_SUCCESS)
    {
      g_printerr ("Could not save the PDF %s: %s\n", path,
		  cairo_status_to_string (status));
      return FALSE;
    }

  return TRUE;
}

/* Folder used for automatic paper saves. */
static gchar *
paper_documents_directory (void)
{
  const gchar *documents =
    g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS);

  if (documents && *documents)
    return g_strdup (documents);

  return g_build_filename (g_get_home_dir (), "Documents", NULL);
}

static const gchar *
paper_type_slug (ScreenatorPaperType type)
{
  switch (type)
    {
    case SCREENATOR_PAPER_PLAIN:
      return "plain";
    case SCREENATOR_PAPER_RULED:
      return "ruled";
    case SCREENATOR_PAPER_GRAPH:
      return "graph";
    case SCREENATOR_PAPER_PDF:
      return "pdf";
    default:
      return "paper";
    }
}

static void
paper_pump_events (void)
{
  gint i = 0;

  while (gtk_events_pending () && i++ < 100)
    gtk_main_iteration_do (FALSE);
}

/* Small non-focusable overlay shown while a PDF is written. */
static GtkWidget *
paper_show_save_progress (ScreenatorData *data)
{
  GtkWidget *window;
  GtkWidget *box;
  GtkWidget *label;
  GtkWidget *bar;
  GdkScreen *screen;
  GdkRectangle area;
  gint width, height, monitor;

  window = gtk_window_new (GTK_WINDOW_POPUP);
  gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
  gtk_window_set_skip_pager_hint (GTK_WINDOW (window), TRUE);
  gtk_window_set_accept_focus (GTK_WINDOW (window), FALSE);
  gtk_window_set_focus_on_map (GTK_WINDOW (window), FALSE);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
  gtk_container_set_border_width (GTK_CONTAINER (box), 18);
  label = gtk_label_new ("Saving PDF...");
  gtk_label_set_markup (GTK_LABEL (label), "<b>Saving PDF...</b>");
  bar = gtk_progress_bar_new ();
  gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (bar), 0.15);
  gtk_progress_bar_pulse (GTK_PROGRESS_BAR (bar));
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box), bar, FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), box);

  make_dialog_popup (window);

  /* Block pointer/keyboard handling on the paper while the save runs. */
  g_object_set_data (G_OBJECT (window), "screenator-save-previous-dialog",
		     data->top_dialog);
  data->top_dialog = window;

  gtk_widget_show_all (window);
  paper_pump_events ();

  screen = gdk_screen_get_default ();
  monitor = screen ? gdk_screen_get_primary_monitor (screen) : 0;
  if (screen && monitor >= 0)
    gdk_screen_get_monitor_geometry (screen, monitor, &area);
  else
    {
      area.x = 0;
      area.y = 0;
      area.width = gdk_screen_width ();
      area.height = gdk_screen_height ();
    }

  gtk_window_get_size (GTK_WINDOW (window), &width, &height);
  gtk_window_move (GTK_WINDOW (window),
		   area.x + MAX (0, (area.width - width) / 2),
		   area.y + MAX (0, (area.height - height) / 2));

  paper_raise_over_panel (data);
  if (gtk_widget_get_window (window))
    gdk_window_raise (gtk_widget_get_window (window));
  paper_pump_events ();

  return window;
}

static void
paper_hide_save_progress (ScreenatorData *data, GtkWidget *window)
{
  GtkWidget *previous;

  if (!window)
    return;

  previous = g_object_get_data (G_OBJECT (window),
				"screenator-save-previous-dialog");
  gtk_widget_destroy (window);
  data->top_dialog = previous;
  paper_update_panel_raise (data);
}

/* Save one paper document to the Documents folder as
 * Screenator-<type>-YYYYMMDD-HHMMSS.pdf.  A document is only written when it
 * has been changed since its last save. */
static gboolean
paper_autosave_document (ScreenatorData *data, ScreenatorPaperDocument *doc,
			 gchar **saved_path)
{
  GDateTime *now;
  gchar *directory, *stamp, *name, *path;

  if (saved_path)
    *saved_path = NULL;

  if (data->paper_saving)
    return FALSE;

  if (!doc || !doc->pages || doc->pages->len == 0 || !doc->modified)
    return FALSE;

  if (data->paper == doc->type)
    {
      paper_commit_pending (data);
      paper_store_active_page (data);
    }

  directory = paper_documents_directory ();
  if (g_mkdir_with_parents (directory, 0755) != 0)
    {
      g_free (directory);
      return FALSE;
    }

  now = g_date_time_new_now_local ();
  stamp = g_date_time_format (now, "%Y%m%d-%H%M%S");
  name = g_strdup_printf ("Screenator-%s-%s.pdf",
			  paper_type_slug (doc->type), stamp);
  path = g_build_filename (directory, name, NULL);

  g_free (name);
  g_free (stamp);
  g_free (directory);
  g_date_time_unref (now);

  {
    GtkWidget *progress;
    gboolean written;

    data->paper_saving = TRUE;
    progress = paper_show_save_progress (data);
    written = paper_write_pdf_document (data, doc, path);
    paper_hide_save_progress (data, progress);
    data->paper_saving = FALSE;

    if (!written)
      {
	g_free (path);
	return FALSE;
      }
  }

  doc->modified = FALSE;
  if (saved_path)
    *saved_path = path;
  else
    g_free (path);

  return TRUE;
}

static void
paper_show_saved_paths (ScreenatorData *data, GList *paths)
{
  GtkWidget *dialog;
  GString *text = g_string_new (NULL);
  GList *l;

  if (!paths)
    return;

  if (g_list_length (paths) == 1)
    g_string_append (text, "Saved to:\n");
  else
    g_string_append (text, "The changed papers were saved to:\n");

  for (l = paths; l; l = l->next)
    {
      g_string_append (text, (const gchar *) l->data);
      g_string_append_c (text, '\n');
    }

  dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
				   GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
				   "%s", text->str);
  gtk_window_set_title (GTK_WINDOW (dialog), "Paper saved");
  show_dialog_above_drawings (data, dialog);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);

  g_string_free (text, TRUE);
}

static void
paper_show_save_failure (ScreenatorData *data)
{
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
				   GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
				   "The paper could not be saved to the "
				   "Documents folder.");
  gtk_window_set_title (GTK_WINDOW (dialog), "Could not save paper");
  show_dialog_above_drawings (data, dialog);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

/* The desktop file chooser is a separate, normally managed window.  The
 * annotation overlay is always above managed windows, so hide it (and the
 * floating controls) while the chooser is open.  The global shortcuts and the
 * painting grab are released too, so the chooser owns the keyboard and
 * pointer. */
static gint
paper_run_native_dialog (ScreenatorData *data, GtkFileChooserNative *dialog)
{
  gboolean was_grabbed;
  gboolean win_visible, icon_visible, settings_visible, bar_visible;
  gint response;
#ifdef GDK_WINDOWING_X11
  GdkWindow *win;
  GdkDisplay *gdk_display = NULL;
  Display *xdisplay = NULL;
  Window prev_focus = None;
  int revert = 0;
#endif

  was_grabbed = screenator_any_grabbed (data);

  paper_finish_temporary_eraser (data);
  screenator_paper_pan_end (data);
  data->native_dialog_open = TRUE;

  /* Releasing the grab can hide an empty Screen Annotation window.  Check the
   * visibility afterwards so the restore below matches the real state. */
  if (was_grabbed)
    release_grab (data, NULL);

  win_visible = data->win && gtk_widget_get_visible (data->win);
  icon_visible = data->icon_win && gtk_widget_get_visible (data->icon_win);
  settings_visible = data->settings_win &&
    gtk_widget_get_visible (data->settings_win);
  bar_visible = data->paper_top_bar &&
    gtk_widget_get_visible (data->paper_top_bar);

#ifdef GDK_WINDOWING_X11
  win = data->win ? gtk_widget_get_window (data->win) : NULL;
  if (win && GDK_IS_X11_WINDOW (win))
    {
      gdk_display = gdk_window_get_display (win);
      xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display);

      if (data->paper == SCREENATOR_PAPER_NONE)
	{
	  XGetInputFocus (xdisplay, &prev_focus, &revert);
	  if (prev_focus == None || prev_focus == PointerRoot ||
	      prev_focus == GDK_WINDOW_XID (win))
	    prev_focus = None;
	}
    }
#endif

  screenator_suspend_shortcuts (data, TRUE);

  if (bar_visible)
    gtk_widget_hide (data->paper_top_bar);
  if (settings_visible)
    gtk_widget_hide (data->settings_win);
  if (icon_visible)
    gtk_widget_hide (data->icon_win);
  if (win_visible)
    gtk_widget_hide (data->win);

  gdk_display_sync (gdk_display_get_default ());
  gdk_window_process_all_updates ();

  response = gtk_native_dialog_run (GTK_NATIVE_DIALOG (dialog));

  if (win_visible)
    gtk_widget_show (data->win);
  if (icon_visible)
    gtk_widget_show (data->icon_win);
  if (settings_visible)
    gtk_widget_show (data->settings_win);
  if (bar_visible)
    gtk_widget_show (data->paper_top_bar);

  if (data->win && gtk_widget_get_window (data->win))
    {
      gdk_window_raise (gtk_widget_get_window (data->win));
      gdk_window_invalidate_rect (gtk_widget_get_window (data->win), NULL, 0);
    }
  raise_icon_window (data);
  update_settings_indicator (data);

  screenator_suspend_shortcuts (data, FALSE);
  data->native_dialog_open = FALSE;

  if (was_grabbed)
    acquire_grab (data, NULL);

  if (data->paper != SCREENATOR_PAPER_NONE)
    paper_take_focus (data);
#ifdef GDK_WINDOWING_X11
  else if (xdisplay && prev_focus != None)
    {
      gdk_x11_display_error_trap_push (gdk_display);
      XSetInputFocus (xdisplay, prev_focus, RevertToParent, CurrentTime);
      XFlush (xdisplay);
      gdk_x11_display_error_trap_pop_ignored (gdk_display);
    }
#endif

  return response;
}

/* Switch the active paper to another selected type.  The current page is
 * stored first and the new document is loaded into the shared backbuffer. */
static void
paper_switch_to (ScreenatorData *data, ScreenatorPaperType type)
{
  ScreenatorPaperDocument *doc;
  ScreenatorPaperPage *page;

  if (data->paper == SCREENATOR_PAPER_NONE || data->paper == type)
    return;

  if (data->paper == SCREENATOR_PAPER_PDF)
    paper_pdf_remember_current (data);

  doc = paper_ensure_document (data, type);
  if (!doc)
    return;

  if (type == SCREENATOR_PAPER_PDF && !doc->pdf_loaded)
    return;

  screenator_paper_pan_end (data);
  paper_commit_pending (data);
  paper_store_active_page (data);
  screenator_page_history_clear (data);

  data->paper = type;
  data->paper_selected = type;

  if (type != SCREENATOR_PAPER_PDF && doc->pages->len == 0)
    {
      ScreenatorPaperPage fresh = paper_new_page (data, type);

      g_array_append_val (doc->pages, fresh);
      doc->page = 0;
    }

  if (doc->pages->len == 0)
    return;

  if (data->paper_continuous_scroll)
    {
      paper_build_continuous_layout (data, doc);

      if (doc->pdf_stack_height > 0)
	{
	  gint top = (doc->pdf_page_tops &&
		      doc->page < doc->pdf_page_tops->len)
	    ? g_array_index (doc->pdf_page_tops, gint, doc->page) : 0;

	  doc->pdf_scroll_y = CLAMP (top, 0,
				     MAX (0, doc->pdf_stack_height -
					  (gint) data->height));
	  paper_pdf_prepare_active (data, doc);
	  reset_undo_history (data);
	  paper_repaint (data);
	  schedule_tray_menu_refresh (data);
	  return;
	}
    }

  page = paper_current_page (doc);
  if (type == SCREENATOR_PAPER_PDF)
    paper_ensure_background (data, doc, doc->page, page);

  paper_load_page (data, page);
  reset_undo_history (data);
  paper_refresh_active_cursor (data);
  paper_repaint (data);
  schedule_tray_menu_refresh (data);
}

static gboolean
on_paper_window_focus_in (GtkWidget *widget, GdkEventFocus *event,
			  gpointer user_data)
{
  ScreenatorData *data = user_data;

  (void) widget;
  (void) event;

  data->paper_window_focused = TRUE;
  if (screenator_paper_local_input (data))
    screenator_paper_shortcuts_focus (data, TRUE);

  return FALSE;
}

static gboolean
on_paper_window_focus_out (GtkWidget *widget, GdkEventFocus *event,
			   gpointer user_data)
{
  ScreenatorData *data = user_data;

  (void) widget;
  (void) event;

  data->paper_window_focused = FALSE;
  if (screenator_paper_local_input (data))
    screenator_paper_shortcuts_focus (data, FALSE);

  return FALSE;
}

static gboolean
on_paper_window_delete (GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
  ScreenatorData *data = user_data;

  (void) widget;
  (void) event;

  screenator_exit_paper_drawing (data);
  return TRUE;
}

/* Create the normal Paper window.  Unlike the Screen Annotation overlay it is
 * a regular managed top-level window: other applications and dialogs can cover
 * it, and Alt+Tab switches away from it normally. */
void
screenator_create_paper_window (ScreenatorData *data)
{
  GtkWidget *window;

  if (!data || data->paper_win)
    return;

  if (!data->overlay_win)
    data->overlay_win = data->win;

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  data->paper_win = window;
  data->paper_window_fullscreen = FALSE;

  gtk_window_set_title (GTK_WINDOW (window), "Screenator Paper");
  gtk_window_set_resizable (GTK_WINDOW (window), TRUE);
  gtk_window_set_decorated (GTK_WINDOW (window), TRUE);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), FALSE);
  gtk_window_set_skip_pager_hint (GTK_WINDOW (window), FALSE);
  gtk_window_set_accept_focus (GTK_WINDOW (window), TRUE);
  gtk_window_set_focus_on_map (GTK_WINDOW (window), TRUE);
  gtk_widget_set_app_paintable (window, TRUE);
  /* Do not set a fixed size request: it becomes a minimum size and would stop
   * the Window mode from being resizable below the original screen size. */
  gtk_window_set_default_size (GTK_WINDOW (window),
			       data->width, data->height);

  gtk_widget_add_events (window,
			 SCREENATOR_WINDOW_EVENTS | GDK_FOCUS_CHANGE_MASK);
  g_signal_connect (window, "draw",
		    G_CALLBACK (on_expose), data);
  g_signal_connect (window, "motion_notify_event",
		    G_CALLBACK (on_motion), data);
  g_signal_connect (window, "button_press_event",
		    G_CALLBACK (on_buttonpress), data);
  g_signal_connect (window, "button_release_event",
		    G_CALLBACK (on_buttonrelease), data);
  g_signal_connect (window, "scroll_event",
		    G_CALLBACK (on_scroll), data);
  g_signal_connect (window, "size-allocate",
		    G_CALLBACK (on_paper_window_size_allocate), data);
  g_signal_connect (window, "delete-event",
		    G_CALLBACK (on_paper_window_delete), data);
  g_signal_connect (window, "focus-in-event",
		    G_CALLBACK (on_paper_window_focus_in), data);
  g_signal_connect (window, "focus-out-event",
		    G_CALLBACK (on_paper_window_focus_out), data);

  gtk_widget_realize (window);
  gtk_widget_hide (window);
}

/* Switch the active drawing window between the Screen overlay and the normal
 * Paper window. */
void
screenator_use_paper_window (ScreenatorData *data, gboolean use)
{
  if (!data)
    return;

  if (use)
    {
      screenator_create_paper_window (data);
      if (!data->paper_win)
	return;

      if (data->overlay_win && data->overlay_win != data->paper_win &&
	  gtk_widget_get_visible (data->overlay_win))
	gtk_widget_hide (data->overlay_win);

      data->win = data->paper_win;
      data->hidden = 0;
      data->paper_window_mode = FALSE;
      data->paper_view_height_current = (gint) data->height;
      screenator_window_mode_clear (data);
      gtk_widget_show (data->paper_win);

      if (!data->paper_window_fullscreen)
	{
	  gtk_window_fullscreen (GTK_WINDOW (data->paper_win));
	  data->paper_window_fullscreen = TRUE;
	}

      gtk_window_deiconify (GTK_WINDOW (data->paper_win));
      gtk_window_set_keep_above (GTK_WINDOW (data->paper_win), FALSE);
      gtk_window_present_with_time (GTK_WINDOW (data->paper_win),
				    gtk_get_current_event_time ());
      screenator_window_mode_enter (data);
      paper_refresh_active_cursor (data);
    }
  else
    {
      if (!data->paper_win)
	return;

      if (data->paper_window_mode)
	{
	  data->paper_window_mode = FALSE;
	  paper_window_restore_buffer (data);
	}

      gtk_widget_hide (data->paper_win);

      if (data->overlay_win)
	{
	  data->win = data->overlay_win;
	  screenator_window_mode_leave (data);
	  paper_refresh_active_cursor (data);

	  if (data->painted)
	    {
	      gtk_widget_show (data->overlay_win);
	      data->hidden = 0;
	    }
	  else
	    {
	      gtk_widget_hide (data->overlay_win);
	      data->hidden = 1;
	    }
	}
    }
}

/* Ctrl+W in Paper mode: switch between fullscreen and Window mode.  In
 * Window mode the paper page is scaled so its width fits the Paper window,
 * and the window can be resized or covered normally. */
void
screenator_toggle_paper_window_fullscreen (ScreenatorData *data)
{
  if (!data || !data->paper_win)
    return;

  if (!data->paper_window_mode)
    {
      ScreenatorPaperDocument *doc = paper_active_document (data);
      ScreenatorPaperPage *page = paper_current_page (doc);
      GdkScreen *screen = gtk_widget_get_screen (data->paper_win);
      GdkRectangle area;
      gint monitor, canvas_width, window_width, window_height;
      gdouble scale;

      if (screen)
	{
	  monitor = gdk_screen_get_primary_monitor (screen);
	  if (monitor < 0)
	    monitor = 0;
	  gdk_screen_get_monitor_workarea (screen, monitor, &area);
	}
      else
	{
	  area.x = 0;
	  area.y = 0;
	  area.width = data->width;
	  area.height = data->height;
	}

      canvas_width = (page && page->canvas_width > 0)
	? page->canvas_width : (gint) data->width;
      if (canvas_width <= 0)
	canvas_width = (gint) data->width;

      window_width = MAX (320, area.width * 4 / 5);
      scale = (gdouble) window_width / (gdouble) canvas_width;
      window_height = MAX (240, (gint) round (data->height * scale));
      window_height = MIN (window_height, area.height * 9 / 10);

      gtk_window_unfullscreen (GTK_WINDOW (data->paper_win));
      gtk_window_set_decorated (GTK_WINDOW (data->paper_win), TRUE);
      gtk_window_set_resizable (GTK_WINDOW (data->paper_win), TRUE);
      gtk_window_set_keep_above (GTK_WINDOW (data->paper_win), FALSE);
      data->paper_window_fullscreen = FALSE;
      data->paper_window_mode = TRUE;

      gtk_window_resize (GTK_WINDOW (data->paper_win),
			 window_width, window_height);
      gtk_window_move (GTK_WINDOW (data->paper_win),
		       area.x + MAX (0, (area.width - window_width) / 2),
		       area.y + MAX (0, (area.height - window_height) / 2));
      gtk_window_present (GTK_WINDOW (data->paper_win));
      paper_window_ensure_buffer (data);
    }
  else
    {
      data->paper_window_mode = FALSE;
      paper_window_restore_buffer (data);
      update_paper_shortcuts (data, TRUE);
      gtk_window_fullscreen (GTK_WINDOW (data->paper_win));
      gtk_window_set_keep_above (GTK_WINDOW (data->paper_win), FALSE);
      data->paper_window_fullscreen = TRUE;
    }

  paper_refresh_active_cursor (data);
  paper_repaint (data);
}

/* Ctrl+D in Paper mode: leave fullscreen just enough for the desktop panel to
 * appear above the paper.  This is not Window mode: the window is not made
 * resizable and its paper presentation is unchanged. */
void
screenator_toggle_paper_panel (ScreenatorData *data)
{
  if (!data || !data->paper_win || data->paper == SCREENATOR_PAPER_NONE ||
      data->paper_window_mode)
    return;

  if (data->paper_window_fullscreen)
    {
      GdkScreen *screen = gtk_widget_get_screen (data->paper_win);
      GdkRectangle area;
      gint monitor = 0;

      if (screen)
	{
	  monitor = gdk_screen_get_primary_monitor (screen);
	  if (monitor < 0)
	    monitor = 0;
	  gdk_screen_get_monitor_geometry (screen, monitor, &area);
	}
      else
	{
	  area.x = 0;
	  area.y = 0;
	  area.width = data->width;
	  area.height = data->height;
	}

      gtk_window_unfullscreen (GTK_WINDOW (data->paper_win));
      data->paper_window_fullscreen = FALSE;

      /* Keep the paper exactly as in fullscreen mode: borderless and
       * non-resizable, covering the monitor.  The desktop panel is in the
       * window-manager dock layer, so it stays above this normal window. */
      gtk_window_set_decorated (GTK_WINDOW (data->paper_win), FALSE);
      gtk_window_set_resizable (GTK_WINDOW (data->paper_win), FALSE);
      gtk_window_set_keep_above (GTK_WINDOW (data->paper_win), FALSE);
      gtk_window_resize (GTK_WINDOW (data->paper_win), area.width, area.height);
      gtk_window_move (GTK_WINDOW (data->paper_win), area.x, area.y);
      gtk_window_present (GTK_WINDOW (data->paper_win));
    }
  else
    {
      gtk_window_set_decorated (GTK_WINDOW (data->paper_win), TRUE);
      gtk_window_set_resizable (GTK_WINDOW (data->paper_win), TRUE);
      gtk_window_fullscreen (GTK_WINDOW (data->paper_win));
      gtk_window_set_keep_above (GTK_WINDOW (data->paper_win), FALSE);
      data->paper_window_fullscreen = TRUE;
    }

  paper_refresh_active_cursor (data);
  paper_repaint (data);
}

/* Enter Paper Drawing with the selected document. */
void
screenator_enter_paper_drawing (ScreenatorData *data,
				ScreenatorPaperType paper)
{
  ScreenatorPaperDocument *doc;
  ScreenatorPaperPage *page;

  if (paper == SCREENATOR_PAPER_NONE)
    {
      screenator_exit_paper_drawing (data);
      return;
    }

  screenator_timer_cancel (data);

  if (data->paper != SCREENATOR_PAPER_NONE)
    {
      paper_switch_to (data, paper);
      return;
    }

  {
    gboolean loaded_saved = FALSE;
    gint saved_page = MAX (0, data->paper_pdf_page);

    if (paper == SCREENATOR_PAPER_PDF && data->paper_pdf_path)
      {
	gint remembered = paper_pdf_memory_get (data, data->paper_pdf_path);

	if (remembered >= 0)
	  saved_page = remembered;
      }

    if (paper == SCREENATOR_PAPER_PDF)
      {
	doc = paper_document (data, SCREENATOR_PAPER_PDF);
	if (!doc || !doc->pdf_loaded)
	  {
	    if (data->paper_pdf_path && *data->paper_pdf_path &&
		g_file_test (data->paper_pdf_path, G_FILE_TEST_IS_REGULAR) &&
		paper_load_pdf_file (data, data->paper_pdf_path))
	      loaded_saved = TRUE;
	    else if (!screenator_paper_open_pdf (data))
	      return;
	  }
      }

    screenator_clear_screenshot_paper (data);

    doc = paper_ensure_document (data, paper);
    if (!doc)
      return;

    /* A PDF loaded from the saved path returns to the page that was open
     * when the application last exited. */
    if (paper == SCREENATOR_PAPER_PDF && loaded_saved && doc->pdf_loaded &&
	doc->pages && doc->pages->len > 0)
      {
	guint p = MIN ((guint) saved_page, doc->pages->len - 1);

	doc->page = p;
	data->paper_pdf_page = (gint) p;
	doc->pdf_scroll_y = 0;

	if (data->paper_continuous_scroll && doc->pdf_page_tops &&
	    doc->pdf_page_tops->len > p)
	  doc->pdf_scroll_y = g_array_index (doc->pdf_page_tops, gint, p);
      }
  }

  if (paper != SCREENATOR_PAPER_PDF && doc->pages->len == 0)
    {
      ScreenatorPaperPage fresh = paper_new_page (data, paper);

      g_array_append_val (doc->pages, fresh);
      doc->page = 0;
    }

  if (doc->pages->len == 0)
    return;

  page = paper_current_page (doc);
  if (paper == SCREENATOR_PAPER_PDF)
    paper_ensure_background (data, doc, doc->page, page);

  /* Keep a copy of the Screen Annotation drawings aside and load the current
   * paper page into the one backbuffer the tools draw on. */
  data->screen_annotations =
    cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
				data->width, data->height);
  copy_surface (data->screen_annotations, data->backbuffer);
  data->screen_annotations_painted = data->painted;

  data->paper = paper;
  data->paper_selected = paper;
  data->hide_drawings = FALSE;

  if (data->paper_continuous_scroll)
    {
      paper_build_continuous_layout (data, doc);

      if (doc->pdf_stack_height > 0)
	{
	  gint top = (doc->pdf_page_tops &&
		      doc->page < doc->pdf_page_tops->len)
	    ? g_array_index (doc->pdf_page_tops, gint, doc->page) : 0;

	  doc->pdf_scroll_y = CLAMP (top, 0,
				     MAX (0, doc->pdf_stack_height -
					  (gint) data->height));
	  paper_pdf_prepare_active (data, doc);
	}
      else
	paper_load_page (data, page);
    }
  else
    paper_load_page (data, page);

  screenator_use_paper_window (data, TRUE);
  paper_top_bar_restore (data);
  paper_top_bar_update_active_tool (data);
  reset_undo_history (data);

  /* A tool selected before leaving Paper mode is activated again when the
   * paper is reopened. */
  if (data->forced_context && !screenator_any_grabbed (data))
    acquire_grab (data, NULL);

  update_paper_shortcuts (data, TRUE);
  paper_repaint (data);

  if (data->hidden)
    show_window (data);
  else
    {
      if (gtk_widget_get_window (data->win))
	gdk_window_raise (gtk_widget_get_window (data->win));
      raise_icon_window (data);
    }

  /* Paper Drawing is the active window: it keeps the keyboard, so typing
   * does not reach the application below. */
  paper_take_focus (data);

  /* Fullscreen mapping is asynchronous on some window managers.  Present and
   * raise again after the mode switch so F3 does not leave the paper behind
   * other applications or minimized in the taskbar. */
  if (data->paper_win)
    {
      gtk_window_deiconify (GTK_WINDOW (data->paper_win));
      gtk_window_present_with_time (GTK_WINDOW (data->paper_win),
				    gtk_get_current_event_time ());
      if (gtk_widget_get_window (data->paper_win))
	gdk_window_raise (gtk_widget_get_window (data->paper_win));
    }

  paper_refresh_active_cursor (data);

  write_keyfile (data);
  schedule_tray_menu_refresh (data);
}

void
screenator_exit_paper_drawing (ScreenatorData *data)
{
  if (data->paper == SCREENATOR_PAPER_NONE)
    return;

  paper_pdf_remember_current (data);
  screenator_timer_cancel (data);
  paper_commit_pending (data);
  paper_store_active_page (data);

  if (data->screen_annotations)
    {
      copy_surface (data->backbuffer, data->screen_annotations);
      cairo_surface_destroy (data->screen_annotations);
      data->screen_annotations = NULL;
    }

  data->painted = data->screen_annotations_painted;
  data->paper = SCREENATOR_PAPER_NONE;
  paper_background_cache_free (data);
  data->hide_drawings = FALSE;
  data->paper_hand_tool = FALSE;
  paper_top_bar_update_active_tool (data);
  paper_finish_temporary_eraser (data);
  screenator_paper_pan_end (data);
  reset_undo_history (data);
  screenator_use_paper_window (data, FALSE);

  /* The toolbar still shows the Paper tool after returning to Screen mode.
   * Activate that tool again so the icon and the actual input agree. */
  if (data->forced_context && !screenator_any_grabbed (data))
    acquire_grab (data, NULL);

  update_paper_shortcuts (data, FALSE);
  paper_restore_focus (data);
  paper_refresh_active_cursor (data);
  paper_repaint (data);

  if (!data->painted && !screenator_any_grabbed (data))
    hide_window (data);

  write_keyfile (data);
  schedule_tray_menu_refresh (data);
}

static gint
paper_pdf_memory_get (ScreenatorData *data, const gchar *path)
{
  gchar *canonical;
  gpointer value;

  if (!data || !path || !*path)
    return -1;

  if (!data->pdf_page_memory)
    data->pdf_page_memory = g_hash_table_new_full (g_str_hash, g_str_equal,
						   g_free, NULL);

  canonical = g_canonicalize_filename (path, NULL);
  if (!canonical)
    return -1;

  value = g_hash_table_lookup (data->pdf_page_memory, canonical);
  g_free (canonical);

  return value ? GPOINTER_TO_INT (value) - 1 : -1;
}

static void
paper_pdf_memory_set (ScreenatorData *data, const gchar *path, gint page)
{
  gchar *canonical;

  if (!data || !path || !*path)
    return;

  if (!data->pdf_page_memory)
    data->pdf_page_memory = g_hash_table_new_full (g_str_hash, g_str_equal,
						   g_free, NULL);

  canonical = g_canonicalize_filename (path, NULL);
  if (!canonical)
    return;

  g_hash_table_insert (data->pdf_page_memory, canonical,
		       GINT_TO_POINTER (MAX (0, page) + 1));
}

/* Remember the page of the PDF that is currently shown. */
static void
paper_pdf_remember_current (ScreenatorData *data)
{
  ScreenatorPaperDocument *doc;

  if (!data || data->paper != SCREENATOR_PAPER_PDF)
    return;

  doc = data->paper_docs[SCREENATOR_PAPER_PDF];
  if (!doc || !doc->pdf_loaded || !doc->pdf_path)
    return;

  paper_pdf_memory_set (data, doc->pdf_path, (gint) doc->page);
  data->paper_pdf_page = (gint) doc->page;

  g_free (data->paper_pdf_path);
  data->paper_pdf_path = g_strdup (doc->pdf_path);
}

/* Load a PDF and make it the selected paper. */
static gboolean
paper_load_pdf_file (ScreenatorData *data, const gchar *path)
{
#ifdef HAVE_POPPLER_GLIB
  PopplerDocument *pdf;
  ScreenatorPaperDocument *doc;
  GError *error = NULL;
  gchar *uri;
  gint pages, i;

  uri = g_filename_to_uri (path, NULL, NULL);
  if (!uri)
    return FALSE;

  pdf = poppler_document_new_from_file (uri, NULL, &error);
  g_free (uri);

  if (!pdf)
    {
      GtkWidget *dialog;

      dialog = gtk_message_dialog_new (NULL,
				       GTK_DIALOG_MODAL,
				       GTK_MESSAGE_ERROR,
				       GTK_BUTTONS_OK,
				       "Could not open the PDF:\n%s",
				       error ? error->message : path);
      gtk_window_set_title (GTK_WINDOW (dialog), "Open PDF");
      show_dialog_above_drawings (data, dialog);
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      g_clear_error (&error);
      return FALSE;
    }

  pages = poppler_document_get_n_pages (pdf);
  if (pages <= 0)
    {
      g_object_unref (pdf);
      return FALSE;
    }

  paper_pdf_remember_current (data);

  doc = paper_ensure_document (data, SCREENATOR_PAPER_PDF);
  g_atomic_int_inc (&data->pdf_render_serial);
  data->paper_page_dirty = FALSE;
  screenator_page_history_clear (data);
  paper_document_clear_pages (doc);

  if (doc->pdf_document)
    g_object_unref ((PopplerDocument *) doc->pdf_document);

  g_free (doc->pdf_path);
  doc->pdf_path = g_strdup (path);
  doc->pdf_document = pdf;
  doc->pdf_page_count = pages;
  doc->pdf_loaded = TRUE;
  {
    gint remembered = paper_pdf_memory_get (data, path);

    doc->page = remembered >= 0 && remembered < pages
      ? (guint) remembered : 0;
  }

  g_free (data->paper_pdf_path);
  data->paper_pdf_path = g_strdup (path);
  data->paper_pdf_page = (gint) doc->page;

  for (i = 0; i < pages; i++)
    {
      ScreenatorPaperPage fresh = paper_new_page (data, SCREENATOR_PAPER_PDF);

      g_array_append_val (doc->pages, fresh);
    }

  paper_pdf_build_layout (data, doc);
  doc->pdf_scroll_y = 0;

  data->paper_selected = SCREENATOR_PAPER_PDF;

  if (data->paper == SCREENATOR_PAPER_PDF)
    {
      ScreenatorPaperPage *page = paper_current_page (doc);

      screenator_paper_pan_end (data);
      if (data->paper_continuous_scroll && doc->pdf_stack_height > 0)
	{
	  gint max_scroll = MAX (0, doc->pdf_stack_height -
				paper_view_height (data));

	  if (doc->pdf_page_tops && doc->page < doc->pdf_page_tops->len)
	    doc->pdf_scroll_y = CLAMP (
	      g_array_index (doc->pdf_page_tops, gint, doc->page),
	      0, max_scroll);
	  paper_pdf_prepare_active (data, doc);
	}
      else
	{
	  paper_ensure_background (data, doc, doc->page, page);
	  paper_load_page (data, page);
	}
      reset_undo_history (data);
      paper_repaint (data);
    }
  else if (data->paper != SCREENATOR_PAPER_NONE)
    paper_switch_to (data, SCREENATOR_PAPER_PDF);

  paper_refresh_active_cursor (data);

  write_keyfile (data);
  schedule_tray_menu_refresh (data);
  return TRUE;
#else
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new (NULL,
				   GTK_DIALOG_MODAL,
				   GTK_MESSAGE_ERROR,
				   GTK_BUTTONS_OK,
				   "Screenator was built without PDF support.");
  gtk_window_set_title (GTK_WINDOW (dialog), "Open PDF");
  show_dialog_above_drawings (data, dialog);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  return FALSE;
#endif
}

static gboolean
clear_paper_pdf_chooser_busy (gpointer user_data)
{
  ((ScreenatorData *) user_data)->paper_pdf_chooser_busy = FALSE;
  return G_SOURCE_REMOVE;
}

/* Ask for a PDF with the desktop's own file chooser. */
gboolean
screenator_paper_open_pdf (ScreenatorData *data)
{
  GtkFileChooserNative *dialog;
  GtkFileFilter *filter;
  ScreenatorPaperDocument *doc =
    paper_document (data, SCREENATOR_PAPER_PDF);
  gchar *folder;
  gchar *path = NULL;
  gboolean loaded = FALSE;
  gint response;

  if (data->paper_pdf_chooser_busy)
    return FALSE;

  data->paper_pdf_chooser_busy = TRUE;

  dialog = gtk_file_chooser_native_new ("Open PDF Paper", NULL,
					GTK_FILE_CHOOSER_ACTION_OPEN,
					"Open", "Cancel");

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "PDF files");
  gtk_file_filter_add_pattern (filter, "*.pdf");
  gtk_file_filter_add_pattern (filter, "*.PDF");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  if (doc && doc->pdf_path)
    folder = g_path_get_dirname (doc->pdf_path);
  else
    {
      const gchar *pictures =
	g_get_user_special_dir (G_USER_DIRECTORY_PICTURES);

      folder = g_strdup (pictures && *pictures
			 ? pictures : g_get_home_dir ());
    }
  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), folder);
  g_free (folder);

  response = paper_run_native_dialog (data, dialog);

  if (response == GTK_RESPONSE_ACCEPT)
    path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

  g_object_unref (dialog);

  if (path && *path)
    loaded = paper_load_pdf_file (data, path);
  g_free (path);
  g_idle_add (clear_paper_pdf_chooser_busy, data);
  return loaded;
}

/* Open a PDF file and switch to Paper mode with it. */
gboolean
screenator_open_pdf_path (ScreenatorData *data, const gchar *path)
{
  if (!path || !*path)
    return FALSE;

  if (!paper_load_pdf_file (data, path))
    return FALSE;

  data->paper_selected = SCREENATOR_PAPER_PDF;
  screenator_enter_paper_drawing (data, SCREENATOR_PAPER_PDF);
  /* The PDF may have replaced a page whose background was already cached. */
  paper_background_invalidate (data);
  paper_repaint (data);
  paper_top_bar_update_active_tool (data);
  write_keyfile (data);
  schedule_tray_menu_refresh (data);
  return TRUE;
}

/* Choose a paper type.  Every document keeps its own pages, drawings and
 * PDF file while another type is shown. */
void
screenator_paper_select (ScreenatorData *data, ScreenatorPaperType paper)
{
  if (paper <= SCREENATOR_PAPER_NONE || paper > SCREENATOR_PAPER_PDF)
    return;

  if (paper != data->paper_selected)
    {
      if (data->paper == data->paper_selected &&
	  data->paper != SCREENATOR_PAPER_NONE)
	{
	  paper_commit_pending (data);
	  paper_store_active_page (data);
	}
    }

  if (paper == SCREENATOR_PAPER_PDF)
    {
      ScreenatorPaperDocument *pdf =
	paper_document (data, SCREENATOR_PAPER_PDF);

      /* Reuse the already opened PDF.  Ask for a file only when no PDF has
       * been loaded yet. */
      if (!pdf || !pdf->pdf_loaded)
	{
	  if (!screenator_paper_open_pdf (data))
	    return;
	}
    }

  data->paper_selected = paper;

  /* Selecting a paper type, or opening a PDF, always shows that paper. */
  screenator_enter_paper_drawing (data, paper);

  paper_top_bar_update_active_tool (data);
  write_keyfile (data);
  schedule_tray_menu_refresh (data);
}

/* Toggle between Screen and Paper modes. */
void
screenator_toggle_paper_drawing (ScreenatorData *data)
{
  ScreenatorPaperType type;

  if (data->paper != SCREENATOR_PAPER_NONE)
    {
      screenator_exit_paper_drawing (data);
      return;
    }

  type = data->paper_selected;
  if (type <= SCREENATOR_PAPER_NONE || type > SCREENATOR_PAPER_PDF)
    type = SCREENATOR_PAPER_PLAIN;

  screenator_enter_paper_drawing (data, type);
}

/* Append an action to the given stack.  The action owns its page payload. */
static void
paper_page_action_push (ScreenatorData *data, gboolean redo,
			ScreenatorPageAction action)
{
  GArray **stack = redo ? &data->page_redo_stack
			: &data->page_undo_stack;

  if (!*stack)
    *stack = g_array_new (FALSE, FALSE, sizeof (ScreenatorPageAction));

  g_array_append_val (*stack, action);
}

/* Record a new user page operation and discard the old redo branch. */
static void
paper_page_action_record (ScreenatorData *data, ScreenatorPageAction action)
{
  paper_page_action_stack_clear (data->page_redo_stack);
  paper_page_action_push (data, FALSE, action);
}

static gboolean
paper_page_action_pop (GArray *stack, ScreenatorPageAction *action)
{
  if (!stack || stack->len == 0)
    return FALSE;

  *action = g_array_index (stack, ScreenatorPageAction, stack->len - 1);
  g_array_set_size (stack, stack->len - 1);
  return TRUE;
}

/* Rebuild the view after a page was inserted or removed. */
static void
paper_page_action_refresh (ScreenatorData *data,
			   ScreenatorPaperDocument *doc, guint index)
{
  ScreenatorPaperPage page;

  if (!doc || !doc->pages)
    return;

  if (doc->pages->len == 0)
    {
      page = paper_new_page (data, doc->type);
      g_array_append_val (doc->pages, page);
      index = 0;
    }

  doc->page = MIN (index, doc->pages->len - 1);
  doc->modified = TRUE;
  data->paper_page_dirty = FALSE;
  data->paper_pending_ink_valid = FALSE;
  data->paper_pdf_page = 0;

  if (paper_continuous (data))
    {
      paper_build_continuous_layout (data, doc);
      doc->pdf_scroll_y = CLAMP (
	(doc->pdf_page_tops && doc->page < doc->pdf_page_tops->len)
	? g_array_index (doc->pdf_page_tops, gint, doc->page) : 0,
	0, MAX (0, doc->pdf_stack_height - paper_view_height (data)));
      paper_pdf_prepare_active (data, doc);
    }
  else
    paper_load_page (data, paper_current_page (doc));

  reset_undo_history (data);
  paper_top_bar_update_active_tool (data);
  paper_repaint (data);
  schedule_tray_menu_refresh (data);
}

/* Revert (undo) or replay (redo) the most recent page insertion/deletion. */
static void
paper_page_action_apply (ScreenatorData *data, gboolean undo)
{
  ScreenatorPaperDocument *doc = paper_active_document (data);
  GArray *from = undo ? data->page_undo_stack : data->page_redo_stack;
  ScreenatorPageAction action;
  ScreenatorPaperPage page;
  guint index;

  if (!doc || !doc->pages || doc->type == SCREENATOR_PAPER_PDF ||
      !paper_page_action_pop (from, &action))
    return;

  if (action.document != doc->type)
    {
      paper_page_clear (&action.page);
      return;
    }

  paper_commit_pending (data);
  paper_store_active_page (data);

  index = MIN (action.index, doc->pages->len);

  if (undo)
    {
      if (action.type == SCREENATOR_PAGE_ACTION_INSERT)
	{
	  if (index >= doc->pages->len)
	    {
	      paper_page_clear (&action.page);
	      return;
	    }

	  /* The inserted page is empty unless something was drawn on it
	   * after insertion.  Drawing undo handles those strokes first. */
	  page = g_array_index (doc->pages, ScreenatorPaperPage, index);
	  g_array_remove_index (doc->pages, index);
	  paper_page_clear (&page);
	}
      else
	{
	  /* A deleted page keeps its annotations in the action payload. */
	  g_array_insert_val (doc->pages, index, action.page);
	}

      memset (&action.page, 0, sizeof (action.page));
      paper_page_action_push (data, TRUE, action);
    }
  else
    {
      if (action.type == SCREENATOR_PAGE_ACTION_INSERT)
	{
	  page = paper_new_page (data, doc->type);
	  g_array_insert_val (doc->pages, index, page);
	  memset (&action.page, 0, sizeof (action.page));
	  paper_page_action_push (data, FALSE, action);
	}
      else
	{
	  if (index >= doc->pages->len)
	    {
	      paper_page_clear (&action.page);
	      return;
	    }

	  page = g_array_index (doc->pages, ScreenatorPaperPage, index);
	  g_array_remove_index (doc->pages, index);
	  action.page = page;
	  paper_page_action_push (data, FALSE, action);
	}
    }

  paper_page_action_refresh (data, doc, index);
}

void
screenator_undo (ScreenatorData *data)
{
  if (!data)
    return;

  if (data->undo_depth > 0)
    {
      undo_drawing (data);
      return;
    }

  paper_page_action_apply (data, TRUE);
}

void
screenator_redo (ScreenatorData *data)
{
  if (!data)
    return;

  if (data->redo_depth > 0)
    {
      redo_drawing (data);
      return;
    }

  paper_page_action_apply (data, FALSE);
}

/* Insert a new page before or after the current one. */
void
screenator_paper_insert_page (ScreenatorData *data, gboolean before)
{
  ScreenatorPaperDocument *doc = paper_active_document (data);
  ScreenatorPageAction action;
  ScreenatorPaperPage page;
  guint index;

  if (!doc || doc->type == SCREENATOR_PAPER_PDF)
    return;

  paper_commit_pending (data);
  paper_store_active_page (data);

  page = paper_new_page (data, doc->type);
  index = doc->page + (before ? 0 : 1);
  g_array_insert_val (doc->pages, index, page);

  memset (&action, 0, sizeof (action));
  action.type = SCREENATOR_PAGE_ACTION_INSERT;
  action.document = doc->type;
  action.index = index;
  paper_page_action_record (data, action);

  doc->modified = TRUE;
  doc->page = index;

  if (paper_continuous (data))
    {
      paper_build_continuous_layout (data, doc);
      doc->pdf_scroll_y = CLAMP (
	(doc->pdf_page_tops && index < doc->pdf_page_tops->len)
	? g_array_index (doc->pdf_page_tops, gint, index) : 0,
	0, MAX (0, doc->pdf_stack_height - paper_view_height (data)));
      paper_pdf_prepare_active (data, doc);
    }
  else
    paper_load_page (data, paper_current_page (doc));

  reset_undo_history (data);
  paper_top_bar_update_active_tool (data);
  paper_repaint (data);
  schedule_tray_menu_refresh (data);
}

/* Delete the current Plain/Ruled/Graph page.  PDF pages are left untouched. */
void
screenator_paper_delete_page (ScreenatorData *data)
{
  ScreenatorPaperDocument *doc = paper_active_document (data);
  ScreenatorPageAction action;
  ScreenatorPaperPage page;
  guint index;

  if (!data || !doc || doc->type == SCREENATOR_PAPER_PDF ||
      !doc->pages || doc->pages->len <= 1)
    return;

  paper_commit_pending (data);
  paper_store_active_page (data);

  index = doc->page;
  if (index >= doc->pages->len)
    index = doc->pages->len - 1;

  page = g_array_index (doc->pages, ScreenatorPaperPage, index);
  g_array_remove_index (doc->pages, index);

  memset (&action, 0, sizeof (action));
  action.type = SCREENATOR_PAGE_ACTION_DELETE;
  action.document = doc->type;
  action.index = index;
  action.page = page;
  paper_page_action_record (data, action);

  if (index >= doc->pages->len)
    index = doc->pages->len - 1;

  doc->page = index;
  doc->modified = TRUE;
  data->paper_page_dirty = FALSE;
  data->paper_pending_ink_valid = FALSE;
  data->paper_pdf_page = 0;

  if (paper_continuous (data))
    {
      paper_build_continuous_layout (data, doc);
      doc->pdf_scroll_y = CLAMP (
	(doc->pdf_page_tops && index < doc->pdf_page_tops->len)
	? g_array_index (doc->pdf_page_tops, gint, index) : 0,
	0, MAX (0, doc->pdf_stack_height - paper_view_height (data)));
      paper_pdf_prepare_active (data, doc);
    }
  else
    paper_load_page (data, paper_current_page (doc));

  reset_undo_history (data);
  paper_top_bar_update_active_tool (data);
  paper_repaint (data);
  schedule_tray_menu_refresh (data);
}

/* Show a page at a given viewport position.  The current page is stored first
 * and the new page is loaded from its full-page annotation layer. */
static void
paper_show_page (ScreenatorData *data, ScreenatorPaperDocument *doc,
		 guint index, gint view_x, gint view_y)
{
  ScreenatorPaperPage *page;
  gint max_x, max_y;

  if (!doc || index >= doc->pages->len)
    return;

  paper_store_active_page (data);

  doc->page = index;
  page = paper_current_page (doc);
  if (!page)
    return;

  paper_top_bar_update_active_tool (data);
  paper_page_view_limits (data, doc, index, page, &max_x, &max_y);
  page->view_x = CLAMP (view_x, 0, max_x);
  page->view_y = CLAMP (view_y, 0, max_y);

  paper_load_page (data, page);
  reset_undo_history (data);
  paper_repaint (data);
  schedule_tray_menu_refresh (data);
}

void
screenator_paper_switch_page (ScreenatorData *data, gint delta)
{
  ScreenatorPaperDocument *doc = paper_active_document (data);
  guint index, count;

  if (!doc)
    return;

  count = doc->pages->len;
  if (count < 2)
    return;

  paper_commit_pending (data);
  index = (doc->page + count + delta) % count;

  if (paper_continuous (data))
    paper_pdf_show_page (data, doc, index);
  else
    paper_show_page (data, doc, index, 0, 0);
}

/* Jump directly to a page.  Home and End use this to reach the first and last
 * page; Page Up and Page Down use screenator_paper_switch_page(). */
void
screenator_paper_goto_page (ScreenatorData *data, gint index)
{
  ScreenatorPaperDocument *doc = paper_active_document (data);
  guint count;

  if (!doc || !doc->pages || doc->pages->len == 0)
    return;

  count = doc->pages->len;
  if (index < 0)
    index = 0;
  if (index >= (gint) count)
    index = (gint) count - 1;

  paper_commit_pending (data);

  if (paper_continuous (data))
    paper_pdf_show_page (data, doc, (guint) index);
  else
    paper_show_page (data, doc, (guint) index, 0, 0);
}

/* Ask for a page number and jump there.  Used by the Paper menu and the tool
 * bar for PDF and multi-page Plain/Ruled/Graph documents. */
static void
paper_show_goto_page_dialog (ScreenatorData *data)
{
  ScreenatorPaperDocument *doc = paper_active_document (data);
  GtkWidget *dialog, *content, *box, *label, *spin;
  gint response;
  guint count;

  if (!doc || !doc->pages)
    return;

  count = doc->pages->len;
  if (count < 2)
    return;

  dialog = gtk_dialog_new_with_buttons (
    "Go to Page", NULL,
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    "Cancel", GTK_RESPONSE_CANCEL,
    "Go", GTK_RESPONSE_ACCEPT, NULL);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);

  content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width (GTK_CONTAINER (box), 16);
  gtk_container_add (GTK_CONTAINER (content), box);

  label = gtk_label_new (NULL);
  {
    gchar *text = g_strdup_printf ("Go to page (1-%u):", count);

    gtk_label_set_text (GTK_LABEL (label), text);
    g_free (text);
  }
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  spin = gtk_spin_button_new_with_range (1.0, (gdouble) count, 1.0);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin),
			     (gdouble) MIN (doc->page + 1, count));
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spin), TRUE);
  gtk_entry_set_activates_default (GTK_ENTRY (spin), TRUE);
  gtk_box_pack_start (GTK_BOX (box), spin, FALSE, FALSE, 0);

  gtk_widget_show_all (dialog);
  show_dialog_above_drawings (data, dialog);
  gtk_widget_grab_focus (spin);
  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if (response == GTK_RESPONSE_ACCEPT)
    screenator_paper_goto_page (
      data, gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (spin)) - 1);

  gtk_widget_destroy (dialog);
}

/* Scroll the active page.  A PDF page in Continuous scrolling moves smoothly
 * through the document.  Every other page scrolls within the page and shows
 * the next or previous page at its top or bottom edge, regardless of the
 * Continuous scrolling option. */
void
screenator_paper_scroll (ScreenatorData *data, gint direction)
{
  ScreenatorPaperDocument *doc = paper_active_document (data);
  ScreenatorPaperPage *page = paper_current_page (doc);
  gint max_x, max_y, step;

  if (!doc || !page)
    return;

  paper_commit_pending (data);

  if (paper_continuous (data))
    {
      step = MAX (48, paper_view_height (data) / 6);
      paper_pdf_set_scroll (data, doc->pdf_scroll_y +
			    direction * step);
      return;
    }

  paper_page_view_limits (data, doc, doc->page, page, &max_x, &max_y);
  step = MAX (48, (gint) data->height / 6);

  if (direction > 0)
    {
      if (page->view_y < max_y)
	paper_set_view (data, page->view_x,
			MIN (max_y, page->view_y + step));
      else if (doc->page + 1 < doc->pages->len)
	paper_show_page (data, doc, doc->page + 1, 0, 0);
      else
	paper_set_view (data, page->view_x, max_y);
    }
  else
    {
      if (page->view_y > 0)
	paper_set_view (data, page->view_x,
			MAX (0, page->view_y - step));
      else if (doc->page > 0)
	{
	  ScreenatorPaperPage *previous =
	    &g_array_index (doc->pages, ScreenatorPaperPage, doc->page - 1);
	  gint previous_max_x, previous_max_y;

	  paper_page_view_limits (data, doc, doc->page - 1, previous,
				  &previous_max_x, &previous_max_y);
	  paper_show_page (data, doc, doc->page - 1, 0, previous_max_y);
	}
      else
	paper_set_view (data, page->view_x, 0);
    }
}


static void
paper_show_no_changes (ScreenatorData *data)
{
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
				   GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
				   "This paper has no changes to save.");
  gtk_window_set_title (GTK_WINDOW (dialog), "Paper saved");
  show_dialog_above_drawings (data, dialog);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

/* Save only the paper that is shown now. */
gboolean
screenator_paper_save (ScreenatorData *data)
{
  ScreenatorPaperDocument *doc;

  if (data->paper_saving)
    return FALSE;

  doc = paper_active_document (data);
  GList *paths = NULL;
  gchar *path = NULL;

  if (!doc)
    {
      paper_show_no_changes (data);
      return FALSE;
    }

  if (data->paper == doc->type)
    {
      paper_commit_pending (data);
      paper_store_active_page (data);
    }

  if (!doc->modified)
    {
      paper_show_no_changes (data);
      return TRUE;
    }

  if (!paper_autosave_document (data, doc, &path))
    {
      paper_show_save_failure (data);
      return FALSE;
    }

  paths = g_list_append (paths, path);
  paper_show_saved_paths (data, paths);
  g_list_free_full (paths, g_free);
  return TRUE;
}

/* Quit, offering once to save every paper that has changed since its last
 * save.  Files are written to the Documents folder automatically. */
gboolean
screenator_request_quit (ScreenatorData *data)
{
  GList *changed = NULL;
  GList *saved = NULL;
  ScreenatorPaperType type;
  gboolean failed = FALSE;

  if (data->paper_saving)
    return FALSE;

  if (data->paper != SCREENATOR_PAPER_NONE)
    {
      paper_commit_pending (data);
      paper_store_active_page (data);
    }

  for (type = SCREENATOR_PAPER_PLAIN;
       type <= SCREENATOR_PAPER_PDF; type++)
    {
      ScreenatorPaperDocument *doc = data->paper_docs[type];

      if (doc && doc->modified && doc->pages && doc->pages->len > 0)
	changed = g_list_append (changed, doc);
    }

  if (changed)
    {
      GtkWidget *dialog;
      gint response;

      dialog = gtk_message_dialog_new (NULL,
				       GTK_DIALOG_MODAL,
				       GTK_MESSAGE_QUESTION,
				       GTK_BUTTONS_NONE,
				       "Some changed papers have not been saved.\n"
				       "Save them in the Documents folder before "
				       "quitting?");
      gtk_window_set_title (GTK_WINDOW (dialog), "Quit Screenator");
      gtk_dialog_add_button (GTK_DIALOG (dialog), "Cancel",
			     GTK_RESPONSE_CANCEL);
      gtk_dialog_add_button (GTK_DIALOG (dialog), "Ignore",
			     GTK_RESPONSE_NO);
      gtk_dialog_add_button (GTK_DIALOG (dialog), "Save",
			     GTK_RESPONSE_YES);
      gtk_dialog_set_default_response (GTK_DIALOG (dialog),
				       GTK_RESPONSE_CANCEL);

      show_dialog_above_drawings (data, dialog);
      response = gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);

      if (response == GTK_RESPONSE_CANCEL)
	{
	  g_list_free (changed);
	  return FALSE;
	}

      if (response == GTK_RESPONSE_YES)
	{
	  GList *l;

	  for (l = changed; l; l = l->next)
	    {
	      gchar *path = NULL;

	      if (!paper_autosave_document (data, l->data, &path))
		{
		  failed = TRUE;
		  break;
		}
	      saved = g_list_append (saved, path);
	    }

	  if (failed)
	    {
	      paper_show_save_failure (data);
	      g_list_free_full (saved, g_free);
	      g_list_free (changed);
	      return FALSE;
	    }

	  paper_show_saved_paths (data, saved);
	  g_list_free_full (saved, g_free);
	}
    }

  g_list_free (changed);
  gtk_main_quit ();
  return TRUE;
}

/* Scale the existing Plain, Ruled and Graph pages to the paper size chosen in
 * the settings dialog.  PDF pages keep their own size. */
static void
paper_apply_size_settings (ScreenatorData *data)
{
  ScreenatorPaperType type;
  gint new_width, new_height;
  gdouble new_pdf_width, new_pdf_height;
  gboolean active_non_pdf =
    data->paper != SCREENATOR_PAPER_NONE &&
    data->paper != SCREENATOR_PAPER_PDF;

  paper_size_dimensions (data, &new_width, &new_height,
			 &new_pdf_width, &new_pdf_height);

  screenator_page_history_clear (data);

  if (active_non_pdf)
    paper_store_active_page (data);

  for (type = SCREENATOR_PAPER_PLAIN;
       type <= SCREENATOR_PAPER_GRAPH; type++)
    {
      ScreenatorPaperDocument *doc = data->paper_docs[type];
      guint i;

      if (!doc || !doc->pages)
	continue;

      for (i = 0; i < doc->pages->len; i++)
	{
	  ScreenatorPaperPage *page =
	    &g_array_index (doc->pages, ScreenatorPaperPage, i);
	  gint old_width = page->canvas_width;
	  gint old_height = page->canvas_height;
	  gint max_x, max_y;

	  if (page->style == SCREENATOR_PAPER_PDF)
	    continue;

	  if ((!old_width || !old_height) && page->annotations)
	    {
	      old_width = cairo_image_surface_get_width (page->annotations);
	      old_height = cairo_image_surface_get_height (page->annotations);
	    }

	  if (old_width == new_width && old_height == new_height)
	    {
	      page->pdf_width = new_pdf_width;
	      page->pdf_height = new_pdf_height;
	      continue;
	    }

	  paper_scale_vector_surface (page, new_width, new_height, TRUE);
	  if (page->annotations && old_width > 0 && old_height > 0)
	    {
	      cairo_surface_t *scaled =
		resize_surface_contain (page->annotations, new_width, new_height);

	      cairo_surface_destroy (page->annotations);
	      page->annotations = scaled;
	    }

	  if (old_width > 0)
	    page->view_x = (gint) round ((gdouble) page->view_x *
					 new_width / old_width);
	  else
	    page->view_x = 0;
	  if (old_height > 0)
	    page->view_y = (gint) round ((gdouble) page->view_y *
					 new_height / old_height);
	  else
	    page->view_y = 0;

	  page->canvas_width = new_width;
	  page->canvas_height = new_height;
	  page->pdf_width = new_pdf_width;
	  page->pdf_height = new_pdf_height;

	  max_x = MAX (0, new_width - (gint) data->width);
	  max_y = MAX (0, new_height -
		       (paper_continuous (data) ? paper_view_height (data)
			: (gint) data->height));
	  page->view_x = CLAMP (page->view_x, 0, max_x);
	  page->view_y = CLAMP (page->view_y, 0, max_y);
	}
      if (doc->pages->len > 0)
	doc->modified = TRUE;
    }

  if (active_non_pdf)
    {
      ScreenatorPaperDocument *doc = paper_active_document (data);
      ScreenatorPaperPage *page = paper_current_page (doc);

      if (paper_continuous (data))
	{
	  paper_build_continuous_layout (data, doc);
	  doc->pdf_scroll_y = CLAMP (
	    (doc->pdf_page_tops && doc->page < doc->pdf_page_tops->len)
	    ? g_array_index (doc->pdf_page_tops, gint, doc->page) : 0,
	    0, MAX (0, doc->pdf_stack_height - paper_view_height (data)));
	  paper_pdf_prepare_active (data, doc);
	}
      else
	paper_load_page (data, page);

      reset_undo_history (data);
      paper_repaint (data);
    }
}

typedef struct
{
  GtkWidget *custom;
  GtkWidget *width;
  GtkWidget *height;
} PaperSettingsWidgets;

static void
paper_settings_custom_toggled (GtkToggleButton *button, gpointer user_data)
{
  PaperSettingsWidgets *widgets = user_data;
  gboolean custom = gtk_toggle_button_get_active (button);

  gtk_widget_set_sensitive (widgets->width, custom);
  gtk_widget_set_sensitive (widgets->height, custom);
}

typedef struct
{
  ScreenatorData *data;
  GtkWidget      *dialog;
  gboolean        reset_clicked;

  GtkWidget      *save_check;
  GtkWidget      *indicator_check;
  GtkWidget      *icon_size_combo;
  GtkWidget      *icon_position_radio[4];
  GtkWidget      *timer_delay_combo;

  GtkWidget      *continuous_check;
  GtkWidget      *toolbar_size_spin;
  GtkWidget      *toolbar_direction_combo;
  GtkWidget      *toolbar_items_view;
  GtkListStore   *toolbar_items_store;
  GtkWidget      *screen_radio;
  GtkWidget      *a4_portrait_radio;
  GtkWidget      *a4_landscape_radio;
  GtkWidget      *custom_radio;
  GtkWidget      *width_spin;
  GtkWidget      *height_spin;
  GtkWidget      *shortcut_entries[SCREENATOR_SHORTCUT_COUNT];

  PaperSettingsWidgets paper_widgets;
} SettingsDialog;

static void
settings_change_font (GtkButton *button, gpointer user_data)
{
  SettingsDialog *sd = user_data;

  (void) button;
  on_icon_change_font (NULL, sd->data);
}

static void
settings_reset (GtkButton *button, gpointer user_data)
{
  SettingsDialog *sd = user_data;

  (void) button;
  sd->reset_clicked = TRUE;
  on_icon_reset_settings (NULL, sd->data);
  gtk_dialog_response (GTK_DIALOG (sd->dialog), GTK_RESPONSE_CLOSE);
}

enum
{
  TOOLBAR_COL_VISIBLE,
  TOOLBAR_COL_ICON,
  TOOLBAR_COL_LABEL,
  TOOLBAR_COL_ID,
  TOOLBAR_COL_HANDLE,
  TOOLBAR_COL_COUNT
};

static GdkPixbuf *
toolbar_settings_icon (ScreenatorData *data, const ToolbarItemInfo *info)
{
  const gint size = 24;
  GdkPixbuf *pixbuf = NULL;

  if (g_strcmp0 (info->id, "eraser_size") == 0)
    pixbuf = paper_eraser_size_sample (MAX (1, (guint) data->eraser_width),
				       size);
  else if (g_strcmp0 (info->id, "line_color") == 0)
    pixbuf = paper_color_swatch (&data->pen_color, FALSE, size);
  else if (g_strcmp0 (info->id, "thickness") == 0)
    pixbuf = paper_thickness_sample (MAX (1, (guint) data->pen_width), size);
  else if (g_strcmp0 (info->id, "fill_color") == 0)
    pixbuf = paper_color_swatch (&data->shape_fill_color,
				 !data->shape_fill_enabled, size);
  else if (info->icon)
    pixbuf = load_menu_icon_at_size (info->icon, size);

  if (pixbuf &&
      (gdk_pixbuf_get_width (pixbuf) != size ||
       gdk_pixbuf_get_height (pixbuf) != size))
    {
      GdkPixbuf *scaled = gdk_pixbuf_scale_simple (pixbuf, size, size,
						    GDK_INTERP_BILINEAR);

      g_object_unref (pixbuf);
      pixbuf = scaled;
    }

  return pixbuf;
}

static void
toolbar_settings_toggle (GtkCellRendererToggle *renderer, gchar *path,
			 gpointer user_data)
{
  SettingsDialog *sd = user_data;
  GtkTreeIter iter;
  gboolean active;

  (void) renderer;

  if (!gtk_tree_model_get_iter_from_string (
	GTK_TREE_MODEL (sd->toolbar_items_store), &iter, path))
    return;

  gtk_tree_model_get (GTK_TREE_MODEL (sd->toolbar_items_store), &iter,
		      TOOLBAR_COL_VISIBLE, &active, -1);
  gtk_list_store_set (sd->toolbar_items_store, &iter,
		      TOOLBAR_COL_VISIBLE, !active, -1);
}

static void
toolbar_settings_add_item (SettingsDialog *sd, const ToolbarItemInfo *info,
			   gboolean visible)
{
  GtkTreeIter iter;

  GdkPixbuf *icon = toolbar_settings_icon (sd->data, info);

  gtk_list_store_append (sd->toolbar_items_store, &iter);
  gtk_list_store_set (sd->toolbar_items_store, &iter,
		      TOOLBAR_COL_VISIBLE, visible,
		      TOOLBAR_COL_ICON, icon,
		      TOOLBAR_COL_LABEL, info->label,
		      TOOLBAR_COL_ID, info->id,
		      TOOLBAR_COL_HANDLE, "☰",
		      -1);

  if (icon)
    g_object_unref (icon);
}

/* Build a complete toolbar item order.  A saved full order wins so hidden
 * rows keep their position; older configurations derive it from the visible
 * items.  Any known item that is missing is appended in default order, and
 * unknown ids are ignored. */
static gchar *
toolbar_full_order (ScreenatorData *data)
{
  GString *out = g_string_new (NULL);
  const gchar *source;
  gchar **tokens, **defaults;
  gboolean added[G_N_ELEMENTS (toolbar_item_info)] = { FALSE };
  guint i, t;

  if (data->paper_toolbar_order && *data->paper_toolbar_order)
    source = data->paper_toolbar_order;
  else if (data->paper_toolbar_items && *data->paper_toolbar_items)
    source = data->paper_toolbar_items;
  else
    source = PAPER_TOOLBAR_DEFAULT_ITEMS;

  tokens = g_strsplit (source, ",", -1);
  for (t = 0; tokens[t]; t++)
    {
      gchar *id = g_strstrip (tokens[t]);
      const gchar *canonical;

      if (!*id)
	continue;

      canonical = toolbar_item_id (id);
      for (i = 0; i < G_N_ELEMENTS (toolbar_item_info); i++)
	{
	  if (added[i] ||
	      g_strcmp0 (canonical, toolbar_item_info[i].id) != 0)
	    continue;

	  if (out->len > 0)
	    g_string_append_c (out, ',');
	  g_string_append (out, toolbar_item_info[i].id);
	  added[i] = TRUE;
	  break;
	}
    }
  g_strfreev (tokens);

  /* Append items that are missing from the saved order, keeping every
   * existing row where it is. */
  defaults = g_strsplit (PAPER_TOOLBAR_DEFAULT_ITEMS, ",", -1);
  for (t = 0; defaults[t]; t++)
    {
      gchar *id = g_strstrip (defaults[t]);
      const gchar *canonical;

      if (!*id)
	continue;

      canonical = toolbar_item_id (id);
      for (i = 0; i < G_N_ELEMENTS (toolbar_item_info); i++)
	{
	  if (added[i] ||
	      g_strcmp0 (canonical, toolbar_item_info[i].id) != 0)
	    continue;

	  if (out->len > 0)
	    g_string_append_c (out, ',');
	  g_string_append (out, toolbar_item_info[i].id);
	  added[i] = TRUE;
	  break;
	}
    }
  g_strfreev (defaults);

  for (i = 0; i < G_N_ELEMENTS (toolbar_item_info); i++)
    {
      if (added[i])
	continue;

      if (out->len > 0)
	g_string_append_c (out, ',');
      g_string_append (out, toolbar_item_info[i].id);
      added[i] = TRUE;
    }

  return g_string_free (out, FALSE);
}

static GtkWidget *
build_toolbar_settings_page (SettingsDialog *sd)
{
  ScreenatorData *data = sd->data;
  GtkWidget *box, *scroll, *view, *note;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  gchar **tokens, **visible_tokens;
  gboolean visible[G_N_ELEMENTS (toolbar_item_info)] = { FALSE };
  gchar *order;
  guint i, j;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width (GTK_CONTAINER (box), 12);

  {
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);

    gtk_box_pack_start (GTK_BOX (row),
			gtk_label_new ("Tool bar icon size (px)"),
			FALSE, FALSE, 0);
    sd->toolbar_size_spin = gtk_spin_button_new_with_range (16, 64, 1);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (sd->toolbar_size_spin),
			       data->paper_toolbar_icon_size);
    gtk_box_pack_start (GTK_BOX (row), sd->toolbar_size_spin,
			FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (box), row, FALSE, FALSE, 0);

    row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start (GTK_BOX (row),
			gtk_label_new ("Tool bar direction"),
			FALSE, FALSE, 0);
    sd->toolbar_direction_combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (
				      sd->toolbar_direction_combo), "Vertical");
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (
				      sd->toolbar_direction_combo), "Horizontal");
    gtk_combo_box_set_active (GTK_COMBO_BOX (sd->toolbar_direction_combo),
			      data->paper_toolbar_vertical ? 0 : 1);
    gtk_box_pack_start (GTK_BOX (row), sd->toolbar_direction_combo,
			FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (box), row, FALSE, FALSE, 0);
  }

  note = gtk_label_new ("Each row shows its tool bar icon. Checked items "
			"appear on the tool bar; drag the ☰ handle of a row to "
			"change its position. Hidden items keep their place in "
			"the list.");
  gtk_label_set_xalign (GTK_LABEL (note), 0.0);
  gtk_label_set_line_wrap (GTK_LABEL (note), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (note), 52);
  gtk_box_pack_start (GTK_BOX (box), note, FALSE, FALSE, 0);

  scroll = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (scroll, 420, 340);

  sd->toolbar_items_store = gtk_list_store_new (TOOLBAR_COL_COUNT,
						G_TYPE_BOOLEAN,
						GDK_TYPE_PIXBUF,
						G_TYPE_STRING,
						G_TYPE_STRING,
						G_TYPE_STRING);
  view = gtk_tree_view_new_with_model (
    GTK_TREE_MODEL (sd->toolbar_items_store));
  sd->toolbar_items_view = view;
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view), FALSE);
  gtk_tree_view_set_reorderable (GTK_TREE_VIEW (view), TRUE);
  gtk_tree_view_set_grid_lines (GTK_TREE_VIEW (view),
				GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);

  renderer = gtk_cell_renderer_text_new ();
  g_object_set (renderer, "text", "☰", "xpad", 6, NULL);
  column = gtk_tree_view_column_new_with_attributes (
    "", renderer, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (view), column);

  renderer = gtk_cell_renderer_pixbuf_new ();
  g_object_set (renderer, "xpad", 4, NULL);
  column = gtk_tree_view_column_new_with_attributes (
    "", renderer, "pixbuf", TOOLBAR_COL_ICON, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (view), column);

  renderer = gtk_cell_renderer_toggle_new ();
  gtk_cell_renderer_toggle_set_activatable (
    GTK_CELL_RENDERER_TOGGLE (renderer), TRUE);
  g_signal_connect (renderer, "toggled",
		    G_CALLBACK (toolbar_settings_toggle), sd);
  column = gtk_tree_view_column_new_with_attributes (
    "Shown", renderer, "active", TOOLBAR_COL_VISIBLE, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (view), column);

  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (
    "Item", renderer, "text", TOOLBAR_COL_LABEL, NULL);
  gtk_tree_view_column_set_expand (column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (view), column);

  gtk_container_add (GTK_CONTAINER (scroll), view);
  gtk_box_pack_start (GTK_BOX (box), scroll, TRUE, TRUE, 0);

  visible_tokens = g_strsplit (data->paper_toolbar_items
			       ? data->paper_toolbar_items
			       : PAPER_TOOLBAR_DEFAULT_ITEMS, ",", -1);
  for (i = 0; visible_tokens[i]; i++)
    {
      gchar *id = g_strstrip (visible_tokens[i]);
      const gchar *canonical;

      if (!*id)
	continue;

      canonical = toolbar_item_id (id);
      for (j = 0; j < G_N_ELEMENTS (toolbar_item_info); j++)
	if (g_strcmp0 (canonical, toolbar_item_info[j].id) == 0)
	  {
	    visible[j] = TRUE;
	    break;
	  }
    }
  g_strfreev (visible_tokens);

  order = toolbar_full_order (data);
  tokens = g_strsplit (order, ",", -1);
  for (i = 0; tokens[i]; i++)
    {
      gchar *id = g_strstrip (tokens[i]);
      const gchar *canonical;

      if (!*id)
	continue;

      canonical = toolbar_item_id (id);
      for (j = 0; j < G_N_ELEMENTS (toolbar_item_info); j++)
	if (g_strcmp0 (canonical, toolbar_item_info[j].id) == 0)
	  {
	    toolbar_settings_add_item (sd, &toolbar_item_info[j], visible[j]);
	    break;
	  }
    }
  g_strfreev (tokens);
  g_free (order);

  return box;
}

/* Collect both the visible items and the full settings-list order.  Keeping
 * the full order means a hidden item returns to its old place when it is
 * shown again. */
static void
settings_collect_toolbar_items (SettingsDialog *sd, gchar **visible_out,
				gchar **order_out)
{
  GString *visible = g_string_new (NULL);
  GString *order = g_string_new (NULL);
  GtkTreeModel *model = GTK_TREE_MODEL (sd->toolbar_items_store);
  GtkTreeIter iter;

  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      do
	{
	  gboolean shown;
	  gchar *id = NULL;
	  const gchar *canonical;

	  gtk_tree_model_get (model, &iter,
			      TOOLBAR_COL_VISIBLE, &shown,
			      TOOLBAR_COL_ID, &id,
			      -1);

	  if (id && *id)
	    {
	      canonical = toolbar_item_id (id);

	      if (order->len > 0)
		g_string_append_c (order, ',');
	      g_string_append (order, canonical);

	      if (shown)
		{
		  if (visible->len > 0)
		    g_string_append_c (visible, ',');
		  g_string_append (visible, canonical);
		}
	    }

	  g_free (id);
	}
      while (gtk_tree_model_iter_next (model, &iter));
    }

  *visible_out = g_string_free (visible, FALSE);
  *order_out = g_string_free (order, FALSE);
}

static GtkWidget *
build_general_settings_page (SettingsDialog *sd)
{
  ScreenatorData *data = sd->data;
  GtkWidget *box, *grid, *label, *combo, *button, *row;
  static const guint timer_delays[] = { 0, 1, 2, 3, 4, 5, 7, 9, 11 };
  guint i;
  gint timer_active = 0;
  guint best = G_MAXUINT;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
  gtk_container_set_border_width (GTK_CONTAINER (box), 12);

  grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
  gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);

  sd->save_check = gtk_check_button_new_with_label (
    "Save annotations on exit");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (sd->save_check),
				data->save_annotations_on_exit);
  gtk_grid_attach (GTK_GRID (grid), sd->save_check, 0, 0, 2, 1);

  sd->indicator_check = gtk_check_button_new_with_label (
    "Show settings indicator on Floating Icon");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (sd->indicator_check),
				data->show_settings_indicator);
  gtk_widget_set_sensitive (sd->indicator_check, data->show_floating_icon);
  gtk_grid_attach (GTK_GRID (grid), sd->indicator_check, 0, 1, 2, 1);

  label = gtk_label_new ("Timer delay");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_grid_attach (GTK_GRID (grid), label, 0, 2, 1, 1);

  combo = gtk_combo_box_text_new ();
  for (i = 0; i < G_N_ELEMENTS (timer_delays); i++)
    {
      gchar *text = g_strdup_printf ("%u s", timer_delays[i]);
      guint diff = ABS ((gint) data->tool_delay_seconds -
			(gint) timer_delays[i]);

      gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), text);
      g_free (text);

      if (diff < best)
	{
	  best = diff;
	  timer_active = (gint) i;
	}
    }
  gtk_combo_box_set_active (GTK_COMBO_BOX (combo), timer_active);
  sd->timer_delay_combo = combo;
  gtk_grid_attach (GTK_GRID (grid), combo, 1, 2, 1, 1);

  row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  button = gtk_button_new_with_label ("Change Font...");
  g_signal_connect (button, "clicked",
		    G_CALLBACK (settings_change_font), sd);
  gtk_box_pack_start (GTK_BOX (row), button, FALSE, FALSE, 0);


  gtk_box_pack_start (GTK_BOX (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL),
		      FALSE, FALSE, 0);

  button = gtk_button_new_with_label (
    "Reset app settings and drawings");
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  g_signal_connect (button, "clicked", G_CALLBACK (settings_reset), sd);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  label = gtk_label_new ("Settings are applied when the dialog is closed.");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 48);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  return box;
}

static GtkWidget *
build_paper_settings_page (SettingsDialog *sd)
{
  ScreenatorData *data = sd->data;
  GtkWidget *box, *frame, *grid, *note;
  GtkWidget *active_radio;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
  gtk_container_set_border_width (GTK_CONTAINER (box), 12);

  sd->continuous_check = gtk_check_button_new_with_label (
    "Continuous scrolling");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (sd->continuous_check),
				data->paper_continuous_scroll);
  gtk_box_pack_start (GTK_BOX (box), sd->continuous_check, FALSE, FALSE, 0);

  frame = gtk_frame_new ("Paper size");
  gtk_box_pack_start (GTK_BOX (box), frame, FALSE, FALSE, 0);

  grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
  gtk_container_set_border_width (GTK_CONTAINER (grid), 10);
  gtk_container_add (GTK_CONTAINER (frame), grid);

  sd->screen_radio = gtk_radio_button_new_with_label (NULL, "Screen");
  sd->a4_portrait_radio = gtk_radio_button_new_with_label_from_widget (
    GTK_RADIO_BUTTON (sd->screen_radio), "A4 portrait");
  sd->a4_landscape_radio = gtk_radio_button_new_with_label_from_widget (
    GTK_RADIO_BUTTON (sd->screen_radio), "A4 landscape");
  sd->custom_radio = gtk_radio_button_new_with_label_from_widget (
    GTK_RADIO_BUTTON (sd->screen_radio), "Custom");

  gtk_grid_attach (GTK_GRID (grid), sd->screen_radio, 0, 0, 2, 1);
  gtk_grid_attach (GTK_GRID (grid), sd->a4_portrait_radio, 0, 1, 2, 1);
  gtk_grid_attach (GTK_GRID (grid), sd->a4_landscape_radio, 0, 2, 2, 1);
  gtk_grid_attach (GTK_GRID (grid), sd->custom_radio, 0, 3, 2, 1);

  gtk_grid_attach (GTK_GRID (grid),
		   gtk_label_new ("Width (mm)"), 0, 4, 1, 1);
  sd->width_spin = gtk_spin_button_new_with_range (10, 1000, 1);
  gtk_spin_button_set_digits (GTK_SPIN_BUTTON (sd->width_spin), 1);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (sd->width_spin),
			     data->paper_custom_width_mm);
  gtk_grid_attach (GTK_GRID (grid), sd->width_spin, 1, 4, 1, 1);

  gtk_grid_attach (GTK_GRID (grid),
		   gtk_label_new ("Height (mm)"), 0, 5, 1, 1);
  sd->height_spin = gtk_spin_button_new_with_range (10, 1000, 1);
  gtk_spin_button_set_digits (GTK_SPIN_BUTTON (sd->height_spin), 1);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (sd->height_spin),
			     data->paper_custom_height_mm);
  gtk_grid_attach (GTK_GRID (grid), sd->height_spin, 1, 5, 1, 1);

  switch (data->paper_size)
    {
    case SCREENATOR_PAPER_SIZE_A4_PORTRAIT:
      active_radio = sd->a4_portrait_radio;
      break;
    case SCREENATOR_PAPER_SIZE_A4_LANDSCAPE:
      active_radio = sd->a4_landscape_radio;
      break;
    case SCREENATOR_PAPER_SIZE_CUSTOM:
      active_radio = sd->custom_radio;
      break;
    default:
      active_radio = sd->screen_radio;
      break;
    }
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (active_radio), TRUE);

  sd->paper_widgets.custom = sd->custom_radio;
  sd->paper_widgets.width = sd->width_spin;
  sd->paper_widgets.height = sd->height_spin;
  g_signal_connect (sd->custom_radio, "toggled",
		    G_CALLBACK (paper_settings_custom_toggled),
		    &sd->paper_widgets);
  paper_settings_custom_toggled (GTK_TOGGLE_BUTTON (sd->custom_radio),
				 &sd->paper_widgets);

  note = gtk_label_new ("Paper size applies to Plain, Ruled and Graph. "
			"PDF pages keep their own size.");
  gtk_label_set_xalign (GTK_LABEL (note), 0.0);
  gtk_label_set_line_wrap (GTK_LABEL (note), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (note), 46);
  gtk_box_pack_start (GTK_BOX (box), note, FALSE, FALSE, 0);

  return box;
}

static GtkWidget *
build_shortcut_settings_page (SettingsDialog *sd)
{
  ScreenatorData *data = sd->data;
  GtkWidget *box, *scroll, *grid, *note;
  guint i;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width (GTK_CONTAINER (box), 12);

  note = gtk_label_new ("Type a shortcut such as F8, Ctrl+S or Alt+F4. "
			"Type None to disable one. Changes take effect when "
			"the dialog is closed.");
  gtk_label_set_xalign (GTK_LABEL (note), 0.0);
  gtk_label_set_line_wrap (GTK_LABEL (note), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (note), 52);
  gtk_box_pack_start (GTK_BOX (box), note, FALSE, FALSE, 0);

  scroll = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
				  GTK_POLICY_NEVER,
				  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (scroll, 420, 360);

  grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 5);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
  gtk_container_set_border_width (GTK_CONTAINER (grid), 4);

  for (i = 0; i < SCREENATOR_SHORTCUT_COUNT; i++)
    {
      GtkWidget *label = gtk_label_new (screenator_shortcut_labels[i]);
      GtkWidget *entry = gtk_entry_new ();

      gtk_label_set_xalign (GTK_LABEL (label), 0.0);
      gtk_entry_set_text (GTK_ENTRY (entry),
			  data->shortcuts[i].text
			  ? data->shortcuts[i].text
			  : screenator_shortcut_defaults[i]);
      gtk_entry_set_width_chars (GTK_ENTRY (entry), 20);
      gtk_grid_attach (GTK_GRID (grid), label, 0, i, 1, 1);
      gtk_grid_attach (GTK_GRID (grid), entry, 1, i, 1, 1);
      sd->shortcut_entries[i] = entry;
    }

  gtk_container_add (GTK_CONTAINER (scroll), grid);
  gtk_box_pack_start (GTK_BOX (box), scroll, TRUE, TRUE, 0);

  return box;
}

static GtkWidget *
build_icon_settings_page (SettingsDialog *sd)
{
  ScreenatorData *data = sd->data;
  GtkWidget *box, *grid, *label, *combo, *frame;
  static const guint sizes[] = { 16, 32, 48, 64, 128, 256 };
  static const gchar * const size_labels[] =
    { "16x16", "32x32", "48x48", "64x64", "128x128", "256x256" };
  static const gchar * const position_labels[] =
    { "Top Right", "Top Left", "Bottom Right", "Bottom Left" };
  gint active = 2;
  guint i;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
  gtk_container_set_border_width (GTK_CONTAINER (box), 12);

  grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 8);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
  gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);

  label = gtk_label_new ("Floating icon size");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

  combo = gtk_combo_box_text_new ();
  for (i = 0; i < G_N_ELEMENTS (sizes); i++)
    {
      gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo),
				      size_labels[i]);
      if (sizes[i] == data->icon_size)
	active = (gint) i;
    }
  gtk_combo_box_set_active (GTK_COMBO_BOX (combo), active);
  sd->icon_size_combo = combo;
  gtk_grid_attach (GTK_GRID (grid), combo, 1, 0, 1, 1);

  frame = gtk_frame_new ("Floating icon position");
  gtk_box_pack_start (GTK_BOX (box), frame, FALSE, FALSE, 0);

  grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 5);
  gtk_container_set_border_width (GTK_CONTAINER (grid), 10);
  gtk_container_add (GTK_CONTAINER (frame), grid);

  for (i = 0; i < G_N_ELEMENTS (position_labels); i++)
    {
      GtkWidget *radio = gtk_radio_button_new_with_label (
	i == 0 ? NULL : gtk_radio_button_get_group (
	  GTK_RADIO_BUTTON (sd->icon_position_radio[0])),
	position_labels[i]);

      sd->icon_position_radio[i] = radio;
      if ((guint) data->icon_position == i)
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);
      gtk_grid_attach (GTK_GRID (grid), radio, 0, i, 1, 1);
    }

  return box;
}

/* One tabbed dialog for every setting. */
static void
show_settings_dialog (ScreenatorData *data)
{
  SettingsDialog *sd;
  GtkWidget *dialog, *content, *notebook, *general, *paper, *toolbar,
    *icons, *shortcuts;
  static const guint icon_sizes[] = { 16, 32, 48, 64, 128, 256 };
  static const guint timer_delays[] = { 0, 1, 2, 3, 4, 5, 7, 9, 11 };
  gint response;

  if (data->top_dialog)
    return;

  sd = g_new0 (SettingsDialog, 1);
  sd->data = data;

  dialog = gtk_dialog_new_with_buttons ("Settings", NULL,
					GTK_DIALOG_MODAL |
					GTK_DIALOG_DESTROY_WITH_PARENT,
					"Close", GTK_RESPONSE_CLOSE,
					NULL);
  sd->dialog = dialog;
  gtk_window_set_keep_above (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);

  /* Keep the hints window at one stable size.  Without the maximum size the
   * dialog grows with its text and is then moved again by the map handlers. */
  {
    GdkGeometry geometry;

    memset (&geometry, 0, sizeof geometry);
    geometry.min_width = 700;
    geometry.max_width = 700;
    geometry.min_height = 500;
    geometry.max_height = 500;
    gtk_window_set_geometry_hints (GTK_WINDOW (dialog), NULL, &geometry,
				   GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);
  }

  show_dialog_above_drawings (data, dialog);

  content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  notebook = gtk_notebook_new ();
  gtk_container_set_border_width (GTK_CONTAINER (notebook), 8);
  gtk_container_add (GTK_CONTAINER (content), notebook);

  general = build_general_settings_page (sd);
  paper = build_paper_settings_page (sd);
  toolbar = build_toolbar_settings_page (sd);
  icons = build_icon_settings_page (sd);
  shortcuts = build_shortcut_settings_page (sd);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), general,
			    gtk_label_new ("General"));
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), paper,
			    gtk_label_new ("Paper"));
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), toolbar,
			    gtk_label_new ("Toolbar"));
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), icons,
			    gtk_label_new ("Floating Icon"));
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), shortcuts,
			    gtk_label_new ("Shortcuts"));

  gtk_widget_show_all (dialog);
  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if (!sd->reset_clicked)
    {
      gboolean new_save =
	gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (sd->save_check));
      gboolean new_indicator =
	gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (sd->indicator_check));
      gint icon_index = gtk_combo_box_get_active (
	GTK_COMBO_BOX (sd->icon_size_combo));
      guint new_icon_size =
	(icon_index >= 0 &&
	 icon_index < (gint) G_N_ELEMENTS (icon_sizes))
	? icon_sizes[icon_index] : data->icon_size;
      gint timer_index = gtk_combo_box_get_active (
	GTK_COMBO_BOX (sd->timer_delay_combo));
      guint new_timer_delay =
	(timer_index >= 0 &&
	 timer_index < (gint) G_N_ELEMENTS (timer_delays))
	? timer_delays[timer_index] : data->tool_delay_seconds;
      guint new_toolbar_size =
	(guint) gtk_spin_button_get_value_as_int (
	  GTK_SPIN_BUTTON (sd->toolbar_size_spin));
      gint toolbar_direction =
	gtk_combo_box_get_active (GTK_COMBO_BOX (
				    sd->toolbar_direction_combo));
      gboolean new_toolbar_vertical = toolbar_direction != 1;
      gboolean new_continuous =
	gtk_toggle_button_get_active (
	  GTK_TOGGLE_BUTTON (sd->continuous_check));
      ScreenatorPaperSize new_size = SCREENATOR_PAPER_SIZE_SCREEN;
      gdouble new_width = gtk_spin_button_get_value (
	GTK_SPIN_BUTTON (sd->width_spin));
      gdouble new_height = gtk_spin_button_get_value (
	GTK_SPIN_BUTTON (sd->height_spin));
      gboolean size_changed;

      if (new_save != data->save_annotations_on_exit)
	{
	  data->save_annotations_on_exit = new_save;

	  /* Do not bring back older drawings after the option is off. */
	  if (!new_save)
	    remove_saved_annotations ();
	}

      if (new_indicator != data->show_settings_indicator)
	{
	  data->show_settings_indicator = new_indicator;
	  update_settings_indicator (data);
	}

      if (new_icon_size != data->icon_size)
	apply_icon_size (data, new_icon_size);

      {
	gint new_position = data->icon_position;
	guint i;

	for (i = 0; i < G_N_ELEMENTS (sd->icon_position_radio); i++)
	  if (sd->icon_position_radio[i] &&
	      gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (sd->icon_position_radio[i])))
	    {
	      new_position = (gint) i;
	      break;
	    }

	if (new_position != (gint) data->icon_position)
	  {
	    data->icon_position = (ScreenatorIconPosition) new_position;
	    screenator_position_icon (data);
	    update_settings_indicator (data);
	  }
      }

      data->tool_delay_seconds = new_timer_delay;

      if (new_toolbar_size != data->paper_toolbar_icon_size ||
	  new_toolbar_vertical != data->paper_toolbar_vertical)
	{
	  data->paper_toolbar_icon_size =
	    CLAMP (new_toolbar_size, 16, 64);
	  data->paper_toolbar_vertical = new_toolbar_vertical;
	  paper_top_bar_rebuild (data);
	}

      {
	gchar *new_items = NULL;
	gchar *new_order = NULL;
	gboolean changed = FALSE;

	settings_collect_toolbar_items (sd, &new_items, &new_order);

	if (g_strcmp0 (new_items, data->paper_toolbar_items) != 0)
	  {
	    g_free (data->paper_toolbar_items);
	    data->paper_toolbar_items = new_items;
	    new_items = NULL;
	    changed = TRUE;
	  }

	if (g_strcmp0 (new_order, data->paper_toolbar_order) != 0)
	  {
	    g_free (data->paper_toolbar_order);
	    data->paper_toolbar_order = new_order;
	    new_order = NULL;
	    changed = TRUE;
	  }

	g_free (new_items);
	g_free (new_order);

	if (changed)
	  paper_top_bar_rebuild (data);
      }

      {
	gchar *new_texts[SCREENATOR_SHORTCUT_COUNT];
	guint i;

	for (i = 0; i < SCREENATOR_SHORTCUT_COUNT; i++)
	  new_texts[i] = g_strdup (
	    gtk_entry_get_text (GTK_ENTRY (sd->shortcut_entries[i])));
	screenator_apply_shortcuts (data, new_texts);
	for (i = 0; i < SCREENATOR_SHORTCUT_COUNT; i++)
	  g_free (new_texts[i]);
      }

      paper_set_continuous_scrolling (data, new_continuous);

      if (gtk_toggle_button_get_active (
	    GTK_TOGGLE_BUTTON (sd->a4_portrait_radio)))
	new_size = SCREENATOR_PAPER_SIZE_A4_PORTRAIT;
      else if (gtk_toggle_button_get_active (
		 GTK_TOGGLE_BUTTON (sd->a4_landscape_radio)))
	new_size = SCREENATOR_PAPER_SIZE_A4_LANDSCAPE;
      else if (gtk_toggle_button_get_active (
		 GTK_TOGGLE_BUTTON (sd->custom_radio)))
	new_size = SCREENATOR_PAPER_SIZE_CUSTOM;

      size_changed = new_size != data->paper_size ||
	(new_size == SCREENATOR_PAPER_SIZE_CUSTOM &&
	 (new_width != data->paper_custom_width_mm ||
	  new_height != data->paper_custom_height_mm));

      data->paper_size = new_size;
      data->paper_custom_width_mm = new_width;
      data->paper_custom_height_mm = new_height;

      if (size_changed)
	paper_apply_size_settings (data);

      write_keyfile (data);
      schedule_tray_menu_refresh (data);
    }

  gtk_widget_destroy (dialog);
  g_free (sd);

  (void) response;
}

static void
on_icon_settings (GtkMenuItem *menuitem, gpointer user_data)
{
  (void) menuitem;
  show_settings_dialog ((ScreenatorData *) user_data);
}

static void on_icon_paper (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  ScreenatorPaperType paper =
    (ScreenatorPaperType) GPOINTER_TO_INT (
      g_object_get_data (G_OBJECT (menuitem), "icon-paper-type"));

  if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem)))
    return;

  screenator_paper_select (data, paper);

  /* A cancelled PDF chooser leaves the clicked radio item active even though
   * nothing was selected.  Put the radio group back on the real selection so
   * the next click cannot be interpreted as another PDF request. */
  if (data->paper_selected != paper)
    {
      GSList *group = gtk_radio_menu_item_get_group (
	GTK_RADIO_MENU_ITEM (menuitem));
      GSList *l;

      for (l = group; l; l = l->next)
	{
	  GtkWidget *radio = GTK_WIDGET (l->data);

	  if (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (radio),
						  "icon-paper-type")) ==
	      (gint) data->paper_selected)
	    {
	      gtk_check_menu_item_set_active (
		GTK_CHECK_MENU_ITEM (radio), TRUE);
	      break;
	    }
	}
    }
}

/* Ask whether to save the current paper before resetting it or replacing its
 * PDF.  Returns FALSE when the user cancels or saving fails. */
static gboolean
paper_confirm_reset_current (ScreenatorData *data, ScreenatorPaperDocument *doc)
{
  GtkWidget *dialog;
  gint response;
  const gchar *name = "paper";

  if (!doc || !doc->modified || !doc->pages || doc->pages->len == 0)
    return TRUE;

  if (data->paper == doc->type)
    {
      paper_commit_pending (data);
      paper_store_active_page (data);
    }

  switch (doc->type)
    {
    case SCREENATOR_PAPER_PLAIN: name = "Plain"; break;
    case SCREENATOR_PAPER_RULED: name = "Ruled"; break;
    case SCREENATOR_PAPER_GRAPH: name = "Graph"; break;
    case SCREENATOR_PAPER_PDF:   name = "PDF";   break;
    default: break;
    }

  dialog = gtk_message_dialog_new (NULL,
				   GTK_DIALOG_MODAL,
				   GTK_MESSAGE_QUESTION,
				   GTK_BUTTONS_NONE,
				   "The current %s paper has unsaved changes.\n"
				   "Save it in the Documents folder before "
				   "resetting or replacing it?",
				   name);
  gtk_window_set_title (GTK_WINDOW (dialog), "Reset paper");
  gtk_dialog_add_button (GTK_DIALOG (dialog), "Cancel",
			 GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button (GTK_DIALOG (dialog), "Ignore",
			 GTK_RESPONSE_NO);
  gtk_dialog_add_button (GTK_DIALOG (dialog), "Save",
			 GTK_RESPONSE_YES);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);

  show_dialog_above_drawings (data, dialog);
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);

  if (response == GTK_RESPONSE_CANCEL)
    return FALSE;

  if (response == GTK_RESPONSE_YES)
    {
      gchar *path = NULL;

      if (!paper_autosave_document (data, doc, &path))
	{
	  paper_show_save_failure (data);
	  return FALSE;
	}

      {
	GList *paths = g_list_append (NULL, path);

	paper_show_saved_paths (data, paths);
	g_list_free_full (paths, g_free);
      }
    }

  return TRUE;
}

/* Reset a Plain/Ruled/Graph paper to one fresh empty page. */
static void
paper_reset_document (ScreenatorData *data, ScreenatorPaperType type)
{
  ScreenatorPaperDocument *doc = paper_document (data, type);
  ScreenatorPaperPage page;

  if (!doc || type == SCREENATOR_PAPER_PDF)
    return;

  if (data->paper == type)
    {
      paper_commit_pending (data);
      paper_store_active_page (data);
    }

  screenator_page_history_clear (data);
  paper_document_clear_pages (doc);
  page = paper_new_page (data, type);
  g_array_append_val (doc->pages, page);
  doc->page = 0;
  doc->modified = FALSE;

  data->paper_pending_ink_valid = FALSE;
  data->paper_page_dirty = FALSE;
  data->paper_pdf_page = 0;
  paper_background_invalidate (data);

  if (data->paper == type)
    {
      if (paper_continuous (data))
	{
	  paper_build_continuous_layout (data, doc);
	  doc->pdf_scroll_y = 0;
	  paper_pdf_prepare_active (data, doc);
	}
      else
	paper_load_page (data, paper_current_page (doc));

      reset_undo_history (data);
      data->painted = 0;
      paper_repaint (data);
    }

  paper_refresh_active_cursor (data);
  schedule_tray_menu_refresh (data);
}

static void on_icon_reset_or_open_pdf (GtkMenuItem *menuitem,
				       gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  ScreenatorPaperType type;
  ScreenatorPaperDocument *doc;

  (void) menuitem;

  type = data->paper != SCREENATOR_PAPER_NONE
    ? data->paper : data->paper_selected;
  if (type <= SCREENATOR_PAPER_NONE || type > SCREENATOR_PAPER_PDF)
    type = SCREENATOR_PAPER_PLAIN;

  doc = paper_document (data, type);

  if (!paper_confirm_reset_current (data, doc))
    return;

  if (type == SCREENATOR_PAPER_PDF)
    {
      if (!screenator_paper_open_pdf (data))
	return;

      data->paper_selected = SCREENATOR_PAPER_PDF;
      screenator_enter_paper_drawing (data, SCREENATOR_PAPER_PDF);
    }
  else
    {
      paper_reset_document (data, type);

      if (data->paper == SCREENATOR_PAPER_NONE)
	screenator_enter_paper_drawing (data, type);
      else if (data->paper == type)
	paper_top_bar_update_active_tool (data);
    }

  paper_top_bar_update_active_tool (data);
  write_keyfile (data);
  schedule_tray_menu_refresh (data);
}

static void on_icon_paper_insert (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;

  screenator_paper_insert_page
    (data, GPOINTER_TO_INT (g_object_get_data (G_OBJECT (menuitem),
					       "paper-before")) != 0);
}

static void on_icon_paper_delete (GtkMenuItem *menuitem, gpointer user_data)
{
  (void) menuitem;
  screenator_paper_delete_page ((ScreenatorData *) user_data);
}

static void on_icon_paper_goto (GtkMenuItem *menuitem, gpointer user_data)
{
  (void) menuitem;
  paper_show_goto_page_dialog ((ScreenatorData *) user_data);
}

static void on_icon_paper_save (GtkMenuItem *menuitem, gpointer user_data)
{
  screenator_paper_save ((ScreenatorData *) user_data);
}

static void on_icon_thickness (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  guint width = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (menuitem),
						     "icon-thickness-width"));

  data->pen_width = width;

  apply_pen_settings (data);
  paper_top_bar_update_images (data);

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

static void on_icon_marker_opacity (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  gint opacity_int = GPOINTER_TO_INT (
    g_object_get_data (G_OBJECT (menuitem), "icon-marker-opacity"));

  data->marker_opacity = opacity_int / 100.0;
  data->marker_color.alpha = data->marker_opacity;

  if (data->default_marker)
    gdk_cairo_set_source_rgba (data->default_marker->paint_ctx,
			       &data->marker_color);

  write_keyfile (data);
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

static void apply_icon_size (ScreenatorData *data, guint new_size)
{
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

static void on_icon_toggle_toolbar (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  gboolean shown = data->paper_top_bar != NULL;
  gboolean wanted = gtk_check_menu_item_get_active (
    GTK_CHECK_MENU_ITEM (menuitem));

  if (shown != wanted)
    screenator_toggle_paper_top_bar (data);
}

static void on_icon_reset_settings (GtkMenuItem *menuitem, gpointer user_data)
{
  ScreenatorData *data = (ScreenatorData *) user_data;
  GHashTableIter it;
  gpointer value;
  ScreenatorPaperType type;

  (void) menuitem;

  screenator_timer_cancel (data);

  /* Leave Paper Drawing and forget every paper page and PDF. */
  if (data->paper != SCREENATOR_PAPER_NONE)
    screenator_exit_paper_drawing (data);

  for (type = SCREENATOR_PAPER_PLAIN;
       type <= SCREENATOR_PAPER_PDF; type++)
    {
      ScreenatorPaperDocument *doc = data->paper_docs[type];

      if (!doc)
	continue;

      if (type == SCREENATOR_PAPER_PDF && doc->pdf_document)
	{
	  g_object_unref (doc->pdf_document);
	  doc->pdf_document = NULL;
	  g_free (doc->pdf_path);
	  doc->pdf_path = NULL;
	  doc->pdf_loaded = FALSE;
	  doc->pdf_page_count = 0;
	}

      paper_document_clear_pages (doc);
      doc->page = 0;
    }

  screenator_page_history_clear (data);
  if (data->pdf_page_memory)
    g_hash_table_remove_all (data->pdf_page_memory);

  g_free (data->paper_pdf_path);
  data->paper_pdf_path = NULL;
  data->paper_pdf_page = 0;
  data->start_paper = FALSE;

  /* Remove every configuration file before read_keyfile() so it loads the
   * built-in defaults instead of the old settings.  The directory is removed
   * again at the end, after the reset helpers have finished writing. */
  screenator_config_remove_all ();

  /* Rebuild every setting from the defaults. */
  g_free (data->font);
  data->font = NULL;
  g_free (data->math_expression);
  data->math_expression = NULL;
  g_free (data->math_polar_expression);
  data->math_polar_expression = NULL;

  read_keyfile (data);
  data->paper_selected = SCREENATOR_PAPER_PLAIN;
  data->paper = SCREENATOR_PAPER_NONE;
  data->paper_hand_tool = FALSE;
  data->pen_tool = 1;
  data->stylus_mode = SCREENATOR_STYLUS_COLOR;
  data->wheel_mode = SCREENATOR_WHEEL_COLOR;

  if (data->paper_top_bar)
    paper_top_bar_destroy (data);

  apply_pen_settings (data);
  update_eraser_cursor (data);
  set_floating_icon_visible (data, TRUE);
  paper_top_bar_restore (data);

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
  clear_screen (data);
  remove_saved_annotations ();

  if (!data->painted && !screenator_any_grabbed (data))
    hide_window (data);

  write_keyfile (data);
  /* Reset means forget every configuration file, including hints, user tool
   * configuration and saved annotations.  Do this last because some of the
   * reset helpers write the default keyfile. */
  screenator_config_remove_all ();
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
  paper_controls_attach_hover (data, data->settings_win);
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
  paper_controls_attach_hover (data, data->icon_win);

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

  /* The tool bar works in both screen and paper modes. */
  paper_top_bar_restore (data);
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

  item = shortcut_menu_item (data, SCREENATOR_SHORTCUT_PEN, "Pen");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("pen.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_icon_pen), data);

  item = shortcut_menu_item (data, SCREENATOR_SHORTCUT_TEXT, "Text");
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

  item = shortcut_menu_item (data, SCREENATOR_SHORTCUT_ARROW, "Arrow");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("arrow.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_icon_arrow), data);

  if (data->paper == SCREENATOR_PAPER_NONE)
    {
      item = shortcut_menu_item (data, SCREENATOR_SHORTCUT_TIMER, "Timer");
      gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
				     gtk_image_new_from_pixbuf (
				       load_menu_icon ("timer.png")));
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
      g_signal_connect (item, "activate", G_CALLBACK (on_icon_timer), data);
    }

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

  /* --- actions submenu, directly below Shapes --- */
  item = gtk_image_menu_item_new_with_label ("Actions");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("actions.png")));
  submenu = gtk_menu_new ();
  {
    GtkWidget *action;
    gchar *label;

    action = shortcut_menu_item (data, SCREENATOR_SHORTCUT_SWITCH_MODE, "Switch Mode");
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (action),
                                   gtk_image_new_from_pixbuf (
                                     load_menu_icon ("switch_mode.png")));
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), action);
    g_signal_connect (action, "activate",
		      G_CALLBACK (on_icon_switch_mode), data);

    action = gtk_separator_menu_item_new ();
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), action);

    action = shortcut_menu_item (data, SCREENATOR_SHORTCUT_UNDO, "Undo");
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (action),
                                   gtk_image_new_from_pixbuf (
                                     load_menu_icon ("undo.png")));
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), action);
    g_signal_connect (action, "activate", G_CALLBACK (on_undo), data);

    action = shortcut_menu_item (data, SCREENATOR_SHORTCUT_REDO, "Redo");
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (action),
                                   gtk_image_new_from_pixbuf (
                                     load_menu_icon ("redo.png")));
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), action);
    g_signal_connect (action, "activate", G_CALLBACK (on_redo), data);

    action = gtk_separator_menu_item_new ();
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), action);

    action = gtk_image_menu_item_new_with_label ("Clear Screen");
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (action),
                                   gtk_image_new_from_pixbuf (
                                     load_menu_icon ("trash.png")));
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), action);
    g_signal_connect (action, "activate", G_CALLBACK (on_clear), data);

    action = gtk_image_menu_item_new_with_label ("Toggle Visibility (Ctrl+T)");
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (action),
                                   gtk_image_new_from_pixbuf (
                                     load_menu_icon ("hide.png")));
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), action);
    g_signal_connect (action, "activate",
		      G_CALLBACK (on_toggle_vis), data);

    action = shortcut_menu_item (data, SCREENATOR_SHORTCUT_SCREENSHOT, "Screenshot");
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (action),
                                   gtk_image_new_from_pixbuf (
                                     load_menu_icon ("screenshot.png")));
    gtk_widget_set_sensitive (action,
			      data->paper == SCREENATOR_PAPER_NONE);
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), action);
    g_signal_connect (action, "activate",
		      G_CALLBACK (on_action_screenshot), data);

    action = gtk_separator_menu_item_new ();
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), action);

    {
      gchar *label = shortcut_label (data, SCREENATOR_SHORTCUT_TOOLBAR,
				     "Show toolbar");
      action = gtk_check_menu_item_new_with_label (label);
      g_free (label);
    }
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (action),
				    data->paper_top_bar != NULL);
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), action);
    g_signal_connect (action, "activate",
		      G_CALLBACK (on_icon_toggle_toolbar), data);

    label = shortcut_label (data, SCREENATOR_SHORTCUT_FLOATING_ICON,
			    "Show Floating Icon");

    action = gtk_check_menu_item_new_with_label (label);
    g_free (label);
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (action),
				    data->show_floating_icon);
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), action);
    g_signal_connect (action, "activate",
		      G_CALLBACK (on_icon_toggle_floating), data);

    action = gtk_image_menu_item_new_with_label ("Show Hints");
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (action),
                                   gtk_image_new_from_pixbuf (
                                     load_menu_icon ("hint.png")));
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), action);
    g_signal_connect (action, "activate", G_CALLBACK (on_show_hints), data);
  }
  gtk_widget_show_all (submenu);
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

  /* --- Paper submenu --- */
  item = gtk_image_menu_item_new_with_label ("Paper");
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
	{ "Plain", SCREENATOR_PAPER_PLAIN },
	{ "Ruled", SCREENATOR_PAPER_RULED },
	{ "Graph", SCREENATOR_PAPER_GRAPH },
	{ "PDF",   SCREENATOR_PAPER_PDF }
      };
    ScreenatorPaperDocument *active_doc = paper_active_document (data);
    gboolean active = data->paper != SCREENATOR_PAPER_NONE;
    gboolean pdf_active = data->paper == SCREENATOR_PAPER_PDF;
    guint page_count = (active_doc && active_doc->pages)
      ? active_doc->pages->len : 0;
    GtkWidget *items[G_N_ELEMENTS (papers)];
    GtkWidget *widget;
    guint i;

    if (active && page_count > 0)
      {
	gchar *page_label =
	  g_strdup_printf ("Page %u of %u",
			   active_doc->page + 1, page_count);
	GtkWidget *info = gtk_menu_item_new_with_label (page_label);

	gtk_widget_set_sensitive (info, FALSE);
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), info);
	gtk_widget_show (info);
	g_free (page_label);
      }

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

	if (papers[i].paper == data->paper_selected)
	  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (subitem), TRUE);
      }
    for (i = 0; i < G_N_ELEMENTS (papers); i++)
      g_signal_connect (items[i], "activate",
			G_CALLBACK (on_icon_paper), data);

    widget = gtk_image_menu_item_new_with_label ("Reset/Open PDF.");
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget),
                                   gtk_image_new_from_pixbuf (
                                     load_menu_icon ("paper.png")));
    g_signal_connect (widget, "activate",
		      G_CALLBACK (on_icon_reset_or_open_pdf), data);
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), widget);
    gtk_widget_show (widget);

    widget = gtk_separator_menu_item_new ();
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), widget);
    gtk_widget_show (widget);

    widget = shortcut_menu_item (data, SCREENATOR_SHORTCUT_INSERT_BEFORE, "Insert Page Before");
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget),
                                   gtk_image_new_from_pixbuf (
                                     load_menu_icon ("add_before.png")));
    gtk_widget_set_sensitive (widget, active && !pdf_active);
    g_object_set_data (G_OBJECT (widget), "paper-before",
		       GINT_TO_POINTER (1));
    g_signal_connect (widget, "activate",
		      G_CALLBACK (on_icon_paper_insert), data);
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), widget);
    gtk_widget_show (widget);

    widget = gtk_image_menu_item_new_with_label ("Delete Page");
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget),
                                   gtk_image_new_from_pixbuf (
                                     load_menu_icon ("delete-page.png")));
    gtk_widget_set_sensitive (widget,
			      active && !pdf_active && page_count > 1);
    g_signal_connect (widget, "activate",
		      G_CALLBACK (on_icon_paper_delete), data);
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), widget);
    gtk_widget_show (widget);

    widget = shortcut_menu_item (data, SCREENATOR_SHORTCUT_INSERT_AFTER, "Insert Page After");
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget),
                                   gtk_image_new_from_pixbuf (
                                     load_menu_icon ("add_after.png")));
    gtk_widget_set_sensitive (widget, active && !pdf_active);
    g_object_set_data (G_OBJECT (widget), "paper-before",
		       GINT_TO_POINTER (0));
    g_signal_connect (widget, "activate",
		      G_CALLBACK (on_icon_paper_insert), data);
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), widget);
    gtk_widget_show (widget);

    widget = gtk_image_menu_item_new_with_label ("Go to Page...");
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget),
                                   gtk_image_new_from_pixbuf (
                                     load_menu_icon ("goto-page.png")));
    gtk_widget_set_sensitive (widget, active && page_count > 1);
    g_signal_connect (widget, "activate",
		      G_CALLBACK (on_icon_paper_goto), data);
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), widget);
    gtk_widget_show (widget);

    widget = gtk_separator_menu_item_new ();
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), widget);
    gtk_widget_show (widget);

    widget = shortcut_menu_item (data, SCREENATOR_SHORTCUT_SAVE, "Save current paper");
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (widget),
                                   gtk_image_new_from_pixbuf (
                                     load_menu_icon ("save.png")));
    gtk_widget_set_sensitive (widget, active);
    g_signal_connect (widget, "activate",
		      G_CALLBACK (on_icon_paper_save), data);
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), widget);
    gtk_widget_show (widget);
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
  item = gtk_image_menu_item_new_with_label ("Opacity");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("opacity.png")));
  submenu = gtk_menu_new ();
  {
    GSList *group = NULL;
    static const gdouble opacities[] =
      { 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0 };
    static const gchar * const labels[] =
      { "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%",
	"100%" };
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

  /* --- marker opacity submenu --- */
  item = gtk_image_menu_item_new_with_label ("Marker Opacity");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("marker_opacity.png")));
  submenu = gtk_menu_new ();
  {
    GSList *group = NULL;
    static const gint percents[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90 };
    GtkWidget *items[G_N_ELEMENTS (percents)];
    GtkWidget *active_item = NULL;
    guint i, closest = 0;
    gdouble closest_diff =
      ABS (data->marker_opacity - percents[0] / 100.0);

    for (i = 1; i < G_N_ELEMENTS (percents); i++)
      {
	gdouble diff = ABS (data->marker_opacity - percents[i] / 100.0);

	if (diff < closest_diff)
	  {
	    closest = i;
	    closest_diff = diff;
	  }
      }

    for (i = 0; i < G_N_ELEMENTS (percents); i++)
      {
	gchar *label = g_strdup_printf ("%d%%", percents[i]);
	GtkWidget *subitem =
	  gtk_radio_menu_item_new_with_label (group, label);

	g_free (label);
	gtk_menu_shell_append (GTK_MENU_SHELL (submenu), subitem);
	gtk_widget_show (subitem);
	group = gtk_radio_menu_item_get_group (
	  GTK_RADIO_MENU_ITEM (subitem));
	g_object_set_data (G_OBJECT (subitem), "icon-marker-opacity",
			   GINT_TO_POINTER (percents[i]));
	items[i] = subitem;
	if (i == closest)
	  active_item = subitem;
      }

    if (active_item)
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item),
				      TRUE);

    for (i = 0; i < G_N_ELEMENTS (percents); i++)
      g_signal_connect (items[i], "activate",
			G_CALLBACK (on_icon_marker_opacity), data);
  }
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

}

GtkWidget *build_icon_menu (ScreenatorData *data)
{
  GtkWidget *menu = gtk_menu_new ();
  GtkWidget *item;

  append_icon_tool_items (data, menu);

  item = gtk_image_menu_item_new_with_label ("Settings...");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("settings.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_icon_settings), data);

  {
    GtkWidget *sep = gtk_separator_menu_item_new ();
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), sep);
  }

  item = gtk_menu_item_new_with_label ("About");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_about), data);

  item = gtk_image_menu_item_new_with_label ("Quit");
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
                                 gtk_image_new_from_pixbuf (
                                   load_menu_icon ("quit.png")));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect (item, "activate", G_CALLBACK (on_quit), data);

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

typedef struct
{
  const gchar *label;
  const gchar *icon;
  gint         pen_tool;
  gint         shape_type;
} QuickTool;

static const QuickTool quick_tools[] =
  {
    { "Pen",            "pen.png",              1,  -1 },
    { "Text",           "text.png",             4,  -1 },
    { "Eraser",         "eraser.png",           3,  -1 },
    { "Marker",         "marker.png",           11, -1 },
    { "Line",           "free-line.png",        9,  SCREENATOR_SHAPE_LINE },
    { "Spline",         "spline.png",           12, SCREENATOR_SHAPE_SPLINE },
    { "Polygon",        "polygon.png",          14, SCREENATOR_SHAPE_STAR },
    { "Triangle",       "triangle.png",         15, SCREENATOR_SHAPE_TRIANGLE },
    { "Arrow",          "arrow.png",            2,  SCREENATOR_SHAPE_ARROW },
    { "Ellipse",        "ellipse.png",          6,  SCREENATOR_SHAPE_ELLIPSE },
    { "Rounded Rect",   "rounded-rectangle.png",7,  SCREENATOR_SHAPE_ROUNDED_RECTANGLE },
    { "Bucket",         "bucket.png",           13, SCREENATOR_SHAPE_BUCKET },
    { "Math",           "math.png",             16, SCREENATOR_SHAPE_MATH },
    { "Parallel",       "bars.png",             17, SCREENATOR_SHAPE_BARS },
    { "Random",         "random-signal.png",    18, SCREENATOR_SHAPE_RANDOM }
  };

#define QUICK_COLS      5
#define QUICK_ROWS      3
#define QUICK_CELL_W    108
#define QUICK_CELL_H    100
#define QUICK_MARGIN    12
#define QUICK_SPACING   8

typedef struct
{
  ScreenatorData *data;
  GtkWidget      *window;
  GtkWidget      *cells[G_N_ELEMENTS (quick_tools)];
  guint           selected;
  gboolean        was_grabbed;
  gboolean        finished;
} QuickMenu;

static void
quick_menu_install_style (GtkWidget *window)
{
  static GtkCssProvider *provider = NULL;
  GtkStyleContext *context;
  GdkScreen *screen;

  if (!provider)
    {
      provider = gtk_css_provider_new ();
      gtk_css_provider_load_from_data (provider,
	"window.quick-menu { background-color: #fafafa; "
	"border: 1px solid #787878; }\n"
	"button.quick-cell { background-image: none; "
	"background-color: #ffffff; border: 1px solid #cdcdcd; "
	"border-radius: 2px; padding: 6px; box-shadow: none; "
	"text-shadow: none; }\n"
	"button.quick-cell:hover { background-color: #cde4fc; }\n"
	"button.quick-cell:focus { background-color: #cde4fc; "
	"border: 2px solid #3d6fa5; }\n"
	"button.quick-cell label { color: #1e1e1e; font-size: 12px; }\n",
	-1, NULL);
      screen = gtk_widget_get_screen (window);
      gtk_style_context_add_provider_for_screen (
	screen ? screen : gdk_screen_get_default (),
	GTK_STYLE_PROVIDER (provider),
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

  context = gtk_widget_get_style_context (window);
  gtk_style_context_add_class (context, "quick-menu");
}

static gint
quick_cell_index (GtkWidget *button)
{
  return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button),
					     "quick-index"));
}

static void
quick_menu_dismiss (QuickMenu *menu, gboolean restore_focus)
{
  ScreenatorData *data;

  if (menu->finished)
    return;

  menu->finished = TRUE;
  data = menu->data;
  data->quick_menu = NULL;
  gtk_widget_destroy (menu->window);

  if (restore_focus)
    dialog_restore_focus (data);

  if (menu->was_grabbed)
    acquire_grab (data, NULL);

  g_free (menu);
}

static void
quick_menu_cancel (ScreenatorData *data)
{
  QuickMenu *menu;

  if (!data->quick_menu)
    return;

  menu = g_object_get_data (G_OBJECT (data->quick_menu), "quick-menu");
  if (menu)
    quick_menu_dismiss (menu, TRUE);
}

static void
quick_menu_activate (QuickMenu *menu, guint index)
{
  ScreenatorData *data;
  gint pen_tool;
  gint shape_type;
  gboolean was_grabbed;

  if (menu->finished || index >= G_N_ELEMENTS (quick_tools))
    return;

  menu->finished = TRUE;
  data = menu->data;
  pen_tool = quick_tools[index].pen_tool;
  shape_type = quick_tools[index].shape_type;
  was_grabbed = menu->was_grabbed;
  data->quick_menu = NULL;
  gtk_widget_destroy (menu->window);
  dialog_restore_focus (data);

  if (pen_tool == 16)
    {
      if (show_math_dialog (data))
	activate_tool_now (data, 16, SCREENATOR_SHAPE_MATH);
      else if (was_grabbed)
	acquire_grab (data, NULL);
    }
  else
    activate_tool_now (data, pen_tool, shape_type);

  g_free (menu);
}

static void
quick_menu_destroy (GtkWidget *widget, gpointer user_data)
{
  QuickMenu *menu = user_data;

  if (menu->finished)
    return;

  menu->finished = TRUE;
  menu->data->quick_menu = NULL;
  if (menu->was_grabbed)
    acquire_grab (menu->data, NULL);
  g_free (menu);
}

static gboolean
quick_menu_focus_out (GtkWidget *widget, GdkEventFocus *event,
		      gpointer user_data)
{
  QuickMenu *menu = user_data;

  if (!menu->finished)
    quick_menu_dismiss (menu, FALSE);
  return TRUE;
}

static gboolean
quick_menu_focus_in (GtkWidget *widget, GdkEventFocus *event,
		     gpointer user_data)
{
  QuickMenu *menu = user_data;

  menu->selected = quick_cell_index (widget);
  return FALSE;
}

static gboolean
quick_menu_enter (GtkWidget *widget, GdkEventCrossing *event,
		  gpointer user_data)
{
  QuickMenu *menu = user_data;
  guint index = quick_cell_index (widget);

  if (menu->selected != index)
    {
      menu->selected = index;
      gtk_widget_grab_focus (widget);
    }
  return FALSE;
}

static void
quick_menu_clicked (GtkButton *button, gpointer user_data)
{
  QuickMenu *menu = user_data;

  quick_menu_activate (menu, quick_cell_index (GTK_WIDGET (button)));
}

static gboolean
quick_menu_key_press (GtkWidget *widget, GdkEventKey *event,
		      gpointer user_data)
{
  QuickMenu *menu = user_data;
  guint sel = menu->selected;
  guint col = sel % QUICK_COLS;
  guint row = sel / QUICK_COLS;

  switch (event->keyval)
    {
    case GDK_KEY_Left:
      col = (col + QUICK_COLS - 1) % QUICK_COLS;
      break;
    case GDK_KEY_Right:
      col = (col + 1) % QUICK_COLS;
      break;
    case GDK_KEY_Up:
      row = (row + QUICK_ROWS - 1) % QUICK_ROWS;
      break;
    case GDK_KEY_Down:
      row = (row + 1) % QUICK_ROWS;
      break;
    case GDK_KEY_Home:
      row = 0;
      col = 0;
      break;
    case GDK_KEY_End:
      row = QUICK_ROWS - 1;
      col = QUICK_COLS - 1;
      break;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_space:
      gtk_button_clicked (GTK_BUTTON (menu->cells[menu->selected]));
      return TRUE;
    case GDK_KEY_Escape:
      quick_menu_dismiss (menu, TRUE);
      return TRUE;
    default:
      return FALSE;
    }

  menu->selected = row * QUICK_COLS + col;
  gtk_widget_grab_focus (menu->cells[menu->selected]);
  return TRUE;
}

static void
quick_menu_position (ScreenatorData *data, GtkWidget *window)
{
  GdkScreen *screen = gtk_widget_get_screen (window);
  GdkRectangle area;
  GtkRequisition req;
  gint monitor;

  if (!screen)
    return;

  if (data->icon_win)
    {
      gint icon_x, icon_y;

      gtk_window_get_position (GTK_WINDOW (data->icon_win),
			       &icon_x, &icon_y);
      monitor = gdk_screen_get_monitor_at_point (
	screen, icon_x + data->icon_size / 2,
	icon_y + data->icon_size / 2);
    }
  else
    monitor = gdk_screen_get_primary_monitor (screen);

  if (monitor < 0)
    monitor = 0;

  gdk_screen_get_monitor_workarea (screen, monitor, &area);
  gtk_widget_get_preferred_size (window, NULL, &req);

  gtk_window_move (GTK_WINDOW (window),
		   area.x + MAX (0, (area.width - req.width) / 2),
		   area.y + MAX (0, (area.height - req.height) / 2));
}

static gboolean
quick_menu_map (GtkWidget *window, GdkEvent *event, gpointer user_data)
{
  QuickMenu *menu = user_data;
  GdkWindow *gdk_window = gtk_widget_get_window (window);

  quick_menu_position (menu->data, window);
  if (gdk_window)
    gdk_window_raise (gdk_window);
  gtk_widget_grab_focus (menu->cells[menu->selected]);
  dialog_take_focus (menu->data, window);
  return FALSE;
}

/* F10 quick tool menu: a centered 5x3 grid of large icon tiles, matching the
 * Windows version.  Arrow keys move the selection, Home/End jump to the
 * corners, Enter/Space activate it, and Escape or clicking outside closes the
 * grid without selecting a tool. */
void
screenator_show_quick_menu (ScreenatorData *data)
{
  QuickMenu *menu;
  GtkWidget *window, *grid;
  guint i;

  if (data->top_dialog)
    return;

  if (data->quick_menu)
    return;

  paper_finish_temporary_eraser (data);
  screenator_paper_pan_end (data);

  /* The quick menu is an immediate selection, so drop any running timer or
   * blocked-grab fallback first. */
  screenator_timer_cancel (data);
  if (data->tool_grab_retry_id)
    {
      g_source_remove (data->tool_grab_retry_id);
      data->tool_grab_retry_id = 0;
    }
  screenator_raw_input_disable (data);

  menu = g_new0 (QuickMenu, 1);
  menu->data = data;
  menu->was_grabbed = screenator_any_grabbed (data);
  if (menu->was_grabbed)
    release_grab (data, NULL);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  menu->window = window;
  g_object_set_data (G_OBJECT (window), "quick-menu", menu);
  gtk_window_set_title (GTK_WINDOW (window), "Quick Tools");
  gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
  gtk_window_set_resizable (GTK_WINDOW (window), FALSE);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
  gtk_window_set_skip_pager_hint (GTK_WINDOW (window), TRUE);
  gtk_window_set_keep_above (GTK_WINDOW (window), TRUE);
  make_dialog_popup (window);
  quick_menu_install_style (window);

  grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), QUICK_SPACING);
  gtk_grid_set_column_spacing (GTK_GRID (grid), QUICK_SPACING);
  gtk_grid_set_row_homogeneous (GTK_GRID (grid), TRUE);
  gtk_grid_set_column_homogeneous (GTK_GRID (grid), TRUE);
  gtk_container_set_border_width (GTK_CONTAINER (grid), QUICK_MARGIN);
  gtk_container_add (GTK_CONTAINER (window), grid);

  for (i = 0; i < G_N_ELEMENTS (quick_tools); i++)
    {
      GtkWidget *button = gtk_button_new ();
      GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
      GtkWidget *image;
      GtkWidget *label;
      GdkPixbuf *pixbuf;

      gtk_style_context_add_class (gtk_widget_get_style_context (button),
				   "quick-cell");
      gtk_widget_set_size_request (button, QUICK_CELL_W, QUICK_CELL_H);
      gtk_widget_set_can_focus (button, TRUE);

      gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
      gtk_widget_set_valign (box, GTK_ALIGN_CENTER);

      pixbuf = load_menu_icon_at_size (quick_tools[i].icon, 48);
      image = pixbuf
	? gtk_image_new_from_pixbuf (pixbuf)
	: gtk_image_new_from_icon_name ("image-missing",
					GTK_ICON_SIZE_DIALOG);
      if (pixbuf)
	g_object_unref (pixbuf);
      gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 0);

      label = gtk_label_new (quick_tools[i].label);
      gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
      gtk_label_set_max_width_chars (GTK_LABEL (label), 14);
      gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
      gtk_label_set_xalign (GTK_LABEL (label), 0.5);
      gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

      gtk_container_add (GTK_CONTAINER (button), box);
      g_object_set_data (G_OBJECT (button), "quick-index",
			 GUINT_TO_POINTER (i));
      g_signal_connect (button, "clicked",
			G_CALLBACK (quick_menu_clicked), menu);
      g_signal_connect (button, "focus-in-event",
			G_CALLBACK (quick_menu_focus_in), menu);
      g_signal_connect (button, "enter-notify-event",
			G_CALLBACK (quick_menu_enter), menu);
      gtk_grid_attach (GTK_GRID (grid), button,
		       (gint) (i % QUICK_COLS), (gint) (i / QUICK_COLS),
		       1, 1);
      menu->cells[i] = button;
    }

  data->quick_menu = window;
  g_signal_connect (window, "key-press-event",
		    G_CALLBACK (quick_menu_key_press), menu);
  g_signal_connect (window, "focus-out-event",
		    G_CALLBACK (quick_menu_focus_out), menu);
  g_signal_connect (window, "destroy",
		    G_CALLBACK (quick_menu_destroy), menu);
  g_signal_connect (window, "map-event",
		    G_CALLBACK (quick_menu_map), menu);

  gtk_widget_show_all (window);
}

/* ------------------------------------------------------------------
 * Paper Drawing tool bar
 *
 * F10 shows a persistent strip of tool icons while Paper Drawing is active.
 * The bar is vertical on the right edge by default; it can be switched to a
 * horizontal bar on the top edge in Settings > General.  Selecting a
 * tool or a menu entry intentionally leaves the bar open; only F10 or leaving
 * Paper Drawing closes it.  The bar is a window of this application, so the
 * paper grab uses owner-events while it is visible: pointer events over the
 * bar reach its buttons, while events over any other application still reach
 * the full-screen drawing window.
 * ------------------------------------------------------------------ */

#define PAPER_TOOLBAR_DEFAULT_ICON_SIZE 32
#define PAPER_TOOLBAR_MARGIN 0
#define PAPER_TOOLBAR_SPACING 2

typedef struct
{
  ScreenatorData *data;
  GtkWidget      *window;
  GtkWidget      *box;
  GtkWidget      *cursor_button;
  GtkWidget      *pen_button;
  GtkWidget      *text_button;
  GtkWidget      *eraser_button;
  GtkWidget      *arrow_button;
  GtkWidget      *timer_button;
  GtkWidget      *visibility_button;
  GtkWidget      *shapes_button;
  GtkWidget      *add_before_button;
  GtkWidget      *delete_page_button;
  GtkWidget      *add_after_button;
  GtkWidget      *page_number_button;
  GtkWidget      *page_number_label;
  GtkWidget      *goto_page_button;
  GtkWidget      *line_image;
  GtkWidget      *fill_image;
  GtkWidget      *thickness_image;
  GtkWidget      *eraser_image;
  GtkWidget      *save_button;
  GtkWidget      *save_image;

  GtkWidget      *tooltip_window;
  GtkWidget      *tooltip_label;
  GtkWidget      *tooltip_button;
  guint           tooltip_timeout;

  gboolean        drag_pending;
  gboolean        dragging;
  gboolean        drag_was_grabbed;
  gboolean        drag_seat_grabbed;
  gint            drag_start_root_x;
  gint            drag_start_root_y;
  gint            drag_window_x;
  gint            drag_window_y;
  GtkWidget      *drag_widget;
} PaperTopBar;

typedef struct
{
  ScreenatorData *data;
  gboolean        was_grabbed;
} PaperTopBarMenu;

static void
paper_toolbar_set_tooltip_text (GtkWidget *button, const gchar *text)
{
  g_object_set_data_full (G_OBJECT (button), "paper-tooltip-text",
			  g_strdup (text ? text : ""), g_free);
}

static void
paper_toolbar_tooltip_hide (ScreenatorData *data)
{
  PaperTopBar *bar;

  if (!data)
    return;

  bar = data->paper_top_bar_data;
  if (!bar)
    return;

  if (bar->tooltip_timeout)
    {
      g_source_remove (bar->tooltip_timeout);
      bar->tooltip_timeout = 0;
    }

  bar->tooltip_button = NULL;

  if (bar->tooltip_window)
    gtk_widget_hide (bar->tooltip_window);
}

/* The tooltip window must never take pointer or keyboard input. */
static void
paper_toolbar_tooltip_realize (GtkWidget *widget, gpointer user_data)
{
  GdkWindow *window = gtk_widget_get_window (widget);
  cairo_region_t *empty;

  (void) user_data;

  if (!window)
    return;

  empty = cairo_region_create ();
  gdk_window_input_shape_combine_region (window, empty, 0, 0);
  cairo_region_destroy (empty);
}

static void
paper_toolbar_tooltip_create (PaperTopBar *bar)
{
  static GtkCssProvider *provider = NULL;
  GtkWidget *label;
  GtkStyleContext *context;

  bar->tooltip_window = gtk_window_new (GTK_WINDOW_POPUP);
  gtk_window_set_decorated (GTK_WINDOW (bar->tooltip_window), FALSE);
  gtk_window_set_resizable (GTK_WINDOW (bar->tooltip_window), FALSE);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (bar->tooltip_window), TRUE);
  gtk_window_set_skip_pager_hint (GTK_WINDOW (bar->tooltip_window), TRUE);
  gtk_window_set_accept_focus (GTK_WINDOW (bar->tooltip_window), FALSE);
  gtk_window_set_focus_on_map (GTK_WINDOW (bar->tooltip_window), FALSE);
  gtk_window_set_keep_above (GTK_WINDOW (bar->tooltip_window), TRUE);
  gtk_window_set_type_hint (GTK_WINDOW (bar->tooltip_window),
			    GDK_WINDOW_TYPE_HINT_TOOLTIP);

  label = gtk_label_new (NULL);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_widget_set_margin_start (label, 6);
  gtk_widget_set_margin_end (label, 6);
  gtk_widget_set_margin_top (label, 2);
  gtk_widget_set_margin_bottom (label, 2);
  gtk_container_add (GTK_CONTAINER (bar->tooltip_window), label);
  bar->tooltip_label = label;
  gtk_widget_show (label);

  if (!provider)
    {
      provider = gtk_css_provider_new ();
      gtk_css_provider_load_from_data (provider,
	"window.paper-tooltip { background-color: #ffffe1; "
	"border: 1px solid #767676; }\n", -1, NULL);
    }

  context = gtk_widget_get_style_context (bar->tooltip_window);
  gtk_style_context_add_provider (context, GTK_STYLE_PROVIDER (provider),
				  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  gtk_style_context_add_class (context, "paper-tooltip");

  g_signal_connect (bar->tooltip_window, "realize",
		    G_CALLBACK (paper_toolbar_tooltip_realize), NULL);

  /* Realize it now (without mapping) so the first tooltip is measured and
   * positioned correctly. */
  gtk_widget_realize (bar->tooltip_window);
}

static gboolean
paper_toolbar_tooltip_show (ScreenatorData *data)
{
  PaperTopBar *bar = data ? data->paper_top_bar_data : NULL;
  GtkWidget *button;
  const gchar *text;
  GtkRequisition req;
  GtkAllocation alloc;
  GdkWindow *bar_window;
  GdkScreen *screen;
  GdkRectangle area;
  gint bx = 0, by = 0, wx = 0, wy = 0;
  gint root_x, root_y, x, y, monitor;

  if (!bar || !bar->tooltip_window || !bar->tooltip_button)
    return G_SOURCE_REMOVE;

  button = bar->tooltip_button;
  text = g_object_get_data (G_OBJECT (button), "paper-tooltip-text");
  if (!text || !*text)
    return G_SOURCE_REMOVE;

  gtk_label_set_text (GTK_LABEL (bar->tooltip_label), text);

  gtk_widget_get_preferred_size (bar->tooltip_window, NULL, &req);
  gtk_widget_get_allocation (button, &alloc);
  gtk_widget_translate_coordinates (button, bar->window, 0, 0, &bx, &by);

  bar_window = gtk_widget_get_window (bar->window);
  if (bar_window)
    gdk_window_get_origin (bar_window, &wx, &wy);

  root_x = wx + bx;
  root_y = wy + by;

  if (data->paper_toolbar_vertical)
    {
      /* Beside the icon, on the inner side of the vertical bar. */
      x = root_x - req.width - 8;
      y = root_y + (alloc.height - req.height) / 2;
    }
  else
    {
      /* Below the icon, horizontally centred on it. */
      x = root_x + (alloc.width - req.width) / 2;
      y = root_y + alloc.height + 8;
    }

  screen = bar_window ? gdk_window_get_screen (bar_window) : NULL;
  if (screen)
    {
      monitor = gdk_screen_get_monitor_at_point (screen, root_x, root_y);
      if (monitor < 0)
	monitor = 0;
      gdk_screen_get_monitor_workarea (screen, monitor, &area);

      /* Flip to the other side when the preferred side has no room. */
      if (data->paper_toolbar_vertical && x < area.x)
	x = root_x + alloc.width + 8;
      else if (!data->paper_toolbar_vertical &&
	       y + req.height > area.y + area.height)
	y = root_y - req.height - 8;

      x = CLAMP (x, area.x, area.x + MAX (0, area.width - req.width));
      y = CLAMP (y, area.y, area.y + MAX (0, area.height - req.height));
    }

  gtk_window_move (GTK_WINDOW (bar->tooltip_window), x, y);
  gtk_widget_show (bar->tooltip_window);

  if (gtk_widget_get_window (bar->tooltip_window))
    gdk_window_raise (gtk_widget_get_window (bar->tooltip_window));

  return G_SOURCE_REMOVE;
}

static gboolean
paper_toolbar_tooltip_timeout (gpointer user_data)
{
  ScreenatorData *data = user_data;
  PaperTopBar *bar = data->paper_top_bar_data;

  if (bar)
    bar->tooltip_timeout = 0;

  paper_toolbar_tooltip_show (data);
  return G_SOURCE_REMOVE;
}

static gboolean
paper_toolbar_tooltip_enter (GtkWidget *widget, GdkEventCrossing *event,
			     gpointer user_data)
{
  ScreenatorData *data = user_data;
  PaperTopBar *bar = data ? data->paper_top_bar_data : NULL;

  (void) event;

  if (!bar)
    return FALSE;

  paper_toolbar_tooltip_hide (data);
  bar->tooltip_button = widget;
  bar->tooltip_timeout =
    g_timeout_add (600, paper_toolbar_tooltip_timeout, data);
  return FALSE;
}

static gboolean
paper_toolbar_tooltip_leave (GtkWidget *widget, GdkEventCrossing *event,
			     gpointer user_data)
{
  ScreenatorData *data = user_data;
  PaperTopBar *bar = data ? data->paper_top_bar_data : NULL;

  (void) event;

  if (bar && bar->tooltip_button == widget)
    paper_toolbar_tooltip_hide (data);

  return FALSE;
}

/* Disabled buttons do not receive enter events, so the custom tooltip never
 * starts for them.  The toolbar window supplies a native tooltip fallback for
 * insensitive children only; active children keep the anchored tooltip. */
static gboolean
paper_toolbar_query_tooltip (GtkWidget *widget, gint x, gint y,
			     gboolean keyboard_mode, GtkTooltip *tooltip,
			     gpointer user_data)
{
  PaperTopBar *bar = user_data;
  GList *children, *l;

  (void) keyboard_mode;

  if (!bar || !bar->box)
    return FALSE;

  children = gtk_container_get_children (GTK_CONTAINER (bar->box));
  for (l = children; l; l = l->next)
    {
      GtkWidget *child = l->data;
      GtkAllocation allocation;
      const gchar *text;
      gint cx = 0, cy = 0;

      if (!gtk_widget_get_visible (child) ||
	  gtk_widget_get_sensitive (child))
	continue;

      gtk_widget_get_allocation (child, &allocation);
      gtk_widget_translate_coordinates (child, widget, 0, 0, &cx, &cy);

      if (x < cx || y < cy ||
	  x >= cx + allocation.width || y >= cy + allocation.height)
	continue;

      text = g_object_get_data (G_OBJECT (child), "paper-tooltip-text");
      if (text && *text)
	{
	  gtk_tooltip_set_text (tooltip, text);
	  g_list_free (children);
	  return TRUE;
	}
    }

  g_list_free (children);
  return FALSE;
}

static void paper_top_bar_update_images (ScreenatorData *data);
static void paper_top_bar_destroy (ScreenatorData *data);

static gint
paper_top_bar_icon_size (ScreenatorData *data)
{
  guint size = data->paper_toolbar_icon_size;

  if (size < 16 || size > 64)
    size = PAPER_TOOLBAR_DEFAULT_ICON_SIZE;

  return (gint) size;
}

static void
paper_toolbar_install_style (GtkWidget *window)
{
  static GtkCssProvider *provider = NULL;
  GtkStyleContext *context;
  GdkScreen *screen;

  if (!provider)
    {
      provider = gtk_css_provider_new ();
      gtk_css_provider_load_from_data (provider,
	"window.paper-toolbar { background-color: #f4f4f4; "
	"border: none; border-image: none; padding: 0; margin: 0; "
	"box-shadow: none; }\n"
	"window.paper-toolbar decoration { border: none; border-image: none; "
	"padding: 0; margin: 0; box-shadow: none; }\n"
	"button.paper-toolbar-button { background-image: none; "
	"background-color: transparent; border: 1px solid #7a7a7a; "
	"border-image: none; border-radius: 0; padding: 0; margin: 0; "
	"min-width: 0; min-height: 0; box-shadow: none; "
	"text-shadow: none; }\n"
	"button.paper-toolbar-button:hover { background-color: #cde4fc; }\n"
	"button.paper-toolbar-button-active { background-color: #8fbfea; }\n",
	-1, NULL);
      screen = gtk_widget_get_screen (window);
      gtk_style_context_add_provider_for_screen (
	screen ? screen : gdk_screen_get_default (),
	GTK_STYLE_PROVIDER (provider),
	GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

  context = gtk_widget_get_style_context (window);
  gtk_style_context_add_class (context, "paper-toolbar");
}

static GdkPixbuf *
paper_color_swatch (const GdkRGBA *color, gboolean none, gint size)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  GdkPixbuf *pixbuf;

  size = MAX (4, size);
  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, size, size);
  cr = cairo_create (surface);

  if (none)
    {
      cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
      cairo_paint (cr);
      cairo_set_source_rgb (cr, 0.85, 0.1, 0.1);
      cairo_set_line_width (cr, MAX (1.0, size / 10.0));
      cairo_move_to (cr, 2, 2);
      cairo_line_to (cr, size - 2, size - 2);
      cairo_stroke (cr);
    }
  else
    {
      cairo_set_source_rgba (cr, color->red, color->green, color->blue, 1.0);
      cairo_rectangle (cr, 0.5, 0.5, size - 1.0, size - 1.0);
      cairo_fill (cr);
    }

  cairo_set_source_rgba (cr, 0.25, 0.25, 0.25, 1.0);
  cairo_set_line_width (cr, 1.0);
  cairo_rectangle (cr, 0.5, 0.5, size - 1.0, size - 1.0);
  cairo_stroke (cr);
  cairo_destroy (cr);
  cairo_surface_flush (surface);

  pixbuf = gdk_pixbuf_get_from_surface (surface, 0, 0, size, size);
  cairo_surface_destroy (surface);
  return pixbuf;
}

static GdkPixbuf *
paper_thickness_sample (guint width, gint size)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  GdkPixbuf *pixbuf;
  gint box = MAX (24, MIN ((gint) width + 10, 56));
  gdouble line = MAX (1.0, MIN ((gdouble) width, box - 6.0));

  size = MAX (16, size);
  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, size, box);
  cr = cairo_create (surface);
  cairo_set_source_rgba (cr, 0.1, 0.1, 0.1, 1.0);
  cairo_set_line_width (cr, line);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to (cr, 5, box / 2.0);
  cairo_line_to (cr, size - 5, box / 2.0);
  cairo_stroke (cr);
  cairo_destroy (cr);
  cairo_surface_flush (surface);

  pixbuf = gdk_pixbuf_get_from_surface (surface, 0, 0, size, box);
  cairo_surface_destroy (surface);
  return pixbuf;
}

static GdkPixbuf *
paper_eraser_size_sample (guint width, gint size)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  GdkPixbuf *pixbuf;
  gdouble diameter;

  size = MAX (16, size);
  diameter = CLAMP ((gdouble) width * size / 150.0,
		    3.0, (gdouble) size - 2.0);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, size, size);
  cr = cairo_create (surface);
  cairo_arc (cr, size / 2.0, size / 2.0, diameter / 2.0, 0, 2.0 * G_PI);
  cairo_set_source_rgba (cr, 0.85, 0.85, 0.85, 1.0);
  cairo_fill_preserve (cr);
  cairo_set_source_rgba (cr, 0.25, 0.25, 0.25, 1.0);
  cairo_set_line_width (cr, 1.0);
  cairo_stroke (cr);
  cairo_destroy (cr);
  cairo_surface_flush (surface);

  pixbuf = gdk_pixbuf_get_from_surface (surface, 0, 0, size, size);
  cairo_surface_destroy (surface);
  return pixbuf;
}

static GtkWidget *
paper_add_eraser_size_item (GtkWidget *menu, GSList **group,
			    guint width, gint icon_size)
{
  GtkWidget *item = gtk_radio_menu_item_new (*group);
  GdkPixbuf *pixbuf = paper_eraser_size_sample (width, icon_size);
  GtkWidget *image = gtk_image_new_from_pixbuf (pixbuf);

  *group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
  gtk_container_add (GTK_CONTAINER (item), image);
  g_object_set_data (G_OBJECT (item), "icon-eraser-width",
		     GUINT_TO_POINTER (width));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  if (pixbuf)
    g_object_unref (pixbuf);
  return item;
}

static void paper_toolbar_set_tooltip_text (GtkWidget *button,
					    const gchar *text);
static gboolean paper_toolbar_tooltip_enter (GtkWidget *widget,
					     GdkEventCrossing *event,
					     gpointer user_data);
static gboolean paper_toolbar_tooltip_leave (GtkWidget *widget,
					     GdkEventCrossing *event,
					     gpointer user_data);
static gboolean paper_toolbar_tooltip_timeout (gpointer user_data);

static GtkWidget *
paper_top_bar_button (ScreenatorData *data, GdkPixbuf *pixbuf,
		      const gchar *tooltip)
{
  GtkWidget *button = gtk_button_new ();
  GtkWidget *image;

  gtk_style_context_add_class (gtk_widget_get_style_context (button),
			       "paper-toolbar-button");
  gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
  gtk_widget_set_can_focus (button, FALSE);

  /* The tooltip is positioned by the toolbar, next to the hovered icon, so
   * it stays aligned in both horizontal and vertical layouts. */
  paper_toolbar_set_tooltip_text (button, tooltip);
  gtk_widget_add_events (button,
			 GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
  g_signal_connect (button, "enter-notify-event",
		    G_CALLBACK (paper_toolbar_tooltip_enter), data);
  g_signal_connect (button, "leave-notify-event",
		    G_CALLBACK (paper_toolbar_tooltip_leave), data);

  image = pixbuf ? gtk_image_new_from_pixbuf (pixbuf)
		 : gtk_image_new_from_icon_name ("image-missing",
						 GTK_ICON_SIZE_SMALL_TOOLBAR);
  gtk_container_add (GTK_CONTAINER (button), image);
  /* One pixel of border on each side, with no padding. */
  gtk_widget_set_size_request (button, paper_top_bar_icon_size (data) + 2, -1);
  return button;
}

static GtkWidget *
paper_top_bar_icon_button (ScreenatorData *data, const gchar *icon,
			   const gchar *tooltip)
{
  GdkPixbuf *pixbuf =
    load_menu_icon_at_size (icon, paper_top_bar_icon_size (data));
  GtkWidget *button = paper_top_bar_button (data, pixbuf, tooltip);

  if (pixbuf)
    g_object_unref (pixbuf);
  return button;
}

/* A compact toolbar indicator showing the active page number.  It uses the
 * same button style as the icons but contains a text label. */
static GtkWidget *
paper_top_bar_page_number_button (ScreenatorData *data)
{
  GtkWidget *button = gtk_button_new ();
  GtkWidget *label = gtk_label_new ("1");

  gtk_style_context_add_class (gtk_widget_get_style_context (button),
			       "paper-toolbar-button");
  gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
  gtk_widget_set_can_focus (button, FALSE);
  paper_toolbar_set_tooltip_text (button, "Current page");
  gtk_widget_add_events (button,
			 GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
  g_signal_connect (button, "enter-notify-event",
		    G_CALLBACK (paper_toolbar_tooltip_enter), data);
  g_signal_connect (button, "leave-notify-event",
		    G_CALLBACK (paper_toolbar_tooltip_leave), data);
  gtk_label_set_xalign (GTK_LABEL (label), 0.5);
  gtk_label_set_yalign (GTK_LABEL (label), 0.5);
  gtk_widget_set_size_request (button,
			       MAX (paper_top_bar_icon_size (data) + 2, 34),
			       -1);
  gtk_container_add (GTK_CONTAINER (button), label);
  return button;
}

/* One row in a colour menu: a coloured square and no text. */
static GtkWidget *
paper_add_swatch_item (GtkWidget *menu, GSList **group,
		       const GdkRGBA *color, gboolean none,
		       gint swatch_size, const gchar *name)
{
  GtkWidget *item = gtk_radio_menu_item_new (*group);
  GdkPixbuf *pixbuf = paper_color_swatch (color, none, swatch_size);
  GtkWidget *image = gtk_image_new_from_pixbuf (pixbuf);

  *group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
  gtk_container_add (GTK_CONTAINER (item), image);
  g_object_set_data (G_OBJECT (item), "icon-color-name", (gpointer) name);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  if (pixbuf)
    g_object_unref (pixbuf);
  return item;
}

static GtkWidget *
paper_add_fill_item (GtkWidget *menu, GSList **group,
		     const GdkRGBA *color, gboolean none,
		     gint swatch_size, const gchar *name)
{
  GtkWidget *item = gtk_radio_menu_item_new (*group);
  GdkPixbuf *pixbuf = paper_color_swatch (color, none, swatch_size);
  GtkWidget *image = gtk_image_new_from_pixbuf (pixbuf);

  *group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
  gtk_container_add (GTK_CONTAINER (item), image);
  g_object_set_data (G_OBJECT (item), "icon-shape-fill-name",
		     (gpointer) name);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  if (pixbuf)
    g_object_unref (pixbuf);
  return item;
}

static GtkWidget *
paper_add_thickness_item (GtkWidget *menu, GSList **group,
			  guint width, gint icon_size)
{
  GtkWidget *item = gtk_radio_menu_item_new (*group);
  GdkPixbuf *pixbuf = paper_thickness_sample (width, icon_size);
  GtkWidget *image = gtk_image_new_from_pixbuf (pixbuf);

  *group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
  gtk_container_add (GTK_CONTAINER (item), image);
  g_object_set_data (G_OBJECT (item), "icon-thickness-width",
		     GUINT_TO_POINTER (width));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  if (pixbuf)
    g_object_unref (pixbuf);
  return item;
}

static void
paper_top_bar_menu_finished (GtkMenuShell *menu, gpointer user_data)
{
  PaperTopBarMenu *state;

  (void) user_data;

  if (g_object_get_data (G_OBJECT (menu), "paper-menu-finished"))
    return;
  g_object_set_data (G_OBJECT (menu), "paper-menu-finished",
		     GINT_TO_POINTER (1));

  state = g_object_get_data (G_OBJECT (menu), "paper-menu-state");
  if (!state)
    return;

  paper_refresh_controls_hover (state->data);
  if (state->was_grabbed)
    acquire_grab (state->data, NULL);

  if (state->data->paper != SCREENATOR_PAPER_NONE)
    paper_take_focus (state->data);

  if (state->data->paper_hand_tool)
    restore_paper_cursor (state->data);

  if (state->data->paper_top_bar &&
      gtk_widget_get_window (state->data->paper_top_bar))
    gdk_window_raise (gtk_widget_get_window (state->data->paper_top_bar));

  g_object_set_data (G_OBJECT (menu), "paper-menu-state", NULL);
}

/* Release the painting grab while a menu is open, so GTK can own the pointer,
 * and restore it when the menu closes. */
static void
paper_top_bar_popup_menu (ScreenatorData *data, GtkWidget *button,
			  GtkWidget *menu)
{
  PaperTopBarMenu *state = g_new0 (PaperTopBarMenu, 1);
  gboolean was_grabbed = screenator_any_grabbed (data);

  state->data = data;
  state->was_grabbed = was_grabbed;

  paper_finish_temporary_eraser (data);
  if (was_grabbed)
    release_grab (data, NULL);

  /* The menu is reused every time it is opened. */
  g_object_set_data (G_OBJECT (menu), "paper-menu-finished", NULL);
  g_object_set_data_full (G_OBJECT (menu), "paper-menu-state", state, g_free);

  gtk_widget_show_all (menu);
  if (data->paper_toolbar_vertical)
    gtk_menu_popup_at_widget (GTK_MENU (menu), button,
			      GDK_GRAVITY_WEST,
			      GDK_GRAVITY_EAST, NULL);
  else
    gtk_menu_popup_at_widget (GTK_MENU (menu), button,
			      GDK_GRAVITY_SOUTH_WEST,
			      GDK_GRAVITY_NORTH_WEST, NULL);

  if (!gtk_widget_get_visible (menu))
    {
      paper_top_bar_menu_finished (GTK_MENU_SHELL (menu), data);
      gtk_widget_destroy (menu);
    }
}

static void
paper_top_bar_settings_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  show_settings_dialog ((ScreenatorData *) user_data);
}

static void
paper_top_bar_hints_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  show_hints_dialog ((ScreenatorData *) user_data, FALSE);
}

static void
on_hints_about (GtkButton *button, gpointer user_data)
{
  (void) button;
  on_about (NULL, user_data);
}

static void
paper_top_bar_quit_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  screenator_request_quit ((ScreenatorData *) user_data);
}

static void
paper_top_bar_switch_mode_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  screenator_toggle_paper_drawing ((ScreenatorData *) user_data);
}

static void
paper_top_bar_save_clicked (GtkButton *button, gpointer user_data)
{
  ScreenatorData *data = user_data;

  (void) button;
  if (data->paper == SCREENATOR_PAPER_NONE)
    screenator_save_screenshot (data);
  else
    screenator_paper_save (data);
}

static void
paper_top_bar_timer_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  screenator_start_timer ((ScreenatorData *) user_data);
}

static void
paper_top_bar_clear_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  clear_screen ((ScreenatorData *) user_data);
}

static void
paper_top_bar_visibility_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  toggle_drawings_visibility ((ScreenatorData *) user_data);
}

static void
paper_top_bar_cursor_clicked (GtkButton *button, gpointer user_data)
{
  ScreenatorData *data = user_data;

  (void) button;

  /* The Cursor button always switches immediately, even when a shape with
   * handles was just committed. */
  screenator_exit_tool (data);
  screenator_activate_cursor_state (data);
  paper_bring_to_front (data);

  write_keyfile (data);
  schedule_tray_menu_refresh (data);
}

static void
paper_top_bar_tool_clicked (GtkButton *button, gpointer user_data)
{
  ScreenatorData *data = user_data;
  gint pen_tool = GPOINTER_TO_INT (
    g_object_get_data (G_OBJECT (button), "paper-tool"));
  gint shape_type = GPOINTER_TO_INT (
    g_object_get_data (G_OBJECT (button), "paper-shape"));

  paper_finish_temporary_eraser (data);
  activate_tool_now (data, pen_tool, shape_type);
  paper_top_bar_update_images (data);

  if (data->paper != SCREENATOR_PAPER_NONE)
    paper_take_focus (data);
}

static void
paper_top_bar_shape_clicked (GtkMenuItem *item, gpointer user_data)
{
  ScreenatorData *data = user_data;
  gint pen_tool = GPOINTER_TO_INT (
    g_object_get_data (G_OBJECT (item), "paper-tool"));
  gint shape_type = GPOINTER_TO_INT (
    g_object_get_data (G_OBJECT (item), "paper-shape"));

  if (pen_tool == 16)
    {
      if (show_math_dialog (data))
	schedule_tool_activation (data, 16, SCREENATOR_SHAPE_MATH);
    }
  else
    schedule_tool_activation (data, pen_tool, shape_type);
}

static void
paper_top_bar_color_clicked (GtkMenuItem *item, gpointer user_data)
{
  on_icon_color (item, user_data);
  paper_top_bar_update_images ((ScreenatorData *) user_data);
}

static void
paper_top_bar_fill_clicked (GtkMenuItem *item, gpointer user_data)
{
  on_icon_shape_fill (item, user_data);
  paper_top_bar_update_images ((ScreenatorData *) user_data);
}

static void
paper_top_bar_thickness_clicked (GtkMenuItem *item, gpointer user_data)
{
  on_icon_thickness (item, user_data);
  paper_top_bar_update_images ((ScreenatorData *) user_data);
}

static void
paper_top_bar_custom_color_clicked (GtkMenuItem *item, gpointer user_data)
{
  on_icon_custom_color (item, user_data);
}

static GtkWidget *
paper_top_bar_build_shapes_menu (ScreenatorData *data)
{
  GtkWidget *menu = gtk_menu_new ();
  guint i;

  for (i = 0; i < G_N_ELEMENTS (quick_tools); i++)
    {
      GtkWidget *item;
      GdkPixbuf *pixbuf;

      if (quick_tools[i].pen_tool == 1 ||
	  quick_tools[i].pen_tool == 4 ||
	  quick_tools[i].pen_tool == 3 ||
	  quick_tools[i].pen_tool == 2)
	continue;

      item = gtk_image_menu_item_new_with_label (quick_tools[i].label);
      pixbuf = load_menu_icon_at_size (quick_tools[i].icon,
				       MAX (16, MIN (paper_top_bar_icon_size (data),
						     32)));
      gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
				     gtk_image_new_from_pixbuf (pixbuf));
      if (pixbuf)
	g_object_unref (pixbuf);

      g_object_set_data (G_OBJECT (item), "paper-tool",
			 GINT_TO_POINTER (quick_tools[i].pen_tool));
      g_object_set_data (G_OBJECT (item), "paper-shape",
			 GINT_TO_POINTER (quick_tools[i].shape_type));
      g_signal_connect (item, "activate",
			G_CALLBACK (paper_top_bar_shape_clicked), data);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
    }

  return menu;
}

static GtkWidget *
paper_top_bar_build_line_menu (ScreenatorData *data)
{
  GtkWidget *menu = gtk_menu_new ();
  GSList *group = NULL;
  GtkWidget *items[G_N_ELEMENTS (line_colors)];
  GtkWidget *active_item = NULL;
  GtkWidget *custom_item;
  guint i;
  gint swatch = MAX (16, MIN (paper_top_bar_icon_size (data), 32));

  for (i = 0; i < G_N_ELEMENTS (line_colors); i++)
    {
      GdkRGBA color;

      if (gdk_rgba_parse (&color, line_colors[i].name))
	items[i] = paper_add_swatch_item (menu, &group, &color, FALSE,
					  swatch, line_colors[i].name);
      else
	{
	  GdkRGBA fallback = { 0, 0, 0, 1 };
	  items[i] = paper_add_swatch_item (menu, &group, &fallback, FALSE,
					    swatch, line_colors[i].name);
	}

      if (data->default_pen &&
	  gdk_rgba_parse (&color, line_colors[i].name))
	{
	  GdkRGBA *current = data->default_pen->paint_color;

	  if (current->red == color.red &&
	      current->green == color.green &&
	      current->blue == color.blue)
	    active_item = items[i];
	}
    }

  custom_item = gtk_radio_menu_item_new_with_label (group, "Custom...");
  group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (custom_item));
  g_object_set_data (G_OBJECT (custom_item), "icon-custom-target",
		     (gpointer) "line");
  if (!active_item)
    active_item = custom_item;
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), custom_item);

  if (active_item)
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item), TRUE);

  for (i = 0; i < G_N_ELEMENTS (line_colors); i++)
    g_signal_connect (items[i], "activate",
		      G_CALLBACK (paper_top_bar_color_clicked), data);
  g_signal_connect (custom_item, "activate",
		    G_CALLBACK (paper_top_bar_custom_color_clicked), data);

  return menu;
}

static GtkWidget *
paper_top_bar_build_fill_menu (ScreenatorData *data)
{
  GtkWidget *menu = gtk_menu_new ();
  GSList *group = NULL;
  GtkWidget *items[G_N_ELEMENTS (fill_colors)];
  GtkWidget *active_item = NULL;
  GtkWidget *custom_item;
  guint i;
  gint swatch = MAX (16, MIN (paper_top_bar_icon_size (data), 32));

  for (i = 0; i < G_N_ELEMENTS (fill_colors); i++)
    {
      gboolean none = g_ascii_strcasecmp (fill_colors[i].name, "none") == 0;
      GdkRGBA color;

      if (!none && gdk_rgba_parse (&color, fill_colors[i].name))
	items[i] = paper_add_fill_item (menu, &group, &color, FALSE,
					swatch, fill_colors[i].name);
      else
	{
	  GdkRGBA fallback = { 0, 0, 0, 1 };
	  items[i] = paper_add_fill_item (menu, &group, &fallback, none,
					  swatch, fill_colors[i].name);
	}

      if (none)
	{
	  if (!data->shape_fill_enabled)
	    active_item = items[i];
	}
      else if (data->shape_fill_enabled &&
	       gdk_rgba_parse (&color, fill_colors[i].name) &&
	       data->shape_fill_color.red == color.red &&
	       data->shape_fill_color.green == color.green &&
	       data->shape_fill_color.blue == color.blue)
	active_item = items[i];
    }

  custom_item = gtk_radio_menu_item_new_with_label (group, "Custom...");
  group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (custom_item));
  g_object_set_data (G_OBJECT (custom_item), "icon-custom-target",
		     (gpointer) "fill");
  if (!active_item)
    active_item = custom_item;
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), custom_item);

  if (active_item)
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item), TRUE);

  for (i = 0; i < G_N_ELEMENTS (fill_colors); i++)
    g_signal_connect (items[i], "activate",
		      G_CALLBACK (paper_top_bar_fill_clicked), data);
  g_signal_connect (custom_item, "activate",
		    G_CALLBACK (paper_top_bar_custom_color_clicked), data);

  return menu;
}

static GtkWidget *
paper_top_bar_build_thickness_menu (ScreenatorData *data)
{
  GtkWidget *menu = gtk_menu_new ();
  GSList *group = NULL;
  static const guint widths[] = { 1, 2, 3, 4, 5, 6, 12, 24, 48 };
  GtkWidget *items[G_N_ELEMENTS (widths)];
  GtkWidget *active_item = NULL;
  guint i, closest = 0;
  guint closest_diff = ABS ((gint) data->pen_width - (gint) widths[0]);
  gint icon_size = MAX (24, MIN (paper_top_bar_icon_size (data), 40));

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
      items[i] = paper_add_thickness_item (menu, &group, widths[i],
					   icon_size);
      if (i == closest)
	active_item = items[i];
    }

  if (active_item)
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item), TRUE);

  for (i = 0; i < G_N_ELEMENTS (widths); i++)
    g_signal_connect (items[i], "activate",
		      G_CALLBACK (paper_top_bar_thickness_clicked), data);

  return menu;
}

static void
paper_top_bar_eraser_size_clicked (GtkMenuItem *item, gpointer user_data)
{
  on_icon_eraser_size (item, user_data);
  paper_top_bar_update_images ((ScreenatorData *) user_data);
}

static GtkWidget *
paper_top_bar_build_eraser_size_menu (ScreenatorData *data)
{
  GtkWidget *menu = gtk_menu_new ();
  GSList *group = NULL;
  static const guint widths[] = { 20, 40, 60, 120 };
  GtkWidget *items[G_N_ELEMENTS (widths)];
  GtkWidget *active_item = NULL;
  guint i, closest = 0;
  guint closest_diff = ABS ((gint) data->eraser_width - (gint) widths[0]);
  gint icon_size = MAX (24, MIN (paper_top_bar_icon_size (data), 40));

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
      items[i] = paper_add_eraser_size_item (menu, &group, widths[i],
					     icon_size);
      if (i == closest)
	active_item = items[i];
    }

  if (active_item)
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item), TRUE);

  for (i = 0; i < G_N_ELEMENTS (widths); i++)
    g_signal_connect (items[i], "activate",
		      G_CALLBACK (paper_top_bar_eraser_size_clicked), data);

  return menu;
}

static GtkWidget *
paper_top_bar_build_opacity_menu (ScreenatorData *data)
{
  GtkWidget *menu = gtk_menu_new ();
  GSList *group = NULL;
  static const guint percents[] =
    { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
  GtkWidget *items[G_N_ELEMENTS (percents)];
  GtkWidget *active_item = NULL;
  guint i, closest = 0;
  gdouble closest_diff = ABS (data->opacity - percents[0] / 100.0);

  for (i = 1; i < G_N_ELEMENTS (percents); i++)
    {
      gdouble diff = ABS (data->opacity - percents[i] / 100.0);

      if (diff < closest_diff)
	{
	  closest = i;
	  closest_diff = diff;
	}
    }

  for (i = 0; i < G_N_ELEMENTS (percents); i++)
    {
      gchar *label = g_strdup_printf ("%u%%", percents[i]);
      GtkWidget *item = gtk_radio_menu_item_new_with_label (group, label);

      g_free (label);
      group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
      g_object_set_data (G_OBJECT (item), "icon-opacity",
			 GINT_TO_POINTER ((gint) percents[i]));
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
      items[i] = item;
      if (i == closest)
	active_item = item;
    }

  if (active_item)
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item), TRUE);

  for (i = 0; i < G_N_ELEMENTS (percents); i++)
    g_signal_connect (items[i], "activate",
		      G_CALLBACK (on_icon_opacity), data);

  return menu;
}

static void
paper_top_bar_marker_opacity_clicked (GtkMenuItem *item, gpointer user_data)
{
  ScreenatorData *data = user_data;
  guint percent = GPOINTER_TO_UINT (
    g_object_get_data (G_OBJECT (item), "marker-opacity"));

  data->marker_opacity = percent / 100.0;
  data->marker_color.alpha = data->marker_opacity;

  if (data->default_marker)
    gdk_cairo_set_source_rgba (data->default_marker->paint_ctx,
			       &data->marker_color);

  write_keyfile (data);
  schedule_tray_menu_refresh (data);
}

static GtkWidget *
paper_top_bar_build_marker_opacity_menu (ScreenatorData *data)
{
  GtkWidget *menu = gtk_menu_new ();
  GSList *group = NULL;
  static const guint percents[] =
    { 10, 20, 30, 40, 50, 60, 70, 80, 90 };
  GtkWidget *items[G_N_ELEMENTS (percents)];
  GtkWidget *active_item = NULL;
  guint i, closest = 0;
  gdouble closest_diff = ABS (data->marker_opacity - percents[0] / 100.0);

  for (i = 1; i < G_N_ELEMENTS (percents); i++)
    {
      gdouble diff = ABS (data->marker_opacity - percents[i] / 100.0);

      if (diff < closest_diff)
	{
	  closest = i;
	  closest_diff = diff;
	}
    }

  for (i = 0; i < G_N_ELEMENTS (percents); i++)
    {
      gchar *label = g_strdup_printf ("%u%%", percents[i]);
      GtkWidget *item = gtk_radio_menu_item_new_with_label (group, label);

      g_free (label);
      group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
      g_object_set_data (G_OBJECT (item), "marker-opacity",
			 GINT_TO_POINTER ((gint) percents[i]));
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
      items[i] = item;
      if (i == closest)
	active_item = item;
    }

  if (active_item)
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (active_item), TRUE);

  for (i = 0; i < G_N_ELEMENTS (percents); i++)
    g_signal_connect (items[i], "activate",
		      G_CALLBACK (paper_top_bar_marker_opacity_clicked), data);

  return menu;
}

static GtkWidget *
paper_top_bar_build_paper_type_menu (ScreenatorData *data)
{
  GtkWidget *menu = gtk_menu_new ();
  GSList *group = NULL;
  static const struct
  {
    const gchar        *label;
    ScreenatorPaperType type;
  } papers[] =
    {
      { "Plain", SCREENATOR_PAPER_PLAIN },
      { "Ruled", SCREENATOR_PAPER_RULED },
      { "Graph", SCREENATOR_PAPER_GRAPH },
      { "PDF",   SCREENATOR_PAPER_PDF }
    };
  GtkWidget *items[G_N_ELEMENTS (papers)];
  guint i;

  for (i = 0; i < G_N_ELEMENTS (papers); i++)
    {
      GtkWidget *subitem =
	gtk_radio_menu_item_new_with_label (group, papers[i].label);

      group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (subitem));
      g_object_set_data (G_OBJECT (subitem), "icon-paper-type",
			 GINT_TO_POINTER (papers[i].type));
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), subitem);
      items[i] = subitem;

      if (papers[i].type == data->paper_selected)
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (subitem), TRUE);
    }

  for (i = 0; i < G_N_ELEMENTS (papers); i++)
    g_signal_connect (items[i], "activate",
		      G_CALLBACK (on_icon_paper), data);

  {
    GtkWidget *sep = gtk_separator_menu_item_new ();
    GtkWidget *open = gtk_image_menu_item_new_with_label ("Reset/Open PDF.");

    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (open),
                                   gtk_image_new_from_pixbuf (
                                     load_menu_icon ("paper.png")));
    g_signal_connect (open, "activate",
		      G_CALLBACK (on_icon_reset_or_open_pdf), data);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), sep);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), open);
  }

  return menu;
}

static void
paper_top_bar_paper_clicked (GtkButton *button, gpointer user_data)
{
  ScreenatorData *data = user_data;
  GtkWidget *menu = paper_top_bar_build_paper_type_menu (data);

  g_signal_connect_after (menu, "selection-done",
			  G_CALLBACK (gtk_widget_destroy), NULL);
  paper_top_bar_popup_menu (data, GTK_WIDGET (button), menu);
}

static void
paper_top_bar_add_before_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  screenator_paper_insert_page ((ScreenatorData *) user_data, TRUE);
}

static void
paper_top_bar_add_after_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  screenator_paper_insert_page ((ScreenatorData *) user_data, FALSE);
}

static void
paper_top_bar_delete_page_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  screenator_paper_delete_page ((ScreenatorData *) user_data);
}

static void
paper_top_bar_goto_page_clicked (GtkButton *button, gpointer user_data)
{
  (void) button;
  paper_show_goto_page_dialog ((ScreenatorData *) user_data);
}

static GtkWidget *
paper_top_bar_menu_button (ScreenatorData *data, GdkPixbuf *pixbuf,
			   const gchar *tooltip, GtkWidget *menu)
{
  GtkWidget *button = paper_top_bar_button (data, pixbuf, tooltip);

  g_object_set_data_full (G_OBJECT (button), "paper-menu", menu,
			  (GDestroyNotify) gtk_widget_destroy);
  g_object_set_data (G_OBJECT (button), "paper-data", data);
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_menu_button_clicked), data);
  g_signal_connect (menu, "selection-done",
		    G_CALLBACK (paper_top_bar_menu_finished), data);
  g_signal_connect (menu, "destroy",
		    G_CALLBACK (paper_top_bar_menu_finished), data);
  return button;
}

/* The clicked handler needs both the button and the data; store the data in
 * the button and use a small trampoline. */
static GtkWidget *
paper_top_bar_build_setting_menu (ScreenatorData *data, const gchar *kind)
{
  if (g_strcmp0 (kind, "line") == 0)
    return paper_top_bar_build_line_menu (data);
  if (g_strcmp0 (kind, "thickness") == 0)
    return paper_top_bar_build_thickness_menu (data);
  if (g_strcmp0 (kind, "fill") == 0)
    return paper_top_bar_build_fill_menu (data);
  if (g_strcmp0 (kind, "eraser_size") == 0)
    return paper_top_bar_build_eraser_size_menu (data);
  if (g_strcmp0 (kind, "opacity") == 0)
    return paper_top_bar_build_opacity_menu (data);
  if (g_strcmp0 (kind, "marker_opacity") == 0)
    return paper_top_bar_build_marker_opacity_menu (data);

  return NULL;
}

static void
paper_top_bar_menu_button_clicked (GtkButton *button, gpointer user_data)
{
  ScreenatorData *data = g_object_get_data (G_OBJECT (button), "paper-data");
  const gchar *kind = g_object_get_data (G_OBJECT (button),
					 "paper-menu-kind");
  GtkWidget *menu = g_object_get_data (G_OBJECT (button), "paper-menu");

  (void) user_data;

  /* Colour, thickness, fill, eraser size and opacity menus are rebuilt each
   * time they open.  A menu created once would keep its old radio selection
   * after the wheel or stylus side button changed the setting. */
  if (kind && data)
    {
      GtkWidget *fresh = paper_top_bar_build_setting_menu (data, kind);

      if (fresh)
	{
	  if (menu)
	    {
	      g_object_set_data (G_OBJECT (button), "paper-menu", NULL);
	      gtk_widget_destroy (menu);
	    }

	  menu = fresh;
	  g_object_set_data_full (G_OBJECT (button), "paper-menu", menu,
				  (GDestroyNotify) gtk_widget_destroy);
	  g_signal_connect (menu, "selection-done",
			    G_CALLBACK (paper_top_bar_menu_finished), data);
	  g_signal_connect (menu, "destroy",
			    G_CALLBACK (paper_top_bar_menu_finished), data);
	}
    }

  if (menu)
    paper_top_bar_popup_menu (data, GTK_WIDGET (button), menu);
}

static void
paper_top_bar_update_images (ScreenatorData *data)
{
  PaperTopBar *bar = data->paper_top_bar_data;
  gint size;
  GdkRGBA color;
  GdkPixbuf *pixbuf;

  if (!bar)
    return;

  size = paper_top_bar_icon_size (data);

  if (bar->line_image)
    {
      color = data->pen_color;
      pixbuf = paper_color_swatch (&color, FALSE, size);
      gtk_image_set_from_pixbuf (GTK_IMAGE (bar->line_image), pixbuf);
      if (pixbuf)
	g_object_unref (pixbuf);
    }

  if (bar->fill_image)
    {
      if (data->shape_fill_enabled)
	pixbuf = paper_color_swatch (&data->shape_fill_color, FALSE, size);
      else
	pixbuf = paper_color_swatch (&data->shape_fill_color, TRUE, size);
      gtk_image_set_from_pixbuf (GTK_IMAGE (bar->fill_image), pixbuf);
      if (pixbuf)
	g_object_unref (pixbuf);
    }

  if (bar->thickness_image)
    {
      pixbuf = paper_thickness_sample (MAX (1, (guint) data->pen_width), size);
      gtk_image_set_from_pixbuf (GTK_IMAGE (bar->thickness_image), pixbuf);
      if (pixbuf)
	g_object_unref (pixbuf);
    }

  if (bar->eraser_image)
    {
      pixbuf = paper_eraser_size_sample (MAX (1, (guint) data->eraser_width),
					 size);
      gtk_image_set_from_pixbuf (GTK_IMAGE (bar->eraser_image), pixbuf);
      if (pixbuf)
	g_object_unref (pixbuf);
    }

  paper_top_bar_update_active_tool (data);
}

static void
paper_toolbar_set_active (GtkWidget *button, gboolean active)
{
  GtkStyleContext *context;

  if (!button)
    return;

  context = gtk_widget_get_style_context (button);
  if (active)
    gtk_style_context_add_class (context, "paper-toolbar-button-active");
  else
    gtk_style_context_remove_class (context, "paper-toolbar-button-active");
}

static void
paper_top_bar_update_active_tool (ScreenatorData *data)
{
  PaperTopBar *bar = data->paper_top_bar_data;
  gboolean cursor, pen, text, eraser, arrow, timer, shapes;

  if (!bar)
    return;

  cursor = !data->timer_active && data->forced_context == NULL;
  timer = data->timer_active;
  pen = text = eraser = arrow = shapes = FALSE;

  if (!cursor && !timer)
    {
      switch (data->pen_tool)
	{
	case 1:
	  pen = TRUE;
	  break;
	case 2:
	  arrow = TRUE;
	  break;
	case 3:
	  eraser = TRUE;
	  break;
	case 4:
	  text = TRUE;
	  break;
	default:
	  if (data->pen_tool >= 5)
	    shapes = TRUE;
	  break;
	}
    }

  /* The Cursor tool is available in both modes; page insertion only makes
   * sense in Paper mode. */
  paper_toolbar_set_visible (bar->cursor_button, TRUE);
  paper_toolbar_set_visible (bar->add_before_button,
			     data->paper != SCREENATOR_PAPER_NONE);
  paper_toolbar_set_visible (bar->add_after_button,
			     data->paper != SCREENATOR_PAPER_NONE);

  paper_toolbar_set_visible (bar->delete_page_button,
			     data->paper != SCREENATOR_PAPER_NONE);
  paper_toolbar_set_visible (bar->goto_page_button,
			     data->paper != SCREENATOR_PAPER_NONE);
  paper_toolbar_set_visible (bar->page_number_button,
			     data->paper != SCREENATOR_PAPER_NONE);

  /* Pages can only be inserted/deleted in an active Plain, Ruled or Graph
   * paper. */
  {
    gboolean can_insert =
      data->paper != SCREENATOR_PAPER_NONE &&
      data->paper != SCREENATOR_PAPER_PDF;
    ScreenatorPaperDocument *doc = paper_active_document (data);
    guint page_count = (doc && doc->pages) ? doc->pages->len : 0;

    if (bar->add_before_button)
      gtk_widget_set_sensitive (bar->add_before_button, can_insert);
    if (bar->delete_page_button)
      gtk_widget_set_sensitive (bar->delete_page_button,
				can_insert && page_count > 1);
    if (bar->add_after_button)
      gtk_widget_set_sensitive (bar->add_after_button, can_insert);
    if (bar->goto_page_button)
      gtk_widget_set_sensitive (bar->goto_page_button,
				data->paper != SCREENATOR_PAPER_NONE &&
				page_count > 1);
    if (bar->page_number_button && bar->page_number_label)
      {
	gchar *text;
	gchar *tip;

	if (data->paper != SCREENATOR_PAPER_NONE && doc && page_count > 0)
	  {
	    text = g_strdup_printf ("%u", doc->page + 1);
	    tip = g_strdup_printf ("Current page: %u of %u",
				   doc->page + 1, page_count);
	  }
	else
	  {
	    text = g_strdup ("-");
	    tip = g_strdup ("Current page");
	  }

	gtk_label_set_text (GTK_LABEL (bar->page_number_label), text);
	paper_toolbar_set_tooltip_text (bar->page_number_button, tip);
	gtk_widget_set_sensitive (bar->page_number_button,
				  data->paper != SCREENATOR_PAPER_NONE &&
				  page_count > 1);
	g_free (text);
	g_free (tip);
      }
  }

  if (bar->save_button && bar->save_image)
    {
      gboolean screen = data->paper == SCREENATOR_PAPER_NONE;
      GdkPixbuf *icon = load_menu_icon_at_size (
	screen ? "screenshot.png" : "save.png",
	paper_top_bar_icon_size (data));

      if (icon)
	{
	  gtk_image_set_from_pixbuf (GTK_IMAGE (bar->save_image), icon);
	  g_object_unref (icon);
	}

      paper_toolbar_set_tooltip_text (
	bar->save_button,
	screen ? "Screenshot (F4)" : "Save current paper (Ctrl+S)");
    }

  paper_toolbar_set_active (bar->cursor_button, cursor);
  paper_toolbar_set_active (bar->pen_button, pen);
  paper_toolbar_set_active (bar->text_button, text);
  paper_toolbar_set_active (bar->eraser_button, eraser);
  paper_toolbar_set_active (bar->arrow_button, arrow);
  paper_toolbar_set_active (bar->timer_button, timer);
  paper_toolbar_set_active (bar->shapes_button, shapes);

  if (bar->timer_button)
    paper_toolbar_set_visible (bar->timer_button,
			       data->paper == SCREENATOR_PAPER_NONE);

  /* Toggle Visibility is useful in Screen Annotation mode; in Paper Drawing
   * the tool bar always hides it. */
  if (bar->visibility_button)
    paper_toolbar_set_visible (bar->visibility_button,
			       data->paper == SCREENATOR_PAPER_NONE);
}

static void
paper_top_bar_destroyed (GtkWidget *widget, gpointer user_data)
{
  PaperTopBar *bar = user_data;
  ScreenatorData *data = bar->data;

  if (bar->tooltip_timeout)
    {
      g_source_remove (bar->tooltip_timeout);
      bar->tooltip_timeout = 0;
    }
  bar->tooltip_button = NULL;

  if (data->paper_top_bar == widget)
    data->paper_top_bar = NULL;
  if (data->paper_top_bar_data == bar)
    data->paper_top_bar_data = NULL;

  if (bar->tooltip_window)
    gtk_widget_destroy (bar->tooltip_window);

  g_free (bar);
}

static void
paper_top_bar_position (PaperTopBar *bar)
{
  ScreenatorData *data = bar->data;
  GdkScreen *screen = gtk_widget_get_screen (bar->window);
  GdkRectangle area;
  GtkRequisition req;
  gint monitor;
  gint x, y;

  if (!screen)
    return;

  monitor = gdk_screen_get_primary_monitor (screen);
  if (monitor < 0)
    monitor = 0;

  gdk_screen_get_monitor_workarea (screen, monitor, &area);
  gtk_widget_get_preferred_size (bar->window, NULL, &req);

  if (data->paper_toolbar_vertical)
    {
      /* Vertical bars sit on the right edge, centred on the screen. */
      x = area.x + MAX (0, area.width - req.width - PAPER_TOOLBAR_MARGIN);
      y = area.y + MAX (0, (area.height - req.height) / 2);
    }
  else
    {
      /* Horizontal bars sit on the top edge, centred on the screen. */
      x = area.x + MAX (0, (area.width - req.width) / 2);
      y = area.y + PAPER_TOOLBAR_MARGIN;
    }

  gtk_window_move (GTK_WINDOW (bar->window), x, y);
}

static void
paper_top_bar_reposition (ScreenatorData *data)
{
  if (data->paper_top_bar && data->paper_top_bar_data)
    paper_top_bar_position ((PaperTopBar *) data->paper_top_bar_data);
}

static gboolean
paper_top_bar_map (GtkWidget *window, GdkEvent *event, gpointer user_data)
{
  PaperTopBar *bar = user_data;
  GdkWindow *gdk_window = gtk_widget_get_window (window);

  (void) event;
  paper_top_bar_position (bar);
  if (gdk_window)
    gdk_window_raise (gdk_window);
  return FALSE;
}

#define PAPER_TOOLBAR_DRAG_THRESHOLD 4

static gboolean
paper_toolbar_drag_press (GtkWidget *widget, GdkEventButton *event,
			  gpointer user_data)
{
  PaperTopBar *bar = user_data;

  (void) widget;

  if (event->button != 1 || bar->drag_pending)
    return FALSE;

  paper_toolbar_tooltip_hide (bar->data);

  bar->drag_pending = TRUE;
  bar->dragging = FALSE;
  bar->drag_was_grabbed = FALSE;
  bar->drag_seat_grabbed = FALSE;
  bar->drag_widget = widget;
  bar->drag_start_root_x = (gint) event->x_root;
  bar->drag_start_root_y = (gint) event->y_root;
  gtk_window_get_position (GTK_WINDOW (bar->window),
			   &bar->drag_window_x, &bar->drag_window_y);
  return FALSE;
}

static void
paper_toolbar_drag_finish (PaperTopBar *bar)
{
  ScreenatorData *data = bar->data;
  GdkWindow *window = gtk_widget_get_window (bar->window);
  GdkDisplay *display = window ? gdk_window_get_display (window)
			       : gdk_display_get_default ();
  GdkSeat *seat = display ? gdk_display_get_default_seat (display) : NULL;

  if (bar->drag_seat_grabbed && seat)
    gdk_seat_ungrab (seat);
  bar->drag_seat_grabbed = FALSE;
  bar->drag_pending = FALSE;
  bar->dragging = FALSE;

  if (bar->drag_widget)
    {
      gtk_widget_unset_state_flags (bar->drag_widget,
				    GTK_STATE_FLAG_ACTIVE);
      bar->drag_widget = NULL;
    }

  if (bar->drag_was_grabbed)
    {
      bar->drag_was_grabbed = FALSE;
      paper_refresh_controls_hover (data);
      acquire_grab (data, NULL);
      paper_top_bar_update_active_tool (data);
    }
}

static gboolean
paper_toolbar_drag_motion (GtkWidget *widget, GdkEventMotion *event,
			   gpointer user_data)
{
  PaperTopBar *bar = user_data;
  ScreenatorData *data = bar->data;
  gint dx, dy;

  (void) widget;

  if (!bar->drag_pending || !(event->state & GDK_BUTTON1_MASK))
    return FALSE;

  dx = (gint) event->x_root - bar->drag_start_root_x;
  dy = (gint) event->y_root - bar->drag_start_root_y;

  if (!bar->dragging)
    {
      if (ABS (dx) < PAPER_TOOLBAR_DRAG_THRESHOLD &&
	  ABS (dy) < PAPER_TOOLBAR_DRAG_THRESHOLD)
	return FALSE;

      bar->dragging = TRUE;
      if (gtk_widget_get_window (bar->window))
	gdk_window_raise (gtk_widget_get_window (bar->window));

      bar->drag_was_grabbed = screenator_any_grabbed (data);
      if (bar->drag_was_grabbed)
	release_grab (data, NULL);

      {
	GdkWindow *window = gtk_widget_get_window (bar->window);
	GdkDisplay *display = window ? gdk_window_get_display (window)
				     : gdk_display_get_default ();
	GdkSeat *seat = display ? gdk_display_get_default_seat (display)
				: NULL;

	if (window && seat &&
	    gdk_seat_grab (seat, window, GDK_SEAT_CAPABILITY_POINTER, FALSE,
			   NULL, NULL, NULL, NULL) == GDK_GRAB_SUCCESS)
	  bar->drag_seat_grabbed = TRUE;
      }
    }

  {
    GdkScreen *screen = gtk_widget_get_screen (bar->window);
    GdkRectangle area;
    GtkRequisition req;
    gint monitor;
    gint new_x = bar->drag_window_x + dx;
    gint new_y = bar->drag_window_y + dy;
    gint right, bottom;
    const gint snap = 12;

    if (screen)
      {
	monitor = gdk_screen_get_monitor_at_point (screen,
						   (gint) event->x_root,
						   (gint) event->y_root);
	if (monitor < 0)
	  monitor = 0;

	gdk_screen_get_monitor_workarea (screen, monitor, &area);
	gtk_widget_get_preferred_size (bar->window, NULL, &req);

	right = area.x + MAX (0, area.width - req.width);
	bottom = area.y + MAX (0, area.height - req.height);

	/* Snap the window borders to the monitor edges when close. */
	if (ABS (new_x - area.x) <= snap)
	  new_x = area.x;
	else if (ABS (new_x - right) <= snap)
	  new_x = right;

	if (ABS (new_y - area.y) <= snap)
	  new_y = area.y;
	else if (ABS (new_y - bottom) <= snap)
	  new_y = bottom;

	new_x = CLAMP (new_x, area.x, right);
	new_y = CLAMP (new_y, area.y, bottom);
      }

    gtk_window_move (GTK_WINDOW (bar->window), new_x, new_y);
  }
  return TRUE;
}

static gboolean
paper_toolbar_drag_release (GtkWidget *widget, GdkEventButton *event,
			    gpointer user_data)
{
  PaperTopBar *bar = user_data;

  (void) widget;

  if (event->button != 1 || !bar->drag_pending)
    return FALSE;

  if (bar->dragging)
    {
      paper_toolbar_drag_finish (bar);
      return TRUE;
    }

  bar->drag_pending = FALSE;
  bar->drag_widget = NULL;
  return FALSE;
}

static void
paper_toolbar_attach_drag (PaperTopBar *bar, GtkWidget *widget)
{
  gtk_widget_add_events (widget,
			 GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
			 GDK_POINTER_MOTION_MASK);
  g_signal_connect (widget, "button-press-event",
		    G_CALLBACK (paper_toolbar_drag_press), bar);
  g_signal_connect (widget, "motion-notify-event",
		    G_CALLBACK (paper_toolbar_drag_motion), bar);
  g_signal_connect (widget, "button-release-event",
		    G_CALLBACK (paper_toolbar_drag_release), bar);
}

/* Older configurations used "hand" for the Cursor item. */
static const gchar *
toolbar_item_id (const gchar *id)
{
  return g_strcmp0 (id, "hand") == 0 ? "cursor" : id;
}

/* Apply the configured visibility and order to the buttons that were
 * created in their default order.  Hidden buttons stay in the box but are not
 * allocated. */
static void
paper_toolbar_apply_items (ScreenatorData *data, PaperTopBar *bar)
{
  GHashTable *widgets;
  GList *children, *l;
  gchar **visible;
  gint position = 0;
  guint i, j;

  if (!bar || !bar->box)
    return;

  widgets = g_hash_table_new (g_str_hash, g_str_equal);
  children = gtk_container_get_children (GTK_CONTAINER (bar->box));

  for (l = children; l; l = l->next)
    {
      GtkWidget *button = l->data;
      const gchar *id = g_object_get_data (G_OBJECT (button),
					   "paper-toolbar-item");

      if (id)
	g_hash_table_insert (widgets, (gpointer) id, button);
    }
  g_list_free (children);

  /* Start with every known item hidden and disabled. */
  for (i = 0; i < G_N_ELEMENTS (toolbar_item_info); i++)
    {
      GtkWidget *button = g_hash_table_lookup (widgets,
					       toolbar_item_info[i].id);

      if (!button)
	continue;

      g_object_set_data (G_OBJECT (button), "paper-toolbar-enabled",
			 GINT_TO_POINTER (FALSE));
      gtk_widget_hide (button);
    }

  /* Then show the configured items in their configured order. */
  visible = g_strsplit (data->paper_toolbar_items
			? data->paper_toolbar_items
			: PAPER_TOOLBAR_DEFAULT_ITEMS, ",", -1);

  for (j = 0; visible[j]; j++)
    {
      gchar *id = g_strstrip (visible[j]);
      GtkWidget *button;

      if (!*id)
	continue;

      button = g_hash_table_lookup (widgets, toolbar_item_id (id));
      if (!button)
	continue;

      g_object_set_data (G_OBJECT (button), "paper-toolbar-enabled",
			 GINT_TO_POINTER (TRUE));
      gtk_widget_show (button);
      gtk_box_reorder_child (GTK_BOX (bar->box), button, position++);
    }

  g_strfreev (visible);
  g_hash_table_destroy (widgets);
}

/* Show a button only when it is enabled in the Toolbar settings page. */
static void
paper_toolbar_set_visible (GtkWidget *button, gboolean show)
{
  if (!button)
    return;

  if (!GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button),
					   "paper-toolbar-enabled")))
    show = FALSE;

  gtk_widget_set_visible (button, show);
}

static void
paper_top_bar_create (ScreenatorData *data)
{
  PaperTopBar *bar;
  GtkWidget *window, *box, *button, *menu;
  GdkPixbuf *pixbuf;
  gint size = paper_top_bar_icon_size (data);

  bar = g_new0 (PaperTopBar, 1);
  bar->data = data;

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  bar->window = window;
  gtk_window_set_title (GTK_WINDOW (window), "Paper Tools");
  gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
  gtk_window_set_resizable (GTK_WINDOW (window), FALSE);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
  gtk_window_set_skip_pager_hint (GTK_WINDOW (window), TRUE);
  gtk_window_set_accept_focus (GTK_WINDOW (window), FALSE);
  gtk_window_set_focus_on_map (GTK_WINDOW (window), FALSE);
  gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_NONE);
  gtk_window_set_keep_above (GTK_WINDOW (window), TRUE);
  g_signal_connect (window, "realize",
		    G_CALLBACK (on_dialog_realize), NULL);
  paper_controls_attach_hover (data, window);
  paper_toolbar_install_style (window);
  paper_toolbar_tooltip_create (bar);

  box = gtk_box_new (data->paper_toolbar_vertical
		      ? GTK_ORIENTATION_VERTICAL
		      : GTK_ORIENTATION_HORIZONTAL,
		      PAPER_TOOLBAR_SPACING);
  gtk_container_set_border_width (GTK_CONTAINER (box), 0);
  gtk_container_add (GTK_CONTAINER (window), box);
  bar->box = box;

  /* Switch Mode is the first action. */
  button = paper_top_bar_icon_button (data, "switch_mode.png",
				      "Switch Mode (F3)");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "switch_mode");
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_switch_mode_clicked), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  /* Cursor: exit the current tool and use the normal pointer. */
  button = paper_top_bar_icon_button (data, "cursor.png",
				      "Cursor (exit the current tool)");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "cursor");
  bar->cursor_button = button;
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_cursor_clicked), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  /* Pen, Text, Eraser, Arrow. */
  {
    struct
    {
      const gchar *icon;
      const gchar *tooltip;
      const gchar *id;
      gint         pen_tool;
      gint         shape_type;
    } tools[] =
      {
	{ "pen.png",    "Pen (F8)",   "pen",    1, -1 },
	{ "text.png",   "Text (F7)",  "text",   4, -1 },
	{ "eraser.png", "Eraser",     "eraser", 3, -1 },
	{ "arrow.png",  "Arrow (F6)", "arrow",  2, SCREENATOR_SHAPE_ARROW }
      };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (tools); i++)
      {
	button = paper_top_bar_icon_button (data, tools[i].icon,
					    tools[i].tooltip);
	g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
			   (gpointer) tools[i].id);
	g_object_set_data (G_OBJECT (button), "paper-tool",
			   GINT_TO_POINTER (tools[i].pen_tool));
	g_object_set_data (G_OBJECT (button), "paper-shape",
			   GINT_TO_POINTER (tools[i].shape_type));
	g_signal_connect (button, "clicked",
			  G_CALLBACK (paper_top_bar_tool_clicked), data);
	gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

	if (tools[i].pen_tool == 1)
	  bar->pen_button = button;
	else if (tools[i].pen_tool == 4)
	  bar->text_button = button;
	else if (tools[i].pen_tool == 3)
	  bar->eraser_button = button;
	else if (tools[i].pen_tool == 2)
	  bar->arrow_button = button;
      }
  }

  /* Timer tool, directly after Arrow.  It is created once so it can follow
   * mode changes, but is only visible in Screen Annotation mode. */
  button = paper_top_bar_icon_button (data, "timer.png", "Timer (F12)");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "timer");
  bar->timer_button = button;
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_timer_clicked), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
  gtk_widget_set_visible (button, data->paper == SCREENATOR_PAPER_NONE);

  /* Shapes menu, before Eraser Size. */
  menu = paper_top_bar_build_shapes_menu (data);
  {
    GdkPixbuf *icon = load_menu_icon_at_size ("shapes.png", size);

    button = paper_top_bar_menu_button (data, icon, "Shapes", menu);
    if (icon)
      g_object_unref (icon);
  }
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "shapes");
  bar->shapes_button = button;
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  /* Undo and redo, directly after Shapes. */
  button = paper_top_bar_icon_button (data, "undo.png", "Undo (Ctrl+Z)");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "undo");
  g_signal_connect (button, "clicked", G_CALLBACK (on_undo), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  button = paper_top_bar_icon_button (data, "redo.png", "Redo (Ctrl+Shift+Z)");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "redo");
  g_signal_connect (button, "clicked", G_CALLBACK (on_redo), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  /* Save the current paper in Paper mode; screenshot in Screen mode. */
  button = paper_top_bar_icon_button (data, "save.png",
				      "Save current paper (Ctrl+S)");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "save");
  bar->save_button = button;
  bar->save_image = gtk_bin_get_child (GTK_BIN (button));
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_save_clicked), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  /* Paper type menu. */
  button = paper_top_bar_icon_button (data, "paper.png", "Paper Type");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "paper");
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_paper_clicked), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  /* Add page before/after. */
  button = paper_top_bar_icon_button (data, "add_before.png",
				      "Insert Page Before (Ctrl+B)");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "insert_before");
  bar->add_before_button = button;
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_add_before_clicked), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  button = paper_top_bar_icon_button (data, "delete-page.png",
				      "Delete Page");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "delete_page");
  bar->delete_page_button = button;
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_delete_page_clicked), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  button = paper_top_bar_icon_button (data, "add_after.png",
				      "Insert Page After (Ctrl+A)");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "insert_after");
  bar->add_after_button = button;
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_add_after_clicked), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  button = paper_top_bar_page_number_button (data);
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "page_number");
  bar->page_number_button = button;
  bar->page_number_label = gtk_bin_get_child (GTK_BIN (button));
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_goto_page_clicked), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  button = paper_top_bar_icon_button (data, "goto-page.png", "Go to Page");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "goto_page");
  bar->goto_page_button = button;
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_goto_page_clicked), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  /* Clear Screen and Toggle Visibility, after Shapes. */
  button = paper_top_bar_icon_button (data, "trash.png", "Clear Screen");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "clear");
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_clear_clicked), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  button = paper_top_bar_icon_button (data, "hide.png", "Toggle Visibility");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "visibility");
  bar->visibility_button = button;
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_visibility_clicked), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  /* Eraser size menu. */
  {
    pixbuf = paper_eraser_size_sample (MAX (1, (guint) data->eraser_width),
				       size);
    menu = paper_top_bar_build_eraser_size_menu (data);
    button = paper_top_bar_menu_button (data, pixbuf, "Eraser Size", menu);
    if (pixbuf)
      g_object_unref (pixbuf);
    g_object_set_data (G_OBJECT (button), "paper-menu-kind",
		       (gpointer) "eraser_size");
    g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		       (gpointer) "eraser_size");
    bar->eraser_image = gtk_bin_get_child (GTK_BIN (button));
    gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
  }

  /* Line colour menu. */
  {
    GdkRGBA color = data->pen_color;

    pixbuf = paper_color_swatch (&color, FALSE, size);
    menu = paper_top_bar_build_line_menu (data);
    button = paper_top_bar_menu_button (data, pixbuf, "Line Color", menu);
    if (pixbuf)
      g_object_unref (pixbuf);
    g_object_set_data (G_OBJECT (button), "paper-menu-kind",
		       (gpointer) "line");
    g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		       (gpointer) "line_color");
    bar->line_image = gtk_bin_get_child (GTK_BIN (button));
    gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
  }

  /* Line thickness menu. */
  {
    pixbuf = paper_thickness_sample (MAX (1, (guint) data->pen_width), size);
    menu = paper_top_bar_build_thickness_menu (data);
    button = paper_top_bar_menu_button (data, pixbuf, "Thickness", menu);
    if (pixbuf)
      g_object_unref (pixbuf);
    g_object_set_data (G_OBJECT (button), "paper-menu-kind",
		       (gpointer) "thickness");
    g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		       (gpointer) "thickness");
    bar->thickness_image = gtk_bin_get_child (GTK_BIN (button));
    gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
  }

  /* Shapes fill colour menu. */
  {
    if (data->shape_fill_enabled)
      pixbuf = paper_color_swatch (&data->shape_fill_color, FALSE, size);
    else
      pixbuf = paper_color_swatch (&data->shape_fill_color, TRUE, size);
    menu = paper_top_bar_build_fill_menu (data);
    button = paper_top_bar_menu_button (data, pixbuf, "Fill Color", menu);
    if (pixbuf)
      g_object_unref (pixbuf);
    g_object_set_data (G_OBJECT (button), "paper-menu-kind",
		       (gpointer) "fill");
    g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		       (gpointer) "fill_color");
    bar->fill_image = gtk_bin_get_child (GTK_BIN (button));
    gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
  }

  /* Line opacity menu. */
  menu = paper_top_bar_build_opacity_menu (data);
  {
    GdkPixbuf *icon = load_menu_icon_at_size ("opacity.png", size);

    button = paper_top_bar_menu_button (data, icon, "Opacity", menu);
    if (icon)
      g_object_unref (icon);
  }
  g_object_set_data (G_OBJECT (button), "paper-menu-kind",
		     (gpointer) "opacity");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "opacity");
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  /* Separate Marker Opacity menu. */
  menu = paper_top_bar_build_marker_opacity_menu (data);
  {
    GdkPixbuf *icon = load_menu_icon_at_size ("marker_opacity.png", size);

    button = paper_top_bar_menu_button (data, icon, "Marker Opacity", menu);
    if (icon)
      g_object_unref (icon);
  }
  g_object_set_data (G_OBJECT (button), "paper-menu-kind",
		     (gpointer) "marker_opacity");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "marker_opacity");
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  button = paper_top_bar_icon_button (data, "settings.png", "Settings");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "settings");
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_settings_clicked), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  button = paper_top_bar_icon_button (data, "hint.png", "Show Hints");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "hints");
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_hints_clicked), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  button = paper_top_bar_icon_button (data, "quit.png", "Quit");
  g_object_set_data (G_OBJECT (button), "paper-toolbar-item",
		     (gpointer) "quit");
  g_signal_connect (button, "clicked",
		    G_CALLBACK (paper_top_bar_quit_clicked), data);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);

  data->paper_top_bar = window;
  data->paper_top_bar_data = bar;
  paper_top_bar_update_active_tool (data);

  g_signal_connect (window, "map-event",
		    G_CALLBACK (paper_top_bar_map), bar);
  g_signal_connect (window, "destroy",
		    G_CALLBACK (paper_top_bar_destroyed), bar);
  gtk_widget_set_has_tooltip (window, TRUE);
  g_signal_connect (window, "query-tooltip",
		    G_CALLBACK (paper_toolbar_query_tooltip), bar);

  /* Dragging any icon (or the gaps between them) moves the bar.  A short
   * click still reaches the button, because the drag starts only after the
   * pointer has moved past the threshold. */
  paper_toolbar_attach_drag (bar, window);
  {
    GList *children = gtk_container_get_children (GTK_CONTAINER (box));
    GList *l;

    for (l = children; l; l = l->next)
      paper_toolbar_attach_drag (bar, GTK_WIDGET (l->data));
    g_list_free (children);
  }

  gtk_widget_show_all (window);
  paper_toolbar_apply_items (data, bar);
  paper_top_bar_update_active_tool (data);
  paper_top_bar_position (bar);
  if (gtk_widget_get_window (window))
    gdk_window_raise (gtk_widget_get_window (window));
}

static void
paper_top_bar_regrab (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;

  if (!data->devdatatable)
    return;

  if (data->paper_panning)
    {
      set_paper_pan_cursor (data);
      return;
    }

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      ScreenatorDeviceData *devdata = value;

      if (devdata->is_grabbed)
	set_device_cursor (data, devdata->device, devdata->cur_context);
    }

  if (data->paper_hand_tool)
    set_paper_pan_cursor (data);
}

static void
paper_top_bar_destroy (ScreenatorData *data)
{
  if (data->paper_top_bar)
    gtk_widget_destroy (data->paper_top_bar);

  /* The bar is gone, so events over the paper must go to the drawing window
   * again even when another Screenator window sits above it. */
  paper_top_bar_regrab (data);
}

static void
paper_top_bar_rebuild (ScreenatorData *data)
{
  if (!data->paper_top_bar)
    return;

  paper_top_bar_destroy (data);
  paper_top_bar_create (data);
  paper_top_bar_regrab (data);
}

/* Reopen the tool bar when Paper Drawing is entered again if it was open
 * before, or at startup when it was left open. */
static void
paper_top_bar_restore (ScreenatorData *data)
{
  if (!data->paper_toolbar_open || data->paper_top_bar)
    return;

  paper_top_bar_create (data);
  paper_top_bar_regrab (data);
}

/* F10 in Paper Drawing: show or hide the persistent tool bar. */
void
screenator_toggle_paper_top_bar (ScreenatorData *data)
{
  if (data->paper_top_bar)
    {
      data->paper_toolbar_open = FALSE;
      data->paper_hand_tool = FALSE;
      paper_top_bar_destroy (data);
    }
  else
    {
      data->paper_toolbar_open = TRUE;
      paper_top_bar_create (data);
      paper_top_bar_regrab (data);
    }

  write_keyfile (data);
  schedule_tray_menu_refresh (data);
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

#define HINTS_DEFAULT_COLOR "#2c3e50"

/* The hints are plain HTML so they can be edited without rebuilding the
 * program.  Only a small, convenient subset is needed: headings, paragraphs,
 * lists, emphasis and coloured spans. */
static gboolean
hints_safe_color (const gchar *color)
{
  const gchar *p = color;

  if (!p || *p != '#')
    return FALSE;

  for (p++; *p; p++)
    if (!g_ascii_isxdigit (*p))
      return FALSE;

  return p != color + 1;
}

static gboolean
hints_tag_color (const gchar *tag, gchar *out, gsize out_size)
{
  const gchar *p = strchr (tag, '#');
  gsize n = 0;

  if (!p || out_size < 2)
    return FALSE;

  out[n++] = '#';
  for (p++; *p && g_ascii_isxdigit (*p) && n + 1 < out_size; p++)
    out[n++] = *p;
  out[n] = '\0';

  return n > 1;
}

static void
hints_append_escaped (GString *out, gunichar c)
{
  if (c == '&')
    g_string_append (out, "&amp;");
  else if (c == '<')
    g_string_append (out, "&lt;");
  else if (c == '>')
    g_string_append (out, "&gt;");
  else
    g_string_append_unichar (out, c);
}

static gboolean
hints_decode_entity (const gchar *text, gsize *consumed, gunichar *value)
{
  if (g_str_has_prefix (text, "&amp;"))
    { *consumed = 5; *value = '&'; return TRUE; }
  if (g_str_has_prefix (text, "&lt;"))
    { *consumed = 4; *value = '<'; return TRUE; }
  if (g_str_has_prefix (text, "&gt;"))
    { *consumed = 4; *value = '>'; return TRUE; }
  if (g_str_has_prefix (text, "&quot;"))
    { *consumed = 6; *value = '"'; return TRUE; }
  if (g_str_has_prefix (text, "&apos;"))
    { *consumed = 6; *value = '\''; return TRUE; }
  if (g_str_has_prefix (text, "&nbsp;"))
    { *consumed = 6; *value = 0x00a0; return TRUE; }

  if (g_str_has_prefix (text, "&#x") || g_str_has_prefix (text, "&#X"))
    {
      const gchar *p = text + 3;
      gunichar value_int = 0;
      gboolean any = FALSE;

      while (g_ascii_isxdigit (*p))
	{
	  value_int = value_int * 16 + (gunichar) g_ascii_xdigit_value (*p);
	  p++;
	  any = TRUE;
	}

      if (any && *p == ';')
	{
	  *consumed = (gsize) (p - text) + 1;
	  *value = value_int;
	  return TRUE;
	}
    }
  else if (g_str_has_prefix (text, "&#"))
    {
      const gchar *p = text + 2;
      gunichar value_int = 0;
      gboolean any = FALSE;

      while (g_ascii_isdigit (*p))
	{
	  value_int = value_int * 10 + (gunichar) (*p - '0');
	  p++;
	  any = TRUE;
	}

      if (any && *p == ';')
	{
	  *consumed = (gsize) (p - text) + 1;
	  *value = value_int;
	  return TRUE;
	}
    }

  return FALSE;
}

/* Collapse source-formatting whitespace without destroying the line breaks
 * that the tags themselves inserted. */
static gchar *
hints_cleanup_markup (GString *out)
{
  GString *clean = g_string_new (NULL);
  const gchar *p = out->str;
  gint newlines = 0;
  gboolean last_space = FALSE;

  while (*p)
    {
      if (*p == ' ')
	{
	  if (p[1] == '\n' || p[1] == '\0' || last_space)
	    {
	      p++;
	      continue;
	    }

	  g_string_append_c (clean, ' ');
	  last_space = TRUE;
	  p++;
	}
      else if (*p == '\n')
	{
	  while (clean->len > 0 && clean->str[clean->len - 1] == ' ')
	    g_string_truncate (clean, clean->len - 1);

	  if (newlines < 2)
	    {
	      g_string_append_c (clean, '\n');
	      newlines++;
	    }

	  last_space = FALSE;
	  p++;
	}
      else
	{
	  g_string_append_c (clean, *p);
	  last_space = FALSE;
	  newlines = 0;
	  p++;
	}
    }

  while (clean->len > 0 &&
	 g_ascii_isspace (clean->str[clean->len - 1]))
    g_string_truncate (clean, clean->len - 1);

  while (clean->len > 0 && g_ascii_isspace (clean->str[0]))
    g_string_erase (clean, 0, 1);

  g_string_free (out, TRUE);
  return g_string_free (clean, FALSE);
}

typedef enum
{
  HINTS_TAG_B = 0,
  HINTS_TAG_I,
  HINTS_TAG_U,
  HINTS_TAG_TT,
  HINTS_TAG_SPAN
} HintsMarkupTag;

static const gchar * const hints_markup_open[] = {
  "<b>", "<i>", "<u>", "<tt>", "<span>"
};
static const gchar * const hints_markup_close[] = {
  "</b>", "</i>", "</u>", "</tt>", "</span>"
};

static void
hints_markup_push (GArray *stack, HintsMarkupTag tag, GString *out)
{
  HintsMarkupTag value = tag;

  g_array_append_val (stack, value);
  g_string_append (out, hints_markup_open[tag]);
}

/* Close only a tag that is actually open and is the innermost element.  This
 * keeps the result valid even when the hand-edited HTML has an extra closing
 * tag or crossed nesting. */
static void
hints_markup_pop (GArray *stack, HintsMarkupTag tag, GString *out)
{
  if (!stack || stack->len == 0)
    return;

  if (g_array_index (stack, HintsMarkupTag, stack->len - 1) != tag)
    return;

  g_string_append (out, hints_markup_close[tag]);
  g_array_set_size (stack, stack->len - 1);
}

static gchar *
hints_css_color_from_declarations (const gchar *declarations)
{
  gchar **parts = g_strsplit (declarations, ";", -1);
  gchar *result = NULL;
  guint i;

  for (i = 0; parts[i] && !result; i++)
    {
      gchar *colon = strchr (parts[i], ':');
      gchar *property;
      gchar *value;
      gchar *hash;

      if (!colon)
	continue;

      *colon = '\0';
      property = g_strstrip (parts[i]);
      if (g_ascii_strcasecmp (property, "color") != 0)
	continue;

      value = g_strstrip (colon + 1);
      hash = strchr (value, '#');
      if (hash)
	{
	  gchar color[16];
	  gsize n = 0;

	  color[n++] = '#';
	  for (hash++; *hash && g_ascii_isxdigit (*hash) && n + 1 < sizeof color;
	       hash++)
	    color[n++] = *hash;
	  color[n] = '\0';

	  if (hints_safe_color (color))
	    result = g_strdup (color);
	}
    }

  g_strfreev (parts);
  return result;
}

static const gchar *
hints_strcasestr (const gchar *haystack, const gchar *needle)
{
  gchar *lower_haystack = g_ascii_strdown (haystack, -1);
  gchar *lower_needle = g_ascii_strdown (needle, -1);
  gchar *match = g_strstr_len (lower_haystack, -1, lower_needle);
  const gchar *result = match
    ? haystack + (match - lower_haystack) : NULL;

  g_free (lower_haystack);
  g_free (lower_needle);
  return result;
}

/* Read simple <style> rules.  The hints file is meant to be hand-edited, so a
 * tiny tag-selector/color parser is enough and avoids a CSS dependency. */
static void
hints_parse_styles (const gchar *html, GHashTable *css)
{
  const gchar *p = html;

  while (p && (p = hints_strcasestr (p, "<style")))
    {
      const gchar *open = strchr (p, '>');
      const gchar *close;
      gchar *text;
      gchar **rules;
      guint i;

      if (!open)
	break;

      close = hints_strcasestr (open + 1, "</style>");
      if (!close)
	break;

      text = g_strndup (open + 1, (gsize) (close - (open + 1)));
      rules = g_strsplit (text, "}", -1);

      for (i = 0; rules[i]; i++)
	{
	  gchar *brace = strchr (rules[i], '{');
	  gchar *selectors;
	  gchar *declarations;
	  gchar *color;
	  gchar **sels;
	  guint j;

	  if (!brace)
	    continue;

	  *brace = '\0';
	  selectors = g_strstrip (rules[i]);
	  declarations = g_strstrip (brace + 1);
	  color = hints_css_color_from_declarations (declarations);
	  if (!color)
	    continue;

	  sels = g_strsplit (selectors, ",", -1);
	  for (j = 0; sels[j]; j++)
	    {
	      gchar *selector = g_strstrip (sels[j]);
	      gchar *space;
	      gchar *q;
	      gchar *key;
	      gboolean valid = TRUE;

	      if (!*selector || selector[0] == '.' || selector[0] == '#')
		continue;

	      space = strrchr (selector, ' ');
	      if (space)
		selector = space + 1;

	      for (q = selector; *q; q++)
		if (!g_ascii_isalnum (*q) && *q != '-' && *q != '_')
		  {
		    valid = FALSE;
		    break;
		  }

	      if (!valid || !*selector)
		continue;

	      key = g_ascii_strdown (selector, -1);
	      g_hash_table_replace (css, key, g_strdup (color));
	    }

	  g_strfreev (sels);
	  g_free (color);
	}

      g_strfreev (rules);
      g_free (text);
      p = close + 8;
    }
}

static gchar *
hints_html_to_markup (const gchar *html)
{
  GString *out = g_string_new (NULL);
  const gchar *p = html;
  gchar list_color[16];
  GArray *open_tags = g_array_new (FALSE, FALSE, sizeof (HintsMarkupTag));
  GHashTable *css_colors = g_hash_table_new_full (g_str_hash, g_str_equal,
						  g_free, g_free);
  gboolean in_space = TRUE;
  gboolean skip_content = FALSE;

  g_strlcpy (list_color, HINTS_DEFAULT_COLOR, sizeof list_color);
  hints_parse_styles (html, css_colors);

  while (p && *p)
    {
      if (skip_content)
	{
	  p = strchr (p, '<');
	  if (!p)
	    break;
	}

      if (*p == '<')
	{
	  const gchar *end;
	  gchar *tag;
	  gchar name[16];
	  gchar color[16] = "";
	  const gchar *q;
	  gsize n = 0;
	  gboolean closing;

	  if (g_str_has_prefix (p, "<!--"))
	    {
	      end = strstr (p + 4, "-->");
	      p = end ? end + 3 : p + strlen (p);
	      continue;
	    }

	  end = strchr (p, '>');
	  if (!end)
	    break;

	  tag = g_strndup (p + 1, (gsize) (end - p - 1));
	  q = tag;
	  while (g_ascii_isspace (*q))
	    q++;
	  closing = (*q == '/');
	  if (closing)
	    q++;
	  while (*q && !g_ascii_isspace (*q) && *q != '/' &&
		 n + 1 < sizeof name)
	    name[n++] = g_ascii_tolower (*q++);
	  name[n] = '\0';

	  if (skip_content)
	    {
	      if (closing &&
		  (strcmp (name, "style") == 0 ||
		   strcmp (name, "script") == 0 ||
		   strcmp (name, "head") == 0))
		skip_content = FALSE;

	      g_free (tag);
	      p = end + 1;
	      continue;
	    }

	  if (!closing &&
	      (strcmp (name, "style") == 0 ||
	       strcmp (name, "script") == 0 ||
	       strcmp (name, "head") == 0))
	    {
	      skip_content = TRUE;
	      g_free (tag);
	      p = end + 1;
	      continue;
	    }

	  color[0] = '\0';
	  if (!closing)
	    {
	      if (hints_tag_color (tag, color, sizeof color) &&
		  hints_safe_color (color))
		{
		  /* Inline style colour wins over the stylesheet. */
		}
	      else
		{
		  const gchar *css_color =
		    g_hash_table_lookup (css_colors, name);

		  if (css_color)
		    g_strlcpy (color, css_color, sizeof color);
		}
	    }

	  if (strcmp (name, "h1") == 0 || strcmp (name, "h2") == 0 ||
	      strcmp (name, "h3") == 0)
	    {
	      if (closing)
		{
		  hints_markup_pop (open_tags, HINTS_TAG_SPAN, out);
		  g_string_append (out, "\n");
		  g_strlcpy (list_color, HINTS_DEFAULT_COLOR,
			     sizeof list_color);
		}
	      else
		{
		  const gchar *size = strcmp (name, "h1") == 0 ? "xx-large"
		    : strcmp (name, "h2") == 0 ? "large" : "medium";
		  HintsMarkupTag tag = HINTS_TAG_SPAN;

		  if (color[0])
		    g_strlcpy (list_color, color, sizeof list_color);

		  g_array_append_val (open_tags, tag);
		  g_string_append_printf (
		    out, "\n<span foreground=\"%s\" weight=\"bold\" "
		    "size=\"%s\">", list_color, size);
		}
	      in_space = TRUE;
	    }
	  else if (strcmp (name, "p") == 0 ||
		   strcmp (name, "div") == 0 ||
		   strcmp (name, "ul") == 0 ||
		   strcmp (name, "ol") == 0)
	    {
	      g_string_append_c (out, '\n');
	      in_space = TRUE;
	    }
	  else if (strcmp (name, "li") == 0)
	    {
	      if (closing)
		g_string_append_c (out, '\n');
	      else
		{
		  g_string_append_printf (
		    out, "\n<span foreground=\"%s\">•</span> ",
		    list_color);
		  in_space = TRUE;
		}
	    }
	  else if (strcmp (name, "br") == 0)
	    {
	      g_string_append_c (out, '\n');
	      in_space = TRUE;
	    }
	  else if (strcmp (name, "b") == 0 ||
		   strcmp (name, "strong") == 0)
	    {
	      if (closing)
		hints_markup_pop (open_tags, HINTS_TAG_B, out);
	      else
		hints_markup_push (open_tags, HINTS_TAG_B, out);
	    }
	  else if (strcmp (name, "i") == 0 ||
		   strcmp (name, "em") == 0)
	    {
	      if (closing)
		hints_markup_pop (open_tags, HINTS_TAG_I, out);
	      else
		hints_markup_push (open_tags, HINTS_TAG_I, out);
	    }
	  else if (strcmp (name, "u") == 0)
	    {
	      if (closing)
		hints_markup_pop (open_tags, HINTS_TAG_U, out);
	      else
		hints_markup_push (open_tags, HINTS_TAG_U, out);
	    }
	  else if (strcmp (name, "code") == 0 ||
		   strcmp (name, "tt") == 0)
	    {
	      if (closing)
		hints_markup_pop (open_tags, HINTS_TAG_TT, out);
	      else
		hints_markup_push (open_tags, HINTS_TAG_TT, out);
	    }
	  else if (strcmp (name, "a") == 0)
	    {
	      if (closing)
		hints_markup_pop (open_tags, HINTS_TAG_SPAN, out);
	      else
		{
		  HintsMarkupTag tag = HINTS_TAG_SPAN;

		  g_array_append_val (open_tags, tag);
		  g_string_append_printf (
		    out, "<span underline=\"single\" foreground=\"%s\">",
		    color[0] ? color : "#2980b9");
		}
	    }
	  else if (strcmp (name, "span") == 0 ||
		   strcmp (name, "font") == 0)
	    {
	      if (closing)
		hints_markup_pop (open_tags, HINTS_TAG_SPAN, out);
	      else
		{
		  HintsMarkupTag tag = HINTS_TAG_SPAN;

		  g_array_append_val (open_tags, tag);
		  if (color[0])
		    g_string_append_printf (out,
					    "<span foreground=\"%s\">",
					    color);
		  else
		    g_string_append (out, "<span>");
		}
	    }

	  g_free (tag);
	  p = end + 1;
	  continue;
	}

      if (*p == '&')
	{
	  gsize consumed = 0;
	  gunichar value = 0;

	  if (hints_decode_entity (p, &consumed, &value))
	    {
	      if (g_unichar_validate (value))
		hints_append_escaped (out, value);
	      in_space = FALSE;
	      p += consumed;
	    }
	  else
	    {
	      hints_append_escaped (out, '&');
	      in_space = FALSE;
	      p++;
	    }
	  continue;
	}

      {
	gunichar c = g_utf8_get_char_validated (p, -1);

	if (c == (gunichar) -1 || c == (gunichar) -2)
	  break;

	if (g_unichar_isspace (c))
	  {
	    if (!in_space && out->len > 0)
	      g_string_append_c (out, ' ');
	    in_space = TRUE;
	  }
	else
	  {
	    hints_append_escaped (out, c);
	    in_space = FALSE;
	  }

	p = g_utf8_next_char (p);
      }
    }

  /* Auto-close anything the hand-edited HTML left open, so the resulting
   * Pango markup is always valid. */
  while (open_tags->len > 0)
    {
      HintsMarkupTag tag =
	g_array_index (open_tags, HintsMarkupTag, open_tags->len - 1);

      g_string_append (out, hints_markup_close[tag]);
      g_array_set_size (open_tags, open_tags->len - 1);
    }
  g_array_free (open_tags, TRUE);
  g_hash_table_destroy (css_colors);

  return hints_cleanup_markup (out);
}

/* Prefer a per-user file, then the installed file.  SCREENATOR_HINTS_FILE is
 * useful for testing a copy without installing it. */
static gchar *
hints_load_html (void)
{
  const gchar *env = g_getenv ("SCREENATOR_HINTS_FILE");
  const gchar *data_dir;
  gchar *path;
  gchar *contents = NULL;

  if (env && *env && g_file_get_contents (env, &contents, NULL, NULL))
    return contents;
  g_free (contents);
  contents = NULL;

  path = screenator_config_path ("hints.html");
  if (g_file_get_contents (path, &contents, NULL, NULL))
    {
      g_free (path);
      return contents;
    }
  g_free (path);
  g_free (contents);
  contents = NULL;

  data_dir = icon_data_dir ();
  path = data_dir
    ? g_build_filename (data_dir, "screenator", "hints.html", NULL)
    : g_build_filename ("/usr/share/screenator", "hints.html", NULL);

  if (!g_file_get_contents (path, &contents, NULL, NULL))
    {
      g_free (contents);
      contents = NULL;
    }

  /* Make the user copy the canonical editable file.  Once it exists, later
   * edits there are what the hints window reads. */
  if (contents && !g_file_test (path, G_FILE_TEST_EXISTS))
    g_file_set_contents (path, contents, -1, NULL);

  g_free (path);

  if (!contents)
    contents = g_strdup (
      "<h2 style=\"color:#c0392b\">Hints file not found</h2>"
      "<p>Edit <b>~/.config/Screenator/hints.html</b> or install "
      "<b>/usr/share/screenator/hints.html</b>.</p>"
      "<ul>"
      "<li><b>F9</b> shows or hides the floating icon.</li>"
      "<li><b>F8</b> selects the Pen, <b>F6</b> the Arrow and "
      "<b>F7</b> the Text tool.</li>"
      "<li><b>F10</b> shows or hides the tool bar.</li>"
      "</ul>");

  return contents;
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
  GtkWidget *dialog, *content, *box;
  GtkWidget *hints_view, *check, *scroll;
  gchar *hints_html, *hints_markup;
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

  gtk_window_set_default_size (GTK_WINDOW (dialog), 700, 500);

  gtk_container_add (GTK_CONTAINER (content), scroll);
  gtk_container_add (GTK_CONTAINER (scroll), box);

  {
    gchar *user_path = screenator_config_path ("hints.html");
    gchar *escaped = g_markup_escape_text (user_path, -1);
    gchar *path_markup = g_strdup_printf (
      "<span size=\"small\" foreground=\"#7f8c8d\">Edit hints: %s</span>",
      escaped);
    GtkWidget *path_label = gtk_label_new (NULL);

    gtk_label_set_markup (GTK_LABEL (path_label), path_markup);
    gtk_label_set_xalign (GTK_LABEL (path_label), 0.0);
    gtk_widget_set_margin_start (path_label, 14);
    gtk_widget_set_margin_end (path_label, 14);
    gtk_widget_set_margin_bottom (path_label, 4);
    gtk_box_pack_start (GTK_BOX (content), path_label, FALSE, FALSE, 0);

    g_free (user_path);
    g_free (escaped);
    g_free (path_markup);
  }

  hints_html = hints_load_html ();
  hints_markup = hints_html_to_markup (hints_html);
  g_free (hints_html);

  hints_view = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (hints_view), hints_markup);
  g_free (hints_markup);
  gtk_label_set_line_wrap (GTK_LABEL (hints_view), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (hints_view), 90);
  gtk_label_set_xalign (GTK_LABEL (hints_view), 0.0);
  gtk_label_set_yalign (GTK_LABEL (hints_view), 0.0);
  gtk_widget_set_hexpand (hints_view, TRUE);
  gtk_box_pack_start (GTK_BOX (box), hints_view, FALSE, FALSE, 4);

  check = gtk_check_button_new_with_label ("Never show hints again");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), data->hide_hints);
  {
    GtkWidget *action_area;
    GtkWidget *close_button;
    GtkWidget *about_button;

    /* A check button must not be added with gtk_dialog_add_action_widget(),
     * because that would close the dialog when it is toggled.  Pack it into
     * the action area directly, then put the check at the left edge, About
     * before Close, and the Close button at the right edge. */
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    action_area = gtk_dialog_get_action_area (GTK_DIALOG (dialog));
    G_GNUC_END_IGNORE_DEPRECATIONS
    close_button =
      gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog),
					  GTK_RESPONSE_CLOSE);

    about_button = gtk_button_new_with_label ("About");
    g_signal_connect (about_button, "clicked",
		      G_CALLBACK (on_hints_about), data);
    gtk_box_pack_start (GTK_BOX (action_area), about_button, FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (action_area), check, FALSE, FALSE, 0);
    gtk_box_reorder_child (GTK_BOX (action_area), check, 0);
    gtk_box_reorder_child (GTK_BOX (action_area), about_button, 1);
    if (close_button)
      gtk_box_reorder_child (GTK_BOX (action_area), close_button, 2);

    gtk_button_box_set_layout (GTK_BUTTON_BOX (action_area),
			       GTK_BUTTONBOX_END);
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
activate_tool_immediately (ScreenatorData *data, gint pen_tool,
			   gint shape_type)
{
  GHashTableIter it;
  gpointer value;
  ScreenatorDeviceData *devdata = NULL;

  /* Selecting any tool leaves the Cursor mode. */
  if (data->paper_hand_tool)
    {
      data->paper_hand_tool = FALSE;
      paper_top_bar_update_active_tool (data);
    }

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
  paper_top_bar_update_active_tool (data);
  paper_bring_to_front (data);
  paper_update_hover_cursor (data);
}

static gboolean
timer_timeout (gpointer user_data)
{
  ScreenatorData *data = user_data;

  data->tool_delay_id = 0;
  data->timer_active = FALSE;
  paper_top_bar_update_active_tool (data);

  /* A dialog or the quick menu may have been opened during the countdown. */
  if (data->top_dialog || data->native_dialog_open || data->quick_menu)
    return G_SOURCE_REMOVE;

  /* The timer always finishes with the Pen tool. */
  activate_tool_immediately (data, 1, -1);

  return G_SOURCE_REMOVE;
}

/* The Timer tool: release the input so the user can switch applications,
 * wait for the configured Timer Delay, then activate the Pen tool.  Tool
 * selection is locked until the countdown finishes. */
void
screenator_start_timer (ScreenatorData *data)
{
  /* The Timer is a Screen Annotation tool; it is not available in Paper
   * Drawing. */
  if (data->paper != SCREENATOR_PAPER_NONE)
    return;

  if (data->timer_active)
    return;

  if (data->tool_delay_seconds == 0)
    {
      activate_tool_immediately (data, 1, -1);
      return;
    }

  data->timer_active = TRUE;

  paper_finish_temporary_eraser (data);
  screenator_paper_pan_end (data);

  if (data->tool_delay_id)
    {
      g_source_remove (data->tool_delay_id);
      data->tool_delay_id = 0;
    }

  release_grab (data, NULL);
  paper_top_bar_update_active_tool (data);

  data->tool_delay_id =
    g_timeout_add_seconds (data->tool_delay_seconds, timer_timeout, data);
}

static void
screenator_timer_cancel (ScreenatorData *data)
{
  if (data->tool_delay_id)
    {
      g_source_remove (data->tool_delay_id);
      data->tool_delay_id = 0;
    }

  data->timer_active = FALSE;
}

static void
activate_tool_now (ScreenatorData *data, gint pen_tool, gint shape_type)
{
  if (data->timer_active)
    return;

  activate_tool_immediately (data, pen_tool, shape_type);
}

static void
schedule_tool_activation (ScreenatorData *data, gint pen_tool, gint shape_type)
{
  if (data->timer_active)
    return;

  activate_tool_now (data, pen_tool, shape_type);
}

void
screenator_schedule_tool (ScreenatorData *data, gint pen_tool, gint shape_type)
{
  schedule_tool_activation (data, pen_tool, shape_type);
}

void on_icon_pen (GtkMenuItem *menuitem,
		  gpointer     user_data)
{
  schedule_tool_activation ((ScreenatorData *) user_data, 1, -1);
}

static void on_icon_timer (GtkMenuItem *menuitem, gpointer user_data)
{
  (void) menuitem;
  screenator_start_timer ((ScreenatorData *) user_data);
}

static void on_icon_switch_mode (GtkMenuItem *menuitem, gpointer user_data)
{
  (void) menuitem;
  screenator_toggle_paper_drawing ((ScreenatorData *) user_data);
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
	    screenator_invalidate_rect (data, &bounds);
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
