// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>
#include <alsa/asoundlib.h>
#include <string.h>

#include "stereo-link.h"
#include "config-io.h"
#include "config-monitor-groups.h"
#include "custom-names.h"
#include "device-port-names.h"
#include "optional-state.h"
#include "port-enable.h"
#include "window-mixer.h"
#include "window-routing.h"

// Callback structure to pass data to save callback
struct stereo_link_save_data {
  struct alsa_card *card;
  char             *config_key;
};

// Check if a source is the left channel of a potential stereo pair
// Left channels have odd lr_num (1, 3, 5, ...) or are 'A', 'C', 'E' for Mix
int is_src_left_channel(struct routing_src *src) {
  if (!src)
    return 0;

  // Mix outputs use letters (A=1, B=2, C=3, etc.)
  // Odd positions are left: A, C, E, ...
  return (src->lr_num % 2) == 1;
}

// Check if a sink is the left channel of a potential stereo pair
int is_snk_left_channel(struct routing_snk *snk) {
  if (!snk || !snk->elem)
    return 0;

  return snk->is_left;
}

// Get generic/constructed pair name for a source (e.g., "PCM 1–2")
// Used for fixed labels in I/O config window
char *get_src_generic_pair_name(struct routing_src *src) {
  if (!src)
    return g_strdup("");

  switch (src->port_category) {
    case PC_HW:
      return g_strdup_printf(
        "%s %d–%d", hw_type_names[src->hw_type], src->lr_num, src->lr_num + 1
      );

    case PC_PCM:
      return g_strdup_printf("PCM %d–%d", src->lr_num, src->lr_num + 1);

    case PC_MIX:
      return g_strdup_printf("Mix %c–%c", src->port_num + 'A', src->port_num + 'B');

    case PC_DSP:
      return g_strdup_printf("DSP %d–%d", src->lr_num, src->lr_num + 1);

    default:
      return g_strdup("");
  }
}

// Get generic/constructed pair name for a sink (e.g., "PCM 1–2")
// Used for fixed labels in I/O config window
char *get_snk_generic_pair_name(struct routing_snk *snk) {
  if (!snk || !snk->elem)
    return g_strdup("");

  struct alsa_elem *elem = snk->elem;

  switch (elem->port_category) {
    case PC_HW:
      return g_strdup_printf(
        "%s %d–%d", hw_type_names[elem->hw_type], elem->lr_num, elem->lr_num + 1
      );

    case PC_PCM:
      return g_strdup_printf("PCM %d–%d", elem->lr_num, elem->lr_num + 1);

    case PC_MIX:
      return g_strdup_printf("Mixer %d–%d", elem->lr_num, elem->lr_num + 1);

    case PC_DSP:
      return g_strdup_printf("DSP %d–%d", elem->lr_num, elem->lr_num + 1);

    default:
      return g_strdup("");
  }
}

// Get default pair name for a source (device-specific or constructed)
// Used for display in routing window when no custom name is set
char *get_src_default_pair_name(struct routing_src *src) {
  if (!src || !src->card)
    return g_strdup("");

  // Check for device-specific pair name first
  int pair_num = (src->lr_num - 1) / 2;
  const char *device_name = get_device_pair_name(
    src->card->pid, src->port_category, src->hw_type, 0, pair_num
  );
  if (device_name)
    return g_strdup(device_name);

  // Fall back to constructed name
  return get_src_generic_pair_name(src);
}

// Get default pair name for a sink (device-specific or constructed)
// Used for display in routing window when no custom name is set
char *get_snk_default_pair_name(struct routing_snk *snk) {
  if (!snk || !snk->elem)
    return g_strdup("");

  struct alsa_elem *elem = snk->elem;

  // Check for device-specific pair name first
  int pair_num = (elem->lr_num - 1) / 2;
  const char *device_name = get_device_pair_name(
    elem->card->pid, elem->port_category, elem->hw_type, 1, pair_num
  );
  if (device_name)
    return g_strdup(device_name);

  // Fall back to constructed name
  return get_snk_generic_pair_name(snk);
}

// Find the partner source (the other channel of a stereo pair)
// Uses cached pointer if available, otherwise searches and caches
struct routing_src *get_src_partner(struct routing_src *src) {
  if (!src)
    return NULL;

  // Use cached pointer if available
  if (src->partner)
    return src->partner;

  if (!src->card)
    return NULL;

  struct alsa_card *card = src->card;
  int partner_lr_num = is_src_left_channel(src)
    ? src->lr_num + 1
    : src->lr_num - 1;

  // Search for partner in routing sources
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *other = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    // Must be same category and hw_type
    if (other->port_category != src->port_category)
      continue;
    if (src->port_category == PC_HW && other->hw_type != src->hw_type)
      continue;

    if (other->lr_num == partner_lr_num) {
      // Cache bidirectionally
      src->partner = other;
      other->partner = src;
      return other;
    }
  }

  return NULL;
}

// Find the partner sink
// Uses cached pointer if available, otherwise searches and caches
struct routing_snk *get_snk_partner(struct routing_snk *snk) {
  if (!snk)
    return NULL;

  // Use cached pointer if available
  if (snk->partner)
    return snk->partner;

  if (!snk->elem || !snk->elem->card)
    return NULL;

  struct alsa_card *card = snk->elem->card;
  struct alsa_elem *elem = snk->elem;
  int partner_lr_num = is_snk_left_channel(snk)
    ? elem->lr_num + 1
    : elem->lr_num - 1;

  // Search for partner in routing sinks
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *other = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (!other->elem)
      continue;

    // Must be same category and hw_type
    if (other->elem->port_category != elem->port_category)
      continue;
    if (elem->port_category == PC_HW && other->elem->hw_type != elem->hw_type)
      continue;

    if (other->elem->lr_num == partner_lr_num) {
      // Cache bidirectionally
      snk->partner = other;
      other->partner = snk;
      return other;
    }
  }

  return NULL;
}

// Check if source has a valid partner for stereo linking
int src_has_valid_partner(struct routing_src *src) {
  return get_src_partner(src) != NULL;
}

// Check if sink has a valid partner for stereo linking
int snk_has_valid_partner(struct routing_snk *snk) {
  return get_snk_partner(snk) != NULL;
}

// Get the left channel source of a pair
static struct routing_src *get_src_left_of_pair(struct routing_src *src) {
  if (!src)
    return NULL;

  if (is_src_left_channel(src))
    return src;

  return get_src_partner(src);
}

// Get the left channel sink of a pair
static struct routing_snk *get_snk_left_of_pair(struct routing_snk *snk) {
  if (!snk)
    return NULL;

  if (is_snk_left_channel(snk))
    return snk;

  return get_snk_partner(snk);
}

// Check if a routing source is linked
int is_src_linked(struct routing_src *src) {
  if (!src)
    return 0;

  // Get the left channel of this pair
  struct routing_src *left = get_src_left_of_pair(src);
  if (!left)
    return 0;

  // Link element is stored on left channel
  struct alsa_elem *link_elem = left->link_elem;
  if (!link_elem)
    return 0;

  return alsa_get_elem_value(link_elem) != 0;
}

// Check if a routing sink is linked
int is_snk_linked(struct routing_snk *snk) {
  if (!snk)
    return 0;

  // Get the left channel of this pair
  struct routing_snk *left = get_snk_left_of_pair(snk);
  if (!left)
    return 0;

  // Link element is stored on left channel
  struct alsa_elem *link_elem = left->link_elem;
  if (!link_elem)
    return 0;

  return alsa_get_elem_value(link_elem) != 0;
}

// Check if source should be displayed (not hidden as R of linked pair)
int should_display_src(struct routing_src *src) {
  if (!src)
    return 0;

  // If not linked, always display
  if (!is_src_linked(src))
    return 1;

  // If linked, only display the left channel
  return is_src_left_channel(src);
}

