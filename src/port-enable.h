// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alsa.h"

// Initialise port enable/disable elements for all routing sources and sinks
// - Creates simulated ALSA boolean elements for port enable state
// - Loads values from state file (defaults to enabled)
// - Registers save callbacks
// Must be called after custom_names_init()
void port_enable_init(struct alsa_card *card);

// Check if a routing source is enabled
// Returns 1 if enabled, 0 if disabled
int is_routing_src_enabled(struct routing_src *src);

// Check if a routing sink is enabled
// Returns 1 if enabled, 0 if disabled
int is_routing_snk_enabled(struct routing_snk *snk);

// Get the enable element for a routing source
// Returns NULL if not available
struct alsa_elem *get_src_enable_elem(struct routing_src *src);

// Get the enable element for a routing sink
// Returns NULL if not available
struct alsa_elem *get_snk_enable_elem(struct routing_snk *snk);

// Check if all sources of a given category are disabled
// Returns 1 if all disabled, 0 if at least one is enabled
int all_sources_disabled(struct alsa_card *card, int port_category);

// Check if all sinks of a given category are disabled
// Returns 1 if all disabled, 0 if at least one is enabled
int all_sinks_disabled(struct alsa_card *card, int port_category);

// Update visibility of routing section grids based on port enable states
void update_routing_section_visibility(struct alsa_card *card);

// Schedule a UI update for a card (runs at idle)
// Use PENDING_UI_UPDATE_* flags from alsa.h
void schedule_ui_update(struct alsa_card *card, int flags);

// Free port enable callback data (for use as GDestroyNotify)
void port_enable_free_callback_data(void *data);
