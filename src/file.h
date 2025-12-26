// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "alsa.h"

// load/save configuration in native format (used by file dialogs and presets)
int load_native(struct alsa_card *card, const char *path);
int save_native(struct alsa_card *card, const char *path);

// load/save configuration (supports both alsactl .state and native .conf)
void activate_load(GSimpleAction *action, GVariant *parameter, gpointer data);
void activate_save(GSimpleAction *action, GVariant *parameter, gpointer data);

// simulation from .state file
void activate_sim(GSimpleAction *action, GVariant *parameter, gpointer data);
