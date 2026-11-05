// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "alsa.h"
#include "remote-server.h"

void dialog_pairing_qr_show(GtkWindow *parent, struct remote_server *server);
