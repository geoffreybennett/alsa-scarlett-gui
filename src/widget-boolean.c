// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "widget-boolean.h"

static void button_clicked(GtkWidget *widget, struct alsa_elem *elem) {
  int value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  alsa_set_elem_value(elem, value);
}


static void toggle_button_set_text(struct alsa_elem *elem, const char *text)
{
  if (text) {
    if (*text == '*') {
      GtkWidget *icon = gtk_image_new_from_icon_name(text + 1);
      gtk_button_set_child(GTK_BUTTON(elem->widget), icon);
    } else {
      gtk_button_set_label(
        GTK_BUTTON(elem->widget), text
      );
    }
  }
}

static void toggle_button_updated(struct alsa_elem *elem) {
  int is_writable = alsa_get_elem_writable(elem);
  gtk_widget_set_sensitive(elem->widget, is_writable);

  int value = alsa_get_elem_value(elem);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(elem->widget), value);

  toggle_button_set_text(elem, elem->bool_text[value]);  
}

GtkWidget *make_boolean_alsa_elem(
  struct alsa_elem *elem,
  const char       *disabled_text,
  const char       *enabled_text
) {
  GtkWidget *button = gtk_toggle_button_new();

  g_signal_connect(
    button, "clicked", G_CALLBACK(button_clicked), elem
  );
  elem->widget = button;
  elem->widget_callback = toggle_button_updated;
  elem->bool_text[0] = disabled_text;
  elem->bool_text[1] = enabled_text;
  
  // get sizes of both possible labels
  int min_width = 0, min_height = 0;
  for (int i = 0; i < 2; i++)
  {
    toggle_button_set_text(elem, elem->bool_text[i]);
    GtkRequisition* min_size = gtk_requisition_new();
    gtk_widget_get_preferred_size(button, min_size, NULL);
    if (min_size->width  > min_width)  min_width  = min_size->width;
    if (min_size->height > min_height) min_height = min_size->height;
  }
  gtk_widget_set_size_request(button, min_width, min_height);
  
  toggle_button_updated(elem);

  return button;
}
