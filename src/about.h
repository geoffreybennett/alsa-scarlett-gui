// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

void activate_about(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
);
