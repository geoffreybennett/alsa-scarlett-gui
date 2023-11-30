// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkdial.h"
#include "widget-gain.h"

static void gain_changed(GtkWidget *widget, struct alsa_elem *elem) {
  int value = gtk_dial_get_value(GTK_DIAL(widget));

  alsa_set_elem_value(elem, value);
}

static void gain_updated(struct alsa_elem *elem) {
  int is_writable = alsa_get_elem_writable(elem);
  gtk_widget_set_sensitive(elem->widget, is_writable);

  int value = alsa_get_elem_value(elem);
  gtk_dial_set_value(GTK_DIAL(elem->widget), value);

  char s[20];
  float scale = (float)(elem->max_dB - elem->min_dB) /
                       (elem->max_val - elem->min_val);

  snprintf(s, 20, "%.1f", value * scale + elem->min_dB);
  gtk_label_set_text(GTK_LABEL(elem->widget2), s);
}

GtkWidget *make_gain_alsa_elem(struct alsa_elem *elem) {
  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(vbox, TRUE);

  GtkWidget *dial = gtk_dial_new_with_range(
    elem->min_val, elem->max_val, 1
  );

  // calculate 0dB value from min/max dB and min/max value
  float scale = (float)(elem->max_dB - elem->min_dB) /
                       (elem->max_val - elem->min_val);
  int zero_db_value = (int)((0 - elem->min_dB) / scale + elem->min_val);

  gtk_dial_set_zero_db(GTK_DIAL(dial), zero_db_value);

  gtk_widget_set_vexpand(dial, TRUE);

  g_signal_connect(
    dial, "value-changed", G_CALLBACK(gain_changed), elem
  );
  elem->widget = dial;
  elem->widget_callback = gain_updated;

  GtkWidget *label = gtk_label_new(NULL);
  elem->widget2 = label;

  gain_updated(elem);

  gtk_box_append(GTK_BOX(vbox), dial);
  gtk_box_append(GTK_BOX(vbox), label);

  return vbox;
}
