// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <alsa/asoundlib.h>
#include <string.h>

#include "custom-names.h"
#include "optional-state.h"
#include "alsa.h"
#include "widget-boolean.h"
#include "window-mixer.h"

// Maximum length for custom names
#define MAX_CUSTOM_NAME_LEN 32

// Callback structure to pass data to save callback
struct custom_name_save_data {
  struct alsa_card *card;
  char             *config_key;
};

// Callback when a custom name value changes
// Saves the new value to the state file
static void custom_name_changed(
  struct alsa_elem *elem,
  void             *private
) {
  struct custom_name_save_data *data = private;

  // get the string value
  size_t size;
  const void *bytes = alsa_get_elem_bytes(elem, &size);

  if (bytes && size > 0) {
    // find actual string length (up to first null byte)
    size_t str_len = strnlen((const char *)bytes, size);

    // only save if valid UTF-8
    if (str_len > 0 && g_utf8_validate((const char *)bytes, str_len, NULL)) {
      char *str = g_strndup((const char *)bytes, str_len);
      optional_state_save(data->card, data->config_key, str);
      g_free(str);
    } else {
      optional_state_save(data->card, data->config_key, "");
    }
  } else {
    optional_state_save(data->card, data->config_key, "");
  }
}

// Free custom name callback data
void custom_names_free_callback_data(void *data) {
  if (!data)
    return;

  struct custom_name_save_data *save_data = data;
  g_free(save_data->config_key);
  g_free(save_data);
}

// Generate ALSA element name for a routing source
// Returns newly allocated string that must be freed
static char *get_src_elem_name(struct routing_src *src) {
  const char *category_name = NULL;

  switch (src->port_category) {
    case PC_HW:
      category_name = hw_type_names[src->hw_type];
      return g_strdup_printf("%s In %d Name", category_name, src->lr_num);

    case PC_PCM:
      return g_strdup_printf("PCM In %d Name", src->lr_num);

    case PC_MIX:
      return g_strdup_printf("Mix In %d Name", src->lr_num);

    case PC_DSP:
      return g_strdup_printf("DSP In %d Name", src->lr_num);

    default:
      return NULL;
  }
}

// Generate ALSA element name for a routing sink
// Returns newly allocated string that must be freed
static char *get_snk_elem_name(struct routing_snk *snk) {
  struct alsa_elem *elem = snk->elem;
  const char *category_name = NULL;

  switch (elem->port_category) {
    case PC_HW:
      category_name = hw_type_names[elem->hw_type];
      return g_strdup_printf("%s Out %d Name", category_name, elem->lr_num);

    case PC_PCM:
      return g_strdup_printf("PCM Out %d Name", elem->lr_num);

    case PC_MIX:
      return g_strdup_printf("Mix Out %d Name", elem->lr_num);

    case PC_DSP:
      return g_strdup_printf("DSP Out %d Name", elem->lr_num);

    default:
      return NULL;
  }
}

// Generate config key for a routing source
// Returns newly allocated string that must be freed
static char *get_src_config_key(struct routing_src *src) {
  const char *category_prefix = NULL;

  switch (src->port_category) {
    case PC_HW:
      category_prefix = hw_type_names[src->hw_type];
      return g_strdup_printf("%s_in_%d_name", category_prefix, src->lr_num);

    case PC_PCM:
      return g_strdup_printf("pcm_in_%d_name", src->lr_num);

    case PC_MIX:
      return g_strdup_printf("mix_in_%d_name", src->lr_num);

    case PC_DSP:
      return g_strdup_printf("dsp_in_%d_name", src->lr_num);

    default:
      return NULL;
  }
}

// Generate config key for a routing sink
// Returns newly allocated string that must be freed
static char *get_snk_config_key(struct routing_snk *snk) {
  struct alsa_elem *elem = snk->elem;
  const char *category_prefix = NULL;

  switch (elem->port_category) {
    case PC_HW:
      category_prefix = hw_type_names[elem->hw_type];
      return g_strdup_printf("%s_out_%d_name", category_prefix, elem->lr_num);

    case PC_PCM:
      return g_strdup_printf("pcm_out_%d_name", elem->lr_num);

    case PC_MIX:
      return g_strdup_printf("mix_out_%d_name", elem->lr_num);

    case PC_DSP:
      return g_strdup_printf("dsp_out_%d_name", elem->lr_num);

    default:
      return NULL;
  }
}

