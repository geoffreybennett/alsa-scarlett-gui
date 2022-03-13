// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "widget-dual.h"

static void dual_button_clicked(GtkWidget *widget, struct alsa_elem *elem) {
  int value1 = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(elem->widget));
  int value2 = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(elem->widget2));

  int value = value1 ? value2 + 1 : 0;

  alsa_set_elem_value(elem, value);

  gtk_widget_set_sensitive(elem->widget2, value1);
}

static void dual_button_updated(struct alsa_elem *elem) {

  // value (from ALSA control) is 0/1/2
  // value1 (first button) is 0/1/1
  // value2 (second button) is X/0/1
  int value = alsa_get_elem_value(elem);
  int value1 = !!value;

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(elem->widget), value1);
  gtk_button_set_label(GTK_BUTTON(elem->widget), elem->bool_text[value1]);
  gtk_widget_set_sensitive(elem->widget2, value1);
  if (value1) {
    int value2 = value - 1;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(elem->widget2), value2);
    gtk_button_set_label(
      GTK_BUTTON(elem->widget2), elem->bool_text[value2 + 2]
    );
  }
}

// speaker switch and talkback have three states, controlled by two
// buttons:
// first button disables/enables the feature
// second button switches between the two enabled states
void make_dual_boolean_alsa_elems(
  struct alsa_elem *elem,
  const char       *disabled_text_1,
  const char       *enabled_text_1,
  const char       *disabled_text_2,
  const char       *enabled_text_2
) {
  GtkWidget *button1 = gtk_toggle_button_new();
  GtkWidget *button2 = gtk_toggle_button_new();

  g_signal_connect(
    button1, "clicked", G_CALLBACK(dual_button_clicked), elem
  );
  g_signal_connect(
    button2, "clicked", G_CALLBACK(dual_button_clicked), elem
  );
  elem->widget = button1;
  elem->widget2 = button2;
  elem->widget_callback = dual_button_updated;
  elem->bool_text[0] = disabled_text_1;
  elem->bool_text[1] = enabled_text_1;
  elem->bool_text[2] = disabled_text_2;
  elem->bool_text[3] = enabled_text_2;

  gtk_button_set_label(GTK_BUTTON(elem->widget2), disabled_text_2);

  dual_button_updated(elem);
}
