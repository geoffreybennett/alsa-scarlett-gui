// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "alsa.h"
#include "gtkhelper.h"
#include "optional-controls.h"
#include "optional-state.h"
#include "custom-names.h"
#include "device-port-names.h"
#include "port-enable.h"
#include "widget-drop-down.h"
#include "widget-drop-down-two-level.h"
#include "widget-gain.h"
#include "widget-text-entry.h"
#include "window-configuration.h"

struct configuration_window {
  struct alsa_card *card;
  GtkWidget        *top;
};

// Structure to track Main/Alt Group controls for one output
struct group_output_data {
  struct alsa_elem *main_elem;
  struct alsa_elem *alt_elem;
  struct alsa_elem *main_source_elem;
  struct alsa_elem *alt_source_elem;
  struct alsa_elem *main_trim_elem;
  struct alsa_elem *alt_trim_elem;
};

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

static void on_destroy(
  struct configuration_window *data,
  GtkWidget                   *widget
) {
  g_free(data);
}

// Keys used to store the configuration window tabs in the state file
#define CONFIG_TAB_KEY "configuration-tab"
#define CONFIG_IO_TAB_KEY "configuration-io-tab"

// Key for storing page ID on notebook pages
#define PAGE_ID_KEY "page-id"

// Data for notebook tab persistence
struct notebook_tab_data {
  struct alsa_card *card;
  const char       *key;
};

// Callback when a notebook tab changes
static void on_tab_changed(
  GtkNotebook *notebook,
  GtkWidget   *page,
  guint        page_num,
  gpointer     user_data
) {
  // During window destruction, the notebook is detached from the
  // hierarchy before tabs are removed, triggering this callback.
  if (!gtk_widget_get_root(GTK_WIDGET(notebook)))
    return;

  struct notebook_tab_data *data = user_data;

  const char *page_id = g_object_get_data(G_OBJECT(page), PAGE_ID_KEY);
  if (page_id)
    optional_state_save(data->card, CONFIG_SECTION_UI, data->key, page_id);
}

// Free notebook tab data
static void free_notebook_tab_data(gpointer data) {
  g_free(data);
}

// Find page index by ID, returns -1 if not found
static int find_page_by_id(GtkNotebook *notebook, const char *page_id) {
  int n_pages = gtk_notebook_get_n_pages(notebook);

  for (int i = 0; i < n_pages; i++) {
    GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
    const char *id = g_object_get_data(G_OBJECT(page), PAGE_ID_KEY);
    if (id && strcmp(id, page_id) == 0)
      return i;
  }

  return -1;
}

// Restore the saved tab selection and connect signal to save changes
static void setup_notebook_tab_persistence(
  GtkNotebook      *notebook,
  struct alsa_card *card,
  const char       *key
) {
  // Restore saved tab from [ui] section
  GHashTable *state = optional_state_load(card, CONFIG_SECTION_UI);
  if (state) {
    const char *value = g_hash_table_lookup(state, key);
    if (value) {
      int page_num = find_page_by_id(notebook, value);
      if (page_num >= 0)
        gtk_notebook_set_current_page(notebook, page_num);
    }
    g_hash_table_destroy(state);
  }

  // Connect signal to save tab changes
  struct notebook_tab_data *data = g_malloc(sizeof(struct notebook_tab_data));
  data->card = card;
  data->key = key;

  g_signal_connect_data(
    notebook, "switch-page", G_CALLBACK(on_tab_changed), data,
    (GClosureNotify)free_notebook_tab_data, 0
  );
}

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

// Add a custom name entry to a grid with enable checkbox
static void add_name_entry_to_grid(
  GtkWidget        *grid,
  int               row,
  const char       *label_text,
  const char       *placeholder,  // device-specific default, or NULL
  struct alsa_elem *custom_name_elem,
  struct alsa_elem *enable_elem
) {
  // Enable checkbox
  if (enable_elem) {
    GtkWidget *checkbox = gtk_check_button_new();
    gtk_widget_set_halign(checkbox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(checkbox, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(checkbox, "Show/hide this port");

    // connect signals
    g_signal_connect(
      checkbox,
      "toggled",
      G_CALLBACK(enable_checkbox_toggled),
      enable_elem
    );

    alsa_elem_add_callback(
      enable_elem,
      enable_checkbox_updated,
      checkbox,
      NULL
    );

    // set initial state
    int value = alsa_get_elem_value(enable_elem);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(checkbox), value != 0);

    gtk_grid_attach(GTK_GRID(grid), checkbox, 0, row, 1, 1);
  }

  // Label showing the generic hardware name
  GtkWidget *label = gtk_label_new(label_text);
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 1, row, 1, 1);

  // Text entry for custom name
  GtkWidget *entry = make_text_entry_alsa_elem(custom_name_elem);
  gtk_widget_set_hexpand(entry, TRUE);

  // Set placeholder text if device-specific default exists
  if (placeholder && *placeholder)
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);

  gtk_grid_attach(GTK_GRID(grid), entry, 2, row, 1, 1);
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

