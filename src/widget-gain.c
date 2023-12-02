// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkdial.h"
#include "widget-gain.h"

struct gain {
  struct alsa_elem *elem;
  GtkWidget        *vbox;
  GtkWidget        *dial;
  GtkWidget        *label;
};

static void gain_changed(GtkWidget *widget, struct gain *data) {
  int value = gtk_dial_get_value(GTK_DIAL(data->dial));

  alsa_set_elem_value(data->elem, value);
}

static void gain_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct gain *data = private;

  int is_writable = alsa_get_elem_writable(elem);
  gtk_widget_set_sensitive(data->dial, is_writable);

  int alsa_value = alsa_get_elem_value(elem);
  gtk_dial_set_value(GTK_DIAL(data->dial), alsa_value);

  char s[20];
  float scale = (float)(elem->max_dB - elem->min_dB) /
                       (elem->max_val - elem->min_val);

  float value = (float)alsa_value * scale + elem->min_dB;

  if (scale < 1)
    snprintf(s, 20, "%.1f", value);
  else
    snprintf(s, 20, "%.0fdB", value);

  gtk_label_set_text(GTK_LABEL(data->label), s);
}

//GList *make_gain_alsa_elem(struct alsa_elem *elem) {
GtkWidget *make_gain_alsa_elem(struct alsa_elem *elem) {
  struct gain *data = g_malloc(sizeof(struct gain));
  data->elem = elem;
  data->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(data->vbox, TRUE);

  data->dial = gtk_dial_new_with_range(elem->min_val, elem->max_val, 1);

  // calculate 0dB value from min/max dB and min/max value
  float scale = (float)(elem->max_dB - elem->min_dB) /
                       (elem->max_val - elem->min_val);
  int zero_db_value = (int)((0 - elem->min_dB) / scale + elem->min_val);

  gtk_dial_set_zero_db(GTK_DIAL(data->dial), zero_db_value);

  data->label = gtk_label_new(NULL);
  gtk_widget_set_vexpand(data->dial, TRUE);

  g_signal_connect(
    data->dial, "value-changed", G_CALLBACK(gain_changed), data
  );

  alsa_elem_add_callback(elem, gain_updated, data);

  gain_updated(elem, data);

  gtk_box_append(GTK_BOX(data->vbox), data->dial);
  gtk_box_append(GTK_BOX(data->vbox), data->label);

  return data->vbox;
}
