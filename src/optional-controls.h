// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alsa.h"

// Types of optional controls
typedef enum {
  OPTIONAL_CONTROL_NAME,
} OptionalControlType;

// Definition of an optional control
struct optional_control_def {
  const char          *alsa_name;     // ALSA element name (e.g., "Name")
  const char          *config_key;    // Config file key (e.g., "name")
  int                  alsa_type;     // SND_CTL_ELEM_TYPE_*
  size_t               max_size;      // For BYTES: max string length
  const char          *default_value; // Default string value
  OptionalControlType  type;          // Type of optional control
};

// Table of optional control definitions
extern const struct optional_control_def optional_controls[];

// Initialise optional controls for a card
// - Checks which controls exist
// - Creates simulated elements for missing controls
// - Loads values from state file
// - Registers save callbacks
void optional_controls_init(struct alsa_card *card);

// Get the Name element for a card (may be simulated)
// Returns NULL if not available
struct alsa_elem *optional_controls_get_name_elem(struct alsa_card *card);

// Free optional control callback data (for use as GDestroyNotify)
void optional_controls_free_callback_data(void *data);
