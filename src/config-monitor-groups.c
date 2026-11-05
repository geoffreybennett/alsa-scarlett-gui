// SPDX-FileCopyrightText: 2022-2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "alsa.h"
#include "config-helpers.h"
#include "custom-names.h"
#include "device-port-names.h"
#include "hardware.h"
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

// Whether this sink is an analogue output with monitor group controls
static int snk_has_monitor_groups(struct routing_snk *snk) {
  return snk->elem->port_category == PC_HW &&
         snk->elem->hw_type == HW_TYPE_ANALOGUE &&
         snk->monitor_groups[0].enable;
}

// Callback when enable checkbox is toggled; user_data is the enable
// elements it sets, two for a tied pair and otherwise one then NULL
static void enable_checkbox_toggled(GtkCheckButton *button, gpointer user_data) {
  struct alsa_elem **elems = user_data;
  gboolean active = gtk_check_button_get_active(button);

  for (int i = 0; i < 2 && elems[i]; i++)
    alsa_set_elem_value(elems[i], active ? 1 : 0);
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

// Whether the device applies one trim to both channels of this sink's
// pair: the ISA C8X does for its headphone outputs
static int snk_trims_tied(struct routing_snk *snk) {
  struct alsa_card *card = snk->elem->card;

  if (card->pid != PID_ISA_C8X)
    return 0;

  const char *name = get_device_port_name(
    card->pid, PC_HW, HW_TYPE_ANALOGUE, 1, snk->elem->lr_num - 1
  );

  return name && strncmp(name, "Headphones ", 11) == 0;
}

static int snk_has_row(struct routing_snk *snk) {
  return should_display_snk(snk) && is_routing_snk_enabled(snk);
}

// Create one trim fader over the trims of one output, or of both
// channels of a tied pair (elem_count 2), tooltipped with the output
// it belongs to
static GtkWidget *create_trim_fader(
  struct alsa_card   *card,
  struct routing_snk *snk,
  struct alsa_elem  **elems,
  int                 elem_count,
  int                 ganged
) {
  GtkWidget *fader = elem_count == 1
    ? make_gain_alsa_elem(elems[0], 0, WIDGET_GAIN_TAPER_LINEAR, 1, FALSE)
    : make_stereo_gain_alsa_elem(
        elems, elem_count, 0, WIDGET_GAIN_TAPER_LINEAR, 1, FALSE
      );
  gtk_widget_set_size_request(fader, 40, 80);
  gtk_widget_set_hexpand(fader, FALSE);
  gtk_widget_set_valign(fader, GTK_ALIGN_CENTER);

  char *name = elem_count == 1
    ? get_snk_display_name_formatted(snk)
    : get_snk_pair_display_name(snk);
  char *tooltip = ganged
    ? g_strdup_printf(
        "%s trim — drag to move the pair together, "
        "Ctrl+drag for this channel only", name
      )
    : g_strdup_printf("%s trim", name);
  gtk_widget_set_tooltip_text(fader, tooltip);
  g_free(tooltip);
  g_free(name);

  card->monitor_group_gains = g_list_prepend(
    card->monitor_group_gains, fader
  );

  return fader;
}

// Make source and trim (either may be NULL) sensitive only while the
// group enable is on
static void track_group_enable(
  struct alsa_card *card,
  struct alsa_elem *enable,
  GtkWidget        *source,
  GtkWidget        *trim
) {
  if (!source && !trim)
    return;

  struct group_enable_data *data = g_malloc(sizeof(*data));
  data->source_widget = source;
  data->trim_widget = trim;

  register_mg_callback(
    card, enable, group_enable_updated, data, free_group_enable_data
  );
  group_enable_updated(enable, data);
}

// Create the widgets for one monitor group of one output row.
// enable_col is the group's first column; source and trim (when
// present) follow it.  snk_r is the partner sink when the output is
// stereo-linked, NULL otherwise.
static void create_group_cells(
  struct alsa_card   *card,
  GtkWidget          *grid,
  int                 row,
  int                 enable_col,
  int                 has_source,
  int                 has_trim,
  struct routing_snk *snk,
  struct routing_snk *snk_r,
  int                 g
) {
  struct monitor_group_elems *elems = &snk->monitor_groups[g];
  struct monitor_group_elems *elems_r =
    snk_r ? &snk_r->monitor_groups[g] : NULL;
  int linked = snk_r != NULL;

  // Source dropdown (create before checkbox so we can track it)
  GtkWidget *source = NULL;
  if (elems->source && has_source) {
    source = make_monitor_group_source_dropdown(
      elems->source, card, linked
    );
    gtk_widget_set_size_request(source, 120, -1);
    gtk_widget_set_hexpand(source, FALSE);
    gtk_widget_set_valign(source, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), source, enable_col + 1, row, 1, 1);
  }

  // Trim (create before checkbox so we can track it).  A linked pair
  // gets one fader per channel: trim calibrates each speaker, so the
  // two channels must be able to differ.  The faders are ganged, so
  // the pair moves together until Ctrl is held.
  //
  // A tied pair has one trim and one enable in the device, so it gets
  // one fader and one checkbox over both channels; unlinked, they span
  // both channels' rows, on the first row showing the pair, and the
  // second row's source follows the shared checkbox.
  struct routing_snk *tied = snk_trims_tied(snk) ? get_snk_partner(snk) : NULL;
  struct monitor_group_elems *tied_elems =
    tied ? &tied->monitor_groups[g] : NULL;
  struct alsa_elem *tied_trims[2] = {
    elems->trim, tied_elems ? tied_elems->trim : NULL
  };
  int shared_rows = tied && !snk_r && snk_has_row(tied);
  int follower = shared_rows && !is_snk_left_channel(snk);
  int tied_rows = shared_rows && !follower ? 2 : 1;

  if (follower) {
    if (tied_elems->enable)
      track_group_enable(card, tied_elems->enable, source, NULL);
    return;
  }

  GtkWidget *trim = NULL;
  if (elems->trim && has_trim && tied_trims[1]) {
    trim = create_trim_fader(card, snk, tied_trims, 2, FALSE);
    gtk_grid_attach(
      GTK_GRID(grid), trim, enable_col + 1 + has_source, row, 1, tied_rows
    );
  } else if (elems->trim && has_trim) {
    int ganged = elems_r && elems_r->trim;

    trim = create_trim_fader(card, snk, &elems->trim, 1, ganged);

    if (ganged) {
      GtkWidget *trim_r = create_trim_fader(
        card, snk_r, &elems_r->trim, 1, ganged
      );
      gang_gain_widgets(trim, trim_r);

      GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
      gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
      gtk_box_append(GTK_BOX(box), trim);
      gtk_box_append(GTK_BOX(box), trim_r);
      trim = box;
    }

    gtk_grid_attach(
      GTK_GRID(grid), trim, enable_col + 1 + has_source, row, 1, 1
    );
  }

  if (!elems->enable)
    return;

  GtkWidget *check = gtk_check_button_new();
  gtk_widget_set_halign(check, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(check, GTK_ALIGN_CENTER);

  struct alsa_elem **enables = g_new0(struct alsa_elem *, 2);
  enables[0] = elems->enable;
  enables[1] = tied_elems ? tied_elems->enable : NULL;

  g_signal_connect_data(
    check, "toggled", G_CALLBACK(enable_checkbox_toggled), enables,
    (GClosureNotify)g_free, 0
  );

  register_mg_callback(
    card, elems->enable, enable_checkbox_updated, check, NULL
  );

  int value = alsa_get_elem_value(elems->enable);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(check), value != 0);

  gtk_grid_attach(GTK_GRID(grid), check, enable_col, row, 1, tied_rows);

  track_group_enable(card, elems->enable, source, trim);
}

