// SPDX-FileCopyrightText: 2023-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

// Supported devices
struct scarlett2_device {
  int         pid;
  const char *name;
};

struct scarlett2_device *get_device_for_pid(int pid);
