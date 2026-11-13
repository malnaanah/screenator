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

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <glib/gstdio.h>

#include "config.h"
#include "screenator.h"

/* Move one old configuration file into the new directory when the new copy
 * does not exist yet.  Rename is preferred; copying handles unusual setups. */
static void
config_move_legacy_file (const gchar *old_path, const gchar *new_path)
{
  gchar *contents = NULL;
  gsize length = 0;

  if (g_file_test (new_path, G_FILE_TEST_EXISTS) ||
      !g_file_test (old_path, G_FILE_TEST_IS_REGULAR))
    return;

  if (g_rename (old_path, new_path) == 0)
    return;

  if (g_file_get_contents (old_path, &contents, &length, NULL))
    {
      if (g_file_set_contents (new_path, contents, (gssize) length, NULL))
	g_remove (old_path);
      g_free (contents);
    }
}

/* All Screenator state lives in ~/.config/Screenator.  Keep accepting the
 * old flat ~/.config/screenator.* names by moving them once. */
static void
config_migrate_legacy_files (const gchar *config_dir)
{
  static gboolean migrated = FALSE;
  const gchar *old_names[] = {
    "screenator.ini",
    "screenator.cfg",
    "screenator-annotations.png"
  };
  guint i;

  if (migrated)
    return;
  migrated = TRUE;

  for (i = 0; i < G_N_ELEMENTS (old_names); i++)
    {
      gchar *old_path = g_build_filename (g_get_user_config_dir (),
					 old_names[i], NULL);
      gchar *new_path = g_build_filename (config_dir, old_names[i], NULL);

      config_move_legacy_file (old_path, new_path);
      g_free (old_path);
      g_free (new_path);
    }
}

static void
config_remove_tree (const gchar *path)
{
  GDir *dir = g_dir_open (path, 0, NULL);

  if (dir)
    {
      const gchar *name;

      while ((name = g_dir_read_name (dir)) != NULL)
	{
	  gchar *child = g_build_filename (path, name, NULL);

	  if (g_file_test (child, G_FILE_TEST_IS_DIR) &&
	      !g_file_test (child, G_FILE_TEST_IS_SYMLINK))
	    config_remove_tree (child);
	  else
	    g_remove (child);

	  g_free (child);
	}

      g_dir_close (dir);
    }

  g_rmdir (path);
}

void
screenator_config_remove_all (void)
{
  static const gchar * const old_files[] = {
    "screenator.ini",
    "screenator.cfg",
    "screenator-annotations.png"
  };
  gchar *dir;
  gchar *old_dir;
  guint i;

  dir = g_build_filename (g_get_user_config_dir (), "Screenator", NULL);
  config_remove_tree (dir);
  g_free (dir);

  /* Also remove the pre-8.0 flat files and hints directory so the next start
   * cannot migrate old settings back in. */
  for (i = 0; i < G_N_ELEMENTS (old_files); i++)
    {
      gchar *path = g_build_filename (g_get_user_config_dir (),
				      old_files[i], NULL);

      g_remove (path);
      g_free (path);
    }

  old_dir = g_build_filename (g_get_user_config_dir (), "screenator", NULL);
  config_remove_tree (old_dir);
  g_free (old_dir);
}

gchar *
screenator_config_path (const gchar *name)
{
  gchar *config_dir =
    g_build_filename (g_get_user_config_dir (), "Screenator", NULL);

  g_mkdir_with_parents (config_dir, 0700);
  config_migrate_legacy_files (config_dir);

  {
    gchar *path = g_build_filename (config_dir, name, NULL);

    g_free (config_dir);
    return path;
  }
}
#include "build-config.h"


#define KEYFILE_FLAGS G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS

const gchar * const screenator_shortcut_defaults[SCREENATOR_SHORTCUT_COUNT] =
{
  "F9",          /* floating icon */
  "F8",          /* pen */
  "F7",          /* text */
  "F6",          /* arrow */
  "F12",         /* timer */
  "F10",         /* toolbar */
  "F3",          /* switch mode */
  "F4",          /* screenshot */
  "Ctrl+Z",      /* undo */
  "Ctrl+Shift+Z",/* redo */
  "Ctrl+S",      /* save */
  "Ctrl+B",      /* insert before */
  "Ctrl+A",      /* insert after */
  "Ctrl+T",      /* toggle visibility */
  "Ctrl+Q",      /* exit paper */
  "Alt+F4",      /* quit */
  "Escape"       /* escape / cancel */
};

const gchar * const screenator_shortcut_labels[SCREENATOR_SHORTCUT_COUNT] =
{
  "Show floating icon",
  "Pen tool",
  "Text tool",
  "Arrow tool",
  "Timer",
  "Show tool bar",
  "Switch Screen/Paper mode",
  "Screenshot",
  "Undo",
  "Redo",
  "Save paper as PDF",
  "Insert page before",
  "Insert page after",
  "Toggle drawings visibility",
  "Exit Paper mode",
  "Quit",
  "Cancel / Escape"
};

const ScreenatorShortcutScope screenator_shortcut_scopes[
  SCREENATOR_SHORTCUT_COUNT] =
{
  SCREENATOR_SHORTCUT_ALWAYS,
  SCREENATOR_SHORTCUT_ALWAYS,
  SCREENATOR_SHORTCUT_ALWAYS,
  SCREENATOR_SHORTCUT_ALWAYS,
  SCREENATOR_SHORTCUT_ALWAYS,
  SCREENATOR_SHORTCUT_ALWAYS,
  SCREENATOR_SHORTCUT_ALWAYS,
  SCREENATOR_SHORTCUT_ALWAYS,
  SCREENATOR_SHORTCUT_PAINTING,
  SCREENATOR_SHORTCUT_PAINTING,
  SCREENATOR_SHORTCUT_PAPER,
  SCREENATOR_SHORTCUT_PAPER,
  SCREENATOR_SHORTCUT_PAPER,
  SCREENATOR_SHORTCUT_PAINTING,
  SCREENATOR_SHORTCUT_PAPER,
  SCREENATOR_SHORTCUT_PAPER,
  SCREENATOR_SHORTCUT_PAINTING
};

static gpointer HOTKEY_SYMBOL_VALUE  = (gpointer) 3;
static gpointer UNDOKEY_SYMBOL_VALUE = (gpointer) 4;

/*
 * Functions for parsing the Configuration-file
 */

