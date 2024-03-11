// SPDX-FileCopyrightText: 2023-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <ctype.h>

#include "gtkhelper.h"
#include "widget-drop-down.h"

struct drop_down {
  struct alsa_elem   *elem;
  GtkWidget          *button;
  GtkWidget          *popover;
  GtkWidget          *listview;
  GtkSingleSelection *selection;
  int                 fixed_text;
};

static void sanitise_class_name(char *s) {
  char *dst = s;

  while (*s) {
    if (isalnum(*s) || *s == '-')
      *dst++ = tolower(*s);
    s++;
  }

  *dst = '\0';
}

static void add_class(GtkWidget *widget, const char *class) {
  char *class_name = g_strdup_printf("selected-%s", class);

  sanitise_class_name(class_name);
  gtk_widget_add_css_class(widget, class_name);
  g_free(class_name);
}

static void list_item_activated(
  GtkListItem      *list_item,
  guint             index,
  struct drop_down *data
) {
  alsa_set_elem_value(data->elem, index);

  gtk_popover_popdown(GTK_POPOVER(data->popover));
}

static void toggle_button_clicked(GtkWidget *widget, struct drop_down *data) {
  gtk_popover_popup(GTK_POPOVER(data->popover));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->button), FALSE);
}

static void setup_factory(
  GtkListItemFactory *factory,
  GtkListItem        *list_item,
  gpointer            user_data
) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

  GtkWidget *label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0);
  gtk_box_append(GTK_BOX(box), label);

  GtkWidget *icon = gtk_image_new_from_icon_name("object-select-symbolic");
  gtk_box_append(GTK_BOX(box), icon);

  gtk_list_item_set_child(list_item, box);
}

static void update_list_item(
  GtkListItem      *list_item,
  struct drop_down *data
) {
  GtkWidget *box = gtk_list_item_get_child(list_item);
  GtkWidget *icon = gtk_widget_get_last_child(box);

  int index = gtk_single_selection_get_selected(data->selection);

  if (index == gtk_list_item_get_position(list_item))
    gtk_widget_set_opacity(icon, 1.0);
  else
    gtk_widget_set_opacity(icon, 0.0);
}

static void bind_factory(
  GtkListItemFactory *factory,
  GtkListItem        *list_item,
  gpointer            user_data
) {
  struct drop_down *data = user_data;

  GtkWidget *box = gtk_list_item_get_child(list_item);
  GtkWidget *label = gtk_widget_get_first_child(box);

  int index = gtk_list_item_get_position(list_item);
  const char *text = alsa_get_item_name(data->elem, index);
  gtk_label_set_text(GTK_LABEL(label), text);

  update_list_item(list_item, data);
}

static void drop_down_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct drop_down *data = private;

  int is_writable = alsa_get_elem_writable(elem);
  gtk_widget_set_sensitive(data->button, is_writable);

  int value = alsa_get_elem_value(elem);
  gtk_single_selection_set_selected(data->selection, value);

  gtk_widget_remove_css_classes_by_prefix(data->button, "selected-");
  add_class(data->button, alsa_get_item_name(elem, value));

  if (data->fixed_text)
    return;

  gtk_button_set_label(
    GTK_BUTTON(data->button),
    alsa_get_item_name(elem, value)
  );
}

static void drop_down_destroy(GtkWidget *widget, GtkWidget *popover) {
  gtk_widget_unparent(popover);
}

GtkWidget *make_drop_down_alsa_elem(
  struct alsa_elem *elem,
  const char       *label_text
) {
  struct drop_down *data = g_malloc(sizeof(struct drop_down));
  data->elem = elem;

  data->button = gtk_toggle_button_new_with_label(label_text);
  gtk_widget_add_css_class(data->button, "drop-down");
  data->fixed_text = !!label_text;

  data->popover = gtk_popover_new();
  gtk_popover_set_has_arrow(GTK_POPOVER(data->popover), FALSE);
  gtk_widget_set_parent(
    data->popover,
    gtk_widget_get_first_child(data->button)
  );
  g_signal_connect(
    gtk_widget_get_first_child(data->button),
    "destroy", G_CALLBACK(drop_down_destroy), data->popover
  );

  GListModel *model = G_LIST_MODEL(gtk_string_list_new(NULL));

  int count = alsa_get_item_count(elem);
  for (int i = 0; i < count; i++) {
    const char *text = alsa_get_item_name(elem, i);

    gtk_string_list_append(GTK_STRING_LIST(model), text);
  }

  GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
  g_signal_connect(
    factory, "setup", G_CALLBACK(setup_factory), data
  );
  g_signal_connect(
    factory, "bind", G_CALLBACK(bind_factory), data
  );

  data->selection = gtk_single_selection_new(model);
  data->listview = gtk_list_view_new(
    GTK_SELECTION_MODEL(data->selection),
    factory
  );
  gtk_list_view_set_single_click_activate(GTK_LIST_VIEW(data->listview), TRUE);

  gtk_popover_set_child(GTK_POPOVER(data->popover), data->listview);

  g_signal_connect(
    data->button, "clicked", G_CALLBACK(toggle_button_clicked), data
  );
  g_signal_connect(
    data->listview, "activate", G_CALLBACK(list_item_activated), data
  );
  drop_down_updated(elem, data);

  alsa_elem_add_callback(elem, drop_down_updated, data);

  return data->button;
}
