// SPDX-FileCopyrightText: 2023-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Device PIDs
#define PID_SCARLETT_GEN1_8I6    0x8002
#define PID_SCARLETT_GEN1_18I6   0x8004
#define PID_SCARLETT_GEN1_18I20  0x800c
#define PID_SCARLETT_GEN1_6I6    0x8012
#define PID_SCARLETT_GEN1_18I8   0x8014
#define PID_SCARLETT_GEN2_18I20  0x8201
#define PID_SCARLETT_GEN2_6I6    0x8203
#define PID_SCARLETT_GEN2_18I8   0x8204
#define PID_CLARETT_USB_2PRE     0x8206
#define PID_CLARETT_USB_4PRE     0x8207
#define PID_CLARETT_USB_8PRE     0x8208
#define PID_CLARETT_PLUS_2PRE    0x820a
#define PID_CLARETT_PLUS_4PRE    0x820b
#define PID_CLARETT_PLUS_8PRE    0x820c
#define PID_SCARLETT_GEN3_2I2    0x8210
#define PID_SCARLETT_GEN3_SOLO   0x8211
#define PID_SCARLETT_GEN3_4I4    0x8212
#define PID_SCARLETT_GEN3_8I6    0x8213
#define PID_SCARLETT_GEN3_18I8   0x8214
#define PID_SCARLETT_GEN3_18I20  0x8215
#define PID_VOCASTER_ONE         0x8216
#define PID_VOCASTER_TWO         0x8217
#define PID_SCARLETT_GEN4_SOLO   0x8218
#define PID_SCARLETT_GEN4_2I2    0x8219
#define PID_SCARLETT_GEN4_4I4    0x821a
#define PID_SCARLETT_GEN4_16I16  0x821b
#define PID_SCARLETT_GEN4_18I16  0x821c
#define PID_SCARLETT_GEN4_18I20  0x821d

// Supported devices
struct scarlett2_device {
  int         pid;
  const char *name;
};

struct scarlett2_device *get_device_for_pid(int pid);
