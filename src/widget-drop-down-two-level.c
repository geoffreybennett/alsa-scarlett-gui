// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <ctype.h>
#include <string.h>

#include "gtkhelper.h"
#include "widget-drop-down-two-level.h"

// Structure to track a group of items
struct item_group {
  char  *name;       // Group name (e.g., "PCM", "Analogue")
  GArray *items;     // Array of item indices into the ALSA enum
  GArray *suffixes;  // Array of suffix strings (e.g., "1", "2")
};

struct drop_down_two_level {
  struct alsa_elem *elem;
  GtkWidget        *button;
  GtkWidget        *popover;
  GtkWidget        *group_box;      // Box containing group buttons
  GtkWidget        *item_box;       // Box containing item buttons
  GArray           *groups;         // Array of struct item_group
  int               current_group;  // Currently selected group index
};

// Parse item name to extract group name and suffix
// E.g., "PCM 1" -> group="PCM", suffix="1"
// E.g., "Mix A" -> group="Mix", suffix="A"
// E.g., "Off" -> group="Off", suffix=NULL
static void parse_item_name(
  const char *name,
  char      **group_out,
  char      **suffix_out
) {
  int len = strlen(name);

  // Find the last space
  const char *last_space = strrchr(name, ' ');

  if (last_space && last_space > name && last_space < name + len - 1) {
    // Check if everything after the space is alphanumeric (number or letter)
    const char *suffix = last_space + 1;
    int valid_suffix = 1;

    for (const char *p = suffix; *p; p++) {
      if (!isalnum(*p)) {
        valid_suffix = 0;
        break;
      }
    }

    if (valid_suffix) {
      *group_out = g_strndup(name, last_space - name);
      *suffix_out = g_strdup(suffix);
      return;
    }
  }

  // No valid suffix found - use whole name as group
  *group_out = g_strdup(name);
  *suffix_out = NULL;
}

// Find or create a group by name
static struct item_group *find_or_create_group(GArray *groups, const char *name) {
  for (int i = 0; i < groups->len; i++) {
    struct item_group *g = &g_array_index(groups, struct item_group, i);
    if (strcmp(g->name, name) == 0)
      return g;
  }

  // Create new group
  struct item_group new_group = {
    .name = g_strdup(name),
    .items = g_array_new(FALSE, FALSE, sizeof(int)),
    .suffixes = g_array_new(FALSE, FALSE, sizeof(char *))
  };
  g_array_append_val(groups, new_group);

  return &g_array_index(groups, struct item_group, groups->len - 1);
}

// Build groups from ALSA enum items
static void build_groups(struct drop_down_two_level *data) {
  data->groups = g_array_new(FALSE, FALSE, sizeof(struct item_group));

  int count = alsa_get_item_count(data->elem);

  for (int i = 0; i < count; i++) {
    const char *name = alsa_get_item_name(data->elem, i);
    char *group_name, *suffix;

    parse_item_name(name, &group_name, &suffix);

    struct item_group *group = find_or_create_group(data->groups, group_name);
    g_array_append_val(group->items, i);
    g_array_append_val(group->suffixes, suffix);

    g_free(group_name);
  }
}

// Free groups
static void free_groups(GArray *groups) {
  for (int i = 0; i < groups->len; i++) {
    struct item_group *g = &g_array_index(groups, struct item_group, i);
    g_free(g->name);

    for (int j = 0; j < g->suffixes->len; j++) {
      char *suffix = g_array_index(g->suffixes, char *, j);
      g_free(suffix);
    }

    g_array_free(g->items, TRUE);
    g_array_free(g->suffixes, TRUE);
  }
  g_array_free(groups, TRUE);
}

// Forward declarations
static void show_group_list(struct drop_down_two_level *data);
static void show_item_list(struct drop_down_two_level *data, int group_idx);

// Callback when an item is clicked
static void item_clicked(GtkWidget *button, gpointer user_data) {
  struct drop_down_two_level *data = user_data;
  int item_idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "item-idx"));

  alsa_set_elem_value(data->elem, item_idx);
  gtk_popover_popdown(GTK_POPOVER(data->popover));
}

// Callback when a group is clicked
static void group_clicked(GtkWidget *button, gpointer user_data) {
  struct drop_down_two_level *data = user_data;
  int group_idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "group-idx"));

  struct item_group *group = &g_array_index(data->groups, struct item_group, group_idx);

  // If group has only one item, select it directly
  if (group->items->len == 1) {
    int item_idx = g_array_index(group->items, int, 0);
    alsa_set_elem_value(data->elem, item_idx);
    gtk_popover_popdown(GTK_POPOVER(data->popover));
    return;
  }

  // Otherwise show item list for this group
  show_item_list(data, group_idx);
}

// Callback for back button
static void back_clicked(GtkWidget *button, gpointer user_data) {
  struct drop_down_two_level *data = user_data;
  show_group_list(data);
}