static gchar* parse_name (GScanner *scanner)
{
  GTokenType token;

  guint buttons = 0;
  guint modifier = 0;
  guint len = 0;
  gchar *name;

  token = g_scanner_cur_token(scanner);

  if (token != G_TOKEN_STRING)
    {
      g_scanner_unexp_token (scanner, G_TOKEN_STRING, NULL,
                             NULL, NULL, "aborting", TRUE);
      exit (1);
    }

  len = strlen (scanner->value.v_string);
  name = g_strndup (scanner->value.v_string, len + 3);

  token = g_scanner_get_next_token (scanner);

  /*
   * Are there any options to limit the scope of the definition?
   */

  if (token == G_TOKEN_LEFT_BRACE)
    {
      g_scanner_set_scope (scanner, 1);
      scanner->config->int_2_float = 0;
      modifier = buttons = 0;
      while ((token = g_scanner_get_next_token (scanner))
             != G_TOKEN_RIGHT_BRACE)
        {
          if (token == G_TOKEN_SYMBOL)
            {
              if ((intptr_t) scanner->value.v_symbol < 11)
                 buttons |= 1 << ((intptr_t) scanner->value.v_symbol - 1);
              else
                 modifier |= 1 << ((intptr_t) scanner->value.v_symbol - 11);
            }
          else if (token == G_TOKEN_INT)
            {
              if (scanner->value.v_int <= 5 && scanner->value.v_int > 0)
                buttons |= 1 << (scanner->value.v_int - 1);
              else
                g_printerr ("Only Buttons 1-5 are supported!\n");
            }
          else
            {
              g_printerr ("skipped token\n");
            }
        }
      g_scanner_set_scope (scanner, 0);
      scanner->config->int_2_float = 1;
      token = g_scanner_get_next_token (scanner);
    }

  name [len] = 124;
  name [len+1] = buttons + 64;
  name [len+2] = modifier + 48;
  name [len+3] = 0;

  return name;
}

