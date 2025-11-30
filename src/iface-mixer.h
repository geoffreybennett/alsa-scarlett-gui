// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alsa.h"

// Structure to store input gain widget and its routing source
struct input_gain_widget {
  GtkWidget          *widget;
  int                 port_num;     // 1-based port number
  struct routing_src *r_src;        // routing source for level lookup
};

// Structure to store output gain widget and its routing sink
struct output_gain_widget {
  GtkWidget          *widget;
  int                 port_num;     // 1-based port number
  struct routing_snk *r_snk;        // routing sink for level lookup
};

GtkWidget *create_iface_mixer_main(struct alsa_card *card);