// Check if sink should be displayed (not hidden as R of linked pair)
int should_display_snk(struct routing_snk *snk) {
  if (!snk)
    return 0;

  // If not linked, always display
  if (!is_snk_linked(snk))
    return 1;

  // If linked, only display the left channel
  return is_snk_left_channel(snk);
}

// Get link element for a source (stored on left channel)
struct alsa_elem *get_src_link_elem(struct routing_src *src) {
  struct routing_src *left = get_src_left_of_pair(src);
  return left ? left->link_elem : NULL;
}

// Get link element for a sink (stored on left channel)
struct alsa_elem *get_snk_link_elem(struct routing_snk *snk) {
  struct routing_snk *left = get_snk_left_of_pair(snk);
  return left ? left->link_elem : NULL;
}

// Get pair name element for a source (stored on left channel)
struct alsa_elem *get_src_pair_name_elem(struct routing_src *src) {
  struct routing_src *left = get_src_left_of_pair(src);
  return left ? left->pair_name_elem : NULL;
}

// Get pair name element for a sink (stored on left channel)
struct alsa_elem *get_snk_pair_name_elem(struct routing_snk *snk) {
  struct routing_snk *left = get_snk_left_of_pair(snk);
  return left ? left->pair_name_elem : NULL;
}

// Free stereo link callback data
static void stereo_link_free_callback_data(void *data) {
  if (!data)
    return;

  struct stereo_link_save_data *save_data = data;
  g_free(save_data->config_key);
  g_free(save_data);
}

// Callback when a link state value changes - save to state file
static void link_state_changed(struct alsa_elem *elem, void *private) {
  struct stereo_link_save_data *data = private;

  long value = alsa_get_elem_value(elem);

  optional_state_save(
    data->card, CONFIG_SECTION_CONTROLS, data->config_key, value ? "1" : "0"
  );
}

// Callback when a pair name value changes - save to state file
static void pair_name_changed(struct alsa_elem *elem, void *private) {
  struct stereo_link_save_data *data = private;

  size_t size;
  const void *bytes = alsa_get_elem_bytes(elem, &size);

  if (bytes && size > 0) {
    size_t str_len = strnlen((const char *)bytes, size);

    if (str_len > 0 && g_utf8_validate((const char *)bytes, str_len, NULL)) {
      char *str = g_strndup((const char *)bytes, str_len);
      optional_state_save(
        data->card, CONFIG_SECTION_CONTROLS, data->config_key, str
      );
      g_free(str);
    } else {
      optional_state_save(
        data->card, CONFIG_SECTION_CONTROLS, data->config_key, ""
      );
    }
  } else {
    optional_state_save(
      data->card, CONFIG_SECTION_CONTROLS, data->config_key, ""
    );
  }
}

// Schedule UI update for stereo link changes
void schedule_stereo_link_ui_update(struct alsa_card *card) {
  if (!card)
    return;

  // Routing updates done synchronously to avoid flicker
  update_routing_section_visibility(card);
  if (card->routing_lines)
    gtk_widget_queue_draw(card->routing_lines);

  // Schedule expensive mixer grid rebuild at idle
  schedule_ui_update(card, PENDING_UI_UPDATE_MIXER_GRID);
}

// Sync enable state when linking: right channel = left channel (left wins)
static void sync_enable_on_link(
  struct alsa_elem *left_enable,
  struct alsa_elem *right_enable
) {
  if (!left_enable || !right_enable)
    return;

  long left_val = alsa_get_elem_value(left_enable);
  alsa_set_elem_value(right_enable, left_val);
}

// Callback to sync source enable state while linked
// When left channel enable changes, update right channel to match
static void src_enable_sync_callback(struct alsa_elem *elem, void *private) {
  struct routing_src *src_l = private;

  // Only sync if linked
  if (!is_src_linked(src_l))
    return;

  struct routing_src *src_r = get_src_partner(src_l);
  if (!src_r || !src_r->enable_elem)
    return;

  long left_val = alsa_get_elem_value(elem);
  long right_val = alsa_get_elem_value(src_r->enable_elem);

  // Only update if different to avoid infinite loops
  if (left_val != right_val)
    alsa_set_elem_value(src_r->enable_elem, left_val);
}

// Callback to sync sink enable state while linked
// When left channel enable changes, update right channel to match
static void snk_enable_sync_callback(struct alsa_elem *elem, void *private) {
  struct routing_snk *snk_l = private;

  // Only sync if linked
  if (!is_snk_linked(snk_l))
    return;

  struct routing_snk *snk_r = get_snk_partner(snk_l);
  if (!snk_r || !snk_r->enable_elem)
    return;

  long left_val = alsa_get_elem_value(elem);
  long right_val = alsa_get_elem_value(snk_r->enable_elem);

  // Only update if different to avoid infinite loops
  if (left_val != right_val)
    alsa_set_elem_value(snk_r->enable_elem, left_val);
}

// Check if two sources form a valid stereo routing connection
// Valid: src_l → snk_l AND src_r → snk_r, where both pairs are adjacent
static int is_valid_stereo_src_connection(
  struct routing_src *src_l,
  struct routing_src *src_r,
  struct routing_snk *snk_l,
  struct routing_snk *snk_r
) {
  if (!src_l || !src_r || !snk_l || !snk_r)
    return 0;

  // Check that sink pair is valid (adjacent, same type)
  struct routing_snk *snk_l_partner = get_snk_partner(snk_l);
  if (snk_l_partner != snk_r)
    return 0;

  // Check L→L and R→R connection
  int src_l_routes_to_snk_l = (snk_l->effective_source_idx == src_l->id);
  int src_r_routes_to_snk_r = (snk_r->effective_source_idx == src_r->id);

  return src_l_routes_to_snk_l && src_r_routes_to_snk_r;
}

// Find all sinks receiving from a source pair and validate routing
static void validate_src_pair_routing(
  struct routing_src *src_l,
  struct routing_src *src_r
) {
  if (!src_l || !src_r)
    return;

  struct alsa_card *card = src_l->card;

  // Find all sinks connected to either src_l or src_r
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    int routes_to_l = (snk->effective_source_idx == src_l->id);
    int routes_to_r = (snk->effective_source_idx == src_r->id);

    if (!routes_to_l && !routes_to_r)
      continue;

    // Check if this forms a valid stereo connection
    struct routing_snk *snk_partner = get_snk_partner(snk);

    if (!snk_partner) {
      // Sink has no partner - clear routing (stereo source can't route to mono)
      alsa_set_elem_value(snk->elem, 0);
      continue;
    }

    // Check if partner has correct routing
    int snk_is_left = is_snk_left_channel(snk);
    struct routing_snk *snk_l = snk_is_left ? snk : snk_partner;
    struct routing_snk *snk_r = snk_is_left ? snk_partner : snk;

    if (!is_valid_stereo_src_connection(src_l, src_r, snk_l, snk_r)) {
      // Invalid routing - clear both channels
      alsa_set_elem_value(snk_l->elem, 0);
      alsa_set_elem_value(snk_r->elem, 0);
    } else {
      // Valid stereo connection - propagate link to sink pair
      struct alsa_elem *snk_link_elem = get_snk_link_elem(snk_l);
      if (snk_link_elem && !alsa_get_elem_value(snk_link_elem))
        alsa_set_elem_value(snk_link_elem, 1);
    }
  }
}

// Validate routing and propagate link when a source pair is linked
static void validate_and_propagate_src_link(struct routing_src *src) {
  if (!src)
    return;

  struct routing_src *src_l = get_src_left_of_pair(src);
  if (!src_l)
    return;

  struct routing_src *src_r = get_src_partner(src_l);
  if (!src_r)
    return;

  // Sync enable state: right = left
  if (src_l->enable_elem && src_r->enable_elem)
    sync_enable_on_link(src_l->enable_elem, src_r->enable_elem);

  // Validate and propagate routing
  validate_src_pair_routing(src_l, src_r);

  // Schedule UI update
  schedule_stereo_link_ui_update(src_l->card);
}