void parse_config (ScreenatorData *data)
{
  ScreenatorPaintContext *context=NULL;
  ScreenatorPaintContext *context_template=NULL;
  GScanner *scanner;
  GTokenType token;
  gchar *filename;
  int file;

  gchar *name, *copy;

  ScreenatorPaintType type;
  GdkRGBA *fg_color=NULL;
  guint width, arrowsize, minwidth;

  /* try user config location */
  filename = screenator_config_path ("screenator.cfg");
  if ((file = open(filename, O_RDONLY)) < 0)
      g_print("Could not open user config %s: %s\n", filename, g_strerror (errno));
  else
      g_print("Using user config %s\n", filename);


  /* try global config file */
  if (file < 0) {
      const gchar *env_cfg;

      g_free(filename);
      /* Inside an AppImage the configuration is bundled with the app and
       * its path is exported by AppRun. */
      env_cfg = g_getenv ("SCREENATOR_CFG");
      if (env_cfg && *env_cfg)
	  filename = g_strdup (env_cfg);
      else
	  filename = g_strdup (SYSCONFDIR "/screenator/screenator.cfg");
      if ((file = open(filename, O_RDONLY)) < 0)
	  g_print("Could not open system config %s: %s\n", filename, g_strerror (errno));
      else
	  g_print("Using system config %s\n", filename);
  }

  /* was the last possibility, no use to go on */
  if (file < 0) {
      g_printerr("No usable config found, leaving tools unconfigured!\n");
      g_free(filename);
      return;
  }

  scanner = g_scanner_new (NULL);
  scanner->input_name = filename;
  scanner->config->case_sensitive = 0;
  scanner->config->scan_octal = 0;
  scanner->config->identifier_2_string = 0;
  scanner->config->char_2_token = 1;
  scanner->config->numbers_2_int = 1;
  scanner->config->int_2_float = 1;

  g_scanner_scope_add_symbol (scanner, 0, "PEN",    (gpointer) SCREENATOR_PEN);
  g_scanner_scope_add_symbol (scanner, 0, "ERASER", (gpointer) SCREENATOR_ERASER);
  g_scanner_scope_add_symbol (scanner, 0, "RECOLOR",(gpointer) SCREENATOR_RECOLOR);
  g_scanner_scope_add_symbol (scanner, 0, "HOTKEY",            HOTKEY_SYMBOL_VALUE);
  g_scanner_scope_add_symbol (scanner, 0, "UNDOKEY",           UNDOKEY_SYMBOL_VALUE);

  g_scanner_scope_add_symbol (scanner, 1, "BUTTON1", (gpointer) 1);
  g_scanner_scope_add_symbol (scanner, 1, "BUTTON2", (gpointer) 2);
  g_scanner_scope_add_symbol (scanner, 1, "BUTTON3", (gpointer) 3);
  g_scanner_scope_add_symbol (scanner, 1, "BUTTON4", (gpointer) 4);
  g_scanner_scope_add_symbol (scanner, 1, "BUTTON5", (gpointer) 5);
  g_scanner_scope_add_symbol (scanner, 1, "SHIFT",   (gpointer) 11);
  g_scanner_scope_add_symbol (scanner, 1, "CONTROL", (gpointer) 12);
  g_scanner_scope_add_symbol (scanner, 1, "META",    (gpointer) 13);
  g_scanner_scope_add_symbol (scanner, 1, "ALT",     (gpointer) 13);

  g_scanner_scope_add_symbol (scanner, 2, "size",      (gpointer) 1);
  g_scanner_scope_add_symbol (scanner, 2, "color",     (gpointer) 2);
  g_scanner_scope_add_symbol (scanner, 2, "arrowsize", (gpointer) 3);
  g_scanner_scope_add_symbol (scanner, 2, "minsize",   (gpointer) 4);

  g_scanner_set_scope (scanner, 0);
  scanner->config->scope_0_fallback = 0;

  g_scanner_input_file (scanner, file);

  token = g_scanner_get_next_token (scanner);
  while (token != G_TOKEN_EOF)
    {
      if (token == G_TOKEN_STRING)
        {
          /*
           * New tool definition
           */

          name = parse_name (scanner);
          token = g_scanner_cur_token(scanner);

          if (token != G_TOKEN_EQUAL_SIGN)
            {
              g_scanner_unexp_token (scanner, G_TOKEN_EQUAL_SIGN, NULL,
                                     NULL, NULL, "aborting", TRUE);
              exit (1);
            }

          token = g_scanner_get_next_token (scanner);

          /* defaults */

          type = SCREENATOR_PEN;
          width = 7;
          arrowsize = 0;
          minwidth = 1;
          fg_color = data->red;

          if (token == G_TOKEN_SYMBOL)
            {
              type = (ScreenatorPaintType) scanner->value.v_symbol;
              token = g_scanner_get_next_token (scanner);
            }
          else if (token == G_TOKEN_STRING)
            {
              copy = parse_name (scanner);
              token = g_scanner_cur_token(scanner);
              context_template = g_hash_table_lookup (data->tool_config, copy);
              if (context_template)
                {
                  type = context_template->type;
                  width = context_template->width;
                  arrowsize = context_template->arrowsize;
                  minwidth = context_template->minwidth;
                  fg_color = context_template->paint_color;
                }
              else
                {
                  g_printerr ("WARNING: Unable to copy \"%s\": "
                              "not yet defined!\n", copy);
                }
            }
          else
            {
              g_printerr ("Expected Tool-definition "
                          "or name of template tool\n");
              exit (1);
            }

          /* Are there any tool-options?
           */

          if (token == G_TOKEN_LEFT_PAREN)
            {
              GdkRGBA *color = NULL;
              g_scanner_set_scope (scanner, 2);
              scanner->config->int_2_float = 1;
              token = g_scanner_get_next_token (scanner);
              while (token != G_TOKEN_RIGHT_PAREN)
                {
                  if (token == G_TOKEN_SYMBOL)
                    {
                      if ((intptr_t) scanner->value.v_symbol == 1)
                        {
                          token = g_scanner_get_next_token (scanner);
                          if (token != G_TOKEN_EQUAL_SIGN)
                            {
                              g_printerr ("Missing \"=\"... aborting\n");
                              exit (1);
                            }
                          token = g_scanner_get_next_token (scanner);
                          if (token != G_TOKEN_FLOAT)
                            {
                              g_printerr ("Missing Size (float)... aborting\n");
                              exit (1);
                            }
                          width = (guint) (scanner->value.v_float + 0.5);
                        }
                      else if ((intptr_t) scanner->value.v_symbol == 2)
                        {
                          token = g_scanner_get_next_token (scanner);
                          if (token != G_TOKEN_EQUAL_SIGN)
                            {
                              g_printerr ("Missing \"=\"... aborting\n");
                              exit (1);
                            }
                          token = g_scanner_get_next_token (scanner);
                          if (token != G_TOKEN_STRING)
                            {
                              g_printerr ("Missing Color (string)... "
                                          "aborting\n");
                              exit (1);
                            }
                          color = g_malloc (sizeof (GdkRGBA));
                          if (gdk_rgba_parse (color, scanner->value.v_string))
                            {
			      fg_color = color;
                            }
                          else
                            {
                              g_printerr ("Unable to parse color. "
                                          "Keeping default.\n");
                              g_free (color);
                            }
                          color = NULL;
                        }
                      else if ((intptr_t) scanner->value.v_symbol == 3)
                        {
                          token = g_scanner_get_next_token (scanner);
                          if (token != G_TOKEN_EQUAL_SIGN)
                            {
                              g_printerr ("Missing \"=\"... aborting\n");
                              exit (1);
                            }
                          token = g_scanner_get_next_token (scanner);
                          if (token != G_TOKEN_FLOAT)
                            {
                              g_printerr ("Missing Arrowsize (float)... "
                                          "aborting\n");
                              exit (1);
                            }
                          arrowsize = scanner->value.v_float;
                        }
                      else if ((intptr_t) scanner->value.v_symbol == 4)
                        {
                          token = g_scanner_get_next_token (scanner);
                          if (token != G_TOKEN_EQUAL_SIGN)
                            {
                              g_printerr ("Missing \"=\"... aborting\n");
                              exit (1);
                            }
                          token = g_scanner_get_next_token (scanner);
                          if (token != G_TOKEN_FLOAT)
                            {
                              g_printerr ("Missing Minsize (float)... "
                                          "aborting\n");
                              exit (1);
                            }
                          minwidth = scanner->value.v_float;
                        }
		      else
                        {
                          g_printerr ("Unknown tool type?????\n");
                        }
                    }
                  else
                    {
                      g_printerr ("skipped token!!!\n");
                    }
                  token = g_scanner_get_next_token (scanner);
                }
              g_scanner_set_scope (scanner, 0);
              token = g_scanner_get_next_token (scanner);
            }

          /*
           * Finally we expect a semicolon
           */

          if (token != ';')
            {
              g_printerr ("Expected \";\"\n");
              exit (1);
            }

          context = paint_context_new (data, type, fg_color, width, arrowsize, minwidth);
          g_hash_table_insert (data->tool_config, name, context);
        }
      else if (token == G_TOKEN_SYMBOL &&
               (scanner->value.v_symbol == HOTKEY_SYMBOL_VALUE ||
                scanner->value.v_symbol == UNDOKEY_SYMBOL_VALUE))
        {
          /*
           * Hot key definition
           */

          gpointer key_type = scanner->value.v_symbol;
          token = g_scanner_get_next_token(scanner);

          if (token != G_TOKEN_EQUAL_SIGN)
            {
              g_scanner_unexp_token (scanner, G_TOKEN_EQUAL_SIGN, NULL,
                                     NULL, NULL, "aborting", TRUE);
              exit (1);
            }

          token = g_scanner_get_next_token (scanner);

          if (token != G_TOKEN_STRING)
            {
              g_scanner_unexp_token (scanner, G_TOKEN_STRING, NULL,
                                     NULL, NULL, "aborting", TRUE);
              exit (1);
            }

          if (key_type == HOTKEY_SYMBOL_VALUE)
            {
              data->hot_keyval = g_strdup(scanner->value.v_string);
            }
          /* UNDOKEY is still accepted so older configuration files parse,
           * but the separate undo key is no longer used. */

          token = g_scanner_get_next_token(scanner);

          if (token != ';')
            {
              g_printerr ("Expected \";\"\n");
              exit (1);
            }
        }
      else
        {
          g_printerr ("Expected name of Tool to define or Hot key definition\n");
          exit(1);
        }

      token = g_scanner_get_next_token (scanner);
    }
  g_scanner_destroy (scanner);
  close (file);
  g_free (filename);
}


static void
read_pen_color (GKeyFile *key_file, GdkRGBA *color)
{
    gchar *value = g_key_file_get_string (key_file, "Drawing", "PenColor",
					  NULL);

    if (value)
	{
	    GdkRGBA parsed;

	    if (gdk_rgba_parse (&parsed, value))
		{
		    /* Opacity is stored separately, so only keep the hue. */
		    parsed.alpha = 1.0;
		    *color = parsed;
		}
	    g_free (value);
	}
}


