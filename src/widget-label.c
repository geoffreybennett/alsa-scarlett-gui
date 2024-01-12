// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "widget-label.h"

struct label {
  struct alsa_elem *elem;
  GtkWidget        *label;
};

static void label_updated(struct alsa_elem *elem, void *private) {
  struct label *data = private;

  const char *text = alsa_get_item_name(elem, alsa_get_elem_value(elem));

  gtk_label_set_text(GTK_LABEL(data->label), text);
}

GtkWidget *make_label_alsa_elem(struct alsa_elem *elem) {
  struct label *data = g_malloc(sizeof(struct label));
  data->label = gtk_label_new(NULL);

  gtk_widget_set_halign(data->label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(data->label, GTK_ALIGN_CENTER);

  alsa_elem_add_callback(elem, label_updated, data);

  label_updated(elem, data);

  return data->label;
}
