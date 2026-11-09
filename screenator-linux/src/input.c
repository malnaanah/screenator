
/*
 * Functions for setting up (parts of) the application
 *
 * Screenator -- a vibe-coded screen annotation tool based on Gromit-MPX 1.4.2
 * Screenator Copyright (C) 2026 Mahmoud Alnaanah <malnaanah@gmail.com>
 */

#include <string.h>
#include <gdk/gdk.h>
#ifdef GDK_WINDOWING_X11
#include <X11/extensions/XInput2.h>
#include <gdk/gdkx.h>
#endif
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#define WAYLAND_HOTKEY_PREFIX "screenator-wayland-hotkey"

#include "input.h"

/* TRUE when the desktop session is Wayland.  Screenator itself uses XWayland,
 * but pointer grabs and input shapes behave differently there, so the input
 * code needs to know the session type.  WAYLAND_DISPLAY is a fallback for
 * launches where XDG_SESSION_TYPE is missing. */
gboolean
screenator_is_wayland_session (void)
{
  const gchar *session = g_getenv ("XDG_SESSION_TYPE");
  const gchar *display;

  if (session && g_ascii_strcasecmp (session, "wayland") == 0)
    return TRUE;

  display = g_getenv ("WAYLAND_DISPLAY");
  return display && *display;
}

static gboolean get_are_all_grabbed(ScreenatorData *data)
{
    // iterate over devices to get grab statuses
    gboolean all_grabbed = FALSE;
    GHashTableIter it;
    gpointer value;
    ScreenatorDeviceData* devdata = NULL;
    g_hash_table_iter_init (&it, data->devdatatable);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
	devdata = value;
	if (devdata->is_grabbed) {
	    all_grabbed = TRUE; // one grabbed device, go on
	    continue;
	} else {
	    all_grabbed = FALSE; // one ungrabbed device, can stop here
	    break;
	}
    }

    return all_grabbed;
}

static gboolean get_are_some_grabbed(ScreenatorData *data)
{
    // iterate over devices to get grab statuses
    gboolean some_grabbed = FALSE;
    GHashTableIter it;
    gpointer value;
    ScreenatorDeviceData* devdata = NULL;

    if (!data->devdatatable)
	return FALSE;

    g_hash_table_iter_init (&it, data->devdatatable);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
	devdata = value;
	if (devdata->is_grabbed) {
	    some_grabbed = TRUE; // one grabbed device, can stop here
	    break;
	}
    }

    return some_grabbed;
}

static guint paper_shortcut_keycode (guint keyval);
static void grab_paper_key (ScreenatorData *data, guint keycode,
			    guint modifier, gboolean grab);
static void grab_escape_shortcut (ScreenatorData *data, gboolean grab);
static GdkFilterReturn raw_input_filter (GdkXEvent *xevent, GdkEvent *event,
					 gpointer user_data);
static GdkDevice *raw_master_device (ScreenatorData *data, int deviceid);
static void raw_draw_press (ScreenatorData *data, GdkDevice *dev,
			    gint x, gint y);
static void raw_draw_motion (ScreenatorData *data, GdkDevice *dev,
			     gint x, gint y);
static void raw_draw_release (ScreenatorData *data, GdkDevice *dev);
static void raw_input_select (ScreenatorData *data, gboolean enable);

static void remove_hotkeys_from_compositor(ScreenatorData *data) {
    char *xdg_current_desktop = getenv("XDG_CURRENT_DESKTOP");
    if (xdg_current_desktop && strcmp(xdg_current_desktop, "GNOME") == 0) {
	/*
	  Get all custom key bindings and save back the ones that are not from us.
	*/
	if(data->debug)
	    g_print("DEBUG: Detected GNOME under Wayland, removing our hotkeys from compositor\n");

	GPtrArray *other_key_bindings_mutable_array = g_ptr_array_new();

	GSettings *settings = g_settings_new("org.gnome.settings-daemon.plugins.media-keys");

	gchar **key_bindings_array = g_settings_get_strv(settings, "custom-keybindings");
	gchar **binding;
	for (binding = key_bindings_array; *binding; binding++) {
	    GSettings *settings = g_settings_new_with_path("org.gnome.settings-daemon.plugins.media-keys.custom-keybinding",
							   *binding);
	    gchar *name = g_settings_get_string(settings, "name");

            if (!g_str_has_prefix(name, WAYLAND_HOTKEY_PREFIX)) {
		g_ptr_array_add(other_key_bindings_mutable_array, strdup(*binding));
            } else {
		if (data->debug)
		    g_print("DEBUG:   removing %s with name '%s'\n", *binding, name);
            }

            g_free(name);
	    g_object_unref(settings);
	}

	g_ptr_array_add(other_key_bindings_mutable_array, NULL);
	gchar **other_key_bindings_array = (gchar **)g_ptr_array_free(other_key_bindings_mutable_array, FALSE);
	g_settings_set_strv(settings, "custom-keybindings", (const gchar**)other_key_bindings_array);

	g_strfreev(other_key_bindings_array);
	g_strfreev(key_bindings_array);
	g_object_unref(settings);
    }
}

static void add_hotkeys_to_compositor(ScreenatorData *data) {
    char *xdg_current_desktop = getenv("XDG_CURRENT_DESKTOP");
    if (xdg_current_desktop && strcmp(xdg_current_desktop, "GNOME") == 0) {

	if(data->debug)
	    g_print("DEBUG: Detected GNOME under Wayland, adding our hotkeys to compositor\n");

	/*
	  Get highest custom keybinding index, collecting keybindings on the way.
	*/
	guint binding_index = 0;
	GPtrArray *new_key_bindings_mutable_array = g_ptr_array_new();

	GSettings *settings = g_settings_new("org.gnome.settings-daemon.plugins.media-keys");

	gchar **old_key_bindings_array = g_settings_get_strv(settings, "custom-keybindings");
	gchar **binding;
	for (binding = old_key_bindings_array; *binding; binding++) {
	    /* store existing bindings in mutable array */
	    g_ptr_array_add(new_key_bindings_mutable_array, strdup(*binding));

	    /* get the path components */
	    gchar **components = g_strsplit (*binding, "/", 0);
	    /* get the last component. -2 because g_strv_length() counts the trailing NULL as well */
	    gchar *custom_numbered = components[g_strv_length(components)-2];
	    guint custom_index = g_ascii_strtoull (custom_numbered+6, NULL, 10);
	    if(custom_index >= binding_index)
		binding_index = custom_index + 1;

	    g_strfreev(components);
	}

	/*
	  Add the single global shortcut: F9 shows or hides the floating
	  icon.  The old Gromit-MPX painting, clear, visibility, quit and
	  undo/redo bindings are not registered.
	 */
	gchar *modifier_hotkey_option[3];
	modifier_hotkey_option[0] = "";
	modifier_hotkey_option[1] = data->hot_keyval ? data->hot_keyval : "F9";
	modifier_hotkey_option[2] = "--toggle-icon";

	for(int i = 0; i < 3; i+=3) {
	    gchar *new_binding = g_malloc(128);
	    snprintf(new_binding,
		     128,
		     "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom%d/",
		     binding_index++);
	    g_ptr_array_add(new_key_bindings_mutable_array, new_binding);

	    GSettings *settings = g_settings_new_with_path("org.gnome.settings-daemon.plugins.media-keys.custom-keybinding",
							   new_binding);
	    gchar name[64];
	    snprintf(name, 64, "%s %s", WAYLAND_HOTKEY_PREFIX, modifier_hotkey_option[i+2]);
	    g_settings_set_string(settings, "name", name);

	    gchar *command;
	    const gchar *self = g_getenv ("APPIMAGE");

	    if (!self || !*self)
		self = "screenator";
	    command = g_strdup_printf ("%s %s", self, modifier_hotkey_option[i+2]);
	    g_settings_set_string(settings, "command", command);
	    g_free (command);

	    gchar binding[64];
	    snprintf(binding, 64, "%s%s", modifier_hotkey_option[i], modifier_hotkey_option[i+1]);
	    g_settings_set_string(settings, "binding", binding);

	    g_object_unref(settings);
	}

	/*
	  Convert the mutable bindings array into a static array and apply this to the setting.
	 */
	g_ptr_array_add(new_key_bindings_mutable_array, NULL);
	gchar **new_key_bindings_array = (gchar **)g_ptr_array_free(new_key_bindings_mutable_array, FALSE);
	g_settings_set_strv(settings, "custom-keybindings", (const gchar**)new_key_bindings_array);

        g_strfreev(new_key_bindings_array);
        g_strfreev(old_key_bindings_array);
	g_object_unref(settings);
    }
}

