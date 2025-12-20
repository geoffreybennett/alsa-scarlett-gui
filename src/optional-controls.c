// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <alsa/asoundlib.h>
#include <string.h>

#include "optional-controls.h"
#include "optional-state.h"
#include "alsa.h"

// Table of optional control definitions
// These are controls that may or may not exist on the device
// If they don't exist, we create simulated elements and persist values
// Config key uses element name (ALSA-style naming)
const struct optional_control_def optional_controls[] = {
  {
    .alsa_name = "Name",
    .alsa_type = SND_CTL_ELEM_TYPE_BYTES,
    .max_size = 32,
    .default_value = NULL,
    .type = OPTIONAL_CONTROL_NAME
  },
  { NULL }
};

// Callback structure to pass data to save callback
struct optional_control_save_data {
  struct alsa_card *card;
  char             *config_key;
};

// Callback when an optional control value changes
// Saves the new value to the state file
static void optional_control_changed(
  struct alsa_elem *elem,
  void             *private
) {
  struct optional_control_save_data *data = private;

  // for BYTES elements, get the string value
  if (elem->type == SND_CTL_ELEM_TYPE_BYTES) {
    size_t size;
    const void *bytes = alsa_get_elem_bytes(elem, &size);

    if (bytes && size > 0) {
      // find actual string length (up to first null byte)
      size_t str_len = strnlen((const char *)bytes, size);

      // only save if valid UTF-8
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
}

// Get the Name element for a card
struct alsa_elem *optional_controls_get_name_elem(struct alsa_card *card) {
  return get_elem_by_name(card->elems, "Name");
}

// Free optional control callback data
void optional_controls_free_callback_data(void *data) {
  if (!data)
    return;

  struct optional_control_save_data *save_data = data;
  g_free(save_data->config_key);
  g_free(save_data);
}

// Initialise optional controls for a card
void optional_controls_init(struct alsa_card *card) {
  if (!card->serial || !*card->serial) {
    // no serial number, can't persist state
    return;
  }

  // load existing state from [controls] section
  GHashTable *state = optional_state_load(card, CONFIG_SECTION_CONTROLS);
  if (!state) {
    state = g_hash_table_new_full(
      g_str_hash, g_str_equal, g_free, g_free
    );
  }

  // process each optional control definition
  for (int i = 0; optional_controls[i].alsa_name; i++) {
    const struct optional_control_def *def = &optional_controls[i];

    // check if the element already exists (real ALSA control)
    struct alsa_elem *elem = get_elem_by_name(card->elems, def->alsa_name);

    if (elem) {
      // real element exists, use it directly without state file
      continue;
    }

    // element doesn't exist, create simulated element
    elem = alsa_create_optional_elem(
      card,
      def->alsa_name,
      def->alsa_type,
      def->max_size
    );

    if (!elem) {
      fprintf(stderr, "Failed to create optional element %s\n", def->alsa_name);
      continue;
    }

    // load value from state file (use element name as config key)
    const char *value = g_hash_table_lookup(state, def->alsa_name);

    if (!value && def->default_value)
      value = def->default_value;

    // set the initial value
    if (value && *value) {
      if (def->alsa_type == SND_CTL_ELEM_TYPE_BYTES)
        alsa_set_elem_bytes(elem, value, strlen(value));
    }

    // register callback to save state on changes
    struct optional_control_save_data *callback_data =
      g_malloc0(sizeof(struct optional_control_save_data));
    callback_data->card = card;
    callback_data->config_key = g_strdup(def->alsa_name);

    alsa_elem_add_callback(
      elem, optional_control_changed, callback_data,
      optional_controls_free_callback_data
    );
  }

  g_hash_table_destroy(state);
}
