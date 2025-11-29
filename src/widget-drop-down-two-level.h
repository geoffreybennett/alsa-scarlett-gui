// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "alsa.h"

// Create a two-level hierarchical dropdown for ALSA enum elements.
// Groups items by stripping trailing numbers from item names.
// E.g., "PCM 1", "PCM 2" become a "PCM" group with "1", "2" subitems.
GtkWidget *make_drop_down_two_level_alsa_elem(struct alsa_elem *elem);
