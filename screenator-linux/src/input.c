
/*
 * Functions for setting up (parts of) the application
 *
 * Screenator -- a vibe-coded screen annotation tool based on Gromit-MPX 1.4.2
 * Screenator Copyright (C) 2026 Mahmoud Alnaanah <malnaanah@gmail.com>
 */

#include <string.h>
#include <errno.h>
#include <signal.h>
#include <gdk/gdk.h>
#include <gio/gdesktopappinfo.h>
#ifdef GDK_WINDOWING_X11
#include <X11/Xatom.h>
#include <X11/extensions/XInput2.h>
#include <X11/Xutil.h>
#include <gdk/gdkx.h>
#endif
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#define WAYLAND_HOTKEY_PREFIX "screenator-wayland-hotkey"

#include "input.h"

static void
screenator_expand_companion_path (gchar **argv)
{
  if (argv && argv[0] && g_str_has_prefix (argv[0], "~/"))
    {
      gchar *expanded = g_build_filename (g_get_home_dir (), argv[0] + 2, NULL);
      g_free (argv[0]);
      argv[0] = expanded;
    }
}

static gboolean
screenator_is_qalculate_gui (const gchar *executable)
{
  gchar *resolved;
  gchar *base;
  gboolean qalculate;
  guint links;

  if (!executable || !*executable)
    return FALSE;

  resolved = g_find_program_in_path (executable);
  if (!resolved)
    resolved = g_strdup (executable);
  for (links = 0; links < 8; links++)
    {
      gchar *link = g_file_read_link (resolved, NULL);

      if (!link)
	break;
      if (!g_path_is_absolute (link))
	{
	  gchar *directory = g_path_get_dirname (resolved);
	  gchar *absolute_link = g_build_filename (directory, link, NULL);
	  g_free (directory);
	  g_free (link);
	  link = absolute_link;
	}
      g_free (resolved);
      resolved = link;
    }
  base = g_path_get_basename (resolved);
  qalculate = g_strcmp0 (base, "qalculate") == 0 ||
    g_strcmp0 (base, "qalculate-gtk") == 0 ||
    g_strcmp0 (base, "qalculate-qt") == 0;
  g_free (base);
  g_free (resolved);
  return qalculate;
}

static gchar **
screenator_companion_environment (void)
{
  static const gchar * const appimage_paths[] = {
    "LD_LIBRARY_PATH", "PATH", "XDG_DATA_DIRS"
  };
  static const gchar * const appimage_gtk_vars[] = {
    "GTK_DATA_PREFIX", "GTK_EXE_PREFIX", "GTK_PATH", "GTK_THEME",
    "GTK_IM_MODULE_FILE", "GDK_PIXBUF_MODULE_FILE",
    "GDK_PIXBUF_MODULEDIR", "GSETTINGS_SCHEMA_DIR", "GI_TYPELIB_PATH"
  };
  const gchar *appdir = g_getenv ("APPDIR");
  gchar **environment = g_get_environ ();
  guint i;

  if (!appdir || !*appdir)
    return environment;

  /* AppImage's GTK hook configures paths for Screenator's bundled GTK.  Those
   * paths break host applications launched as companions (notably their
   * gdk-pixbuf loader modules), so remove only the AppImage-specific entries
   * from the child environment. */
  for (i = 0; i < G_N_ELEMENTS (appimage_gtk_vars); i++)
    environment = g_environ_unsetenv (environment, appimage_gtk_vars[i]);

  for (i = 0; i < G_N_ELEMENTS (appimage_paths); i++)
    {
	  const gchar *value = g_environ_getenv (environment, appimage_paths[i]);
      gchar **parts;
      GString *filtered;
      guint part;

      if (!value || !*value)
	continue;
      parts = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);
      filtered = g_string_new (NULL);
      for (part = 0; parts[part]; part++)
	{
	  if (g_str_has_prefix (parts[part], appdir))
	    continue;
	  if (filtered->len > 0)
	    g_string_append (filtered, G_SEARCHPATH_SEPARATOR_S);
	  g_string_append (filtered, parts[part]);
	}
      if (filtered->len > 0)
	environment = g_environ_setenv (environment, appimage_paths[i],
					filtered->str, TRUE);
      else
	environment = g_environ_unsetenv (environment, appimage_paths[i]);
      g_string_free (filtered, TRUE);
      g_strfreev (parts);
    }

  environment = g_environ_unsetenv (environment, "APPDIR");
  environment = g_environ_unsetenv (environment, "APPIMAGE");
  environment = g_environ_unsetenv (environment, "ARGV0");
  return environment;
}

