// SPDX-FileCopyrightText: 2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <fcntl.h>

#include "fcp-shared.h"
#include "fcp-socket.h"
#include "error.h"

// Connect to the FCP socket server for the given card
int fcp_socket_connect(struct alsa_card *card) {
  if (!card || !card->fcp_socket) {
    fprintf(stderr, "FCP socket path is not available");
    return -1;
  }

  int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock_fd < 0) {
    fprintf(stderr, "Cannot create socket: %s", strerror(errno));
    return -1;
  }

  struct sockaddr_un addr = {
    .sun_family = AF_UNIX
  };
  strncpy(addr.sun_path, card->fcp_socket, sizeof(addr.sun_path) - 1);

  if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    fprintf(stderr, "Cannot connect to server at %s: %s",
            addr.sun_path, strerror(errno));
    close(sock_fd);
    return -1;
  }

  return sock_fd;
}

// Send a simple command with no payload to the server
int fcp_socket_send_command(int sock_fd, uint8_t command) {
  struct fcp_socket_msg_header header = {
    .magic = FCP_SOCKET_MAGIC_REQUEST,
    .msg_type = command,
    .payload_length = 0
  };

  if (write(sock_fd, &header, sizeof(header)) != sizeof(header)) {
    fprintf(stderr, "Error sending command: %s", strerror(errno));
    return -1;
  }

  return 0;
}

// Handle server responses from a command
int fcp_socket_handle_response(int sock_fd, bool show_progress) {
  struct fcp_socket_msg_header header;
  ssize_t bytes_read;

  // Read response header
  bytes_read = read(sock_fd, &header, sizeof(header));
  if (bytes_read != sizeof(header)) {
    if (bytes_read == 0) {
      // Server closed the connection
      return 0;
    }
    fprintf(stderr, "Error reading response header: %s", strerror(errno));
    return -1;
  }

  // Verify the magic value
  if (header.magic != FCP_SOCKET_MAGIC_RESPONSE) {
    fprintf(stderr, "Invalid response magic: 0x%02x", header.magic);
    return -1;
  }

  // Handle different response types
  switch (header.msg_type) {
    case FCP_SOCKET_RESPONSE_VERSION: {
      // Protocol version response
      uint8_t version;
      bytes_read = read(sock_fd, &version, sizeof(version));
      if (bytes_read != sizeof(version)) {
        fprintf(stderr, "Error reading version: %s", strerror(errno));
        return -1;
      }
      // Protocol version mismatch?
      if (version != FCP_SOCKET_PROTOCOL_VERSION) {
        fprintf(stderr, "Protocol version mismatch: expected %d, got %d",
                FCP_SOCKET_PROTOCOL_VERSION, version);
        return -1;
      }
      break;
    }

    case FCP_SOCKET_RESPONSE_SUCCESS:
      // Command completed successfully
      return 0;

    case FCP_SOCKET_RESPONSE_ERROR: {
      // Error response
      int16_t error_code;
      bytes_read = read(sock_fd, &error_code, sizeof(error_code));
      if (bytes_read != sizeof(error_code)) {
        fprintf(stderr, "Error reading error code: %s", strerror(errno));
        return -1;
      }

      if (error_code > 0 && error_code <= FCP_SOCKET_ERR_MAX) {
        fprintf(stderr, "Server error: %s", fcp_socket_error_messages[error_code]);
      } else {
        fprintf(stderr, "Unknown server error code: %d", error_code);
      }
      return -1;
    }

    case FCP_SOCKET_RESPONSE_PROGRESS: {
      // Progress update
      if (show_progress) {
        uint8_t percent;
        bytes_read = read(sock_fd, &percent, sizeof(percent));
        if (bytes_read != sizeof(percent)) {
          fprintf(stderr, "Error reading progress: %s", strerror(errno));
          return -1;
        }
        fprintf(stderr, "\rProgress: %d%%", percent);
        if (percent == 100)
          fprintf(stderr, "\n");
      } else {
        // Skip the progress byte
        uint8_t dummy;
        if (read(sock_fd, &dummy, sizeof(dummy)) < 0) {
          fprintf(stderr, "Error reading progress: %s", strerror(errno));
          return -1;
        }
      }

      // Continue reading responses
      return fcp_socket_handle_response(sock_fd, show_progress);
    }

    default:
      fprintf(stderr, "Unknown response type: 0x%02x", header.msg_type);
      return -1;
  }

  return 0;
}

// Wait for server to disconnect (used after reboot command)
int fcp_socket_wait_for_disconnect(int sock_fd) {
  fd_set rfds;
  struct timeval tv, start_time, now;
  char buf[1];
  const int TIMEOUT_SECS = 2;

  gettimeofday(&start_time, NULL);

  while (1) {
    FD_ZERO(&rfds);
    FD_SET(sock_fd, &rfds);

    gettimeofday(&now, NULL);
    int elapsed = now.tv_sec - start_time.tv_sec;
    if (elapsed >= TIMEOUT_SECS) {
      fprintf(stderr, "Timeout waiting for server disconnect\n");
      return -1;
    }

    tv.tv_sec = TIMEOUT_SECS - elapsed;
    tv.tv_usec = 0;

    int ret = select(sock_fd + 1, &rfds, NULL, NULL, &tv);
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      fprintf(stderr, "Select error: %s\n", strerror(errno));
      return -1;
    }

    if (ret > 0) {
      // Try to read one byte
      ssize_t n = read(sock_fd, buf, 1);
      if (n < 0) {
        if (errno == EINTR || errno == EAGAIN)
          continue;
        fprintf(stderr, "Read error: %s\n", strerror(errno));
        return -1;
      }
      if (n == 0) {
        // EOF received - server has disconnected
        return 0;
      }
      // Ignore any data received, just keep waiting for EOF
    }
  }
}

