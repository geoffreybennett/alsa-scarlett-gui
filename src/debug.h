// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Initialise debug system from ALSA_SCARLETT_GUI_DEBUG environment variable.
// Call once at startup. The env var is a comma-separated list of categories.
//
// Example: ALSA_SCARLETT_GUI_DEBUG=stereo-link,routing
void debug_init(void);

// Check if a debug category is enabled.
int debug_enabled(const char *category);