static gint
read_pen_tool (GKeyFile *key_file)
{
    static const gchar * const names[] = { "none", "pen", "arrow", "eraser",
					   "text", "rectangle", "ellipse",
					   "rounded-rectangle", "angled-line",
					   "free-line", "angled-arrow", "marker",
					   "spline", "bucket", "polygon", "triangle",
					   "math", "bars" };
    gchar *value = g_key_file_get_string (key_file, "Drawing", "Tool", NULL);
    gint tool = 0;

    if (value)
	{
	    guint i;

	    for (i = 0; i < G_N_ELEMENTS (names); i++)
		if (g_ascii_strcasecmp (value, names[i]) == 0)
		    {
			tool = i;
			break;
		    }

	    /* Older versions saved the Polygon tool as "star". */
	    if (g_ascii_strcasecmp (value, "star") == 0)
		tool = 14;

	    g_free (value);
	}

    return tool;
}


#define PAPER_TOOLBAR_ITEMS_VERSION 4

/* Older saved toolbars predate Delete Page, Show Hints, Go to Page and the
 * current-page indicator.  Add them once, then record the migration version
 * so hiding them in Settings stays respected on later starts. */
static gchar *
toolbar_items_migrate (const gchar *items, gint version)
{
  gchar **tokens;
  GString *out;
  gboolean any = FALSE;
  gboolean has_delete = FALSE;
  gboolean has_hints = FALSE;
  gboolean has_goto = FALSE;
  gboolean has_page_number = FALSE;
  gboolean add_delete;
  gboolean add_hints;
  gboolean add_goto;
  gboolean add_page_number;
  guint i;

  if (!items || !*items || version >= PAPER_TOOLBAR_ITEMS_VERSION)
    return g_strdup (items ? items : "");

  tokens = g_strsplit (items, ",", -1);

  for (i = 0; tokens[i]; i++)
    {
      gchar *id = g_strstrip (tokens[i]);

      if (*id)
	any = TRUE;
      if (g_strcmp0 (id, "delete_page") == 0)
	has_delete = TRUE;
      if (g_strcmp0 (id, "hints") == 0)
	has_hints = TRUE;
      if (g_strcmp0 (id, "goto_page") == 0)
	has_goto = TRUE;
      if (g_strcmp0 (id, "page_number") == 0)
	has_page_number = TRUE;
    }

  if (!any)
    {
      g_strfreev (tokens);
      return g_strdup (items);
    }

  add_delete = !has_delete;
  add_hints = !has_hints;
  add_goto = !has_goto;
  add_page_number = !has_page_number;
  out = g_string_new (NULL);

  for (i = 0; tokens[i]; i++)
    {
      gchar *id = g_strstrip (tokens[i]);

      if (!*id)
	continue;

      if (out->len > 0)
	g_string_append_c (out, ',');

      g_string_append (out, id);

      if (add_page_number && g_strcmp0 (id, "insert_after") == 0)
	{
	  g_string_append (out, ",page_number");
	  add_page_number = FALSE;
	}
      else if (add_page_number && g_strcmp0 (id, "goto_page") == 0)
	{
	  /* No Insert Page After in this saved layout: keep the indicator
	   * immediately before Go to Page. */
	  g_string_append (out, ",page_number");
	  add_page_number = FALSE;
	}

      if (add_delete && g_strcmp0 (id, "insert_before") == 0)
	{
	  g_string_append (out, ",delete_page");
	  add_delete = FALSE;
	}

      if (add_hints && g_strcmp0 (id, "settings") == 0)
	{
	  g_string_append (out, ",hints");
	  add_hints = FALSE;
	}

      if (add_goto && g_strcmp0 (id, "insert_after") == 0)
	{
	  g_string_append (out, ",goto_page");
	  add_goto = FALSE;
	}
    }

  if (add_delete)
    {
      if (out->len > 0)
	g_string_append_c (out, ',');
      g_string_append (out, "delete_page");
    }

  if (add_hints)
    {
      if (out->len > 0)
	g_string_append_c (out, ',');
      g_string_append (out, "hints");
    }

  if (add_goto)
    {
      if (out->len > 0)
	g_string_append_c (out, ',');
      g_string_append (out, "goto_page");
    }

  if (add_page_number)
    {
      if (out->len > 0)
	g_string_append_c (out, ',');
      g_string_append (out, "page_number");
    }

  g_strfreev (tokens);
  return g_string_free (out, FALSE);
}

