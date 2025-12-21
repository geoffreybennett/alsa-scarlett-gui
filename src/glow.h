// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cairo.h>
#include "alsa.h"

// glow effect configuration
#define GLOW_LAYERS 4
#define GLOW_MAX_WIDTH 16.0
#define GLOW_MIN_DB -60.0
#define GLOW_MAX_DB 0.0

// calculate glow intensity (0 to 1) from dB level, with curve applied
double get_glow_intensity(double level_db);

// calculate glow layer width and alpha for a given layer and intensity
void get_glow_layer_params(
  int     layer,
  double  intensity,
  double *width,
  double *alpha
);

// convert dB level to RGB colour (green → yellow → red)
void level_to_colour(double db, double *r, double *g, double *b);

// get the level meter index for a routing sink
int get_routing_snk_level_index(
  struct alsa_card   *card,
  struct routing_snk *r_snk
);

// get level in dB for a routing source (-80 if no level data)
double get_routing_src_level_db(
  struct alsa_card   *card,
  struct routing_src *r_src
);
