// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alsa.h"
#include "compressor-curve.h"

// Structure to store compressor curve widget and its routing elements
struct dsp_comp_widget {
  GtkCompressorCurve *curve;
  struct routing_snk *input_snk;  // DSP Input sink
  struct routing_src *output_src; // DSP Output source
};

GtkWidget *create_dsp_controls(struct alsa_card *card);
