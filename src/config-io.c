// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "alsa.h"
#include "custom-names.h"
#include "device-port-names.h"
#include "stereo-link.h"
#include "widget-text-entry.h"
#include "window-configuration.h"
#include "config-io.h"

// Key for I/O sub-tab persistence
#define CONFIG_IO_TAB_KEY "configuration-io-tab"

// Structure to track a column's enable-all checkbox and its children
struct column_checkbox_data {
  GtkWidget *column_checkbox;
  GArray    *child_elems;  // array of struct alsa_elem*
  int        updating;     // flag to prevent recursion
  int        was_inconsistent;  // track if we were inconsistent before click
};

// Structure to track a tab's enable-all checkbox and its column checkboxes
struct tab_checkbox_data {
  GtkWidget                   *tab_checkbox;
  struct column_checkbox_data *left_column;   // can be NULL if no inputs
  struct column_checkbox_data *right_column;  // can be NULL if no outputs
  GArray                      *columns;       // array of column_checkbox_data* for multi-column support
  int                          updating;      // flag to prevent recursion
  int                          was_inconsistent;  // track if we were inconsistent before click
};

// Free column checkbox data
static void free_column_checkbox_data(gpointer data) {
  struct column_checkbox_data *ccd = data;
  if (ccd->child_elems)
    g_array_free(ccd->child_elems, TRUE);
  g_free(ccd);
}

// Update column checkbox state based on child states
static void update_column_checkbox_state(struct column_checkbox_data *data) {
  if (data->updating || !data->child_elems || data->child_elems->len == 0)
    return;

  int enabled_count = 0;

  for (int i = 0; i < data->child_elems->len; i++) {
    struct alsa_elem *elem = g_array_index(data->child_elems, struct alsa_elem *, i);
    if (alsa_get_elem_value(elem))
      enabled_count++;
  }

  data->updating = 1;

  GtkCheckButton *check = GTK_CHECK_BUTTON(data->column_checkbox);

  if (enabled_count == 0) {
    // all disabled
    gtk_check_button_set_active(check, FALSE);
    gtk_check_button_set_inconsistent(check, FALSE);
    data->was_inconsistent = 0;
  } else if (enabled_count == data->child_elems->len) {
    // all enabled
    gtk_check_button_set_active(check, TRUE);
    gtk_check_button_set_inconsistent(check, FALSE);
    data->was_inconsistent = 0;
  } else {
    // mixed state
    gtk_check_button_set_inconsistent(check, TRUE);
    data->was_inconsistent = 1;
  }

  data->updating = 0;
}

// Callback when an individual port enable checkbox changes
static void child_enable_changed(struct alsa_elem *elem, void *private) {
  struct column_checkbox_data *data = private;
  update_column_checkbox_state(data);
}

// Callback when column checkbox is clicked
static void column_checkbox_toggled(GtkCheckButton *button, gpointer user_data) {
  struct column_checkbox_data *data = user_data;

  if (data->updating)
    return;

  data->updating = 1;

  // If we were inconsistent before the click, turn all off (to clear the mixed state)
  // Otherwise, use the new active state (toggle between all on/all off)
  gboolean new_state;

  if (data->was_inconsistent) {
    new_state = FALSE;  // turn all off when clicking indeterminate
    gtk_check_button_set_active(button, FALSE);  // also update the checkbox itself
    data->was_inconsistent = 0;
  } else {
    gboolean active = gtk_check_button_get_active(button);
    new_state = active;  // toggle normally
  }

  // update all child checkboxes
  for (int i = 0; i < data->child_elems->len; i++) {
    struct alsa_elem *elem = g_array_index(data->child_elems, struct alsa_elem *, i);
    alsa_set_elem_value(elem, new_state ? 1 : 0);
  }

  // clear inconsistent state
  gtk_check_button_set_inconsistent(button, FALSE);

  data->updating = 0;
}

// Free tab checkbox data
static void free_tab_checkbox_data(gpointer data) {
  struct tab_checkbox_data *tcd = data;
  // don't free column data - they're managed by the columns themselves
  if (tcd->columns)
    g_array_free(tcd->columns, TRUE);
  g_free(tcd);
}

// Helper to count column state
static void count_column_state(
  struct column_checkbox_data *col,
  int *all_enabled,
  int *all_disabled,
  int *columns_exist
) {
  if (!col || !col->child_elems)
    return;

  (*columns_exist)++;
  if (gtk_check_button_get_inconsistent(GTK_CHECK_BUTTON(col->column_checkbox))) {
    // mixed state - don't count as enabled or disabled
  } else if (gtk_check_button_get_active(GTK_CHECK_BUTTON(col->column_checkbox))) {
    (*all_enabled)++;
  } else {
    (*all_disabled)++;
  }
}

// Update tab checkbox state based on column checkbox states
static void update_tab_checkbox_state(struct tab_checkbox_data *data) {
  if (data->updating)
    return;

  // count how many column checkboxes are in each state
  int all_enabled = 0;
  int all_disabled = 0;
  int columns_exist = 0;

  // Use columns array if present, otherwise fall back to left/right
  if (data->columns) {
    for (int i = 0; i < data->columns->len; i++) {
      struct column_checkbox_data *col = g_array_index(
        data->columns, struct column_checkbox_data *, i
      );
      count_column_state(col, &all_enabled, &all_disabled, &columns_exist);
    }
  } else {
    count_column_state(data->left_column, &all_enabled, &all_disabled, &columns_exist);
    count_column_state(data->right_column, &all_enabled, &all_disabled, &columns_exist);
  }

  if (columns_exist == 0)
    return;

  data->updating = 1;

  GtkCheckButton *check = GTK_CHECK_BUTTON(data->tab_checkbox);

  if (all_disabled == columns_exist) {
    // all columns are disabled
    gtk_check_button_set_active(check, FALSE);
    gtk_check_button_set_inconsistent(check, FALSE);
    data->was_inconsistent = 0;
  } else if (all_enabled == columns_exist) {
    // all columns are enabled
    gtk_check_button_set_active(check, TRUE);
    gtk_check_button_set_inconsistent(check, FALSE);
    data->was_inconsistent = 0;
  } else {
    // mixed state
    gtk_check_button_set_inconsistent(check, TRUE);
    data->was_inconsistent = 1;
  }

  data->updating = 0;
}

// Callback when a column checkbox changes (need to update tab checkbox)
static void tab_child_enable_changed(struct alsa_elem *elem, void *private) {
  struct tab_checkbox_data *data = private;
  update_tab_checkbox_state(data);
}

// Helper to set column state from tab checkbox toggle
static void set_column_state(struct column_checkbox_data *col, gboolean new_state) {
  if (!col || !col->child_elems)
    return;

  col->was_inconsistent = 0;

  // Directly set all child elements to the target state
  for (int i = 0; i < col->child_elems->len; i++) {
    struct alsa_elem *elem = g_array_index(
      col->child_elems, struct alsa_elem *, i
    );
    alsa_set_elem_value(elem, new_state ? 1 : 0);
  }

  // Update the column checkbox UI to match
  if (col->column_checkbox) {
    gtk_check_button_set_inconsistent(
      GTK_CHECK_BUTTON(col->column_checkbox),
      FALSE
    );
    gtk_check_button_set_active(
      GTK_CHECK_BUTTON(col->column_checkbox),
      new_state
    );
  }
}

// Callback when tab checkbox is clicked
static void tab_checkbox_toggled(GtkCheckButton *button, gpointer user_data) {
  struct tab_checkbox_data *data = user_data;

  if (data->updating)
    return;

  data->updating = 1;

  // If we were inconsistent before the click, turn all off
  // Otherwise, use the new active state (toggle between all on/all off)
  gboolean new_state;

  if (data->was_inconsistent) {
    new_state = FALSE;  // turn all off when clicking indeterminate
    gtk_check_button_set_active(button, FALSE);  // also update the checkbox itself
    data->was_inconsistent = 0;
  } else {
    gboolean active = gtk_check_button_get_active(button);
    new_state = active;  // toggle normally
  }

  // Set all column checkboxes to the same state
  if (data->columns) {
    for (int i = 0; i < data->columns->len; i++) {
      struct column_checkbox_data *col = g_array_index(
        data->columns, struct column_checkbox_data *, i
      );
      set_column_state(col, new_state);
    }
  } else {
    set_column_state(data->left_column, new_state);
    set_column_state(data->right_column, new_state);
  }

  // clear tab inconsistent state
  gtk_check_button_set_inconsistent(button, FALSE);

  data->updating = 0;
}

// Create a grid for name entries
static GtkWidget *create_name_grid(void) {
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  gtk_widget_set_margin_start(grid, 16);
  return grid;
}

// Callback when enable checkbox is toggled
static void enable_checkbox_toggled(GtkCheckButton *button, gpointer user_data) {
  struct alsa_elem *elem = user_data;
  gboolean active = gtk_check_button_get_active(button);
  alsa_set_elem_value(elem, active ? 1 : 0);
}

// Callback when enable element changes
static void enable_checkbox_updated(struct alsa_elem *elem, void *private) {
  GtkCheckButton *button = GTK_CHECK_BUTTON(private);
  int value = alsa_get_elem_value(elem);
  gtk_check_button_set_active(button, value != 0);
}

