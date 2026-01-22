// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "alsa.h"

void add_io_tab(GtkWidget *top_notebook, struct alsa_card *card);
void update_config_io_mixer_labels(struct alsa_card *card);