void read_keyfile(ScreenatorData *data)
{
    gchar *filename = screenator_config_path ("screenator.ini");

    /*
      set defaults
    */
    data->opacity = DEFAULT_OPACITY;
    data->marker_opacity = 0.40;
    data->pen_width = KEY_DFLT_PEN_WIDTH;
    data->eraser_width = KEY_DFLT_ERASER_WIDTH;
    gdk_rgba_parse (&data->pen_color, KEY_DFLT_PEN_COLOR);
    gdk_rgba_parse (&data->custom_line_color, "#FF00FF");
    data->custom_line_color.alpha = 1.0;
    data->pen_tool = 0;
    data->icon_size = KEY_DFLT_ICON_SIZE;
    data->icon_position = SCREENATOR_ICON_TOP_RIGHT;
    data->tool_delay_seconds = 2;
    data->tool_delay_id = 0;
    data->timer_active = FALSE;
    data->force_active_icon = FALSE;
    data->font = g_strdup (KEY_DFLT_FONT);
    data->shape_fill_enabled = FALSE;
    gdk_rgba_parse (&data->shape_fill_color, KEY_DFLT_SHAPE_FILL_COLOR);
    data->shape_fill_color.alpha = 1.0;
    gdk_rgba_parse (&data->custom_fill_color, "#FF00FF");
    data->custom_fill_color.alpha = 1.0;
    data->paper = SCREENATOR_PAPER_NONE;
    data->paper_continuous_scroll = TRUE;
    data->paper_size = SCREENATOR_PAPER_SIZE_SCREEN;
    data->paper_custom_width_mm = 210.0;
    data->paper_custom_height_mm = 297.0;
    data->paper_toolbar_icon_size = 32;
    data->paper_toolbar_open = TRUE;
    data->paper_toolbar_vertical = FALSE;
    g_free (data->paper_toolbar_items);
    data->paper_toolbar_items = g_strdup (PAPER_TOOLBAR_DEFAULT_ITEMS);
    g_free (data->paper_toolbar_order);
    data->paper_toolbar_order = g_strdup (PAPER_TOOLBAR_DEFAULT_ITEMS);
    {
      guint i;

      for (i = 0; i < SCREENATOR_SHORTCUT_COUNT; i++)
	{
	  g_free (data->shortcuts[i].text);
	  data->shortcuts[i].text =
	    g_strdup (screenator_shortcut_defaults[i]);
	}
    }
    data->paper_pdf_page = 0;
    data->paper_hand_tool = FALSE;
    data->math_expression = g_strdup ("cos(2*pi*x)");
    data->math_xmin = -2.0;
    data->math_xmax = 2.0;
    data->math_polar = FALSE;
    data->math_polar_expression = g_strdup ("cos(3*theta)");
    data->math_theta_min = 0.0;
    data->math_theta_max = 2.0 * G_PI;
    data->hide_hints = FALSE;
    data->show_settings_indicator = TRUE;
    data->show_floating_icon = TRUE;
    data->save_annotations_on_exit = TRUE;

    /*
      read actual settings
    */
    GError *error = NULL;
    GKeyFile *key_file = g_key_file_new ();

    if (!g_key_file_load_from_file (key_file, filename, KEYFILE_FLAGS, &error)) {
	// treat file-not-found not as an error
	if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
	    g_warning ("Error loading key file: %s", error->message);
	g_error_free(error);
	goto cleanup;
    }

    if (g_key_file_has_key (key_file, "Drawing", "Opacity", NULL))
	{
	    gdouble opacity =
		g_key_file_get_double (key_file, "Drawing", "Opacity", NULL);

	    // 0.0 is treated as "not set" so strokes never become invisible
	    if (opacity > 0.0 && opacity <= 1.0)
		data->opacity = opacity;
	}

    if (g_key_file_has_key (key_file, "Drawing", "MarkerOpacity", NULL))
	{
	    gdouble opacity =
		g_key_file_get_double (key_file, "Drawing", "MarkerOpacity", NULL);

	    if (opacity >= 0.10 && opacity <= 0.90)
		data->marker_opacity = opacity;
	}

    if (g_key_file_has_key (key_file, "Drawing", "CustomLineColor", NULL))
	{
	    gchar *value = g_key_file_get_string (key_file, "Drawing",
						  "CustomLineColor", NULL);
	    GdkRGBA parsed;

	    if (value && gdk_rgba_parse (&parsed, value))
		{
		    parsed.alpha = 1.0;
		    data->custom_line_color = parsed;
		}
	    g_free (value);
	}

    if (g_key_file_has_key (key_file, "Drawing", "CustomFillColor", NULL))
	{
	    gchar *value = g_key_file_get_string (key_file, "Drawing",
						  "CustomFillColor", NULL);
	    GdkRGBA parsed;

	    if (value && gdk_rgba_parse (&parsed, value))
		{
		    parsed.alpha = 1.0;
		    data->custom_fill_color = parsed;
		}
	    g_free (value);
	}

    if (g_key_file_has_key (key_file, "Drawing", "PenWidth", NULL))
	{
	    gint width =
		g_key_file_get_integer (key_file, "Drawing", "PenWidth", NULL);

	    if (width > 0)
		data->pen_width = width;
	}

    if (g_key_file_has_key (key_file, "Drawing", "EraserWidth", NULL))
	{
	    gint width =
		g_key_file_get_integer (key_file, "Drawing", "EraserWidth", NULL);

	    if (width >= 4 && width <= 128)
		data->eraser_width = width;
	}

    read_pen_color (key_file, &data->pen_color);

    if (g_key_file_has_key (key_file, "Drawing", "IconSize", NULL))
	{
	    gint size =
		g_key_file_get_integer (key_file, "Drawing", "IconSize", NULL);

	    if (size > 0)
		data->icon_size = size;
	}

    if (g_key_file_has_key (key_file, "Drawing", "IconPosition", NULL))
	{
	    gint position =
		g_key_file_get_integer (key_file, "Drawing", "IconPosition", NULL);

	    if (position >= SCREENATOR_ICON_TOP_RIGHT &&
		position <= SCREENATOR_ICON_BOTTOM_LEFT)
		data->icon_position = (ScreenatorIconPosition) position;
	}

    if (g_key_file_has_key (key_file, "Drawing", "Font", NULL))
	{
	    gchar *font =
		g_key_file_get_string (key_file, "Drawing", "Font", NULL);

	    if (font && *font)
		{
		    g_free (data->font);
		    data->font = font;
		}
	    else
		g_free (font);
	}

    if (g_key_file_has_key (key_file, "Drawing", "ShapeFill", NULL))
	{
	    gchar *fill =
		g_key_file_get_string (key_file, "Drawing", "ShapeFill", NULL);

	    if (fill && g_ascii_strcasecmp (fill, "none") != 0)
		{
		    GdkRGBA parsed;

		    if (gdk_rgba_parse (&parsed, fill))
			{
			    parsed.alpha = 1.0;
			    data->shape_fill_color = parsed;
			    data->shape_fill_enabled = TRUE;
			}
		}
	    g_free (fill);
	}

    if (g_key_file_has_key (key_file, "Drawing", "Paper", NULL))
	{
	    gchar *paper =
		g_key_file_get_string (key_file, "Drawing", "Paper", NULL);

	    if (paper)
		{
		    if (g_ascii_strcasecmp (paper, "plain") == 0)
		      {
			data->paper = SCREENATOR_PAPER_PLAIN;
			data->paper_selected = SCREENATOR_PAPER_PLAIN;
		      }
		    else if (g_ascii_strcasecmp (paper, "ruled") == 0)
		      {
			data->paper = SCREENATOR_PAPER_RULED;
			data->paper_selected = SCREENATOR_PAPER_RULED;
		      }
		    else if (g_ascii_strcasecmp (paper, "graph") == 0)
		      {
			data->paper = SCREENATOR_PAPER_GRAPH;
			data->paper_selected = SCREENATOR_PAPER_GRAPH;
		      }
		    else if (g_ascii_strcasecmp (paper, "pdf") == 0)
			data->paper_selected = SCREENATOR_PAPER_PDF;
		    else
			data->paper_selected = SCREENATOR_PAPER_NONE;
		    g_free (paper);
		}
	}

    if (g_key_file_has_key (key_file, "Drawing", "PaperPDFPath", NULL))
	{
	    gchar *path =
		g_key_file_get_string (key_file, "Drawing", "PaperPDFPath",
				       NULL);

	    if (path && *path)
	      {
		g_free (data->paper_pdf_path);
		data->paper_pdf_path = path;
	      }
	    else
		g_free (path);
	}

    if (g_key_file_has_key (key_file, "Drawing", "PaperPDFPage", NULL))
	data->paper_pdf_page =
	    g_key_file_get_integer (key_file, "Drawing", "PaperPDFPage",
				    NULL);
    if (data->paper_pdf_page < 0)
	data->paper_pdf_page = 0;

    if (!data->pdf_page_memory)
      data->pdf_page_memory = g_hash_table_new_full (g_str_hash, g_str_equal,
						     g_free, NULL);
    else
      g_hash_table_remove_all (data->pdf_page_memory);

    if (g_key_file_has_group (key_file, "PDFPages"))
      {
	gint count = g_key_file_get_integer (key_file, "PDFPages", "Count",
					     NULL);
	gint i;

	for (i = 0; i < count; i++)
	  {
	    gchar *key = g_strdup_printf ("Path%d", i);
	    gchar *path = g_key_file_get_string (key_file, "PDFPages", key,
						 NULL);
	    gchar *page_key = g_strdup_printf ("Page%d", i);
	    gint page = g_key_file_get_integer (key_file, "PDFPages", page_key,
						NULL);

	    if (path && *path)
	      {
		gchar *canonical = g_canonicalize_filename (path, NULL);

		if (canonical)
		  g_hash_table_insert (data->pdf_page_memory, canonical,
				       GINT_TO_POINTER (MAX (0, page) + 1));
	      }

	    g_free (key);
	    g_free (page_key);
	    g_free (path);
	  }
      }

    if (g_key_file_has_key (key_file, "Drawing", "PaperContinuousScroll",
			     NULL))
	data->paper_continuous_scroll =
	    g_key_file_get_boolean (key_file, "Drawing",
				    "PaperContinuousScroll", NULL);

    if (g_key_file_has_key (key_file, "Drawing", "PaperSize", NULL))
	{
	    gchar *size =
		g_key_file_get_string (key_file, "Drawing", "PaperSize", NULL);

	    if (size)
		{
		    if (g_ascii_strcasecmp (size, "a4-portrait") == 0)
			data->paper_size = SCREENATOR_PAPER_SIZE_A4_PORTRAIT;
		    else if (g_ascii_strcasecmp (size, "a4-landscape") == 0)
			data->paper_size = SCREENATOR_PAPER_SIZE_A4_LANDSCAPE;
		    else if (g_ascii_strcasecmp (size, "custom") == 0)
			data->paper_size = SCREENATOR_PAPER_SIZE_CUSTOM;
		    else
			data->paper_size = SCREENATOR_PAPER_SIZE_SCREEN;
		    g_free (size);
		}
	}

    if (g_key_file_has_key (key_file, "Drawing", "PaperCustomWidth", NULL))
	data->paper_custom_width_mm =
	    g_key_file_get_double (key_file, "Drawing", "PaperCustomWidth",
				   NULL);
    if (g_key_file_has_key (key_file, "Drawing", "PaperCustomHeight", NULL))
	data->paper_custom_height_mm =
	    g_key_file_get_double (key_file, "Drawing", "PaperCustomHeight",
				   NULL);

    data->paper_custom_width_mm =
	CLAMP (data->paper_custom_width_mm, 10.0, 1000.0);
    data->paper_custom_height_mm =
	CLAMP (data->paper_custom_height_mm, 10.0, 1000.0);

    if (g_key_file_has_key (key_file, "Drawing", "TimerDelay", NULL))
	data->tool_delay_seconds =
	    g_key_file_get_integer (key_file, "Drawing", "TimerDelay", NULL);
    data->tool_delay_seconds = CLAMP (data->tool_delay_seconds, 0, 60);

    if (g_key_file_has_key (key_file, "Drawing", "PaperToolbarIconSize",
			    NULL))
	data->paper_toolbar_icon_size =
	    g_key_file_get_integer (key_file, "Drawing",
				    "PaperToolbarIconSize", NULL);
    data->paper_toolbar_icon_size =
	CLAMP (data->paper_toolbar_icon_size, 16, 64);

    if (g_key_file_has_key (key_file, "Drawing", "PaperToolbarOpen", NULL))
	data->paper_toolbar_open =
	    g_key_file_get_boolean (key_file, "Drawing", "PaperToolbarOpen",
				    NULL);

    if (g_key_file_has_key (key_file, "Drawing", "PaperToolbarVertical",
			    NULL))
	data->paper_toolbar_vertical =
	    g_key_file_get_boolean (key_file, "Drawing",
				    "PaperToolbarVertical", NULL);

    if (g_key_file_has_key (key_file, "Drawing", "PaperToolbarItems", NULL))
	{
	  gchar *items = g_key_file_get_string (key_file, "Drawing",
						"PaperToolbarItems", NULL);
	  gint items_version = g_key_file_get_integer (
	    key_file, "Drawing", "PaperToolbarItemsVersion", NULL);

	  g_free (data->paper_toolbar_items);
	  data->paper_toolbar_items = toolbar_items_migrate (items, items_version);
	  g_free (items);

	  if (g_key_file_has_key (key_file, "Drawing", "PaperToolbarOrder",
				  NULL))
	    {
	      gchar *order = g_key_file_get_string (
		key_file, "Drawing", "PaperToolbarOrder", NULL);

	      g_free (data->paper_toolbar_order);
	      data->paper_toolbar_order = order ? order : g_strdup ("");
	    }
	  else
	    {
	      /* Let the settings page derive a full order from the visible
	       * items and the default order; hidden items keep a sensible
	       * position instead of being forced to the end. */
	      g_free (data->paper_toolbar_order);
	      data->paper_toolbar_order = NULL;
	    }
	}

    if (g_key_file_has_key (key_file, "Drawing", "MathExpression", NULL))
	{
	    gchar *expression =
		g_key_file_get_string (key_file, "Drawing", "MathExpression",
				       NULL);

	    if (expression && *expression)
		{
		    g_free (data->math_expression);
		    data->math_expression = expression;
		}
	    else
		g_free (expression);
	}

    if (g_key_file_has_key (key_file, "Drawing", "MathXMin", NULL))
	data->math_xmin =
	    g_key_file_get_double (key_file, "Drawing", "MathXMin", NULL);

    if (g_key_file_has_key (key_file, "Drawing", "MathXMax", NULL))
	data->math_xmax =
	    g_key_file_get_double (key_file, "Drawing", "MathXMax", NULL);

    /* A saved range that is empty or reversed would draw nothing. */
    if (!(data->math_xmax > data->math_xmin))
	{
	    data->math_xmin = -2.0;
	    data->math_xmax = 2.0;
	}

    if (g_key_file_has_key (key_file, "Drawing", "MathPolar", NULL))
	data->math_polar =
	    g_key_file_get_boolean (key_file, "Drawing", "MathPolar", NULL);

    if (g_key_file_has_key (key_file, "Drawing", "MathPolarExpression",
			    NULL))
	{
	    gchar *expression =
		g_key_file_get_string (key_file, "Drawing",
				       "MathPolarExpression", NULL);

	    if (expression && *expression)
		{
		    g_free (data->math_polar_expression);
		    data->math_polar_expression = expression;
		}
	    else
		g_free (expression);
	}

    if (g_key_file_has_key (key_file, "Drawing", "MathThetaMin", NULL))
	data->math_theta_min =
	    g_key_file_get_double (key_file, "Drawing", "MathThetaMin", NULL);

    if (g_key_file_has_key (key_file, "Drawing", "MathThetaMax", NULL))
	data->math_theta_max =
	    g_key_file_get_double (key_file, "Drawing", "MathThetaMax", NULL);

    /* A saved theta range that is empty or reversed would draw nothing. */
    if (!(data->math_theta_max > data->math_theta_min))
	{
	    data->math_theta_min = 0.0;
	    data->math_theta_max = 2.0 * G_PI;
	}

    if (g_key_file_has_key (key_file, "Drawing", "HideHints", NULL))
	data->hide_hints =
	    g_key_file_get_boolean (key_file, "Drawing", "HideHints", NULL);
    else if (g_key_file_has_key (key_file, "Drawing", "HideStylusHint", NULL))
	/* Older builds only had the stylus hint.  Treat that choice as the
	 * single hints dialog choice. */
	data->hide_hints =
	    g_key_file_get_boolean (key_file, "Drawing", "HideStylusHint",
				    NULL);

    if (g_key_file_has_key (key_file, "Drawing", "ShowSettingsIndicator", NULL))
	data->show_settings_indicator =
	    g_key_file_get_boolean (key_file, "Drawing",
				    "ShowSettingsIndicator", NULL);

    if (g_key_file_has_key (key_file, "Drawing", "ShowFloatingIcon", NULL))
	data->show_floating_icon =
	    g_key_file_get_boolean (key_file, "Drawing", "ShowFloatingIcon",
				    NULL);
    else if (g_key_file_has_key (key_file, "Drawing", "ShowFloatingMenu",
				 NULL))
	/* Older builds saved the setting under this name. */
	data->show_floating_icon =
	    g_key_file_get_boolean (key_file, "Drawing", "ShowFloatingMenu",
				    NULL);

    if (g_key_file_has_key (key_file, "Drawing", "SaveAnnotationsOnExit",
			    NULL))
	data->save_annotations_on_exit =
	    g_key_file_get_boolean (key_file, "Drawing",
				    "SaveAnnotationsOnExit", NULL);

    {
      static const gchar * const shortcut_keys[SCREENATOR_SHORTCUT_COUNT] =
	{
	  "FloatingIcon", "Pen", "Text", "Arrow", "Timer", "Toolbar",
	  "SwitchMode", "Screenshot", "Undo", "Redo", "Save",
	  "InsertBefore", "InsertAfter", "ToggleVisibility", "ExitPaper",
	  "Quit", "Escape"
	};
      guint i;

      for (i = 0; i < SCREENATOR_SHORTCUT_COUNT; i++)
	{
	  if (g_key_file_has_key (key_file, "Shortcuts", shortcut_keys[i],
				  NULL))
	    {
	      gchar *text = g_key_file_get_string (key_file, "Shortcuts",
						   shortcut_keys[i], NULL);

	      g_free (data->shortcuts[i].text);
	      data->shortcuts[i].text = text ? text : g_strdup ("");
	    }
	}
    }

    data->pen_tool = read_pen_tool (key_file);
    screenator_resolve_shortcuts (data);

 cleanup:
    g_free(filename);
    g_key_file_free(key_file);
}