// Update link button appearance based on state
static void update_link_button_appearance(GtkToggleButton *button) {
  gboolean active = gtk_toggle_button_get_active(button);

  GtkWidget *label = gtk_button_get_child(GTK_BUTTON(button));
  gtk_label_set_text(GTK_LABEL(label), active ? "🔗" : "⛓️‍💥");

  if (active)
    gtk_widget_remove_css_class(GTK_WIDGET(button), "dim-label");
  else
    gtk_widget_add_css_class(GTK_WIDGET(button), "dim-label");
}

// Callback when link toggle button is clicked
static void link_button_toggled(GtkToggleButton *button, gpointer user_data) {
  struct alsa_elem *elem = user_data;
  gboolean active = gtk_toggle_button_get_active(button);
  alsa_set_elem_value(elem, active ? 1 : 0);
  update_link_button_appearance(button);
}

// Callback when link element changes
static void link_button_updated(struct alsa_elem *elem, void *private) {
  GtkToggleButton *button = GTK_TOGGLE_BUTTON(private);
  int value = alsa_get_elem_value(elem);
  gtk_toggle_button_set_active(button, value != 0);
  update_link_button_appearance(button);
}

// Create a link toggle button for a stereo pair
static GtkWidget *create_link_button(struct alsa_elem *link_elem) {
  GtkWidget *link_button = gtk_toggle_button_new();
  GtkWidget *label = gtk_label_new("🔗");
  gtk_button_set_child(GTK_BUTTON(link_button), label);
  gtk_widget_set_halign(link_button, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(link_button, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text(link_button, "Link as stereo pair");
  gtk_widget_add_css_class(link_button, "flat");

  g_signal_connect(link_button, "toggled",
    G_CALLBACK(link_button_toggled), link_elem);
  alsa_elem_add_callback(link_elem, link_button_updated, link_button, NULL);

  int value = alsa_get_elem_value(link_elem);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(link_button), value != 0);
  update_link_button_appearance(GTK_TOGGLE_BUTTON(link_button));

  return link_button;
}

// Structure to track widgets for a linkable pair (for visibility toggling)
// When linked: show pair widgets, hide individual channel widgets
// When unlinked: show individual channel widgets, hide pair widgets
struct pair_visibility_data {
  GtkWidget *ch1_enable;   // First channel enable checkbox
  GtkWidget *ch1_label;    // First channel label
  GtkWidget *ch1_entry;    // First channel name entry
  GtkWidget *ch2_enable;   // Second channel enable checkbox
  GtkWidget *ch2_label;    // Second channel label
  GtkWidget *ch2_entry;    // Second channel name entry
  GtkWidget *pair_enable;  // Pair enable checkbox (spans both rows)
  GtkWidget *pair_label;   // Pair label (spans both rows)
  GtkWidget *pair_entry;   // Pair name entry (spans both rows)

  // For mixer/DSP input pairs: data to update pair label text on link change
  struct alsa_card   *card;
  struct routing_snk *snk_left;
  struct routing_snk *snk_right;
};

// Set visibility for pair widgets based on linked state
static void set_pair_visibility(struct pair_visibility_data *pv, int linked) {
  // Individual channels: hidden when linked, shown when unlinked
  if (pv->ch1_enable)
    gtk_widget_set_visible(pv->ch1_enable, !linked);
  if (pv->ch1_label)
    gtk_widget_set_visible(pv->ch1_label, !linked);
  if (pv->ch1_entry)
    gtk_widget_set_visible(pv->ch1_entry, !linked);
  if (pv->ch2_enable)
    gtk_widget_set_visible(pv->ch2_enable, !linked);
  if (pv->ch2_label)
    gtk_widget_set_visible(pv->ch2_label, !linked);
  if (pv->ch2_entry)
    gtk_widget_set_visible(pv->ch2_entry, !linked);

  // Pair widgets: shown when linked, hidden when unlinked
  if (pv->pair_enable)
    gtk_widget_set_visible(pv->pair_enable, linked);
  if (pv->pair_label)
    gtk_widget_set_visible(pv->pair_label, linked);
  if (pv->pair_entry)
    gtk_widget_set_visible(pv->pair_entry, linked);
}

// Callback when link state changes - update widget visibility and label
static void link_visibility_changed(struct alsa_elem *elem, void *private) {
  struct pair_visibility_data *pv = private;
  int linked = alsa_get_elem_value(elem);

  // Update pair label text before changing visibility (for mixer/DSP inputs)
  if (pv->card && pv->snk_left && pv->snk_right)
    update_config_io_mixer_labels(pv->card);

  set_pair_visibility(pv, linked);
}

// Create an enable checkbox widget
static GtkWidget *create_enable_checkbox(struct alsa_elem *enable_elem) {
  if (!enable_elem)
    return NULL;

  GtkWidget *checkbox = gtk_check_button_new();
  gtk_widget_set_halign(checkbox, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(checkbox, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text(checkbox, "Show/hide this port");

  g_signal_connect(checkbox, "toggled",
    G_CALLBACK(enable_checkbox_toggled), enable_elem);
  alsa_elem_add_callback(enable_elem, enable_checkbox_updated, checkbox, NULL);

  int value = alsa_get_elem_value(enable_elem);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(checkbox), value != 0);

  return checkbox;
}

// Pre-extracted channel info for add_pair_to_grid
struct pair_channel_info {
  struct alsa_elem *enable_elem;
  struct alsa_elem *custom_name_elem;
  char             *generic_name;   // freed by add_pair_to_grid
  const char       *placeholder;    // device port name, not freed
};

// Pre-extracted pair info for add_pair_to_grid
struct pair_grid_info {
  struct alsa_elem       *link_elem;
  int                     linked;
  struct alsa_elem       *pair_name_elem;
  char                   *pair_label_text;   // freed by add_pair_to_grid
  char                   *pair_placeholder;  // freed by add_pair_to_grid
  struct pair_channel_info ch[2];            // [0]=left, [1]=right
};

// Add a linkable pair to a grid using pre-extracted channel info.
// Returns the number of rows added (always 2).
static int add_pair_to_grid(
  GtkWidget                   *grid,
  int                          row,
  struct pair_grid_info       *info,
  struct column_checkbox_data *col_data
) {
  // Link button at column 0, spanning 2 rows
  GtkWidget *link_button = create_link_button(info->link_elem);
  gtk_grid_attach(GTK_GRID(grid), link_button, 0, row, 1, 2);

  // Per-channel widgets (individual view)
  GtkWidget *ch_enable[2] = {0};
  GtkWidget *ch_label[2] = {0};
  GtkWidget *ch_entry[2] = {0};

  for (int c = 0; c < 2; c++) {
    struct pair_channel_info *ch = &info->ch[c];
    int r = row + c;

    ch_enable[c] = create_enable_checkbox(ch->enable_elem);
    if (ch_enable[c])
      gtk_grid_attach(GTK_GRID(grid), ch_enable[c], 1, r, 1, 1);

    ch_label[c] = gtk_label_new(ch->generic_name);
    gtk_widget_set_halign(ch_label[c], GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), ch_label[c], 2, r, 1, 1);
    g_free(ch->generic_name);

    if (ch->custom_name_elem) {
      ch_entry[c] = make_text_entry_alsa_elem(ch->custom_name_elem);
      gtk_widget_set_hexpand(ch_entry[c], TRUE);
      if (ch->placeholder && *ch->placeholder)
        gtk_entry_set_placeholder_text(
          GTK_ENTRY(ch_entry[c]), ch->placeholder
        );
      gtk_grid_attach(GTK_GRID(grid), ch_entry[c], 3, r, 1, 1);
    }
  }

  // Pair widgets (spanning both rows, shown when linked)
  GtkWidget *pair_enable =
    create_enable_checkbox(info->ch[0].enable_elem);
  if (pair_enable) {
    gtk_widget_set_valign(pair_enable, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), pair_enable, 1, row, 1, 2);
  }

  GtkWidget *pair_label = gtk_label_new(info->pair_label_text);
  g_free(info->pair_label_text);
  gtk_widget_set_halign(pair_label, GTK_ALIGN_START);
  gtk_widget_set_valign(pair_label, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), pair_label, 2, row, 1, 2);

  GtkWidget *pair_entry = NULL;
  if (info->pair_name_elem) {
    pair_entry = make_text_entry_alsa_elem(info->pair_name_elem);
    if (info->pair_placeholder)
      gtk_entry_set_placeholder_text(
        GTK_ENTRY(pair_entry), info->pair_placeholder
      );
    gtk_widget_set_hexpand(pair_entry, TRUE);
    gtk_widget_set_valign(pair_entry, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), pair_entry, 3, row, 1, 2);
  }
  g_free(info->pair_placeholder);

  // Setup visibility toggling
  struct pair_visibility_data *pv =
    g_malloc0(sizeof(struct pair_visibility_data));
  pv->ch1_enable = ch_enable[0];
  pv->ch1_label = ch_label[0];
  pv->ch1_entry = ch_entry[0];
  pv->ch2_enable = ch_enable[1];
  pv->ch2_label = ch_label[1];
  pv->ch2_entry = ch_entry[1];
  pv->pair_enable = pair_enable;
  pv->pair_label = pair_label;
  pv->pair_entry = pair_entry;

  set_pair_visibility(pv, info->linked);
  alsa_elem_add_callback(
    info->link_elem, link_visibility_changed, pv, g_free
  );

  // Register with column checkbox if available
  for (int c = 0; c < 2; c++) {
    if (col_data && info->ch[c].enable_elem) {
      g_array_append_val(
        col_data->child_elems, info->ch[c].enable_elem
      );
      alsa_elem_add_callback(
        info->ch[c].enable_elem, child_enable_changed,
        col_data, NULL
      );
    }
  }

  return 2;
}

