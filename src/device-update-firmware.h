// SPDX-FileCopyrightText: 2024-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>
#include "alsa.h"

void create_update_firmware_window(GtkWidget *w, struct alsa_card *card);
