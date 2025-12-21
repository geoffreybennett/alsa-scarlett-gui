// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <alsa/asoundlib.h>
#include <string.h>

#include "dsp-state.h"
#include "optional-state.h"
#include "alsa.h"

// Scaling factors for storing float values as integers
#define FREQ_SCALE 10    // Hz * 10 (1000.5 Hz = 10005)
#define Q_SCALE 1000     // Q * 1000 (0.707 = 707)
#define GAIN_SCALE 10    // dB * 10 (3.5 dB = 35)

// Generate element name for a filter parameter
static char *get_filter_elem_name(
  const char *filter_type,
  int         channel,
  int         stage,
  const char *param
) {
  return g_strdup_printf(
    "Line In %d %s Filter %d %s",
    channel, filter_type, stage, param
  );
}

// Create a simulated integer element for a DSP parameter
static struct alsa_elem *create_dsp_elem(
  struct alsa_card *card,
  const char       *elem_name,
  long              min_val,
  long              max_val,
  long              default_val,
  GHashTable       *state
) {
  // check if element already exists
  struct alsa_elem *elem = get_elem_by_name(card->elems, elem_name);
  if (elem)
    return elem;

  // create simulated integer element
  elem = alsa_create_optional_elem(
    card,
    elem_name,
    SND_CTL_ELEM_TYPE_INTEGER,
    0
  );

  if (!elem) {
    fprintf(stderr, "Failed to create DSP element %s\n", elem_name);
    return NULL;
  }

  // set min/max values
  elem->min_val = min_val;
  elem->max_val = max_val;

  // load value from state file
  const char *value_str = g_hash_table_lookup(state, elem_name);
  long value = default_val;

  if (value_str && *value_str) {
    value = strtol(value_str, NULL, 10);
    if (value < min_val)
      value = min_val;
    if (value > max_val)
      value = max_val;
  }

  elem->value = value;

  return elem;
}

// Create all filter parameter elements for one filter stage
static void create_filter_stage_elems(
  struct alsa_card *card,
  const char       *filter_type,
  int               channel,
  int               stage,
  BiquadFilterType  default_type,
  GHashTable       *state
) {
  char *name;

  // Enable (boolean stored as 0/1)
  name = get_filter_elem_name(filter_type, channel, stage, "Enable");
  create_dsp_elem(card, name, 0, 1, 1, state);
  g_free(name);

  // Type (BiquadFilterType enum)
  name = get_filter_elem_name(filter_type, channel, stage, "Type");
  create_dsp_elem(card, name, 0, BIQUAD_TYPE_COUNT - 1, default_type, state);
  g_free(name);

  // Freq (Hz * FREQ_SCALE, 20-20000 Hz)
  name = get_filter_elem_name(filter_type, channel, stage, "Freq");
  create_dsp_elem(
    card, name,
    20 * FREQ_SCALE,
    20000 * FREQ_SCALE,
    1000 * FREQ_SCALE,
    state
  );
  g_free(name);

  // Q (Q * Q_SCALE, 0.1-10)
  name = get_filter_elem_name(filter_type, channel, stage, "Q");
  create_dsp_elem(
    card, name,
    (long)(0.1 * Q_SCALE),
    10 * Q_SCALE,
    (long)(0.707 * Q_SCALE),
    state
  );
  g_free(name);

  // Gain (dB * GAIN_SCALE, -18 to +18 dB)
  name = get_filter_elem_name(filter_type, channel, stage, "Gain");
  create_dsp_elem(
    card, name,
    -18 * GAIN_SCALE,
    18 * GAIN_SCALE,
    0,
    state
  );
  g_free(name);
}

// Initialise DSP filter state elements for a card
void dsp_state_init(struct alsa_card *card) {
  if (!card->serial || !*card->serial)
    return;

  // check if card has DSP controls by looking for a DSP element
  char *test_name = g_strdup_printf("Line In 1 DSP Capture Switch");
  struct alsa_elem *test_elem = get_elem_by_name(card->elems, test_name);
  g_free(test_name);

  if (!test_elem)
    return;

  // load existing state from [controls] section
  GHashTable *state = optional_state_load(card, CONFIG_SECTION_CONTROLS);
  if (!state) {
    state = g_hash_table_new_full(
      g_str_hash, g_str_equal, g_free, g_free
    );
  }

  // create elements for each channel
  for (int channel = 1; channel <= 2; channel++) {

    // check if this channel has DSP
    char *dsp_name = g_strdup_printf("Line In %d DSP Capture Switch", channel);
    struct alsa_elem *dsp_elem = get_elem_by_name(card->elems, dsp_name);
    g_free(dsp_name);

    if (!dsp_elem)
      continue;

    // Pre-Comp stages (2 stages, default to highpass)
    for (int stage = 1; stage <= 2; stage++) {
      create_filter_stage_elems(
        card, "Pre-Comp", channel, stage, BIQUAD_TYPE_HIGHPASS, state
      );
    }

    // PEQ bands (3 bands, default to peaking)
    for (int stage = 1; stage <= 3; stage++) {
      create_filter_stage_elems(
        card, "PEQ", channel, stage, BIQUAD_TYPE_PEAKING, state
      );
    }
  }

  g_hash_table_destroy(state);
}

// Helper to get a filter parameter element
static struct alsa_elem *get_filter_param_elem(
  struct alsa_card *card,
  const char       *filter_type,
  int               channel,
  int               stage,
  const char       *param
) {
  char *name = get_filter_elem_name(filter_type, channel, stage, param);
  struct alsa_elem *elem = get_elem_by_name(card->elems, name);
  g_free(name);
  return elem;
}

struct alsa_elem *dsp_state_get_enable_elem(
  struct alsa_card *card,
  const char       *filter_type,
  int               channel,
  int               stage
) {
  return get_filter_param_elem(card, filter_type, channel, stage, "Enable");
}

struct alsa_elem *dsp_state_get_type_elem(
  struct alsa_card *card,
  const char       *filter_type,
  int               channel,
  int               stage
) {
  return get_filter_param_elem(card, filter_type, channel, stage, "Type");
}

struct alsa_elem *dsp_state_get_freq_elem(
  struct alsa_card *card,
  const char       *filter_type,
  int               channel,
  int               stage
) {
  return get_filter_param_elem(card, filter_type, channel, stage, "Freq");
}

struct alsa_elem *dsp_state_get_q_elem(
  struct alsa_card *card,
  const char       *filter_type,
  int               channel,
  int               stage
) {
  return get_filter_param_elem(card, filter_type, channel, stage, "Q");
}

struct alsa_elem *dsp_state_get_gain_elem(
  struct alsa_card *card,
  const char       *filter_type,
  int               channel,
  int               stage
) {
  return get_filter_param_elem(card, filter_type, channel, stage, "Gain");
}
