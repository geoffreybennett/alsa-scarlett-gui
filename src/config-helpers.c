// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config-helpers.h"

GtkWidget *config_bold_label(const char *text) {
  GtkWidget *w = gtk_label_new(NULL);

  char *markup = g_strdup_printf("<b>%s</b>", text);
  gtk_label_set_markup(GTK_LABEL(w), markup);
  g_free(markup);

  gtk_widget_set_halign(w, GTK_ALIGN_START);
  return w;
}

GtkWidget *config_help_label(const char *text) {
  GtkWidget *w = gtk_label_new(text);

  gtk_widget_set_halign(w, GTK_ALIGN_START);
  gtk_widget_add_css_class(w, "dim-label");
  return w;
}

GtkWidget *config_wrapped_help_label(const char *text, int max_chars) {
  GtkWidget *w = config_help_label(text);

  gtk_label_set_wrap(GTK_LABEL(w), TRUE);
  if (max_chars > 0)
    gtk_label_set_max_width_chars(GTK_LABEL(w), max_chars);

  gtk_label_set_xalign(GTK_LABEL(w), 0.0);
  gtk_label_set_yalign(GTK_LABEL(w), 0.0);
  gtk_widget_set_valign(w, GTK_ALIGN_START);
  return w;
}

void config_append_separator(GtkBox *box) {
  GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_margin_top(sep, 5);
  gtk_widget_set_margin_bottom(sep, 5);
  gtk_box_append(box, sep);
}
