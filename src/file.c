// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "alsa.h"
#include "alsa-sim.h"
#include "error.h"
#include "file.h"
#include "optional-state.h"
#include "stringhelper.h"

static void run_alsactl(
  struct alsa_card *card,
  char             *cmd,
  char             *fn
) {
  GtkWindow *w = GTK_WINDOW(card->window_main);

  gchar *alsactl_path = g_find_program_in_path("alsactl");

  if (!alsactl_path)
    alsactl_path = g_strdup("/usr/sbin/alsactl");

  gchar *argv[] = {
    alsactl_path, cmd, card->device, "-I", "-f", fn, NULL
  };

  gchar  *stdout;
  gchar  *stderr;
  gint    exit_status;
  GError *error = NULL;

  gboolean result = g_spawn_sync(
    NULL,
    argv,
    NULL,
    G_SPAWN_SEARCH_PATH,
    NULL,
    NULL,
    &stdout,
    &stderr,
    &exit_status,
    &error
  );

  if (result && WIFEXITED(exit_status) && WEXITSTATUS(exit_status) == 0)
    goto done;

  char *error_message =
    result
      ? g_strdup_printf("%s\n%s", stdout, stderr)
      : g_strdup_printf("%s", error->message);

  char *msg = g_strdup_printf(
    "Error running “alsactl %s %s -f %s”: %s",
    cmd, card->device, fn, error_message
  );
  show_error(w, msg);
  g_free(msg);
  g_free(error_message);

done:
  g_free(alsactl_path);
  g_free(stdout);
  g_free(stderr);
  if (error)
    g_error_free(error);
}

// Convert element value to string for saving
static char *elem_value_to_string(struct alsa_elem *elem) {
  int type = elem->type;

  if (type == SND_CTL_ELEM_TYPE_BOOLEAN) {
    return g_strdup(alsa_get_elem_value(elem) ? "true" : "false");
  } else if (type == SND_CTL_ELEM_TYPE_ENUMERATED) {
    long value = alsa_get_elem_value(elem);
    char *item_name = alsa_get_item_name(elem, value);
    return g_strdup(item_name ? item_name : "");
  } else if (type == SND_CTL_ELEM_TYPE_INTEGER) {
    int count = elem->count;

    // single value
    if (count <= 1)
      return g_strdup_printf("%ld", alsa_get_elem_value(elem));

    // multi-valued: get all values and format as comma-separated
    long *values = alsa_get_elem_int_values(elem);
    GString *str = g_string_new(NULL);

    for (int i = 0; i < count; i++) {
      if (i > 0)
        g_string_append_c(str, ',');
      g_string_append_printf(str, "%ld", values[i]);
    }

    free(values);
    return g_string_free(str, FALSE);
  } else if (type == SND_CTL_ELEM_TYPE_BYTES) {
    // bytes type used for custom names - treat as string
    size_t size;
    const void *data = alsa_get_elem_bytes(elem, &size);
    if (!data || size == 0)
      return g_strdup("");

    // find actual string length (may be null-terminated before size)
    size_t len = strnlen(data, size);
    return g_strndup(data, len);
  }

  return NULL;
}

// Check if element should be saved (skip volatile/read-only elements)
static int should_save_elem(struct alsa_elem *elem) {
  // skip volatile elements like level meters
  if (alsa_get_elem_volatile(elem))
    return 0;

  // skip non-writable elements (read-only status values)
  if (!alsa_get_elem_writable(elem))
    return 0;

  return 1;
}

// Save card configuration to native format
int save_native(struct alsa_card *card, const char *path) {
  GKeyFile *key_file = g_key_file_new();
  GError *error = NULL;

  // add device section
  if (card->serial && *card->serial)
    g_key_file_set_string(
      key_file, CONFIG_SECTION_DEVICE, "serial", card->serial
    );
  if (card->name)
    g_key_file_set_string(
      key_file, CONFIG_SECTION_DEVICE, "model", card->name
    );

  // add controls section
  for (guint i = 0; i < card->elems->len; i++) {
    struct alsa_elem *elem = g_ptr_array_index(card->elems, i);

    if (!should_save_elem(elem))
      continue;

    char *value_str = elem_value_to_string(elem);
    if (!value_str)
      continue;

    g_key_file_set_string(
      key_file,
      CONFIG_SECTION_CONTROLS,
      elem->name,
      value_str
    );

    g_free(value_str);
  }

  // save to file
  int ret = 0;
  if (!g_key_file_save_to_file(key_file, path, &error)) {
    ret = -1;
    if (error)
      g_error_free(error);
  }

  g_key_file_free(key_file);
  return ret;
}