// Reboot a device using the FCP socket interface
int fcp_socket_reboot_device(struct alsa_card *card) {
  int sock_fd, ret = -1;

  sock_fd = fcp_socket_connect(card);
  if (sock_fd < 0)
    return -1;

  // Send reboot command and wait for server to disconnect
  if (fcp_socket_send_command(sock_fd, FCP_SOCKET_REQUEST_REBOOT) == 0)
    ret = fcp_socket_wait_for_disconnect(sock_fd);

  close(sock_fd);
  return ret;
}

// Handle server responses with progress callback
static int handle_response_with_progress(
  int   sock_fd,
  void  (*progress_callback)(int percent, void *user_data),
  void  *user_data
) {
  struct fcp_socket_msg_header header;
  ssize_t bytes_read;

  while (1) {
    bytes_read = read(sock_fd, &header, sizeof(header));
    if (bytes_read != sizeof(header)) {
      if (bytes_read == 0)
        return 0;
      fprintf(stderr, "Error reading response header: %s", strerror(errno));
      return -1;
    }

    if (header.magic != FCP_SOCKET_MAGIC_RESPONSE) {
      fprintf(stderr, "Invalid response magic: 0x%02x", header.magic);
      return -1;
    }

    switch (header.msg_type) {
      case FCP_SOCKET_RESPONSE_VERSION: {
        uint8_t version;
        bytes_read = read(sock_fd, &version, sizeof(version));
        if (bytes_read != sizeof(version)) {
          fprintf(stderr, "Error reading version: %s", strerror(errno));
          return -1;
        }
        if (version != FCP_SOCKET_PROTOCOL_VERSION) {
          fprintf(stderr, "Protocol version mismatch: expected %d, got %d",
                  FCP_SOCKET_PROTOCOL_VERSION, version);
          return -1;
        }
        break;
      }

      case FCP_SOCKET_RESPONSE_SUCCESS:
        return 0;

      case FCP_SOCKET_RESPONSE_ERROR: {
        int16_t error_code;
        bytes_read = read(sock_fd, &error_code, sizeof(error_code));
        if (bytes_read != sizeof(error_code)) {
          fprintf(stderr, "Error reading error code: %s", strerror(errno));
          return -1;
        }
        if (error_code > 0 && error_code <= FCP_SOCKET_ERR_MAX)
          fprintf(stderr, "Server error: %s", fcp_socket_error_messages[error_code]);
        else
          fprintf(stderr, "Unknown server error code: %d", error_code);
        return -1;
      }

      case FCP_SOCKET_RESPONSE_PROGRESS: {
        uint8_t percent;
        bytes_read = read(sock_fd, &percent, sizeof(percent));
        if (bytes_read != sizeof(percent)) {
          fprintf(stderr, "Error reading progress: %s", strerror(errno));
          return -1;
        }
        if (progress_callback)
          progress_callback(percent, user_data);
        break;
      }

      default:
        fprintf(stderr, "Unknown response type: 0x%02x", header.msg_type);
        return -1;
    }
  }
}

// Reset config using FCP socket, with progress callback
int fcp_socket_reset_config(
  struct alsa_card *card,
  void (*progress_callback)(int percent, void *user_data),
  void *user_data
) {
  int sock_fd, ret = -1;

  sock_fd = fcp_socket_connect(card);
  if (sock_fd < 0)
    return -1;

  if (fcp_socket_send_command(sock_fd, FCP_SOCKET_REQUEST_CONFIG_ERASE) == 0)
    ret = handle_response_with_progress(sock_fd, progress_callback, user_data);

  close(sock_fd);
  return ret;
}

// Erase app firmware using FCP socket, with progress callback
int fcp_socket_erase_app_firmware(
  struct alsa_card *card,
  void (*progress_callback)(int percent, void *user_data),
  void *user_data
) {
  int sock_fd, ret = -1;

  sock_fd = fcp_socket_connect(card);
  if (sock_fd < 0)
    return -1;

  if (fcp_socket_send_command(sock_fd, FCP_SOCKET_REQUEST_APP_FIRMWARE_ERASE) == 0)
    ret = handle_response_with_progress(sock_fd, progress_callback, user_data);

  close(sock_fd);
  return ret;
}

// Upload firmware using FCP socket, with progress callback
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
) {
  int sock_fd, ret = -1;

  sock_fd = fcp_socket_connect(card);
  if (sock_fd < 0)
    return -1;

  // Build and send header
  // Payload = firmware_payload header (56 bytes) + firmware data
  struct fcp_socket_msg_header header = {
    .magic = FCP_SOCKET_MAGIC_REQUEST,
    .msg_type = command,
    .payload_length = sizeof(struct firmware_payload) + firmware_size
  };

  if (write(sock_fd, &header, sizeof(header)) != sizeof(header)) {
    fprintf(stderr, "Error sending header: %s", strerror(errno));
    goto out;
  }

  // Build and send firmware payload header
  struct firmware_payload payload = {
    .size = firmware_size,
    .usb_vid = usb_vid,
    .usb_pid = usb_pid
  };
  memcpy(payload.sha256, sha256, 32);
  if (md5)
    memcpy(payload.md5, md5, 16);

  if (write(sock_fd, &payload, sizeof(payload)) != sizeof(payload)) {
    fprintf(stderr, "Error sending payload header: %s", strerror(errno));
    goto out;
  }

  // Send firmware data
  ssize_t written = write(sock_fd, firmware_data, firmware_size);
  if (written != firmware_size) {
    fprintf(stderr, "Error sending firmware data: %s", strerror(errno));
    goto out;
  }

  // Handle server responses
  ret = handle_response_with_progress(sock_fd, progress_callback, user_data);

out:
  close(sock_fd);
  return ret;
}