// Check if sources routed to a sink pair form a valid stereo connection
static void validate_snk_pair_routing(
  struct routing_snk *snk_l,
  struct routing_snk *snk_r
) {
  if (!snk_l || !snk_r || !snk_l->elem || !snk_r->elem)
    return;

  struct alsa_card *card = snk_l->elem->card;

  int src_l_idx = snk_l->effective_source_idx;
  int src_r_idx = snk_r->effective_source_idx;

  // If either is Off, clear both
  if (src_l_idx == 0 || src_r_idx == 0) {
    alsa_set_elem_value(snk_l->elem, 0);
    alsa_set_elem_value(snk_r->elem, 0);
    return;
  }

  // Get the sources
  if (src_l_idx >= card->routing_srcs->len ||
      src_r_idx >= card->routing_srcs->len) {
    alsa_set_elem_value(snk_l->elem, 0);
    alsa_set_elem_value(snk_r->elem, 0);
    return;
  }

  // Both connected to the same source - valid mono→stereo
  if (src_l_idx == src_r_idx)
    return;

  struct routing_src *src_l = &g_array_index(
    card->routing_srcs, struct routing_src, src_l_idx
  );
  struct routing_src *src_r = &g_array_index(
    card->routing_srcs, struct routing_src, src_r_idx
  );

  // Check if sources form a valid adjacent pair
  struct routing_src *src_l_partner = get_src_partner(src_l);

  if (src_l_partner != src_r ||
      !is_src_left_channel(src_l) ||
      is_src_left_channel(src_r)) {
    // Invalid - clear both
    alsa_set_elem_value(snk_l->elem, 0);
    alsa_set_elem_value(snk_r->elem, 0);
    return;
  }

  // Valid stereo connection - propagate link to source pair
  struct alsa_elem *src_link_elem = get_src_link_elem(src_l);
  if (src_link_elem && !alsa_get_elem_value(src_link_elem))
    alsa_set_elem_value(src_link_elem, 1);
}

// Validate routing and propagate link when a sink pair is linked
static void validate_and_propagate_snk_link(struct routing_snk *snk) {
  if (!snk)
    return;

  struct routing_snk *snk_l = get_snk_left_of_pair(snk);
  if (!snk_l)
    return;

  struct routing_snk *snk_r = get_snk_partner(snk_l);
  if (!snk_r)
    return;

  // Sync enable state: right = left
  if (snk_l->enable_elem && snk_r->enable_elem)
    sync_enable_on_link(snk_l->enable_elem, snk_r->enable_elem);

  // Validate and propagate routing
  validate_snk_pair_routing(snk_l, snk_r);

  // Schedule UI update
  schedule_stereo_link_ui_update(snk_l->elem->card);
}

// Propagate unlink from one end to the other
static void propagate_src_unlink(struct routing_src *src) {
  if (!src)
    return;

  struct routing_src *src_l = get_src_left_of_pair(src);
  if (!src_l)
    return;

  struct routing_src *src_r = get_src_partner(src_l);
  if (!src_r)
    return;

  struct alsa_card *card = src_l->card;

  // Find all sinks that have L→L, R→R connections and unlink them
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (snk->effective_source_idx != src_l->id &&
        snk->effective_source_idx != src_r->id)
      continue;

    struct routing_snk *snk_partner = get_snk_partner(snk);
    if (!snk_partner)
      continue;

    struct routing_snk *snk_l = is_snk_left_channel(snk) ? snk : snk_partner;
    struct routing_snk *snk_r = is_snk_left_channel(snk) ? snk_partner : snk;

    if (is_valid_stereo_src_connection(src_l, src_r, snk_l, snk_r)) {
      // Unlink the sink pair (keep routing)
      struct alsa_elem *snk_link_elem = get_snk_link_elem(snk_l);
      if (snk_link_elem && alsa_get_elem_value(snk_link_elem))
        alsa_set_elem_value(snk_link_elem, 0);
    }
  }

  schedule_stereo_link_ui_update(card);
}

static void propagate_snk_unlink(struct routing_snk *snk) {
  if (!snk || !snk->elem)
    return;

  struct routing_snk *snk_l = get_snk_left_of_pair(snk);
  if (!snk_l)
    return;

  struct routing_snk *snk_r = get_snk_partner(snk_l);
  if (!snk_r)
    return;

  struct alsa_card *card = snk_l->elem->card;

  int src_l_idx = snk_l->effective_source_idx;
  int src_r_idx = snk_r->effective_source_idx;

  if (src_l_idx == 0 || src_r_idx == 0 ||
      src_l_idx >= card->routing_srcs->len ||
      src_r_idx >= card->routing_srcs->len)
    return;

  struct routing_src *src_l = &g_array_index(
    card->routing_srcs, struct routing_src, src_l_idx
  );
  struct routing_src *src_r = &g_array_index(
    card->routing_srcs, struct routing_src, src_r_idx
  );

  // Check if sources form a valid pair
  if (get_src_partner(src_l) == src_r && is_src_left_channel(src_l)) {
    // Unlink the source pair (keep routing)
    struct alsa_elem *src_link_elem = get_src_link_elem(src_l);
    if (src_link_elem && alsa_get_elem_value(src_link_elem))
      alsa_set_elem_value(src_link_elem, 0);
  }

  schedule_stereo_link_ui_update(card);
}

static long get_mixer_gain_min_val(struct alsa_card *card) {
  struct alsa_elem *e = card->mixer_gains[0][0];
  return e ? e->min_val : 0;
}

// Average mixer gain values when mixer inputs are linked (PC_MIX sinks)
// When inputs 1-2 are linked, for each mix output (A, B, ...):
// - If output is also linked (stereo→stereo): average diagonal, zero off-diagonal
// - Otherwise (stereo→mono): average both inputs, set both to average
static void average_mixer_input_gains(
  struct alsa_card   *card,
  struct routing_snk *snk_l,
  struct routing_snk *snk_r
) {
  if (!card || !snk_l || !snk_r || !snk_l->elem || !snk_r->elem)
    return;

  // Only for mixer inputs
  if (snk_l->elem->port_category != PC_MIX)
    return;

  int in_l = snk_l->elem->lr_num - 1;
  int in_r = snk_r->elem->lr_num - 1;
  long min_val = get_mixer_gain_min_val(card);

  // For each mixer output, check if it's part of a linked pair
  for (int mix = 0; mix < card->routing_in_count[PC_MIX]; mix++) {
    struct routing_src *mix_src = NULL;
    for (int j = 0; j < card->routing_srcs->len; j++) {
      struct routing_src *s = &g_array_index(
        card->routing_srcs, struct routing_src, j
      );
      if (s->port_category == PC_MIX && s->port_num == mix) {
        mix_src = s;
        break;
      }
    }

    int output_linked = mix_src && is_src_linked(mix_src);

    if (output_linked && is_src_left_channel(mix_src)) {
      // Stereo→Stereo: average diagonal, zero off-diagonal
      struct routing_src *mix_src_r = get_src_partner(mix_src);
      if (!mix_src_r)
        continue;

      int mix_r = mix_src_r->port_num;

      // Diagonal: Mix L Input L, Mix R Input R → average
      struct alsa_elem *diag_ll = card->mixer_gains[mix][in_l];
      struct alsa_elem *diag_rr = card->mixer_gains[mix_r][in_r];

      if (diag_ll && diag_rr) {
        long avg = (alsa_get_elem_value(diag_ll) +
                    alsa_get_elem_value(diag_rr)) / 2;
        alsa_set_elem_value(diag_ll, avg);
        alsa_set_elem_value(diag_rr, avg);
      }

      // Off-diagonal: Mix L Input R, Mix R Input L → zero
      struct alsa_elem *off_lr = card->mixer_gains[mix][in_r];
      if (off_lr)
        alsa_set_elem_value(off_lr, min_val);

      struct alsa_elem *off_rl = card->mixer_gains[mix_r][in_l];
      if (off_rl)
        alsa_set_elem_value(off_rl, min_val);

    } else if (!output_linked) {
      // Stereo→Mono: take same-side diagonal value
      struct alsa_elem *elem_l = card->mixer_gains[mix][in_l];
      struct alsa_elem *elem_r = card->mixer_gains[mix][in_r];

      if (!elem_l || !elem_r)
        continue;

      // Even output index = left of pair → keep left input value
      // Odd output index = right of pair → keep right input value
      long val = mix % 2 == 0
        ? alsa_get_elem_value(elem_l)
        : alsa_get_elem_value(elem_r);
      alsa_set_elem_value(elem_l, val);
      alsa_set_elem_value(elem_r, val);
    }
    // Skip R channel of linked output pair (handled above with L)
  }
}

