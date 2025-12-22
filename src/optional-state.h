// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alsa.h"

// Configuration file sections
#define CONFIG_SECTION_DEVICE   "device"
#define CONFIG_SECTION_CONTROLS "controls"
#define CONFIG_SECTION_UI       "ui"

// Load the optional control state for a device from a specific section
// Returns hash table of key → value (as string)
// Caller must free the hash table with g_hash_table_destroy()
// Returns NULL if card has no serial or file doesn't exist
GHashTable *optional_state_load(struct alsa_card *card, const char *section);

// Save a single value to the state file in a specific section
// Creates the state file and directory if needed
// Returns 0 on success, -1 on error
int optional_state_save(
  struct alsa_card *card,
  const char       *section,
  const char       *key,
  const char       *value
);

// Remove the state file for a given serial
// Returns 0 on success, -1 on error
int optional_state_remove(const char *serial);
