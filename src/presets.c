// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gdk/gdk.h>
#include <glib/gstdio.h>
#include <graphene.h>

#include "error.h"
#include "file.h"
#include "presets.h"

// Data structure for the presets button and popover
struct presets_data {
  struct alsa_card *card;
  GtkWidget        *popover;
  GtkWidget        *box;
};

// Helper to attach preset name to a widget
static void set_preset_name(GtkWidget *widget, const char *name) {
  g_object_set_data_full(G_OBJECT(widget), "preset-name", g_strdup(name), g_free);
}

// Get the presets directory path
static char *get_presets_dir(void) {
  const char *config_home = g_get_user_config_dir();
  return g_build_filename(config_home, "alsa-scarlett-gui", NULL);
}

// Get the preset file path for a given serial and preset name
static char *get_preset_path(const char *serial, const char *name) {
  char *presets_dir = get_presets_dir();
  char *filename = g_strdup_printf("%s-%s.conf", serial, name);
  char *path = g_build_filename(presets_dir, filename, NULL);

  g_free(presets_dir);
  g_free(filename);

  return path;
}

// Create the presets directory if it doesn't exist
static int ensure_presets_dir(void) {
  char *presets_dir = get_presets_dir();
  int ret = 0;

  if (g_mkdir_with_parents(presets_dir, 0755) < 0)
    ret = -1;

  g_free(presets_dir);
  return ret;
}

// Scan for presets matching the card's serial number
// Returns a GList of preset names (newly allocated strings)
static GList *scan_presets(const char *serial) {
  GList *presets = NULL;
  char *presets_dir = get_presets_dir();
  GDir *dir = g_dir_open(presets_dir, 0, NULL);

  if (!dir) {
    g_free(presets_dir);
    return NULL;
  }

  // Build the prefix to match: "{serial}-"
  char *prefix = g_strdup_printf("%s-", serial);
  size_t prefix_len = strlen(prefix);

  const char *filename;
  while ((filename = g_dir_read_name(dir)) != NULL) {
    // Check if filename starts with prefix and ends with .conf
    if (g_str_has_prefix(filename, prefix) &&
        g_str_has_suffix(filename, ".conf")) {
      // Extract preset name from filename (remove ".conf" suffix)
      size_t len = strlen(filename);
      char *name = g_strndup(filename + prefix_len, len - prefix_len - strlen(".conf"));
      presets = g_list_insert_sorted(presets, name, (GCompareFunc)g_strcmp0);
    }
  }

  g_dir_close(dir);
  g_free(prefix);
  g_free(presets_dir);

  return presets;
}

// Load a preset
static void load_preset(struct alsa_card *card, const char *name) {
  char *path = get_preset_path(card->serial, name);

  if (load_native(card, path) < 0) {
    char *msg = g_strdup_printf("Error loading preset \"%s\"", name);
    show_error(GTK_WINDOW(card->window_main), msg);
    g_free(msg);
  }

  g_free(path);
}

// Save a preset
static int save_preset(struct alsa_card *card, const char *name) {
  if (ensure_presets_dir() < 0)
    return -1;

  char *path = get_preset_path(card->serial, name);
  int ret = save_native(card, path);

  if (ret < 0) {
    char *msg = g_strdup_printf("Error saving preset \"%s\"", name);
    show_error(GTK_WINDOW(card->window_main), msg);
    g_free(msg);
  }

  g_free(path);
  return ret;
}

// Delete a preset
static int delete_preset(const char *serial, const char *name) {
  char *path = get_preset_path(serial, name);
  int ret = g_unlink(path);
  g_free(path);
  return ret;
}

// Trim leading and trailing whitespace, return newly allocated string
static char *trim_string(const char *str) {
  if (!str || !*str)
    return g_strdup("");

  return g_strstrip(g_strdup(str));
}

// Validate preset name (no path separators, not empty after trim)
static int validate_preset_name(const char *name) {
  if (!name || !*name)
    return 0;

  // Check for invalid characters
  if (strchr(name, '/') || strchr(name, '\\'))
    return 0;

  return 1;
}

// Data for save dialog
struct save_dialog_data {
  struct presets_data *presets_data;
  GtkWidget           *window;
  GtkWidget           *entry;
};

// Free save dialog data
static void free_save_dialog_data(gpointer user_data) {
  g_free(user_data);
}

// Handle save button click or entry activation
static void do_save_preset(struct save_dialog_data *dialog_data) {
  char *name = trim_string(gtk_editable_get_text(GTK_EDITABLE(dialog_data->entry)));

  if (validate_preset_name(name)) {
    save_preset(dialog_data->presets_data->card, name);
    gtk_window_destroy(GTK_WINDOW(dialog_data->window));
  } else {
    show_error(
      GTK_WINDOW(dialog_data->presets_data->card->window_main),
      "Invalid preset name"
    );
  }

  g_free(name);
}

