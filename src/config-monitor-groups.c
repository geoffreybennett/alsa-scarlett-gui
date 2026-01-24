// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "alsa.h"
#include "custom-names.h"
#include "port-enable.h"
#include "stereo-link.h"
#include "widget-drop-down-two-level.h"
#include "widget-gain.h"
#include "window-configuration.h"

// Entry for tracking registered callbacks so they can be removed on rebuild
struct monitor_group_cb_entry {
  struct alsa_elem *elem;
  void             *data;
};

// Register a callback and track it for later cleanup
static void register_mg_callback(
  struct alsa_card   *card,
  struct alsa_elem   *elem,
  AlsaElemCallback   *callback,
  void               *data,
  GDestroyNotify      destroy
) {
  alsa_elem_add_callback(elem, callback, data, destroy);

  struct monitor_group_cb_entry *entry =
    g_malloc(sizeof(struct monitor_group_cb_entry));
  entry->elem = elem;
  entry->data = data;
  card->monitor_group_cbs = g_list_prepend(
    card->monitor_group_cbs, entry
  );
}

// Remove all tracked callbacks and gain widgets (before grid rebuild)
static void cleanup_monitor_group_grid(struct alsa_card *card) {
  for (GList *l = card->monitor_group_cbs; l; l = l->next) {
    struct monitor_group_cb_entry *entry = l->data;
    alsa_elem_remove_callbacks_by_data(entry->elem, entry->data);
    g_free(entry);
  }
  g_list_free(card->monitor_group_cbs);
  card->monitor_group_cbs = NULL;

  for (GList *l = card->monitor_group_gains; l; l = l->next)
    cleanup_gain_widget(l->data);
  g_list_free(card->monitor_group_gains);
  card->monitor_group_gains = NULL;
}

// Structure to track Main/Alt Group controls for one output
struct group_output_data {
  struct alsa_elem *main_elem;
  struct alsa_elem *alt_elem;
  struct alsa_elem *main_source_elem;
  struct alsa_elem *alt_source_elem;
  struct alsa_elem *main_trim_elem;
  struct alsa_elem *alt_trim_elem;
};

// Structure to track group output label and its sink
struct group_output_label_data {
  GtkLabel           *label;
  struct routing_snk *snk;
};

// Structure to track widgets that should be enabled/disabled based on group
// output state
struct group_enable_data {
  GtkWidget *source_widget;
  GtkWidget *trim_widget;
};

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