void setup_input_devices (ScreenatorData *data)
{
  /* ungrab all */
  release_grab (data, NULL); 

  /* and clear our own device data list */
  GHashTableIter it;
  gpointer value;
  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value)) 
    devdata_free(value);
  g_hash_table_remove_all(data->devdatatable);

  if (!data->raw_filter_installed && GDK_IS_X11_DISPLAY (data->display))
    {
      int event_base, error_base;

      if (XQueryExtension (GDK_DISPLAY_XDISPLAY (data->display),
			   "XInputExtension", &data->xi_opcode,
			   &event_base, &error_base))
	gdk_window_add_filter (NULL, raw_input_filter, data);
      else
	data->xi_opcode = -1;

      data->raw_filter_installed = TRUE;
    }

  screenator_resolve_shortcuts (data);

  if (!data->pen_keycode)
    data->pen_keycode = paper_shortcut_keycode (GDK_KEY_F8);
  if (!data->text_keycode)
    data->text_keycode = paper_shortcut_keycode (GDK_KEY_F7);
  if (!data->arrow_keycode)
    data->arrow_keycode = paper_shortcut_keycode (GDK_KEY_F6);
  if (!data->timer_keycode)
    data->timer_keycode = paper_shortcut_keycode (GDK_KEY_F12);
  if (!data->quick_keycode)
    data->quick_keycode = paper_shortcut_keycode (GDK_KEY_F10);

  /* get devices */
  GdkDeviceManager *device_manager = gdk_display_get_device_manager(data->display);
  GList *devices, *d;
  int i = 0;

  devices = gdk_device_manager_list_devices(device_manager, GDK_DEVICE_TYPE_MASTER);
  for(d = devices; d; d = d->next)
    {
      GdkDevice *device = (GdkDevice *) d->data;
    
      /* only enable devices with 2 ore more axes */
      if (gdk_device_get_source(device) != GDK_SOURCE_KEYBOARD && gdk_device_get_n_axes(device) >= 2)
        {
          gdk_device_set_mode (device, GDK_MODE_SCREEN);

          ScreenatorDeviceData *devdata;

          devdata  = g_malloc0(sizeof (ScreenatorDeviceData));
          devdata->device = device;
          devdata->index = i;


	  if (GDK_IS_X11_DISPLAY(data->display)) {
	      gint dev_id = gdk_x11_device_get_id(device);

	      gint kbd_dev_id = -1;
	      XIDeviceInfo* devinfo;
	      int devicecount = 0;
	      
	      devinfo = XIQueryDevice(GDK_DISPLAY_XDISPLAY(data->display),
				      dev_id,
				      &devicecount);
	      if(devicecount)
		  kbd_dev_id = devinfo->attachment;
	      XIFreeDeviceInfo(devinfo);
 
	      if(kbd_dev_id != -1)
		  {
		      if(data->debug)
		  g_printerr("DEBUG: Grabbing configured shortcuts from keyboard '%d'.\n", kbd_dev_id);

	      {
		XIEventMask mask;
		unsigned char bits[4] = {0,0,0,0};
		const guint lock_masks[] = { 0, LockMask, Mod2Mask,
					     LockMask | Mod2Mask };
		guint k;

		mask.mask = bits;
		mask.mask_len = sizeof(bits);
		XISetMask(bits, XI_KeyPress);
		XISetMask(bits, XI_KeyRelease);

		gdk_x11_display_error_trap_push(data->display);

		for (k = 0; k < SCREENATOR_SHORTCUT_COUNT; k++)
		  {
		    XIGrabModifiers modifiers[4];
		    gint m;

		    if (screenator_shortcut_scopes[k] !=
			SCREENATOR_SHORTCUT_ALWAYS ||
			!data->shortcuts[k].keycode)
		      continue;

		    for (m = 0; m < (gint) G_N_ELEMENTS (lock_masks); m++)
		      {
			modifiers[m].modifiers =
			  data->shortcuts[k].modifier | lock_masks[m];
			modifiers[m].status = 0;
		      }

		    if (XIGrabKeycode(GDK_DISPLAY_XDISPLAY(data->display),
				      kbd_dev_id,
				      data->shortcuts[k].keycode,
				      GDK_WINDOW_XID(data->root),
				      GrabModeAsync,
				      GrabModeAsync,
				      True,
				      &mask,
				      4,
				      modifiers) != 0)
		      g_printerr("ERROR: Grabbing shortcut keycode %u from keyboard device %d failed.\n",
				 data->shortcuts[k].keycode, kbd_dev_id);
		  }

	      }

		      XSync(GDK_DISPLAY_XDISPLAY(data->display), False);
		      if(gdk_x11_display_error_trap_pop(data->display))
			  {
			      g_printerr("ERROR: Grabbing keys from keyboard device %d failed due to X11 error.\n",
					 kbd_dev_id);
			      devdata_free(devdata);
			      continue;
			  }
		  }
	  } // GDK_IS_X11_DISPLAY()

	  /*
	    When running under XWayland, hotkey grabbing does not work and we
	    have to register shortcuts with the compositor.
	   */
	  if (screenator_is_wayland_session ()) {
	      remove_hotkeys_from_compositor(data);
	      add_hotkeys_to_compositor(data);
          }

          g_hash_table_insert(data->devdatatable, device, devdata);
          g_printerr ("Enabled Device %d: \"%s\", (Type: %d)\n", 
		      i++, gdk_device_get_name(device), gdk_device_get_source(device));
        }
    }

  g_printerr ("Now %d enabled devices.\n", g_hash_table_size(data->devdatatable));

  /* Offer the hints dialog once per run. */
  schedule_hints (data);
}

