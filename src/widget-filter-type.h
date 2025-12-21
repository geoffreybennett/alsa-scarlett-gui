// SPDX-FileCopyrightText: 2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "biquad.h"

// Callback type for filter type changes
typedef void (*FilterTypeChangedCallback)(
  GtkWidget       *widget,
  BiquadFilterType type,
  gpointer         user_data
);

// Create a dropdown widget for selecting filter types with icons
GtkWidget *make_filter_type_dropdown(BiquadFilterType initial_type);

// Get/set the selected filter type
BiquadFilterType filter_type_dropdown_get_selected(GtkWidget *widget);
void filter_type_dropdown_set_selected(GtkWidget *widget, BiquadFilterType type);

// Redraw the button icon (call after changing the filter type externally)
void filter_type_dropdown_queue_redraw(GtkWidget *widget);

// Connect a callback to be called when the filter type changes
void filter_type_dropdown_connect_changed(
  GtkWidget                  *widget,
  FilterTypeChangedCallback   callback,
  gpointer                    user_data
);
