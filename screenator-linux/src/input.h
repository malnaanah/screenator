

/*
 * Screenator -- a vibe-coded screen annotation tool based on Gromit-MPX 1.4.2
 * Screenator Copyright (C) 2026 Mahmoud Alnaanah <malnaanah@gmail.com>
 */

#ifndef INPUT_H
#define INPUT_H

#include "screenator.h"

void setup_input_devices (ScreenatorData *data);
void shutdown_input_devices (ScreenatorData *data);
void release_grab (ScreenatorData *data, GdkDevice *dev);
void acquire_grab (ScreenatorData *data, GdkDevice *dev);
void toggle_grab  (ScreenatorData *data, GdkDevice *dev);
gboolean screenator_any_grabbed (ScreenatorData *data);
void screenator_suspend_shortcuts (ScreenatorData *data, gboolean suspend);
gboolean screenator_is_wayland_session (void);
gboolean screenator_raise_companion_above_paper (ScreenatorData *data,
						 guint slot);
void screenator_clear_companion_transient (ScreenatorData *data, guint slot);
gboolean screenator_launch_companion (ScreenatorData *data, guint slot);
gint snoop_key_press (GtkWidget *grab_widget, GdkEventKey *event, gpointer func_data);

#endif
