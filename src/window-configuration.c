// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "gtkhelper.h"
#include "optional-controls.h"
#include "widget-text-entry.h"
#include "window-configuration.h"

struct configuration_window {
  struct alsa_card *card;
  GtkWidget        *top;
};

static void on_destroy(
  struct configuration_window *data,
  GtkWidget                   *widget
) {
  g_free(data);
}

GtkWidget *create_configuration_controls(struct alsa_card *card) {
  struct configuration_window *data =
    g_malloc0(sizeof(struct configuration_window));
  data->card = card;

  // create main container
  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
  gtk_widget_set_margin_start(vbox, 20);
  gtk_widget_set_margin_end(vbox, 20);
  gtk_widget_set_margin_top(vbox, 20);
  gtk_widget_set_margin_bottom(vbox, 20);

  data->top = vbox;

  // Device Name section
  struct alsa_elem *name_elem = optional_controls_get_name_elem(card);
  if (name_elem) {
    GtkWidget *name_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    GtkWidget *name_label = gtk_label_new(NULL);
    gtk_label_set_markup(
      GTK_LABEL(name_label),
      "<b>Device Name</b>"
    );
    gtk_widget_set_halign(name_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(name_section), name_label);

    GtkWidget *name_entry = make_text_entry_alsa_elem(name_elem);
    gtk_widget_set_hexpand(name_entry, TRUE);
    gtk_box_append(GTK_BOX(name_section), name_entry);

    GtkWidget *name_help = gtk_label_new(
      "This name will appear in the window title and can help you\n"
      "identify this device if you have multiple interfaces."
    );
    gtk_widget_set_halign(name_help, GTK_ALIGN_START);
    gtk_widget_add_css_class(name_help, "dim-label");
    gtk_box_append(GTK_BOX(name_section), name_help);

    gtk_box_append(GTK_BOX(vbox), name_section);
  }

  // cleanup on destroy
  g_object_weak_ref(
    G_OBJECT(data->top),
    (GWeakNotify)on_destroy,
    data
  );

  return data->top;
}
