// SPDX-FileCopyrightText: 2026 Julien Mary <julien@quackkiller.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alsa.h"

// Export channel names to a WirePlumber config file in
// ~/.config/wireplumber/wireplumber.conf.d/
// and optionally restart WirePlumber to apply.
// Returns 0 on success, -1 on error.
int pipewire_export_names(struct alsa_card *card);
