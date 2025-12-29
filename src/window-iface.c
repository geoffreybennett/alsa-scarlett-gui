// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>
#include <string.h>

#include "iface-mixer.h"
#include "iface-no-mixer.h"
#include "iface-none.h"
#include "iface-unknown.h"
#include "iface-update.h"
#include "iface-waiting.h"
#include "main.h"
#include "menu.h"
#include "routing-lines.h"
#include "window-iface.h"
#include "window-mixer.h"
#include "window-startup.h"
#include "optional-controls.h"

static GtkWidget *no_cards_window;
static int window_count;

// Get the window title for a card
// Uses the Name element if available, otherwise card->name
// Returns newly allocated string that must be freed
char *get_card_window_title(struct alsa_card *card) {
  struct alsa_elem *name_elem = optional_controls_get_name_elem(card);

  if (name_elem) {
    size_t size;
    const void *bytes = alsa_get_elem_bytes(name_elem, &size);

    if (bytes && size > 0) {
      // find actual string length (up to first null byte)
      size_t str_len = strnlen((const char *)bytes, size);

      // only use if non-empty and valid UTF-8
      if (str_len > 0 && g_utf8_validate((const char *)bytes, str_len, NULL)) {
        char *custom_name = g_strndup((const char *)bytes, str_len);
        char *title = g_strdup_printf("%s - %s", card->name, custom_name);
        g_free(custom_name);
        return title;
      }
    }
  }

  // no custom name or it's empty, use serial number if available
  if (card->serial && *card->serial)
    return g_strdup_printf("%s - %s", card->name, card->serial);

  // no serial number either, just use card name
  return g_strdup(card->name);
}

// Update all window titles when the name changes
static void update_window_titles(struct alsa_elem *elem, void *private) {
  struct alsa_card *card = private;

  char *title = get_card_window_title(card);

  // update main window
  if (card->window_main)
    gtk_window_set_title(GTK_WINDOW(card->window_main), title);

  // update sub-windows with appropriate suffixes
  if (card->window_routing) {
    char *routing_title = g_strdup_printf("%s - Routing", title);
    gtk_window_set_title(GTK_WINDOW(card->window_routing), routing_title);
    g_free(routing_title);
  }

  if (card->window_mixer) {
    char *mixer_title = g_strdup_printf("%s - Mixer", title);
    gtk_window_set_title(GTK_WINDOW(card->window_mixer), mixer_title);
    g_free(mixer_title);
  }

  if (card->window_levels) {
    char *levels_title = g_strdup_printf("%s - Levels", title);
    gtk_window_set_title(GTK_WINDOW(card->window_levels), levels_title);
    g_free(levels_title);
  }

  if (card->window_configuration) {
    char *config_title = g_strdup_printf("%s - Configuration", title);
    gtk_window_set_title(GTK_WINDOW(card->window_configuration), config_title);
    g_free(config_title);
  }

  if (card->window_startup) {
    char *startup_title = g_strdup_printf("%s - Startup Configuration", title);
    gtk_window_set_title(GTK_WINDOW(card->window_startup), startup_title);
    g_free(startup_title);
  }

  if (card->window_dsp) {
    char *dsp_title = g_strdup_printf("%s - DSP", title);
    gtk_window_set_title(GTK_WINDOW(card->window_dsp), dsp_title);
    g_free(dsp_title);
  }

  g_free(title);
}

// Clean up gain widget lists
static void cleanup_gain_widget_lists(struct alsa_card *card) {
  for (GList *l = card->input_gain_widgets; l != NULL; l = l->next)
    g_free(l->data);
  g_list_free(card->input_gain_widgets);
  card->input_gain_widgets = NULL;

  for (GList *l = card->output_gain_widgets; l != NULL; l = l->next)
    g_free(l->data);
  g_list_free(card->output_gain_widgets);
  card->output_gain_widgets = NULL;

  for (GList *l = card->mixer_gain_widgets; l != NULL; l = l->next) {
    struct mixer_gain_widget *mg = l->data;
    if (mg->widget)
      g_object_unref(mg->widget);
    g_free(mg);
  }
  g_list_free(card->mixer_gain_widgets);
  card->mixer_gain_widgets = NULL;

  for (GList *l = card->dsp_comp_widgets; l != NULL; l = l->next)
    g_free(l->data);
  g_list_free(card->dsp_comp_widgets);
  card->dsp_comp_widgets = NULL;
}

