// SPDX-FileCopyrightText: 2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include "alsa.h"

// Connect to the FCP socket server for the given card
// Returns socket file descriptor on success, -1 on error
int fcp_socket_connect(struct alsa_card *card);

// Send a simple command with no payload to the server
// Returns 0 on success, -1 on error
int fcp_socket_send_command(int sock_fd, uint8_t command);

// Handle server responses from a command
// Returns 0 on success, -1 on error
int fcp_socket_handle_response(int sock_fd, bool show_progress);

// Wait for server to disconnect (used after reboot command)
// Returns 0 if disconnected, -1 on timeout or error
int fcp_socket_wait_for_disconnect(int sock_fd);

// Reboot a device using the FCP socket interface
// Returns 0 on success, -1 on error
int fcp_socket_reboot_device(struct alsa_card *card);

// Reset config using FCP socket, with progress callback
// Returns 0 on success, -1 on error
int fcp_socket_reset_config(
  struct alsa_card *card,
  void (*progress_callback)(int percent, void *user_data),
  void *user_data
);

// Erase app firmware using FCP socket, with progress callback
// Returns 0 on success, -1 on error
int fcp_socket_erase_app_firmware(
  struct alsa_card *card,
  void (*progress_callback)(int percent, void *user_data),
  void *user_data
);

// Upload firmware using FCP socket, with progress callback
// command should be FCP_SOCKET_REQUEST_APP_FIRMWARE_UPDATE or
// FCP_SOCKET_REQUEST_ESP_FIRMWARE_UPDATE
// Returns 0 on success, -1 on error
int fcp_socket_upload_firmware(
  struct alsa_card *card,
  uint8_t           command,
  const uint8_t    *firmware_data,
  uint32_t          firmware_size,
  uint16_t          usb_vid,
  uint16_t          usb_pid,
  const uint8_t    *sha256,
  const uint8_t    *md5,
  void (*progress_callback)(int percent, void *user_data),
  void *user_data
);
