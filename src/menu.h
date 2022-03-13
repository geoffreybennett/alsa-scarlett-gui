// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "alsa.h"

GMenu *create_app_menu(GtkApplication *app);
void add_window_action_map(GtkWindow *w);
void add_load_save_action_map(struct alsa_card *card);
void add_startup_action_map(struct alsa_card *card);
void add_mixer_action_map(struct alsa_card *card);