// Average mixer gain values when mixer outputs are linked (PC_MIX sources)
// When Mix A-B are linked, for each mixer input (1, 2, ...):
// - If input is also linked (stereo→stereo): average diagonal, zero off-diagonal
// - Otherwise (mono→stereo): average both outputs, set both to average
static void average_mixer_output_gains(
  struct alsa_card   *card,
  struct routing_src *src_l,
  struct routing_src *src_r
) {
  if (!card || !src_l || !src_r)
    return;

  // Only for mixer outputs
  if (src_l->port_category != PC_MIX)
    return;

  int mix_l = src_l->port_num;
  int mix_r = src_r->port_num;
  long min_val = get_mixer_gain_min_val(card);

  // For each mixer input, check if it's part of a linked pair
  for (int in = 0; in < card->routing_out_count[PC_MIX]; in++) {
    struct routing_snk *r_snk = NULL;
    for (int j = 0; j < card->routing_snks->len; j++) {
      struct routing_snk *s = &g_array_index(
        card->routing_snks, struct routing_snk, j
      );
      if (s->elem && s->elem->port_category == PC_MIX &&
          s->elem->lr_num == in + 1) {
        r_snk = s;
        break;
      }
    }

    int input_linked = r_snk && is_snk_linked(r_snk);

    if (input_linked && is_snk_left_channel(r_snk)) {
      // Stereo→Stereo: average diagonal, zero off-diagonal
      struct routing_snk *r_snk_r = get_snk_partner(r_snk);
      if (!r_snk_r || !r_snk_r->elem)
        continue;

      int in_r = r_snk_r->elem->lr_num - 1;

      // Diagonal: Mix L Input L, Mix R Input R → average
      struct alsa_elem *diag_ll = card->mixer_gains[mix_l][in];
      struct alsa_elem *diag_rr = card->mixer_gains[mix_r][in_r];

      if (diag_ll && diag_rr) {
        long avg = (alsa_get_elem_value(diag_ll) +
                    alsa_get_elem_value(diag_rr)) / 2;
        alsa_set_elem_value(diag_ll, avg);
        alsa_set_elem_value(diag_rr, avg);
      }

      // Off-diagonal: Mix L Input R, Mix R Input L → zero
      struct alsa_elem *off_lr = card->mixer_gains[mix_l][in_r];
      if (off_lr)
        alsa_set_elem_value(off_lr, min_val);

      struct alsa_elem *off_rl = card->mixer_gains[mix_r][in];
      if (off_rl)
        alsa_set_elem_value(off_rl, min_val);

    } else if (!input_linked) {
      // Mono→Stereo: take same-side diagonal value
      struct alsa_elem *elem_l = card->mixer_gains[mix_l][in];
      struct alsa_elem *elem_r = card->mixer_gains[mix_r][in];

      if (!elem_l || !elem_r)
        continue;

      // Odd input index (1-based) = left of pair → keep left output
      // Even input index = right of pair → keep right output
      long val = !(in % 2)
        ? alsa_get_elem_value(elem_l)
        : alsa_get_elem_value(elem_r);
      alsa_set_elem_value(elem_l, val);
      alsa_set_elem_value(elem_r, val);
    }
    // Skip R channel of linked input pair (handled above with L)
  }
}

// Distribute mixer gains when mixer outputs are unlinked.
// For each linked input pair, copy the diagonal value into the
// off-diagonal slot of each output column so that the stereo→mono
// widgets inherit the correct gain.
static void distribute_mixer_output_gains(
  struct alsa_card   *card,
  struct routing_src *src_l,
  struct routing_src *src_r
) {
  if (!card || !src_l || !src_r)
    return;

  if (src_l->port_category != PC_MIX)
    return;

  int mix_l = src_l->port_num;
  int mix_r = src_r->port_num;

  for (int in = 0; in < card->routing_out_count[PC_MIX]; in++) {
    struct routing_snk *r_snk = NULL;
    for (int j = 0; j < card->routing_snks->len; j++) {
      struct routing_snk *s = &g_array_index(
        card->routing_snks, struct routing_snk, j
      );
      if (s->elem && s->elem->port_category == PC_MIX &&
          s->elem->lr_num == in + 1) {
        r_snk = s;
        break;
      }
    }

    if (!r_snk || !is_snk_linked(r_snk) || !is_snk_left_channel(r_snk))
      continue;

    struct routing_snk *r_snk_r = get_snk_partner(r_snk);
    if (!r_snk_r || !r_snk_r->elem)
      continue;

    int in_r = r_snk_r->elem->lr_num - 1;

    // L-L diagonal → fill left column
    struct alsa_elem *diag_ll = card->mixer_gains[mix_l][in];
    struct alsa_elem *off_lr = card->mixer_gains[mix_l][in_r];

    if (diag_ll && off_lr)
      alsa_set_elem_value(off_lr, alsa_get_elem_value(diag_ll));

    // R-R diagonal → fill right column
    struct alsa_elem *diag_rr = card->mixer_gains[mix_r][in_r];
    struct alsa_elem *off_rl = card->mixer_gains[mix_r][in];

    if (diag_rr && off_rl)
      alsa_set_elem_value(off_rl, alsa_get_elem_value(diag_rr));
  }
}

// Distribute mixer gains when mixer inputs are unlinked.
// For each linked output pair, copy the diagonal value into the
// off-diagonal slot of each input row so that the mono→stereo
// widgets inherit the correct gain.
static void distribute_mixer_input_gains(
  struct alsa_card   *card,
  struct routing_snk *snk_l,
  struct routing_snk *snk_r
) {
  if (!card || !snk_l || !snk_r || !snk_l->elem || !snk_r->elem)
    return;

  if (snk_l->elem->port_category != PC_MIX)
    return;

  int in_l = snk_l->elem->lr_num - 1;
  int in_r = snk_r->elem->lr_num - 1;

  for (int mix = 0; mix < card->routing_in_count[PC_MIX]; mix++) {
    struct routing_src *mix_src = NULL;
    for (int j = 0; j < card->routing_srcs->len; j++) {
      struct routing_src *s = &g_array_index(
        card->routing_srcs, struct routing_src, j
      );
      if (s->port_category == PC_MIX && s->port_num == mix) {
        mix_src = s;
        break;
      }
    }

    if (!mix_src || !is_src_linked(mix_src) ||
        !is_src_left_channel(mix_src))
      continue;

    struct routing_src *mix_src_r = get_src_partner(mix_src);
    if (!mix_src_r)
      continue;

    int mix_r = mix_src_r->port_num;

    // L-L diagonal → fill left input row
    struct alsa_elem *diag_ll = card->mixer_gains[mix][in_l];
    struct alsa_elem *off_rl = card->mixer_gains[mix_r][in_l];

    if (diag_ll && off_rl)
      alsa_set_elem_value(off_rl, alsa_get_elem_value(diag_ll));

    // R-R diagonal → fill right input row
    struct alsa_elem *diag_rr = card->mixer_gains[mix_r][in_r];
    struct alsa_elem *off_lr = card->mixer_gains[mix][in_r];

    if (diag_rr && off_lr)
      alsa_set_elem_value(off_lr, alsa_get_elem_value(diag_rr));
  }
}

