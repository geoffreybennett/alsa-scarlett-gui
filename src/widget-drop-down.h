// SPDX-FileCopyrightText: 2023-2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>

#include "alsa.h"

GtkWidget *make_drop_down_alsa_elem(
  struct alsa_elem *elem,
  const char       *label_text
);

// Operations for a drop-down whose ALSA value isn't a 0..N-1 row index.
// All callbacks receive the user_data passed to make_value_mapped_drop_down_alsa_elem.
struct drop_down_value_ops {

  // Map an ALSA value to a row index in the model, or -1 if no row
  // matches (in which case refresh_model is expected to add one).
  int (*value_to_row)(int value, void *user_data);

  // Map a row index to the ALSA value to write. current_value is the
  // live element value at click time; return it to make the click a
  // no-op (useful for synthetic rows that don't represent a setting).
  // The row is guaranteed to be a valid index into the current model.
  int (*row_to_value)(int row, int current_value, void *user_data);

  // Return the button label for the given ALSA value. The returned
  // string is owned by the caller and must be freed with g_free().
  char *(*button_label)(int value, void *user_data);

  // Update the model so that value_to_row(value) returns a valid index
  // afterwards. May be NULL if the model is static and value_to_row
  // never returns -1. Called before every refresh.
  void (*refresh_model)(GtkStringList *model, int value, void *user_data);
};

// Build a drop-down that decouples the ALSA element value from the row
// index. The widget takes ownership of model and frees user_data with
// user_data_free on destruction (user_data_free may be NULL).
GtkWidget *make_value_mapped_drop_down_alsa_elem(
  struct alsa_elem                 *elem,
  GtkStringList                    *model,
  const struct drop_down_value_ops *ops,
  void                             *user_data,
  GDestroyNotify                    user_data_free
);
