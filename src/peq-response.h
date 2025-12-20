// SPDX-FileCopyrightText: 2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>
#include "biquad.h"

G_BEGIN_DECLS

#define GTK_TYPE_FILTER_RESPONSE (gtk_filter_response_get_type())

G_DECLARE_FINAL_TYPE(
  GtkFilterResponse,
  gtk_filter_response,
  GTK,
  FILTER_RESPONSE,
  GtkWidget
)

#define FILTER_RESPONSE_MAX_BANDS 8

// Create a new filter response widget with the specified number of bands
GtkWidget *gtk_filter_response_new(int num_bands);

// Set filter parameters for a band (0-indexed)
void gtk_filter_response_set_filter(
  GtkFilterResponse          *response,
  int                         band,
  const struct biquad_params *params
);

// Enable/disable individual bands
void gtk_filter_response_set_band_enabled(
  GtkFilterResponse *response,
  int                band,
  gboolean           enabled
);

// Global enable (e.g., when section is disabled)
void gtk_filter_response_set_enabled(
  GtkFilterResponse *response,
  gboolean           enabled
);

// Overall DSP enable (e.g., when all DSP is disabled)
void gtk_filter_response_set_dsp_enabled(
  GtkFilterResponse *response,
  gboolean           enabled
);

// Set highlighted band (-1 for none)
void gtk_filter_response_set_highlight(
  GtkFilterResponse *response,
  int                band
);

G_END_DECLS