// Convert string value back to element value (single value)
static int string_to_elem_value(
  struct alsa_elem *elem,
  const char       *str,
  long             *value
) {
  int type = elem->type;

  if (type == SND_CTL_ELEM_TYPE_BOOLEAN) {
    if (strcmp(str, "true") == 0 || strcmp(str, "1") == 0)
      *value = 1;
    else
      *value = 0;
    return 0;
  } else if (type == SND_CTL_ELEM_TYPE_ENUMERATED) {
    // find the enum item by name
    int count = alsa_get_item_count(elem);
    for (int i = 0; i < count; i++) {
      char *item_name = alsa_get_item_name(elem, i);
      if (item_name && strcmp(item_name, str) == 0) {
        *value = i;
        return 0;
      }
    }
    // not found - try parsing as integer
    char *end;
    long v = strtol(str, &end, 10);
    if (end != str && *end == '\0') {
      *value = v;
      return 0;
    }
    return -1;
  } else if (type == SND_CTL_ELEM_TYPE_INTEGER) {
    char *end;
    long v = strtol(str, &end, 10);
    if (end != str && (*end == '\0' || *end == ',')) {
      *value = v;
      return 0;
    }
    return -1;
  }

  return -1;
}

// Set element value from string, handling multi-valued elements
static void set_elem_from_string(struct alsa_elem *elem, const char *str) {
  int type = elem->type;

  // for multi-valued integers, parse comma-separated values
  if (type == SND_CTL_ELEM_TYPE_INTEGER && elem->count > 1) {
    long *values = calloc(elem->count, sizeof(long));
    const char *p = str;
    int i = 0;

    while (*p && i < elem->count) {
      char *end;
      values[i] = strtol(p, &end, 10);
      if (end == p)
        break;
      i++;
      p = end;
      if (*p == ',')
        p++;
    }

    if (i > 0) {
      alsa_set_elem_int_values(elem, values, i);
      alsa_elem_change(elem);
    }

    free(values);
    return;
  }

  // bytes type for custom names
  if (type == SND_CTL_ELEM_TYPE_BYTES) {
    size_t len = strlen(str);
    alsa_set_elem_bytes(elem, str, len);
    // alsa_set_elem_bytes already schedules callback for simulated elements
    return;
  }

  // single value
  long value;
  if (string_to_elem_value(elem, str, &value) == 0) {
    alsa_set_elem_value(elem, value);
    // explicitly trigger callback to update UI immediately
    alsa_elem_change(elem);
  }
}

// Load card configuration from native format
int load_native(struct alsa_card *card, const char *path) {
  GKeyFile *key_file = g_key_file_new();
  GError *error = NULL;

  if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error)) {
    g_key_file_free(key_file);
    if (error)
      g_error_free(error);
    return -1;
  }

  // get all keys from controls section
  gsize num_keys;
  gchar **keys = g_key_file_get_keys(
    key_file, CONFIG_SECTION_CONTROLS, &num_keys, NULL
  );

  if (!keys) {
    g_key_file_free(key_file);
    return 0;
  }

  // two passes: some elements may be read-only until other controls
  // (like enable switches) are set first
  for (int pass = 0; pass < 2; pass++) {

    // for each key, find the matching element and set its value
    for (gsize i = 0; i < num_keys; i++) {
      gchar *value = g_key_file_get_string(
        key_file, CONFIG_SECTION_CONTROLS, keys[i], NULL
      );

      if (!value)
        continue;

      // find element by name
      struct alsa_elem *elem = get_elem_by_name(card->elems, keys[i]);
      if (!elem) {
        g_free(value);
        continue;
      }

      // skip non-writable elements (may become writable on second pass)
      if (!alsa_get_elem_writable(elem)) {
        g_free(value);
        continue;
      }

      // convert string to value and set
      set_elem_from_string(elem, value);

      g_free(value);
    }
  }

  g_strfreev(keys);
  g_key_file_free(key_file);

  return 0;
}

// Create filter list for file dialogs
static GListModel *create_file_filters(void) {
  GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);

  GtkFileFilter *conf_filter = gtk_file_filter_new();
  gtk_file_filter_set_name(conf_filter, "alsa-scarlett-gui config (.conf)");
  gtk_file_filter_add_pattern(conf_filter, "*.conf");
  g_list_store_append(filters, conf_filter);
  g_object_unref(conf_filter);

  GtkFileFilter *state_filter = gtk_file_filter_new();
  gtk_file_filter_set_name(state_filter, "alsactl state file (.state)");
  gtk_file_filter_add_pattern(state_filter, "*.state");
  g_list_store_append(filters, state_filter);
  g_object_unref(state_filter);

  return G_LIST_MODEL(filters);
}