// Clean up subwindows
static void cleanup_subwindows(struct alsa_card *card) {
  if (card->window_routing) {
    gtk_window_destroy(GTK_WINDOW(card->window_routing));
    card->window_routing = NULL;
  }
  if (card->window_mixer) {
    gtk_window_destroy(GTK_WINDOW(card->window_mixer));
    card->window_mixer = NULL;
  }
  if (card->window_levels) {
    gtk_window_destroy(GTK_WINDOW(card->window_levels));
    card->window_levels = NULL;
  }
  if (card->window_configuration) {
    gtk_window_destroy(GTK_WINDOW(card->window_configuration));
    card->window_configuration = NULL;
  }
  if (card->window_startup) {
    gtk_window_destroy(GTK_WINDOW(card->window_startup));
    card->window_startup = NULL;
  }
  if (card->window_modal) {
    gtk_window_destroy(GTK_WINDOW(card->window_modal));
    card->window_modal = NULL;
  }
  if (card->window_dsp) {
    gtk_window_destroy(GTK_WINDOW(card->window_dsp));
    card->window_dsp = NULL;
  }
}

// Handle main window close - clean up before window is destroyed
static gboolean main_window_close_request(GtkWindow *w, gpointer data) {
  struct alsa_card *card = data;

  // set to NULL first so timer callbacks can detect cleanup is happening
  card->window_main = NULL;

  // cancel the levels timer before destroying windows
  if (card->levels_timer) {
    g_source_remove(card->levels_timer);
    card->levels_timer = 0;
  }

  routing_levels_cleanup(card);
  cleanup_gain_widget_lists(card);
  cleanup_subwindows(card);

  return FALSE;
}

