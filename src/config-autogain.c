// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "alsa.h"
#include "widget-gain.h"
#include "window-configuration.h"

#define AUTOGAIN_TARGET_COUNT 3

void add_autogain_tab(GtkWidget *notebook, struct alsa_card *card) {
  static const char *names[] = {
    "Autogain Hot Target",
    "Autogain Mean Target",
    "Autogain Peak Target"
  };
  static const char *labels[] = { "Hot", "Mean", "Peak" };

  int count = 0;
  for (int i = 0; i < AUTOGAIN_TARGET_COUNT; i++)
    if (get_elem_by_name(card->elems, names[i]))
      count++;

  if (!count)
    return;

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(content, 20);
  gtk_widget_set_margin_end(content, 20);
  gtk_widget_set_margin_top(content, 20);
  gtk_widget_set_margin_bottom(content, 20);

  const char *help_text = count == 1
    ? "Configure the target level for the Autogain feature.\n"
      "This setting controls what signal level Autogain aims for."
    : "Configure the target levels for the Autogain feature.\n"
      "These settings control what signal levels Autogain aims for.";
  GtkWidget *help = gtk_label_new(help_text);
  gtk_widget_set_halign(help, GTK_ALIGN_START);
  gtk_widget_add_css_class(help, "dim-label");
  gtk_box_append(GTK_BOX(content), help);

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 20);
  gtk_widget_set_margin_top(grid, 10);
  gtk_widget_set_halign(grid, GTK_ALIGN_START);

  int col = 0;
  for (int i = 0; i < AUTOGAIN_TARGET_COUNT; i++) {
    struct alsa_elem *elem = get_elem_by_name(card->elems, names[i]);
    if (!elem)
      continue;

    GtkWidget *label = gtk_label_new(labels[i]);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), label, col, 0, 1, 1);

    GtkWidget *gain = make_gain_alsa_elem(
      elem,
      0,                       // zero_is_off
      WIDGET_GAIN_TAPER_LINEAR,
      1,                       // can_control
      FALSE                    // show_level
    );
    gtk_grid_attach(GTK_GRID(grid), gain, col, 1, 1, 1);
    col++;
  }

  gtk_box_append(GTK_BOX(content), grid);

  g_object_set_data(G_OBJECT(content), PAGE_ID_KEY, (gpointer)"autogain");
  gtk_notebook_append_page(
    GTK_NOTEBOOK(notebook), content, gtk_label_new("Autogain")
  );
}
