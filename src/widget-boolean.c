// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkhelper.h"
#include "widget-boolean.h"

struct boolean {
  struct alsa_elem *elem;
  int               backwards;
  GtkWidget        *button;
  guint             source;
  const char       *text[2];
  GtkWidget        *icons[2];
};

static void button_clicked(GtkWidget *widget, struct boolean *data) {
  int value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  alsa_set_elem_value(data->elem, value ^ data->backwards);
}

static void toggle_button_set_text(struct boolean *data, int value) {
  const char *text = data->text[value];

  if (!text)
    return;

  if (*text == '*')
    gtk_button_set_child(GTK_BUTTON(data->button), data->icons[value]);
  else
    gtk_button_set_label(GTK_BUTTON(data->button), text);
}

static void toggle_button_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct boolean *data = private;

  int is_writable = alsa_get_elem_writable(elem);
  gtk_widget_set_sensitive(data->button, is_writable);

  int value = !!alsa_get_elem_value(elem) ^ data->backwards;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->button), value);

  toggle_button_set_text(data, value);
}

static gboolean update_toggle_button(struct boolean *data) {
  toggle_button_updated(data->elem, data);

  return G_SOURCE_CONTINUE;
}

static void on_destroy(struct boolean *data) {
  if (data->source)
    g_source_remove(data->source);

  for (int i = 0; i < 2; i++)
    if (data->icons[i])
      g_object_unref(data->icons[i]);

  g_free(data);
}

static void load_icons(struct boolean *data) {
  for (int i = 0; i < 2; i++)
    if (data->text[i] && *data->text[i] == '*') {
      char *path = g_strdup_printf(
        "/vu/b4/alsa-scarlett-gui/icons/%s.svg", data->text[i] + 1
      );
      data->icons[i] = gtk_image_new_from_resource(path);
      gtk_widget_set_align(data->icons[i], GTK_ALIGN_CENTER, GTK_ALIGN_CENTER);
      g_object_ref(data->icons[i]);
      g_free(path);
    }
}

GtkWidget *make_boolean_alsa_elem(
  struct alsa_elem *elem,
  const char       *disabled_text,
  const char       *enabled_text
) {
  struct boolean *data = g_malloc0(sizeof(struct boolean));
  data->elem = elem;
  data->button = gtk_toggle_button_new();

  if (strncmp(elem->name, "Master", 6) == 0 &&
      strstr(elem->name, "Playback Switch"))
    data->backwards = 1;

  g_signal_connect(
    data->button, "clicked", G_CALLBACK(button_clicked), data
  );
  alsa_elem_add_callback(elem, toggle_button_updated, data);
  data->text[0] = disabled_text;
  data->text[1] = enabled_text;
  load_icons(data);

  // find the maximum width and height of both possible labels
  int max_width = 0, max_height = 0;
  for (int i = 0; i < 2; i++) {
    toggle_button_set_text(data, i);

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

  // periodically update volatile controls
  if (alsa_get_elem_volatile(elem))
    data->source =
      g_timeout_add_seconds(1, (GSourceFunc)update_toggle_button, data);

  g_object_weak_ref(G_OBJECT(data->button), (GWeakNotify)on_destroy, data);

  return data->button;
}
