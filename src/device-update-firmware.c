// SPDX-FileCopyrightText: 2024-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>
#include "device-reset-config.h"
#include "fcp-shared.h"
#include "fcp-socket.h"
#include "scarlett2.h"
#include "scarlett2-firmware.h"
#include "scarlett4-firmware.h"
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

// Find a firmware section by type in a container
static struct scarlett4_firmware *find_firmware_section(
  struct scarlett4_firmware_container *container,
  enum scarlett4_firmware_type type
) {
  for (uint32_t i = 0; i < container->num_sections; i++)
    if (container->sections[i]->type == type)
      return container->sections[i];
  return NULL;
}

// HWDEP (Scarlett2) firmware update implementation
static gpointer update_firmware_hwdep(struct modal_data *modal_data) {
  struct alsa_card *card = modal_data->card;
  int err = 0;
  snd_hwdep_t *hwdep = NULL;
  struct scarlett2_firmware_file *firmware = NULL;

  update_progress(modal_data, g_strdup("Checking firmware..."), 0);
  firmware = scarlett2_get_best_firmware(card->pid);

  if (!firmware) {
    return update_progress(
      modal_data, g_strdup("No update firmware found for device"), -1
    );
  }

  if (firmware->header.usb_pid != card->pid) {
    scarlett2_free_firmware_file(firmware);
    return update_progress(
      modal_data, g_strdup("Firmware file does not match device"), -1
    );
  }

  update_progress(modal_data, g_strdup("Resetting configuration..."), 0);

  err = scarlett2_open_card(card->device, &hwdep);
  if (err < 0) {
    scarlett2_free_firmware_file(firmware);
    return update_progress(
      modal_data,
      g_strdup_printf("Unable to open hwdep interface: %s", snd_strerror(err)),
      -1
    );
  }

  err = scarlett2_erase_config(hwdep);
  if (err < 0) {
    scarlett2_close(hwdep);
    scarlett2_free_firmware_file(firmware);
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
      scarlett2_free_firmware_file(firmware);
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

  update_progress(modal_data, g_strdup("Erasing flash..."), 0);

  err = scarlett2_erase_firmware(hwdep);
  if (err < 0) {
    scarlett2_close(hwdep);
    scarlett2_free_firmware_file(firmware);
    return update_progress(
      modal_data,
      g_strdup_printf("Unable to erase firmware: %s", snd_strerror(err)),
      -1
    );
  }

  while (1) {
    g_usleep(50000);
    err = scarlett2_get_erase_progress(hwdep);
    if (err < 0) {
      scarlett2_close(hwdep);
      scarlett2_free_firmware_file(firmware);
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

  update_progress(modal_data, g_strdup("Writing firmware..."), 0);

  size_t offset = 0;
  size_t len = firmware->header.firmware_length;
  unsigned char *buf = firmware->firmware_data;

  while (offset < len) {
    err = snd_hwdep_write(hwdep, buf + offset, len - offset);
    if (err < 0) {
      scarlett2_close(hwdep);
      scarlett2_free_firmware_file(firmware);
      return update_progress(
        modal_data,
        g_strdup_printf("Unable to write firmware: %s", snd_strerror(err)),
        -1
      );
    }
    offset += err;
    update_progress(modal_data, NULL, (offset * 100) / len);
  }

  g_main_context_invoke(NULL, modal_start_reboot_progress, modal_data);
  scarlett2_reboot(hwdep);
  scarlett2_close(hwdep);
  scarlett2_free_firmware_file(firmware);

  return NULL;
}

// FCP socket (Scarlett4) firmware update implementation
static gpointer update_firmware_socket(struct modal_data *modal_data) {
  struct alsa_card *card = modal_data->card;
  struct scarlett4_firmware_container *container = NULL;
  struct scarlett4_firmware *app_fw = NULL;
  int err;

  update_progress(modal_data, g_strdup("Checking firmware..."), 0);
  container = scarlett4_get_best_firmware(card->pid);

  if (!container) {
    return update_progress(
      modal_data, g_strdup("No update firmware found for device"), -1
    );
  }

  if (container->usb_pid != card->pid) {
    scarlett4_free_firmware_container(container);
    return update_progress(
      modal_data, g_strdup("Firmware file does not match device"), -1
    );
  }

  // Find App firmware section
  app_fw = find_firmware_section(container, SCARLETT4_FIRMWARE_APP);
  if (!app_fw) {
    scarlett4_free_firmware_container(container);
    return update_progress(
      modal_data, g_strdup("No App firmware found in container"), -1
    );
  }

  // TODO: Implement full multi-step upgrade process:
  // 1. Check if ESP needs update
  // 2. If ESP needs update, check if Leapfrog is already loaded
  // 3. If Leapfrog needed: upload leapfrog -> reboot -> wait
  // 4. If ESP needed: upload ESP (no reboot)
  // 5. Upload App -> reboot
  //
  // For now, just erase and upload App firmware directly

  update_progress(modal_data, g_strdup("Erasing firmware..."), 0);

  err = fcp_socket_erase_app_firmware(card, fcp_progress_callback, modal_data);
  if (err < 0) {
    scarlett4_free_firmware_container(container);
    return update_progress(
      modal_data, g_strdup("Failed to erase firmware"), -1
    );
  }

  update_progress(modal_data, g_strdup("Uploading firmware..."), 0);

  fprintf(stderr, "DEBUG: app_fw->firmware_length=%u data=%p\n",
    app_fw->firmware_length, (void*)app_fw->firmware_data);

  err = fcp_socket_upload_firmware(
    card,
    FCP_SOCKET_REQUEST_APP_FIRMWARE_UPDATE,
    app_fw->firmware_data,
    app_fw->firmware_length,
    app_fw->usb_vid,
    app_fw->usb_pid,
    app_fw->sha256,
    NULL,  // No MD5 for App firmware
    fcp_progress_callback,
    modal_data
  );

  if (err < 0) {
    scarlett4_free_firmware_container(container);
    return update_progress(
      modal_data, g_strdup("Failed to upload firmware"), -1
    );
  }

  g_main_context_invoke(NULL, modal_start_reboot_progress, modal_data);
  fcp_socket_reboot_device(card);
  scarlett4_free_firmware_container(container);

  return NULL;
}

gpointer update_firmware_thread(gpointer user_data) {
  struct modal_data *modal_data = user_data;
  struct alsa_card *card = modal_data->card;

  if (card->driver_type == DRIVER_TYPE_HWDEP)
    return update_firmware_hwdep(modal_data);

  if (card->driver_type == DRIVER_TYPE_SOCKET)
    return update_firmware_socket(modal_data);

  return update_progress(
    modal_data, g_strdup("Unsupported driver type for firmware update"), -1
  );
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
