// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkhelper.h"

void gtk_widget_set_margin(GtkWidget *w, int margin) {
  gtk_widget_set_margin_top(w, margin);
  gtk_widget_set_margin_bottom(w, margin);
  gtk_widget_set_margin_start(w, margin);
  gtk_widget_set_margin_end(w, margin);
}

void gtk_widget_set_expand(GtkWidget *w, gboolean expand) {
  gtk_widget_set_hexpand(w, expand);
  gtk_widget_set_vexpand(w, expand);
}

void gtk_widget_set_align(GtkWidget *w, GtkAlign x, GtkAlign y) {
  gtk_widget_set_halign(w, x);
  gtk_widget_set_valign(w, y);
}

void gtk_grid_set_spacing(GtkGrid *grid, int spacing) {
  gtk_grid_set_row_spacing(grid, spacing);
  gtk_grid_set_column_spacing(grid, spacing);
}

void gtk_widget_add_class(GtkWidget *w, const char *class) {
  GtkStyleContext *style_context = gtk_widget_get_style_context(w);
  gtk_style_context_add_class(style_context, class);
}
