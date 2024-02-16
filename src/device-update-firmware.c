// SPDX-FileCopyrightText: 2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>
#include "device-reset-config.h"
#include "scarlett2.h"
#include "scarlett2-firmware.h"
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

#define fail(msg) { \
  if (hwdep) \
    scarlett2_close(hwdep); \
  if (firmware) \
    scarlett2_free_firmware_file(firmware); \
  return update_progress(modal_data, msg, -1); \
}

#define failsndmsg(msg) g_strdup_printf(msg, snd_strerror(err))

gpointer update_firmware_thread(gpointer user_data) {
  struct modal_data *modal_data = user_data;
  struct alsa_card *card = modal_data->card;

  int err = 0;
  snd_hwdep_t *hwdep = NULL;

  // read the firmware file
  update_progress(modal_data, g_strdup("Checking firmware..."), 0);
  struct scarlett2_firmware_file *firmware =
    scarlett2_get_best_firmware(card->pid);

  // if no firmware, fail
  if (!firmware)
    fail(failsndmsg("No update firmware found for device: %s"));

  if (firmware->header.usb_pid != card->pid)
    fail(g_strdup("Firmware file does not match device"));

  update_progress(modal_data, g_strdup("Resetting configuration..."), 0);

  err = scarlett2_open_card(card->device, &hwdep);
  if (err < 0)
    fail(failsndmsg("Unable to open hwdep interface: %s"));

  err = scarlett2_erase_config(hwdep);
  if (err < 0)
    fail(failsndmsg("Unable to reset configuration: %s"));

  while (1) {
    g_usleep(50000);

    err = scarlett2_get_erase_progress(hwdep);
    if (err < 0)
      fail(failsndmsg("Unable to get erase progress: %s"));
    if (err == 255)
      break;

    update_progress(modal_data, NULL, err);
  }

  update_progress(modal_data, g_strdup("Erasing flash..."), 0);

  err = scarlett2_erase_firmware(hwdep);
  if (err < 0)
    fail(failsndmsg("Unable to erase upgrade firmware: %s"));

  while (1) {
    g_usleep(50000);

    err = scarlett2_get_erase_progress(hwdep);
    if (err < 0)
      fail(failsndmsg("Unable to get erase progress: %s"));
    if (err == 255)
      break;

    update_progress(modal_data, NULL, err);
  }

  update_progress(modal_data, g_strdup("Writing firmware..."), 0);

  size_t offset = 0;
  size_t len = firmware->header.firmware_length;
  unsigned char *buf = firmware->firmware_data;

  while (offset < len) {
    err = snd_hwdep_write(hwdep, buf + offset, len - offset);
    if (err < 0)
      fail(failsndmsg("Unable to write firmware: %s"));

    offset += err;

    update_progress(modal_data, NULL, (offset * 100) / len);
  }

  g_main_context_invoke(NULL, modal_start_reboot_progress, modal_data);
  scarlett2_reboot(hwdep);
  scarlett2_close(hwdep);

  return NULL;
}

static void join_thread(gpointer thread) {
  g_thread_join(thread);
}

static void update_firmware_yes_callback(struct modal_data *modal_data) {
  GThread *thread = g_thread_new(
    "update_firmware_thread", update_firmware_thread, modal_data
  );
  g_object_set_data_full(
    G_OBJECT(modal_data->button_box), "thread", thread, join_thread
  );
}

void create_update_firmware_window(GtkWidget *w, struct alsa_card *card) {
  create_modal_window(
    w, card,
    "Confirm Update Firmware",
    "Updating Firmware",
    "The firmware update process will take about 15 seconds.\n"
    "Please do not disconnect the device while updating.\n"
    "Ready to proceed?",
    update_firmware_yes_callback
  );
}
