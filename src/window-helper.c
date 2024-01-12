// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "window-helper.h"

gboolean window_startup_close_request(GtkWindow *w, gpointer data) {
  struct alsa_card *card = data;

  gtk_widget_activate_action(
    GTK_WIDGET(card->window_main), "win.startup", NULL
  );
  return true;
}

GtkWidget *create_subwindow(
  struct alsa_card *card,
  const char       *name,
  GCallback         close_callback
) {
  char *title = g_strdup_printf("%s %s", card->name, name);

  GtkWidget *w = gtk_window_new();
  gtk_window_set_resizable(GTK_WINDOW(w), FALSE);
  gtk_window_set_title(GTK_WINDOW(w), title);
  g_signal_connect(w, "close_request", G_CALLBACK(close_callback), card);

  g_free(title);
  return w;
}