// Callback to update source/trim sensitivity when group output enabled state
// changes
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
  char *text = is_snk_linked(data->snk)
    ? get_snk_pair_display_name(data->snk)
    : get_snk_display_name_formatted(data->snk);
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

    // Skip hidden or right-channel-of-linked-pair sinks
    struct routing_snk *snk = get_analogue_output_snk(card, i);
    if (snk && (!should_display_snk(snk) ||
                !is_routing_snk_enabled(snk)))
      continue;

    int linked = snk && is_snk_linked(snk);

    // Row label — use pair name when linked, otherwise single name
    char *label_text;
    if (linked)
      label_text = get_snk_pair_display_name(snk);
    else
      label_text = snk ? get_snk_display_name_formatted(snk) : g_strdup("");

    GtkWidget *label = gtk_label_new(label_text);
    g_free(label_text);
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

    // Register callbacks to update label when name changes
    if (snk) {
      if (snk->custom_name_elem) {
        struct group_output_label_data *label_data =
          g_malloc(sizeof(struct group_output_label_data));
        label_data->label = GTK_LABEL(label);
        label_data->snk = snk;

        register_mg_callback(
          card, snk->custom_name_elem,
          group_output_label_updated,
          label_data, free_group_output_label_data
        );
      }

      // Also update label when pair name changes (for linked pairs)
      struct alsa_elem *pair_elem = get_snk_pair_name_elem(snk);
      if (pair_elem) {
        struct group_output_label_data *pair_label_data =
          g_malloc(sizeof(struct group_output_label_data));
        pair_label_data->label = GTK_LABEL(label);
        pair_label_data->snk = snk;

        register_mg_callback(
          card, pair_elem,
          group_output_label_updated,
          pair_label_data, free_group_output_label_data
        );
      }
    }

    // When linked, get the right channel data for stereo trim
    struct group_output_data data_r = {0};
    if (linked) {
      struct routing_snk *snk_r = get_snk_partner(snk);
      if (snk_r)
        get_group_output_elems(card, snk_r->elem->lr_num, &data_r);
    }

    // Main Source dropdown (create before checkbox so we can track it)
    GtkWidget *main_source = NULL;
    if (data.main_source_elem && main_source_col >= 0) {
      main_source = make_monitor_group_source_dropdown(
        data.main_source_elem, card, linked
      );
      gtk_widget_set_size_request(main_source, 120, -1);
      gtk_widget_set_hexpand(main_source, FALSE);
      gtk_widget_set_valign(main_source, GTK_ALIGN_CENTER);
      gtk_grid_attach(
        GTK_GRID(grid), main_source, main_source_col, row, 1, 1
      );
    }

    // Main Trim (create before checkbox so we can track it)
    GtkWidget *main_trim = NULL;
    if (data.main_trim_elem && main_trim_col >= 0) {
      if (linked && data_r.main_trim_elem) {
        struct alsa_elem *elems[] = {
          data.main_trim_elem, data_r.main_trim_elem
        };
        main_trim = make_stereo_gain_alsa_elem(
          elems, 2, 0, WIDGET_GAIN_TAPER_LINEAR, 1, FALSE
        );
      } else {
        main_trim = make_gain_alsa_elem(
          data.main_trim_elem,
          0, WIDGET_GAIN_TAPER_LINEAR, 1, FALSE
        );
      }
      gtk_widget_set_size_request(main_trim, 40, 80);
      gtk_widget_set_hexpand(main_trim, FALSE);
      gtk_widget_set_valign(main_trim, GTK_ALIGN_CENTER);
      gtk_grid_attach(
        GTK_GRID(grid), main_trim, main_trim_col, row, 1, 1
      );
      card->monitor_group_gains = g_list_prepend(
        card->monitor_group_gains, main_trim
      );
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

      register_mg_callback(
        card, data.main_elem,
        enable_checkbox_updated, main_check, NULL
      );

      int value = alsa_get_elem_value(data.main_elem);
      gtk_check_button_set_active(
        GTK_CHECK_BUTTON(main_check), value != 0
      );

      gtk_grid_attach(
        GTK_GRID(grid), main_check, main_col, row, 1, 1
      );

      // Register callback to enable/disable source and trim
      if (main_source || main_trim) {
        struct group_enable_data *enable_data =
          g_malloc(sizeof(struct group_enable_data));
        enable_data->source_widget = main_source;
        enable_data->trim_widget = main_trim;

        register_mg_callback(
          card, data.main_elem,
          group_enable_updated,
          enable_data, free_group_enable_data
        );

        if (main_source)
          gtk_widget_set_sensitive(main_source, value != 0);
        if (main_trim)
          gtk_widget_set_sensitive(main_trim, value != 0);
      }
    }

    // Alt Source dropdown (create before checkbox so we can track it)
    GtkWidget *alt_source = NULL;
    if (data.alt_source_elem && alt_source_col >= 0) {
      alt_source = make_monitor_group_source_dropdown(
        data.alt_source_elem, card, linked
      );
      gtk_widget_set_size_request(alt_source, 120, -1);
      gtk_widget_set_hexpand(alt_source, FALSE);
      gtk_widget_set_valign(alt_source, GTK_ALIGN_CENTER);
      gtk_grid_attach(
        GTK_GRID(grid), alt_source, alt_source_col, row, 1, 1
      );
    }

    // Alt Trim (create before checkbox so we can track it)
    GtkWidget *alt_trim = NULL;
    if (data.alt_trim_elem && alt_trim_col >= 0) {
      if (linked && data_r.alt_trim_elem) {
        struct alsa_elem *elems[] = {
          data.alt_trim_elem, data_r.alt_trim_elem
        };
        alt_trim = make_stereo_gain_alsa_elem(
          elems, 2, 0, WIDGET_GAIN_TAPER_LINEAR, 1, FALSE
        );
      } else {
        alt_trim = make_gain_alsa_elem(
          data.alt_trim_elem,
          0, WIDGET_GAIN_TAPER_LINEAR, 1, FALSE
        );
      }
      gtk_widget_set_size_request(alt_trim, 40, 80);
      gtk_widget_set_hexpand(alt_trim, FALSE);
      gtk_widget_set_valign(alt_trim, GTK_ALIGN_CENTER);
      gtk_grid_attach(
        GTK_GRID(grid), alt_trim, alt_trim_col, row, 1, 1
      );
      card->monitor_group_gains = g_list_prepend(
        card->monitor_group_gains, alt_trim
      );
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

      register_mg_callback(
        card, data.alt_elem,
        enable_checkbox_updated, alt_check, NULL
      );

      int value = alsa_get_elem_value(data.alt_elem);
      gtk_check_button_set_active(
        GTK_CHECK_BUTTON(alt_check), value != 0
      );

      gtk_grid_attach(
        GTK_GRID(grid), alt_check, alt_col, row, 1, 1
      );

      // Register callback to enable/disable source and trim
      if (alt_source || alt_trim) {
        struct group_enable_data *enable_data =
          g_malloc(sizeof(struct group_enable_data));
        enable_data->source_widget = alt_source;
        enable_data->trim_widget = alt_trim;

        register_mg_callback(
          card, data.alt_elem,
          group_enable_updated,
          enable_data, free_group_enable_data
        );

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

void add_monitor_groups_tab(GtkWidget *notebook, struct alsa_card *card) {
  if (!get_elem_by_prefix(card->elems, "Main Group Output"))
    return;

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(content, 20);
  gtk_widget_set_margin_end(content, 20);
  gtk_widget_set_margin_top(content, 20);
  gtk_widget_set_margin_bottom(content, 20);

  GtkWidget *help = gtk_label_new(
    "Monitor groups let you organise your outputs into Main and "
    "Alt sets.\n"
    "Switch between them using the Alt button on your interface "
    "or the toggle in the main window.\n"
    "The main volume knob controls the enabled group; use Trim "
    "for per-output level calibration.\n\n"
    "Examples:\n"
    "  \xe2\x80\xa2 Connect two sets of speakers for A/B reference "
    "mixing\n"
    "  \xe2\x80\xa2 Create a surround setup with all speakers in one "
    "group\n"
    "  \xe2\x80\xa2 Quick DAW/Direct monitoring switch by assigning "
    "same output to both groups with different sources"
  );
  gtk_widget_set_halign(help, GTK_ALIGN_START);
  gtk_widget_add_css_class(help, "dim-label");
  gtk_box_append(GTK_BOX(content), help);

  GtkWidget *grid = create_main_alt_group_grid(card);
  gtk_box_append(GTK_BOX(content), grid);
  card->monitor_groups_grid = grid;

  GtkWidget *scrolled = wrap_tab_content_scrolled(content);
  g_object_set_data(
    G_OBJECT(scrolled), PAGE_ID_KEY, (gpointer)"monitor-groups"
  );
  gtk_notebook_append_page(
    GTK_NOTEBOOK(notebook), scrolled,
    gtk_label_new("Monitor Groups")
  );
}

void rebuild_monitor_groups_grid(struct alsa_card *card) {
  if (!card || !card->monitor_groups_grid)
    return;

  GtkWidget *parent = gtk_widget_get_parent(card->monitor_groups_grid);
  if (!parent)
    return;

  // Clean up callbacks and gain widgets from old grid
  cleanup_monitor_group_grid(card);

  // Remove old grid from parent
  gtk_box_remove(GTK_BOX(parent), card->monitor_groups_grid);

  // Create new grid with current stereo state
  GtkWidget *grid = create_main_alt_group_grid(card);
  gtk_box_append(GTK_BOX(parent), grid);
  card->monitor_groups_grid = grid;
}