void shutdown_input_devices(ScreenatorData *data)
{
    release_grab(data, NULL); /* ungrab all */
    if (screenator_is_wayland_session ())
	remove_hotkeys_from_compositor(data);
}

gboolean screenator_any_grabbed (ScreenatorData *data)
{
    return get_are_some_grabbed (data);
}

/* TRUE while the active drawing surface is the Paper window.  All Paper
 * presentations (fullscreen, borderless panel view and Window mode) use
 * ordinary focused-window input instead of a global pointer grab. */
gboolean
screenator_paper_local_input (ScreenatorData *data)
{
  return data && data->paper != SCREENATOR_PAPER_NONE &&
    data->win == data->paper_win;
}

static gboolean
paper_window_input_mode (ScreenatorData *data)
{
  return screenator_paper_local_input (data);
}

/* Ctrl+Z undoes and Ctrl+Shift+Z redoes while painting mode is active.  The
 * keys are grabbed only while a device is grabbed, so they keep their usual
 * meaning everywhere else.  They go through XInput2 like the configured
 * hotkeys because the keyboard focus belongs to the application below the
 * overlay. */
static void
grab_undo_shortcut (ScreenatorData *data, gboolean grab)
{
  if (grab && screenator_paper_local_input (data))
    grab = FALSE;

  grab_paper_key (data, data->shortcuts[SCREENATOR_SHORTCUT_UNDO].keycode,
		  data->shortcuts[SCREENATOR_SHORTCUT_UNDO].modifier, grab);
  grab_paper_key (data, data->shortcuts[SCREENATOR_SHORTCUT_REDO].keycode,
		  data->shortcuts[SCREENATOR_SHORTCUT_REDO].modifier, grab);
}


/* Parse one shortcut such as "Ctrl+Shift+Z" into a key value and modifier
 * mask.  The last "+"-separated part is the key. */
void
screenator_resolve_shortcuts (ScreenatorData *data)
{
  GdkKeymap *keymap = gdk_keymap_get_default ();
  guint i;

  for (i = 0; i < SCREENATOR_SHORTCUT_COUNT; i++)
    {
      ScreenatorShortcut *shortcut = &data->shortcuts[i];
      gchar **parts;
      guint modifier = 0;
      guint keyval = 0;
      guint j, count;

      shortcut->keyval = 0;
      shortcut->keycode = 0;
      shortcut->modifier = 0;

      if (!shortcut->text || !*shortcut->text ||
	  g_ascii_strcasecmp (shortcut->text, "none") == 0)
	continue;

      parts = g_strsplit (shortcut->text, "+", -1);
      count = g_strv_length (parts);

      for (j = 0; j + 1 < count; j++)
	{
	  gchar *name = g_strstrip (parts[j]);

	  if (g_ascii_strcasecmp (name, "ctrl") == 0 ||
	      g_ascii_strcasecmp (name, "control") == 0)
	    modifier |= GDK_CONTROL_MASK;
	  else if (g_ascii_strcasecmp (name, "shift") == 0)
	    modifier |= GDK_SHIFT_MASK;
	  else if (g_ascii_strcasecmp (name, "alt") == 0 ||
		   g_ascii_strcasecmp (name, "mod1") == 0)
	    modifier |= GDK_MOD1_MASK;
	  else if (g_ascii_strcasecmp (name, "super") == 0 ||
		   g_ascii_strcasecmp (name, "meta") == 0)
	    modifier |= GDK_SUPER_MASK;
	}

      if (count > 0)
	{
	  gchar *name = g_strstrip (parts[count - 1]);

	  if (*name)
	    {
	      keyval = gdk_keyval_from_name (name);

	      if (!keyval && g_utf8_strlen (name, -1) == 1)
		{
		  gchar *lower = g_utf8_strdown (name, -1);
		  gchar *upper = g_utf8_strup (name, -1);

		  keyval = gdk_keyval_from_name (lower);
		  if (!keyval)
		    keyval = gdk_keyval_from_name (upper);
		  if (!keyval)
		    keyval = gdk_unicode_to_keyval (g_utf8_get_char (name));

		  g_free (lower);
		  g_free (upper);
		}
	    }
	}

      g_strfreev (parts);

      shortcut->keyval = keyval;
      shortcut->modifier = modifier;

      if (keyval && keymap)
	{
	  GdkKeymapKey *keys = NULL;
	  gint n_keys = 0;

	  if (gdk_keymap_get_entries_for_keyval (keymap, keyval,
						 &keys, &n_keys))
	    {
	      gint k;

	      for (k = 0; k < n_keys; k++)
		if (keys[k].level == 0)
		  {
		    shortcut->keycode = keys[k].keycode;
		    break;
		  }

	      if (!shortcut->keycode && n_keys > 0)
		shortcut->keycode = keys[0].keycode;

	      g_free (keys);
	    }
	}
    }
}

gboolean
screenator_shortcut_matches (ScreenatorData *data, ScreenatorShortcutId id,
			     GdkEventKey *event)
{
  ScreenatorShortcut *shortcut;
  guint modifiers;

  if (id < 0 || id >= SCREENATOR_SHORTCUT_COUNT)
    return FALSE;

  shortcut = &data->shortcuts[id];
  if (!shortcut->keycode && !shortcut->keyval)
    return FALSE;

  modifiers = event->state & gtk_accelerator_get_default_mod_mask ();
  if (modifiers != shortcut->modifier)
    return FALSE;

  if (shortcut->keycode)
    return event->hardware_keycode == shortcut->keycode;

  return event->keyval == shortcut->keyval;
}

static guint
paper_shortcut_keycode (guint keyval)
{
  GdkKeymapKey *keys = NULL;
  gint n_keys = 0;
  guint keycode = 0;
  gint i;

  if (!gdk_keymap_get_entries_for_keyval (
	  gdk_keymap_get_for_display (gdk_display_get_default ()),
	  keyval, &keys, &n_keys))
    return 0;

  for (i = 0; i < n_keys; i++)
    if (keys[i].level == 0)
      {
	keycode = keys[i].keycode;
	break;
      }
  if (!keycode && n_keys > 0)
    keycode = keys[0].keycode;

  g_free (keys);
  return keycode;
}

/* Experimental fallback for the case where another application holds the
 * pointer grab (for example its context menu is open).  XInput2 raw events
 * are delivered to every client that selected them, regardless of the grab;
 * the absolute pointer position still comes from XQueryPointer.  This lets a
 * pending tool draw while the other menu owns the pointer. */
