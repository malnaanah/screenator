

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
gint snoop_key_press (GtkWidget *grab_widget, GdkEventKey *event, gpointer func_data);

#endif
