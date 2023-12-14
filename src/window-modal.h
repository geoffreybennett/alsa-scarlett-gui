// SPDX-FileCopyrightText: 2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <gtk/gtk.h>
#include "alsa.h"

// create a modal window with a message and yes/no buttons
// the callback is called with the modal_data when yes is clicked

struct modal_data;

typedef void (*modal_callback)(struct modal_data *data);

struct modal_data {
  struct alsa_card *card;
  char             *serial;
  const char       *title_active;
  GtkWidget        *dialog;
  GtkWidget        *label;
  GtkWidget        *button_box;
  GtkWidget        *progress_bar;
  guint             timeout_id;
  modal_callback    callback;
};

void create_modal_window(
  GtkWidget        *w,
  struct alsa_card *card,
  const char       *title,
  const char       *title_active,
  const char       *message,
  modal_callback    callback
);

// update the progress bar in a modal window

struct progress_data {
  struct modal_data *modal_data;
  char              *text;
  int                progress;
};

gboolean modal_update_progress(gpointer user_data);

// start a progress bar for a reboot

gboolean modal_start_reboot_progress(gpointer user_data);