// Callback for save button
static void save_button_clicked(GtkButton *button, gpointer user_data) {
  do_save_preset(user_data);
}

// Callback for entry activation (Enter key)
static void entry_activated(GtkEntry *entry, gpointer user_data) {
  do_save_preset(user_data);
}

// Callback for cancel button
static void cancel_button_clicked(GtkButton *button, gpointer user_data) {
  struct save_dialog_data *dialog_data = user_data;
  gtk_window_destroy(GTK_WINDOW(dialog_data->window));
}

// Callback for Escape key
static gboolean key_pressed(
  GtkEventControllerKey *controller,
  guint                  keyval,
  guint                  keycode,
  GdkModifierType        state,
  gpointer               user_data
) {
  struct save_dialog_data *dialog_data = user_data;

  if (keyval == GDK_KEY_Escape) {
    gtk_window_destroy(GTK_WINDOW(dialog_data->window));
    return TRUE;
  }

  return FALSE;
}

// Show the "Save as Preset" dialog
static void show_save_preset_dialog(struct presets_data *data) {
  gtk_popover_popdown(GTK_POPOVER(data->popover));

  struct save_dialog_data *dialog_data = g_malloc0(sizeof(struct save_dialog_data));
  dialog_data->presets_data = data;

  // Create window
  dialog_data->window = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog_data->window), "Save as Preset");
  gtk_window_set_modal(GTK_WINDOW(dialog_data->window), TRUE);
  gtk_window_set_transient_for(
    GTK_WINDOW(dialog_data->window),
    GTK_WINDOW(data->card->window_main)
  );
  gtk_window_set_resizable(GTK_WINDOW(dialog_data->window), FALSE);
  gtk_widget_add_css_class(dialog_data->window, "window-frame");
  gtk_widget_add_css_class(dialog_data->window, "modal");

  // Clean up data when window is destroyed
  g_object_set_data_full(
    G_OBJECT(dialog_data->window), "dialog-data",
    dialog_data, free_save_dialog_data
  );

  // Add key controller for Escape
  GtkEventController *key_controller = gtk_event_controller_key_new();
  g_signal_connect(key_controller, "key-pressed", G_CALLBACK(key_pressed), dialog_data);
  gtk_widget_add_controller(dialog_data->window, key_controller);

  // Main box
  GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
  gtk_widget_add_css_class(main_box, "window-content");
  gtk_widget_add_css_class(main_box, "big-padding");
  gtk_window_set_child(GTK_WINDOW(dialog_data->window), main_box);

  // Label and entry
  GtkWidget *label = gtk_label_new("Preset name:");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(main_box), label);

  dialog_data->entry = gtk_entry_new();
  gtk_widget_set_size_request(dialog_data->entry, 250, -1);
  g_signal_connect(dialog_data->entry, "activate", G_CALLBACK(entry_activated), dialog_data);
  gtk_box_append(GTK_BOX(main_box), dialog_data->entry);

  // Separator
  GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_append(GTK_BOX(main_box), sep);

  // Button box
  GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
  gtk_widget_set_halign(button_box, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(main_box), button_box);

  GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
  g_signal_connect(cancel_btn, "clicked", G_CALLBACK(cancel_button_clicked), dialog_data);
  gtk_box_append(GTK_BOX(button_box), cancel_btn);

  GtkWidget *save_btn = gtk_button_new_with_label("Save");
  g_signal_connect(save_btn, "clicked", G_CALLBACK(save_button_clicked), dialog_data);
  gtk_box_append(GTK_BOX(button_box), save_btn);

  gtk_widget_set_visible(dialog_data->window, TRUE);
  gtk_widget_grab_focus(dialog_data->entry);
}

// Callback for "Save as Preset..." button
static void save_preset_clicked(GtkButton *button, gpointer user_data) {
  struct presets_data *data = user_data;
  show_save_preset_dialog(data);
}

// Nested buttons in GTK4: the inner button's clicked signal doesn't fire
// for mouse clicks - the outer button swallows them. We use a gesture in
// capture phase to detect click location, and a clicked signal for keyboard.

// Gesture callback for preset row - check if click was on delete button
static void preset_row_clicked(
  GtkGestureClick *gesture,
  int              n_press,
  double           x,
  double           y,
  gpointer         user_data
) {
  struct presets_data *data = user_data;
  GtkWidget *row = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  GtkWidget *delete_btn = g_object_get_data(G_OBJECT(row), "delete-btn");
  const char *name = g_object_get_data(G_OBJECT(row), "preset-name");

  if (!name)
    return;

  // Check if click was on the delete button (translate coordinates)
  gboolean on_delete = FALSE;
  if (delete_btn) {
    graphene_point_t src = GRAPHENE_POINT_INIT(x, y);
    graphene_point_t dest;
    if (gtk_widget_compute_point(row, delete_btn, &src, &dest))
      on_delete = gtk_widget_contains(delete_btn, dest.x, dest.y);
  }

  if (on_delete)
    delete_preset(data->card->serial, name);
  else
    load_preset(data->card, name);

  gtk_popover_popdown(GTK_POPOVER(data->popover));
}

