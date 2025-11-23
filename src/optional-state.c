// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <glib.h>

#include "optional-state.h"

// Get the config directory path
// Returns newly allocated string that must be freed with g_free()
static char *get_config_dir(void) {
  const char *config_home = g_get_user_config_dir();
  return g_build_filename(config_home, "alsa-scarlett-gui", NULL);
}

// Get the state file path for a given serial number
char *optional_state_get_path(const char *serial) {
  char *config_dir = get_config_dir();
  char *filename = g_strdup_printf("%s.conf", serial);
  char *path = g_build_filename(config_dir, filename, NULL);

  g_free(config_dir);
  g_free(filename);

  return path;
}

// Create the config directory if it doesn't exist
// Returns 0 on success, -1 on error
static int ensure_config_dir(void) {
  char *config_dir = get_config_dir();
  int ret = 0;

  if (g_mkdir_with_parents(config_dir, 0755) < 0) {
    g_warning("Failed to create config directory: %s", config_dir);
    ret = -1;
  }

  g_free(config_dir);
  return ret;
}

// Load optional controls from state file using GKeyFile
GHashTable *optional_state_load(const char *serial) {
  if (!serial || !*serial)
    return NULL;

  char *path = optional_state_get_path(serial);
  GKeyFile *key_file = g_key_file_new();
  GError *error = NULL;

  // try to load the file
  if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error)) {
    // file doesn't exist or can't be read - not an error
    g_key_file_free(key_file);
    g_free(path);
    if (error)
      g_error_free(error);
    return NULL;
  }

  g_free(path);

  // create hash table for controls
  GHashTable *controls = g_hash_table_new_full(
    g_str_hash, g_str_equal, g_free, g_free
  );

  // get all keys from the global group
  gsize num_keys;
  gchar **keys = g_key_file_get_keys(
    key_file,
    "global",
    &num_keys,
    &error
  );

  if (!keys) {
    if (error)
      g_error_free(error);
    g_key_file_free(key_file);
    return controls;
  }

  // read each key-value pair (skip 'serial' key)
  for (gsize i = 0; i < num_keys; i++) {
    // skip the serial key as it's metadata, not a control
    if (g_strcmp0(keys[i], "serial") == 0)
      continue;

    gchar *value = g_key_file_get_string(
      key_file,
      "global",
      keys[i],
      NULL
    );

    if (value) {
      g_hash_table_insert(controls, g_strdup(keys[i]), value);
    }
  }

  g_strfreev(keys);
  g_key_file_free(key_file);

  return controls;
}

// Save optional control state to file using GKeyFile
int optional_state_save(
  const char *serial,
  const char *control_name,
  const char *value
) {
  if (!serial || !*serial || !control_name || !*control_name)
    return -1;

  if (ensure_config_dir() < 0)
    return -1;

  char *path = optional_state_get_path(serial);
  GKeyFile *key_file = g_key_file_new();
  GError *error = NULL;

  // load existing file if it exists
  g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL);

  // set the device serial in the global group
  g_key_file_set_string(key_file, "global", "serial", serial);

  // set the control value in the global group
  g_key_file_set_string(
    key_file,
    "global",
    control_name,
    value ? value : ""
  );

  // save to file
  if (!g_key_file_save_to_file(key_file, path, &error)) {
    g_warning("Failed to save state file %s: %s", path, error->message);
    g_error_free(error);
    g_key_file_free(key_file);
    g_free(path);
    return -1;
  }

  g_key_file_free(key_file);
  g_free(path);

  return 0;
}
