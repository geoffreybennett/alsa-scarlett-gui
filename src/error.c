// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "error.h"

void show_error(GtkWindow *w, char *s) {
  if (!w) {
    printf("%s\n", s);
    return;
  }

  GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", s);
  gtk_alert_dialog_show(dialog, w);
  g_object_unref(dialog);
}
