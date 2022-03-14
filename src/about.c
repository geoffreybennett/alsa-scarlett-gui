// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "about.h"

void activate_about(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  GtkWindow *w = GTK_WINDOW(data);

  const char *authors[] = {
    "Geoffrey D. Bennett",
    NULL
  };

  gtk_show_about_dialog(
    w,
    "program-name", "ALSA Scarlett Gen 2/3 Control Panel",
    "version", "Version " VERSION,
    "comments", "GTK4 interface to the ALSA Scarlett Gen 2/3 Mixer controls",
    "website", "https://github.com/geoffreybennett/alsa-scarlett-gui",
    "copyright", "Copyright 2022 Geoffrey D. Bennett",
    "license-type", GTK_LICENSE_GPL_3_0,
    "logo-icon-name", "alsa-scarlett-gui-logo",
    "title", "About ALSA Scarlett Mixer Interface",
    "authors", authors,
    NULL
  );
}
