// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "iface-mixer.h"
#include "iface-no-mixer.h"
#include "iface-none.h"
#include "iface-unknown.h"
#include "iface-update.h"
#include "main.h"
#include "menu.h"
#include "window-iface.h"
#include "window-startup.h"

static GtkWidget *no_cards_window;
static int window_count;

void create_card_window(struct alsa_card *card) {
  if (no_cards_window) {
    gtk_window_destroy(GTK_WINDOW(no_cards_window));
    no_cards_window = NULL;
  }
  window_count++;

  int has_startup = true;
  int has_mixer = true;

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

  // Firmware update required
  // or firmware version available and in MSD mode
  // (updating will disable MSD mode)
  if (firmware_version < min_firmware_version ||
      (card->best_firmware_version > firmware_version &&
         in_msd_mode)) {
    card->window_main_contents = create_iface_update_main(card);
    has_startup = false;
    has_mixer = false;

  // Scarlett Gen 1
  } else if (get_elem_by_prefix(card->elems, "Matrix")) {
    card->window_main_contents = create_iface_mixer_main(card);

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
  gtk_window_set_title(GTK_WINDOW(card->window_main), card->name);
  gtk_application_window_set_show_menubar(
    GTK_APPLICATION_WINDOW(card->window_main), TRUE
  );
  add_window_action_map(GTK_WINDOW(card->window_main));
  if (has_startup)
    add_startup_action_map(card);
  if (has_mixer)
    add_mixer_action_map(card);
  if (card->device)
    add_load_save_action_map(card);

  gtk_window_set_child(
    GTK_WINDOW(card->window_main),
    card->window_main_contents
  );
  gtk_widget_set_visible(card->window_main, TRUE);
}

void create_no_card_window(void) {
  if (!window_count)
    no_cards_window = create_window_iface_none(app);
}

void destroy_card_window(struct alsa_card *card) {
  // remove the windows
  gtk_window_destroy(GTK_WINDOW(card->window_main));
  if (card->window_routing)
    gtk_window_destroy(GTK_WINDOW(card->window_routing));
  if (card->window_mixer)
    gtk_window_destroy(GTK_WINDOW(card->window_mixer));
  if (card->window_levels)
    gtk_window_destroy(GTK_WINDOW(card->window_levels));
  if (card->window_startup)
    gtk_window_destroy(GTK_WINDOW(card->window_startup));
  if (card->window_modal) {
    gtk_window_destroy(GTK_WINDOW(card->window_modal));
  }

  // if last window, display the "no card found" blank window
  window_count--;
  create_no_card_window();
}

void check_modal_window_closed(void) {
  if (!window_count)
    gtk_widget_set_visible(no_cards_window, TRUE);
}
