// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "about.h"

static GdkTexture *logo = NULL;

void activate_about(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  GtkWindow *w = GTK_WINDOW(data);

  const char *authors[] = {
    "Geoffrey D. Bennett <g@b4.vu>",
    NULL
  };

  if (!logo)
    logo = gdk_texture_new_from_resource(
      "/vu/b4/alsa-scarlett-gui/icons/vu.b4.alsa-scarlett-gui.png"
    );

  gtk_show_about_dialog(
    w,
    "program-name", "ALSA Scarlett2 Control Panel",
    "version", "Version " VERSION,
    "comments",
      "Gtk4 GUI for the ALSA controls presented by the\n"
      "Linux kernel Focusrite Scarlett2 Mixer Driver",
    "website", "https://github.com/geoffreybennett/alsa-scarlett-gui",
    "copyright", "Copyright 2022-2024 Geoffrey D. Bennett",
    "license-type", GTK_LICENSE_GPL_3_0,
    "logo", logo,
    "title", "About ALSA Scarlett2 Mixer Interface",
    "authors", authors,
    NULL
  );
}
