// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

void gtk_widget_set_margin(GtkWidget *w, int margin);
void gtk_widget_set_expand(GtkWidget *w, gboolean expand);
void gtk_widget_set_align(GtkWidget *w, GtkAlign x, GtkAlign y);
void gtk_grid_set_spacing(GtkGrid *grid, int spacing);
void gtk_widget_remove_css_classes_by_prefix(GtkWidget *w, const char *prefix);
