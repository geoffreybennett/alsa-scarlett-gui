// SPDX-FileCopyrightText: 2023-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stddef.h>

#include "hardware.h"

struct scarlett2_device scarlett2_supported[] = {
  { 0x8203, "Scarlett 2nd Gen 6i6" },
  { 0x8204, "Scarlett 2nd Gen 18i8" },
  { 0x8201, "Scarlett 2nd Gen 18i20" },
  { 0x8211, "Scarlett 3rd Gen Solo" },
  { 0x8210, "Scarlett 3rd Gen 2i2" },
  { 0x8212, "Scarlett 3rd Gen 4i4" },
  { 0x8213, "Scarlett 3rd Gen 8i6" },
  { 0x8214, "Scarlett 3rd Gen 18i8" },
  { 0x8215, "Scarlett 3rd Gen 18i20" },
  { 0x8216, "Vocaster One" },
  { 0x8217, "Vocaster Two" },
  { 0x8218, "Scarlett 4th Gen Solo" },
  { 0x8219, "Scarlett 4th Gen 2i2" },
  { 0x821a, "Scarlett 4th Gen 4i4" },
  { 0x8206, "Clarett USB 2Pre" },
  { 0x8207, "Clarett USB 4Pre" },
  { 0x8208, "Clarett USB 8Pre" },
  { 0x820a, "Clarett+ 2Pre" },
  { 0x820b, "Clarett+ 4Pre" },
  { 0x820c, "Clarett+ 8Pre" },
  { 0, NULL }
};

struct scarlett2_device *get_device_for_pid(int pid) {
  for (int i = 0; scarlett2_supported[i].name; i++)
    if (scarlett2_supported[i].pid == pid)
      return &scarlett2_supported[i];

  return NULL;
}