static GdkDevice *
raw_master_device (ScreenatorData *data, int deviceid)
{
  GdkDeviceManager *manager = gdk_display_get_device_manager (data->display);
  GdkDevice *dev = gdk_x11_device_manager_lookup (manager, deviceid);

  if (dev && gdk_device_get_device_type (dev) == GDK_DEVICE_TYPE_SLAVE)
    dev = gdk_device_get_associated_device (dev);

  if (dev && gdk_device_get_source (dev) == GDK_SOURCE_KEYBOARD)
    return NULL;

  return dev;
}

static void
raw_draw_press (ScreenatorData *data, GdkDevice *dev, gint x, gint y)
{
  ScreenatorDeviceData *devdata = g_hash_table_lookup (data->devdatatable, dev);

  if (!devdata || !data->forced_context || data->raw_button_down)
    return;

  devdata->cur_context = data->forced_context;
  data->maxwidth = devdata->cur_context->width;
  data->raw_button_down = TRUE;
  data->raw_device = dev;

  snap_undo_state (data);
  stroke_begin (data, dev);
  draw_line (data, dev, x, y, x, y);
  coord_list_prepend (data, dev, x, y, data->maxwidth);

  devdata->lastx = x;
  devdata->lasty = y;
  devdata->motion_time = 0;
  devdata->smooth_active = FALSE;
}

static void
raw_draw_motion (ScreenatorData *data, GdkDevice *dev, gint x, gint y)
{
  ScreenatorDeviceData *devdata = g_hash_table_lookup (data->devdatatable, dev);

  if (!devdata || !data->raw_button_down || data->raw_device != dev)
    return;

  draw_line (data, dev, devdata->lastx, devdata->lasty, x, y);
  coord_list_prepend (data, dev, x, y, data->maxwidth);

  devdata->lastx = x;
  devdata->lasty = y;
}

static void
raw_draw_release (ScreenatorData *data, GdkDevice *dev)
{
  if (!data->raw_button_down || data->raw_device != dev)
    return;

  draw_line_finish (data, dev);
  stroke_finish (data, dev);
  coord_list_free (data, dev);

  data->raw_button_down = FALSE;
  data->raw_device = NULL;
}

static GdkFilterReturn
raw_input_filter (GdkXEvent *xevent, GdkEvent *event, gpointer user_data)
{
  ScreenatorData *data = user_data;
  XEvent *xev = (XEvent *) xevent;
  XGenericEventCookie *cookie;
  XIRawEvent *raw;
  GdkDevice *dev;
  ScreenatorDeviceData *devdata;
  Window root_ret, child_ret;
  int root_x, root_y, win_x, win_y;
  unsigned int mask;

  if (!data->raw_input_enabled || xev->type != GenericEvent)
    return GDK_FILTER_CONTINUE;

  cookie = &xev->xcookie;
  if (cookie->extension != data->xi_opcode)
    return GDK_FILTER_CONTINUE;

  if (cookie->evtype != XI_RawMotion &&
      cookie->evtype != XI_RawButtonPress &&
      cookie->evtype != XI_RawButtonRelease)
    return GDK_FILTER_CONTINUE;

  if (!XGetEventData (GDK_DISPLAY_XDISPLAY (data->display), cookie))
    return GDK_FILTER_CONTINUE;

  raw = (XIRawEvent *) cookie->data;
  dev = raw_master_device (data, raw->deviceid);
  devdata = dev ? g_hash_table_lookup (data->devdatatable, dev) : NULL;

  if (devdata && data->forced_context &&
      (data->forced_context->type == SCREENATOR_PEN ||
       data->forced_context->type == SCREENATOR_ERASER ||
       data->forced_context->type == SCREENATOR_TEXT) &&
      XQueryPointer (GDK_DISPLAY_XDISPLAY (data->display),
		     GDK_WINDOW_XID (data->root),
		     &root_ret, &child_ret,
		     &root_x, &root_y, &win_x, &win_y, &mask))
    {
      if (data->forced_context->type == SCREENATOR_TEXT)
	{
	  if (cookie->evtype == XI_RawButtonPress && raw->detail == 1)
	    screenator_show_text (data, dev, root_x, root_y);
	}
      else
	{
	  switch (cookie->evtype)
	    {
	    case XI_RawButtonPress:
	      if (raw->detail == 1)
		raw_draw_press (data, dev, root_x, root_y);
	      break;
	    case XI_RawButtonRelease:
	      if (raw->detail == 1)
		raw_draw_release (data, dev);
	      break;
	    case XI_RawMotion:
	      raw_draw_motion (data, dev, root_x, root_y);
	      break;
	    }
	}
    }

  XFreeEventData (GDK_DISPLAY_XDISPLAY (data->display), cookie);
  return GDK_FILTER_CONTINUE;
}

static void
raw_input_select (ScreenatorData *data, gboolean enable)
{
  XIEventMask mask;
  unsigned char bits[4] = { 0, 0, 0, 0 };

  if (!GDK_IS_X11_DISPLAY (data->display) || data->xi_opcode <= 0 ||
      !data->root)
    return;

  if (enable)
    {
      XISetMask (bits, XI_RawMotion);
      XISetMask (bits, XI_RawButtonPress);
      XISetMask (bits, XI_RawButtonRelease);
    }

  mask.deviceid = XIAllMasterDevices;
  mask.mask_len = sizeof (bits);
  mask.mask = bits;

  gdk_x11_display_error_trap_push (data->display);
  XISelectEvents (GDK_DISPLAY_XDISPLAY (data->display),
		  GDK_WINDOW_XID (data->root), &mask, 1);
  XSync (GDK_DISPLAY_XDISPLAY (data->display), False);
  gdk_x11_display_error_trap_pop_ignored (data->display);
}

void
screenator_raw_input_enable (ScreenatorData *data)
{
  if (data->raw_input_enabled)
    return;

  data->raw_input_enabled = TRUE;
  raw_input_select (data, TRUE);
}

void
screenator_raw_input_disable (ScreenatorData *data)
{
  if (!data->raw_input_enabled)
    return;

  if (data->raw_button_down && data->raw_device)
    raw_draw_release (data, data->raw_device);

  data->raw_input_enabled = FALSE;
  raw_input_select (data, FALSE);
}

/* Escape exits the active drawing tool.  Like Ctrl+Z it is grabbed through
 * XInput2 while a painting device is grabbed, so it reaches Screenator even
 * when the keyboard focus belongs to the application below the overlay.  It is
 * released again as soon as painting stops, so it keeps its normal meaning
 * everywhere else. */
static void
grab_escape_shortcut (ScreenatorData *data, gboolean grab)
{
  if (grab && screenator_paper_local_input (data))
    grab = FALSE;

  grab_paper_key (data, data->shortcuts[SCREENATOR_SHORTCUT_ESCAPE].keycode,
		  data->shortcuts[SCREENATOR_SHORTCUT_ESCAPE].modifier, grab);
}


