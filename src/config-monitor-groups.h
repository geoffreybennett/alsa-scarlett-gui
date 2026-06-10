// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "alsa.h"

void add_monitor_groups_tab(GtkWidget *notebook, struct alsa_card *card);

// Rebuild the monitor groups grid (e.g., when stereo link state changes)
void rebuild_monitor_groups_grid(struct alsa_card *card);
