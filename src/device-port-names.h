// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Get device-specific default port name
// Returns NULL if no device-specific name defined for this port
const char *get_device_port_name(
  int pid,           // USB product ID
  int port_category, // PC_HW, PC_PCM, PC_MIX
  int hw_type,       // for PC_HW: HW_TYPE_ANALOGUE, HW_TYPE_SPDIF, HW_TYPE_ADAT
  int is_snk,        // 0 = source, 1 = sink
  int port_num       // 0-based port number
);
