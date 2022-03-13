// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkdial.h"
#include "widget-gain.h"

// gain controls -80dB - +6dB, 0.5dB steps
#define DIAL_MIN_VALUE 0
#define DIAL_MAX_VALUE 172
#define DIAL_ZERO_DB_VALUE 160

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
  snprintf(s, 20, "%.1f", (value / 2.0) - 80);
  gtk_label_set_text(GTK_LABEL(elem->widget2), s);
}

GtkWidget *make_gain_alsa_elem(struct alsa_elem *elem) {
  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(vbox, TRUE);

  GtkWidget *dial = gtk_dial_new_with_range(
    DIAL_MIN_VALUE, DIAL_MAX_VALUE, 1
  );
  gtk_dial_set_zero_db(GTK_DIAL(dial), DIAL_ZERO_DB_VALUE);

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