static void
grab_paper_key (ScreenatorData *data, guint keycode, guint modifier,
		gboolean grab)
{
  GList *devices, *d;
  gint i;

  if (!keycode || !GDK_IS_X11_DISPLAY (data->display))
    return;

  devices = gdk_device_manager_list_devices (
      gdk_display_get_device_manager (data->display), GDK_DEVICE_TYPE_MASTER);

  for (d = devices; d; d = d->next)
    {
      XIEventMask mask;
      unsigned char bits[4] = { 0, 0, 0, 0 };
      XIGrabModifiers modifiers[4];
      const guint lock_masks[] = { 0, LockMask, Mod2Mask,
				   LockMask | Mod2Mask };
      gint nmods = 0;

      if (gdk_device_get_source (d->data) != GDK_SOURCE_KEYBOARD)
	continue;

      for (i = 0; i < (gint) G_N_ELEMENTS (lock_masks); i++)
	{
	  modifiers[nmods].modifiers = modifier | lock_masks[i];
	  modifiers[nmods].status = 0;
	  nmods++;
	}

      mask.mask = bits;
      mask.mask_len = sizeof (bits);
      XISetMask (bits, XI_KeyPress);
      XISetMask (bits, XI_KeyRelease);

      gdk_x11_display_error_trap_push (data->display);

      if (grab)
	XIGrabKeycode (GDK_DISPLAY_XDISPLAY (data->display),
		       gdk_x11_device_get_id (d->data), keycode,
		       GDK_WINDOW_XID (data->root),
		       GrabModeAsync, GrabModeAsync, True,
		       &mask, nmods, modifiers);
      else
	XIUngrabKeycode (GDK_DISPLAY_XDISPLAY (data->display),
			 gdk_x11_device_get_id (d->data), keycode,
			 GDK_WINDOW_XID (data->root), nmods, modifiers);

      gdk_x11_display_error_trap_pop_ignored (data->display);
    }

  g_list_free (devices);
}

/* In Window mode only unmodified globally-always shortcuts stay grabbed.
 * Everything else is delivered to the focused Paper window as a normal key
 * event, so it cannot be stolen from other applications. */
static gboolean
window_mode_shortcut_may_grab (ScreenatorData *data, guint index)
{
  if (!screenator_paper_local_input (data))
    return TRUE;

  return screenator_shortcut_scopes[index] == SCREENATOR_SHORTCUT_ALWAYS &&
    (data->shortcuts[index].modifier &
     (GDK_CONTROL_MASK | GDK_MOD1_MASK)) == 0;
}

/* Replace the configured shortcuts, ungrab the old keys and grab the new
 * ones according to their current scope. */
void
screenator_apply_shortcuts (ScreenatorData *data, gchar * const *new_texts)
{
  gboolean paper = data->paper != SCREENATOR_PAPER_NONE;
  gboolean painting = screenator_any_grabbed (data);
  guint i;

  for (i = 0; i < SCREENATOR_SHORTCUT_COUNT; i++)
    grab_paper_key (data, data->shortcuts[i].keycode,
		    data->shortcuts[i].modifier, FALSE);

  for (i = 0; i < SCREENATOR_SHORTCUT_COUNT; i++)
    {
      g_free (data->shortcuts[i].text);
      data->shortcuts[i].text = g_strdup (new_texts[i] ? new_texts[i] : "");
    }

  screenator_resolve_shortcuts (data);

  for (i = 0; i < SCREENATOR_SHORTCUT_COUNT; i++)
    {
      gboolean grab = FALSE;

      if (screenator_shortcut_scopes[i] == SCREENATOR_SHORTCUT_ALWAYS)
	grab = TRUE;
      else if (screenator_shortcut_scopes[i] ==
	       SCREENATOR_SHORTCUT_PAINTING)
	grab = painting;
      else if (screenator_shortcut_scopes[i] == SCREENATOR_SHORTCUT_PAPER)
	grab = paper;

      if (!window_mode_shortcut_may_grab (data, i))
	grab = FALSE;

      grab_paper_key (data, data->shortcuts[i].keycode,
		      data->shortcuts[i].modifier, grab);
    }
}

/* Grab the Paper Drawing shortcuts so they work even after Ctrl+T has hidden
 * the drawings.  Ctrl+Z stays with the key snooper (and the painting grab),
 * so it is not grabbed twice.  F4 saves a screenshot in Screen Annotation
 * mode and Alt+F4 quits in Paper Drawing, so the F4 grabs are kept in both
 * modes; the other Paper Drawing keys are only grabbed while it is active.
 * The hardware keycodes are resolved once and also used by the snooper, so
 * the shortcuts keep working with a non-Latin keyboard layout. */
/* In Window mode only grab Control/Alt shortcuts while the Paper window is
 * the focused application.  Unmodified global shortcuts such as F8/F9/F10
 * stay global. */
void
screenator_paper_shortcuts_focus (ScreenatorData *data, gboolean focused)
{
  gboolean painting;
  guint i;

  if (!data)
    return;

  /* Window mode receives its shortcuts as ordinary key events while the
   * Paper window is focused.  Only global always-on keys stay grabbed. */
  if (screenator_paper_local_input (data))
    {
      update_paper_shortcuts (data, TRUE);
      return;
    }

  painting = screenator_any_grabbed (data);

  for (i = 0; i < SCREENATOR_SHORTCUT_COUNT; i++)
    {
      guint modifier = data->shortcuts[i].modifier;
      gboolean grab;

      if ((modifier & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) == 0)
	continue;

      grab = focused &&
	(screenator_shortcut_scopes[i] == SCREENATOR_SHORTCUT_ALWAYS ||
	 screenator_shortcut_scopes[i] == SCREENATOR_SHORTCUT_PAPER ||
	 (screenator_shortcut_scopes[i] == SCREENATOR_SHORTCUT_PAINTING &&
	  painting));

      grab_paper_key (data, data->shortcuts[i].keycode, modifier, grab);
    }
}

/* Entering Window mode: drop the global pointer grab while keeping the
 * selected tool active.  From now on the normal focused window supplies the
 * pointer events and the window cursor only appears inside the Paper window. */
