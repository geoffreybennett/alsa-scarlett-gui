// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "alsa.h"
#include "optional-controls.h"
#include "widget-text-entry.h"
#include "window-configuration.h"
#include "config-device-name.h"

void add_device_name_tab(GtkWidget *notebook, struct alsa_card *card) {
  struct alsa_elem *name_elem = optional_controls_get_name_elem(card);
  if (!name_elem)
    return;

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_top(content, 20);

  GtkWidget *help = gtk_label_new(
    "This name will appear in the window title and can help you\n"
    "identify this device if you have multiple interfaces."
  );
  gtk_widget_set_halign(help, GTK_ALIGN_START);
  gtk_widget_add_css_class(help, "dim-label");
  gtk_box_append(GTK_BOX(content), help);

  GtkWidget *entry = make_text_entry_alsa_elem(name_elem);
  gtk_widget_set_hexpand(entry, TRUE);
  gtk_box_append(GTK_BOX(content), entry);

  g_object_set_data(G_OBJECT(content), PAGE_ID_KEY, (gpointer)"device-name");
  gtk_notebook_append_page(
    GTK_NOTEBOOK(notebook), content, gtk_label_new("Device Name")
  );
}