static gboolean
screenator_spawn_companion_command (gchar **argv, gint argc, GPid *child_pid,
				    GError **error)
{
  gchar **launch_argv;
  gchar **environment;
  gboolean has_new_instance = FALSE;
  gboolean qalculate = FALSE;
  gint i, launch_argc = argc;

  if (argc <= 0)
    return FALSE;

  /* Keep the caller-owned parsed argv intact.  Callers release it as soon as
   * this function returns, so the Qalculate option must go in a separate
   * spawn vector rather than reallocating the caller's allocation. */
  launch_argv = g_new0 (gchar *, argc + 2);
  for (i = 0; i < argc; i++)
    launch_argv[i] = argv[i];

  for (i = 0; i < argc; i++)
    {
      gchar *lower = g_ascii_strdown (argv[i], -1);
      gboolean is_qalculate = screenator_is_qalculate_gui (argv[i]) ||
	strstr (lower, "io.github.qalculate") != NULL;

      g_free (lower);
      if (is_qalculate)
	qalculate = TRUE;
      if (i > 0 && (g_strcmp0 (argv[i], "--new-instance") == 0 ||
		    g_strcmp0 (argv[i], "-n") == 0))
	has_new_instance = TRUE;
    }

  if (qalculate)
    {
      if (!has_new_instance)
	{
	  launch_argv[launch_argc++] = (gchar *) "--new-instance";
	}
    }


  environment = screenator_companion_environment ();
  gboolean launched = g_spawn_async (NULL, launch_argv, environment,
				     G_SPAWN_SEARCH_PATH,
				     NULL, NULL, child_pid, error);
  g_strfreev (environment);
  g_free (launch_argv);
  return launched;
}

static gboolean
screenator_launch_companion_app_info (GAppInfo *app,
				      GAppLaunchContext *context,
				      GPid *child_pid,
				      GError **error)
{
  const gchar *executable = app ? g_app_info_get_executable (app) : NULL;
  const gchar *app_id = app ? g_app_info_get_id (app) : NULL;
  gchar *app_id_lower = g_ascii_strdown (app_id ? app_id : "", -1);

  if (screenator_is_qalculate_gui (executable) ||
      strstr (app_id_lower, "qalculate") != NULL)
    {
	  const gchar *command_line = g_app_info_get_commandline (app);
      gchar **argv = NULL;
      gint argc = 0, i, out = 0;
      gboolean launched = FALSE;

      if (command_line && g_shell_parse_argv (command_line, &argc, &argv,
						      error) && argc > 0)
	{
	  /* Desktop Exec entries can contain URI field codes; no file is passed
	   * when launching a companion, so discard those placeholders. */
	  for (i = 0; i < argc; i++)
	    if (!strchr (argv[i], '%'))
	      argv[out++] = argv[i];
	    else
	      g_free (argv[i]);
	  argv[out] = NULL;
	  launched = screenator_spawn_companion_command (argv, out, child_pid,
							 error);
	}

      g_strfreev (argv);
	  /* g_app_info_get_commandline() returns storage owned by the GAppInfo. */
      g_free (app_id_lower);
      return launched;
    }

  g_free (app_id_lower);
  return g_app_info_launch (app, NULL, context, error);
}

static GAppInfo *
screenator_companion_app_info (ScreenatorData *data, guint slot)
{
  GList *apps, *l;
  GAppInfo *found = NULL;
  gchar **argv = NULL;
  gint argc = 0;
  gchar *command_basename = NULL;

  if (!data || slot >= G_N_ELEMENTS (data->companion_app_ids))
    return NULL;

  if (data->companion_app_ids[slot] &&
      g_file_test (data->companion_app_ids[slot], G_FILE_TEST_IS_REGULAR))
    return G_APP_INFO (g_desktop_app_info_new_from_filename (
      data->companion_app_ids[slot]));
  if (data->companion_app_ids[slot])
    {
      GDesktopAppInfo *desktop_app = g_desktop_app_info_new (
	data->companion_app_ids[slot]);
      if (desktop_app)
	return G_APP_INFO (desktop_app);
    }

  if (!data->companion_app_commands[slot] ||
      !g_shell_parse_argv (data->companion_app_commands[slot], &argc,
			   &argv, NULL) || argc == 0)
    {
      g_strfreev (argv);
      return NULL;
    }
  screenator_expand_companion_path (argv);
  if (g_str_has_suffix (argv[0], ".desktop") &&
      g_file_test (argv[0], G_FILE_TEST_IS_REGULAR))
    {
      found = G_APP_INFO (g_desktop_app_info_new_from_filename (argv[0]));
      g_strfreev (argv);
      return found;
    }
  command_basename = g_path_get_basename (argv[0]);

  apps = g_app_info_get_all ();
  for (l = apps; l; l = l->next)
    {
      GAppInfo *app = l->data;
      const gchar *id = g_app_info_get_id (app);
	const gchar *executable = g_app_info_get_executable (app);
	gchar *executable_basename = executable
	  ? g_path_get_basename (executable) : NULL;
	if ((data->companion_app_ids[slot] && id &&
	     g_strcmp0 (id, data->companion_app_ids[slot]) == 0) ||
	    (executable &&
	     g_strcmp0 (executable_basename, command_basename) == 0))
	{
          found = g_object_ref (app);
	  g_free (executable_basename);
          break;
        }
	g_free (executable_basename);
    }
  g_list_free_full (apps, g_object_unref);
  g_free (command_basename);
  g_strfreev (argv);
  return found;
}

#ifdef GDK_WINDOWING_X11
static gboolean
screenator_x11_window_exists (GdkDisplay *display, Display *xdisplay,
			      Window window)
{
  XWindowAttributes attributes;
  Status status;
  gint error;

  gdk_x11_display_error_trap_push (display);
  status = XGetWindowAttributes (xdisplay, window, &attributes);
  XSync (xdisplay, False);
  error = gdk_x11_display_error_trap_pop (display);
  return status != 0 && error == 0;
}

