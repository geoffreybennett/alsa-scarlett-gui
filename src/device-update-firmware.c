// SPDX-FileCopyrightText: 2024-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>
#include <unistd.h>
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

// Compare two 4-valued firmware versions
// Returns: -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
static int version_cmp(const uint32_t *v1, const uint32_t *v2) {
  for (int i = 0; i < 4; i++) {
    if (v1[i] < v2[i]) return -1;
    if (v1[i] > v2[i]) return 1;
  }
  return 0;
}

// FCP socket (Scarlett4) firmware update implementation
static gpointer update_firmware_socket(struct modal_data *modal_data) {
  struct alsa_card *card = modal_data->card;
  struct scarlett4_firmware_container *container = NULL;
  struct scarlett4_firmware *leapfrog_fw = NULL;
  struct scarlett4_firmware *esp_fw = NULL;
  struct scarlett4_firmware *app_fw = NULL;
  int sock_fd = -1;
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

  // Find firmware sections
  leapfrog_fw = find_firmware_section(container, SCARLETT4_FIRMWARE_LEAPFROG);
  esp_fw = find_firmware_section(container, SCARLETT4_FIRMWARE_ESP);
  app_fw = find_firmware_section(container, SCARLETT4_FIRMWARE_APP);

  if (!app_fw) {
    scarlett4_free_firmware_container(container);
    return update_progress(
      modal_data, g_strdup("No App firmware found in container"), -1
    );
  }

  // Check if ESP needs update
  int need_esp = 0;
  if (esp_fw) {
    if (version_cmp(card->esp_firmware_version, esp_fw->firmware_version) != 0)
      need_esp = 1;
  }

  // Check if Leapfrog is already loaded (device firmware == leapfrog version)
  int leapfrog_loaded = 0;
  if (leapfrog_fw) {
    if (version_cmp(card->firmware_version_4, leapfrog_fw->firmware_version) == 0)
      leapfrog_loaded = 1;
  }

  // If ESP needs update and Leapfrog is NOT loaded, we need to load it first
  int need_leapfrog = need_esp && leapfrog_fw && !leapfrog_loaded;

  fprintf(stderr, "FW update: device=%u.%u.%u.%u esp=%u.%u.%u.%u\n",
    card->firmware_version_4[0], card->firmware_version_4[1],
    card->firmware_version_4[2], card->firmware_version_4[3],
    card->esp_firmware_version[0], card->esp_firmware_version[1],
    card->esp_firmware_version[2], card->esp_firmware_version[3]);
  if (leapfrog_fw)
    fprintf(stderr, "FW update: leapfrog=%u.%u.%u.%u\n",
      leapfrog_fw->firmware_version[0], leapfrog_fw->firmware_version[1],
      leapfrog_fw->firmware_version[2], leapfrog_fw->firmware_version[3]);
  if (esp_fw)
    fprintf(stderr, "FW update: esp_fw=%u.%u.%u.%u\n",
      esp_fw->firmware_version[0], esp_fw->firmware_version[1],
      esp_fw->firmware_version[2], esp_fw->firmware_version[3]);
  if (app_fw)
    fprintf(stderr, "FW update: app_fw=%u.%u.%u.%u\n",
      app_fw->firmware_version[0], app_fw->firmware_version[1],
      app_fw->firmware_version[2], app_fw->firmware_version[3]);
  fprintf(stderr, "FW update: need_esp=%d leapfrog_loaded=%d need_leapfrog=%d\n",
    need_esp, leapfrog_loaded, need_leapfrog);

  // Open connection once for all operations
  sock_fd = fcp_socket_connect(card);
  if (sock_fd < 0) {
    scarlett4_free_firmware_container(container);
    return update_progress(
      modal_data, g_strdup("Failed to connect to device"), -1
    );
  }

  // Step 1: Upload Leapfrog if needed, then reboot and exit
  // When device comes back running leapfrog, the upgrade prompt will appear
  // again and we'll detect leapfrog_loaded and continue with ESP + App
  if (need_leapfrog) {
    update_progress(modal_data, g_strdup("Erasing app firmware..."), 0);
    err = fcp_socket_erase_app_firmware_fd(sock_fd, fcp_progress_callback, modal_data);
    if (err < 0) {
      close(sock_fd);
      scarlett4_free_firmware_container(container);
      return update_progress(
        modal_data, g_strdup("Failed to erase app firmware"), -1
      );
    }

    update_progress(modal_data, g_strdup("Uploading leapfrog firmware..."), 0);
    err = fcp_socket_upload_firmware_fd(
      sock_fd, FCP_SOCKET_REQUEST_APP_FIRMWARE_UPDATE,
      leapfrog_fw->firmware_data, leapfrog_fw->firmware_length,
      leapfrog_fw->usb_vid, leapfrog_fw->usb_pid,
      leapfrog_fw->sha256, NULL,
      fcp_progress_callback, modal_data
    );
    if (err < 0) {
      close(sock_fd);
      scarlett4_free_firmware_container(container);
      return update_progress(
        modal_data, g_strdup("Failed to upload leapfrog firmware"), -1
      );
    }

    // Reboot - modal will wait for device to come back
    g_main_context_invoke(NULL, modal_start_reboot_progress, modal_data);
    fcp_socket_reboot_device_fd(sock_fd);
    close(sock_fd);
    scarlett4_free_firmware_container(container);
    return NULL;
  }

  // Step 2: Upload ESP if needed (no reboot after)
  if (need_esp) {
    update_progress(modal_data, g_strdup("Uploading ESP firmware..."), 0);
    err = fcp_socket_upload_firmware_fd(
      sock_fd, FCP_SOCKET_REQUEST_ESP_FIRMWARE_UPDATE,
      esp_fw->firmware_data, esp_fw->firmware_length,
      esp_fw->usb_vid, esp_fw->usb_pid,
      esp_fw->sha256, esp_fw->md5,
      fcp_progress_callback, modal_data
    );
    if (err < 0) {
      close(sock_fd);
      scarlett4_free_firmware_container(container);
      return update_progress(
        modal_data, g_strdup("Failed to upload ESP firmware"), -1
      );
    }
  }

  // Step 3: Erase and upload App firmware
  update_progress(modal_data, g_strdup("Erasing app firmware..."), 0);
  err = fcp_socket_erase_app_firmware_fd(sock_fd, fcp_progress_callback, modal_data);
  if (err < 0) {
    close(sock_fd);
    scarlett4_free_firmware_container(container);
    return update_progress(
      modal_data, g_strdup("Failed to erase app firmware"), -1
    );
  }

  update_progress(modal_data, g_strdup("Uploading app firmware..."), 0);
  err = fcp_socket_upload_firmware_fd(
    sock_fd, FCP_SOCKET_REQUEST_APP_FIRMWARE_UPDATE,
    app_fw->firmware_data, app_fw->firmware_length,
    app_fw->usb_vid, app_fw->usb_pid,
    app_fw->sha256, NULL,
    fcp_progress_callback, modal_data
  );
  if (err < 0) {
    close(sock_fd);
    scarlett4_free_firmware_container(container);
    return update_progress(
      modal_data, g_strdup("Failed to upload app firmware"), -1
    );
  }

  // Final reboot
  g_main_context_invoke(NULL, modal_start_reboot_progress, modal_data);
  fcp_socket_reboot_device_fd(sock_fd);
  close(sock_fd);
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

void create_update_firmware_window(
  GtkWidget        *w,
  struct alsa_card *card,
  GtkWidget        *parent_label
) {
  create_modal_window_autostart(
    w, card,
    "Updating Firmware",
    "Updating Firmware",
    "Please do not disconnect the device while updating.",
    update_firmware_yes_callback,
    parent_label
  );
}
