// SPDX-FileCopyrightText: 2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTK_TYPE_COMPRESSOR_CURVE (gtk_compressor_curve_get_type())

G_DECLARE_FINAL_TYPE(
  GtkCompressorCurve,
  gtk_compressor_curve,
  GTK,
  COMPRESSOR_CURVE,
  GtkWidget
)

GtkWidget *gtk_compressor_curve_new(void);

void gtk_compressor_curve_set_threshold(GtkCompressorCurve *curve, int threshold);
void gtk_compressor_curve_set_ratio(GtkCompressorCurve *curve, int ratio);
void gtk_compressor_curve_set_knee_width(GtkCompressorCurve *curve, int knee_width);
void gtk_compressor_curve_set_makeup_gain(GtkCompressorCurve *curve, int makeup_gain);

G_END_DECLS
