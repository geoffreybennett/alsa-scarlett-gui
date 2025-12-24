// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "alsa.h"
#include "widget-drop-down.h"
#include "window-configuration.h"
#include "config-device-settings.h"

void add_device_settings_tab(GtkWidget *notebook, struct alsa_card *card) {
  struct alsa_elem *spdif_source = get_elem_by_name(
    card->elems, "S/PDIF Source Capture Enum"
  );

  if (!spdif_source)
    return;

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_top(content, 20);
  gtk_widget_set_margin_start(content, 20);
  gtk_widget_set_margin_end(content, 20);
  gtk_widget_set_margin_bottom(content, 20);

  // S/PDIF Source control
  GtkWidget *label = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(label), "<b>S/PDIF Source</b>");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(content), label);

  GtkWidget *dropdown = make_drop_down_alsa_elem(spdif_source, NULL);
  gtk_widget_set_halign(dropdown, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(content), dropdown);

  GtkWidget *help = gtk_label_new(
    "Select the S/PDIF input source: None to disable S/PDIF input,\n"
    "Optical for the optical input, or RCA for the coaxial input."
  );
  gtk_widget_set_halign(help, GTK_ALIGN_START);
  gtk_widget_add_css_class(help, "dim-label");
  gtk_box_append(GTK_BOX(content), help);

  g_object_set_data(G_OBJECT(content), PAGE_ID_KEY, (gpointer)"device-settings");
  gtk_notebook_append_page(
    GTK_NOTEBOOK(notebook), content, gtk_label_new("Device Settings")
  );
}
