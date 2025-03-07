// SPDX-FileCopyrightText: 2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

// Error codes
#define FCP_SOCKET_ERR_INVALID_MAGIC    1
#define FCP_SOCKET_ERR_INVALID_COMMAND  2
#define FCP_SOCKET_ERR_INVALID_LENGTH   3
#define FCP_SOCKET_ERR_INVALID_HASH     4
#define FCP_SOCKET_ERR_INVALID_USB_ID   5
#define FCP_SOCKET_ERR_CONFIG           6
#define FCP_SOCKET_ERR_FCP              7
#define FCP_SOCKET_ERR_TIMEOUT          8
#define FCP_SOCKET_ERR_READ             9
#define FCP_SOCKET_ERR_WRITE           10
#define FCP_SOCKET_ERR_NOT_LEAPFROG    11
#define FCP_SOCKET_ERR_INVALID_STATE   12
#define FCP_SOCKET_ERR_MAX             12

// Protocol constants
#define FCP_SOCKET_PROTOCOL_VERSION 1
#define FCP_SOCKET_MAGIC_REQUEST    0x53
#define FCP_SOCKET_MAGIC_RESPONSE   0x73

// Maximum payload length (2MB)
#define MAX_PAYLOAD_LENGTH 2 * 1024 * 1024

// Request types
#define FCP_SOCKET_REQUEST_REBOOT               0x0001
#define FCP_SOCKET_REQUEST_CONFIG_ERASE         0x0002
#define FCP_SOCKET_REQUEST_APP_FIRMWARE_ERASE   0x0003
#define FCP_SOCKET_REQUEST_APP_FIRMWARE_UPDATE  0x0004
#define FCP_SOCKET_REQUEST_ESP_FIRMWARE_UPDATE  0x0005

// Response types
#define FCP_SOCKET_RESPONSE_VERSION  0x00
#define FCP_SOCKET_RESPONSE_SUCCESS  0x01
#define FCP_SOCKET_RESPONSE_ERROR    0x02
#define FCP_SOCKET_RESPONSE_PROGRESS 0x03

extern const char *fcp_socket_error_messages[];

// Message structures
#pragma pack(push, 1)

struct fcp_socket_msg_header {
  uint8_t  magic;
  uint8_t  msg_type;
  uint32_t payload_length;
};

struct firmware_payload {
  uint32_t size;
  uint16_t usb_vid;
  uint16_t usb_pid;
  uint8_t  sha256[32];
  uint8_t  md5[16];
  uint8_t  data[];
};

struct version_msg {
  struct fcp_socket_msg_header header;
  uint8_t                      version;
};

struct progress_msg {
  struct fcp_socket_msg_header header;
  uint8_t                      percent;
};

struct error_msg {
  struct fcp_socket_msg_header header;
  int16_t                      error_code;
};

#pragma pack(pop)