// Update the cached display name for a routing source
static void update_src_display_name(struct routing_src *src) {
  if (!src)
    return;

  // free old cached name
  g_free(src->display_name);
  src->display_name = NULL;

  // check if custom name is set
  if (src->custom_name_elem) {
    size_t size;
    const void *bytes = alsa_get_elem_bytes(src->custom_name_elem, &size);

    if (bytes && size > 0) {
      size_t str_len = strnlen((const char *)bytes, size);
      if (str_len > 0 && g_utf8_validate((const char *)bytes, str_len, NULL)) {
        // create properly null-terminated copy
        src->display_name = g_strndup((const char *)bytes, str_len);
        return;
      }
    }
  }

  // fall back to default name
  src->display_name = g_strdup(src->name ? src->name : "");
}

// Update the cached display name for a routing sink
static void update_snk_display_name(struct routing_snk *snk) {
  if (!snk || !snk->elem)
    return;

  // free old cached name
  g_free(snk->display_name);
  snk->display_name = NULL;

  // check if custom name is set
  if (snk->custom_name_elem) {
    size_t size;
    const void *bytes = alsa_get_elem_bytes(snk->custom_name_elem, &size);

    if (bytes && size > 0) {
      size_t str_len = strnlen((const char *)bytes, size);
      if (str_len > 0 && g_utf8_validate((const char *)bytes, str_len, NULL)) {
        // create properly null-terminated copy
        snk->display_name = g_strndup((const char *)bytes, str_len);
        return;
      }
    }
  }

  // fall back to element name
  snk->display_name = g_strdup(snk->elem->name ? snk->elem->name : "");
}

// Callback when a routing source's custom name changes
// Updates the cached display name and triggers UI updates
static void src_custom_name_display_changed(
  struct alsa_elem   *elem,
  void               *private
) {
  struct routing_src *src = private;
  update_src_display_name(src);

  // update mixer window labels if mixer window exists
  struct alsa_card *card = src->card;
  if (card->window_mixer) {
    // update mixer input labels (showing which source is connected)
    update_mixer_labels(card);

    // update mixer output labels (Mix A, Mix B, etc.) if this is a mixer source
    if (src->port_category == PC_MIX) {
      char *label = get_mixer_output_label_for_mixer_window(src);
      if (src->mixer_label_left) {
        gtk_label_set_text(GTK_LABEL(src->mixer_label_left), label);
        gtk_widget_set_tooltip_text(src->mixer_label_left, label);
      }
      if (src->mixer_label_right) {
        gtk_label_set_text(GTK_LABEL(src->mixer_label_right), label);
        gtk_widget_set_tooltip_text(src->mixer_label_right, label);
      }
      g_free(label);
    }
  }

  // update talkback button label in routing window if it exists
  if (src->talkback_widget && src->port_category == PC_MIX) {
    // use custom name if set, otherwise just the letter (e.g., "A")
    const char *display_name = src->display_name;
    char *formatted_name = NULL;

    if (src->custom_name_elem) {
      size_t size;
      const void *bytes = alsa_get_elem_bytes(src->custom_name_elem, &size);
      size_t str_len = bytes ? strnlen((const char *)bytes, size) : 0;

      if (str_len > 0) {
        // custom name - use it as-is
        formatted_name = g_strdup(display_name);
      }
    }

    if (!formatted_name) {
      // default name - strip "Mix " prefix (src->name is "Mix X")
      formatted_name = g_strdup(src->name + 4);
    }

    boolean_widget_update_labels(
      src->talkback_widget, formatted_name, formatted_name
    );
    g_free(formatted_name);
  }

  // update routing window labels if widget exists
  if (src->widget) {
    // find the label child in the box widget and update it
    for (GtkWidget *child = gtk_widget_get_first_child(src->widget);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
      if (GTK_IS_LABEL(child)) {
        // check if this is a custom name or default
        if (src->custom_name_elem) {
          size_t size;
          const void *bytes = alsa_get_elem_bytes(src->custom_name_elem, &size);
          size_t str_len = bytes ? strnlen((const char *)bytes, size) : 0;

          if (str_len > 0) {
            // custom name - use it as-is
            gtk_label_set_text(GTK_LABEL(child), src->display_name);
            gtk_widget_set_tooltip_text(child, src->display_name);
          } else {
            // default name - format it
            const char *formatted_text = NULL;
            switch (src->port_category) {
              case PC_DSP:
              case PC_MIX:
                // strip prefix for DSP/Mix
                if (strlen(src->name) > 4) {
                  formatted_text = src->name + 4;
                } else {
                  formatted_text = src->name;
                }
                break;
              default:
                formatted_text = src->display_name;
                break;
            }
            gtk_label_set_text(GTK_LABEL(child), formatted_text);
            gtk_widget_set_tooltip_text(child, formatted_text);
          }
        }
        break;
      }
    }
  }
}

