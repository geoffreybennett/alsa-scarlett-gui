// SPDX-FileCopyrightText: 2023-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stddef.h>

#include "hardware.h"

struct scarlett2_device scarlett2_supported[] = {
  { PID_SCARLETT_GEN2_6I6,   "Scarlett 2nd Gen 6i6" },
  { PID_SCARLETT_GEN2_18I8,  "Scarlett 2nd Gen 18i8" },
  { PID_SCARLETT_GEN2_18I20, "Scarlett 2nd Gen 18i20" },
  { PID_SCARLETT_GEN3_SOLO,  "Scarlett 3rd Gen Solo" },
  { PID_SCARLETT_GEN3_2I2,   "Scarlett 3rd Gen 2i2" },
  { PID_SCARLETT_GEN3_4I4,   "Scarlett 3rd Gen 4i4" },
  { PID_SCARLETT_GEN3_8I6,   "Scarlett 3rd Gen 8i6" },
  { PID_SCARLETT_GEN3_18I8,  "Scarlett 3rd Gen 18i8" },
  { PID_SCARLETT_GEN3_18I20, "Scarlett 3rd Gen 18i20" },
  { PID_VOCASTER_ONE,        "Vocaster One" },
  { PID_VOCASTER_TWO,        "Vocaster Two" },
  { PID_SCARLETT_GEN4_SOLO,  "Scarlett 4th Gen Solo" },
  { PID_SCARLETT_GEN4_2I2,   "Scarlett 4th Gen 2i2" },
  { PID_SCARLETT_GEN4_4I4,   "Scarlett 4th Gen 4i4" },
  { PID_SCARLETT_GEN4_16I16, "Scarlett 4th Gen 16i16" },
  { PID_SCARLETT_GEN4_18I16, "Scarlett 4th Gen 18i16" },
  { PID_SCARLETT_GEN4_18I20, "Scarlett 4th Gen 18i20" },
  { PID_CLARETT_USB_2PRE,    "Clarett USB 2Pre" },
  { PID_CLARETT_USB_4PRE,    "Clarett USB 4Pre" },
  { PID_CLARETT_USB_8PRE,    "Clarett USB 8Pre" },
  { PID_CLARETT_PLUS_2PRE,   "Clarett+ 2Pre" },
  { PID_CLARETT_PLUS_4PRE,   "Clarett+ 4Pre" },
  { PID_CLARETT_PLUS_8PRE,   "Clarett+ 8Pre" },
  { 0, NULL }
};

struct scarlett2_device *get_device_for_pid(int pid) {
  for (int i = 0; scarlett2_supported[i].name; i++)
    if (scarlett2_supported[i].pid == pid)
      return &scarlett2_supported[i];

  return NULL;
}