void
screenator_window_mode_enter (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;
  gboolean active = FALSE;
  GdkWindow *window;

  if (!data || !data->devdatatable)
    return;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      ScreenatorDeviceData *devdata = value;

      if (!devdata->is_grabbed)
	continue;

      active = TRUE;

      if (devdata->window_input)
	continue;

#ifdef GDK_WINDOWING_X11
      if (GDK_IS_X11_DISPLAY (data->display))
	{
	  gdk_x11_display_error_trap_push (data->display);
	  gdk_device_ungrab (devdata->device, GDK_CURRENT_TIME);
	  XSync (GDK_DISPLAY_XDISPLAY (data->display), False);
	  gdk_x11_display_error_trap_pop_ignored (data->display);
	}
      else
#endif
	gdk_device_ungrab (devdata->device, GDK_CURRENT_TIME);

      devdata->window_input = 1;
    }

  /* Undo and Escape were grabbed globally while the fullscreen Paper window
   * was active; the focused Window-mode window receives them as normal key
   * events instead. */
  grab_undo_shortcut (data, FALSE);
  grab_escape_shortcut (data, FALSE);

  /* Release global Ctrl/Alt grabs; the focused window receives them now. */
  screenator_paper_shortcuts_focus (data, TRUE);

  if (!active && data->forced_context)
    {
      /* A tool was selected but its grab was not active yet. */
      acquire_grab (data, NULL);
      return;
    }

  window = data->win ? gtk_widget_get_window (data->win) : NULL;
  if (window)
    gdk_window_set_cursor (window,
			   data->controls_hover > 0 ? NULL
			   : data->forced_context
			   ? get_tool_cursor (data, data->forced_context)
			   : NULL);
}

/* Leaving Window mode: turn the local tool state back into a real pointer
 * grab so fullscreen Paper mode keeps working as before.  The caller must
 * clear data->paper_window_mode first. */
void
screenator_window_mode_leave (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;
  gboolean active = FALSE;
  GdkWindow *window;

  if (!data || !data->devdatatable)
    return;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      ScreenatorDeviceData *devdata = value;

      if (!devdata->window_input)
	continue;

      devdata->window_input = 0;
      active = active || devdata->is_grabbed;
      devdata->is_grabbed = 0;
    }

  if (active)
    acquire_grab (data, NULL);
  else
    {
      window = data->win ? gtk_widget_get_window (data->win) : NULL;
      if (window)
	gdk_window_set_cursor (window,
			       data->forced_context
			       ? get_tool_cursor (data, data->forced_context)
			       : NULL);
    }
}

/* Forget Window-mode tool input without taking a new grab, used when the
 * Paper window is closed. */
void
screenator_window_mode_clear (ScreenatorData *data)
{
  GHashTableIter it;
  gpointer value;

  if (!data || !data->devdatatable)
    return;

  g_hash_table_iter_init (&it, data->devdatatable);
  while (g_hash_table_iter_next (&it, NULL, &value))
    {
      ScreenatorDeviceData *devdata = value;

      if (!devdata->window_input)
	continue;

      devdata->window_input = 0;
      devdata->is_grabbed = 0;
      devdata->motion_time = 0;
    }
}

void
update_paper_shortcuts (ScreenatorData *data, gboolean grab)
{
  guint i;

  for (i = 0; i < SCREENATOR_SHORTCUT_COUNT; i++)
    {
      gboolean on = FALSE;

      if (screenator_shortcut_scopes[i] == SCREENATOR_SHORTCUT_ALWAYS)
	on = TRUE;
      else if (screenator_shortcut_scopes[i] == SCREENATOR_SHORTCUT_PAPER)
	on = grab;

      if (!window_mode_shortcut_may_grab (data, i))
	on = FALSE;

      grab_paper_key (data, data->shortcuts[i].keycode,
		      data->shortcuts[i].modifier, on);
    }
}

/* Temporarily release the global shortcuts while a desktop file chooser is
 * open, so typing and function keys belong to the chooser.  Always-active
 * shortcuts are grabbed again; Painting and Paper shortcuts only while their
 * scope is active. */
void
screenator_suspend_shortcuts (ScreenatorData *data, gboolean suspend)
{
  gboolean paper = data->paper != SCREENATOR_PAPER_NONE;
  gboolean painting = screenator_any_grabbed (data);
  guint i;

  for (i = 0; i < SCREENATOR_SHORTCUT_COUNT; i++)
    {
      gboolean grab = FALSE;

      if (!suspend)
	{
	  if (screenator_shortcut_scopes[i] == SCREENATOR_SHORTCUT_ALWAYS)
	    grab = TRUE;
	  else if (screenator_shortcut_scopes[i] ==
		   SCREENATOR_SHORTCUT_PAINTING)
	    grab = painting;
	  else if (screenator_shortcut_scopes[i] ==
		   SCREENATOR_SHORTCUT_PAPER)
	    grab = paper;

	  if (!window_mode_shortcut_may_grab (data, i))
	    grab = FALSE;
	}

      grab_paper_key (data, data->shortcuts[i].keycode,
		      data->shortcuts[i].modifier, grab);
    }

#ifdef GDK_WINDOWING_X11
  if (GDK_IS_X11_DISPLAY (data->display))
    XSync (GDK_DISPLAY_XDISPLAY (data->display), False);
#endif
}

void release_grab (ScreenatorData *data, 
		   GdkDevice *dev)
{
  /* A text that is still being typed is committed when painting stops. */
  text_editor_finish (data);

  /* Commit a stroke that is still in progress before painting is stopped. */
  stroke_finish_all (data);

  /* Editable shapes are committed when painting stops. */
  star_finish_all (data);
  triangle_finish_all (data);
  frame_finish_all (data);
  line_finish_all (data);

  /* An unfinished spline is not committed; drop its preview as well. */
  spline_cancel_all (data);

  if (screenator_is_wayland_session () && !paper_window_input_mode (data))
    {
      /*
	When running under Wayland (XWayland in our case),
	make the whole transparent window transparent to input.
      */
      cairo_region_t *r = cairo_region_create();
      gtk_widget_input_shape_combine_region(data->win, r);
      cairo_region_destroy(r);

      // force a redraw, otherwise input shape is not applied,
      // at least on newer GNOME versions
      gdk_window_invalidate_rect(gtk_widget_get_window(data->win), NULL, 0);
    }


  if(!dev) /* this means release all grabs */
    {
      GHashTableIter it;
      gpointer value;
      ScreenatorDeviceData* devdata; 
      g_hash_table_iter_init (&it, data->devdatatable);
      while (g_hash_table_iter_next (&it, NULL, &value)) 
        {
          devdata = value;
          if(devdata->is_grabbed)
	  {
	    if (!devdata->window_input)
	      {
		gdk_x11_display_error_trap_push(data->display);
		gdk_device_ungrab(devdata->device, GDK_CURRENT_TIME);
		XSync(GDK_DISPLAY_XDISPLAY(data->display), False);
		if(gdk_x11_display_error_trap_pop(data->display))
		  g_printerr("WARNING: Ungrabbing device '%s' failed.\n", gdk_device_get_name(devdata->device));
	      }

	    devdata->window_input = 0;
	    devdata->is_grabbed = 0;
            /* workaround buggy GTK3 ? */
	    devdata->motion_time = 0;
	  }
        }

      if(data->debug)
        g_printerr ("DEBUG: Ungrabbed all Devices.\n");

      indicate_active(data, FALSE);
      grab_undo_shortcut (data, FALSE);
      grab_escape_shortcut (data, FALSE);

      /* Keep the selected tool cursor on the overlay window even when no
       * device is grabbed; use the normal pointer only when no tool is
       * selected. */
      if (!data->raw_input_enabled && !data->paper_hand_tool)
	{
	  GdkWindow *window = data->win
	    ? gtk_widget_get_window (data->win) : NULL;

	  if (window)
	    gdk_window_set_cursor (window,
				   data->forced_context
				   ? get_tool_cursor (data,
						      data->forced_context)
				   : NULL);
	}

      /* Paper Drawing keeps the annotation window visible. */
      if (!data->painted && data->paper == SCREENATOR_PAPER_NONE &&
	  !data->screenshot_paper)
	  hide_window (data);

      return;
    }


  /* get the data for this device */
  ScreenatorDeviceData *devdata = g_hash_table_lookup(data->devdatatable, dev);

  if (devdata->is_grabbed)
    {
      if (!devdata->window_input)
	gdk_device_ungrab(devdata->device, GDK_CURRENT_TIME);

      devdata->window_input = 0;
      devdata->is_grabbed = 0;
      /* workaround buggy GTK3 ? */
      devdata->motion_time = 0;


      if(data->debug)
        g_printerr ("DEBUG: Ungrabbed Device '%s'.\n", gdk_device_get_name(devdata->device));

      if(!get_are_some_grabbed(data))
	{
	  indicate_active(data, FALSE);
	  grab_undo_shortcut (data, FALSE);
	  grab_escape_shortcut (data, FALSE);

	  if (!data->raw_input_enabled && !data->paper_hand_tool)
	    {
	      GdkWindow *window = data->win
		? gtk_widget_get_window (data->win) : NULL;

	      if (window)
		gdk_window_set_cursor (window,
				       data->forced_context
				       ? get_tool_cursor (data,
							  data->forced_context)
				       : NULL);
	    }
	}
    }

  /* Paper Drawing keeps the annotation window visible. */
  if (!data->painted && data->paper == SCREENATOR_PAPER_NONE &&
      !data->screenshot_paper)
    hide_window (data);
}


