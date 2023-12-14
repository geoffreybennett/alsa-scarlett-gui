// SPDX-FileCopyrightText: 2023 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <alsa/asoundlib.h>

#include "scarlett2.h"

#include "scarlett2-ioctls.h"

int scarlett2_open_card(char *alsa_name, snd_hwdep_t **hwdep) {
  return snd_hwdep_open(hwdep, alsa_name, SND_HWDEP_OPEN_DUPLEX);
}

int scarlett2_get_protocol_version(snd_hwdep_t *hwdep) {
  int version = 0;
  int err = snd_hwdep_ioctl(hwdep, SCARLETT2_IOCTL_PVERSION, &version);

  if (err < 0)
    return err;
  return version;
}

int scarlett2_close(snd_hwdep_t *hwdep) {
  return snd_hwdep_close(hwdep);
}

int scarlett2_reboot(snd_hwdep_t *hwdep) {
  return snd_hwdep_ioctl(hwdep, SCARLETT2_IOCTL_REBOOT, 0);
}

static int scarlett2_select_flash_segment(snd_hwdep_t *hwdep, int segment) {
  return snd_hwdep_ioctl(hwdep, SCARLETT2_IOCTL_SELECT_FLASH_SEGMENT, &segment);
}

static int scarlett2_erase_flash_segment(snd_hwdep_t *hwdep) {
  return snd_hwdep_ioctl(hwdep, SCARLETT2_IOCTL_ERASE_FLASH_SEGMENT, 0);
}

int scarlett2_erase_config(snd_hwdep_t *hwdep) {
  int err;

  err = scarlett2_select_flash_segment(hwdep, SCARLETT2_SEGMENT_ID_SETTINGS);
  if (err < 0)
    return err;
  return scarlett2_erase_flash_segment(hwdep);
}

int scarlett2_erase_firmware(snd_hwdep_t *hwdep) {
  int err;

  err = scarlett2_select_flash_segment(hwdep, SCARLETT2_SEGMENT_ID_FIRMWARE);
  if (err < 0)
    return err;
  return scarlett2_erase_flash_segment(hwdep);
}

int scarlett2_get_erase_progress(snd_hwdep_t *hwdep) {
  struct scarlett2_flash_segment_erase_progress progress;

  int err = snd_hwdep_ioctl(
    hwdep, SCARLETT2_IOCTL_GET_ERASE_PROGRESS, &progress
  );
  if (err < 0)
    return err;

  // translate progress from [1..num_blocks, 255] to [[0..100), 255]]
  if (progress.num_blocks == 0 ||
      progress.progress == 0 ||
      progress.progress == 255)
    return progress.progress;

  return (progress.progress - 1) * 100 / progress.num_blocks;
}
