// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "alsa.h"

gboolean window_startup_close_request(GtkWindow *w, gpointer data);

GtkWidget *create_subwindow(
  struct alsa_card *card,
  const char       *name,
  GCallback         close_callback
);
