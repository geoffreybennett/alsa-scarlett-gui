// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "alsa.h"

enum {
  WIDGET_GAIN_TAPER_LINEAR,
  WIDGET_GAIN_TAPER_LOG
};

GtkWidget *make_gain_alsa_elem(
  struct alsa_elem *elem,
  int               zero_is_off,
  int               taper_type
);
