// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alsa.h"

void add_drop_controller_motion(
  struct alsa_card *card,
  GtkWidget        *routing_overlay
);
