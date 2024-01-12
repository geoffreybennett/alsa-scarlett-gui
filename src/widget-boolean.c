// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "widget-boolean.h"

struct boolean {
  struct alsa_elem *elem;
  GtkWidget        *button;
};

static void button_clicked(GtkWidget *widget, struct alsa_elem *elem) {
  int value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  alsa_set_elem_value(elem, value);
}

static void toggle_button_set_text(GtkWidget *button, const char *text) {
  if (!text)
    return;

  if (*text == '*') {
    GtkWidget *icon = gtk_image_new_from_icon_name(text + 1);
    gtk_button_set_child(GTK_BUTTON(button), icon);
  } else {
    gtk_button_set_label(GTK_BUTTON(button), text);
  }
}

static void toggle_button_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct boolean *data = private;

  int is_writable = alsa_get_elem_writable(elem);
  gtk_widget_set_sensitive(data->button, is_writable);

  int value = !!alsa_get_elem_value(elem);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->button), value);

  toggle_button_set_text(data->button, elem->bool_text[value]);
}

GtkWidget *make_boolean_alsa_elem(
  struct alsa_elem *elem,
  const char       *disabled_text,
  const char       *enabled_text
) {
  struct boolean *data = g_malloc(sizeof(struct boolean));
  data->elem = elem;
  data->button = gtk_toggle_button_new();

  g_signal_connect(
    data->button, "clicked", G_CALLBACK(button_clicked), elem
  );
  alsa_elem_add_callback(elem, toggle_button_updated, data);
  elem->bool_text[0] = disabled_text;
  elem->bool_text[1] = enabled_text;

  // find the maximum width and height of both possible labels
  int max_width = 0, max_height = 0;
  for (int i = 0; i < 2; i++) {
    toggle_button_set_text(data->button, elem->bool_text[i]);

    GtkRequisition *size = gtk_requisition_new();
    gtk_widget_get_preferred_size(data->button, size, NULL);

    if (size->width > max_width)
      max_width = size->width;
    if (size->height > max_height)
      max_height = size->height;
  }

  // set the widget minimum size to the maximum label size so that the
  // widget doesn't change size when the label changes
  gtk_widget_set_size_request(data->button, max_width, max_height);

  toggle_button_updated(elem, data);

  return data->button;
}
