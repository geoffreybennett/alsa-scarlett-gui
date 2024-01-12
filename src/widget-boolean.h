// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "alsa.h"

GtkWidget *make_boolean_alsa_elem(
  struct alsa_elem *alsa_elem,
  const char       *disabled_text,
  const char       *enabled_text
);