// Show the group list (first level)
static void show_group_list(struct drop_down_two_level *data) {
  // Clear existing children
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(data->group_box)))
    gtk_box_remove(GTK_BOX(data->group_box), child);

  while ((child = gtk_widget_get_first_child(data->item_box)))
    gtk_box_remove(GTK_BOX(data->item_box), child);

  // Get current value to highlight the active group
  int current_value = alsa_get_elem_value(data->elem);

  // Add group buttons
  for (int i = 0; i < data->groups->len; i++) {
    struct item_group *group = &g_array_index(data->groups, struct item_group, i);

    GtkWidget *button = gtk_button_new_with_label(group->name);
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_set_hexpand(button, TRUE);

    // Check if current value is in this group
    for (int j = 0; j < group->items->len; j++) {
      int item_idx = g_array_index(group->items, int, j);
      if (item_idx == current_value) {
        gtk_widget_add_css_class(button, "suggested-action");
        break;
      }
    }

    g_object_set_data(G_OBJECT(button), "group-idx", GINT_TO_POINTER(i));
    g_signal_connect(button, "clicked", G_CALLBACK(group_clicked), data);

    gtk_box_append(GTK_BOX(data->group_box), button);
  }

  gtk_widget_set_visible(data->group_box, TRUE);
  gtk_widget_set_visible(data->item_box, FALSE);

  data->current_group = -1;
}

// Show item list for a specific group (second level)
static void show_item_list(struct drop_down_two_level *data, int group_idx) {
  struct item_group *group = &g_array_index(data->groups, struct item_group, group_idx);

  // Clear existing children
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(data->item_box)))
    gtk_box_remove(GTK_BOX(data->item_box), child);

  // Add back button with group name
  GtkWidget *back_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  GtkWidget *back_icon = gtk_image_new_from_icon_name("go-previous-symbolic");
  GtkWidget *back_label = gtk_label_new(group->name);
  gtk_widget_set_hexpand(back_label, TRUE);
  gtk_label_set_xalign(GTK_LABEL(back_label), 0.0);
  gtk_box_append(GTK_BOX(back_box), back_icon);
  gtk_box_append(GTK_BOX(back_box), back_label);

  GtkWidget *back_button = gtk_button_new();
  gtk_button_set_child(GTK_BUTTON(back_button), back_box);
  gtk_widget_add_css_class(back_button, "flat");
  gtk_widget_set_hexpand(back_button, TRUE);
  g_signal_connect(back_button, "clicked", G_CALLBACK(back_clicked), data);
  gtk_box_append(GTK_BOX(data->item_box), back_button);

  // Add separator
  GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_append(GTK_BOX(data->item_box), separator);

  // Get current value
  int current_value = alsa_get_elem_value(data->elem);

  // Add item buttons
  for (int i = 0; i < group->items->len; i++) {
    int item_idx = g_array_index(group->items, int, i);
    char *suffix = g_array_index(group->suffixes, char *, i);

    // Use suffix if available, otherwise use full item name
    const char *label = suffix ? suffix : alsa_get_item_name(data->elem, item_idx);

    GtkWidget *button = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_set_hexpand(button, TRUE);

    if (item_idx == current_value)
      gtk_widget_add_css_class(button, "suggested-action");

    g_object_set_data(G_OBJECT(button), "item-idx", GINT_TO_POINTER(item_idx));
    g_signal_connect(button, "clicked", G_CALLBACK(item_clicked), data);

    gtk_box_append(GTK_BOX(data->item_box), button);
  }

  gtk_widget_set_visible(data->group_box, FALSE);
  gtk_widget_set_visible(data->item_box, TRUE);

  data->current_group = group_idx;
}

// Callback when the main button is clicked
static void toggle_button_clicked(GtkWidget *widget, struct drop_down_two_level *data) {
  show_group_list(data);
  gtk_popover_popup(GTK_POPOVER(data->popover));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->button), FALSE);
}

// Callback when ALSA element value changes
static void drop_down_updated(struct alsa_elem *elem, void *private) {
  struct drop_down_two_level *data = private;

  int is_writable = alsa_get_elem_writable(elem);
  gtk_widget_set_sensitive(data->button, is_writable);

  int value = alsa_get_elem_value(elem);
  const char *name = alsa_get_item_name(elem, value);

  gtk_button_set_label(GTK_BUTTON(data->button), name);
}

// Cleanup when widget is destroyed
static void drop_down_destroy(GtkWidget *widget, struct drop_down_two_level *data) {
  gtk_widget_unparent(data->popover);
  free_groups(data->groups);
  g_free(data);
}

GtkWidget *make_drop_down_two_level_alsa_elem(struct alsa_elem *elem) {
  struct drop_down_two_level *data = g_malloc0(sizeof(struct drop_down_two_level));
  data->elem = elem;
  data->current_group = -1;

  // Build groups from ALSA enum items
  build_groups(data);

  // Create main button
  data->button = gtk_toggle_button_new();
  gtk_widget_add_css_class(data->button, "drop-down");

  // Create popover
  data->popover = gtk_popover_new();
  gtk_popover_set_has_arrow(GTK_POPOVER(data->popover), FALSE);
  gtk_widget_set_parent(data->popover, data->button);

  // Create stack-like container for group/item lists
  GtkWidget *stack_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_size_request(stack_box, 150, -1);

  data->group_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  data->item_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

  gtk_box_append(GTK_BOX(stack_box), data->group_box);
  gtk_box_append(GTK_BOX(stack_box), data->item_box);

  // Wrap in scrolled window for long lists
  GtkWidget *scrolled = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scrolled),
    GTK_POLICY_NEVER,
    GTK_POLICY_AUTOMATIC
  );
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scrolled), 300);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scrolled), TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), stack_box);

  gtk_popover_set_child(GTK_POPOVER(data->popover), scrolled);

  // Connect signals
  g_signal_connect(data->button, "clicked", G_CALLBACK(toggle_button_clicked), data);
  g_signal_connect(data->button, "destroy", G_CALLBACK(drop_down_destroy), data);

  // Set initial state
  drop_down_updated(elem, data);

  // Register ALSA callback
  alsa_elem_add_callback(elem, drop_down_updated, data, NULL);

  return data->button;
}
