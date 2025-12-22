// SPDX-FileCopyrightText: 2024-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "asound-stream-parser.h"

int get_sample_rate_category(int sample_rate) {
  if (sample_rate <= 50000)
    return SR_LOW;
  if (sample_rate <= 100000)
    return SR_MID;
  return SR_HIGH;
}