// Create the monitor group grid: one column set per group
static GtkWidget *create_main_alt_group_grid(struct alsa_card *card) {
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  gtk_widget_set_margin_top(grid, 10);
  gtk_widget_set_valign(grid, GTK_ALIGN_START);
  gtk_grid_set_row_homogeneous(GTK_GRID(grid), FALSE);

  int group_count = card->monitor_group_count;

  // Check if source/trim controls exist (check first grouped output)
  int has_source = 0;
  int has_trim = 0;
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (!snk_has_monitor_groups(snk))
      continue;

    has_source = snk->monitor_groups[0].source != NULL;
    has_trim = snk->monitor_groups[0].trim != NULL;
    break;
  }

  // Column layout: 0 is the output label, then per group a separator
  // followed by Enable [Source] [Trim]
  int group_width = 1 + has_source + has_trim;

  for (int g = 0; g < group_count; g++) {
    int enable_col = 2 + g * (group_width + 1);

    // Row 0: group header
    GtkWidget *header = gtk_label_new(NULL);
    char *markup = g_markup_printf_escaped(
      "<b>%s</b>", card->monitor_group_names[g]
    );
    gtk_label_set_markup(GTK_LABEL(header), markup);
    g_free(markup);
    gtk_widget_set_halign(header, GTK_ALIGN_CENTER);
    gtk_grid_attach(
      GTK_GRID(grid), header, enable_col, 0, group_width, 1
    );

    // Row 1: sub-headings
    GtkWidget *enable_label = gtk_label_new("Enable");
    gtk_widget_add_css_class(enable_label, "dim-label");
    gtk_grid_attach(GTK_GRID(grid), enable_label, enable_col, 1, 1, 1);

    if (has_source) {
      GtkWidget *source_label = gtk_label_new("Source");
      gtk_widget_add_css_class(source_label, "dim-label");
      gtk_grid_attach(
        GTK_GRID(grid), source_label, enable_col + 1, 1, 1, 1
      );
    }

    if (has_trim) {
      GtkWidget *trim_label = gtk_label_new("Trim");
      gtk_widget_add_css_class(trim_label, "dim-label");
      gtk_grid_attach(
        GTK_GRID(grid), trim_label, enable_col + 1 + has_source, 1, 1, 1
      );
    }
  }

  // Add rows for each grouped output (starting after 2 header rows)
  int row = 2;
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (!snk_has_monitor_groups(snk))
      continue;

    // Skip hidden or right-channel-of-linked-pair sinks
    if (!should_display_snk(snk) || !is_routing_snk_enabled(snk))
      continue;

    int linked = is_snk_linked(snk);

    // Row label — use pair name when linked, otherwise single name
    char *label_text = linked
      ? get_snk_pair_display_name(snk)
      : get_snk_display_name_formatted(snk);

    GtkWidget *label = gtk_label_new(label_text);
    g_free(label_text);
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

    // Register callbacks to update label when name changes
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

    // When linked, the partner provides the right channel elements
    struct routing_snk *snk_r = linked ? get_snk_partner(snk) : NULL;

    for (int g = 0; g < group_count; g++)
      create_group_cells(
        card, grid, row, 2 + g * (group_width + 1),
        has_source, has_trim, snk, snk_r, g
      );

    row++;
  }

  // Add vertical separators spanning all data rows
  for (int g = 0; g < group_count; g++) {
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_margin_start(sep, 10);
    gtk_widget_set_margin_end(sep, 10);
    gtk_grid_attach(
      GTK_GRID(grid), sep, 1 + g * (group_width + 1), 0, 1, row
    );
  }

  return grid;
}