// Fill pair_grid_info for a source pair
static void fill_src_pair_info(
  struct routing_src    *src_left,
  struct routing_src    *src_right,
  struct pair_grid_info *info
) {
  info->link_elem = get_src_link_elem(src_left);
  info->linked = is_src_linked(src_left);
  info->pair_name_elem = get_src_pair_name_elem(src_left);
  info->pair_label_text = get_src_generic_pair_name(src_left);

  int pair_num = (src_left->lr_num - 1) / 2;
  const char *device_pair = get_device_pair_name(
    src_left->card->pid, src_left->port_category,
    src_left->hw_type, 0, pair_num
  );
  info->pair_placeholder = device_pair ? g_strdup(device_pair) : NULL;

  struct routing_src *srcs[] = { src_left, src_right };
  for (int c = 0; c < 2; c++) {
    info->ch[c].enable_elem = srcs[c]->enable_elem;
    info->ch[c].custom_name_elem = srcs[c]->custom_name_elem;
    info->ch[c].generic_name = get_src_generic_name(srcs[c]);
    info->ch[c].placeholder = get_device_port_name(
      srcs[c]->card->pid, srcs[c]->port_category,
      srcs[c]->hw_type, 0, srcs[c]->port_num
    );
  }
}

// Fill pair_grid_info for a sink pair
static void fill_snk_pair_info(
  struct routing_snk    *snk_left,
  struct routing_snk    *snk_right,
  struct pair_grid_info *info
) {
  info->link_elem = get_snk_link_elem(snk_left);
  info->linked = is_snk_linked(snk_left);
  info->pair_name_elem = get_snk_pair_name_elem(snk_left);
  info->pair_label_text = get_snk_generic_pair_name(snk_left);

  struct alsa_elem *left_elem = snk_left->elem;
  int pair_num = (left_elem->lr_num - 1) / 2;
  const char *device_pair = get_device_pair_name(
    left_elem->card->pid, left_elem->port_category,
    left_elem->hw_type, 1, pair_num
  );
  info->pair_placeholder = device_pair ? g_strdup(device_pair) : NULL;

  struct routing_snk *snks[] = { snk_left, snk_right };
  for (int c = 0; c < 2; c++) {
    struct alsa_elem *elem = snks[c]->elem;
    info->ch[c].enable_elem = snks[c]->enable_elem;
    info->ch[c].custom_name_elem = snks[c]->custom_name_elem;
    info->ch[c].generic_name = get_snk_generic_name(snks[c]);
    info->ch[c].placeholder = get_device_port_name(
      elem->card->pid, elem->port_category,
      elem->hw_type, 1, elem->port_num
    );
  }
}

// Add a linkable source pair to a grid (returns rows consumed: 2)
static int add_src_pair_to_grid(
  GtkWidget                   *grid,
  int                          row,
  struct routing_src          *src_left,
  struct routing_src          *src_right,
  struct column_checkbox_data *col_data
) {
  struct pair_grid_info info = {0};
  fill_src_pair_info(src_left, src_right, &info);
  return add_pair_to_grid(grid, row, &info, col_data);
}

// Add a linkable sink pair to a grid (returns rows consumed: 2)
static int add_snk_pair_to_grid(
  GtkWidget                   *grid,
  int                          row,
  struct routing_snk          *snk_left,
  struct routing_snk          *snk_right,
  struct column_checkbox_data *col_data
) {
  struct pair_grid_info info = {0};
  fill_snk_pair_info(snk_left, snk_right, &info);
  return add_pair_to_grid(grid, row, &info, col_data);
}

// Structure to track mixer input label and its sink
struct mixer_input_label_data {
  GtkLabel             *label;
  struct routing_snk   *snk;
  struct alsa_card     *card;
};

// Get the port category and hw_type of the source routed to a mixer sink
// For fixed mixer inputs, this effectively categorises each mixer input
static void get_routing_src_info_for_mixer_snk(
  struct alsa_card   *card,
  struct routing_snk *snk,
  int                *port_category,  // output: PC_PCM, PC_HW, etc.
  int                *hw_type         // output: HW_TYPE_ANALOGUE, etc. (only valid if PC_HW)
) {
  int routing_src_idx = alsa_get_elem_value(snk->elem);
  if (routing_src_idx >= 0 && routing_src_idx < card->routing_srcs->len) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, routing_src_idx
    );
    *port_category = r_src->port_category;
    *hw_type = r_src->hw_type;
  } else {
    *port_category = PC_OFF;
    *hw_type = -1;
  }
}

// Check if there are mixer inputs routed from a specific source type
static int has_mixer_inputs_for_src_type(
  struct alsa_card *card,
  int               src_port_category,
  int               src_hw_type  // only used for PC_HW, -1 for others
) {
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    if (snk->elem->port_category != PC_MIX)
      continue;
    if (!snk->enable_elem)
      continue;

    int src_cat, hw_t;
    get_routing_src_info_for_mixer_snk(card, snk, &src_cat, &hw_t);

    if (src_cat == src_port_category) {
      if (src_port_category != PC_HW || hw_t == src_hw_type)
        return 1;
    }
  }
  return 0;
}

// Get the formatted text for a mixer input label
// Returns newly allocated string that must be freed
static char *get_mixer_input_label_text(
  struct alsa_card   *card,
  struct routing_snk *snk
) {
  // Get the current routing source
  int routing_src_idx = alsa_get_elem_value(snk->elem);
  struct routing_src *r_src = NULL;

  if (routing_src_idx >= 0 && routing_src_idx < card->routing_srcs->len) {
    r_src = &g_array_index(
      card->routing_srcs, struct routing_src, routing_src_idx
    );
  }

  // Format depends on whether mixer inputs are fixed
  if (card->has_fixed_mixer_inputs) {
    // For fixed mixer inputs, just show the source name
    return g_strdup(r_src ? get_routing_src_display_name(r_src) : "Off");
  } else {
    // For configurable mixer inputs, show "Mixer X - [Source Name]"
    char *snk_name = get_snk_generic_name(snk);
    char *result = g_strdup_printf(
      "%s - %s",
      snk_name,
      r_src ? get_routing_src_display_name(r_src) : "Off"
    );
    g_free(snk_name);
    return result;
  }
}

// Callback to update mixer input label when routing changes
static void mixer_input_label_updated(struct alsa_elem *elem, void *private) {
  struct mixer_input_label_data *data = private;

  char *label_text = get_mixer_input_label_text(data->card, data->snk);
  gtk_label_set_text(data->label, label_text);
  g_free(label_text);
}

// Structure to track stereo mixer input pair label
struct mixer_input_pair_label_data {
  GtkLabel           *label;
  struct routing_snk *snk_left;
  struct routing_snk *snk_right;
  struct alsa_card   *card;
};

// Generate pair label text: "Mixer X–Y - Source" or "Mixer X–Y - Left / Right"
static char *get_mixer_input_pair_label_text(
  struct alsa_card   *card,
  struct routing_snk *snk_left,
  struct routing_snk *snk_right
) {
  // Get left and right channel sources
  int idx_l = alsa_get_elem_value(snk_left->elem);
  int idx_r = alsa_get_elem_value(snk_right->elem);

  struct routing_src *r_src_l = NULL;
  struct routing_src *r_src_r = NULL;

  if (idx_l >= 0 && idx_l < card->routing_srcs->len)
    r_src_l = &g_array_index(card->routing_srcs, struct routing_src, idx_l);
  if (idx_r >= 0 && idx_r < card->routing_srcs->len)
    r_src_r = &g_array_index(card->routing_srcs, struct routing_src, idx_r);

  // For fixed mixer inputs, just show the source pair name
  if (card->has_fixed_mixer_inputs) {
    if (r_src_l && r_src_r &&
        is_src_linked(r_src_l) &&
        get_src_partner(r_src_l) == r_src_r)
      return get_src_pair_display_name(r_src_l);

    const char *src_l =
      r_src_l ? get_routing_src_display_name(r_src_l) : "Off";
    const char *src_r =
      r_src_r ? get_routing_src_display_name(r_src_r) : "Off";
    if (strcmp(src_l, src_r) == 0)
      return g_strdup(src_l);
    return g_strdup_printf("%s / %s", src_l, src_r);
  }

  char *pair_prefix = get_snk_generic_pair_name(snk_left);

  // Connected to a stereo source pair - use pair display name
  if (r_src_l && r_src_r &&
      is_src_linked(r_src_l) &&
      get_src_partner(r_src_l) == r_src_r) {
    char *pair_name = get_src_pair_display_name(r_src_l);
    char *result = g_strdup_printf(
      "%s - %s", pair_prefix, pair_name
    );
    g_free(pair_name);
    g_free(pair_prefix);
    return result;
  }

  // Get individual source names
  const char *src_l =
    r_src_l ? get_routing_src_display_name(r_src_l) : "Off";
  const char *src_r =
    r_src_r ? get_routing_src_display_name(r_src_r) : "Off";

  char *result;
  if (strcmp(src_l, src_r) == 0)
    result = g_strdup_printf("%s - %s", pair_prefix, src_l);
  else
    result = g_strdup_printf(
      "%s - %s / %s", pair_prefix, src_l, src_r
    );
  g_free(pair_prefix);
  return result;
}

