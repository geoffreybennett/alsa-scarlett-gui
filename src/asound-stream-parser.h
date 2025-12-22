// SPDX-FileCopyrightText: 2024-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Sample rate categories
enum {
  SR_LOW,   // 44.1/48 kHz
  SR_MID,   // 88.2/96 kHz
  SR_HIGH,  // 176.4/192 kHz
  SR_COUNT
};

int get_sample_rate_category(int sample_rate);
