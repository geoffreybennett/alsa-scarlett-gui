// SPDX-FileCopyrightText: 2023-2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <ctype.h>

#include "gtkhelper.h"
#include "widget-drop-down.h"

// Two construction modes share this state:
//   Enum mode (ops == NULL): model is built from alsa_get_item_name
//   and the ALSA value is the row index.
//   Value-mapped mode (ops != NULL): caller-supplied model, ops map
//   between ALSA values and row indices.
struct drop_down {
  struct alsa_elem   *elem;
  GtkWidget          *button;
  GtkWidget          *popover;
  GtkWidget          *listview;
  GtkStringList      *model;
  GtkSingleSelection *selection;
  int                 fixed_text;

  // Latest ALSA value seen by drop_down_updated. Read by
  // list_item_activated to skip redundant writes when the user picks a
  // row whose value hasn't changed.
  int                 current_value;

  // Per-row tick icons. icon_count tracks the allocated length so the
  // value-mapped path can grow the array when the model expands.
  GtkWidget         **icons;
  int                 icon_count;
  int                 ticked_row;

  const struct drop_down_value_ops *ops;
  void                             *user_data;
  GDestroyNotify                    user_data_free;
};

static int is_value_mapped(struct drop_down *data) {
  return data->ops != NULL;
}

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

static void resize_icons(struct drop_down *data, int new_count) {
  if (new_count == data->icon_count)
    return;
  data->icons = g_realloc(data->icons, new_count * sizeof(GtkWidget *));
  for (int i = data->icon_count; i < new_count; i++)
    data->icons[i] = NULL;
  data->icon_count = new_count;
}

static void update_icons(struct drop_down *data) {
  for (int i = 0; i < data->icon_count; i++)
    if (data->icons[i])
      gtk_widget_set_opacity(
        data->icons[i],
        i == data->ticked_row ? 1.0 : 0.0
      );
}

static void list_item_activated(
  GtkListView *list,
  guint        index,
  gpointer     user_data
) {
  struct drop_down *data = user_data;

  if (is_value_mapped(data)) {
    int value = data->ops->row_to_value(
      (int)index, data->current_value, data->user_data
    );

    // ops can return the current value to mean "no-op", which is how
    // the sleep dropdown ignores clicks on the synthetic "Custom (N s)"
    // row. Skip the write so the element doesn't see a redundant set.
    if (value != data->current_value)
      alsa_set_elem_value(data->elem, value);
  } else {
    data->current_value = (int)index;
    data->ticked_row = (int)index;
    update_icons(data);
    alsa_set_elem_value(data->elem, (int)index);
  }

  gtk_popover_popdown(GTK_POPOVER(data->popover));
}

static void toggle_button_clicked(GtkWidget *widget, gpointer user_data) {
  struct drop_down *data = user_data;

  gtk_popover_popup(GTK_POPOVER(data->popover));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->button), FALSE);

  // Land focus on the currently-ticked row so Enter accepts the
  // current value and arrow keys adjust from there.
  if (data->ticked_row >= 0) {
    gtk_single_selection_set_selected(data->selection, data->ticked_row);
    gtk_list_view_scroll_to(
      GTK_LIST_VIEW(data->listview),
      data->ticked_row,
      GTK_LIST_SCROLL_FOCUS,
      NULL
    );
  }
}

static void setup_factory(
  GtkListItemFactory *factory,
  GtkListItem        *list_item,
  gpointer            user_data
) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

  GtkWidget *label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_box_append(GTK_BOX(box), label);

  GtkWidget *icon = gtk_image_new_from_icon_name("object-select-symbolic");
  gtk_box_append(GTK_BOX(box), icon);

  gtk_list_item_set_child(list_item, box);
}

static void bind_factory(
  GtkListItemFactory *factory,
  GtkListItem        *list_item,
  gpointer            user_data
) {
  struct drop_down *data = user_data;

  GtkWidget *box = gtk_list_item_get_child(list_item);
  GtkWidget *label = gtk_widget_get_first_child(box);
  GtkWidget *icon = gtk_widget_get_last_child(box);

  int index = gtk_list_item_get_position(list_item);

  if (is_value_mapped(data)) {
    GtkStringObject *so = gtk_list_item_get_item(list_item);
    gtk_label_set_text(GTK_LABEL(label), gtk_string_object_get_string(so));
  } else {
    gtk_label_set_text(GTK_LABEL(label), alsa_get_item_name(data->elem, index));
  }

  if (index < data->icon_count)
    data->icons[index] = icon;
  gtk_widget_set_opacity(icon, index == data->ticked_row ? 1.0 : 0.0);
}

