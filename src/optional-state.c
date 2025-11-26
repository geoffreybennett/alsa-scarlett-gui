// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <glib.h>

#include "optional-state.h"

// Debounce delay in milliseconds
#define SAVE_DEBOUNCE_MS 100

// Pending saves: serial -> GHashTable(key -> value)
static GHashTable *pending_saves = NULL;
static guint save_timeout_id = 0;

// Get the config directory path
// Returns newly allocated string that must be freed with g_free()
static char *get_config_dir(void) {
  const char *config_home = g_get_user_config_dir();
  return g_build_filename(config_home, "alsa-scarlett-gui", NULL);
}

// Get the state file path for a given serial number
static char *get_state_path(const char *serial) {
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

  char *path = get_state_path(serial);
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

// Flush all pending saves to disk
static gboolean flush_pending_saves(gpointer user_data) {
  save_timeout_id = 0;

  if (!pending_saves)
    return G_SOURCE_REMOVE;

  if (ensure_config_dir() < 0)
    return G_SOURCE_REMOVE;

  // iterate over each serial with pending saves
  GHashTableIter serial_iter;
  gpointer serial_key, serial_value;

  g_hash_table_iter_init(&serial_iter, pending_saves);
  while (g_hash_table_iter_next(&serial_iter, &serial_key, &serial_value)) {
    const char *serial = serial_key;
    GHashTable *changes = serial_value;

    char *path = get_state_path(serial);
    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;

    // load existing file if it exists
    g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL);

    // set the device serial in the global group
    g_key_file_set_string(key_file, "global", "serial", serial);

    // apply all pending changes for this serial
    GHashTableIter change_iter;
    gpointer change_key, change_value;

    g_hash_table_iter_init(&change_iter, changes);
    while (g_hash_table_iter_next(&change_iter, &change_key, &change_value)) {
      const char *control_name = change_key;
      const char *value = change_value;

      g_key_file_set_string(
        key_file,
        "global",
        control_name,
        value ? value : ""
      );
    }

    // save to file
    if (!g_key_file_save_to_file(key_file, path, &error)) {
      g_warning("Failed to save state file %s: %s", path, error->message);
      g_error_free(error);
    }

    g_key_file_free(key_file);
    g_free(path);
  }

  // clear all pending saves
  g_hash_table_remove_all(pending_saves);

  return G_SOURCE_REMOVE;
}

// Save optional control state to file using GKeyFile (debounced)
int optional_state_save(
  const char *serial,
  const char *control_name,
  const char *value
) {
  if (!serial || !*serial || !control_name || !*control_name)
    return -1;

  // initialise pending_saves hash table if needed
  if (!pending_saves) {
    pending_saves = g_hash_table_new_full(
      g_str_hash, g_str_equal,
      g_free,
      (GDestroyNotify)g_hash_table_destroy
    );
  }

  // get or create the hash table for this serial
  GHashTable *serial_changes = g_hash_table_lookup(pending_saves, serial);
  if (!serial_changes) {
    serial_changes = g_hash_table_new_full(
      g_str_hash, g_str_equal, g_free, g_free
    );
    g_hash_table_insert(pending_saves, g_strdup(serial), serial_changes);
  }

  // add/update the pending change
  g_hash_table_insert(
    serial_changes,
    g_strdup(control_name),
    g_strdup(value ? value : "")
  );

  // cancel existing timeout if any
  if (save_timeout_id) {
    g_source_remove(save_timeout_id);
  }

  // schedule new timeout
  save_timeout_id = g_timeout_add(
    SAVE_DEBOUNCE_MS,
    flush_pending_saves,
    NULL
  );

  return 0;
}