// Callback to update mixer input pair label when routing changes
static void mixer_input_pair_label_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct mixer_input_pair_label_data *data = private;

  char *label_text = get_mixer_input_pair_label_text(
    data->card, data->snk_left, data->snk_right
  );
  gtk_label_set_text(data->label, label_text);
  g_free(label_text);
}

// Free mixer input pair label data
static void free_mixer_input_pair_label_data(void *data) {
  g_free(data);
}

// Callback when a source's custom name changes - update all mixer labels that
// reference it
static void source_name_changed_update_mixer_labels(
  struct alsa_elem *elem,
  void             *private
) {
  struct alsa_card *card = elem->card;

  // Find which routing source this custom name belongs to
  struct routing_src *changed_src = NULL;
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    if (r_src->custom_name_elem == elem) {
      changed_src = r_src;
      break;
    }
  }

  if (!changed_src)
    return;

  // Find the index of this source
  int changed_src_idx = changed_src->id;

  // Update all mixer/DSP input labels that are routed to this source
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    int cat = snk->elem->port_category;
    if (cat != PC_MIX && cat != PC_DSP)
      continue;

    // Check if this mixer input is routed to the changed source
    int routing_src_idx = alsa_get_elem_value(snk->elem);
    if (routing_src_idx == changed_src_idx) {
      // Trigger update of this mixer input's label by calling its callback
      alsa_elem_change(snk->elem);
    }
  }
}

// Free mixer input label data
static void free_mixer_input_label_data(void *data) {
  g_free(data);
}

// Helper to create a mixer input label with routing update callback
static GtkWidget *create_mixer_input_label(
  struct alsa_card   *card,
  struct routing_snk *snk
) {
  char *label_text = get_mixer_input_label_text(card, snk);
  GtkWidget *label = gtk_label_new(label_text);
  g_free(label_text);
  gtk_widget_set_halign(label, GTK_ALIGN_START);

  struct mixer_input_label_data *label_data =
    g_malloc(sizeof(struct mixer_input_label_data));
  label_data->label = GTK_LABEL(label);
  label_data->snk = snk;
  label_data->card = card;

  alsa_elem_add_callback(
    snk->elem,
    mixer_input_label_updated,
    label_data,
    free_mixer_input_label_data
  );

  return label;
}

// Add a mixer input stereo pair to the grid (returns rows consumed)
static int add_mixer_snk_pair_to_grid(
  struct alsa_card            *card,
  GtkWidget                   *grid,
  int                          row,
  struct routing_snk          *snk_left,
  struct routing_snk          *snk_right,
  struct column_checkbox_data *col_data
) {
  struct alsa_elem *link_elem = get_snk_link_elem(snk_left);
  int linked = is_snk_linked(snk_left);

  // Link button at column 0, spanning 2 rows
  GtkWidget *link_button = create_link_button(link_elem);
  gtk_grid_attach(GTK_GRID(grid), link_button, 0, row, 1, 2);

  // Individual channel 1 widgets
  GtkWidget *ch1_enable = create_enable_checkbox(snk_left->enable_elem);
  if (ch1_enable)
    gtk_grid_attach(GTK_GRID(grid), ch1_enable, 1, row, 1, 1);

  GtkWidget *ch1_label = create_mixer_input_label(card, snk_left);
  gtk_grid_attach(GTK_GRID(grid), ch1_label, 2, row, 1, 1);

  // Individual channel 2 widgets
  GtkWidget *ch2_enable = create_enable_checkbox(snk_right->enable_elem);
  if (ch2_enable)
    gtk_grid_attach(GTK_GRID(grid), ch2_enable, 1, row + 1, 1, 1);

  GtkWidget *ch2_label = create_mixer_input_label(card, snk_right);
  gtk_grid_attach(GTK_GRID(grid), ch2_label, 2, row + 1, 1, 1);

  // Pair widgets (spanning both rows)
  GtkWidget *pair_enable = create_enable_checkbox(snk_left->enable_elem);
  if (pair_enable) {
    gtk_widget_set_valign(pair_enable, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), pair_enable, 1, row, 1, 2);
  }

  // Pair label shows "Mixer X–Y - Source L / Source R"
  char *pair_label_text = get_mixer_input_pair_label_text(
    card, snk_left, snk_right
  );
  GtkWidget *pair_label = gtk_label_new(pair_label_text);
  g_free(pair_label_text);
  gtk_widget_set_halign(pair_label, GTK_ALIGN_START);
  gtk_widget_set_valign(pair_label, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), pair_label, 2, row, 1, 2);

  // Store reference for stereo link updates
  snk_left->config_io_pair_label = pair_label;

  // Register callbacks to update pair label when routing changes
  struct mixer_input_pair_label_data *pair_label_data =
    g_malloc(sizeof(struct mixer_input_pair_label_data));
  pair_label_data->label = GTK_LABEL(pair_label);
  pair_label_data->snk_left = snk_left;
  pair_label_data->snk_right = snk_right;
  pair_label_data->card = card;

  alsa_elem_add_callback(
    snk_left->elem, mixer_input_pair_label_updated,
    pair_label_data, NULL
  );
  alsa_elem_add_callback(
    snk_right->elem, mixer_input_pair_label_updated,
    pair_label_data, free_mixer_input_pair_label_data
  );

  // Setup visibility toggling
  struct pair_visibility_data *pv = g_malloc0(sizeof(struct pair_visibility_data));
  pv->ch1_enable = ch1_enable;
  pv->ch1_label = ch1_label;
  pv->ch1_entry = NULL;
  pv->ch2_enable = ch2_enable;
  pv->ch2_label = ch2_label;
  pv->ch2_entry = NULL;
  pv->pair_enable = pair_enable;
  pv->pair_label = pair_label;
  pv->pair_entry = NULL;
  pv->card = card;
  pv->snk_left = snk_left;
  pv->snk_right = snk_right;

  set_pair_visibility(pv, linked);
  alsa_elem_add_callback(link_elem, link_visibility_changed, pv, g_free);

  // Register both channels with column checkbox
  if (col_data) {
    if (snk_left->enable_elem) {
      g_array_append_val(col_data->child_elems, snk_left->enable_elem);
      alsa_elem_add_callback(
        snk_left->enable_elem, child_enable_changed, col_data, NULL
      );
    }
    if (snk_right->enable_elem) {
      g_array_append_val(col_data->child_elems, snk_right->enable_elem);
      alsa_elem_add_callback(
        snk_right->enable_elem, child_enable_changed, col_data, NULL
      );
    }
  }

  return 2;  // consumed 2 rows
}