// Create filter list for simulation file dialog (state files only)
static GListModel *create_state_filter(void) {
  GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);

  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "alsactl state file (.state)");
  gtk_file_filter_add_pattern(filter, "*.state");
  g_list_store_append(filters, filter);
  g_object_unref(filter);

  return G_LIST_MODEL(filters);
}

// Callback for load file dialog completion
static void load_dialog_complete(
  GObject      *source,
  GAsyncResult *result,
  gpointer      data
) {
  struct alsa_card *card = data;
  GtkWindow *w = GTK_WINDOW(card->window_main);
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  GError *error = NULL;

  GFile *file = gtk_file_dialog_open_finish(dialog, result, &error);
  if (!file) {
    if (error && !g_error_matches(error, GTK_DIALOG_ERROR,
                                  GTK_DIALOG_ERROR_DISMISSED))
      show_error(w, error->message);
    g_clear_error(&error);
    return;
  }

  char *fn = g_file_get_path(file);

  // determine format from extension
  if (string_ends_with(fn, ".conf")) {
    if (load_native(card, fn) < 0) {
      char *msg = g_strdup_printf("Error loading from %s", fn);
      show_error(w, msg);
      g_free(msg);
    }
  } else {
    run_alsactl(card, "restore", fn);
  }

  g_free(fn);
  g_object_unref(file);
}

void activate_load(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  struct alsa_card *card = data;

  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Load Configuration");

  GListModel *filters = create_file_filters();
  gtk_file_dialog_set_filters(dialog, filters);
  g_object_unref(filters);

  gtk_file_dialog_open(
    dialog,
    GTK_WINDOW(card->window_main),
    NULL,
    load_dialog_complete,
    card
  );

  g_object_unref(dialog);
}

// Callback for save file dialog completion
static void save_dialog_complete(
  GObject      *source,
  GAsyncResult *result,
  gpointer      data
) {
  struct alsa_card *card = data;
  GtkWindow *w = GTK_WINDOW(card->window_main);
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  GError *error = NULL;

  GFile *file = gtk_file_dialog_save_finish(dialog, result, &error);
  if (!file) {
    if (error && !g_error_matches(error, GTK_DIALOG_ERROR,
                                  GTK_DIALOG_ERROR_DISMISSED))
      show_error(w, error->message);
    g_clear_error(&error);
    return;
  }

  char *fn = g_file_get_path(file);

  // determine format from extension
  int use_native = string_ends_with(fn, ".conf");
  int use_state = string_ends_with(fn, ".state");

  // if no recognized extension, default to .conf and append it
  char *fn_with_ext;
  if (!use_native && !use_state) {
    fn_with_ext = g_strdup_printf("%s.conf", fn);
    use_native = 1;
  } else {
    fn_with_ext = g_strdup(fn);
  }

  if (use_native) {
    if (save_native(card, fn_with_ext) < 0) {
      char *msg = g_strdup_printf("Error saving to %s", fn_with_ext);
      show_error(w, msg);
      g_free(msg);
    }
  } else {
    run_alsactl(card, "store", fn_with_ext);
  }

  g_free(fn);
  g_free(fn_with_ext);
  g_object_unref(file);
}

void activate_save(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  struct alsa_card *card = data;

  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Save Configuration");

  GListModel *filters = create_file_filters();
  gtk_file_dialog_set_filters(dialog, filters);
  g_object_unref(filters);

  gtk_file_dialog_save(
    dialog,
    GTK_WINDOW(card->window_main),
    NULL,
    save_dialog_complete,
    card
  );

  g_object_unref(dialog);
}

// Callback for simulation file dialog completion
static void sim_dialog_complete(
  GObject      *source,
  GAsyncResult *result,
  gpointer      data
) {
  GtkWindow *w = data;
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
  GError *error = NULL;

  GFile *file = gtk_file_dialog_open_finish(dialog, result, &error);
  if (!file) {
    if (error && !g_error_matches(error, GTK_DIALOG_ERROR,
                                  GTK_DIALOG_ERROR_DISMISSED))
      show_error(w, error->message);
    g_clear_error(&error);
    return;
  }

  char *fn = g_file_get_path(file);

  create_sim_from_file(w, fn);

  g_free(fn);
  g_object_unref(file);
}

void activate_sim(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  GtkWidget *w = data;

  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(
    dialog, "Load Configuration File for Interface Simulation"
  );

  GListModel *filters = create_state_filter();
  gtk_file_dialog_set_filters(dialog, filters);
  g_object_unref(filters);

  gtk_file_dialog_open(
    dialog,
    GTK_WINDOW(w),
    NULL,
    sim_dialog_complete,
    w
  );

  g_object_unref(dialog);
}