// Reverse-lookup: find monitor group enum value for a routing_src index
static int monitor_group_enum_for_src(
  struct alsa_card *card,
  int               routing_src_idx
) {
  for (int i = 0; i < card->monitor_group_src_map_count; i++) {
    if (card->monitor_group_src_map[i] == routing_src_idx)
      return i;
  }
  return -1;
}

// Apply left-wins-with-partner logic to a monitor group source pair.
// Sets the enum values for both L and R source elements based on the
// left channel's current source, accounting for stereo-linked sources.
static void sync_monitor_group_source_pair(
  struct alsa_card *card,
  struct alsa_elem *source_l,
  struct alsa_elem *source_r
) {
  if (!source_l || !source_r)
    return;
  if (!card->monitor_group_src_map)
    return;

  int enum_val = alsa_get_elem_value(source_l);
  if (enum_val < 0 || enum_val >= card->monitor_group_src_map_count)
    return;

  int src_idx = card->monitor_group_src_map[enum_val];
  if (src_idx <= 0 || src_idx >= card->routing_srcs->len) {
    // Invalid or Off — set both to same
    alsa_set_elem_value(source_r, enum_val);
    return;
  }

  struct routing_src *src = &g_array_index(
    card->routing_srcs, struct routing_src, src_idx
  );

  struct routing_src *partner = get_src_partner(src);

  if (!partner || !is_src_linked(src)) {
    // Mono source — both get the same value
    alsa_set_elem_value(source_r, enum_val);
    return;
  }

  // Source is part of a linked pair
  struct routing_src *left_src = is_src_left_channel(src) ? src : partner;
  struct routing_src *right_src = is_src_left_channel(src) ? partner : src;

  int left_enum = monitor_group_enum_for_src(card, left_src->id);
  int right_enum = monitor_group_enum_for_src(card, right_src->id);

  if (left_enum < 0 || right_enum < 0) {
    // Can't find partner enum — fall back to same value
    alsa_set_elem_value(source_r, enum_val);
    return;
  }

  alsa_set_elem_value(source_l, left_enum);
  alsa_set_elem_value(source_r, right_enum);
}

// Sync monitor group settings when outputs are linked.
// Left channel values win for enable/source; trim is averaged.
static void sync_monitor_groups_on_link(
  struct routing_snk *snk_l,
  struct routing_snk *snk_r
) {
  if (!snk_l || !snk_r || !snk_l->elem)
    return;

  struct alsa_card *card = snk_l->elem->card;

  // Enable: left wins → write to right
  if (snk_l->main_group_switch && snk_r->main_group_switch) {
    long val = alsa_get_elem_value(snk_l->main_group_switch);
    alsa_set_elem_value(snk_r->main_group_switch, val);
  }
  if (snk_l->alt_group_switch && snk_r->alt_group_switch) {
    long val = alsa_get_elem_value(snk_l->alt_group_switch);
    alsa_set_elem_value(snk_r->alt_group_switch, val);
  }

  // Source: left wins with partner assignment
  sync_monitor_group_source_pair(
    card, snk_l->main_group_source, snk_r->main_group_source
  );
  sync_monitor_group_source_pair(
    card, snk_l->alt_group_source, snk_r->alt_group_source
  );

  // Trim: average L/R → write average to both
  if (snk_l->main_group_trim && snk_r->main_group_trim) {
    long avg = (alsa_get_elem_value(snk_l->main_group_trim) +
                alsa_get_elem_value(snk_r->main_group_trim)) / 2;
    alsa_set_elem_value(snk_l->main_group_trim, avg);
    alsa_set_elem_value(snk_r->main_group_trim, avg);
  }
  if (snk_l->alt_group_trim && snk_r->alt_group_trim) {
    long avg = (alsa_get_elem_value(snk_l->alt_group_trim) +
                alsa_get_elem_value(snk_r->alt_group_trim)) / 2;
    alsa_set_elem_value(snk_l->alt_group_trim, avg);
    alsa_set_elem_value(snk_r->alt_group_trim, avg);
  }
}

// Monitor group sync: keep left/right elements in lockstep while
// linked.  A single callback handles both directions — left always
// wins.  For SOURCE type, uses stereo-aware enum assignment instead
// of plain value copy.
enum mg_sync_type { MG_SYNC_VALUE, MG_SYNC_SOURCE };

struct mg_sync_data {
  struct routing_snk *snk_l;
  struct alsa_elem   *left_elem;
  struct alsa_elem   *right_elem;
  enum mg_sync_type   type;
};

static void mg_sync_callback(struct alsa_elem *elem, void *private) {
  (void)elem;
  struct mg_sync_data *data = private;

  if (!is_snk_linked(data->snk_l))
    return;

  if (data->type == MG_SYNC_SOURCE) {
    struct alsa_card *card = data->snk_l->elem->card;
    sync_monitor_group_source_pair(
      card, data->left_elem, data->right_elem
    );
  } else {
    long left_val = alsa_get_elem_value(data->left_elem);
    if (alsa_get_elem_value(data->right_elem) != left_val)
      alsa_set_elem_value(data->right_elem, left_val);
  }
}

// Register a sync callback on both elements of a left/right pair.
// The first registration owns the data (frees on cleanup).
static void register_mg_sync_pair(
  struct routing_snk *snk_l,
  struct alsa_elem   *left_elem,
  struct alsa_elem   *right_elem,
  enum mg_sync_type   type
) {
  if (!left_elem || !right_elem)
    return;

  struct mg_sync_data *data = g_malloc(sizeof(struct mg_sync_data));
  data->snk_l = snk_l;
  data->left_elem = left_elem;
  data->right_elem = right_elem;
  data->type = type;

  alsa_elem_add_callback(left_elem, mg_sync_callback, data, g_free);
  alsa_elem_add_callback(right_elem, mg_sync_callback, data, NULL);
}

// Register monitor group sync callbacks for a HW analogue sink pair.
// Called from create_snk_stereo_link_elems for left channel sinks.
static void register_monitor_group_sync_callbacks(
  struct routing_snk *snk_l
) {
  struct routing_snk *snk_r = get_snk_partner(snk_l);
  if (!snk_r)
    return;

  register_mg_sync_pair(
    snk_l, snk_l->main_group_switch, snk_r->main_group_switch,
    MG_SYNC_VALUE
  );
  register_mg_sync_pair(
    snk_l, snk_l->alt_group_switch, snk_r->alt_group_switch,
    MG_SYNC_VALUE
  );
  register_mg_sync_pair(
    snk_l, snk_l->main_group_source, snk_r->main_group_source,
    MG_SYNC_SOURCE
  );
  register_mg_sync_pair(
    snk_l, snk_l->alt_group_source, snk_r->alt_group_source,
    MG_SYNC_SOURCE
  );
  register_mg_sync_pair(
    snk_l, snk_l->main_group_trim, snk_r->main_group_trim,
    MG_SYNC_VALUE
  );
  register_mg_sync_pair(
    snk_l, snk_l->alt_group_trim, snk_r->alt_group_trim,
    MG_SYNC_VALUE
  );
}

// Callback when source link state changes
static void src_link_state_changed(struct alsa_elem *elem, void *private) {
  struct routing_src *src = private;
  struct routing_src *src_l = get_src_left_of_pair(src);
  struct routing_src *src_r = src_l ? get_src_partner(src_l) : NULL;

  if (alsa_get_elem_value(elem)) {
    validate_and_propagate_src_link(src);

    // Average mixer gains when mixer outputs are linked
    if (src_l && src_r && src_l->port_category == PC_MIX)
      average_mixer_output_gains(src_l->card, src_l, src_r);
  } else {
    // Distribute mixer gains before unlinking
    if (src_l && src_r && src_l->port_category == PC_MIX)
      distribute_mixer_output_gains(src_l->card, src_l, src_r);

    propagate_src_unlink(src);
  }

  // Recreate mixer widgets when mixer outputs are linked/unlinked
  if (src_l && src_l->port_category == PC_MIX)
    recreate_mixer_widgets(src_l->card);

  // Trigger visibility callbacks via enable_elem change notifications
  // The visibility callback in port-enable.c is the single source of truth
  if (src_l && src_l->enable_elem)
    alsa_elem_change(src_l->enable_elem);
  if (src_r && src_r->enable_elem)
    alsa_elem_change(src_r->enable_elem);
}