// Add a pair of fixed mixer input sinks whose sources are linkable.
// Visibility is driven by the source's link element (no link button).
// Returns 2 (rows consumed).
static int add_fixed_src_pair_to_grid(
  struct alsa_card            *card,
  GtkWidget                   *grid,
  int                          row,
  struct routing_snk          *snk_left,
  struct routing_snk          *snk_right,
  struct alsa_elem            *src_link_elem,
  struct column_checkbox_data *col_data
) {
  int linked = alsa_get_elem_value(src_link_elem);

  // Individual channel widgets
  GtkWidget *ch1_enable = create_enable_checkbox(snk_left->enable_elem);
  if (ch1_enable)
    gtk_grid_attach(GTK_GRID(grid), ch1_enable, 0, row, 1, 1);

  GtkWidget *ch1_label = create_mixer_input_label(card, snk_left);
  gtk_grid_attach(GTK_GRID(grid), ch1_label, 1, row, 1, 1);

  GtkWidget *ch2_enable =
    create_enable_checkbox(snk_right->enable_elem);
  if (ch2_enable)
    gtk_grid_attach(GTK_GRID(grid), ch2_enable, 0, row + 1, 1, 1);

  GtkWidget *ch2_label = create_mixer_input_label(card, snk_right);
  gtk_grid_attach(GTK_GRID(grid), ch2_label, 1, row + 1, 1, 1);

  // Pair widgets (spanning both rows)
  GtkWidget *pair_enable =
    create_enable_checkbox(snk_left->enable_elem);
  if (pair_enable) {
    gtk_widget_set_valign(pair_enable, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), pair_enable, 0, row, 1, 2);
  }

  char *pair_label_text = get_mixer_input_pair_label_text(
    card, snk_left, snk_right
  );
  GtkWidget *pair_label = gtk_label_new(pair_label_text);
  g_free(pair_label_text);
  gtk_widget_set_halign(pair_label, GTK_ALIGN_START);
  gtk_widget_set_valign(pair_label, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), pair_label, 1, row, 1, 2);

  // Update pair label when source names change
  struct mixer_input_pair_label_data *pair_label_data =
    g_malloc(sizeof(struct mixer_input_pair_label_data));
  pair_label_data->label = GTK_LABEL(pair_label);
  pair_label_data->snk_left = snk_left;
  pair_label_data->snk_right = snk_right;
  pair_label_data->card = card;

  alsa_elem_add_callback(
    snk_left->elem, mixer_input_pair_label_updated,
    pair_label_data, NULL
  );
  alsa_elem_add_callback(
    snk_right->elem, mixer_input_pair_label_updated,
    pair_label_data, NULL
  );
  alsa_elem_add_callback(
    src_link_elem, mixer_input_pair_label_updated,
    pair_label_data, free_mixer_input_pair_label_data
  );

  // Also update when the source pair name changes
  int src_l_idx = alsa_get_elem_value(snk_left->elem);
  if (src_l_idx > 0 && src_l_idx < card->routing_srcs->len) {
    struct routing_src *src_l = &g_array_index(
      card->routing_srcs, struct routing_src, src_l_idx
    );
    struct alsa_elem *src_pair_name =
      get_src_pair_name_elem(src_l);
    if (src_pair_name)
      alsa_elem_add_callback(
        src_pair_name, mixer_input_pair_label_updated,
        pair_label_data, NULL
      );
  }

  // Visibility toggling driven by source link state
  struct pair_visibility_data *pv =
    g_malloc0(sizeof(struct pair_visibility_data));
  pv->ch1_enable = ch1_enable;
  pv->ch1_label = ch1_label;
  pv->ch2_enable = ch2_enable;
  pv->ch2_label = ch2_label;
  pv->pair_enable = pair_enable;
  pv->pair_label = pair_label;
  pv->card = card;
  pv->snk_left = snk_left;
  pv->snk_right = snk_right;

  set_pair_visibility(pv, linked);
  alsa_elem_add_callback(
    src_link_elem, link_visibility_changed, pv, g_free
  );

  // Register both channels with column checkbox
  if (col_data) {
    if (snk_left->enable_elem) {
      g_array_append_val(col_data->child_elems, snk_left->enable_elem);
      alsa_elem_add_callback(
        snk_left->enable_elem, child_enable_changed, col_data, NULL
      );
    }
    if (snk_right->enable_elem) {
      g_array_append_val(col_data->child_elems, snk_right->enable_elem);
      alsa_elem_add_callback(
        snk_right->enable_elem, child_enable_changed, col_data, NULL
      );
    }
  }

  return 2;
}

// Add enable checkboxes only (no custom names) for routing sinks
static void add_snk_enables_for_category(
  struct alsa_card            *card,
  GtkWidget                   *grid,
  int                          port_category,
  int                          hw_type,  // only used for PC_HW sinks, -1 for others
  struct column_checkbox_data *col_data,  // for column checkbox tracking
  int                          src_port_category,  // filter by source type, -1 for no filter
  int                          src_hw_type  // for PC_HW sources, -1 for no filter
) {
  int row = 0;
  int skip_idx = -1;

  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    // skip partner of a handled source pair
    if (i == skip_idx)
      continue;

    // skip if not the right category
    if (snk->elem->port_category != port_category)
      continue;

    // for hardware, also check hw_type
    if (port_category == PC_HW && snk->elem->hw_type != hw_type)
      continue;

    // for mixer inputs with source type filter, check the source type
    if (port_category == PC_MIX && src_port_category != -1) {
      int src_cat, src_hw;
      get_routing_src_info_for_mixer_snk(card, snk, &src_cat, &src_hw);
      if (src_cat != src_port_category)
        continue;
      if (src_port_category == PC_HW && src_hw != src_hw_type)
        continue;
    }

    // skip if no enable element
    if (!snk->enable_elem)
      continue;

    // For fixed mixer inputs, check if source forms a linkable pair
    if (port_category == PC_MIX && card->has_fixed_mixer_inputs) {
      int src_idx = alsa_get_elem_value(snk->elem);
      if (src_idx > 0 && src_idx < card->routing_srcs->len) {
        struct routing_src *src = &g_array_index(
          card->routing_srcs, struct routing_src, src_idx
        );
        struct alsa_elem *src_link = get_src_link_elem(src);

        if (src_link && is_src_left_channel(src)) {
          // Find the next qualifying sink with partner source
          struct routing_src *src_partner =
            get_src_partner(src);

          for (int j = i + 1;
               src_partner && j < card->routing_snks->len;
               j++) {
            struct routing_snk *snk_r = &g_array_index(
              card->routing_snks, struct routing_snk, j
            );
            if (snk_r->elem->port_category != PC_MIX)
              continue;
            if (!snk_r->enable_elem)
              continue;
            int r_idx = alsa_get_elem_value(snk_r->elem);
            if (r_idx == src_partner->id) {
              row += add_fixed_src_pair_to_grid(
                card, grid, row, snk, snk_r,
                src_link, col_data
              );
              skip_idx = j;
              goto next_snk;
            }
            break;  // not adjacent — don't search further
          }
        }
      }
    }

    // Check for stereo pair (mixer/DSP inputs, non-fixed)
    if ((port_category == PC_MIX || port_category == PC_DSP) &&
        !card->has_fixed_mixer_inputs) {
      struct routing_snk *partner = get_snk_partner(snk);

      if (partner && is_snk_left_channel(snk)) {
        row += add_mixer_snk_pair_to_grid(
          card, grid, row, snk, partner, col_data
        );
        continue;
      } else if (partner && !is_snk_left_channel(snk)) {
        // Right channel - skip (handled with its partner)
        continue;
      }
      // No partner - fall through to single channel handling
    }

    // Single channel (no partner or fixed mixer inputs)
    GtkWidget *checkbox = create_enable_checkbox(snk->enable_elem);
    if (checkbox)
      gtk_grid_attach(GTK_GRID(grid), checkbox, 0, row, 1, 1);

    // Label
    GtkWidget *label;
    if (port_category == PC_MIX || port_category == PC_DSP) {
      label = create_mixer_input_label(card, snk);
    } else {
      char *label_text = get_snk_default_name_formatted(snk, 0);
      label = gtk_label_new(label_text);
      g_free(label_text);
      gtk_widget_set_halign(label, GTK_ALIGN_START);
    }
    gtk_grid_attach(GTK_GRID(grid), label, 1, row, 1, 1);

    row++;

    // register with column checkbox if available
    if (col_data) {
      g_array_append_val(col_data->child_elems, snk->enable_elem);
      alsa_elem_add_callback(
        snk->enable_elem,
        child_enable_changed,
        col_data,
        NULL
      );
    }
next_snk:;
  }
}

// Add custom name entries for routing sources of a specific category and hw_type
static void add_src_names_for_category(
  struct alsa_card            *card,
  GtkWidget                   *grid,
  int                          port_category,
  int                          hw_type,  // only used for PC_HW, -1 for others
  struct column_checkbox_data *col_data  // for column checkbox tracking
) {
  int row = 0;

  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    // skip if not the right category
    if (src->port_category != port_category)
      continue;

    // for hardware, also check hw_type
    if (port_category == PC_HW && src->hw_type != hw_type)
      continue;

    // skip if no custom name element
    if (!src->custom_name_elem)
      continue;

    // Check if this source can be part of a stereo pair
    struct routing_src *partner = get_src_partner(src);

    if (partner && is_src_left_channel(src)) {
      // This is the left channel of a linkable pair
      row += add_src_pair_to_grid(grid, row, src, partner, col_data);
    } else if (partner && !is_src_left_channel(src)) {
      // This is the right channel - skip (already handled with its partner)
      continue;
    } else {
      // No partner - add as single channel (column 0 is empty for consistency)
      char *generic_name = get_src_generic_name(src);
      const char *device_default = get_device_port_name(
        src->card->pid, src->port_category, src->hw_type, 0, src->port_num
      );

      // Enable checkbox at column 1
      if (src->enable_elem) {
        GtkWidget *checkbox = create_enable_checkbox(src->enable_elem);
        if (checkbox)
          gtk_grid_attach(GTK_GRID(grid), checkbox, 1, row, 1, 1);
      }

      // Label at column 2
      GtkWidget *label = gtk_label_new(generic_name);
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), label, 2, row, 1, 1);

      // Entry at column 3
      GtkWidget *entry = make_text_entry_alsa_elem(src->custom_name_elem);
      gtk_widget_set_hexpand(entry, TRUE);
      if (device_default && *device_default)
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), device_default);
      gtk_grid_attach(GTK_GRID(grid), entry, 3, row, 1, 1);

      g_free(generic_name);

      // register with column checkbox if available
      if (col_data && src->enable_elem) {
        g_array_append_val(col_data->child_elems, src->enable_elem);
        alsa_elem_add_callback(
          src->enable_elem, child_enable_changed, col_data, NULL
        );
      }

      row++;
    }
  }
}

