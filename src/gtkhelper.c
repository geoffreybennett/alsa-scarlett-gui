// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
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

void gtk_widget_remove_css_classes_by_prefix(
  GtkWidget  *w,
  const char *prefix
) {
  char **classes = gtk_widget_get_css_classes(w);

  for (char **i = classes; *i != NULL; i++)
    if (strncmp(*i, prefix, strlen(prefix)) == 0)
      gtk_widget_remove_css_class(w, *i);

  g_strfreev(classes);
}

void gtk_widget_get_color_compat(GtkWidget *w, GdkRGBA *color) {
#if GTK_CHECK_VERSION(4, 10, 0)
  gtk_widget_get_color(w, color);
#else
  gtk_style_context_get_color(gtk_widget_get_style_context(w), color);
#endif
}

#if !GTK_CHECK_VERSION(4, 12, 0)

// Move keyboard focus to the list view row at the given position, for
// GTK before 4.12 where gtk_list_view_scroll_to() is unavailable. Without
// this, the freshly-popped list leaves focus on row 0, which shows the
// focus highlight and looks like the selection.
void gtk_list_view_focus_row(GtkWidget *listview, guint pos) {
  GtkWidget *row = gtk_widget_get_first_child(listview);

  for (guint i = 0; i < pos && row; i++)
    row = gtk_widget_get_next_sibling(row);

  if (row)
    gtk_widget_grab_focus(row);
}

#endif
