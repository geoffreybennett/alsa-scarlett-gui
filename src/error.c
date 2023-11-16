// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "error.h"

void show_error(GtkWindow *w, char *s) {
  if (!w) {
    printf("%s\n", s);
    return;
  }

  GtkWidget *dialog = gtk_message_dialog_new(
    w,
    GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
    GTK_MESSAGE_ERROR,
    GTK_BUTTONS_CLOSE,
    "%s",
    s
  );
  gtk_widget_set_visible(dialog, TRUE);

  g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);
}