// Add custom name entries for routing sinks of a specific category and hw_type
static void add_snk_names_for_category(
  struct alsa_card            *card,
  GtkWidget                   *grid,
  int                          port_category,
  int                          hw_type,  // only used for PC_HW, -1 for others
  struct column_checkbox_data *col_data  // for column checkbox tracking
) {
  int row = 0;

  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    // skip if not the right category
    if (snk->elem->port_category != port_category)
      continue;

    // for hardware, also check hw_type
    if (port_category == PC_HW && snk->elem->hw_type != hw_type)
      continue;

    // skip if no custom name element
    if (!snk->custom_name_elem)
      continue;

    // Check if this sink can be part of a stereo pair
    struct routing_snk *partner = get_snk_partner(snk);

    if (partner && is_snk_left_channel(snk)) {
      // This is the left channel of a linkable pair
      row += add_snk_pair_to_grid(grid, row, snk, partner, col_data);
    } else if (partner && !is_snk_left_channel(snk)) {
      // This is the right channel - skip (already handled with its partner)
      continue;
    } else {
      // No partner - add as single channel (column 0 is empty for consistency)
      char *generic_name = get_snk_generic_name(snk);
      const char *device_default = get_device_port_name(
        snk->elem->card->pid, snk->elem->port_category,
        snk->elem->hw_type, 1, snk->elem->port_num
      );

      // Enable checkbox at column 1
      if (snk->enable_elem) {
        GtkWidget *checkbox = create_enable_checkbox(snk->enable_elem);
        if (checkbox)
          gtk_grid_attach(GTK_GRID(grid), checkbox, 1, row, 1, 1);
      }

      // Label at column 2
      GtkWidget *label = gtk_label_new(generic_name);
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      gtk_grid_attach(GTK_GRID(grid), label, 2, row, 1, 1);

      // Entry at column 3
      GtkWidget *entry = make_text_entry_alsa_elem(snk->custom_name_elem);
      gtk_widget_set_hexpand(entry, TRUE);
      if (device_default && *device_default)
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), device_default);
      gtk_grid_attach(GTK_GRID(grid), entry, 3, row, 1, 1);

      g_free(generic_name);

      // register with column checkbox if available
      if (col_data && snk->enable_elem) {
        g_array_append_val(col_data->child_elems, snk->enable_elem);
        alsa_elem_add_callback(
          snk->enable_elem, child_enable_changed, col_data, NULL
        );
      }

      row++;
    }
  }
}

// Check if there are any sources/sinks for a given category/hw_type combination
static int has_io_for_category(
  struct alsa_card *card,
  int               port_category,
  int               hw_type,
  int               check_sources,
  int               check_sinks
) {
  if (check_sources) {
    for (int i = 0; i < card->routing_srcs->len; i++) {
      struct routing_src *src = &g_array_index(
        card->routing_srcs, struct routing_src, i
      );
      if (src->port_category == port_category &&
          (port_category != PC_HW || src->hw_type == hw_type) &&
          src->custom_name_elem)
        return 1;
    }
  }

  if (check_sinks) {
    for (int i = 0; i < card->routing_snks->len; i++) {
      struct routing_snk *snk = &g_array_index(
        card->routing_snks, struct routing_snk, i
      );
      if (snk->elem->port_category == port_category &&
          (port_category != PC_HW || snk->elem->hw_type == hw_type) &&
          snk->custom_name_elem)
        return 1;
    }
  }

  return 0;
}

// Create a single column for mixer inputs filtered by source type
// Returns the column_checkbox_data, adds vbox to parent hbox
static struct column_checkbox_data *create_mixer_input_column(
  GtkWidget                *parent_hbox,
  struct alsa_card         *card,
  const char               *label_text,
  int                       src_port_category,
  int                       src_hw_type,
  struct tab_checkbox_data *tab_data
) {
  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

  // Header with checkbox and label
  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

  // Create column checkbox data
  struct column_checkbox_data *col_data = g_malloc0(sizeof(struct column_checkbox_data));
  col_data->child_elems = g_array_new(FALSE, FALSE, sizeof(struct alsa_elem *));
  col_data->updating = 0;

  // Column checkbox
  col_data->column_checkbox = gtk_check_button_new();
  gtk_check_button_set_active(GTK_CHECK_BUTTON(col_data->column_checkbox), TRUE);
  g_signal_connect(
    col_data->column_checkbox,
    "toggled",
    G_CALLBACK(column_checkbox_toggled),
    col_data
  );
  gtk_box_append(GTK_BOX(header), col_data->column_checkbox);

  // Label
  GtkWidget *label = gtk_label_new(NULL);
  char *markup = g_strdup_printf("<b>%s</b>", label_text);
  gtk_label_set_markup(GTK_LABEL(label), markup);
  g_free(markup);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(header), label);

  gtk_widget_set_halign(header, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(vbox), header);

  // Create grid and populate with mixer inputs filtered by source type
  GtkWidget *grid = create_name_grid();
  add_snk_enables_for_category(
    card, grid, PC_MIX, -1, col_data,
    src_port_category, src_hw_type
  );
  gtk_box_append(GTK_BOX(vbox), grid);
  gtk_box_append(GTK_BOX(parent_hbox), vbox);

  // Register callbacks from children to update tab checkbox
  for (int i = 0; i < col_data->child_elems->len; i++) {
    struct alsa_elem *elem = g_array_index(col_data->child_elems, struct alsa_elem *, i);
    alsa_elem_add_callback(elem, tab_child_enable_changed, tab_data, NULL);
  }

  // Update column checkbox initial state
  update_column_checkbox_state(col_data);

  // Attach cleanup to the vbox
  g_object_weak_ref(
    G_OBJECT(vbox),
    (GWeakNotify)free_column_checkbox_data,
    col_data
  );

  return col_data;
}

// Create a two-column layout with column checkboxes
// Returns the box containing both columns
static GtkWidget *create_two_column_layout(
  GtkWidget                    **left_grid,      // returns the left grid
  GtkWidget                    **right_grid,     // returns the right grid
  struct column_checkbox_data  **left_col_data,  // returns left column data
  struct column_checkbox_data  **right_col_data, // returns right column data
  struct tab_checkbox_data     **tab_data,       // returns tab checkbox data
  int                            show_left,
  int                            show_right,
  const char                    *left_label_text,
  const char                    *right_label_text
) {
  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 30);
  gtk_widget_set_margin_start(hbox, 20);
  gtk_widget_set_margin_end(hbox, 20);
  gtk_widget_set_margin_top(hbox, 20);
  gtk_widget_set_margin_bottom(hbox, 20);

  // Create tab checkbox data structure
  struct tab_checkbox_data *tab_checkbox_data = g_malloc0(sizeof(struct tab_checkbox_data));
  tab_checkbox_data->updating = 0;
  *tab_data = tab_checkbox_data;

  // Left column
  if (show_left) {
    GtkWidget *left_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    // Header with checkbox and label
    GtkWidget *left_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    // Create column checkbox data
    struct column_checkbox_data *col_data = g_malloc0(sizeof(struct column_checkbox_data));
    col_data->child_elems = g_array_new(FALSE, FALSE, sizeof(struct alsa_elem *));
    col_data->updating = 0;

    // Column checkbox
    col_data->column_checkbox = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(col_data->column_checkbox), TRUE);
    g_signal_connect(
      col_data->column_checkbox,
      "toggled",
      G_CALLBACK(column_checkbox_toggled),
      col_data
    );
    gtk_box_append(GTK_BOX(left_header), col_data->column_checkbox);

    // Label
    GtkWidget *left_label = gtk_label_new(NULL);
    char *left_markup = g_strdup_printf("<b>%s</b>", left_label_text);
    gtk_label_set_markup(GTK_LABEL(left_label), left_markup);
    g_free(left_markup);
    gtk_widget_set_halign(left_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(left_header), left_label);

    gtk_widget_set_halign(left_header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(left_vbox), left_header);

    *left_grid = create_name_grid();
    gtk_box_append(GTK_BOX(left_vbox), *left_grid);
    gtk_box_append(GTK_BOX(hbox), left_vbox);

    *left_col_data = col_data;
    tab_checkbox_data->left_column = col_data;

    // attach cleanup to the vbox
    g_object_weak_ref(
      G_OBJECT(left_vbox),
      (GWeakNotify)free_column_checkbox_data,
      col_data
    );
  } else {
    if (left_grid)
      *left_grid = NULL;
    if (left_col_data)
      *left_col_data = NULL;
    tab_checkbox_data->left_column = NULL;
  }

  // Right column
  if (show_right) {
    GtkWidget *right_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    // Header with checkbox and label
    GtkWidget *right_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    // Create column checkbox data
    struct column_checkbox_data *col_data = g_malloc0(sizeof(struct column_checkbox_data));
    col_data->child_elems = g_array_new(FALSE, FALSE, sizeof(struct alsa_elem *));
    col_data->updating = 0;

    // Column checkbox
    col_data->column_checkbox = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(col_data->column_checkbox), TRUE);
    g_signal_connect(
      col_data->column_checkbox,
      "toggled",
      G_CALLBACK(column_checkbox_toggled),
      col_data
    );
    gtk_box_append(GTK_BOX(right_header), col_data->column_checkbox);

    // Label
    GtkWidget *right_label = gtk_label_new(NULL);
    char *right_markup = g_strdup_printf("<b>%s</b>", right_label_text);
    gtk_label_set_markup(GTK_LABEL(right_label), right_markup);
    g_free(right_markup);
    gtk_widget_set_halign(right_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(right_header), right_label);

    gtk_widget_set_halign(right_header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(right_vbox), right_header);

    *right_grid = create_name_grid();
    gtk_box_append(GTK_BOX(right_vbox), *right_grid);
    gtk_box_append(GTK_BOX(hbox), right_vbox);

    *right_col_data = col_data;
    tab_checkbox_data->right_column = col_data;

    // attach cleanup to the vbox
    g_object_weak_ref(
      G_OBJECT(right_vbox),
      (GWeakNotify)free_column_checkbox_data,
      col_data
    );
  } else {
    if (right_grid)
      *right_grid = NULL;
    if (right_col_data)
      *right_col_data = NULL;
    tab_checkbox_data->right_column = NULL;
  }

  return hbox;
}

