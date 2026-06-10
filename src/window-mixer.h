// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alsa.h"

// Structure to store mixer gain widget and its coordinates
struct mixer_gain_widget {
  GtkWidget *widget;
  int mix_num;               // 0-based mix number (A=0, B=1, etc.)
  int input_num;             // 0-based input number
  struct routing_snk *r_snk; // routing sink for this input (for level lookup)
  struct alsa_elem *elem;    // alsa element for gain value (for post-gain level)

  // Stereo support: track multiple elements and sinks for stereo widgets
  struct alsa_elem *elems[4]; // array of controlled elements
  int elem_count;             // number of elements (1 = mono, >1 = stereo)
  struct routing_snk *r_snks[2]; // routing sinks for level lookup (L and R)
  int r_snk_count;            // number of routing sinks
};

GtkWidget *create_mixer_controls(struct alsa_card *card);
void update_mixer_labels(struct alsa_card *card);
void rebuild_mixer_grid(struct alsa_card *card);
void update_mixer_availability(struct alsa_card *card, int available);

// Recreate mixer widgets when stereo state changes
// This destroys existing widgets and creates new ones based on current stereo
void recreate_mixer_widgets(struct alsa_card *card);
