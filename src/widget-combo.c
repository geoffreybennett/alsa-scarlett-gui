// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "widget-combo.h"

struct combo {
  struct alsa_elem *elem;
  GtkWidget        *combo_box;
};

static void combo_box_changed(GtkWidget *widget, struct combo *data) {
  int value = gtk_combo_box_get_active(GTK_COMBO_BOX(data->combo_box));

  alsa_set_elem_value(data->elem, value);
}

static void combo_box_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct combo *data = private;

  int value = alsa_get_elem_value(elem);
  gtk_combo_box_set_active(GTK_COMBO_BOX(data->combo_box), value);
}

// Center-align text in the combo box
static void combo_box_center_text(GtkComboBoxText *widget) {
  GList *renderers = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(widget));

  // Assuming there's only one renderer, which is the case with GtkComboBoxText
  GtkCellRenderer *renderer = (GtkCellRenderer *)g_list_nth_data(renderers, 0);

  // Set the "xalign" property of the renderer to center-align text
  g_object_set(renderer, "xalign", (gfloat)0.5, NULL);

  // Free the GList when done
  g_list_free(renderers);
}

GtkWidget *make_combo_box_alsa_elem(struct alsa_elem *elem) {
  struct combo *data = g_malloc(sizeof(struct combo));
  data->elem = elem;
  data->combo_box = gtk_combo_box_text_new();

  combo_box_center_text(GTK_COMBO_BOX_TEXT(data->combo_box));

  int count = alsa_get_item_count(elem);

  for (int i = 0; i < count; i++) {
    const char *text = alsa_get_item_name(elem, i);
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(data->combo_box), NULL, text);
  }

  g_signal_connect(
    data->combo_box, "changed", G_CALLBACK(combo_box_changed), data
  );

  alsa_elem_add_callback(elem, combo_box_updated, data);

  combo_box_updated(elem, data);

  return data->combo_box;
}
