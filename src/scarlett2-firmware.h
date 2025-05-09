// SPDX-FileCopyrightText: 2023-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

// System-wide firmware directory
#define SCARLETT2_FIRMWARE_DIR "/usr/lib/firmware/scarlett2"

#define MAGIC_STRING "SCARLETT"

struct scarlett2_firmware_header {
  char magic[8];             // "SCARLETT"
  uint16_t usb_vid;          // Big-endian
  uint16_t usb_pid;          // Big-endian
  uint32_t firmware_version; // Big-endian
  uint32_t firmware_length;  // Big-endian
  uint8_t sha256[32];
} __attribute__((packed));

struct scarlett2_firmware_file {
  struct scarlett2_firmware_header header;
  uint8_t *firmware_data;
};

struct scarlett2_firmware_header *scarlett2_read_firmware_header(
  const char *fn
);

void scarlett2_free_firmware_header(
  struct scarlett2_firmware_header *firmware
);

struct scarlett2_firmware_file *scarlett2_read_firmware_file(
  const char *fn
);

void scarlett2_free_firmware_file(
  struct scarlett2_firmware_file *firmware
);

void scarlett2_enum_firmware(void);

uint32_t scarlett2_get_best_firmware_version(uint32_t pid);
struct scarlett2_firmware_file *scarlett2_get_best_firmware(uint32_t pid);
