// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <alsa/asoundlib.h>
#include <string.h>

#include "port-enable.h"
#include "config-io.h"
#include "config-monitor-groups.h"
#include "optional-state.h"
#include "alsa.h"
#include "config-monitor-groups.h"
#include "stereo-link.h"
#include "window-mixer.h"

// Callback structure to pass data to save callback
struct port_enable_save_data {
  struct alsa_card *card;
  char             *config_key;
};

// Callback when a port enable value changes
// Saves the new value to the state file
static void port_enable_changed(
  struct alsa_elem *elem,
  void             *private
) {
  struct port_enable_save_data *data = private;

  // get the boolean value
  long value = alsa_get_elem_value(elem);

  // save as "1" or "0"
  optional_state_save(
    data->card, CONFIG_SECTION_CONTROLS, data->config_key, value ? "1" : "0"
  );
}

// Flush pending UI updates for a card
static gboolean flush_pending_ui_updates(gpointer user_data) {
  struct alsa_card *card = user_data;

  card->pending_ui_update_idle = FALSE;

  if (card->pending_ui_updates & PENDING_UI_UPDATE_MIXER_GRID) {
    update_mixer_labels(card);
    update_config_io_mixer_labels(card);
    rebuild_mixer_grid(card);
  }

  if (card->pending_ui_updates & PENDING_UI_UPDATE_MONITOR_GROUPS)
    rebuild_monitor_groups_grid(card);

  card->pending_ui_updates = 0;

  return G_SOURCE_REMOVE;
}

// Schedule a UI update for a card (runs at idle)
void schedule_ui_update(struct alsa_card *card, int flags) {
  if (!card)
    return;

  card->pending_ui_updates |= flags;

  if (!card->pending_ui_update_idle) {
    card->pending_ui_update_idle = TRUE;
    g_idle_add_full(
      G_PRIORITY_HIGH_IDLE, flush_pending_ui_updates, card, NULL
    );
  }
}

// Update the Sources label arrows based on which sections are visible
static void update_sources_label(struct alsa_card *card) {
  if (!card || !card->routing_src_label)
    return;

  // Check if we have any enabled sources in each category
  int has_hw_or_pcm_sources =
    !all_sources_disabled(card, PC_HW) ||
    !all_sources_disabled(card, PC_PCM);
  int has_mixer_or_dsp_sources =
    !all_sources_disabled(card, PC_MIX) ||
    !all_sources_disabled(card, PC_DSP);

  // Hide the label if there are no arrows (no sources enabled)
  if (!has_hw_or_pcm_sources && !has_mixer_or_dsp_sources) {
    gtk_widget_set_visible(card->routing_src_label, FALSE);
    return;
  }

  // Show the label and set text based on what's visible
  gtk_widget_set_visible(card->routing_src_label, TRUE);

  char *text;
  if (has_hw_or_pcm_sources && has_mixer_or_dsp_sources) {
    text = "↑\nSources →";
  } else if (has_hw_or_pcm_sources) {
    text = "↑\nSources";
  } else {
    text = "Sources →";
  }

  gtk_label_set_text(GTK_LABEL(card->routing_src_label), text);
}

// Update the Sinks label arrows based on which sections are visible
static void update_sinks_label(struct alsa_card *card) {
  if (!card || !card->routing_snk_label)
    return;

  // Check if we have any enabled sinks in each category
  int has_hw_or_pcm_sinks =
    !all_sinks_disabled(card, PC_HW) ||
    !all_sinks_disabled(card, PC_PCM);

  // Don't count mixer sinks if they're fixed (not configurable)
  int has_mixer_sinks = card->has_fixed_mixer_inputs ? 0 : !all_sinks_disabled(card, PC_MIX);
  int has_mixer_or_dsp_sinks =
    has_mixer_sinks ||
    !all_sinks_disabled(card, PC_DSP);

  // Hide the label if there are no arrows (no sinks enabled)
  if (!has_hw_or_pcm_sinks && !has_mixer_or_dsp_sinks) {
    gtk_widget_set_visible(card->routing_snk_label, FALSE);
    return;
  }

  // Show the label and set text based on what's visible
  gtk_widget_set_visible(card->routing_snk_label, TRUE);

  char *text;
  if (has_hw_or_pcm_sinks && has_mixer_or_dsp_sinks) {
    text = "← Sinks\n↓";
  } else if (has_hw_or_pcm_sinks) {
    text = "Sinks\n↓";
  } else {
    text = "← Sinks";
  }

  gtk_label_set_text(GTK_LABEL(card->routing_snk_label), text);
}

