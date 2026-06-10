// SPDX-FileCopyrightText: 2023-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#include "alsa.h"

// Firmware directory for big Gen 4 devices
#define SCARLETT4_FIRMWARE_DIR "/usr/lib/firmware/scarlett4"

enum scarlett4_firmware_type {
  SCARLETT4_FIRMWARE_CONTAINER,
  SCARLETT4_FIRMWARE_APP,
  SCARLETT4_FIRMWARE_ESP,
  SCARLETT4_FIRMWARE_LEAPFROG,
  SCARLETT4_FIRMWARE_TYPE_COUNT
};

extern const char *scarlett4_firmware_type_magic[SCARLETT4_FIRMWARE_TYPE_COUNT];

/* On-disk format of the firmware container header and the firmware
 * header. These are preceded by the 8-byte magic string identifying
 * the type of the following header.
 */
struct scarlett4_firmware_container_header_disk {
  uint16_t usb_vid;             // Big-endian
  uint16_t usb_pid;             // Big-endian
  uint32_t firmware_version[4]; // Big-endian
  uint32_t num_sections;        // Big-endian
} __attribute__((packed));

struct scarlett4_firmware_header_disk {
  uint16_t usb_vid;             // Big-endian
  uint16_t usb_pid;             // Big-endian
  uint32_t firmware_version[4]; // Big-endian
  uint32_t firmware_length;     // Big-endian
  uint8_t  sha256[32];
} __attribute__((packed));

/* In-memory representation of the firmware */
struct scarlett4_firmware {
  enum scarlett4_firmware_type type;
  uint16_t  usb_vid;
  uint16_t  usb_pid;
  uint32_t  firmware_version[4];
  uint32_t  firmware_length;
  uint8_t   sha256[32];
  uint8_t   md5[16];
  uint8_t  *firmware_data;
};

/* In-memory representation of the firmware container */
struct scarlett4_firmware_container {
  uint16_t                   usb_vid;
  uint16_t                   usb_pid;
  uint32_t                   firmware_version[4];
  uint32_t                   num_sections;
  struct scarlett4_firmware **sections;
};

/* Read just the firmware container header from a file */
struct scarlett4_firmware_container *scarlett4_read_firmware_header(const char *fn);

/* Read all sections of a firmware container from a file */
struct scarlett4_firmware_container *scarlett4_read_firmware_file(const char *fn);

void scarlett4_free_firmware_container(struct scarlett4_firmware_container *container);

const char *scarlett4_firmware_type_to_string(enum scarlett4_firmware_type type);

/* Enumerate firmware files in the firmware directory */
void scarlett4_enum_firmware(void);

/* Get the best firmware version for a device (4-valued), or NULL if none */
uint32_t *scarlett4_get_best_firmware_version(uint32_t pid);

/* Get the best firmware container for a device, or NULL if none */
struct scarlett4_firmware_container *scarlett4_get_best_firmware(uint32_t pid);

/* Check if device is in mid-upgrade state (leapfrog loaded, ESP needs update)
 * Returns 1 if mid-upgrade, 0 otherwise */
int scarlett4_is_mid_upgrade(struct alsa_card *card);
