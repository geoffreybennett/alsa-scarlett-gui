// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "alsa.h"
#include "gtkhelper.h"
#include "optional-controls.h"
#include "custom-names.h"
#include "widget-text-entry.h"
#include "window-configuration.h"

struct configuration_window {
  struct alsa_card *card;
  GtkWidget        *top;
};

static void on_destroy(
  struct configuration_window *data,
  GtkWidget                   *widget
) {
  g_free(data);
}

// Create a grid for name entries
static GtkWidget *create_name_grid(void) {
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  return grid;
}

// Get a clean label for a routing sink
// Returns newly allocated string that must be freed
static char *get_clean_snk_label(struct routing_snk *snk) {
  struct alsa_elem *elem = snk->elem;

  switch (elem->port_category) {
    case PC_HW:
      return g_strdup_printf("%s %d", hw_type_names[elem->hw_type], elem->lr_num);

    case PC_PCM:
      return g_strdup_printf("PCM %d", elem->lr_num);

    case PC_MIX:
      return g_strdup_printf("Mixer %d", elem->lr_num);

    case PC_DSP:
      return g_strdup_printf("DSP %d", elem->lr_num);

    default:
      return g_strdup(elem->name ? elem->name : "");
  }
}

// Add a custom name entry to a grid
static void add_name_entry_to_grid(
  GtkWidget        *grid,
  int               row,
  const char       *default_name,
  struct alsa_elem *custom_name_elem
) {
  // Label showing the default name
  GtkWidget *label = gtk_label_new(default_name);
  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

  // Text entry for custom name
  GtkWidget *entry = make_text_entry_alsa_elem(custom_name_elem);
  gtk_widget_set_hexpand(entry, TRUE);
  gtk_grid_attach(GTK_GRID(grid), entry, 1, row, 1, 1);
}

// Add custom name entries for routing sources of a specific category and hw_type
static void add_src_names_for_category(
  struct alsa_card *card,
  GtkWidget        *grid,
  int               port_category,
  int               hw_type  // only used for PC_HW, -1 for others
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

    add_name_entry_to_grid(
      grid,
      row++,
      src->name,
      src->custom_name_elem
    );
  }
}

// Add custom name entries for routing sinks of a specific category and hw_type
static void add_snk_names_for_category(
  struct alsa_card *card,
  GtkWidget        *grid,
  int               port_category,
  int               hw_type  // only used for PC_HW, -1 for others
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

    char *clean_label = get_clean_snk_label(snk);
    add_name_entry_to_grid(
      grid,
      row++,
      clean_label,
      snk->custom_name_elem
    );
    g_free(clean_label);
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

// Create a two-column layout for inputs and outputs
// Returns the box containing both columns
static GtkWidget *create_two_column_layout(
  GtkWidget **left_grid,   // returns the left grid (Inputs), can be NULL if show_inputs=0
  GtkWidget **right_grid,  // returns the right grid (Outputs), can be NULL if show_outputs=0
  int         show_inputs,
  int         show_outputs
) {
  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 30);
  gtk_widget_set_margin_start(hbox, 20);
  gtk_widget_set_margin_end(hbox, 20);
  gtk_widget_set_margin_top(hbox, 20);
  gtk_widget_set_margin_bottom(hbox, 20);

  // Left column (Inputs)
  if (show_inputs) {
    GtkWidget *left_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *left_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(left_label), "<b>Inputs</b>");
    gtk_widget_set_halign(left_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(left_vbox), left_label);

    *left_grid = create_name_grid();
    gtk_box_append(GTK_BOX(left_vbox), *left_grid);
    gtk_box_append(GTK_BOX(hbox), left_vbox);
  } else if (left_grid) {
    *left_grid = NULL;
  }

  // Right column (Outputs)
  if (show_outputs) {
    GtkWidget *right_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *right_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(right_label), "<b>Outputs</b>");
    gtk_widget_set_halign(right_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(right_vbox), right_label);

    *right_grid = create_name_grid();
    gtk_box_append(GTK_BOX(right_vbox), *right_grid);
    gtk_box_append(GTK_BOX(hbox), right_vbox);
  } else if (right_grid) {
    *right_grid = NULL;
  }

  return hbox;
}

