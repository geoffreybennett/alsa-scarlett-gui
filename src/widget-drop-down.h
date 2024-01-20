// SPDX-FileCopyrightText: 2023-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "alsa.h"

GtkWidget *make_drop_down_alsa_elem(
  struct alsa_elem *elem,
  const char       *label_text
);