void add_monitor_groups_tab(GtkWidget *notebook, struct alsa_card *card) {
  if (!get_elem_by_prefix(card->elems, "Main Group Output"))
    return;

  // The group grid is built by walking the sinks, which a device
  // without a routing matrix does not have
  if (!card->routing_snks)
    return;

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(content, 20);
  gtk_widget_set_margin_end(content, 20);
  gtk_widget_set_margin_top(content, 20);
  gtk_widget_set_margin_bottom(content, 20);

  // e.g. "Main and Alt" or "Main, Alt 1, and Alt 2"
  GString *group_list = g_string_new(NULL);
  for (int g = 0; g < card->monitor_group_count; g++) {
    if (g)
      g_string_append(
        group_list,
        g < card->monitor_group_count - 1 ? ", "
          : card->monitor_group_count > 2 ? ", and "
          : " and "
      );
    g_string_append(group_list, card->monitor_group_names[g]);
  }

  const char *switch_text = card->monitor_group_count > 2
    ? "Switch between them using the Main/Alt buttons on your "
      "interface or the Monitor Group selector in the main window.\n"
    : "Switch between them using the Alt button on your interface "
      "or the toggle in the main window.\n";

  char *help_text = g_strdup_printf(
    "Monitor groups let you organise your outputs into %s sets.\n"
    "%s"
    "The main volume knob controls the enabled group; use Trim "
    "for per-output level calibration.\n\n"
    "Examples:\n"
    "  \xe2\x80\xa2 Connect two sets of speakers for A/B reference "
    "mixing\n"
    "  \xe2\x80\xa2 Create a surround setup with all speakers in one "
    "group\n"
    "  \xe2\x80\xa2 Quick DAW/Direct monitoring switch by assigning "
    "same output to multiple groups with different sources",
    group_list->str, switch_text
  );
  g_string_free(group_list, TRUE);

  gtk_box_append(GTK_BOX(content), config_help_label(help_text));
  g_free(help_text);

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