static gchar *
screenator_companion_identity_normalize (const gchar *identity)
{
  GString *normalized = g_string_new (NULL);

  for (; identity && *identity; identity++)
    if (g_ascii_isalnum (*identity))
      g_string_append_c (normalized, g_ascii_tolower (*identity));

  return g_string_free (normalized, FALSE);
}

static gboolean
screenator_companion_identity_matches (const gchar *candidate,
				       const gchar *window_identity)
{
  gchar *candidate_normalized =
    screenator_companion_identity_normalize (candidate);
  gchar *window_normalized =
    screenator_companion_identity_normalize (window_identity);
  gboolean matches = g_strcmp0 (candidate_normalized, window_normalized) == 0;

  /* Some desktop launchers append the toolkit to the executable name while
   * the window class uses only the product name (for example, Qalculate). */
  if (!matches && strlen (candidate_normalized) >= 4)
    {
      const gchar *suffix = NULL;

      if (g_str_has_prefix (window_normalized, candidate_normalized))
	{
	  suffix = window_normalized + strlen (candidate_normalized);
	  matches = g_str_equal (suffix, "qt") || g_str_equal (suffix, "qt5") ||
	    g_str_equal (suffix, "qt6") || g_str_equal (suffix, "gtk") ||
	    g_str_equal (suffix, "gtk3");
	}
      else if (g_str_has_prefix (candidate_normalized, window_normalized))
	{
	  suffix = candidate_normalized + strlen (window_normalized);
	  matches = g_str_equal (suffix, "qt") || g_str_equal (suffix, "qt5") ||
	    g_str_equal (suffix, "qt6") || g_str_equal (suffix, "gtk") ||
	    g_str_equal (suffix, "gtk3");
	}
    }

  g_free (candidate_normalized);
  g_free (window_normalized);
  return matches;
}

static void
screenator_companion_add_identity (GPtrArray *identities,
				   const gchar *identity)
{
  if (identity && *identity)
    g_ptr_array_add (identities, g_strdup (identity));
}

static gboolean
screenator_attach_companion_window (ScreenatorData *data, guint slot,
                                    Window app_window)
{
  GdkWindow *paper_window;
  GdkDisplay *display;
  Display *xdisplay;
  XEvent event = { 0 };

  if (!data || slot >= G_N_ELEMENTS (data->companion_app_windows) ||
      !data->paper_win || app_window == None)
    return FALSE;

  paper_window = gtk_widget_get_window (data->paper_win);
  if (!paper_window || !GDK_IS_X11_WINDOW (paper_window))
    return FALSE;

  display = gdk_window_get_display (paper_window);
  xdisplay = GDK_DISPLAY_XDISPLAY (display);
  if (!screenator_x11_window_exists (display, xdisplay, app_window))
    return FALSE;

  gdk_x11_display_error_trap_push (display);
  XSetTransientForHint (xdisplay, app_window,
                        GDK_WINDOW_XID (paper_window));
  XRaiseWindow (xdisplay, app_window);

  event.xclient.type = ClientMessage;
  event.xclient.window = app_window;
  event.xclient.message_type = XInternAtom (xdisplay, "_NET_RESTACK_WINDOW",
                                            False);
  event.xclient.format = 32;
  event.xclient.data.l[0] = 1;
  event.xclient.data.l[1] = GDK_WINDOW_XID (paper_window);
  event.xclient.data.l[2] = Above;
  XSendEvent (xdisplay, DefaultRootWindow (xdisplay), False,
              SubstructureRedirectMask | SubstructureNotifyMask, &event);
  event.xclient.message_type = XInternAtom (xdisplay, "_NET_WM_STATE", False);
  event.xclient.data.l[0] = 1;
  event.xclient.data.l[1] = XInternAtom (xdisplay, "_NET_WM_STATE_ABOVE", False);
  event.xclient.data.l[2] = 0;
  event.xclient.data.l[3] = 1;
  XSendEvent (xdisplay, DefaultRootWindow (xdisplay), False,
              SubstructureRedirectMask | SubstructureNotifyMask, &event);
  XSync (xdisplay, False);
  gdk_x11_display_error_trap_pop_ignored (display);
  data->companion_app_windows[slot] = app_window;
  return TRUE;
}