// Page IDs for hardware type tabs
static const char *hw_type_page_ids[] = {
  "analogue",
  "spdif",
  "adat"
};

// Add a hardware tab (Analogue, S/PDIF, or ADAT)
static void add_hw_tab(
  GtkWidget        *notebook,
  struct alsa_card *card,
  int               hw_type
) {
  int has_inputs = has_io_for_category(card, PC_HW, hw_type, 1, 0);
  int has_outputs = has_io_for_category(card, PC_HW, hw_type, 0, 1);

  if (!has_inputs && !has_outputs)
    return;

  GtkWidget *left_grid, *right_grid;
  struct column_checkbox_data *left_col_data, *right_col_data;
  struct tab_checkbox_data *tab_data;
  GtkWidget *content = create_two_column_layout(
    &left_grid, &right_grid,
    &left_col_data, &right_col_data,
    &tab_data,
    has_inputs, has_outputs,
    "Inputs", "Outputs"
  );

  // Left column: Inputs (sources - audio from hardware)
  if (left_grid)
    add_src_names_for_category(card, left_grid, PC_HW, hw_type, left_col_data);

  // Right column: Outputs (sinks - audio to hardware)
  if (right_grid)
    add_snk_names_for_category(card, right_grid, PC_HW, hw_type, right_col_data);

  // Register callbacks from children to update tab checkbox
  if (left_col_data) {
    for (int i = 0; i < left_col_data->child_elems->len; i++) {
      struct alsa_elem *elem = g_array_index(left_col_data->child_elems, struct alsa_elem *, i);
      alsa_elem_add_callback(elem, tab_child_enable_changed, tab_data, NULL);
    }
  }
  if (right_col_data) {
    for (int i = 0; i < right_col_data->child_elems->len; i++) {
      struct alsa_elem *elem = g_array_index(right_col_data->child_elems, struct alsa_elem *, i);
      alsa_elem_add_callback(elem, tab_child_enable_changed, tab_data, NULL);
    }
  }

  // Update column checkbox initial states
  if (left_col_data)
    update_column_checkbox_state(left_col_data);
  if (right_col_data)
    update_column_checkbox_state(right_col_data);

  // Create custom tab label with checkbox
  GtkWidget *tab_label_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  tab_data->tab_checkbox = gtk_check_button_new();
  gtk_check_button_set_active(GTK_CHECK_BUTTON(tab_data->tab_checkbox), TRUE);
  g_signal_connect(
    tab_data->tab_checkbox,
    "toggled",
    G_CALLBACK(tab_checkbox_toggled),
    tab_data
  );
  gtk_box_append(GTK_BOX(tab_label_box), tab_data->tab_checkbox);

  GtkWidget *tab_label_text = gtk_label_new(hw_type_names[hw_type]);
  gtk_box_append(GTK_BOX(tab_label_box), tab_label_text);

  GtkWidget *scrolled = wrap_tab_content_scrolled(content);
  g_object_set_data(G_OBJECT(scrolled), PAGE_ID_KEY, (gpointer)hw_type_page_ids[hw_type]);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, tab_label_box);

  // Update tab checkbox initial state
  update_tab_checkbox_state(tab_data);

  // attach cleanup to the content
  g_object_weak_ref(
    G_OBJECT(content),
    (GWeakNotify)free_tab_checkbox_data,
    tab_data
  );
}

// Page IDs for category tabs (matched by tab_name parameter)
static const char *get_category_page_id(const char *tab_name) {
  if (strcmp(tab_name, "PCM") == 0) return "pcm";
  if (strcmp(tab_name, "Mixer") == 0) return "mixer";
  if (strcmp(tab_name, "DSP") == 0) return "dsp";
  return tab_name;
}

// Add a non-hardware tab (PCM, Mixer, DSP)
static void add_category_tab(
  GtkWidget        *notebook,
  struct alsa_card *card,
  int               port_category,
  const char       *tab_name,
  int               show_inputs,
  int               show_outputs
) {
  if (!has_io_for_category(card, port_category, -1, show_inputs, show_outputs))
    return;

  GtkWidget *left_grid, *right_grid;
  struct column_checkbox_data *left_col_data, *right_col_data;
  struct tab_checkbox_data *tab_data;

  // PCM: sources (outputs) on left, sinks (inputs) on right
  // Others: sinks (inputs) on left, sources (outputs) on right
  int pcm = (port_category == PC_PCM);

  GtkWidget *content = create_two_column_layout(
    &left_grid, &right_grid,
    &left_col_data, &right_col_data,
    &tab_data,
    pcm ? show_outputs : show_inputs,
    pcm ? show_inputs : show_outputs,
    pcm ? "Outputs" : "Inputs",
    pcm ? "Inputs" : "Outputs"
  );

  if (pcm) {
    // Left column: Outputs (sources - audio from the subsystem)
    if (show_outputs && left_grid)
      add_src_names_for_category(
        card, left_grid, port_category, -1, left_col_data
      );

    // Right column: Inputs (sinks - audio into the subsystem)
    if (show_inputs && right_grid)
      add_snk_names_for_category(
        card, right_grid, port_category, -1, right_col_data
      );
  } else {
    // Left column: Inputs (sinks - audio into the subsystem)
    if (show_inputs && left_grid) {
      if (port_category == PC_DSP)
        add_snk_enables_for_category(
          card, left_grid, port_category, -1, left_col_data, -1, -1
        );
      else
        add_snk_names_for_category(
          card, left_grid, port_category, -1, left_col_data
        );
    }

    // Right column: Outputs (sources - audio from the subsystem)
    if (show_outputs && right_grid)
      add_src_names_for_category(
        card, right_grid, port_category, -1, right_col_data
      );
  }

  // Register callbacks from children to update tab checkbox
  if (left_col_data) {
    for (int i = 0; i < left_col_data->child_elems->len; i++) {
      struct alsa_elem *elem = g_array_index(left_col_data->child_elems, struct alsa_elem *, i);
      alsa_elem_add_callback(elem, tab_child_enable_changed, tab_data, NULL);
    }
  }
  if (right_col_data) {
    for (int i = 0; i < right_col_data->child_elems->len; i++) {
      struct alsa_elem *elem = g_array_index(right_col_data->child_elems, struct alsa_elem *, i);
      alsa_elem_add_callback(elem, tab_child_enable_changed, tab_data, NULL);
    }
  }

  // Update column checkbox initial states
  if (left_col_data)
    update_column_checkbox_state(left_col_data);
  if (right_col_data)
    update_column_checkbox_state(right_col_data);

  // Create custom tab label with checkbox
  GtkWidget *tab_label_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  tab_data->tab_checkbox = gtk_check_button_new();
  gtk_check_button_set_active(GTK_CHECK_BUTTON(tab_data->tab_checkbox), TRUE);
  g_signal_connect(
    tab_data->tab_checkbox,
    "toggled",
    G_CALLBACK(tab_checkbox_toggled),
    tab_data
  );
  gtk_box_append(GTK_BOX(tab_label_box), tab_data->tab_checkbox);

  GtkWidget *tab_label_text = gtk_label_new(tab_name);
  gtk_box_append(GTK_BOX(tab_label_box), tab_label_text);

  GtkWidget *scrolled = wrap_tab_content_scrolled(content);
  g_object_set_data(
    G_OBJECT(scrolled), PAGE_ID_KEY, (gpointer)get_category_page_id(tab_name)
  );
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, tab_label_box);

  // Update tab checkbox initial state
  update_tab_checkbox_state(tab_data);

  // attach cleanup to the content
  g_object_weak_ref(
    G_OBJECT(content),
    (GWeakNotify)free_tab_checkbox_data,
    tab_data
  );
}