// Callback when sink link state changes
static void snk_link_state_changed(struct alsa_elem *elem, void *private) {
  struct routing_snk *snk = private;
  struct routing_snk *snk_l = get_snk_left_of_pair(snk);
  struct routing_snk *snk_r = snk_l ? get_snk_partner(snk_l) : NULL;

  if (alsa_get_elem_value(elem)) {
    validate_and_propagate_snk_link(snk);

    // Average mixer gains when mixer inputs are linked
    if (snk_l && snk_r && snk_l->elem && snk_l->elem->port_category == PC_MIX)
      average_mixer_input_gains(snk_l->elem->card, snk_l, snk_r);

    // Sync monitor group settings when HW analogue outputs are linked
    if (snk_l && snk_r && snk_l->elem &&
        snk_l->elem->port_category == PC_HW &&
        snk_l->elem->hw_type == HW_TYPE_ANALOGUE)
      sync_monitor_groups_on_link(snk_l, snk_r);
  } else {
    // Distribute mixer gains before unlinking
    if (snk_l && snk_r && snk_l->elem &&
        snk_l->elem->port_category == PC_MIX)
      distribute_mixer_input_gains(snk_l->elem->card, snk_l, snk_r);

    propagate_snk_unlink(snk);
  }

  // Recreate mixer widgets when mixer inputs are linked/unlinked
  if (snk_l && snk_l->elem && snk_l->elem->port_category == PC_MIX)
    recreate_mixer_widgets(snk_l->elem->card);

  // Rebuild monitor groups grid when HW analogue outputs link/unlink
  if (snk_l && snk_l->elem &&
      snk_l->elem->port_category == PC_HW &&
      snk_l->elem->hw_type == HW_TYPE_ANALOGUE)
    rebuild_monitor_groups_grid(snk_l->elem->card);

  // Trigger visibility callbacks via enable_elem change notifications
  // The visibility callback in port-enable.c is the single source of truth
  if (snk_l && snk_l->enable_elem)
    alsa_elem_change(snk_l->enable_elem);
  if (snk_r && snk_r->enable_elem)
    alsa_elem_change(snk_r->enable_elem);
}

// Callback when pair name changes - trigger UI update
static void pair_name_display_changed(struct alsa_elem *elem, void *private) {
  struct alsa_card *card = elem->card;
  schedule_stereo_link_ui_update(card);
}

// Generate element name for stereo link or pair name
// is_sink: 0 = source (In for HW, Out for PCM/Mix/DSP)
//          1 = sink (Out for HW, In for PCM/Mix/DSP)
// suffix: "Link" or "Name"
static char *make_stereo_elem_name(
  int         port_category,
  int         hw_type,
  int         lr_num,
  int         is_sink,
  const char *suffix
) {
  // Direction depends on port category and whether it's a source or sink
  // Sources: HW=In, PCM/Mix/DSP=Out (signal flows from these)
  // Sinks: HW=Out, PCM/Mix/DSP=In (signal flows to these)
  const char *direction = (port_category == PC_HW) != is_sink ? "In" : "Out";

  switch (port_category) {
    case PC_HW:
      return g_strdup_printf(
        "%s %s %d-%d %s", hw_type_names[hw_type], direction,
        lr_num, lr_num + 1, suffix
      );

    case PC_PCM:
      return g_strdup_printf("PCM %s %d-%d %s", direction, lr_num, lr_num + 1, suffix);

    case PC_MIX:
      return g_strdup_printf("Mixer %s %d-%d %s", direction, lr_num, lr_num + 1, suffix);

    case PC_DSP:
      return g_strdup_printf("DSP %s %d-%d %s", direction, lr_num, lr_num + 1, suffix);

    default:
      return NULL;
  }
}

// Create link and pair name elements for a source (only on left channel)
static void create_src_stereo_link_elems(
  struct alsa_card   *card,
  struct routing_src *src,
  GHashTable         *state
) {
  // Only create on left channels that have a valid partner
  if (!is_src_left_channel(src))
    return;
  if (src->port_category == PC_OFF)
    return;
  if (!src_has_valid_partner(src))
    return;

  // Create link state element
  char *link_key = make_stereo_elem_name(
    src->port_category, src->hw_type, src->lr_num, 0, "Link"
  );
  if (!link_key)
    return;

  struct alsa_elem *link_elem = get_elem_by_name(card->elems, link_key);
  if (!link_elem) {
    link_elem = alsa_create_optional_elem(
      card, link_key, SND_CTL_ELEM_TYPE_BOOLEAN, 0
    );
  }

  if (!link_elem) {
    g_free(link_key);
    return;
  }

  src->link_elem = link_elem;

  // Load initial value from state
  const char *value = g_hash_table_lookup(state, link_key);
  alsa_set_elem_value(link_elem, (value && strcmp(value, "1") == 0) ? 1 : 0);
  g_free(link_key);

  // Create pair name element
  char *pair_key = make_stereo_elem_name(
    src->port_category, src->hw_type, src->lr_num, 0, "Name"
  );
  if (!pair_key)
    return;

  struct alsa_elem *pair_name_elem = get_elem_by_name(card->elems, pair_key);
  if (!pair_name_elem) {
    pair_name_elem = alsa_create_optional_elem(
      card, pair_key, SND_CTL_ELEM_TYPE_BYTES, MAX_PAIR_NAME_LEN
    );
  }

  if (!pair_name_elem) {
    g_free(pair_key);
    return;
  }

  src->pair_name_elem = pair_name_elem;

  // Load initial value from state
  const char *pair_value = g_hash_table_lookup(state, pair_key);
  if (pair_value && *pair_value)
    alsa_set_elem_bytes(pair_name_elem, pair_value, strlen(pair_value));
  g_free(pair_key);
}

// Create link and pair name elements for a sink (only on left channel)
static void create_snk_stereo_link_elems(
  struct alsa_card   *card,
  struct routing_snk *snk,
  GHashTable         *state
) {
  if (!snk->elem)
    return;

  struct alsa_elem *elem = snk->elem;

  // Only create on left channels that have a valid partner
  if (!is_snk_left_channel(snk))
    return;
  if (elem->port_category == PC_OFF)
    return;
  if (!snk_has_valid_partner(snk))
    return;

  // Create link state element
  char *link_key = make_stereo_elem_name(
    elem->port_category, elem->hw_type, elem->lr_num, 1, "Link"
  );
  if (!link_key)
    return;

  struct alsa_elem *link_elem = get_elem_by_name(card->elems, link_key);
  if (!link_elem) {
    link_elem = alsa_create_optional_elem(
      card, link_key, SND_CTL_ELEM_TYPE_BOOLEAN, 0
    );
  }

  if (!link_elem) {
    g_free(link_key);
    return;
  }

  snk->link_elem = link_elem;

  // Load initial value from state
  const char *value = g_hash_table_lookup(state, link_key);
  alsa_set_elem_value(link_elem, (value && strcmp(value, "1") == 0) ? 1 : 0);
  g_free(link_key);

  // Mixer inputs don't have custom names (they show the connected source)
  if (elem->port_category == PC_MIX)
    return;

  // Create pair name element
  char *pair_key = make_stereo_elem_name(
    elem->port_category, elem->hw_type, elem->lr_num, 1, "Name"
  );
  if (!pair_key)
    return;

  struct alsa_elem *pair_name_elem = get_elem_by_name(card->elems, pair_key);
  if (!pair_name_elem) {
    pair_name_elem = alsa_create_optional_elem(
      card, pair_key, SND_CTL_ELEM_TYPE_BYTES, MAX_PAIR_NAME_LEN
    );
  }

  if (!pair_name_elem) {
    g_free(pair_key);
    return;
  }

  snk->pair_name_elem = pair_name_elem;

  // Load initial value from state
  const char *pair_value = g_hash_table_lookup(state, pair_key);
  if (pair_value && *pair_value)
    alsa_set_elem_bytes(pair_name_elem, pair_value, strlen(pair_value));
  g_free(pair_key);
}

