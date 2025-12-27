// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "alsa.h"

GtkWidget *create_routing_controls(struct alsa_card *card);

// Update hardware output label to show monitor group status (Main/Alt indicators)
void update_hw_output_label(struct routing_snk *r_snk);

// Update hardware input label for S/PDIF and ADAT availability
void update_hw_input_label(struct routing_src *r_src);

// Update cached effective source index for a routing sink
void update_snk_effective_source(struct routing_snk *r_snk);

// Update all PCM labels when channel availability changes
void update_all_pcm_labels(struct alsa_card *card);

// Update all HW I/O labels when availability changes
void update_all_hw_io_labels(struct alsa_card *card);

// Check if a sink is currently muted (in inactive monitor group)
int is_snk_monitor_muted(struct routing_snk *r_snk);
