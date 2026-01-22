// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "alsa.h"

enum {
  WIDGET_GAIN_TAPER_LINEAR,
  WIDGET_GAIN_TAPER_LOG,
  WIDGET_GAIN_TAPER_GEN4_VOLUME
};

GtkWidget *make_gain_alsa_elem(
  struct alsa_elem *elem,
  int               zero_is_off,
  int               taper_type,
  int               can_control,
  gboolean          show_level
);

// Get the dial widget from a gain widget container (for level updates)
GtkWidget *get_gain_dial(GtkWidget *gain_widget);

// Clean up a gain widget (remove callbacks) before destroying it
// Must be called before unreffing/destroying the widget
void cleanup_gain_widget(GtkWidget *gain_widget);

// Maximum number of elements that can be controlled by a stereo gain widget
#define MAX_STEREO_GAIN_ELEMS 4

// Create a stereo gain widget controlling multiple elements in sync
// All elements must have compatible value ranges
GtkWidget *make_stereo_gain_alsa_elem(
  struct alsa_elem **elems,
  int               elem_count,
  int               zero_is_off,
  int               taper_type,
  int               can_control,
  gboolean          show_level
);
