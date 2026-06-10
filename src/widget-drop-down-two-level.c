// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <ctype.h>
#include <string.h>

#include "custom-names.h"
#include "port-enable.h"
#include "stereo-link.h"
#include "widget-drop-down-two-level.h"

// Structure to track a group of items
struct item_group {
  char   *name;      // Group name (e.g., "PCM", "Analogue")
  GArray *items;     // Array of item indices into the ALSA enum
  GArray *suffixes;  // Array of suffix strings (e.g., "1", "2")
};

struct drop_down_two_level {
  struct alsa_elem *elem;
  struct alsa_card *card;          // NULL = legacy mode
  int               stereo;        // whether sink is stereo-linked
  GtkWidget        *button;
  GtkWidget        *popover;
  GtkWidget        *group_box;     // Box containing group buttons
  GtkWidget        *item_box;      // Box containing item buttons
  GArray           *groups;        // Array of struct item_group
  int               current_group; // Currently selected group index
  GList            *name_cb_elems; // Elems with name callbacks
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

  if (last_space && last_space > name &&
      last_space < name + len - 1) {
    // Check if after the space is alphanumeric
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
static struct item_group *find_or_create_group(
  GArray     *groups,
  const char *name
) {
  for (int i = 0; i < groups->len; i++) {
    struct item_group *g =
      &g_array_index(groups, struct item_group, i);
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

  return &g_array_index(
    groups, struct item_group, groups->len - 1
  );
}

// Get routing source for a monitor group enum index
static struct routing_src *get_mg_src(
  struct drop_down_two_level *data,
  int                         enum_idx
) {
  if (!data->card || !data->card->monitor_group_src_map)
    return NULL;

  if (enum_idx < 0 ||
      enum_idx >= data->card->monitor_group_src_map_count)
    return NULL;

  int src_idx = data->card->monitor_group_src_map[enum_idx];
  if (src_idx <= 0 || src_idx >= data->card->routing_srcs->len)
    return NULL;

  return &g_array_index(
    data->card->routing_srcs, struct routing_src, src_idx
  );
}

// Get display name for a monitor group enum item.
// Returns newly allocated string. Single source of truth for
// both item labels and button label.
static char *get_mg_item_label(
  struct drop_down_two_level *data,
  int                         enum_idx
) {
  struct routing_src *src = get_mg_src(data, enum_idx);
  if (!src)
    return NULL;

  int src_linked = is_src_linked(src);
  int src_is_left = is_src_left_channel(src);

  if (src_linked && src_is_left && data->stereo)
    return get_src_pair_display_name(src);

  return g_strdup(get_routing_src_display_name(src));
}

// Build groups from ALSA enum items (legacy mode)
static void build_groups_legacy(struct drop_down_two_level *data) {
  int count = alsa_get_item_count(data->elem);

  for (int i = 0; i < count; i++) {
    const char *name = alsa_get_item_name(data->elem, i);
    char *group_name, *suffix;

    parse_item_name(name, &group_name, &suffix);

    struct item_group *group =
      find_or_create_group(data->groups, group_name);
    g_array_append_val(group->items, i);
    g_array_append_val(group->suffixes, suffix);

    g_free(group_name);
  }
}

// Build groups with stereo awareness and display names
static void build_groups_stereo(struct drop_down_two_level *data) {
  int count = alsa_get_item_count(data->elem);

  for (int i = 0; i < count; i++) {
    const char *alsa_name = alsa_get_item_name(data->elem, i);

    struct routing_src *src = get_mg_src(data, i);

    // Unmapped items: use ALSA name as fallback
    if (!src) {
      char *group_name, *suffix;
      parse_item_name(alsa_name, &group_name, &suffix);

      struct item_group *group =
        find_or_create_group(data->groups, group_name);
      g_array_append_val(group->items, i);
      g_array_append_val(group->suffixes, suffix);
      g_free(group_name);
      continue;
    }

    // Skip disabled sources
    if (!is_routing_src_enabled(src))
      continue;

    int src_linked = is_src_linked(src);
    int src_is_left = is_src_left_channel(src);

    if (data->stereo) {
      // Stereo sink: skip right channel of linked pair
      if (src_linked && !src_is_left)
        continue;
    } else {
      // Mono sink: hide linked sources entirely
      if (src_linked)
        continue;
    }

    // Derive group name from ALSA enum name
    char *group_name, *alsa_suffix;
    parse_item_name(alsa_name, &group_name, &alsa_suffix);
    g_free(alsa_suffix);

    char *suffix = get_mg_item_label(data, i);

    struct item_group *group =
      find_or_create_group(data->groups, group_name);
    g_array_append_val(group->items, i);
    g_array_append_val(group->suffixes, suffix);

    g_free(group_name);
  }
}

static void build_groups(struct drop_down_two_level *data) {
  data->groups =
    g_array_new(FALSE, FALSE, sizeof(struct item_group));

  if (data->card)
    build_groups_stereo(data);
  else
    build_groups_legacy(data);
}

// Free groups
static void free_groups(GArray *groups) {
  for (int i = 0; i < groups->len; i++) {
    struct item_group *g =
      &g_array_index(groups, struct item_group, i);
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
static void show_item_list(
  struct drop_down_two_level *data, int group_idx
);

// Callback when an item is clicked
static void item_clicked(GtkWidget *button, gpointer user_data) {
  struct drop_down_two_level *data = user_data;
  int item_idx = GPOINTER_TO_INT(
    g_object_get_data(G_OBJECT(button), "item-idx")
  );

  alsa_set_elem_value(data->elem, item_idx);
  gtk_popover_popdown(GTK_POPOVER(data->popover));
}

// Callback when a group is clicked
static void group_clicked(
  GtkWidget *button, gpointer user_data
) {
  struct drop_down_two_level *data = user_data;
  int group_idx = GPOINTER_TO_INT(
    g_object_get_data(G_OBJECT(button), "group-idx")
  );

  struct item_group *group =
    &g_array_index(data->groups, struct item_group, group_idx);

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
static void back_clicked(
  GtkWidget *button, gpointer user_data
) {
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
    struct item_group *group =
      &g_array_index(data->groups, struct item_group, i);

    GtkWidget *button =
      gtk_button_new_with_label(group->name);
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_set_hexpand(button, TRUE);

    // Check if current value is in this group
    for (int j = 0; j < group->items->len; j++) {
      int item_idx = g_array_index(group->items, int, j);
      if (item_idx == current_value) {
        gtk_widget_add_css_class(
          button, "suggested-action"
        );
        break;
      }
    }

    g_object_set_data(
      G_OBJECT(button), "group-idx", GINT_TO_POINTER(i)
    );
    g_signal_connect(
      button, "clicked", G_CALLBACK(group_clicked), data
    );

    gtk_box_append(GTK_BOX(data->group_box), button);
  }

  gtk_widget_set_visible(data->group_box, TRUE);
  gtk_widget_set_visible(data->item_box, FALSE);

  data->current_group = -1;
}

// Show item list for a specific group (second level)
static void show_item_list(
  struct drop_down_two_level *data, int group_idx
) {
  struct item_group *group =
    &g_array_index(data->groups, struct item_group, group_idx);

  // Clear existing children
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(data->item_box)))
    gtk_box_remove(GTK_BOX(data->item_box), child);

  // Add back button with group name
  GtkWidget *back_box =
    gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  GtkWidget *back_icon =
    gtk_image_new_from_icon_name("go-previous-symbolic");
  GtkWidget *back_label = gtk_label_new(group->name);
  gtk_widget_set_hexpand(back_label, TRUE);
  gtk_label_set_xalign(GTK_LABEL(back_label), 0.0);
  gtk_box_append(GTK_BOX(back_box), back_icon);
  gtk_box_append(GTK_BOX(back_box), back_label);

  GtkWidget *back_button = gtk_button_new();
  gtk_button_set_child(GTK_BUTTON(back_button), back_box);
  gtk_widget_add_css_class(back_button, "flat");
  gtk_widget_set_hexpand(back_button, TRUE);
  g_signal_connect(
    back_button, "clicked", G_CALLBACK(back_clicked), data
  );
  gtk_box_append(GTK_BOX(data->item_box), back_button);

  // Add separator
  GtkWidget *separator =
    gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_append(GTK_BOX(data->item_box), separator);

  // Get current value
  int current_value = alsa_get_elem_value(data->elem);

  // Add item buttons
  for (int i = 0; i < group->items->len; i++) {
    int item_idx = g_array_index(group->items, int, i);
    char *suffix = g_array_index(group->suffixes, char *, i);

    // Use suffix if available, otherwise ALSA name
    const char *label = suffix
      ? suffix
      : alsa_get_item_name(data->elem, item_idx);

    GtkWidget *button = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_set_hexpand(button, TRUE);

    if (item_idx == current_value)
      gtk_widget_add_css_class(button, "suggested-action");

    g_object_set_data(
      G_OBJECT(button), "item-idx",
      GINT_TO_POINTER(item_idx)
    );
    g_signal_connect(
      button, "clicked", G_CALLBACK(item_clicked), data
    );

    gtk_box_append(GTK_BOX(data->item_box), button);
  }

  gtk_widget_set_visible(data->group_box, FALSE);
  gtk_widget_set_visible(data->item_box, TRUE);

  data->current_group = group_idx;
}

// Update button label with display name for monitor group mode
static void update_button_label(struct drop_down_two_level *data) {
  int value = alsa_get_elem_value(data->elem);

  char *label = get_mg_item_label(data, value);
  if (!label) {
    const char *name = alsa_get_item_name(data->elem, value);
    gtk_button_set_label(GTK_BUTTON(data->button), name);
    return;
  }

  gtk_button_set_label(GTK_BUTTON(data->button), label);
  g_free(label);
}

// Callback when the main button is clicked
static void toggle_button_clicked(
  GtkWidget *widget, struct drop_down_two_level *data
) {
  show_group_list(data);
  gtk_popover_popup(GTK_POPOVER(data->popover));
  gtk_toggle_button_set_active(
    GTK_TOGGLE_BUTTON(data->button), FALSE
  );
}

// Callback when ALSA element value changes
static void drop_down_updated(
  struct alsa_elem *elem, void *private
) {
  struct drop_down_two_level *data = private;

  int is_writable = alsa_get_elem_writable(elem);
  gtk_widget_set_sensitive(data->button, is_writable);

  if (data->card) {
    update_button_label(data);
  } else {
    int value = alsa_get_elem_value(elem);
    const char *name = alsa_get_item_name(elem, value);
    gtk_button_set_label(GTK_BUTTON(data->button), name);
  }
}

// Rebuild groups with current display names
static void rebuild_groups(struct drop_down_two_level *data) {
  free_groups(data->groups);
  build_groups(data);
}

// Callback when a source's custom name or pair name changes
static void name_changed_callback(
  struct alsa_elem *elem, void *private
) {
  struct drop_down_two_level *data = private;
  rebuild_groups(data);
  update_button_label(data);
}

// Cleanup when widget is destroyed
static void drop_down_destroy(
  GtkWidget *widget, struct drop_down_two_level *data
) {
  alsa_elem_remove_callbacks_by_data(data->elem, data);

  for (GList *l = data->name_cb_elems; l; l = l->next) {
    struct alsa_elem *elem = l->data;
    alsa_elem_remove_callbacks_by_data(elem, data);
  }
  g_list_free(data->name_cb_elems);

  gtk_widget_unparent(data->popover);
  free_groups(data->groups);
  g_free(data);
}

// Create the popover and attach it to the button
static void create_popover(struct drop_down_two_level *data) {
  data->popover = gtk_popover_new();
  gtk_popover_set_has_arrow(
    GTK_POPOVER(data->popover), FALSE
  );
  gtk_widget_set_parent(data->popover, data->button);

  // Create stack-like container for group/item lists
  GtkWidget *stack_box =
    gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_size_request(stack_box, 150, -1);

  data->group_box =
    gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  data->item_box =
    gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

  gtk_box_append(GTK_BOX(stack_box), data->group_box);
  gtk_box_append(GTK_BOX(stack_box), data->item_box);

  // Wrap in scrolled window for long lists
  GtkWidget *scrolled = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scrolled),
    GTK_POLICY_NEVER,
    GTK_POLICY_AUTOMATIC
  );
  gtk_scrolled_window_set_max_content_height(
    GTK_SCROLLED_WINDOW(scrolled), 300
  );
  gtk_scrolled_window_set_propagate_natural_height(
    GTK_SCROLLED_WINDOW(scrolled), TRUE
  );
  gtk_scrolled_window_set_child(
    GTK_SCROLLED_WINDOW(scrolled), stack_box
  );

  gtk_popover_set_child(
    GTK_POPOVER(data->popover), scrolled
  );
}

// Register name change callbacks for relevant sources
static void register_name_callbacks(
  struct drop_down_two_level *data
) {
  if (!data->card || !data->card->monitor_group_src_map)
    return;

  int count = data->card->monitor_group_src_map_count;

  for (int i = 0; i < count; i++) {
    struct routing_src *src = get_mg_src(data, i);
    if (!src)
      continue;

    if (src->custom_name_elem) {
      // Avoid duplicate registrations (same elem for L/R)
      if (!g_list_find(
            data->name_cb_elems, src->custom_name_elem
          )) {
        alsa_elem_add_callback(
          src->custom_name_elem,
          name_changed_callback, data, NULL
        );
        data->name_cb_elems = g_list_prepend(
          data->name_cb_elems, src->custom_name_elem
        );
      }
    }

    if (src->pair_name_elem) {
      if (!g_list_find(
            data->name_cb_elems, src->pair_name_elem
          )) {
        alsa_elem_add_callback(
          src->pair_name_elem,
          name_changed_callback, data, NULL
        );
        data->name_cb_elems = g_list_prepend(
          data->name_cb_elems, src->pair_name_elem
        );
      }
    }
  }
}

static GtkWidget *create_dropdown_common(
  struct drop_down_two_level *data
) {
  build_groups(data);

  data->button = gtk_toggle_button_new();
  gtk_widget_add_css_class(data->button, "drop-down");

  create_popover(data);

  g_signal_connect(
    data->button, "clicked",
    G_CALLBACK(toggle_button_clicked), data
  );
  g_signal_connect(
    data->button, "destroy",
    G_CALLBACK(drop_down_destroy), data
  );

  drop_down_updated(data->elem, data);
  alsa_elem_add_callback(
    data->elem, drop_down_updated, data, NULL
  );

  return data->button;
}

GtkWidget *make_drop_down_two_level_alsa_elem(
  struct alsa_elem *elem
) {
  struct drop_down_two_level *data =
    g_malloc0(sizeof(struct drop_down_two_level));
  data->elem = elem;
  data->current_group = -1;
  return create_dropdown_common(data);
}

GtkWidget *make_monitor_group_source_dropdown(
  struct alsa_elem *elem,
  struct alsa_card *card,
  int               stereo
) {
  struct drop_down_two_level *data =
    g_malloc0(sizeof(struct drop_down_two_level));
  data->elem = elem;
  data->card = card;
  data->stereo = stereo;
  data->current_group = -1;

  GtkWidget *button = create_dropdown_common(data);
  register_name_callbacks(data);
  return button;
}
