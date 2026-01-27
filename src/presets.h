// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "alsa.h"

// Save initial configuration on first-ever load of a real interface
void save_initial_config(struct alsa_card *card);

// Create the Presets button with popover menu for the main window
GtkWidget *create_presets_button(struct alsa_card *card);
