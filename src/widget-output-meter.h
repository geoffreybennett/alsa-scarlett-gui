// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTK_TYPE_OUTPUT_METER (gtk_output_meter_get_type())

G_DECLARE_FINAL_TYPE(
  GtkOutputMeter,
  gtk_output_meter,
  GTK,
  OUTPUT_METER,
  GtkWidget
)

GtkWidget *gtk_output_meter_new(void);

void gtk_output_meter_set_levels(
  GtkOutputMeter *meter,
  double          left_db,
  double          right_db
);

void gtk_output_meter_reset_peaks(GtkOutputMeter *meter);

G_END_DECLS