void acquire_grab (ScreenatorData *data, 
		   GdkDevice *dev)
{
  gboolean window_mode = paper_window_input_mode (data);

  screenator_marker_prepare (data);
  show_window (data);

  if (screenator_is_wayland_session () && !window_mode) {
      /*
	When running under Wayland (XWayland in our case), make the whole transparent window react to input.
	Otherwise, no draw-cursor is shown over Wayland-only windows.
      */
      cairo_rectangle_int_t rect = {0, 0, data->width, data->height};
      cairo_region_t *r = cairo_region_create_rectangle(&rect);
      gtk_widget_input_shape_combine_region(data->win, r);
      cairo_region_destroy(r);
      // force a redraw, otherwise input shape is not applied,
      // at least on newer GNOME versions
      gdk_window_invalidate_rect(gtk_widget_get_window(data->win), NULL, 0);
  }

  if(!dev) /* this means grab all */
    {
      GHashTableIter it;
      gpointer value;
      ScreenatorDeviceData* devdata = NULL; 
      g_hash_table_iter_init (&it, data->devdatatable);
      while (g_hash_table_iter_next (&it, NULL, &value)) 
        {
          GdkCursor *cursor;

	  devdata = value;
          if(devdata->is_grabbed)
            continue;

	  /* Apply the menu-selected tool immediately so the grab cursor
	   * matches the chosen tool (Pen / Arrow / Eraser) instead of
	   * showing the previous tool's cursor until painting starts. */
	  if (data->forced_context)
	    devdata->cur_context = data->forced_context;

	  cursor = data->controls_hover > 0
	    ? NULL : get_tool_cursor (data, devdata->cur_context);

	  if (window_mode)
	    {
	      /* A normal window already receives pointer events only while the
	       * pointer is over it.  Do not take a global grab here: the tool
	       * cursor, drawing and clicks then stay inside the Paper window and
	       * other applications keep their own pointer input. */
	      devdata->is_grabbed = 1;
	      devdata->window_input = 1;
	      (void) cursor;
	      continue;
	    }

	  if(gdk_device_grab(devdata->device,
			     gtk_widget_get_window(data->win),
			     GDK_OWNERSHIP_NONE,
			     screenator_paper_grab_owner_events (data),
			     SCREENATOR_MOUSE_EVENTS,
			     cursor,
			     GDK_CURRENT_TIME) != GDK_GRAB_SUCCESS)
	    {
	      /* this probably means the device table is outdated,
		 e.g. this device doesn't exist anymore */
	      g_printerr("Error grabbing Device '%s' while grabbing all, ignoring.\n",
			 gdk_device_get_name(devdata->device));
	      continue;

	    }

          devdata->is_grabbed = 1;
        }

      if (window_mode)
	{
	  GdkWindow *window = data->win ? gtk_widget_get_window (data->win)
					: NULL;

	  if (window)
	    gdk_window_set_cursor (window,
				   data->controls_hover > 0 ? NULL
				   : data->forced_context
				   ? get_tool_cursor (data, data->forced_context)
				   : NULL);
	}

      if(data->debug)
        g_printerr("DEBUG: Grabbed all Devices.\n");

      indicate_active(data, TRUE);
      grab_undo_shortcut (data, TRUE);
      grab_escape_shortcut (data, TRUE);
      screenator_raw_input_disable (data);

      return;
    }


  /* get the data for this device */
  ScreenatorDeviceData *devdata = g_hash_table_lookup (data->devdatatable, dev);

  if (!devdata->is_grabbed)
    {
      GdkCursor *cursor = data->controls_hover > 0
	? NULL : get_tool_cursor (data, devdata->cur_context);

      if (window_mode)
	{
	  devdata->is_grabbed = 1;
	  devdata->window_input = 1;
	  set_device_cursor (data, devdata->device, devdata->cur_context);
	}
      else if(gdk_device_grab(devdata->device,
			     gtk_widget_get_window(data->win),
			     GDK_OWNERSHIP_NONE,
			     screenator_paper_grab_owner_events (data),
			     SCREENATOR_MOUSE_EVENTS,
			     cursor,
			     GDK_CURRENT_TIME) != GDK_GRAB_SUCCESS)
	{
	  /* this probably means the device table is outdated,
	     e.g. this device doesn't exist anymore */
	  g_printerr("Error grabbing device '%s', rescanning device list.\n",
		     gdk_device_get_name(devdata->device));
	  setup_input_devices(data);
	  return;
	}
      else
	devdata->is_grabbed = 1;
      
      if(data->debug)
        g_printerr("DEBUG: Grabbed Device '%s'.\n", gdk_device_get_name(devdata->device));

      indicate_active(data, TRUE);
      grab_undo_shortcut (data, TRUE);
      grab_escape_shortcut (data, TRUE);

    }

}