// Callback when a source's custom name changes - update all mixer labels that reference it
static void source_name_changed_update_mixer_labels(struct alsa_elem *elem, void *private) {
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

  // Update all mixer input labels that are currently routed to this source
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (snk->elem->port_category != PC_MIX)
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

    // Enable checkbox
    GtkWidget *checkbox = gtk_check_button_new();
    gtk_widget_set_halign(checkbox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(checkbox, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(checkbox, "Show/hide this port");

    g_signal_connect(
      checkbox,
      "toggled",
      G_CALLBACK(enable_checkbox_toggled),
      snk->enable_elem
    );

    alsa_elem_add_callback(
      snk->enable_elem,
      enable_checkbox_updated,
      checkbox,
      NULL
    );

    int value = alsa_get_elem_value(snk->enable_elem);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(checkbox), value != 0);

    gtk_grid_attach(GTK_GRID(grid), checkbox, 0, row, 1, 1);

    // Label showing the mixer input and current source
    GtkWidget *label;
    if (port_category == PC_MIX) {
      // For mixer inputs, get the formatted label text
      char *label_text = get_mixer_input_label_text(card, snk);
      label = gtk_label_new(label_text);
      g_free(label_text);

      // Create data structure for the callback
      struct mixer_input_label_data *label_data =
        g_malloc(sizeof(struct mixer_input_label_data));
      label_data->label = GTK_LABEL(label);
      label_data->snk = snk;
      label_data->card = card;

      // Register callback to update label when routing changes
      alsa_elem_add_callback(
        snk->elem,
        mixer_input_label_updated,
        label_data,
        free_mixer_input_label_data
      );
    } else {
      // For other categories, use formatted default name
      char *label_text = get_snk_default_name_formatted(snk);
      label = gtk_label_new(label_text);
      g_free(label_text);
    }

    gtk_widget_set_halign(label, GTK_ALIGN_START);
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

    char *generic_name = get_src_generic_name(src);
    const char *device_default = get_device_port_name(
      src->card->pid, src->port_category, src->hw_type, 0, src->port_num
    );
    add_name_entry_to_grid(
      grid,
      row++,
      generic_name,
      device_default,
      src->custom_name_elem,
      src->enable_elem
    );
    g_free(generic_name);

    // register with column checkbox if available
    if (col_data && src->enable_elem) {
      g_array_append_val(col_data->child_elems, src->enable_elem);
      alsa_elem_add_callback(
        src->enable_elem,
        child_enable_changed,
        col_data,
        NULL
      );
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

    char *generic_name = get_snk_generic_name(snk);
    const char *device_default = get_device_port_name(
      snk->elem->card->pid,
      snk->elem->port_category,
      snk->elem->hw_type,
      1,
      snk->elem->port_num
    );
    add_name_entry_to_grid(
      grid,
      row++,
      generic_name,
      device_default,
      snk->custom_name_elem,
      snk->enable_elem
    );
    g_free(generic_name);

    // register with column checkbox if available
    if (col_data && snk->enable_elem) {
      g_array_append_val(col_data->child_elems, snk->enable_elem);
      alsa_elem_add_callback(
        snk->enable_elem,
        child_enable_changed,
        col_data,
        NULL
      );
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

// Create a two-column layout for inputs and outputs with column checkboxes
// Returns the box containing both columns
static GtkWidget *create_two_column_layout(
  GtkWidget                    **left_grid,      // returns the left grid (Inputs)
  GtkWidget                    **right_grid,     // returns the right grid (Outputs)
  struct column_checkbox_data  **left_col_data,  // returns left column data
  struct column_checkbox_data  **right_col_data, // returns right column data
  struct tab_checkbox_data     **tab_data,       // returns tab checkbox data
  int                            show_inputs,
  int                            show_outputs
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

  // Left column (Inputs)
  if (show_inputs) {
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
    gtk_label_set_markup(GTK_LABEL(left_label), "<b>Inputs</b>");
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

  // Right column (Outputs)
  if (show_outputs) {
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
    gtk_label_set_markup(GTK_LABEL(right_label), "<b>Outputs</b>");
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

// Wrap tab content in a scrolled window for vertical scrolling
static GtkWidget *wrap_tab_content_scrolled(GtkWidget *content) {
  GtkWidget *scrolled = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scrolled),
    GTK_POLICY_NEVER,
    GTK_POLICY_AUTOMATIC
  );
  gtk_scrolled_window_set_propagate_natural_height(
    GTK_SCROLLED_WINDOW(scrolled), TRUE
  );
  gtk_widget_set_vexpand(scrolled, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), content);
  return scrolled;
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
    has_inputs, has_outputs
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
  GtkWidget *content = create_two_column_layout(
    &left_grid, &right_grid,
    &left_col_data, &right_col_data,
    &tab_data,
    show_inputs, show_outputs
  );

  // Left column: Inputs (sinks - audio into the subsystem)
  if (show_inputs && left_grid) {
    // DSP inputs only get enable checkboxes (no custom names)
    if (port_category == PC_DSP)
      add_snk_enables_for_category(card, left_grid, port_category, -1, left_col_data, -1, -1);
    else
      add_snk_names_for_category(card, left_grid, port_category, -1, left_col_data);
  }

  // Right column: Outputs (sources - audio from the subsystem)
  if (show_outputs && right_grid)
    add_src_names_for_category(card, right_grid, port_category, -1, right_col_data);

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

// Check if Main/Alt Group controls exist
static int has_main_alt_group_controls(struct alsa_card *card) {
  struct alsa_elem *elem = get_elem_by_prefix(
    card->elems, "Main Group Output"
  );
  return elem != NULL;
}

// Get Main/Alt Group elements for a given output number (1-based)
static void get_group_output_elems(
  struct alsa_card        *card,
  int                      output_num,
  struct group_output_data *data
) {
  char name[64];

  snprintf(name, sizeof(name), "Main Group Output %d Playback Switch", output_num);
  data->main_elem = get_elem_by_name(card->elems, name);

  snprintf(name, sizeof(name), "Alt Group Output %d Playback Switch", output_num);
  data->alt_elem = get_elem_by_name(card->elems, name);

  snprintf(name, sizeof(name), "Main Group Output %d Source Playback Enum", output_num);
  data->main_source_elem = get_elem_by_name(card->elems, name);

  snprintf(name, sizeof(name), "Alt Group Output %d Source Playback Enum", output_num);
  data->alt_source_elem = get_elem_by_name(card->elems, name);

  snprintf(name, sizeof(name), "Main Group Output %d Trim Playback Volume", output_num);
  data->main_trim_elem = get_elem_by_name(card->elems, name);

  snprintf(name, sizeof(name), "Alt Group Output %d Trim Playback Volume", output_num);
  data->alt_trim_elem = get_elem_by_name(card->elems, name);
}

// Get the routing sink for an analogue output by number (1-based)
static struct routing_snk *get_analogue_output_snk(
  struct alsa_card *card,
  int               output_num
) {
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    if (snk->elem->port_category == PC_HW &&
        snk->elem->hw_type == HW_TYPE_ANALOGUE &&
        snk->elem->lr_num == output_num) {
      return snk;
    }
  }
  return NULL;
}

// Structure to track group output label and its sink
struct group_output_label_data {
  GtkLabel           *label;
  struct routing_snk *snk;
};

// Structure to track widgets that should be enabled/disabled based on group output state
struct group_enable_data {
  GtkWidget *source_widget;
  GtkWidget *trim_widget;
};

// Callback to update source/trim sensitivity when group output enabled state changes
static void group_enable_updated(struct alsa_elem *elem, void *private) {
  struct group_enable_data *data = private;
  int enabled = alsa_get_elem_value(elem);

  if (data->source_widget)
    gtk_widget_set_sensitive(data->source_widget, enabled);
  if (data->trim_widget)
    gtk_widget_set_sensitive(data->trim_widget, enabled);
}

// Free group enable data
static void free_group_enable_data(void *data) {
  g_free(data);
}

// Callback to update group output label when custom name changes
static void group_output_label_updated(struct alsa_elem *elem, void *private) {
  struct group_output_label_data *data = private;
  char *text = get_snk_display_name_formatted(data->snk);
  gtk_label_set_text(data->label, text);
  g_free(text);
}

// Free group output label data
static void free_group_output_label_data(void *data) {
  g_free(data);
}

// Create the Main/Alt Group grid
static GtkWidget *create_main_alt_group_grid(struct alsa_card *card) {
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  gtk_widget_set_margin_top(grid, 10);
  gtk_widget_set_valign(grid, GTK_ALIGN_START);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);

  // Check if source/trim controls exist (check first output)
  struct group_output_data first_data;
  get_group_output_elems(card, 1, &first_data);
  int has_source = first_data.main_source_elem != NULL;
  int has_trim = first_data.main_trim_elem != NULL;

  // Column layout with separators:
  // 0: Output label
  // 1: Separator
  // 2: Main checkbox
  // 3: Main Source (if has_source)
  // 4: Main Trim (if has_trim)
  // 5: Separator
  // 6: Alt checkbox
  // 7: Alt Source (if has_source)
  // 8: Alt Trim (if has_trim)
  int sep1_col = 1;
  int main_col = 2;
  int main_source_col = has_source ? 3 : -1;
  int main_trim_col = has_trim ? (has_source ? 4 : 3) : -1;
  int sep2_col = 2 + 1 + (has_source ? 1 : 0) + (has_trim ? 1 : 0);
  int alt_col = sep2_col + 1;
  int alt_source_col = has_source ? alt_col + 1 : -1;
  int alt_trim_col = has_trim ? alt_col + 1 + (has_source ? 1 : 0) : -1;

  // Calculate column spans for Main and Alt headers
  int main_span = 1 + (has_source ? 1 : 0) + (has_trim ? 1 : 0);
  int alt_span = main_span;

  // Row 0: Main group headers
  GtkWidget *main_header = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(main_header), "<b>Main</b>");
  gtk_widget_set_halign(main_header, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), main_header, main_col, 0, main_span, 1);

  GtkWidget *alt_header = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(alt_header), "<b>Alt</b>");
  gtk_widget_set_halign(alt_header, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), alt_header, alt_col, 0, alt_span, 1);

  // Row 1: Sub-headings for columns
  GtkWidget *main_enable_label = gtk_label_new("Enable");
  gtk_widget_add_css_class(main_enable_label, "dim-label");
  gtk_grid_attach(GTK_GRID(grid), main_enable_label, main_col, 1, 1, 1);

  if (has_source) {
    GtkWidget *main_source_label = gtk_label_new("Source");
    gtk_widget_add_css_class(main_source_label, "dim-label");
    gtk_grid_attach(GTK_GRID(grid), main_source_label, main_source_col, 1, 1, 1);
  }

  if (has_trim) {
    GtkWidget *main_trim_label = gtk_label_new("Trim");
    gtk_widget_add_css_class(main_trim_label, "dim-label");
    gtk_grid_attach(GTK_GRID(grid), main_trim_label, main_trim_col, 1, 1, 1);
  }

  GtkWidget *alt_enable_label = gtk_label_new("Enable");
  gtk_widget_add_css_class(alt_enable_label, "dim-label");
  gtk_grid_attach(GTK_GRID(grid), alt_enable_label, alt_col, 1, 1, 1);

  if (has_source) {
    GtkWidget *alt_source_label = gtk_label_new("Source");
    gtk_widget_add_css_class(alt_source_label, "dim-label");
    gtk_grid_attach(GTK_GRID(grid), alt_source_label, alt_source_col, 1, 1, 1);
  }

  if (has_trim) {
    GtkWidget *alt_trim_label = gtk_label_new("Trim");
    gtk_widget_add_css_class(alt_trim_label, "dim-label");
    gtk_grid_attach(GTK_GRID(grid), alt_trim_label, alt_trim_col, 1, 1, 1);
  }

  // Add rows for each output (starting after 2 header rows)
  int row = 2;
  for (int i = 1; ; i++) {
    struct group_output_data data;
    get_group_output_elems(card, i, &data);

    // Stop when no more outputs exist
    if (!data.main_elem && !data.alt_elem)
      break;

    // Row label - use formatted display name (custom name or default)
    struct routing_snk *snk = get_analogue_output_snk(card, i);
    char *label_text = snk ? get_snk_display_name_formatted(snk) : g_strdup("");
    GtkWidget *label = gtk_label_new(label_text);
    g_free(label_text);
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

    // Register callback to update label when custom name changes
    if (snk && snk->custom_name_elem) {
      struct group_output_label_data *label_data =
        g_malloc(sizeof(struct group_output_label_data));
      label_data->label = GTK_LABEL(label);
      label_data->snk = snk;

      alsa_elem_add_callback(
        snk->custom_name_elem,
        group_output_label_updated,
        label_data,
        free_group_output_label_data
      );
    }

    // Main Source dropdown (create before checkbox so we can track it)
    GtkWidget *main_source = NULL;
    if (data.main_source_elem && main_source_col >= 0) {
      main_source = make_drop_down_two_level_alsa_elem(data.main_source_elem);
      gtk_widget_set_size_request(main_source, 120, -1);
      gtk_widget_set_hexpand(main_source, FALSE);
      gtk_widget_set_valign(main_source, GTK_ALIGN_CENTER);
      gtk_grid_attach(GTK_GRID(grid), main_source, main_source_col, row, 1, 1);
    }

    // Main Trim (create before checkbox so we can track it)
    GtkWidget *main_trim = NULL;
    if (data.main_trim_elem && main_trim_col >= 0) {
      main_trim = make_gain_alsa_elem(
        data.main_trim_elem,
        0,                       // zero_is_off
        WIDGET_GAIN_TAPER_LINEAR,
        1,                       // can_control
        FALSE                    // show_level
      );
      gtk_widget_set_size_request(main_trim, 40, 80);
      gtk_widget_set_hexpand(main_trim, FALSE);
      gtk_widget_set_valign(main_trim, GTK_ALIGN_CENTER);
      gtk_grid_attach(GTK_GRID(grid), main_trim, main_trim_col, row, 1, 1);
    }

    // Main checkbox
    if (data.main_elem) {
      GtkWidget *main_check = gtk_check_button_new();
      gtk_widget_set_halign(main_check, GTK_ALIGN_CENTER);
      gtk_widget_set_valign(main_check, GTK_ALIGN_CENTER);

      g_signal_connect(
        main_check,
        "toggled",
        G_CALLBACK(enable_checkbox_toggled),
        data.main_elem
      );

      alsa_elem_add_callback(
        data.main_elem,
        enable_checkbox_updated,
        main_check,
        NULL
      );

      int value = alsa_get_elem_value(data.main_elem);
      gtk_check_button_set_active(GTK_CHECK_BUTTON(main_check), value != 0);

      gtk_grid_attach(GTK_GRID(grid), main_check, main_col, row, 1, 1);

      // Register callback to enable/disable source and trim when main is toggled
      if (main_source || main_trim) {
        struct group_enable_data *enable_data =
          g_malloc(sizeof(struct group_enable_data));
        enable_data->source_widget = main_source;
        enable_data->trim_widget = main_trim;

        alsa_elem_add_callback(
          data.main_elem,
          group_enable_updated,
          enable_data,
          free_group_enable_data
        );

        // Set initial sensitivity based on current state
        if (main_source)
          gtk_widget_set_sensitive(main_source, value != 0);
        if (main_trim)
          gtk_widget_set_sensitive(main_trim, value != 0);
      }
    }

    // Alt Source dropdown (create before checkbox so we can track it)
    GtkWidget *alt_source = NULL;
    if (data.alt_source_elem && alt_source_col >= 0) {
      alt_source = make_drop_down_two_level_alsa_elem(data.alt_source_elem);
      gtk_widget_set_size_request(alt_source, 120, -1);
      gtk_widget_set_hexpand(alt_source, FALSE);
      gtk_widget_set_valign(alt_source, GTK_ALIGN_CENTER);
      gtk_grid_attach(GTK_GRID(grid), alt_source, alt_source_col, row, 1, 1);
    }

    // Alt Trim (create before checkbox so we can track it)
    GtkWidget *alt_trim = NULL;
    if (data.alt_trim_elem && alt_trim_col >= 0) {
      alt_trim = make_gain_alsa_elem(
        data.alt_trim_elem,
        0,                       // zero_is_off
        WIDGET_GAIN_TAPER_LINEAR,
        1,                       // can_control
        FALSE                    // show_level
      );
      gtk_widget_set_size_request(alt_trim, 40, 80);
      gtk_widget_set_hexpand(alt_trim, FALSE);
      gtk_widget_set_valign(alt_trim, GTK_ALIGN_CENTER);
      gtk_grid_attach(GTK_GRID(grid), alt_trim, alt_trim_col, row, 1, 1);
    }

    // Alt checkbox
    if (data.alt_elem) {
      GtkWidget *alt_check = gtk_check_button_new();
      gtk_widget_set_halign(alt_check, GTK_ALIGN_CENTER);
      gtk_widget_set_valign(alt_check, GTK_ALIGN_CENTER);

      g_signal_connect(
        alt_check,
        "toggled",
        G_CALLBACK(enable_checkbox_toggled),
        data.alt_elem
      );

      alsa_elem_add_callback(
        data.alt_elem,
        enable_checkbox_updated,
        alt_check,
        NULL
      );

      int value = alsa_get_elem_value(data.alt_elem);
      gtk_check_button_set_active(GTK_CHECK_BUTTON(alt_check), value != 0);

      gtk_grid_attach(GTK_GRID(grid), alt_check, alt_col, row, 1, 1);

      // Register callback to enable/disable source and trim when alt is toggled
      if (alt_source || alt_trim) {
        struct group_enable_data *enable_data =
          g_malloc(sizeof(struct group_enable_data));
        enable_data->source_widget = alt_source;
        enable_data->trim_widget = alt_trim;

        alsa_elem_add_callback(
          data.alt_elem,
          group_enable_updated,
          enable_data,
          free_group_enable_data
        );

        // Set initial sensitivity based on current state
        if (alt_source)
          gtk_widget_set_sensitive(alt_source, value != 0);
        if (alt_trim)
          gtk_widget_set_sensitive(alt_trim, value != 0);
      }
    }

    row++;
  }

  // Add vertical separators spanning all data rows
  int total_rows = row;

  GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_margin_start(sep1, 10);
  gtk_widget_set_margin_end(sep1, 10);
  gtk_grid_attach(GTK_GRID(grid), sep1, sep1_col, 0, 1, total_rows);

  GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_margin_start(sep2, 10);
  gtk_widget_set_margin_end(sep2, 10);
  gtk_grid_attach(GTK_GRID(grid), sep2, sep2_col, 0, 1, total_rows);

  return grid;
}

