// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alsa.h"

// Initialise custom name elements for all routing sources and sinks
// - Creates simulated ALSA elements for custom names
// - Loads values from state file
// - Registers save callbacks
// Must be called after alsa_get_routing_controls()
void custom_names_init(struct alsa_card *card);

// Get display name for a routing source
// Returns custom name if set, otherwise returns the default name
const char *get_routing_src_display_name(struct routing_src *src);

// Get display name for a routing sink
// Returns custom name if set, otherwise returns the element name
const char *get_routing_snk_display_name(struct routing_snk *snk);

// Get mixer output label for mixer window (returns "Mix A" for defaults)
// Returns newly allocated string that must be freed
char *get_mixer_output_label_for_mixer_window(struct routing_src *src);

// Free custom name callback data for simulated elements
// Called from card_destroy_callback
void custom_names_free_callback_data(void *data);
