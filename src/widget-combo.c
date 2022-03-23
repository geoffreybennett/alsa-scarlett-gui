// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "widget-combo.h"

#include <libintl.h>
#define _(String) gettext (String)

static void combo_box_changed(GtkWidget *widget, struct alsa_elem *elem) {
  int value = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));

  alsa_set_elem_value(elem, value);
}

static void combo_box_updated(struct alsa_elem *elem) {
  int value = alsa_get_elem_value(elem);
  gtk_combo_box_set_active(GTK_COMBO_BOX(elem->widget), value);
}

GtkWidget *make_combo_box_alsa_elem(struct alsa_elem *elem) {
  GtkWidget *combo_box = gtk_combo_box_text_new();
  int count = alsa_get_item_count(elem);

  for (int i = 0; i < count; i++) {
    const char *text = _(alsa_get_item_name(elem, i));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box), NULL, text);
  }

  g_signal_connect(
    combo_box, "changed", G_CALLBACK(combo_box_changed), elem
  );
  elem->widget = combo_box;
  elem->widget_callback = combo_box_updated;

  combo_box_updated(elem);

  return combo_box;
}
