// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkhelper.h"
#include "iface-unknown.h"

GtkWidget *create_iface_unknown_main(void) {
  GtkWidget *label = gtk_label_new(
    "Sorry, I don’t recognise the controls on this card.\n\n"

    "These Focusrite models should be supported:\n"
    "– Gen 1: 6i6/8i6/18i6/18i8/18i20\n"
    "– Gen 2: 6i6/18i8/18i20\n"
    "– Gen 3: Solo/2i2/4i4/8i6/18i8/18i20\n"
    "– Gen 4: Solo/2i2/4i4/16i16/18i16/18i20\n"
    "– Vocaster One and Two\n"
    "– Clarett USB and Clarett+ 2Pre/4Pre/8Pre\n\n"

    "Please check the prerequisites at:\n"
    "https://github.com/geoffreybennett/alsa-scarlett-gui/"
  );
  gtk_widget_set_margin(label, 30);

  return label;
}