// Update visibility of routing section grids based on port enable states
void update_routing_section_visibility(struct alsa_card *card) {
  if (!card)
    return;

  // Hardware Inputs
  if (card->routing_hw_in_grid) {
    int all_disabled = all_sources_disabled(card, PC_HW);
    gtk_widget_set_visible(card->routing_hw_in_grid, !all_disabled);
  }

  // Hardware Outputs
  if (card->routing_hw_out_grid) {
    int all_disabled = all_sinks_disabled(card, PC_HW);
    gtk_widget_set_visible(card->routing_hw_out_grid, !all_disabled);
  }

  // PCM Inputs (sources - to PC)
  if (card->routing_pcm_in_grid) {
    int all_disabled = all_sources_disabled(card, PC_PCM);
    gtk_widget_set_visible(card->routing_pcm_in_grid, !all_disabled);
  }

  // PCM Outputs (sinks - from PC)
  if (card->routing_pcm_out_grid) {
    int all_disabled = all_sinks_disabled(card, PC_PCM);
    gtk_widget_set_visible(card->routing_pcm_out_grid, !all_disabled);
  }

  // DSP Inputs (sinks)
  if (card->routing_dsp_in_grid) {
    int all_disabled = all_sinks_disabled(card, PC_DSP);
    gtk_widget_set_visible(card->routing_dsp_in_grid, !all_disabled);
  }

  // DSP Outputs (sources)
  if (card->routing_dsp_out_grid) {
    int all_disabled = all_sources_disabled(card, PC_DSP);
    gtk_widget_set_visible(card->routing_dsp_out_grid, !all_disabled);
  }

  // Mixer Inputs (sinks)
  if (card->routing_mixer_in_grid) {
    int all_disabled = all_sinks_disabled(card, PC_MIX);
    gtk_widget_set_visible(card->routing_mixer_in_grid, !all_disabled);
  }

  // Mixer Outputs (sources)
  if (card->routing_mixer_out_grid) {
    int all_disabled = all_sources_disabled(card, PC_MIX);
    gtk_widget_set_visible(card->routing_mixer_out_grid, !all_disabled);
  }

  // Update the Sources and Sinks label arrows
  update_sources_label(card);
  update_sinks_label(card);
}

// Callback to update routing source visibility
static void src_visibility_changed(
  struct alsa_elem   *elem,
  void               *private
) {
  struct routing_src *src = private;

  // widget might not exist yet if routing window hasn't been created
  if (!src->widget)
    return;

  // A source is visible if enabled AND should be displayed
  // (should_display_src returns 0 for right channel of linked pair)
  int enabled = alsa_get_elem_value(elem);
  int visible = enabled && should_display_src(src);
  gtk_widget_set_visible(src->widget, visible);

  // update section visibility
  update_routing_section_visibility(src->card);

  // redraw routing lines to reflect new layout
  if (src->card && src->card->routing_lines)
    gtk_widget_queue_draw(src->card->routing_lines);

  // schedule expensive updates
  int flags = PENDING_UI_UPDATE_MONITOR_GROUPS;
  if (src->port_category == PC_MIX)
    flags |= PENDING_UI_UPDATE_MIXER_GRID;
  schedule_ui_update(src->card, flags);
}

// Callback to update routing sink visibility
static void snk_visibility_changed(
  struct alsa_elem   *elem,
  void               *private
) {
  struct routing_snk *snk = private;
  struct alsa_card *card = snk->elem ? snk->elem->card : NULL;

  // update routing widget visibility if it exists
  // (fixed mixer inputs don't have routing widgets)
  if (snk->box_widget) {
    // A sink is visible if enabled AND should be displayed
    // (should_display_snk returns 0 for right channel of linked pair)
    int enabled = alsa_get_elem_value(elem);
    int visible = enabled && should_display_snk(snk);
    gtk_widget_set_visible(snk->box_widget, visible);

    // update section visibility
    if (card)
      update_routing_section_visibility(card);

    // redraw routing lines to reflect new layout
    if (card && card->routing_lines)
      gtk_widget_queue_draw(card->routing_lines);
  }

  if (card) {
    // rebuild mixer grid for mixer inputs
    // (needed for fixed mixer inputs which don't have routing widgets)
    if (snk->elem->port_category == PC_MIX)
      schedule_ui_update(card, PENDING_UI_UPDATE_MIXER_GRID);

    // rebuild monitor groups for analogue output sinks
    if (snk->elem->port_category == PC_HW)
      schedule_ui_update(card, PENDING_UI_UPDATE_MONITOR_GROUPS);
  }
}

