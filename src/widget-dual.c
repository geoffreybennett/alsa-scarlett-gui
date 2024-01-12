// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "widget-dual.h"

struct dual_button {
  struct alsa_elem *elem;
  GtkWidget        *button1;
  GtkWidget        *button2;
};

static void dual_button_clicked(GtkWidget *widget, struct dual_button *data) {
  int value1 = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(data->button1));
  int value2 = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(data->button2));

  int value = value1 ? value2 + 1 : 0;

  alsa_set_elem_value(data->elem, value);

  gtk_widget_set_sensitive(data->button2, value1);
}

static void dual_button_updated(
  struct alsa_elem   *elem,
  void               *private
) {
  struct dual_button *data = private;

  // value (from ALSA control) is 0/1/2
  // value1 (first button) is 0/1/1
  // value2 (second button) is X/0/1
  int value = alsa_get_elem_value(elem);
  int value1 = !!value;

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->button1), value1);
  gtk_button_set_label(GTK_BUTTON(data->button1), elem->bool_text[value1]);
  gtk_widget_set_sensitive(data->button2, value1);
  if (value1) {
    int value2 = value - 1;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->button2), value2);
    gtk_button_set_label(
      GTK_BUTTON(data->button2), elem->bool_text[value2 + 2]
    );
  }
}

// speaker switch and talkback have three states, controlled by two
// buttons:
// first button disables/enables the feature
// second button switches between the two enabled states
GtkWidget *make_dual_boolean_alsa_elems(
  struct alsa_elem *elem,
  const char       *label_text,
  const char       *disabled_text_1,
  const char       *enabled_text_1,
  const char       *disabled_text_2,
  const char       *enabled_text_2
) {
  struct dual_button *data = g_malloc(sizeof(struct dual_button));
  data->elem = elem;
  data->button1 = gtk_toggle_button_new();
  data->button2 = gtk_toggle_button_new();

  g_signal_connect(
    data->button1, "clicked", G_CALLBACK(dual_button_clicked), data
  );
  g_signal_connect(
    data->button2, "clicked", G_CALLBACK(dual_button_clicked), data
  );
  alsa_elem_add_callback(elem, dual_button_updated, data);
  elem->bool_text[0] = disabled_text_1;
  elem->bool_text[1] = enabled_text_1;
  elem->bool_text[2] = disabled_text_2;
  elem->bool_text[3] = enabled_text_2;

  gtk_button_set_label(GTK_BUTTON(data->button2), disabled_text_2);

  dual_button_updated(elem, data);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  GtkWidget *label = gtk_label_new(label_text);
  gtk_box_append(GTK_BOX(box), label);
  gtk_box_append(GTK_BOX(box), GTK_WIDGET(data->button1));
  gtk_box_append(GTK_BOX(box), GTK_WIDGET(data->button2));

  return box;
}
