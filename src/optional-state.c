// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <glib.h>
#include <glib/gstdio.h>

#include "optional-state.h"

// Debounce delay in milliseconds
#define SAVE_DEBOUNCE_MS 100

// Pending save entry with section and key
struct pending_entry {
  char *section;
  char *key;
  char *value;
};

// Pending saves: serial -> GArray of pending_entry
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

// Free a pending entry
static void free_pending_entry(struct pending_entry *entry) {
  g_free(entry->section);
  g_free(entry->key);
  g_free(entry->value);
  g_free(entry);
}

// Load optional controls from state file using GKeyFile
GHashTable *optional_state_load(struct alsa_card *card, const char *section) {
  if (!card || !card->serial || !*card->serial || !section)
    return NULL;

  char *path = get_state_path(card->serial);
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

  // create hash table for values
  GHashTable *values = g_hash_table_new_full(
    g_str_hash, g_str_equal, g_free, g_free
  );

  // get all keys from the specified section
  gsize num_keys;
  gchar **keys = g_key_file_get_keys(key_file, section, &num_keys, &error);

  if (!keys) {
    if (error)
      g_error_free(error);
    g_key_file_free(key_file);
    return values;
  }

  // read each key-value pair
  for (gsize i = 0; i < num_keys; i++) {
    gchar *value = g_key_file_get_string(key_file, section, keys[i], NULL);

    if (value)
      g_hash_table_insert(values, g_strdup(keys[i]), value);
  }

  g_strfreev(keys);
  g_key_file_free(key_file);

  return values;
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
    GArray *entries = serial_value;

    char *path = get_state_path(serial);
    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;

    // load existing file if it exists
    g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL);

    // apply all pending changes for this serial
    for (guint i = 0; i < entries->len; i++) {
      struct pending_entry *entry = g_array_index(entries, struct pending_entry *, i);

      g_key_file_set_string(
        key_file,
        entry->section,
        entry->key,
        entry->value ? entry->value : ""
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

// Free array of pending entries
static void free_pending_entries(GArray *entries) {
  for (guint i = 0; i < entries->len; i++) {
    struct pending_entry *entry = g_array_index(entries, struct pending_entry *, i);
    free_pending_entry(entry);
  }
  g_array_free(entries, TRUE);
}

// Remove the state file for a given serial
int optional_state_remove(const char *serial) {
  if (!serial || !*serial)
    return -1;

  char *path = get_state_path(serial);
  int ret = 0;

  if (g_file_test(path, G_FILE_TEST_EXISTS)) {
    if (g_unlink(path) < 0) {
      g_warning("Failed to remove state file: %s", path);
      ret = -1;
    }
  }

  g_free(path);
  return ret;
}

// Save optional control state to file using GKeyFile (debounced)
int optional_state_save(
  struct alsa_card *card,
  const char       *section,
  const char       *key,
  const char       *value
) {
  if (!card || !card->serial || !*card->serial || !section || !key || !*key)
    return -1;

  const char *serial = card->serial;

  // initialise pending_saves hash table if needed
  if (!pending_saves) {
    pending_saves = g_hash_table_new_full(
      g_str_hash, g_str_equal,
      g_free,
      (GDestroyNotify)free_pending_entries
    );
  }

  // get or create the array for this serial
  GArray *entries = g_hash_table_lookup(pending_saves, serial);
  if (!entries) {
    entries = g_array_new(FALSE, FALSE, sizeof(struct pending_entry *));
    g_hash_table_insert(pending_saves, g_strdup(serial), entries);
  }

  // create new pending entry
  struct pending_entry *entry = g_malloc(sizeof(struct pending_entry));
  entry->section = g_strdup(section);
  entry->key = g_strdup(key);
  entry->value = g_strdup(value ? value : "");

  g_array_append_val(entries, entry);

  // also ensure [device] section has serial and model
  // (add these first time we save anything for this serial)
  static GHashTable *device_section_written = NULL;
  if (!device_section_written) {
    device_section_written = g_hash_table_new_full(
      g_str_hash, g_str_equal, g_free, NULL
    );
  }

  if (!g_hash_table_contains(device_section_written, serial)) {
    g_hash_table_add(device_section_written, g_strdup(serial));

    struct pending_entry *serial_entry = g_malloc(sizeof(struct pending_entry));
    serial_entry->section = g_strdup(CONFIG_SECTION_DEVICE);
    serial_entry->key = g_strdup("serial");
    serial_entry->value = g_strdup(serial);
    g_array_append_val(entries, serial_entry);

    if (card->name) {
      struct pending_entry *model_entry = g_malloc(sizeof(struct pending_entry));
      model_entry->section = g_strdup(CONFIG_SECTION_DEVICE);
      model_entry->key = g_strdup("model");
      model_entry->value = g_strdup(card->name);
      g_array_append_val(entries, model_entry);
    }
  }

  // cancel existing timeout if any
  if (save_timeout_id)
    g_source_remove(save_timeout_id);

  // schedule new timeout
  save_timeout_id = g_timeout_add(
    SAVE_DEBOUNCE_MS,
    flush_pending_saves,
    NULL
  );

  return 0;
}
