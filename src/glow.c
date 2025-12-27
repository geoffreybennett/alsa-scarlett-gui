// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <string.h>

#include "glow.h"

// calculate glow intensity (0 to 1) from dB level, with curve applied
double get_glow_intensity(double level_db) {
  if (level_db < GLOW_MIN_DB)
    return 0.0;

  double intensity = (level_db - GLOW_MIN_DB) / (GLOW_MAX_DB - GLOW_MIN_DB);
  if (intensity > 1.0)
    intensity = 1.0;

  // apply curve so glow ramps up more gradually
  return intensity * intensity;
}

// calculate glow layer width and alpha for a given layer and intensity
void get_glow_layer_params(
  int     layer,
  double  intensity,
  double *width,
  double *alpha
) {
  double layer_frac = (double)layer / (GLOW_LAYERS - 1);

  // width: minimum of 4 pixels, scaling up to GLOW_MAX_WIDTH
  // (routing line is 2 pixels, so glow must be > 2 to be visible)
  *width = 4.0 + (GLOW_MAX_WIDTH - 4.0) * intensity * (0.3 + 0.7 * layer_frac);

  // alpha: minimum of 0.08, scaling up with intensity
  *alpha = 0.08 + intensity * 0.32 * (1.0 - 0.7 * layer_frac);
}

// convert dB level to RGB colour (green → yellow → red)
void level_to_colour(double db, double *r, double *g, double *b) {
  if (db < -18.0) {
    // green
    *r = 0.0;
    *g = 1.0;
    *b = 0.0;
  } else if (db < -12.0) {
    // green → yellow-green
    double t = (db + 18.0) / 6.0;
    *r = 0.5 * t;
    *g = 1.0;
    *b = 0.0;
  } else if (db < -6.0) {
    // yellow-green → yellow
    double t = (db + 12.0) / 6.0;
    *r = 0.5 + 0.5 * t;
    *g = 1.0;
    *b = 0.0;
  } else if (db < -3.0) {
    // yellow → orange
    double t = (db + 6.0) / 3.0;
    *r = 1.0;
    *g = 1.0 - 0.25 * t;
    *b = 0.0;
  } else {
    // orange → red
    double t = fmin(1.0, (db + 3.0) / 3.0);
    *r = 1.0;
    *g = 0.75 - 0.75 * t;
    *b = 0.0;
  }
}

// compute the level meter index for a routing sink
static int compute_snk_level_index(
  struct alsa_card   *card,
  struct routing_snk *r_snk
) {
  if (!r_snk->elem)
    return -1;

  int cat = r_snk->elem->port_category;
  if (cat == PC_OFF)
    return -1;

  // if meter labels are available, search for matching "Sink" label
  if (card->level_meter_elem && card->level_meter_elem->meter_labels) {
    for (int i = 0; i < card->routing_levels_count; i++) {
      const char *label = card->level_meter_elem->meter_labels[i];
      if (!label)
        continue;

      if (strncmp(label, "Sink ", 5) != 0)
        continue;

      // label+5 is like "Analogue 1"; elem->name is like
      // "Analogue 1 Playback Enum"; match the label as a
      // complete word prefix (followed by space)
      const char *sink_name = label + 5;
      int sink_name_len = strlen(sink_name);
      if (strncmp(r_snk->elem->name, sink_name, sink_name_len) == 0 &&
          r_snk->elem->name[sink_name_len] == ' ')
        return i;
    }

    return -1;
  }

  // without labels, level meters are ordered:
  // HW Outputs, Mixer Inputs, DSP Inputs, PCM Inputs
  int index = 0;
  for (int c = PC_HW; c < cat; c++)
    index += card->routing_out_count[c];
  index += r_snk->elem->port_num;

  if (index >= card->routing_levels_count)
    return -1;

  return index;
}

// compute the level meter index for a routing source
static int compute_src_level_index(
  struct alsa_card   *card,
  struct routing_src *r_src
) {
  if (!card->level_meter_elem || !card->routing_levels ||
      r_src->port_category == PC_OFF)
    return -1;

  // if meter labels are available, search for matching "Source" label
  if (card->level_meter_elem->meter_labels) {
    for (int i = 0; i < card->routing_levels_count; i++) {
      const char *label = card->level_meter_elem->meter_labels[i];
      if (!label)
        continue;

      if (strncmp(label, "Source ", 7) != 0)
        continue;

      if (strcmp(label + 7, r_src->name) == 0)
        return i;
    }

    return -1;
  }

  // without labels, level meters are at sinks only; find a sink
  // connected to this source and return its level index
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (r_snk->effective_source_idx != r_src->id)
      continue;

    if (r_snk->level_index >= 0)
      return r_snk->level_index;
  }

  return -1;
}

// compute and cache level meter indices for all routing srcs/snks
void init_routing_level_indices(struct alsa_card *card) {

  // sinks first (sources may depend on them in the no-labels path)
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    r_snk->level_index = compute_snk_level_index(card, r_snk);
  }

  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    r_src->level_index = compute_src_level_index(card, r_src);
  }
}

// get the cached level meter index for a routing sink
int get_routing_snk_level_index(struct routing_snk *r_snk) {
  return r_snk->level_index;
}

// get level in dB for a routing source (-80 if no level data)
double get_routing_src_level_db(
  struct alsa_card   *card,
  struct routing_src *r_src
) {
  if (r_src->level_index < 0)
    return -80.0;

  return card->routing_levels[r_src->level_index];
}
