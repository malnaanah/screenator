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

#include "config.h"
#include "screenator.h"
#include "build-config.h"


#define KEYFILE_FLAGS G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS

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
  filename = g_strjoin (G_DIR_SEPARATOR_S,
                        g_get_user_config_dir(), "screenator.cfg", NULL);
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
          else if (key_type == UNDOKEY_SYMBOL_VALUE)
            {
              data->undo_keyval = g_strdup(scanner->value.v_string);
            }

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


/* Names of the per-tool hint flags, in ScreenatorToolHint order. */
static const gchar * const hint_names[] =
{
  "pen", "marker", "eraser", "text", "line", "arrow", "spline", "polygon",
  "triangle", "ellipse", "rounded-rectangle", "math", "bars", "bucket"
};

void read_keyfile(ScreenatorData *data)
{
    gchar *filename = g_strjoin (G_DIR_SEPARATOR_S,
				 g_get_user_config_dir(), "screenator.ini", NULL);

    /*
      set defaults
    */
    data->opacity = DEFAULT_OPACITY;
    data->pen_width = KEY_DFLT_PEN_WIDTH;
    data->eraser_width = KEY_DFLT_ERASER_WIDTH;
    gdk_rgba_parse (&data->pen_color, KEY_DFLT_PEN_COLOR);
    data->pen_tool = 0;
    data->icon_size = KEY_DFLT_ICON_SIZE;
    data->font = g_strdup (KEY_DFLT_FONT);
    data->shape_fill_enabled = FALSE;
    gdk_rgba_parse (&data->shape_fill_color, KEY_DFLT_SHAPE_FILL_COLOR);
    data->shape_fill_color.alpha = 1.0;
    data->paper = SCREENATOR_PAPER_NONE;
    data->math_expression = g_strdup ("cos(2*pi*x)");
    data->math_xmin = -2.0;
    data->math_xmax = 2.0;
    data->math_polar = FALSE;
    data->math_polar_expression = g_strdup ("cos(3*theta)");
    data->math_theta_min = 0.0;
    data->math_theta_max = 2.0 * G_PI;
    memset (data->hide_tool_hints, 0, sizeof (data->hide_tool_hints));
    data->hide_spline_hint = FALSE;
    data->hide_snap_hint = FALSE;
    data->hide_text_hint = FALSE;
    data->show_settings_indicator = TRUE;
    data->show_floating_icon = TRUE;

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
			data->paper = SCREENATOR_PAPER_PLAIN;
		    else if (g_ascii_strcasecmp (paper, "ruled") == 0)
			data->paper = SCREENATOR_PAPER_RULED;
		    else if (g_ascii_strcasecmp (paper, "graph") == 0)
			data->paper = SCREENATOR_PAPER_GRAPH;
		    g_free (paper);
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

    if (g_key_file_has_key (key_file, "Drawing", "HiddenToolHints", NULL))
	{
	    gchar *value = g_key_file_get_string (key_file, "Drawing",
						  "HiddenToolHints", NULL);

	    if (value)
		{
		    gchar **names = g_strsplit (value, ",", -1);
		    guint i;

		    for (i = 0; names[i]; i++)
			{
			    gchar *name = g_strstrip (names[i]);
			    guint j;

			    for (j = 0; j < G_N_ELEMENTS (hint_names); j++)
				if (g_ascii_strcasecmp (name, hint_names[j]) == 0)
				    data->hide_tool_hints[j] = TRUE;
			}

		    g_strfreev (names);
		    g_free (value);
		}
	}
    else if (g_key_file_has_key (key_file, "Drawing", "HideToolHint", NULL) &&
	     g_key_file_get_boolean (key_file, "Drawing", "HideToolHint", NULL))
	{
	    /* An older build hid all the tool hints with one flag. */
	    guint j;

	    for (j = 0; j < G_N_ELEMENTS (hint_names); j++)
		data->hide_tool_hints[j] = TRUE;
	}

    if (g_key_file_has_key (key_file, "Drawing", "HideSplineHint", NULL))
	data->hide_spline_hint =
	    g_key_file_get_boolean (key_file, "Drawing", "HideSplineHint", NULL);

    if (g_key_file_has_key (key_file, "Drawing", "HideSnapHint", NULL))
	data->hide_snap_hint =
	    g_key_file_get_boolean (key_file, "Drawing", "HideSnapHint", NULL);

    if (g_key_file_has_key (key_file, "Drawing", "HideTextHint", NULL))
	data->hide_text_hint =
	    g_key_file_get_boolean (key_file, "Drawing", "HideTextHint", NULL);

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

    data->pen_tool = read_pen_tool (key_file);

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
    gchar *filename = g_strjoin (G_DIR_SEPARATOR_S,
				 g_get_user_config_dir(), "screenator.ini", NULL);

    GError *error = NULL;
    GKeyFile *key_file = g_key_file_new ();
    gchar *color = pen_color_to_string (&data->pen_color);
    gchar *fill = data->shape_fill_enabled
	? pen_color_to_string (&data->shape_fill_color) : g_strdup ("none");

    g_key_file_set_double (key_file, "Drawing", "Opacity", data->opacity);
    g_key_file_set_integer (key_file, "Drawing", "PenWidth", data->pen_width);
    g_key_file_set_integer (key_file, "Drawing", "EraserWidth",
			    data->eraser_width);
    g_key_file_set_string (key_file, "Drawing", "PenColor", color);
    g_key_file_set_integer (key_file, "Drawing", "IconSize", data->icon_size);
    g_key_file_set_string (key_file, "Drawing", "Font",
			   (data->font && *data->font) ? data->font
						       : KEY_DFLT_FONT);
    g_key_file_set_string (key_file, "Drawing", "ShapeFill", fill);
    g_key_file_set_string (key_file, "Drawing", "Paper",
			   data->paper == SCREENATOR_PAPER_PLAIN ? "plain" :
			   data->paper == SCREENATOR_PAPER_RULED ? "ruled" :
			   data->paper == SCREENATOR_PAPER_GRAPH ? "graph" :
			   "none");
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
    {
	GString *hidden = g_string_new (NULL);
	guint i;

	for (i = 0; i < G_N_ELEMENTS (hint_names); i++)
	    if (data->hide_tool_hints[i])
		{
		    if (hidden->len > 0)
			g_string_append_c (hidden, ',');
		    g_string_append (hidden, hint_names[i]);
		}

	g_key_file_set_string (key_file, "Drawing", "HiddenToolHints",
			       hidden->str);
	g_string_free (hidden, TRUE);
    }
    g_key_file_set_boolean (key_file, "Drawing", "HideSplineHint",
			    data->hide_spline_hint);
    g_key_file_set_boolean (key_file, "Drawing", "HideSnapHint",
			    data->hide_snap_hint);
    g_key_file_set_boolean (key_file, "Drawing", "HideTextHint",
			    data->hide_text_hint);
    g_key_file_set_boolean (key_file, "Drawing", "ShowSettingsIndicator",
			    data->show_settings_indicator);
    g_key_file_set_boolean (key_file, "Drawing", "ShowFloatingIcon",
			    data->show_floating_icon);
    g_free (color);
    g_free (fill);

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
