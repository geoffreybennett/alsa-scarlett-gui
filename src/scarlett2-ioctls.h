// SPDX-FileCopyrightText: 2023 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCARLETT2_IOCTLS_H
#define SCARLETT2_IOCTLS_H

#include <alsa/hwdep.h>

int scarlett2_open_card(char *alsa_name, snd_hwdep_t **hwdep);
int scarlett2_get_protocol_version(snd_hwdep_t *hwdep);
int scarlett2_lock(snd_hwdep_t *hwdep);
int scarlett2_unlock(snd_hwdep_t *hwdep);
int scarlett2_close(snd_hwdep_t *hwdep);

int scarlett2_reboot(snd_hwdep_t *hwdep);
int scarlett2_erase_config(snd_hwdep_t *hwdep);
int scarlett2_erase_firmware(snd_hwdep_t *hwdep);
int scarlett2_get_erase_progress(snd_hwdep_t *hwdep);
int scarlett2_write_firmware(
  snd_hwdep_t *hwdep,
  off_t offset,
  unsigned char *buf,
  size_t buf_len
);

#endif // SCARLETT2_IOCTLS_H
