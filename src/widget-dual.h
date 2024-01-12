// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alsa.h"

// speaker switch and talkback have three states, controlled by two
// buttons:
// first button disables/enables the feature
// second button switches between the two features states
GtkWidget *make_dual_boolean_alsa_elems(
  struct alsa_elem *alsa_elem,
  const char       *label_text,
  const char       *disabled_text_1,
  const char       *enabled_text_1,
  const char       *disabled_text_2,
  const char       *enabled_text_2
);
