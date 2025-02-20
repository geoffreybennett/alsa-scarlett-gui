// SPDX-FileCopyrightText: 2023-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stringhelper.h"
#include "widget-input-select.h"

struct input_select {
  struct alsa_elem *elem;
  GtkWidget        *button;
  int               line_num;
};

static void input_select_clicked(
  GtkWidget           *widget,
  struct input_select *data
) {
  int count = alsa_get_item_count(data->elem);

  // select the item that matches the line number that was clicked on
  for (int i = 0; i < count; i++) {
    const char *text = alsa_get_item_name(data->elem, i);
    int a, b;
    get_two_num_from_string(text, &a, &b);

    if ((b == -1 && a == data->line_num) ||
        (a <= data->line_num && b >= data->line_num)) {
      alsa_set_elem_value(data->elem, i);
      break;
    }
  }
}

static void input_select_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct input_select *data = private;
  int line_num = data->line_num;
  int is_writable = alsa_get_elem_writable(elem);

  int value = alsa_get_elem_value(elem);
  const char *text = alsa_get_item_name(elem, value);

  int a, b;
  get_two_num_from_string(text, &a, &b);

  // set the button active if it's the selected line number
  // (or in the range)
  int active = b == -1
                 ? a == line_num
                 : a <= line_num && b >= line_num;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->button), active);
  gtk_widget_set_sensitive(data->button, !active && is_writable);
}

GtkWidget *make_input_select_alsa_elem(
  struct alsa_elem *elem,
  int               line_num
) {
  struct input_select *data = malloc(sizeof(struct input_select));
  data->elem = elem;
  data->button = gtk_toggle_button_new();
  data->line_num = line_num;

  gtk_widget_add_css_class(data->button, "input-select");

  char s[20];
  snprintf(s, 20, "%d", line_num);
  gtk_button_set_label(GTK_BUTTON(data->button), s);

  g_signal_connect(
    data->button, "clicked", G_CALLBACK(input_select_clicked), data
  );
  alsa_elem_add_callback(elem, input_select_updated, data);

  input_select_updated(elem, data);

  return data->button;
}