// Register callbacks for a source's link and pair name elements
static void register_src_link_callbacks(
  struct alsa_card   *card,
  struct routing_src *src
) {
  if (!src->link_elem)
    return;

  struct stereo_link_save_data *link_save_data =
    g_malloc0(sizeof(struct stereo_link_save_data));
  link_save_data->card = card;
  link_save_data->config_key = g_strdup(src->link_elem->name);

  alsa_elem_add_callback(
    src->link_elem, link_state_changed, link_save_data,
    stereo_link_free_callback_data
  );
  alsa_elem_add_callback(
    src->link_elem, src_link_state_changed, src, NULL
  );

  if (src->pair_name_elem) {
    struct stereo_link_save_data *pair_save_data =
      g_malloc0(sizeof(struct stereo_link_save_data));
    pair_save_data->card = card;
    pair_save_data->config_key = g_strdup(src->pair_name_elem->name);

    alsa_elem_add_callback(
      src->pair_name_elem, pair_name_changed, pair_save_data,
      stereo_link_free_callback_data
    );
    alsa_elem_add_callback(
      src->pair_name_elem, pair_name_display_changed, NULL, NULL
    );
  }

  if (src->enable_elem)
    alsa_elem_add_callback(
      src->enable_elem, src_enable_sync_callback, src, NULL
    );
}

// Register callbacks for a sink's link and pair name elements
static void register_snk_link_callbacks(
  struct alsa_card   *card,
  struct routing_snk *snk
) {
  if (!snk->link_elem)
    return;

  struct stereo_link_save_data *link_save_data =
    g_malloc0(sizeof(struct stereo_link_save_data));
  link_save_data->card = card;
  link_save_data->config_key = g_strdup(snk->link_elem->name);

  alsa_elem_add_callback(
    snk->link_elem, link_state_changed, link_save_data,
    stereo_link_free_callback_data
  );
  alsa_elem_add_callback(
    snk->link_elem, snk_link_state_changed, snk, NULL
  );

  if (snk->pair_name_elem) {
    struct stereo_link_save_data *pair_save_data =
      g_malloc0(sizeof(struct stereo_link_save_data));
    pair_save_data->card = card;
    pair_save_data->config_key = g_strdup(snk->pair_name_elem->name);

    alsa_elem_add_callback(
      snk->pair_name_elem, pair_name_changed, pair_save_data,
      stereo_link_free_callback_data
    );
    alsa_elem_add_callback(
      snk->pair_name_elem, pair_name_display_changed, NULL, NULL
    );
  }

  if (snk->enable_elem)
    alsa_elem_add_callback(
      snk->enable_elem, snk_enable_sync_callback, snk, NULL
    );

  if (snk->elem &&
      snk->elem->port_category == PC_HW &&
      snk->elem->hw_type == HW_TYPE_ANALOGUE)
    register_monitor_group_sync_callbacks(snk);
}

// Get stereo pair display name for a linked source pair
char *get_src_pair_display_name(struct routing_src *src) {
  struct routing_src *src_l = get_src_left_of_pair(src);
  if (!src_l)
    return g_strdup("");

  // Check for custom pair name
  if (src_l->pair_name_elem) {
    size_t size;
    const void *bytes = alsa_get_elem_bytes(src_l->pair_name_elem, &size);

    if (bytes && size > 0) {
      size_t str_len = strnlen((const char *)bytes, size);
      if (str_len > 0 && g_utf8_validate((const char *)bytes, str_len, NULL))
        return g_strndup((const char *)bytes, str_len);
    }
  }

  // Fall back to default pair name (device-specific or constructed)
  return get_src_default_pair_name(src_l);
}

// Get stereo pair display name for a linked sink pair
char *get_snk_pair_display_name(struct routing_snk *snk) {
  struct routing_snk *snk_l = get_snk_left_of_pair(snk);
  if (!snk_l)
    return g_strdup("");

  // Check for custom pair name
  if (snk_l->pair_name_elem) {
    size_t size;
    const void *bytes = alsa_get_elem_bytes(snk_l->pair_name_elem, &size);

    if (bytes && size > 0) {
      size_t str_len = strnlen((const char *)bytes, size);
      if (str_len > 0 && g_utf8_validate((const char *)bytes, str_len, NULL))
        return g_strndup((const char *)bytes, str_len);
    }
  }

  // Fall back to default pair name (device-specific or constructed)
  return get_snk_default_pair_name(snk_l);
}

// Get default stereo link state for a pair based on category rules
static int get_pair_category_default(
  int port_category,
  int hw_type,
  int pair_index,
  int is_sink
) {
  if (is_sink) {
    switch (port_category) {
      case PC_HW:
      case PC_PCM:
      case PC_MIX:
        return 1;
      case PC_DSP:
        return pair_index == 0 ? 0 : 1;
      default:
        return 0;
    }
  }

  // Source
  switch (port_category) {
    case PC_HW:
      if (hw_type == HW_TYPE_ANALOGUE)
        return pair_index == 0 ? 0 : 1;
      return 1;
    case PC_PCM:
    case PC_MIX:
      return 1;
    case PC_DSP:
      return pair_index == 0 ? 0 : 1;
    default:
      return 0;
  }
}

