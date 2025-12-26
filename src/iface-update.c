// SPDX-FileCopyrightText: 2024-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "alsa.h"
#include "device-update-firmware.h"
#include "gtkhelper.h"
#include "scarlett2-firmware.h"

GtkWidget *create_iface_update_main(struct alsa_card *card) {
  GtkWidget *top = gtk_frame_new(NULL);
  gtk_widget_add_css_class(top, "window-frame");

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 30);
  gtk_widget_add_css_class(content, "window-content");
  gtk_widget_add_css_class(content, "top-level-content");
  gtk_widget_add_css_class(content, "big-padding");
  gtk_frame_set_child(GTK_FRAME(top), content);

  // explanation
  GtkWidget *w;

  // Check if we have firmware available for this device
  int has_firmware = card->best_firmware_version ||
                     card->best_firmware_version_4;

  w = gtk_label_new(has_firmware ? "Firmware Update Available"
                                 : "Firmware Update Required");
  gtk_widget_add_css_class(w, "window-title");
  gtk_box_append(GTK_BOX(content), w);

  if (!has_firmware) {
    const char *url = card->driver_type == DRIVER_TYPE_SOCKET
      ? "https://github.com/geoffreybennett/scarlett4-firmware"
      : "https://github.com/geoffreybennett/scarlett2-firmware";

    char *markup = g_strdup_printf(
      "A firmware update is required for this device in order to\n"
      "access all of its features. Please obtain the firmware from\n"
      "<a class=\"linktext\" href=\"%s\">%s</a>,\n"
      "and restart this application.",
      url, url
    );

    w = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(w), markup);
    g_free(markup);

    gtk_box_append(GTK_BOX(content), w);
    return top;
  }

  w = gtk_label_new(
    "A firmware update is available for this device.\n"
    "This process may take a couple of minutes.\n"
    "Please do not disconnect the device during the update."
  );
  gtk_box_append(GTK_BOX(content), w);

  w = gtk_button_new_with_label("Update");
  g_signal_connect(
    GTK_BUTTON(w), "clicked", G_CALLBACK(create_update_firmware_window), card
  );
  gtk_box_append(GTK_BOX(content), w);

  return top;
}