// Get formatted default name for a routing sink (ignoring custom name)
// Returns newly allocated string that must be freed
char *get_snk_default_name_formatted(struct routing_snk *snk) {
  if (!snk || !snk->elem)
    return g_strdup("");

  struct alsa_elem *elem = snk->elem;
  switch (elem->port_category) {
    case PC_HW:
      return g_strdup_printf("%s %d", hw_type_names[elem->hw_type], elem->lr_num);

    case PC_PCM:
      return g_strdup_printf("PCM %d", elem->lr_num);

    case PC_MIX:
      return g_strdup_printf("Mixer %d", elem->lr_num);

    case PC_DSP:
      return g_strdup_printf("DSP %d", elem->lr_num);

    default:
      return g_strdup(elem->name ? elem->name : "");
  }
}

// Get formatted display name for a routing sink (for UI display)
// Returns custom name if set, otherwise formatted default name
// Returns newly allocated string that must be freed
char *get_snk_display_name_formatted(struct routing_snk *snk) {
  if (!snk || !snk->elem)
    return g_strdup("");

  // check if custom name is set
  if (snk->custom_name_elem) {
    size_t size;
    const void *bytes = alsa_get_elem_bytes(snk->custom_name_elem, &size);
    size_t str_len = bytes ? strnlen((const char *)bytes, size) : 0;

    if (str_len > 0 && snk->display_name) {
      // custom name - use it as-is
      return g_strdup(snk->display_name);
    }
  }

  return get_snk_default_name_formatted(snk);
}

// Callback when a routing sink's custom name changes
// Updates the cached display name and triggers UI updates
static void snk_custom_name_display_changed(
  struct alsa_elem   *elem,
  void               *private
) {
  struct routing_snk *snk = private;
  update_snk_display_name(snk);

  // update routing window labels if box widget exists
  if (snk->box_widget) {
    // find the label child in the box widget and update it
    for (GtkWidget *child = gtk_widget_get_first_child(snk->box_widget);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
      if (GTK_IS_LABEL(child)) {
        char *formatted_name = get_snk_display_name_formatted(snk);
        gtk_label_set_text(GTK_LABEL(child), formatted_name);
        g_free(formatted_name);
        break;
      }
    }
  }
}

// Create simulated element for a routing source
static void create_src_custom_name_elem(
  struct alsa_card   *card,
  struct routing_src *src,
  GHashTable         *state
) {
  // skip PC_OFF
  if (src->port_category == PC_OFF)
    return;

  // generate element name
  char *elem_name = get_src_elem_name(src);
  if (!elem_name)
    return;

  // check if real element exists (for forward compatibility)
  struct alsa_elem *elem = get_elem_by_name(card->elems, elem_name);

  if (elem) {
    // real element exists, store the pointer and set up callbacks
    src->custom_name_elem = elem;
    g_free(elem_name);
    alsa_elem_add_callback(elem, src_custom_name_display_changed, src, NULL);
    update_src_display_name(src);
    return;
  }

  // create simulated element
  elem = alsa_create_optional_elem(
    card,
    elem_name,
    SND_CTL_ELEM_TYPE_BYTES,
    MAX_CUSTOM_NAME_LEN
  );
  g_free(elem_name);

  if (!elem) {
    fprintf(stderr, "Failed to create custom name element for source\n");
    return;
  }

  src->custom_name_elem = elem;

  // generate config key
  char *config_key = get_src_config_key(src);
  if (!config_key)
    return;

  // load value from state file
  const char *value = g_hash_table_lookup(state, config_key);
  if (value && *value) {
    alsa_set_elem_bytes(elem, value, strlen(value));
  }

  // register callback to save state on changes
  struct custom_name_save_data *callback_data =
    g_malloc0(sizeof(struct custom_name_save_data));
  callback_data->card = card;
  callback_data->config_key = config_key;  // transfer ownership

  alsa_elem_add_callback(
    elem, custom_name_changed, callback_data,
    custom_names_free_callback_data
  );

  // register callback to update display name and UI
  alsa_elem_add_callback(elem, src_custom_name_display_changed, src, NULL);

  // initialise cached display name
  update_src_display_name(src);
}