// Free port enable callback data
void port_enable_free_callback_data(void *data) {
  if (!data)
    return;

  struct port_enable_save_data *save_data = data;
  g_free(save_data->config_key);
  g_free(save_data);
}

// Generate ALSA element name for a routing source
// Returns newly allocated string that must be freed
static char *get_src_enable_elem_name(struct routing_src *src) {
  const char *category_name = NULL;

  switch (src->port_category) {
    case PC_HW:
      category_name = hw_type_names[src->hw_type];
      return g_strdup_printf(
        "%s In %d Switch", category_name, src->lr_num
      );

    case PC_PCM:
      return g_strdup_printf("PCM Out %d Switch", src->lr_num);

    case PC_MIX:
      return g_strdup_printf("Mixer Out %d Switch", src->lr_num);

    case PC_DSP:
      return g_strdup_printf("DSP Out %d Switch", src->lr_num);

    default:
      return NULL;
  }
}

// Generate ALSA element name for a routing sink
// Returns newly allocated string that must be freed
static char *get_snk_enable_elem_name(struct routing_snk *snk) {
  struct alsa_elem *elem = snk->elem;
  const char *category_name = NULL;

  switch (elem->port_category) {
    case PC_HW:
      category_name = hw_type_names[elem->hw_type];
      return g_strdup_printf(
        "%s Out %d Switch", category_name, elem->lr_num
      );

    case PC_PCM:
      return g_strdup_printf("PCM In %d Switch", elem->lr_num);

    case PC_MIX:
      return g_strdup_printf("Mixer In %d Switch", elem->lr_num);

    case PC_DSP:
      return g_strdup_printf("DSP In %d Switch", elem->lr_num);

    default:
      return NULL;
  }
}

// Create simulated enable element for a routing source
static void create_src_enable_elem(
  struct alsa_card   *card,
  struct routing_src *src,
  GHashTable         *state
) {
  // skip PC_OFF
  if (src->port_category == PC_OFF)
    return;

  // generate element name
  char *elem_name = get_src_enable_elem_name(src);
  if (!elem_name)
    return;

  // check if real element exists (for forward compatibility)
  struct alsa_elem *elem = get_elem_by_name(card->elems, elem_name);

  if (elem) {
    // real element exists, use it directly
    src->enable_elem = elem;
    g_free(elem_name);
    return;
  }

  // create simulated boolean element
  elem = alsa_create_optional_elem(
    card,
    elem_name,
    SND_CTL_ELEM_TYPE_BOOLEAN,
    0  // size not used for boolean
  );
  g_free(elem_name);

  if (!elem) {
    fprintf(stderr, "Failed to create enable element for source\n");
    return;
  }

  src->enable_elem = elem;

  // use element name as config key
  char *config_key = get_src_enable_elem_name(src);
  if (!config_key)
    return;

  // load value from state file (default to enabled)
  const char *value = g_hash_table_lookup(state, config_key);
  long enabled = 1;  // default to enabled

  if (value && *value) {
    enabled = (strcmp(value, "1") == 0) ? 1 : 0;
  }

  // set the initial value
  elem->value = enabled;

  // register callback to save state on changes
  struct port_enable_save_data *callback_data =
    g_malloc0(sizeof(struct port_enable_save_data));
  callback_data->card = card;
  callback_data->config_key = config_key;  // transfer ownership

  alsa_elem_add_callback(
    elem, port_enable_changed, callback_data,
    port_enable_free_callback_data
  );

  // register callback to update widget visibility
  alsa_elem_add_callback(elem, src_visibility_changed, src, NULL);
}

