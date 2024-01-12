// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkhelper.h"
#include "iface-unknown.h"

GtkWidget *create_iface_unknown_main(void) {
  GtkWidget *label = gtk_label_new(
    "Sorry, I don’t recognise the controls on this card.\n\n"

    "These Focusrite Scarlett models should be supported:\n"
    "– Gen 2: 6i6/18i8/18i20\n"
    "– Gen 3: Solo/2i2/4i4/8i6/18i8/18i20\n\n"

    "Are you running a recent kernel with Scarlett Gen 2/3 support "
    "enabled?\n\n"

    "Check dmesg output for “Focusrite Scarlett Gen 2/3 Mixer "
    "Driver”:\n\n"

    "dmesg | grep Scarlett\n\n"

    "You may need to create a file /etc/modprobe.d/scarlett.conf\n"
    "with an “options snd_usb_audio ...” line and reboot."
  );
  gtk_widget_set_margin(label, 30);

  return label;
}