static Window
screenator_find_companion_window (ScreenatorData *data, guint slot)
{
  GdkWindow *paper_window;
  Display *xdisplay;
  Window root, parent, *children = NULL;
  unsigned int child_count;
  Atom clients_atom, actual_type;
  Atom pid_atom;
  int actual_format;
  unsigned long client_count, bytes_after;
  unsigned char *client_data = NULL;
  GPtrArray *identities = g_ptr_array_new_with_free_func (g_free);
  GAppInfo *app = screenator_companion_app_info (data, slot);
  guint i;
  Window match = None;

  if (!data || !data->paper_win)
    {
      if (app)
	g_object_unref (app);
      g_ptr_array_unref (identities);
      return None;
    }

  paper_window = gtk_widget_get_window (data->paper_win);
  if (!paper_window || !GDK_IS_X11_WINDOW (paper_window))
    {
      if (app)
	g_object_unref (app);
      g_ptr_array_unref (identities);
      return None;
    }

  xdisplay = GDK_DISPLAY_XDISPLAY (gdk_window_get_display (paper_window));
  root = DefaultRootWindow (xdisplay);
  if (app)
    {
      const gchar *name = g_app_info_get_name (app);
      const gchar *executable = g_app_info_get_executable (app);

      screenator_companion_add_identity (identities, name);
      if (executable)
	{
	  gchar *base = g_path_get_basename (executable);
	  screenator_companion_add_identity (identities, base);
	  g_free (base);
	}
      if (G_IS_DESKTOP_APP_INFO (app))
	{
	  const gchar *wm_class = g_desktop_app_info_get_startup_wm_class (
	    G_DESKTOP_APP_INFO (app));
	  screenator_companion_add_identity (identities, wm_class);
	}
    }
  if (data->companion_app_commands[slot])
    {
      gchar **argv = NULL;
      gint argc = 0;
      if (g_shell_parse_argv (data->companion_app_commands[slot], &argc,
			      &argv, NULL) && argc > 0)
	{
	  gchar *base = g_path_get_basename (argv[0]);
	  screenator_companion_add_identity (identities, base);
	  g_free (base);
	}
      g_strfreev (argv);
    }
  if (data->companion_app_ids[slot])
    {
      gchar *base = g_path_get_basename (data->companion_app_ids[slot]);
      gchar *dot;

      if (g_str_has_suffix (base, ".desktop"))
	base[strlen (base) - strlen (".desktop")] = '\0';
      screenator_companion_add_identity (identities, base);
      dot = strrchr (base, '.');
      if (dot)
	screenator_companion_add_identity (identities, dot + 1);
      g_free (base);
    }

  if (app)
    g_object_unref (app);
  if (identities->len == 0)
    {
      g_ptr_array_unref (identities);
      return None;
    }

  clients_atom = XInternAtom (xdisplay, "_NET_CLIENT_LIST_STACKING", True);
  pid_atom = XInternAtom (xdisplay, "_NET_WM_PID", False);
  if (clients_atom == None)
    clients_atom = XInternAtom (xdisplay, "_NET_CLIENT_LIST", False);
  if (XGetWindowProperty (xdisplay, root, clients_atom, 0, 2048, False,
                          XA_WINDOW, &actual_type, &actual_format,
                          &client_count, &bytes_after, &client_data) != Success ||
      actual_type != XA_WINDOW || actual_format != 32 || !client_data)
    {
      if (client_data)
	XFree (client_data);
      if (!XQueryTree (xdisplay, root, &root, &parent, &children, &child_count))
	{
	    g_ptr_array_unref (identities);
	  return None;
	}
      client_data = (unsigned char *) children;
      client_count = child_count;
    }
  for (i = 0; i < client_count; i++)
    {
      XClassHint hint = { 0 };
      Window client = ((Window *) client_data)[i];
      gboolean pid_matches = FALSE;
      if (client == GDK_WINDOW_XID (paper_window))
        continue;
      gdk_x11_display_error_trap_push (gdk_window_get_display (paper_window));
	if (data->companion_app_pids[slot] > 0)
	{
	  Atom pid_type;
	  int pid_format;
	  unsigned long pid_count, pid_bytes_after;
	  unsigned char *pid_data = NULL;

	  if (XGetWindowProperty (xdisplay, client, pid_atom, 0, 1, False,
			      XA_CARDINAL, &pid_type, &pid_format,
			      &pid_count, &pid_bytes_after, &pid_data) == Success &&
	      pid_type == XA_CARDINAL && pid_format == 32 && pid_count == 1 &&
	      pid_data)
	    pid_matches = (GPid) *((unsigned long *) pid_data) ==
	      data->companion_app_pids[slot];
	  if (pid_data)
	    XFree (pid_data);
	}

	if (pid_matches)
	  match = client;
	else if (XGetClassHint (xdisplay, client, &hint))
	  {
	    guint identity_index;
	    for (identity_index = 0; identity_index < identities->len;
		 identity_index++)
	    {
	      const gchar *identity = g_ptr_array_index (identities, identity_index);
	      if (screenator_companion_identity_matches (identity, hint.res_name) ||
		  screenator_companion_identity_matches (identity, hint.res_class))
		{
		  match = client;
		  break;
		}
	    }
	  }
	if (hint.res_name) XFree (hint.res_name);
	if (hint.res_class) XFree (hint.res_class);
      XSync (xdisplay, False);
      gdk_x11_display_error_trap_pop_ignored (
	gdk_window_get_display (paper_window));
    }
  XFree (client_data);
  g_ptr_array_unref (identities);
  return match;
}
#endif

gboolean
screenator_raise_companion_above_paper (ScreenatorData *data, guint slot)
{
#ifdef GDK_WINDOWING_X11
  if (!data || slot >= G_N_ELEMENTS (data->companion_app_windows) ||
      !data->companion_app_windows[slot])
    return FALSE;

  return screenator_attach_companion_window (
    data, slot, (Window) data->companion_app_windows[slot]);
#else
  (void) data;
  (void) slot;
  return FALSE;
#endif
}

void
screenator_clear_companion_transient (ScreenatorData *data, guint slot)
{
#ifdef GDK_WINDOWING_X11
  GdkWindow *paper_window;

  if (!data || slot >= G_N_ELEMENTS (data->companion_app_windows) ||
      !data->companion_app_windows[slot] || !data->paper_win)
    return;

  paper_window = gtk_widget_get_window (data->paper_win);
  if (paper_window && GDK_IS_X11_WINDOW (paper_window))
    {
      GdkDisplay *display = gdk_window_get_display (paper_window);
      Display *xdisplay = GDK_DISPLAY_XDISPLAY (display);

      gdk_x11_display_error_trap_push (display);
      XDeleteProperty (xdisplay, (Window) data->companion_app_windows[slot],
                       XA_WM_TRANSIENT_FOR);
      XSync (xdisplay, False);
      gdk_x11_display_error_trap_pop_ignored (display);
    }
#else
  (void) data;
  (void) slot;
#endif
  if (data && slot < G_N_ELEMENTS (data->companion_app_windows))
    data->companion_app_windows[slot] = 0;
}

