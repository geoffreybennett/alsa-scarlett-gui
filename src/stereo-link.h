// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alsa.h"

// Maximum length for stereo pair names
#define MAX_PAIR_NAME_LEN 32

// Initialise stereo link elements for all routing sources and sinks
// - Creates simulated ALSA boolean elements for link state
// - Creates simulated ALSA BYTES elements for pair names
// - Loads values from state file
// - Registers save callbacks
// Must be called after port_enable_init()
void stereo_link_init(struct alsa_card *card);

// Check if a routing source is linked to its partner (forms stereo pair)
// Returns 1 if linked, 0 if not linked or no valid partner
int is_src_linked(struct routing_src *src);

// Check if a routing sink is linked to its partner (forms stereo pair)
// Returns 1 if linked, 0 if not linked or no valid partner
int is_snk_linked(struct routing_snk *snk);

// Get the partner source for a routing source (adjacent L/R pair)
// Returns NULL if no valid partner exists
struct routing_src *get_src_partner(struct routing_src *src);

// Get the partner sink for a routing sink (adjacent L/R pair)
// Returns NULL if no valid partner exists
struct routing_snk *get_snk_partner(struct routing_snk *snk);

// Check if a routing source should be displayed (not hidden as R of linked pair)
// Returns 1 if should display, 0 if hidden
int should_display_src(struct routing_src *src);

// Check if a routing sink should be displayed (not hidden as R of linked pair)
// Returns 1 if should display, 0 if hidden
int should_display_snk(struct routing_snk *snk);

// Get the link element for a routing source pair
// Returns NULL if not available (element stored on left channel)
struct alsa_elem *get_src_link_elem(struct routing_src *src);

// Get the link element for a routing sink pair
// Returns NULL if not available (element stored on left channel)
struct alsa_elem *get_snk_link_elem(struct routing_snk *snk);

// Get the pair name element for a routing source pair
// Returns NULL if not available (element stored on left channel)
struct alsa_elem *get_src_pair_name_elem(struct routing_src *src);

// Get the pair name element for a routing sink pair
// Returns NULL if not available (element stored on left channel)
struct alsa_elem *get_snk_pair_name_elem(struct routing_snk *snk);

// Check if source has a valid partner for stereo linking
// A valid pair is two adjacent channels of the same type (odd/even)
int src_has_valid_partner(struct routing_src *src);

// Check if sink has a valid partner for stereo linking
// A valid pair is two adjacent channels of the same type (odd/even)
int snk_has_valid_partner(struct routing_snk *snk);

// Get the stereo pair display name for a linked source pair
// Returns the pair name if set, otherwise "L Name / R Name" format
// Returns newly allocated string that must be freed
char *get_src_pair_display_name(struct routing_src *src);

// Get the stereo pair display name for a linked sink pair
// Returns the pair name if set, otherwise "L Name / R Name" format
// Returns newly allocated string that must be freed
char *get_snk_pair_display_name(struct routing_snk *snk);

// Schedule debounced UI updates for stereo link changes
void schedule_stereo_link_ui_update(struct alsa_card *card);

// Check if source is the left channel of a potential pair (odd lr_num)
int is_src_left_channel(struct routing_src *src);

// Check if sink is the left channel of a potential pair (odd lr_num)
int is_snk_left_channel(struct routing_snk *snk);

// Get the generic/constructed pair name for a source (e.g., "PCM 1–2")
// Used for fixed labels in I/O config window
// Returns newly allocated string that must be freed
char *get_src_generic_pair_name(struct routing_src *src);

// Get the generic/constructed pair name for a sink (e.g., "PCM 1–2")
// Used for fixed labels in I/O config window
// Returns newly allocated string that must be freed
char *get_snk_generic_pair_name(struct routing_snk *snk);

// Get default pair name for a source (device-specific or constructed)
// Used for display in routing window when no custom name is set
// Returns newly allocated string that must be freed
char *get_src_default_pair_name(struct routing_src *src);

// Get default pair name for a sink (device-specific or constructed)
// Used for display in routing window when no custom name is set
// Returns newly allocated string that must be freed
char *get_snk_default_pair_name(struct routing_snk *snk);