void create_card_window(struct alsa_card *card) {
  if (no_cards_window) {
    gtk_window_destroy(GTK_WINDOW(no_cards_window));
    no_cards_window = NULL;
  }

  // Replacing an existing window
  if (card->window_main)
    gtk_window_destroy(GTK_WINDOW(card->window_main));

  // New window
  else
    window_count++;

  int has_startup = true;
  int has_mixer = true;

  // Check if the FCP driver is not initialised yet
  if (card->driver_type == DRIVER_TYPE_SOCKET_UNINIT) {
    card->window_main_contents = create_iface_waiting_main(card);
    has_startup = false;
    has_mixer = false;

    // Create minimal window with only the waiting interface
    card->window_main = gtk_application_window_new(app);
    gtk_window_set_resizable(GTK_WINDOW(card->window_main), FALSE);

    char *title = get_card_window_title(card);
    gtk_window_set_title(GTK_WINDOW(card->window_main), title);
    g_free(title);

    g_signal_connect(
      card->window_main, "close-request",
      G_CALLBACK(main_window_close_request), card
    );

    gtk_window_set_child(GTK_WINDOW(card->window_main), card->window_main_contents);
    gtk_widget_set_visible(card->window_main, TRUE);

    return;
  }

  struct alsa_elem *msd_elem =
    get_elem_by_name(card->elems, "MSD Mode Switch");
  int in_msd_mode = msd_elem && alsa_get_elem_value(msd_elem);

  struct alsa_elem *firmware_elem =
    get_elem_by_name(card->elems, "Firmware Version");
  struct alsa_elem *min_firmware_elem =
    get_elem_by_name(card->elems, "Minimum Firmware Version");
  int firmware_version = 0;
  int min_firmware_version = 0;
  if (firmware_elem && min_firmware_elem) {
    firmware_version = alsa_get_elem_value(firmware_elem);
    min_firmware_version = alsa_get_elem_value(min_firmware_elem);
  }

  // Check if FCP/Scarlett4 device needs firmware update (4-valued comparison)
  int fcp_needs_update = 0;
  if (card->driver_type == DRIVER_TYPE_SOCKET &&
      card->best_firmware_version_4 && firmware_elem) {
    long *current = alsa_get_elem_int_values(firmware_elem);
    if (current) {
      for (int i = 0; i < 4; i++) {
        if ((uint32_t)current[i] < card->best_firmware_version_4[i]) {
          fcp_needs_update = 1;
          break;
        }
        if ((uint32_t)current[i] > card->best_firmware_version_4[i])
          break;
      }
      g_free(current);
    }
  }

  // Firmware update required
  // or firmware version available and in MSD mode
  // (updating will disable MSD mode)
  // or FCP device with newer firmware available
  if (firmware_version < min_firmware_version ||
      (card->best_firmware_version > firmware_version && in_msd_mode) ||
      fcp_needs_update) {
    card->window_main_contents = create_iface_update_main(card);
    has_startup = false;
    has_mixer = false;

  // Scarlett Gen 1
  } else if (get_elem_by_prefix(card->elems, "Matrix")) {
    card->window_main_contents = create_iface_mixer_main(card);
    has_startup = false;

  // Scarlett Gen 2, Gen 3 4i4+, Gen 4, Clarett, or Vocaster
  } else if (get_elem_by_prefix(card->elems, "Mixer")) {
    card->window_main_contents = create_iface_mixer_main(card);

  // Scarlett Gen 3 Solo or 2i2
  } else if (get_elem_by_prefix(card->elems, "Phantom")) {
    card->window_main_contents = create_iface_no_mixer_main(card);
    has_mixer = false;

  // Scarlett Gen 3+ or Vocaster in MSD Mode
  } else if (msd_elem) {
    card->window_main_contents = create_startup_controls(card);
    has_startup = false;
    has_mixer = false;

  // Unknown
  } else {
    card->window_main_contents = create_iface_unknown_main();
    has_startup = false;
    has_mixer = false;
  }

  card->window_main = gtk_application_window_new(app);
  gtk_window_set_resizable(GTK_WINDOW(card->window_main), FALSE);

  char *title = get_card_window_title(card);
  gtk_window_set_title(GTK_WINDOW(card->window_main), title);
  g_free(title);

  g_signal_connect(
    card->window_main, "close-request",
    G_CALLBACK(main_window_close_request), card
  );

  gtk_application_window_set_show_menubar(
    GTK_APPLICATION_WINDOW(card->window_main), TRUE
  );
  add_window_action_map(GTK_WINDOW(card->window_main));
  if (has_startup)
    add_startup_action_map(card);
  if (has_mixer)
    add_mixer_action_map(card);
  if (card->window_dsp)
    add_dsp_action_map(card);
  if (card->device)
    add_load_save_action_map(card);

  restore_window_visibility(card);

  gtk_window_set_child(
    GTK_WINDOW(card->window_main),
    card->window_main_contents
  );

  // register callback to update window titles when name changes
  struct alsa_elem *name_elem = optional_controls_get_name_elem(card);
  if (name_elem)
    alsa_elem_add_callback(name_elem, update_window_titles, card, NULL);

  gtk_widget_set_visible(card->window_main, TRUE);
}

void create_no_card_window(void) {
  if (!window_count)
    no_cards_window = create_window_iface_none(app);
}

void destroy_card_window(struct alsa_card *card) {
  // clean up first (close-request is not emitted by gtk_window_destroy,
  // only by user-initiated close)
  routing_levels_cleanup(card);
  cleanup_gain_widget_lists(card);
  cleanup_subwindows(card);

  if (card->window_main) {
    gtk_window_destroy(GTK_WINDOW(card->window_main));
    card->window_main = NULL;
  }

  // if last window, display the "no card found" blank window
  window_count--;
  create_no_card_window();
}

void check_modal_window_closed(void) {
  if (!window_count)
    gtk_widget_set_visible(no_cards_window, TRUE);
}