typedef struct
{
  ScreenatorData *data;
  guint slot;
} CompanionWatch;

static gboolean
screenator_companion_watch (gpointer user_data)
{
  CompanionWatch *watch = user_data;
  ScreenatorData *data = watch->data;
  guint slot = watch->slot;
  gboolean found = FALSE;

  if (!data->companion_app_active[slot] ||
      data->paper == SCREENATOR_PAPER_NONE)
    goto finished;

#ifdef GDK_WINDOWING_X11
  if (data->companion_app_windows[slot])
    {
      GdkWindow *paper_window = gtk_widget_get_window (data->paper_win);
      GdkDisplay *display = gdk_window_get_display (paper_window);
      Display *xdisplay = GDK_DISPLAY_XDISPLAY (display);

      found = screenator_x11_window_exists (
	display, xdisplay, (Window) data->companion_app_windows[slot]);
    }

  if (!found)
    {
      Window app_window = screenator_find_companion_window (data, slot);

      if (app_window != None)
        found = screenator_attach_companion_window (data, slot, app_window);
    }

  if (found)
    {
      data->companion_app_bound[slot] = TRUE;
      return G_SOURCE_CONTINUE;
    }
#endif

  if (data->companion_app_bound[slot])
    goto finished;
  if (data->companion_app_watch_attempts[slot] == 0)
    {
      if (data->companion_app_pids[slot] > 0 &&
	  (kill (data->companion_app_pids[slot], 0) == 0 || errno == EPERM))
	return G_SOURCE_CONTINUE;
      goto finished;
    }

  data->companion_app_watch_attempts[slot]--;
  return G_SOURCE_CONTINUE;

 finished:
  data->companion_app_watch_id[slot] = 0;
  data->companion_app_windows[slot] = 0;
  data->companion_app_pids[slot] = 0;
  screenator_paper_companion_active (data, slot, FALSE);
  return G_SOURCE_REMOVE;
}

gboolean
screenator_launch_companion (ScreenatorData *data, guint slot)
{
  GAppInfo *app;
  GdkAppLaunchContext *context;
  GError *error = NULL;
  gboolean launched;
  GPid child_pid = 0;

  if (!data || slot >= G_N_ELEMENTS (data->companion_app_ids))
    return FALSE;

  app = screenator_companion_app_info (data, slot);
  if (!app && (!data->companion_app_commands[slot] ||
               !*data->companion_app_commands[slot]))
    {
      g_warning ("Companion App %u is not installed or configured", slot + 1);
      return FALSE;
    }

  screenator_paper_companion_active (data, slot, TRUE);
#ifdef GDK_WINDOWING_X11
  if (data->paper_win)
    {
      GdkWindow *paper_window = gtk_widget_get_window (data->paper_win);

      if (paper_window && GDK_IS_X11_WINDOW (paper_window))
        {
          Display *xdisplay = GDK_DISPLAY_XDISPLAY (
            gdk_window_get_display (paper_window));
          Window focus;
          int revert;

          XGetInputFocus (xdisplay, &focus, &revert);
          data->companion_app_previous_focus[slot] = focus;
        }
    }
#endif

  if (data->companion_app_windows[slot])
    screenator_raise_companion_above_paper (data, slot);
  data->companion_app_pids[slot] = 0;

  context = gdk_display_get_app_launch_context (gdk_display_get_default ());
  gdk_app_launch_context_set_timestamp (context, gtk_get_current_event_time ());
  if (data->companion_app_commands[slot] &&
      *data->companion_app_commands[slot])
    {
      gchar **argv = NULL;
      gint argc = 0;
      gchar *expanded_command = g_strdup (
	data->companion_app_commands[slot]);

      if (g_str_has_prefix (expanded_command, "~/"))
	{
	  gchar *expanded = g_build_filename (g_get_home_dir (),
					      expanded_command + 2, NULL);
	  g_free (expanded_command);
	  expanded_command = expanded;
	}

      /* Accept an executable or .desktop file as a single path, including
       * paths containing spaces and ~/... paths. */
      if (g_str_has_suffix (expanded_command, ".desktop") &&
	  g_file_test (expanded_command, G_FILE_TEST_IS_REGULAR))
	{
	  GDesktopAppInfo *desktop_app =
	    g_desktop_app_info_new_from_filename (expanded_command);
	  launched = desktop_app && screenator_launch_companion_app_info (
	    G_APP_INFO (desktop_app), G_APP_LAUNCH_CONTEXT (context), &child_pid,
	    &error);
	  if (desktop_app)
	    g_object_unref (desktop_app);
	}
      else if (g_file_test (expanded_command, G_FILE_TEST_IS_REGULAR) &&
	       g_file_test (expanded_command, G_FILE_TEST_IS_EXECUTABLE))
	{
	  argv = g_new0 (gchar *, 2);
	  argv[0] = g_strdup (expanded_command);
	  launched = screenator_spawn_companion_command (argv, 1, &child_pid,
							 &error);
	}
      else
	{
	  launched = g_shell_parse_argv (expanded_command, &argc, &argv, &error) &&
	    argc > 0;
	  if (launched)
	    {
	      screenator_expand_companion_path (argv);
	      launched = screenator_spawn_companion_command (argv, argc,
							     &child_pid, &error);
	    }
	}
      g_free (expanded_command);
      g_strfreev (argv);
    }
  else
	launched = screenator_launch_companion_app_info (
	  app, G_APP_LAUNCH_CONTEXT (context), &child_pid, &error);
  g_object_unref (context);
  if (app)
    g_object_unref (app);

  if (!launched)
    {
      screenator_paper_companion_active (data, slot, FALSE);
      if (error)
        g_warning ("Could not launch Companion App %u: %s", slot + 1,
                   error->message);
    }
  if (error)
    g_error_free (error);

  if (launched)
    {
      CompanionWatch *watch = g_new0 (CompanionWatch, 1);

      data->companion_app_pids[slot] = child_pid;
      data->companion_app_watch_attempts[slot] = 240;
      if (!data->companion_app_watch_id[slot])
        {
          watch->data = data;
          watch->slot = slot;
          data->companion_app_watch_id[slot] = g_timeout_add_full (
            G_PRIORITY_DEFAULT, 250, screenator_companion_watch, watch, g_free);
        }
      else
        g_free (watch);
    }

  return launched;
}

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
static guint shortcut_fallback_keycode (guint keyval);
static gboolean shortcut_event_matches_key (GdkEventKey *event, guint keyval);
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