// Add a hardware tab (Analogue, S/PDIF, or ADAT)
static void add_hw_tab(
  GtkWidget        *notebook,
  struct alsa_card *card,
  int               hw_type
) {
  if (!has_io_for_category(card, PC_HW, hw_type, 1, 1))
    return;

  GtkWidget *left_grid, *right_grid;
  GtkWidget *content = create_two_column_layout(&left_grid, &right_grid, 1, 1);

  // Left column: Inputs (sources - audio from hardware)
  add_src_names_for_category(card, left_grid, PC_HW, hw_type);

  // Right column: Outputs (sinks - audio to hardware)
  add_snk_names_for_category(card, right_grid, PC_HW, hw_type);

  GtkWidget *label = gtk_label_new(hw_type_names[hw_type]);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), content, label);
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
  GtkWidget *content = create_two_column_layout(
    &left_grid, &right_grid, show_inputs, show_outputs
  );

  // Left column: Inputs (sinks - audio into the subsystem)
  if (show_inputs && left_grid)
    add_snk_names_for_category(card, left_grid, port_category, -1);

  // Right column: Outputs (sources - audio from the subsystem)
  if (show_outputs && right_grid)
    add_src_names_for_category(card, right_grid, port_category, -1);

  GtkWidget *label = gtk_label_new(tab_name);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), content, label);
}

GtkWidget *create_configuration_controls(struct alsa_card *card) {
  struct configuration_window *data =
    g_malloc0(sizeof(struct configuration_window));
  data->card = card;

  // create top-level frame with CSS styling
  GtkWidget *top = gtk_frame_new(NULL);
  gtk_widget_add_css_class(top, "window-frame");

  // create main container
  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
  gtk_widget_add_css_class(vbox, "window-content");
  gtk_frame_set_child(GTK_FRAME(top), vbox);

  data->top = top;

  // Device Name section
  struct alsa_elem *name_elem = optional_controls_get_name_elem(card);
  if (name_elem) {
    GtkWidget *name_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    GtkWidget *name_label = gtk_label_new(NULL);
    gtk_label_set_markup(
      GTK_LABEL(name_label),
      "<b>Device Name</b>"
    );
    gtk_widget_set_halign(name_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(name_section), name_label);

    GtkWidget *name_entry = make_text_entry_alsa_elem(name_elem);
    gtk_widget_set_hexpand(name_entry, TRUE);
    gtk_box_append(GTK_BOX(name_section), name_entry);

    GtkWidget *name_help = gtk_label_new(
      "This name will appear in the window title and can help you\n"
      "identify this device if you have multiple interfaces."
    );
    gtk_widget_set_halign(name_help, GTK_ALIGN_START);
    gtk_widget_add_css_class(name_help, "dim-label");
    gtk_box_append(GTK_BOX(name_section), name_help);

    gtk_box_append(GTK_BOX(vbox), name_section);
  }

  // I/O Names section with tabs
  GtkWidget *notebook = gtk_notebook_new();

  // Add tabs for each hardware type (Analogue, S/PDIF, ADAT)
  for (int hw_type = 0; hw_type < HW_TYPE_COUNT; hw_type++) {
    add_hw_tab(notebook, card, hw_type);
  }

  // Add tabs for other categories (PCM, Mixer, DSP)
  add_category_tab(notebook, card, PC_PCM, "PCM", 1, 1);
  add_category_tab(
    notebook, card, PC_MIX, "Mixer",
    !card->has_fixed_mixer_inputs,  // hide inputs if fixed
    1                                // always show outputs
  );
  add_category_tab(notebook, card, PC_DSP, "DSP", 1, 1);

  // Only add the notebook if there are any tabs
  if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)) > 0) {
    GtkWidget *io_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    GtkWidget *io_label = gtk_label_new(NULL);
    gtk_label_set_markup(
      GTK_LABEL(io_label),
      "<b>I/O Names</b>"
    );
    gtk_widget_set_halign(io_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(io_section), io_label);

    GtkWidget *io_help = gtk_label_new(
      "Give custom names to your inputs and outputs to help identify them.\n"
      "These names will appear throughout the application."
    );
    gtk_widget_set_halign(io_help, GTK_ALIGN_START);
    gtk_widget_add_css_class(io_help, "dim-label");
    gtk_box_append(GTK_BOX(io_section), io_help);

    gtk_box_append(GTK_BOX(io_section), notebook);
    gtk_box_append(GTK_BOX(vbox), io_section);
  }

  // cleanup on destroy
  g_object_weak_ref(
    G_OBJECT(data->top),
    (GWeakNotify)on_destroy,
    data
  );

  return data->top;
}