// Create simulated element for a routing sink
static void create_snk_custom_name_elem(
  struct alsa_card   *card,
  struct routing_snk *snk,
  GHashTable         *state
) {
  // skip sinks without valid port category
  if (snk->elem->port_category == PC_OFF)
    return;

  // generate element name
  char *elem_name = get_snk_elem_name(snk);
  if (!elem_name)
    return;

  // check if real element exists (for forward compatibility)
  struct alsa_elem *elem = get_elem_by_name(card->elems, elem_name);

  if (elem) {
    // real element exists, store the pointer and set up callbacks
    snk->custom_name_elem = elem;
    g_free(elem_name);
    alsa_elem_add_callback(elem, snk_custom_name_display_changed, snk, NULL);
    update_snk_display_name(snk);
    return;
  }

  // create simulated element
  elem = alsa_create_optional_elem(
    card,
    elem_name,
    SND_CTL_ELEM_TYPE_BYTES,
    MAX_CUSTOM_NAME_LEN
  );
  g_free(elem_name);

  if (!elem) {
    fprintf(stderr, "Failed to create custom name element for sink\n");
    return;
  }

  snk->custom_name_elem = elem;

  // generate config key
  char *config_key = get_snk_config_key(snk);
  if (!config_key)
    return;

  // load value from state file
  const char *value = g_hash_table_lookup(state, config_key);
  if (value && *value) {
    alsa_set_elem_bytes(elem, value, strlen(value));
  }

  // register callback to save state on changes
  struct custom_name_save_data *callback_data =
    g_malloc0(sizeof(struct custom_name_save_data));
  callback_data->card = card;
  callback_data->config_key = config_key;  // transfer ownership

  alsa_elem_add_callback(
    elem, custom_name_changed, callback_data,
    custom_names_free_callback_data
  );

  // register callback to update display name and UI
  alsa_elem_add_callback(elem, snk_custom_name_display_changed, snk, NULL);

  // initialise cached display name
  update_snk_display_name(snk);
}

// Initialise custom name elements for all routing sources and sinks
void custom_names_init(struct alsa_card *card) {
  if (!card->serial || !*card->serial) {
    // no serial number, can't persist state
    return;
  }

  // check if card has routing controls
  if (!card->routing_srcs || !card->routing_snks) {
    // card doesn't have routing controls
    return;
  }

  // load existing state
  GHashTable *state = optional_state_load(card);
  if (!state) {
    state = g_hash_table_new_full(
      g_str_hash, g_str_equal, g_free, g_free
    );
  }

  // create custom name elements for all routing sources
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    create_src_custom_name_elem(card, src, state);
  }

  // create custom name elements for all routing sinks
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    create_snk_custom_name_elem(card, snk, state);
  }

  g_hash_table_destroy(state);

  // initialise display names for sources/sinks without custom name elements
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    if (!src->display_name)
      update_src_display_name(src);
  }

  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    if (!snk->display_name)
      update_snk_display_name(snk);
  }
}

// Get display name for a routing source (returns cached value)
const char *get_routing_src_display_name(struct routing_src *src) {
  if (!src)
    return "";
  return src->display_name ? src->display_name : "";
}

// Get mixer output label for mixer window (returns "Mix A" for defaults)
// Returns newly allocated string that must be freed
char *get_mixer_output_label_for_mixer_window(struct routing_src *src) {
  if (!src || src->port_category != PC_MIX)
    return g_strdup("");

  const char *display_name = src->display_name;
  if (!display_name)
    return g_strdup_printf("Mix %c", src->port_num + 'A');

  // check if custom name is set
  if (src->custom_name_elem) {
    size_t size;
    const void *bytes = alsa_get_elem_bytes(src->custom_name_elem, &size);
    size_t str_len = bytes ? strnlen((const char *)bytes, size) : 0;

    if (str_len > 0) {
      // custom name - use it as-is
      return g_strdup(display_name);
    }
  }

  // default name - ensure it's in "Mix X" format
  return g_strdup_printf("Mix %c", src->port_num + 'A');
}

// Get display name for a routing sink (returns cached value)
const char *get_routing_snk_display_name(struct routing_snk *snk) {
  if (!snk)
    return "";
  return snk->display_name ? snk->display_name : "";
}
