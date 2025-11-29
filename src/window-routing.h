// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "alsa.h"

GtkWidget *create_routing_controls(struct alsa_card *card);

// Update hardware output label to show monitor group status (Main/Alt indicators)
void update_hw_output_label(struct routing_snk *r_snk);

// Update cached effective source index for a routing sink
void update_snk_effective_source(struct routing_snk *r_snk);