// Create simulated enable element for a routing sink
static void create_snk_enable_elem(
  struct alsa_card   *card,
  struct routing_snk *snk,
  GHashTable         *state
) {
  // skip sinks without valid port category
  if (snk->elem->port_category == PC_OFF)
    return;

  // generate element name
  char *elem_name = get_snk_enable_elem_name(snk);
  if (!elem_name)
    return;

  // check if real element exists (for forward compatibility)
  struct alsa_elem *elem = get_elem_by_name(card->elems, elem_name);

  if (elem) {
    // real element exists, use it directly
    snk->enable_elem = elem;
    g_free(elem_name);
    return;
  }

  // create simulated boolean element
  elem = alsa_create_optional_elem(
    card,
    elem_name,
    SND_CTL_ELEM_TYPE_BOOLEAN,
    0  // size not used for boolean
  );
  g_free(elem_name);

  if (!elem) {
    fprintf(stderr, "Failed to create enable element for sink\n");
    return;
  }

  snk->enable_elem = elem;

  // use element name as config key
  char *config_key = get_snk_enable_elem_name(snk);
  if (!config_key)
    return;

  // load value from state file (default to enabled)
  const char *value = g_hash_table_lookup(state, config_key);
  long enabled = 1;  // default to enabled

  if (value && *value) {
    enabled = (strcmp(value, "1") == 0) ? 1 : 0;
  }

  // set the initial value
  elem->value = enabled;

  // register callback to save state on changes
  struct port_enable_save_data *callback_data =
    g_malloc0(sizeof(struct port_enable_save_data));
  callback_data->card = card;
  callback_data->config_key = config_key;  // transfer ownership

  alsa_elem_add_callback(
    elem, port_enable_changed, callback_data,
    port_enable_free_callback_data
  );

  // register callback to update widget visibility
  alsa_elem_add_callback(elem, snk_visibility_changed, snk, NULL);
}

// Initialise port enable elements for all routing sources and sinks
void port_enable_init(struct alsa_card *card) {
  if (!card->serial || !*card->serial) {
    // no serial number, can't persist state
    return;
  }

  // check if card has routing controls
  if (!card->routing_srcs || !card->routing_snks) {
    // card doesn't have routing controls
    return;
  }

  // load existing state from [controls] section
  GHashTable *state = optional_state_load(card, CONFIG_SECTION_CONTROLS);
  if (!state) {
    state = g_hash_table_new_full(
      g_str_hash, g_str_equal, g_free, g_free
    );
  }

  // create enable elements for all routing sources
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    create_src_enable_elem(card, src, state);
  }

  // create enable elements for all routing sinks
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    create_snk_enable_elem(card, snk, state);
  }

  g_hash_table_destroy(state);
}

// Check if a routing source is enabled
int is_routing_src_enabled(struct routing_src *src) {
  if (!src || !src->enable_elem)
    return 1;  // default to enabled if no element

  return alsa_get_elem_value(src->enable_elem) != 0;
}

// Check if a routing sink is enabled
int is_routing_snk_enabled(struct routing_snk *snk) {
  if (!snk || !snk->enable_elem)
    return 1;  // default to enabled if no element

  return alsa_get_elem_value(snk->enable_elem) != 0;
}

// Get the enable element for a routing source
struct alsa_elem *get_src_enable_elem(struct routing_src *src) {
  if (!src)
    return NULL;
  return src->enable_elem;
}

// Get the enable element for a routing sink
struct alsa_elem *get_snk_enable_elem(struct routing_snk *snk) {
  if (!snk)
    return NULL;
  return snk->enable_elem;
}

// Check if all sources of a given category are disabled
int all_sources_disabled(struct alsa_card *card, int port_category) {
  if (!card || !card->routing_srcs)
    return 1;

  for (int i = 1; i < card->routing_srcs->len; i++) {  // start at 1 to skip "Off"
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    if (src->port_category == port_category) {
      if (is_routing_src_enabled(src))
        return 0;  // found at least one enabled
    }
  }

  return 1;  // all disabled (or none exist)
}

// Check if all sinks of a given category are disabled
int all_sinks_disabled(struct alsa_card *card, int port_category) {
  if (!card || !card->routing_snks)
    return 1;

  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (snk->elem && snk->elem->port_category == port_category) {
      if (is_routing_snk_enabled(snk))
        return 0;  // found at least one enabled
    }
  }

  return 1;  // all disabled (or none exist)
}
