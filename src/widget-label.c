// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "widget-label.h"

static void label_updated(struct alsa_elem *elem) {
  const char *text = alsa_get_item_name(elem, alsa_get_elem_value(elem));

  gtk_label_set_text(GTK_LABEL(elem->widget), text);
}

GtkWidget *make_label_alsa_elem(struct alsa_elem *elem) {
  GtkWidget *label = gtk_label_new(NULL);
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);

  elem->widget = label;
  elem->widget_callback = label_updated;

  label_updated(elem);

  return label;
}
