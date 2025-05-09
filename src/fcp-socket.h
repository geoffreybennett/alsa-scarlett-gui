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