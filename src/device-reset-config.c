// SPDX-FileCopyrightText: 2024-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>
#include "device-reset-config.h"
#include "fcp-socket.h"
#include "optional-state.h"
#include "scarlett2.h"
#include "scarlett2-ioctls.h"
#include "window-modal.h"

static gpointer update_progress(
  struct modal_data *modal_data,
  char              *text,
  int                progress
) {
  struct progress_data *progress_data = g_new0(struct progress_data, 1);
  progress_data->modal_data = modal_data;
  progress_data->text = text;
  progress_data->progress = progress;

  g_main_context_invoke(NULL, modal_update_progress, progress_data);
  return NULL;
}

// Progress callback for FCP socket
static void fcp_progress_callback(int percent, void *user_data) {
  struct modal_data *modal_data = user_data;
  update_progress(modal_data, NULL, percent);
}

// HWDEP (Scarlett2) reset config implementation
static gpointer reset_config_hwdep(struct modal_data *modal_data) {
  snd_hwdep_t *hwdep = NULL;

  int err = scarlett2_open_card(modal_data->card->device, &hwdep);
  if (err < 0) {
    return update_progress(
      modal_data,
      g_strdup_printf("Unable to open hwdep interface: %s", snd_strerror(err)),
      -1
    );
  }

  err = scarlett2_erase_config(hwdep);
  if (err < 0) {
    scarlett2_close(hwdep);
    return update_progress(
      modal_data,
      g_strdup_printf("Unable to reset configuration: %s", snd_strerror(err)),
      -1
    );
  }

  while (1) {
    g_usleep(50000);

    err = scarlett2_get_erase_progress(hwdep);
    if (err < 0) {
      scarlett2_close(hwdep);
      return update_progress(
        modal_data,
        g_strdup_printf("Unable to get erase progress: %s", snd_strerror(err)),
        -1
      );
    }
    if (err == 255)
      break;

    update_progress(modal_data, NULL, err);
  }

  g_main_context_invoke(NULL, modal_start_reboot_progress, modal_data);
  scarlett2_reboot(hwdep);
  scarlett2_close(hwdep);

  return NULL;
}

// FCP socket reset config implementation
static gpointer reset_config_socket(struct modal_data *modal_data) {
  int err = fcp_socket_reset_config(
    modal_data->card, fcp_progress_callback, modal_data
  );
  if (err < 0) {
    return update_progress(
      modal_data,
      g_strdup("Unable to reset configuration via FCP socket"),
      -1
    );
  }

  g_main_context_invoke(NULL, modal_start_reboot_progress, modal_data);
  fcp_socket_reboot_device(modal_data->card);

  return NULL;
}

gpointer reset_config_thread(gpointer user_data) {
  struct modal_data *modal_data = user_data;

  update_progress(modal_data, g_strdup("Resetting configuration..."), 0);

  // Remove the application config file for this device
  optional_state_remove(modal_data->card->serial);

  if (modal_data->card->driver_type == DRIVER_TYPE_HWDEP)
    return reset_config_hwdep(modal_data);

  if (modal_data->card->driver_type == DRIVER_TYPE_SOCKET)
    return reset_config_socket(modal_data);

  return update_progress(
    modal_data,
    g_strdup("Unsupported driver type for reset configuration"),
    -1
  );
}

static void join_thread(gpointer thread) {
  g_thread_join(thread);
}

static void reset_config_yes_callback(struct modal_data *modal_data) {
  GThread *thread = g_thread_new(
    "reset_config_thread", reset_config_thread, modal_data
  );
  g_object_set_data_full(
    G_OBJECT(modal_data->button_box), "thread", thread, join_thread
  );
}

void create_reset_config_window(GtkWidget *w, struct alsa_card *card) {
  create_modal_window(
    w, card,
    "Confirm Reset Configuration",
    "Resetting Configuration",
    "Are you sure you want to reset the configuration?",
    reset_config_yes_callback
  );
}
