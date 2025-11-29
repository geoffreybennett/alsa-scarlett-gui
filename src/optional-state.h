// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alsa.h"

// Load the optional control state for a device
// Returns hash table of control_name → value (as string)
// Caller must free the hash table with g_hash_table_destroy()
// Returns NULL if card has no serial or file doesn't exist
GHashTable *optional_state_load(struct alsa_card *card);

// Save a single optional control value to the state file
// Creates the state file and directory if needed
// Returns 0 on success, -1 on error
int optional_state_save(
  struct alsa_card *card,
  const char       *key,
  const char       *value
);
