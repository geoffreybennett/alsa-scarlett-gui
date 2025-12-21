// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alsa.h"
#include "biquad.h"

// Initialise DSP filter state elements for all filter stages
// - Creates simulated ALSA elements for filter parameters
// - Loads values from state file
// Must be called after alsa_get_routing_controls()
void dsp_state_init(struct alsa_card *card);

// Get the simulated element for a filter parameter
// filter_type is "Pre-Comp" or "PEQ"
// channel is 1 or 2 (Line In channel)
// stage is 1-based stage/band number
// Returns NULL if not available
struct alsa_elem *dsp_state_get_enable_elem(
  struct alsa_card *card,
  const char       *filter_type,
  int               channel,
  int               stage
);

struct alsa_elem *dsp_state_get_type_elem(
  struct alsa_card *card,
  const char       *filter_type,
  int               channel,
  int               stage
);

struct alsa_elem *dsp_state_get_freq_elem(
  struct alsa_card *card,
  const char       *filter_type,
  int               channel,
  int               stage
);

struct alsa_elem *dsp_state_get_q_elem(
  struct alsa_card *card,
  const char       *filter_type,
  int               channel,
  int               stage
);

struct alsa_elem *dsp_state_get_gain_elem(
  struct alsa_card *card,
  const char       *filter_type,
  int               channel,
  int               stage
);