static void drop_down_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct drop_down *data = private;

  int is_writable = alsa_get_elem_writable(elem);
  gtk_widget_set_sensitive(data->button, is_writable);

  int value = alsa_get_elem_value(elem);
  data->current_value = value;

  if (is_value_mapped(data)) {
    if (data->ops->refresh_model)
      data->ops->refresh_model(data->model, value, data->user_data);

    int row = data->ops->value_to_row(value, data->user_data);

    // Keep the icon array sized to the (possibly grown) model so
    // bind_factory has somewhere to record icons for new rows.
    int model_n = (int)g_list_model_get_n_items(G_LIST_MODEL(data->model));
    if (model_n > data->icon_count)
      resize_icons(data, model_n);

    data->ticked_row = row;
    update_icons(data);
    gtk_single_selection_set_selected(data->selection, row);

    char *label = data->ops->button_label(value, data->user_data);
    gtk_button_set_label(GTK_BUTTON(data->button), label);
    g_free(label);
  } else {
    data->ticked_row = value;
    update_icons(data);
    gtk_single_selection_set_selected(data->selection, value);

    gtk_widget_remove_css_classes_by_prefix(data->button, "selected-");
    add_class(data->button, alsa_get_item_name(elem, value));

    if (!data->fixed_text)
      gtk_button_set_label(
        GTK_BUTTON(data->button),
        alsa_get_item_name(elem, value)
      );
  }
}

static void drop_down_destroy(GtkWidget *widget, gpointer user_data) {
  struct drop_down *data = user_data;

  // Detach from the element so a late event can't dereference us.
  alsa_elem_remove_callbacks_by_data(data->elem, data);

  gtk_widget_unparent(data->popover);
  g_free(data->icons);
  if (data->user_data_free)
    data->user_data_free(data->user_data);
  g_free(data);
}

// Build the popover/listview/button infrastructure shared by both
// dropdown variants. The caller has already populated data->elem,
// data->model, data->fixed_text, and any value-mapped fields.
static void build_drop_down(struct drop_down *data, const char *label_text) {
  data->button = gtk_toggle_button_new_with_label(label_text);
  gtk_widget_add_css_class(data->button, "drop-down");

  data->popover = gtk_popover_new();
  gtk_popover_set_has_arrow(GTK_POPOVER(data->popover), FALSE);
  gtk_widget_set_parent(
    data->popover,
    gtk_widget_get_first_child(data->button)
  );
  g_signal_connect(
    gtk_widget_get_first_child(data->button),
    "destroy", G_CALLBACK(drop_down_destroy), data
  );

  GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "setup", G_CALLBACK(setup_factory), data);
  g_signal_connect(factory, "bind", G_CALLBACK(bind_factory), data);

  data->selection = gtk_single_selection_new(G_LIST_MODEL(data->model));
  data->listview = gtk_list_view_new(
    GTK_SELECTION_MODEL(data->selection), factory
  );
  gtk_list_view_set_single_click_activate(GTK_LIST_VIEW(data->listview), TRUE);
  gtk_widget_add_css_class(data->listview, "drop-down-list");

  gtk_popover_set_child(GTK_POPOVER(data->popover), data->listview);

  g_signal_connect(
    data->button, "clicked", G_CALLBACK(toggle_button_clicked), data
  );
  g_signal_connect(
    data->listview, "activate", G_CALLBACK(list_item_activated), data
  );

  drop_down_updated(data->elem, data);

  alsa_elem_add_callback(data->elem, drop_down_updated, data, NULL);
}

GtkWidget *make_drop_down_alsa_elem(
  struct alsa_elem *elem,
  const char       *label_text
) {
  struct drop_down *data = g_malloc0(sizeof(struct drop_down));
  data->elem = elem;
  data->fixed_text = !!label_text;
  data->ticked_row = -1;

  data->model = gtk_string_list_new(NULL);

  data->icon_count = alsa_get_item_count(elem);
  data->icons = g_malloc0(data->icon_count * sizeof(GtkWidget *));
  for (int i = 0; i < data->icon_count; i++)
    gtk_string_list_append(data->model, alsa_get_item_name(elem, i));

  build_drop_down(data, label_text);

  return data->button;
}

GtkWidget *make_value_mapped_drop_down_alsa_elem(
  struct alsa_elem                 *elem,
  GtkStringList                    *model,
  const struct drop_down_value_ops *ops,
  void                             *user_data,
  GDestroyNotify                    user_data_free
) {
  struct drop_down *data = g_malloc0(sizeof(struct drop_down));
  data->elem = elem;
  data->ticked_row = -1;
  data->model = model;
  data->ops = ops;
  data->user_data = user_data;
  data->user_data_free = user_data_free;

  int n = (int)g_list_model_get_n_items(G_LIST_MODEL(model));
  data->icons = g_malloc0(n * sizeof(GtkWidget *));
  data->icon_count = n;

  build_drop_down(data, "");

  return data->button;
}