// Clicked callback for row button (keyboard activation)
static void preset_row_activated(GtkButton *button, gpointer user_data) {
  struct presets_data *data = user_data;
  const char *name = g_object_get_data(G_OBJECT(button), "preset-name");

  if (name) {
    load_preset(data->card, name);
    gtk_popover_popdown(GTK_POPOVER(data->popover));
  }
}

// Clicked callback for delete button (keyboard activation)
static void delete_preset_clicked(GtkButton *button, gpointer user_data) {
  struct presets_data *data = user_data;
  GtkWidget *row = gtk_widget_get_parent(gtk_widget_get_parent(GTK_WIDGET(button)));
  const char *name = g_object_get_data(G_OBJECT(row), "preset-name");

  if (name) {
    delete_preset(data->card->serial, name);
    gtk_popover_popdown(GTK_POPOVER(data->popover));
  }
}

// Create a row for a preset
static GtkWidget *create_preset_row(
  struct presets_data *data,
  const char          *name
) {
  GtkWidget *btn = gtk_button_new();
  gtk_widget_add_css_class(btn, "flat");
  set_preset_name(btn, name);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_button_set_child(GTK_BUTTON(btn), box);

  GtkWidget *label = gtk_label_new(name);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(box), label);

  GtkWidget *delete_btn = gtk_button_new_from_icon_name("window-close-symbolic");
  gtk_widget_add_css_class(delete_btn, "flat");
  gtk_widget_add_css_class(delete_btn, "circular");
  gtk_widget_set_tooltip_text(delete_btn, "Delete preset");
  g_signal_connect(delete_btn, "clicked", G_CALLBACK(delete_preset_clicked), data);
  gtk_box_append(GTK_BOX(box), delete_btn);

  // Store delete button reference for coordinate checking
  g_object_set_data(G_OBJECT(btn), "delete-btn", delete_btn);

  // Gesture to check click location (mouse) - capture phase to run before button
  GtkGesture *gesture = gtk_gesture_click_new();
  gtk_event_controller_set_propagation_phase(
    GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_CAPTURE
  );
  g_signal_connect(gesture, "released", G_CALLBACK(preset_row_clicked), data);
  gtk_widget_add_controller(btn, GTK_EVENT_CONTROLLER(gesture));

  // Clicked signal for keyboard activation
  g_signal_connect(btn, "clicked", G_CALLBACK(preset_row_activated), data);

  return btn;
}

// Populate the presets box
static void populate_presets_list(struct presets_data *data) {
  // Remove all existing children
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(data->box)) != NULL)
    gtk_box_remove(GTK_BOX(data->box), child);

  // Scan for presets
  GList *presets = scan_presets(data->card->serial);
  int has_presets = presets != NULL;

  // Add preset rows
  for (GList *l = presets; l; l = l->next) {
    GtkWidget *row = create_preset_row(data, l->data);
    gtk_box_append(GTK_BOX(data->box), row);
  }

  // Free the list
  g_list_free_full(presets, g_free);

  // Add separator if there were presets
  if (has_presets) {
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(data->box), sep);
  }

  // Add "Save as Preset..." button
  GtkWidget *save_btn = gtk_button_new_with_label("Save as Preset...");
  g_signal_connect(save_btn, "clicked", G_CALLBACK(save_preset_clicked), data);
  gtk_box_append(GTK_BOX(data->box), save_btn);
}

// Callback when popover is shown - refresh the preset list
static void popover_show(GtkWidget *popover, gpointer user_data) {
  struct presets_data *data = user_data;
  populate_presets_list(data);
}

// Free presets data
static void free_presets_data(gpointer user_data) {
  g_free(user_data);
}

// Create the Presets button with popover menu
GtkWidget *create_presets_button(struct alsa_card *card) {
  struct presets_data *data = g_malloc0(sizeof(struct presets_data));
  data->card = card;

  // Create menu button
  GtkWidget *button = gtk_menu_button_new();
  gtk_menu_button_set_label(GTK_MENU_BUTTON(button), "Presets");

  // Create popover
  data->popover = gtk_popover_new();
  gtk_popover_set_has_arrow(GTK_POPOVER(data->popover), FALSE);
  gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), data->popover);

  // Create box for presets
  data->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_size_request(data->box, 200, -1);
  gtk_popover_set_child(GTK_POPOVER(data->popover), data->box);

  g_signal_connect(
    data->popover, "show",
    G_CALLBACK(popover_show), data
  );

  // Clean up data when button is destroyed
  g_object_set_data_full(
    G_OBJECT(button), "presets-data",
    data, free_presets_data
  );

  return button;
}