void toggle_grab (ScreenatorData *data, 
		  GdkDevice* dev)
{
  if(dev == NULL) /* toggle all */
    {
      if (get_are_all_grabbed(data))
	{
	  screenator_marker_leave (data);
	  screenator_clear_screenshot_paper (data);
	  release_grab (data, NULL);
	}
      else
	acquire_grab (data, NULL);
      return; 
    }

  /* get the data for this device */
  ScreenatorDeviceData *devdata = g_hash_table_lookup(data->devdatatable, dev);
      
  if(devdata)
    {
      if(devdata->is_grabbed)
        release_grab (data, devdata->device);
      else
        acquire_grab (data, devdata->device);
    }
  else
    g_printerr("ERROR: No such device '%s' in internal table.\n", gdk_device_get_name(dev));
}



/* Key snooper */
gint snoop_key_press(GtkWidget   *grab_widget,
		     GdkEventKey *event,
		     gpointer     func_data)
{
  ScreenatorData *data = (ScreenatorData *) func_data;
  GdkDevice *dev = gdk_event_get_device((GdkEvent*)event);

  (void) grab_widget;

  /* The keys belong to the dialog while one is open. */
  if (data->top_dialog || data->native_dialog_open)
    return FALSE;

  if (event->type != GDK_KEY_PRESS)
    return FALSE;

  /* Escape exits the current tool exactly like a right click on the canvas:
   * an unfinished shape is finished (or dropped for a spline) and painting
   * mode stops.  The inline text editor handles its own Escape, which
   * cancels the text instead of committing it. */
  if (screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_ESCAPE, event) &&
      !data->text_editor_active)
    return screenator_exit_tool (data);

  /* Switch between Screen and Paper modes from either mode. */
  if (!data->quick_menu &&
      screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_SWITCH_MODE,
				   event))
    {
      if (data->text_editor_active)
	return FALSE;
      screenator_toggle_paper_drawing (data);
      return TRUE;
    }

  /* Undo and redo work while painting, also in Screen mode.  The inline text
   * editor keeps its own Ctrl+Z for text undo. */
  if (screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_UNDO, event) ||
      screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_REDO, event))
    {
      if (data->hidden || data->text_editor_active)
	return FALSE;

      if (screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_REDO, event))
	screenator_redo (data);
      else
	screenator_undo (data);
      return TRUE;
    }

  /* Shortcuts that work while Paper mode has the keyboard focus.  The quick
   * tool menu gets the arrow keys first, and the inline text editor keeps
   * Ctrl+A for selecting text. */
  if (data->paper != SCREENATOR_PAPER_NONE && !data->quick_menu)
    {
      if (screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_QUIT, event))
	{
	  screenator_request_quit (data);
	  return TRUE;
	}

      /* Ctrl+W switches between fullscreen and Window mode. */
      if ((event->state & GDK_CONTROL_MASK) != 0 &&
	  (event->state & (GDK_SHIFT_MASK | GDK_MOD1_MASK)) == 0 &&
	  event->keyval == GDK_KEY_w)
	{
	  screenator_toggle_paper_window_fullscreen (data);
	  return TRUE;
	}

      /* Ctrl+D shows the desktop panel above the page without entering
       * Window mode. */
      if ((event->state & GDK_CONTROL_MASK) != 0 &&
	  (event->state & (GDK_SHIFT_MASK | GDK_MOD1_MASK)) == 0 &&
	  event->keyval == GDK_KEY_d)
	{
	  screenator_toggle_paper_panel (data);
	  return TRUE;
	}

      if (screenator_shortcut_matches (data,
				       SCREENATOR_SHORTCUT_TOGGLE_VISIBILITY,
				       event))
	{
	  if (data->text_editor_active)
	    return FALSE;
	  toggle_drawings_visibility (data);
	  return TRUE;
	}

      if (screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_EXIT_PAPER,
				       event))
	{
	  if (data->text_editor_active)
	    return FALSE;
	  screenator_exit_paper_drawing (data);
	  return TRUE;
	}

      if (screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_SAVE, event))
	{
	  if (data->text_editor_active)
	    return FALSE;
	  screenator_paper_save (data);
	  return TRUE;
	}

      if (screenator_shortcut_matches (data,
				       SCREENATOR_SHORTCUT_INSERT_BEFORE,
				       event))
	{
	  if (data->text_editor_active)
	    return FALSE;
	  screenator_paper_insert_page (data, TRUE);
	  return TRUE;
	}

      if (screenator_shortcut_matches (data,
				       SCREENATOR_SHORTCUT_INSERT_AFTER,
				       event))
	{
	  if (data->text_editor_active)
	    return FALSE;
	  screenator_paper_insert_page (data, FALSE);
	  return TRUE;
	}

      if (!data->text_editor_active &&
	  !(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK |
			    GDK_MOD1_MASK)))
	{
	  if (event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_Down)
	    {
	      screenator_paper_scroll (data,
				       event->keyval == GDK_KEY_Up ? -1 : 1);
	      return TRUE;
	    }

	  if (event->keyval == GDK_KEY_Page_Up ||
	      event->keyval == GDK_KEY_Page_Down)
	    {
	      screenator_paper_switch_page (
		data, event->keyval == GDK_KEY_Page_Up ? -1 : 1);
	      return TRUE;
	    }

	  if (event->keyval == GDK_KEY_Home)
	    {
	      screenator_paper_goto_page (data, 0);
	      return TRUE;
	    }

	  if (event->keyval == GDK_KEY_End)
	    {
	      screenator_paper_goto_page (data, G_MAXINT);
	      return TRUE;
	    }
	}
    }

  /* The screenshot shortcut works in both Screen and Paper modes. */
  if (!data->quick_menu &&
      screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_SCREENSHOT,
				   event))
    {
      screenator_save_screenshot (data);
      return TRUE;
    }

  if (screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_PEN, event))
    {
      screenator_schedule_tool (data, 1, -1);
      return TRUE;
    }

  if (screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_TEXT, event))
    {
      screenator_schedule_tool (data, 4, -1);
      return TRUE;
    }

  if (screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_ARROW, event))
    {
      screenator_schedule_tool (data, 2, SCREENATOR_SHAPE_ARROW);
      return TRUE;
    }

  if (data->paper == SCREENATOR_PAPER_NONE &&
      screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_TIMER, event))
    {
      screenator_start_timer (data);
      return TRUE;
    }

  if (screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_TOOLBAR, event))
    {
      screenator_toggle_paper_top_bar (data);
      return TRUE;
    }

  if (screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_FLOATING_ICON,
				   event))
    {
      if(data->debug)
	g_printerr("DEBUG: Received hotkey press from device '%s'\n", gdk_device_get_name(dev));

      set_floating_icon_visible (data, !data->show_floating_icon);
      return TRUE;
    }

  return FALSE;
}