// Add the Mixer tab with special handling for fixed vs non-fixed mixer inputs
static void add_mixer_tab(GtkWidget *notebook, struct alsa_card *card) {
  if (!has_io_for_category(card, PC_MIX, -1, 1, 1))
    return;

  // Register callbacks on all source custom names to update mixer labels
  // Do this once for all mixer inputs before creating them
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    if (r_src->custom_name_elem) {
      alsa_elem_add_callback(
        r_src->custom_name_elem,
        source_name_changed_update_mixer_labels,
        NULL,
        NULL
      );
    }
  }

  GtkWidget *content;
  struct tab_checkbox_data *tab_data;

  if (card->has_fixed_mixer_inputs) {
    // For fixed mixer inputs, create multiple columns by source type
    content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_margin_start(content, 20);
    gtk_widget_set_margin_end(content, 20);
    gtk_widget_set_margin_top(content, 20);
    gtk_widget_set_margin_bottom(content, 20);

    // Create tab checkbox data structure
    tab_data = g_malloc0(sizeof(struct tab_checkbox_data));
    tab_data->updating = 0;
    tab_data->columns = g_array_new(FALSE, FALSE, sizeof(struct column_checkbox_data *));

    // Labels for hardware input types
    const char *hw_labels[] = {"Analogue Inputs", "S/PDIF Inputs", "ADAT Inputs"};

    // PCM Outputs column (playback from computer)
    if (has_mixer_inputs_for_src_type(card, PC_PCM, -1)) {
      struct column_checkbox_data *col = create_mixer_input_column(
        content, card, "PCM Outputs", PC_PCM, -1, tab_data
      );
      g_array_append_val(tab_data->columns, col);
    }

    // Hardware input columns (Analogue, S/PDIF, ADAT)
    for (int hw_type = 0; hw_type < HW_TYPE_COUNT; hw_type++) {
      if (has_mixer_inputs_for_src_type(card, PC_HW, hw_type)) {
        struct column_checkbox_data *col = create_mixer_input_column(
          content, card, hw_labels[hw_type], PC_HW, hw_type, tab_data
        );
        g_array_append_val(tab_data->columns, col);
      }
    }

    // Outputs column (mixer outputs with custom names)
    if (has_io_for_category(card, PC_MIX, -1, 0, 1)) {
      GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

      // Header with checkbox and label
      GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

      // Create column checkbox data
      struct column_checkbox_data *col_data = g_malloc0(sizeof(struct column_checkbox_data));
      col_data->child_elems = g_array_new(FALSE, FALSE, sizeof(struct alsa_elem *));
      col_data->updating = 0;

      // Column checkbox
      col_data->column_checkbox = gtk_check_button_new();
      gtk_check_button_set_active(GTK_CHECK_BUTTON(col_data->column_checkbox), TRUE);
      g_signal_connect(
        col_data->column_checkbox,
        "toggled",
        G_CALLBACK(column_checkbox_toggled),
        col_data
      );
      gtk_box_append(GTK_BOX(header), col_data->column_checkbox);

      // Label
      GtkWidget *label = gtk_label_new(NULL);
      gtk_label_set_markup(GTK_LABEL(label), "<b>Outputs</b>");
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      gtk_box_append(GTK_BOX(header), label);

      gtk_widget_set_halign(header, GTK_ALIGN_START);
      gtk_box_append(GTK_BOX(vbox), header);

      GtkWidget *grid = create_name_grid();
      add_src_names_for_category(card, grid, PC_MIX, -1, col_data);
      gtk_box_append(GTK_BOX(vbox), grid);
      gtk_box_append(GTK_BOX(content), vbox);

      // Register callbacks from children to update tab checkbox
      for (int i = 0; i < col_data->child_elems->len; i++) {
        struct alsa_elem *elem = g_array_index(col_data->child_elems, struct alsa_elem *, i);
        alsa_elem_add_callback(elem, tab_child_enable_changed, tab_data, NULL);
      }

      update_column_checkbox_state(col_data);
      g_array_append_val(tab_data->columns, col_data);

      // Attach cleanup to the vbox
      g_object_weak_ref(
        G_OBJECT(vbox),
        (GWeakNotify)free_column_checkbox_data,
        col_data
      );
    }
  } else {
    // Non-fixed mixer inputs: single Inputs column with "Mixer X - [Source]" format
    GtkWidget *left_grid, *right_grid;
    struct column_checkbox_data *left_col_data, *right_col_data;
    content = create_two_column_layout(
      &left_grid, &right_grid,
      &left_col_data, &right_col_data,
      &tab_data,
      1, 1,
      "Inputs", "Outputs"
    );

    // Left column: Mixer Inputs (with stereo linking, no custom names)
    if (left_grid)
      add_snk_enables_for_category(card, left_grid, PC_MIX, -1, left_col_data, -1, -1);

    // Right column: Mixer Outputs (with custom names)
    if (right_grid)
      add_src_names_for_category(card, right_grid, PC_MIX, -1, right_col_data);

    // Register callbacks from children to update tab checkbox
    if (left_col_data) {
      for (int i = 0; i < left_col_data->child_elems->len; i++) {
        struct alsa_elem *elem = g_array_index(left_col_data->child_elems, struct alsa_elem *, i);
        alsa_elem_add_callback(elem, tab_child_enable_changed, tab_data, NULL);
      }
    }
    if (right_col_data) {
      for (int i = 0; i < right_col_data->child_elems->len; i++) {
        struct alsa_elem *elem = g_array_index(right_col_data->child_elems, struct alsa_elem *, i);
        alsa_elem_add_callback(elem, tab_child_enable_changed, tab_data, NULL);
      }
    }

    // Update column checkbox initial states
    if (left_col_data)
      update_column_checkbox_state(left_col_data);
    if (right_col_data)
      update_column_checkbox_state(right_col_data);
  }

  // Create custom tab label with checkbox
  GtkWidget *tab_label_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  tab_data->tab_checkbox = gtk_check_button_new();
  gtk_check_button_set_active(GTK_CHECK_BUTTON(tab_data->tab_checkbox), TRUE);
  g_signal_connect(
    tab_data->tab_checkbox,
    "toggled",
    G_CALLBACK(tab_checkbox_toggled),
    tab_data
  );
  gtk_box_append(GTK_BOX(tab_label_box), tab_data->tab_checkbox);

  GtkWidget *tab_label_text = gtk_label_new("Mixer");
  gtk_box_append(GTK_BOX(tab_label_box), tab_label_text);

  GtkWidget *scrolled = wrap_tab_content_scrolled(content);
  g_object_set_data(G_OBJECT(scrolled), PAGE_ID_KEY, (gpointer)"mixer");
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, tab_label_box);

  // Update tab checkbox initial state
  update_tab_checkbox_state(tab_data);

  // attach cleanup to the content
  g_object_weak_ref(
    G_OBJECT(content),
    (GWeakNotify)free_tab_checkbox_data,
    tab_data
  );
}

// Update all config-io mixer/DSP input pair labels
// Called when source stereo link state changes
void update_config_io_mixer_labels(struct alsa_card *card) {
  if (!card || !card->routing_snks)
    return;

  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    // Only process mixer/DSP inputs with pair labels
    int cat = snk->elem->port_category;
    if (cat != PC_MIX && cat != PC_DSP)
      continue;

    if (!snk->config_io_pair_label)
      continue;

    // Get the partner (snk is the left channel since it has the label)
    struct routing_snk *partner = get_snk_partner(snk);
    if (!partner)
      continue;

    char *label_text = get_mixer_input_pair_label_text(card, snk, partner);
    gtk_label_set_text(GTK_LABEL(snk->config_io_pair_label), label_text);
    g_free(label_text);
  }
}

void add_io_tab(GtkWidget *top_notebook, struct alsa_card *card) {
  // Create the sub-notebook for I/O Configuration
  GtkWidget *notebook = gtk_notebook_new();

  // Add tabs for each hardware type (Analogue, S/PDIF, ADAT)
  for (int hw_type = 0; hw_type < HW_TYPE_COUNT; hw_type++) {
    add_hw_tab(notebook, card, hw_type);
  }

  // Add tabs for other categories (PCM, DSP, Mixer)
  add_category_tab(notebook, card, PC_PCM, "PCM", 1, 1);
  add_category_tab(notebook, card, PC_DSP, "DSP", 1, 1);
  add_mixer_tab(notebook, card);

  // I/O tab (only if there are any sub-tabs)
  if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)) > 0) {
    GtkWidget *io_tab_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(io_tab_content, 20);

    GtkWidget *io_help = gtk_label_new(
      "Use the checkboxes to hide unused inputs and outputs from the display.\n"
      "You can also give each port a custom name to help identify it.\n"
      "Use the link buttons to pair adjacent channels as stereo."
    );
    gtk_widget_set_halign(io_help, GTK_ALIGN_START);
    gtk_widget_add_css_class(io_help, "dim-label");
    gtk_box_append(GTK_BOX(io_tab_content), io_help);

    gtk_widget_set_vexpand(notebook, TRUE);
    gtk_box_append(GTK_BOX(io_tab_content), notebook);

    // Restore saved I/O tab and connect signal to save tab changes
    setup_notebook_tab_persistence(
      GTK_NOTEBOOK(notebook), card, CONFIG_IO_TAB_KEY
    );

    GtkWidget *io_tab_label = gtk_label_new("I/O Configuration");
    g_object_set_data(
      G_OBJECT(io_tab_content), PAGE_ID_KEY, (gpointer)"io-config"
    );
    gtk_notebook_append_page(
      GTK_NOTEBOOK(top_notebook), io_tab_content, io_tab_label
    );
  }
}