GtkWidget *create_configuration_controls(struct alsa_card *card) {
  struct configuration_window *data =
    g_malloc0(sizeof(struct configuration_window));
  data->card = card;

  // create main container
  GtkWidget *top = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
  gtk_widget_add_css_class(top, "window-frame");
  GtkWidget *vbox = top;

  data->top = top;

  // Get the device name element for the Device Name tab
  struct alsa_elem *name_elem = optional_controls_get_name_elem(card);

  // Create top-level notebook for configuration sections
  GtkWidget *top_notebook = gtk_notebook_new();
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(top_notebook), GTK_POS_TOP);
  gtk_widget_add_css_class(top_notebook, "outer-notebook");

  // Device Name tab
  if (name_elem) {
    GtkWidget *name_tab_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(name_tab_content, 20);

    GtkWidget *name_help = gtk_label_new(
      "This name will appear in the window title and can help you\n"
      "identify this device if you have multiple interfaces."
    );
    gtk_widget_set_halign(name_help, GTK_ALIGN_START);
    gtk_widget_add_css_class(name_help, "dim-label");
    gtk_box_append(GTK_BOX(name_tab_content), name_help);

    GtkWidget *name_entry = make_text_entry_alsa_elem(name_elem);
    gtk_widget_set_hexpand(name_entry, TRUE);
    gtk_box_append(GTK_BOX(name_tab_content), name_entry);

    GtkWidget *name_tab_label = gtk_label_new("Device Name");
    g_object_set_data(
      G_OBJECT(name_tab_content), PAGE_ID_KEY, (gpointer)"device-name"
    );
    gtk_notebook_append_page(
      GTK_NOTEBOOK(top_notebook), name_tab_content, name_tab_label
    );
  }

  // Ports tab (contains the sub-notebook with Analogue, S/PDIF, etc.)
  GtkWidget *notebook = gtk_notebook_new();

  // Add tabs for each hardware type (Analogue, S/PDIF, ADAT)
  for (int hw_type = 0; hw_type < HW_TYPE_COUNT; hw_type++) {
    add_hw_tab(notebook, card, hw_type);
  }

  // Add tabs for other categories (PCM, Mixer, DSP)
  add_category_tab(notebook, card, PC_PCM, "PCM", 1, 1);

  // Mixer tab - special handling for inputs (enable-only, no custom names)
  if (has_io_for_category(card, PC_MIX, -1, 1, 1)) {

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

      // DSP Inputs column (if any)
      if (has_mixer_inputs_for_src_type(card, PC_DSP, -1)) {
        struct column_checkbox_data *col = create_mixer_input_column(
          content, card, "DSP Inputs", PC_DSP, -1, tab_data
        );
        g_array_append_val(tab_data->columns, col);
      }

      // Mix Inputs column (if any - mixer feeding back into itself)
      if (has_mixer_inputs_for_src_type(card, PC_MIX, -1)) {
        struct column_checkbox_data *col = create_mixer_input_column(
          content, card, "Mix Inputs", PC_MIX, -1, tab_data
        );
        g_array_append_val(tab_data->columns, col);
      }

      // Outputs column (Mixer Outputs with custom names)
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
        1, 1  // show both inputs and outputs
      );

      // Left column: Mixer Inputs (enable-only, no custom names)
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

  add_category_tab(notebook, card, PC_DSP, "DSP", 1, 1);

  // Ports tab (only if there are any sub-tabs)
  if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)) > 0) {
    GtkWidget *io_tab_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(io_tab_content, 20);

    GtkWidget *io_help = gtk_label_new(
      "Use the checkboxes to hide unused inputs and outputs from the display.\n"
      "You can also give each port a custom name to help identify it."
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

  // Monitor Groups tab
  if (has_main_alt_group_controls(card)) {
    GtkWidget *group_tab_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(group_tab_content, 20);
    gtk_widget_set_margin_end(group_tab_content, 20);
    gtk_widget_set_margin_top(group_tab_content, 20);
    gtk_widget_set_margin_bottom(group_tab_content, 20);

    GtkWidget *group_help = gtk_label_new(
      "Monitor groups let you organise your outputs into Main and Alt sets.\n"
      "Switch between them using the Alt button on your interface or the toggle in the main window.\n"
      "The main volume knob controls the enabled group; use Trim for per-output level calibration.\n\n"
      "Examples:\n"
      "  • Connect two sets of speakers for A/B reference mixing\n"
      "  • Create a surround setup with all speakers in one group\n"
      "  • Quick DAW/Direct monitoring switch by assigning same output to both groups with different sources"
    );
    gtk_widget_set_halign(group_help, GTK_ALIGN_START);
    gtk_widget_add_css_class(group_help, "dim-label");
    gtk_box_append(GTK_BOX(group_tab_content), group_help);

    GtkWidget *group_grid = create_main_alt_group_grid(card);
    gtk_box_append(GTK_BOX(group_tab_content), group_grid);

    GtkWidget *group_scrolled = wrap_tab_content_scrolled(group_tab_content);
    g_object_set_data(
      G_OBJECT(group_scrolled), PAGE_ID_KEY, (gpointer)"monitor-groups"
    );
    GtkWidget *group_tab_label = gtk_label_new("Monitor Groups");
    gtk_notebook_append_page(
      GTK_NOTEBOOK(top_notebook), group_scrolled, group_tab_label
    );
  }

  // Add top notebook to main container (only if it has pages)
  if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(top_notebook)) > 0) {
    gtk_widget_set_vexpand(top_notebook, TRUE);
    gtk_box_append(GTK_BOX(vbox), top_notebook);

    // Restore saved tab and connect signal to save tab changes
    setup_notebook_tab_persistence(
      GTK_NOTEBOOK(top_notebook), card, CONFIG_TAB_KEY
    );
  }

  // cleanup on destroy
  g_object_weak_ref(
    G_OBJECT(data->top),
    (GWeakNotify)on_destroy,
    data
  );

  return data->top;
}
