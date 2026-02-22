// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alsa.h"

#define DEFAULT_LEVELS_INTERVAL_MS 50

// Convert levels update rate in Hz to interval in ms.
// Returns DEFAULT_LEVELS_INTERVAL_MS for unrecognised values.
int levels_hz_to_ms(int hz);

// Load per-card preferences from optional state into card struct
void load_preferences(struct alsa_card *card);

GtkWidget *create_preferences_controls(struct alsa_card *card);