static gchar *
pen_color_to_string (const GdkRGBA *color)
{
    return g_strdup_printf ("#%02X%02X%02X",
			    (guint) (CLAMP (color->red, 0.0, 1.0) * 255.0 + 0.5),
			    (guint) (CLAMP (color->green, 0.0, 1.0) * 255.0 + 0.5),
			    (guint) (CLAMP (color->blue, 0.0, 1.0) * 255.0 + 0.5));
}


void write_keyfile(ScreenatorData *data)
{
    static const gchar * const tool_names[] = { "none", "pen", "arrow", "eraser",
					       "text", "rectangle", "ellipse",
					       "rounded-rectangle", "angled-line",
					       "free-line", "angled-arrow", "marker",
					       "spline", "bucket", "polygon", "triangle",
					       "math", "bars" };
    gchar *filename = screenator_config_path ("screenator.ini");

    GError *error = NULL;
    GKeyFile *key_file = g_key_file_new ();
    gchar *color = pen_color_to_string (&data->pen_color);
    gchar *fill = data->shape_fill_enabled
	? pen_color_to_string (&data->shape_fill_color) : g_strdup ("none");
    gchar *custom_line = pen_color_to_string (&data->custom_line_color);
    gchar *custom_fill = pen_color_to_string (&data->custom_fill_color);

    g_key_file_set_double (key_file, "Drawing", "Opacity", data->opacity);
    g_key_file_set_double (key_file, "Drawing", "MarkerOpacity",
			   data->marker_opacity);
    g_key_file_set_integer (key_file, "Drawing", "PenWidth", data->pen_width);
    g_key_file_set_integer (key_file, "Drawing", "EraserWidth",
			    data->eraser_width);
    g_key_file_set_string (key_file, "Drawing", "PenColor", color);
    g_key_file_set_integer (key_file, "Drawing", "IconSize", data->icon_size);
    g_key_file_set_integer (key_file, "Drawing", "IconPosition",
			    (gint) data->icon_position);
    g_key_file_set_string (key_file, "Drawing", "Font",
			   (data->font && *data->font) ? data->font
						       : KEY_DFLT_FONT);
    g_key_file_set_string (key_file, "Drawing", "ShapeFill", fill);
    g_key_file_set_string (key_file, "Drawing", "CustomLineColor", custom_line);
    g_key_file_set_string (key_file, "Drawing", "CustomFillColor", custom_fill);
    /* Remember the selected paper type even while Paper Drawing is off, so
     * F3 turns it back on with the same type after a restart. */
    g_key_file_set_string (key_file, "Drawing", "Paper",
			   data->paper_selected == SCREENATOR_PAPER_PLAIN ? "plain" :
			   data->paper_selected == SCREENATOR_PAPER_RULED ? "ruled" :
			   data->paper_selected == SCREENATOR_PAPER_GRAPH ? "graph" :
			   data->paper_selected == SCREENATOR_PAPER_PDF ? "pdf" :
			   "none");
    {
      ScreenatorPaperDocument *pdf =
	data->paper_docs[SCREENATOR_PAPER_PDF];
      const gchar *pdf_path =
	(pdf && pdf->pdf_path) ? pdf->pdf_path : data->paper_pdf_path;
      gint pdf_page = (pdf && pdf->pdf_loaded) ? (gint) pdf->page
					      : data->paper_pdf_page;

      g_key_file_set_string (key_file, "Drawing", "PaperPDFPath",
			     (pdf_path && *pdf_path) ? pdf_path : "");
      g_key_file_set_integer (key_file, "Drawing", "PaperPDFPage",
			      MAX (0, pdf_page));

      if (!data->pdf_page_memory)
	data->pdf_page_memory = g_hash_table_new_full (g_str_hash, g_str_equal,
						       g_free, NULL);
      if (pdf && pdf->pdf_loaded && pdf_path && *pdf_path)
	{
	  gchar *canonical = g_canonicalize_filename (pdf_path, NULL);

	  if (canonical)
	    g_hash_table_insert (data->pdf_page_memory, canonical,
				 GINT_TO_POINTER (MAX (0, pdf_page) + 1));
	}

      g_key_file_remove_group (key_file, "PDFPages", NULL);
      {
	GHashTableIter it;
	gpointer key, value;
	gint index = 0;

	g_key_file_set_integer (key_file, "PDFPages", "Count",
				g_hash_table_size (data->pdf_page_memory));
	g_hash_table_iter_init (&it, data->pdf_page_memory);
	while (g_hash_table_iter_next (&it, &key, &value))
	  {
	    gchar *path_key = g_strdup_printf ("Path%d", index);
	    gchar *page_key = g_strdup_printf ("Page%d", index);

	    g_key_file_set_string (key_file, "PDFPages", path_key,
				   (const gchar *) key);
	    g_key_file_set_integer (key_file, "PDFPages", page_key,
				    GPOINTER_TO_INT (value) - 1);
	    g_free (path_key);
	    g_free (page_key);
	    index++;
	  }
      }
    }
    g_key_file_set_boolean (key_file, "Drawing", "PaperContinuousScroll",
			    data->paper_continuous_scroll);
    g_key_file_set_string (key_file, "Drawing", "PaperSize",
			   data->paper_size == SCREENATOR_PAPER_SIZE_A4_PORTRAIT
			   ? "a4-portrait" :
			   data->paper_size == SCREENATOR_PAPER_SIZE_A4_LANDSCAPE
			   ? "a4-landscape" :
			   data->paper_size == SCREENATOR_PAPER_SIZE_CUSTOM
			   ? "custom" : "screen");
    g_key_file_set_double (key_file, "Drawing", "PaperCustomWidth",
			   data->paper_custom_width_mm);
    g_key_file_set_double (key_file, "Drawing", "PaperCustomHeight",
			   data->paper_custom_height_mm);
    g_key_file_set_integer (key_file, "Drawing", "PaperToolbarIconSize",
			    data->paper_toolbar_icon_size);
    g_key_file_set_integer (key_file, "Drawing", "TimerDelay",
			    data->tool_delay_seconds);
    g_key_file_set_boolean (key_file, "Drawing", "PaperToolbarOpen",
			    data->paper_toolbar_open);
    g_key_file_set_boolean (key_file, "Drawing", "PaperToolbarVertical",
			    data->paper_toolbar_vertical);
    g_key_file_set_string (key_file, "Drawing", "PaperToolbarItems",
			   data->paper_toolbar_items
			   ? data->paper_toolbar_items
			   : PAPER_TOOLBAR_DEFAULT_ITEMS);
    g_key_file_set_string (key_file, "Drawing", "PaperToolbarOrder",
			   data->paper_toolbar_order
			   ? data->paper_toolbar_order
			   : (data->paper_toolbar_items
			      ? data->paper_toolbar_items
			      : PAPER_TOOLBAR_DEFAULT_ITEMS));
    g_key_file_set_integer (key_file, "Drawing", "PaperToolbarItemsVersion",
			    PAPER_TOOLBAR_ITEMS_VERSION);
    {
      static const gchar * const shortcut_keys[SCREENATOR_SHORTCUT_COUNT] =
	{
	  "FloatingIcon", "Pen", "Text", "Arrow", "Timer", "Toolbar",
	  "SwitchMode", "Screenshot", "Undo", "Redo", "Save",
	  "InsertBefore", "InsertAfter", "ToggleVisibility", "ExitPaper",
	  "Quit", "Escape"
	};
      guint i;

      for (i = 0; i < SCREENATOR_SHORTCUT_COUNT; i++)
	g_key_file_set_string (key_file, "Shortcuts", shortcut_keys[i],
			       data->shortcuts[i].text
			       ? data->shortcuts[i].text
			       : screenator_shortcut_defaults[i]);
    }
    g_key_file_set_string (key_file, "Drawing", "Tool",
			   (data->pen_tool >= 0 &&
			    data->pen_tool < (gint) G_N_ELEMENTS (tool_names))
			   ? tool_names[data->pen_tool] : "none");
    g_key_file_set_string (key_file, "Drawing", "MathExpression",
			   data->math_expression ? data->math_expression : "");
    g_key_file_set_double (key_file, "Drawing", "MathXMin", data->math_xmin);
    g_key_file_set_double (key_file, "Drawing", "MathXMax", data->math_xmax);
    g_key_file_set_boolean (key_file, "Drawing", "MathPolar",
			    data->math_polar);
    g_key_file_set_string (key_file, "Drawing", "MathPolarExpression",
			   data->math_polar_expression
			   ? data->math_polar_expression : "");
    g_key_file_set_double (key_file, "Drawing", "MathThetaMin",
			   data->math_theta_min);
    g_key_file_set_double (key_file, "Drawing", "MathThetaMax",
			   data->math_theta_max);
    g_key_file_set_boolean (key_file, "Drawing", "HideHints",
			    data->hide_hints);
    g_key_file_set_boolean (key_file, "Drawing", "ShowSettingsIndicator",
			    data->show_settings_indicator);
    g_key_file_set_boolean (key_file, "Drawing", "ShowFloatingIcon",
			    data->show_floating_icon);
    g_key_file_set_boolean (key_file, "Drawing", "SaveAnnotationsOnExit",
			    data->save_annotations_on_exit);
    g_free (color);
    g_free (fill);
    g_free (custom_line);
    g_free (custom_fill);

    // Save as a file.
    if (!g_key_file_save_to_file (key_file, filename, &error)) {
	g_warning ("Error saving key file: %s", error->message);
	g_error_free(error);
	goto cleanup;
    }

 cleanup:
    g_free(filename);
    g_key_file_free(key_file);
}
