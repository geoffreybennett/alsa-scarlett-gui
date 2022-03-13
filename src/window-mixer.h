// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alsa.h"

GtkWidget *create_mixer_controls(struct alsa_card *card);
void update_mixer_labels(struct alsa_card *card);
