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

// Get formatted default name for a routing source (ignoring custom name)
// If abbreviated is true, DSP/Mix use short form ("1", "A") for routing window
// Returns newly allocated string that must be freed
char *get_src_default_name_formatted(struct routing_src *src, int abbreviated);

// Get the formatted name to display for a routing source
// Mixer/DSP always show abbreviated form, others show display_name
// Returns newly allocated string that must be freed
char *get_src_display_name_formatted(struct routing_src *src);

// Get generic hardware name for a routing source (no device-specific names)
// Returns e.g. "Analogue 1", "PCM 1", "Mix A"
// Returns newly allocated string that must be freed
char *get_src_generic_name(struct routing_src *src);

// Get formatted default name for a routing sink (ignoring custom name)
// If abbreviated is true, Mix/DSP use short form ("1") for routing window
// Returns newly allocated string that must be freed
char *get_snk_default_name_formatted(struct routing_snk *snk, int abbreviated);

// Get generic hardware name for a routing sink (no device-specific names)
// Returns e.g. "Analogue 1", "PCM 1", "Mixer 1"
// Returns newly allocated string that must be freed
char *get_snk_generic_name(struct routing_snk *snk);

// Get formatted display name for a routing sink (for UI display)
// Mix/DSP sinks use abbreviated form ("1"), others use display_name
// Returns newly allocated string that must be freed
char *get_snk_display_name_formatted(struct routing_snk *snk);

// Get mixer output label for mixer window (returns "Mix A" for defaults)
// Returns newly allocated string that must be freed
char *get_mixer_output_label_for_mixer_window(struct routing_src *src);

// Free custom name callback data for simulated elements
// Called from card_destroy_callback
void custom_names_free_callback_data(void *data);
