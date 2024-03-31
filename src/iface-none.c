// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "alsa.h"
#include "iface-none.h"
#include "gtkhelper.h"
#include "menu.h"

GtkWidget *create_window_iface_none(GtkApplication *app) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 50);
  gtk_widget_set_margin(box, 50);
  GtkWidget *picture = gtk_picture_new_for_resource(
    "/vu/b4/alsa-scarlett-gui/icons/vu.b4.alsa-scarlett-gui.png"
  );
  GtkWidget *label = gtk_label_new("No Scarlett/Clarett/Vocaster interface found.");

  gtk_box_append(GTK_BOX(box), picture);
  gtk_box_append(GTK_BOX(box), label);

  GtkWidget *w = gtk_application_window_new(app);
  gtk_window_set_resizable(GTK_WINDOW(w), FALSE);
  gtk_window_set_title(GTK_WINDOW(w), "ALSA Scarlett2 Control Panel");
  gtk_window_set_child(GTK_WINDOW(w), box);
  gtk_application_window_set_show_menubar(
    GTK_APPLICATION_WINDOW(w), TRUE
  );
  add_window_action_map(GTK_WINDOW(w));
  if (!alsa_has_reopen_callbacks()) {
    gtk_widget_set_visible(w, TRUE);
  }

  return w;
}
