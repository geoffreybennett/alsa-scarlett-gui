// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <glib.h>

// Load the optional control state for a device by serial number
// Returns hash table of control_name → value (as string)
// Caller must free the hash table with g_hash_table_destroy()
// Returns NULL if file doesn't exist or can't be loaded
GHashTable *optional_state_load(const char *serial);

// Save a single optional control value to the state file
// Creates the state file and directory if needed
// Returns 0 on success, -1 on error
int optional_state_save(
  const char *serial,
  const char *control_name,
  const char *value
);

// Get the state file path for a given serial number
// Returns newly allocated string that must be freed with g_free()
char *optional_state_get_path(const char *serial);