/* Clear Screen is bound to Delete by default.  In Screen mode, keep that key
 * with the foreground application unless a non-Cursor drawing tool is active. */
static gboolean
screen_clear_shortcut_tool_active (ScreenatorData *data)
{
  return data && data->paper == SCREENATOR_PAPER_NONE &&
    data->pen_tool != 0 &&
    (data->forced_context != NULL || screenator_any_grabbed (data));
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
paper_companion_app_active (ScreenatorData *data)
{
  guint i;

  if (!data)
    return FALSE;
  for (i = 0; i < G_N_ELEMENTS (data->companion_app_active); i++)
    if (data->companion_app_active[i])
      return TRUE;
  return FALSE;
}

static gboolean
paper_shortcut_suppressed_by_companion (ScreenatorData *data, guint index)
{
  return index == SCREENATOR_SHORTCUT_QUIT &&
    paper_companion_app_active (data);
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

void
screenator_refresh_visibility_shortcut (ScreenatorData *data)
{
  gboolean active;

  if (!data || data->paper != SCREENATOR_PAPER_NONE)
    return;

  active = data->forced_context != NULL || screenator_any_grabbed (data);
  grab_paper_key (data,
	  data->shortcuts[SCREENATOR_SHORTCUT_TOGGLE_VISIBILITY].keycode,
	  data->shortcuts[SCREENATOR_SHORTCUT_TOGGLE_VISIBILITY].modifier,
	  active);
}

void
screenator_refresh_clear_shortcut (ScreenatorData *data)
{
  gboolean active;

  if (!data || data->paper != SCREENATOR_PAPER_NONE)
    return;

  active = screen_clear_shortcut_tool_active (data);
  grab_paper_key (data,
	  data->shortcuts[SCREENATOR_SHORTCUT_CLEAR_SCREEN].keycode,
	  data->shortcuts[SCREENATOR_SHORTCUT_CLEAR_SCREEN].modifier,
	  active);
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
	shortcut->keycode = paper_shortcut_keycode (keyval);
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

  if (!keycode)
    keycode = shortcut_fallback_keycode (keyval);

  return keycode;
}

/* Physical X11/evdev keycodes for the Latin keys.  Arabic and some other
 * layouts do not expose Latin keysyms at all, so get_entries_for_keyval()
 * cannot resolve Ctrl+D, Ctrl+W and similar shortcuts there.  Evdev keycodes
 * are layout independent and are the same physical keys used by the layout.
 * The fallback is only used when the keymap lookup fails. */
static guint
shortcut_fallback_keycode (guint keyval)
{
  guint key = gdk_keyval_to_lower (keyval);

  /* Number row. */
  if (key >= GDK_KEY_1 && key <= GDK_KEY_9)
    return 10 + (key - GDK_KEY_1);
  if (key == GDK_KEY_0)
    return 19;

  /* Top letter row. */
  if (key >= GDK_KEY_q && key <= GDK_KEY_p)
    return 24 + (key - GDK_KEY_q);

  /* Home letter row. */
  if (key >= GDK_KEY_a && key <= GDK_KEY_l)
    return 38 + (key - GDK_KEY_a);

  /* Bottom letter row. */
  if (key >= GDK_KEY_z && key <= GDK_KEY_m)
    return 52 + (key - GDK_KEY_z);

  switch (key)
    {
    case GDK_KEY_minus:      return 20;
    case GDK_KEY_equal:      return 21;
    case GDK_KEY_bracketleft: return 34;
    case GDK_KEY_bracketright: return 35;
    case GDK_KEY_backslash:  return 51;
    case GDK_KEY_semicolon:  return 47;
    case GDK_KEY_apostrophe: return 48;
    case GDK_KEY_grave:      return 49;
    case GDK_KEY_comma:      return 59;
    case GDK_KEY_period:     return 60;
    case GDK_KEY_slash:      return 61;
    case GDK_KEY_space:      return 65;
    default:
      return 0;
    }
}

static gboolean
shortcut_event_matches_key (GdkEventKey *event, guint keyval)
{
  guint keycode;

  if (!event)
    return FALSE;

  keycode = paper_shortcut_keycode (keyval);
  if (keycode)
    return event->hardware_keycode == keycode;

  return event->keyval == keyval;
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

static gboolean
shortcut_tool_is_active (ScreenatorData *data)
{
  return data->paper == SCREENATOR_PAPER_NONE && data->pen_tool != 0 &&
    (data->forced_context != NULL || screenator_any_grabbed (data));
}

void
screenator_refresh_cycle_setting_shortcut (ScreenatorData *data)
{
  gboolean active;

  if (!data)
    return;

  active = data->paper != SCREENATOR_PAPER_NONE ||
    shortcut_tool_is_active (data);
  if (!window_mode_shortcut_may_grab (
	data, SCREENATOR_SHORTCUT_CYCLE_ACTIVE_SETTING))
    active = FALSE;
  if (paper_shortcut_suppressed_by_companion (
	data, SCREENATOR_SHORTCUT_CYCLE_ACTIVE_SETTING))
    active = FALSE;

  grab_paper_key (
    data, data->shortcuts[SCREENATOR_SHORTCUT_CYCLE_ACTIVE_SETTING].keycode,
    data->shortcuts[SCREENATOR_SHORTCUT_CYCLE_ACTIVE_SETTING].modifier,
    active);
}

/* Replace the configured shortcuts, ungrab the old keys and grab the new
 * ones according to their current scope. */
void
screenator_apply_shortcuts (ScreenatorData *data, gchar * const *new_texts)
{
  gboolean paper = data->paper != SCREENATOR_PAPER_NONE;
  gboolean painting = screenator_any_grabbed (data) ||
    data->forced_context != NULL;
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
      else if (screenator_shortcut_scopes[i] ==
	       SCREENATOR_SHORTCUT_PAINTING_OR_PAPER)
	grab = painting || paper;
      else if (screenator_shortcut_scopes[i] ==
	       SCREENATOR_SHORTCUT_TOOL_OR_PAPER)
	grab = paper || shortcut_tool_is_active (data);

      if (!window_mode_shortcut_may_grab (data, i))
	grab = FALSE;
	if (data->paper == SCREENATOR_PAPER_NONE &&
	    i == SCREENATOR_SHORTCUT_CLEAR_SCREEN &&
	    !screen_clear_shortcut_tool_active (data))
	grab = FALSE;
      if (paper_shortcut_suppressed_by_companion (data, i))
	grab = FALSE;

      grab_paper_key (data, data->shortcuts[i].keycode,
		      data->shortcuts[i].modifier, grab);
    }
}

/* Grab the Paper Drawing shortcuts so they work while the drawing window is
 * active.  Ctrl+Z stays with the key snooper (and the painting grab),
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
  gboolean painting, paper;
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

  painting = screenator_any_grabbed (data) || data->forced_context != NULL;
  paper = data->paper != SCREENATOR_PAPER_NONE;

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
	  painting) ||
	 (screenator_shortcut_scopes[i] ==
	  SCREENATOR_SHORTCUT_PAINTING_OR_PAPER && (paper || painting)) ||
	 (screenator_shortcut_scopes[i] ==
	  SCREENATOR_SHORTCUT_TOOL_OR_PAPER &&
	  (paper || shortcut_tool_is_active (data))));
      if (paper_shortcut_suppressed_by_companion (data, i))
	grab = FALSE;

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
      else if (screenator_shortcut_scopes[i] ==
	       SCREENATOR_SHORTCUT_PAINTING_OR_PAPER)
	on = grab ||
	  (data->paper == SCREENATOR_PAPER_NONE &&
	   (data->forced_context != NULL || screenator_any_grabbed (data)));
      else if (screenator_shortcut_scopes[i] ==
	       SCREENATOR_SHORTCUT_TOOL_OR_PAPER)
	on = grab || shortcut_tool_is_active (data);

      if (!window_mode_shortcut_may_grab (data, i))
	on = FALSE;
	if (data->paper == SCREENATOR_PAPER_NONE &&
	    i == SCREENATOR_SHORTCUT_CLEAR_SCREEN &&
	    !screen_clear_shortcut_tool_active (data))
	on = FALSE;
      if (paper_shortcut_suppressed_by_companion (data, i))
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
  gboolean painting = screenator_any_grabbed (data) ||
    data->forced_context != NULL;
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
	  else if (screenator_shortcut_scopes[i] ==
		   SCREENATOR_SHORTCUT_PAINTING_OR_PAPER)
	    grab = painting || paper;
	  else if (screenator_shortcut_scopes[i] ==
		   SCREENATOR_SHORTCUT_TOOL_OR_PAPER)
	    grab = paper || shortcut_tool_is_active (data);
	  if (paper_shortcut_suppressed_by_companion (data, i))
	    grab = FALSE;

	  if (!window_mode_shortcut_may_grab (data, i))
	    grab = FALSE;
	  if (data->paper == SCREENATOR_PAPER_NONE &&
	      i == SCREENATOR_SHORTCUT_CLEAR_SCREEN &&
	      !screen_clear_shortcut_tool_active (data))
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
      screenator_refresh_visibility_shortcut (data);
      screenator_refresh_clear_shortcut (data);
      screenator_refresh_cycle_setting_shortcut (data);

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

      screenator_refresh_visibility_shortcut (data);
      screenator_refresh_clear_shortcut (data);
      screenator_refresh_cycle_setting_shortcut (data);
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
      screenator_refresh_visibility_shortcut (data);
      screenator_refresh_clear_shortcut (data);
      screenator_refresh_cycle_setting_shortcut (data);
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
      screenator_refresh_visibility_shortcut (data);
      screenator_refresh_clear_shortcut (data);
      screenator_refresh_cycle_setting_shortcut (data);

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

  /* Ctrl+C cycles the active tool's setting.  It remains available in Paper
   * mode even when no drawing tool is selected, and follows the selected
   * tool in Screen mode. */
  if ((data->paper != SCREENATOR_PAPER_NONE ||
       shortcut_tool_is_active (data)) &&
      screenator_shortcut_matches (
	data, SCREENATOR_SHORTCUT_CYCLE_ACTIVE_SETTING, event))
    {
      screenator_cycle_active_setting (data);
      return TRUE;
    }

  /* Shortcuts that work while Paper mode has the keyboard focus.  The quick
   * tool menu gets the arrow keys first, and the inline text editor keeps
   * Ctrl+A for selecting text. */
  if (data->paper != SCREENATOR_PAPER_NONE && !data->quick_menu)
    {
      if (screenator_shortcut_matches (data, SCREENATOR_SHORTCUT_QUIT, event))
	{
	  if (!paper_companion_app_active (data))
	    screenator_request_quit (data);
	  return TRUE;
	}

      if (!data->text_editor_active &&
	  screenator_shortcut_matches (
	    data, SCREENATOR_SHORTCUT_TOGGLE_VISIBILITY, event))
	{
	  toggle_drawings_visibility (data);
	  return TRUE;
	}

      if (screenator_shortcut_matches (
	    data, SCREENATOR_SHORTCUT_CLEAR_SCREEN, event))
	{
	  clear_screen (data);
	  return TRUE;
	}

      if (!data->text_editor_active &&
	  screenator_shortcut_matches (
	    data, SCREENATOR_SHORTCUT_DELETE_PAGE, event))
	{
	  screenator_paper_delete_page (data);
	  return TRUE;
	}

      if (!data->text_editor_active &&
	  (event->state & GDK_CONTROL_MASK) != 0 &&
	  (event->state & (GDK_SHIFT_MASK | GDK_MOD1_MASK)) == 0 &&
	  shortcut_event_matches_key (event, GDK_KEY_o))
	{
	  screenator_paper_open_pdf (data);
	  return TRUE;
	}

      /* Ctrl+W switches between fullscreen and Window mode. */
      if ((event->state & GDK_CONTROL_MASK) != 0 &&
	  (event->state & (GDK_SHIFT_MASK | GDK_MOD1_MASK)) == 0 &&
	  shortcut_event_matches_key (event, GDK_KEY_w))
	{
	  screenator_toggle_paper_window_fullscreen (data);
	  return TRUE;
	}

      /* Ctrl+D shows the desktop panel above the page without entering
       * Window mode. */
      if ((event->state & GDK_CONTROL_MASK) != 0 &&
	  (event->state & (GDK_SHIFT_MASK | GDK_MOD1_MASK)) == 0 &&
	  shortcut_event_matches_key (event, GDK_KEY_d))
	{
	  screenator_toggle_paper_panel (data);
	  return TRUE;
	}

      if (!data->text_editor_active)
	{
	  static const ScreenatorShortcutId companion_shortcuts[] =
	    { SCREENATOR_SHORTCUT_COMPANION_1,
	      SCREENATOR_SHORTCUT_COMPANION_2,
	      SCREENATOR_SHORTCUT_COMPANION_3,
	      SCREENATOR_SHORTCUT_COMPANION_4,
	      SCREENATOR_SHORTCUT_COMPANION_5,
	      SCREENATOR_SHORTCUT_COMPANION_6,
	      SCREENATOR_SHORTCUT_COMPANION_7 };
	  guint slot;

	  for (slot = 0; slot < G_N_ELEMENTS (companion_shortcuts); slot++)
	    if (screenator_shortcut_matches (data, companion_shortcuts[slot],
					     event))
	      {
		screenator_launch_companion (data, slot);
		return TRUE;
	      }
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

  /* Ctrl+T works in Screen mode while an annotation tool is active. */
  if (data->paper == SCREENATOR_PAPER_NONE && !data->quick_menu &&
      (screenator_any_grabbed (data) || data->forced_context != NULL) &&
      screenator_shortcut_matches (
	data, SCREENATOR_SHORTCUT_TOGGLE_VISIBILITY, event))
    {
      if (data->text_editor_active)
	return FALSE;
      toggle_drawings_visibility (data);
      return TRUE;
    }

	  if (!data->quick_menu &&
	      screen_clear_shortcut_tool_active (data) &&
	      screenator_shortcut_matches (
	data, SCREENATOR_SHORTCUT_CLEAR_SCREEN, event))
    {
      clear_screen (data);
      return TRUE;
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
