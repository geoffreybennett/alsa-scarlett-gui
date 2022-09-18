// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkhelper.h"
#include "iface-unknown.h"

#include <libintl.h>
#define _(String) gettext (String)

GtkWidget *create_iface_unknown_main(void) {
  GtkWidget *label = gtk_label_new(
  	g_strdup_printf(
    "%s\n\n"
    "%s\n"
    "– %s\n"
    "– %s\n\n"
    "%s\n\n"
    "%s:\n\n"
    "%s\n\n"
    "%s",
    
    _("Sorry, I don’t recognise the controls on this card."),
    _("These Focusrite Scarlett models should be supported:"),
    _("Gen 2: 6i6/18i8/18i20"),
    _("Gen 3: Solo/2i2/4i4/8i6/18i8/18i20"),
    _("Are you running a recent kernel with Scarlett Gen 2/3 support "
    "enabled?"),
    g_strdup_printf(_("Check dmesg output for \"%s\""),
    	"Focusrite Scarlett Gen 2/3 Mixer Driver"),
    "dmesg | grep Scarlett",
    _("You may need to create a file /etc/modprobe.d/scarlett.conf\n"
    "with an \"options snd_usb_audio ...\" line and reboot.")
    )
  );
  gtk_widget_set_margin(label, 30);

  return label;
}