// Determine default stereo link states on first run based on
// category rules and existing routing/mixer connections.
// Called before callbacks are registered, so alsa_set_elem_value()
// is safe (no side effects).
static void determine_default_stereo_links(struct alsa_card *card) {

  // Phase 1: Set category defaults
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    if (!src->link_elem || !is_src_left_channel(src))
      continue;

    int pair_index = (src->lr_num - 1) / 2;
    alsa_set_elem_value(
      src->link_elem,
      get_pair_category_default(
        src->port_category, src->hw_type, pair_index, 0
      )
    );
  }

  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    if (!snk->link_elem || !snk->elem || !is_snk_left_channel(snk))
      continue;

    int pair_index = (snk->elem->lr_num - 1) / 2;
    alsa_set_elem_value(
      snk->link_elem,
      get_pair_category_default(
        snk->elem->port_category, snk->elem->hw_type,
        pair_index, 1
      )
    );
  }

  // Phase 2: Fixpoint constraint propagation
  long min_val = get_mixer_gain_min_val(card);
  int changed = 1;

  while (changed) {
    changed = 0;

    // Sink-side: check each stereo sink pair
    for (int i = 0; i < card->routing_snks->len; i++) {
      struct routing_snk *snk_l = &g_array_index(
        card->routing_snks, struct routing_snk, i
      );
      if (!snk_l->link_elem || !snk_l->elem)
        continue;
      if (!is_snk_left_channel(snk_l))
        continue;
      if (!alsa_get_elem_value(snk_l->link_elem))
        continue;

      struct routing_snk *snk_r = get_snk_partner(snk_l);
      if (!snk_r || !snk_r->elem)
        continue;

      int src_l_id = alsa_get_elem_value(snk_l->elem);
      int src_r_id = alsa_get_elem_value(snk_r->elem);

      // No connection — leave as stereo
      if (src_l_id == 0 && src_r_id == 0)
        continue;

      // Check stereo compatibility
      int compatible = 0;
      struct routing_src *src_l = NULL;
      struct routing_src *src_r = NULL;

      if (src_l_id != 0 && src_r_id != 0 &&
          src_l_id != src_r_id &&
          src_l_id < card->routing_srcs->len &&
          src_r_id < card->routing_srcs->len) {
        src_l = &g_array_index(
          card->routing_srcs, struct routing_src, src_l_id
        );
        src_r = &g_array_index(
          card->routing_srcs, struct routing_src, src_r_id
        );

        if (get_src_partner(src_l) == src_r &&
            is_src_left_channel(src_l)) {
          compatible = 1;
        }
      }

      if (!compatible) {
        alsa_set_elem_value(snk_l->link_elem, 0);
        changed = 1;

        // Downgrade source pair if identifiable
        if (src_l && src_r &&
            get_src_partner(src_l) == src_r) {
          struct alsa_elem *sle = get_src_link_elem(src_l);
          if (sle && alsa_get_elem_value(sle)) {
            alsa_set_elem_value(sle, 0);
            changed = 1;
          }
        }
      } else {
        // Compatible but source pair is mono — downgrade sink
        struct alsa_elem *sle = get_src_link_elem(src_l);
        if (sle && !alsa_get_elem_value(sle)) {
          alsa_set_elem_value(snk_l->link_elem, 0);
          changed = 1;
        }
      }
    }

    // Source-side: check each stereo source pair
    for (int i = 0; i < card->routing_srcs->len; i++) {
      struct routing_src *src_l = &g_array_index(
        card->routing_srcs, struct routing_src, i
      );
      if (!src_l->link_elem || !is_src_left_channel(src_l))
        continue;
      if (!alsa_get_elem_value(src_l->link_elem))
        continue;

      struct routing_src *src_r = get_src_partner(src_l);
      if (!src_r)
        continue;

      int downgrade_src = 0;

      for (int j = 0; j < card->routing_snks->len; j++) {
        struct routing_snk *snk = &g_array_index(
          card->routing_snks, struct routing_snk, j
        );
        if (!snk->elem)
          continue;

        // Skip fixed mixer inputs - they follow their source's link state
        if (snk->elem->port_category == PC_MIX &&
            !alsa_get_elem_writable(snk->elem))
          continue;

        int snk_src_id = alsa_get_elem_value(snk->elem);
        if (snk_src_id != src_l->id && snk_src_id != src_r->id)
          continue;

        // Found a sink connected to our source pair
        struct routing_snk *snk_left =
          get_snk_left_of_pair(snk);
        if (!snk_left || !snk_left->link_elem) {
          downgrade_src = 1;
          break;
        }

        struct routing_snk *snk_right =
          get_snk_partner(snk_left);
        if (!snk_right || !snk_right->elem) {
          downgrade_src = 1;
          break;
        }

        // Check stereo-compatible connection
        int sl_src = alsa_get_elem_value(snk_left->elem);
        int sr_src = alsa_get_elem_value(snk_right->elem);

        if (sl_src != src_l->id || sr_src != src_r->id) {
          downgrade_src = 1;
          if (alsa_get_elem_value(snk_left->link_elem)) {
            alsa_set_elem_value(snk_left->link_elem, 0);
            changed = 1;
          }
          break;
        }

        // Stereo-compatible but sink pair is mono
        if (!alsa_get_elem_value(snk_left->link_elem)) {
          downgrade_src = 1;
          break;
        }
      }

      if (downgrade_src &&
          alsa_get_elem_value(src_l->link_elem)) {
        alsa_set_elem_value(src_l->link_elem, 0);
        changed = 1;
      }
    }

    // Mixer gain matrix check
    for (int i = 0; i < card->routing_snks->len; i++) {
      struct routing_snk *in_l = &g_array_index(
        card->routing_snks, struct routing_snk, i
      );
      if (!in_l->elem || !in_l->link_elem)
        continue;
      if (in_l->elem->port_category != PC_MIX)
        continue;
      if (!is_snk_left_channel(in_l) ||
          !alsa_get_elem_value(in_l->link_elem))
        continue;

      struct routing_snk *in_r = get_snk_partner(in_l);
      if (!in_r || !in_r->elem)
        continue;

      int input_l = in_l->elem->lr_num;
      int input_r = in_r->elem->lr_num;

      for (int j = 0; j < card->routing_srcs->len; j++) {
        struct routing_src *out_l = &g_array_index(
          card->routing_srcs, struct routing_src, j
        );
        if (!out_l->link_elem)
          continue;
        if (out_l->port_category != PC_MIX)
          continue;
        if (!is_src_left_channel(out_l) ||
            !alsa_get_elem_value(out_l->link_elem))
          continue;

        struct routing_src *out_r = get_src_partner(out_l);
        if (!out_r)
          continue;

        char name[80];

        snprintf(
          name, sizeof(name),
          "Mix %c Input %02d Playback Volume",
          'A' + out_l->port_num, input_r
        );
        struct alsa_elem *off_lr =
          get_elem_by_name(card->elems, name);

        snprintf(
          name, sizeof(name),
          "Mix %c Input %02d Playback Volume",
          'A' + out_r->port_num, input_l
        );
        struct alsa_elem *off_rl =
          get_elem_by_name(card->elems, name);

        int has_crosstalk =
          (off_lr &&
            alsa_get_elem_value(off_lr) != min_val) ||
          (off_rl &&
            alsa_get_elem_value(off_rl) != min_val);

        if (has_crosstalk) {
          if (alsa_get_elem_value(in_l->link_elem)) {
            alsa_set_elem_value(in_l->link_elem, 0);
            changed = 1;
          }
          if (alsa_get_elem_value(out_l->link_elem)) {
            alsa_set_elem_value(out_l->link_elem, 0);
            changed = 1;
          }
        }
      }
    }
  }

  // Phase 3: Save all link values
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    if (!src->link_elem || !is_src_left_channel(src))
      continue;
    optional_state_save(
      card, CONFIG_SECTION_CONTROLS, src->link_elem->name,
      alsa_get_elem_value(src->link_elem) ? "1" : "0"
    );
  }

  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    if (!snk->link_elem || !is_snk_left_channel(snk))
      continue;
    optional_state_save(
      card, CONFIG_SECTION_CONTROLS, snk->link_elem->name,
      alsa_get_elem_value(snk->link_elem) ? "1" : "0"
    );
  }
}

// Initialise stereo link elements
void stereo_link_init(struct alsa_card *card) {
  if (!card->serial || !*card->serial)
    return;

  if (!card->routing_srcs || !card->routing_snks)
    return;

  // Load existing state
  GHashTable *state = optional_state_load(card, CONFIG_SECTION_CONTROLS);
  if (!state) {
    state = g_hash_table_new_full(
      g_str_hash, g_str_equal, g_free, g_free
    );
  }

  // Create elements for all routing sources
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    create_src_stereo_link_elems(card, src, state);
  }

  // Create elements for all routing sinks
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    create_snk_stereo_link_elems(card, snk, state);
  }

  // Detect first run: if no Link keys exist in state AND all link
  // elements are simulated, determine sensible defaults.
  // If any link element is real (driver-provided), the driver
  // manages its own defaults.
  int has_link_keys = 0;
  int has_real_link_elem = 0;

  for (int i = 0; i < card->routing_srcs->len && !has_real_link_elem; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    if (src->link_elem && !src->link_elem->is_simulated)
      has_real_link_elem = 1;
  }

  for (int i = 0; i < card->routing_snks->len && !has_real_link_elem; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    if (snk->link_elem && !snk->link_elem->is_simulated)
      has_real_link_elem = 1;
  }

  if (!has_real_link_elem) {
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, state);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
      if (g_str_has_suffix((const char *)key, " Link")) {
        has_link_keys = 1;
        break;
      }
    }

    if (!has_link_keys)
      determine_default_stereo_links(card);
  }

  g_hash_table_destroy(state);

  // Register callbacks after defaults are determined, so that
  // alsa_set_elem_value() calls above don't trigger side effects
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    register_src_link_callbacks(card, src);
  }

  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    register_snk_link_callbacks(card, snk);
  }
}
