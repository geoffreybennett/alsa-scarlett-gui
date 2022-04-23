// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tooltips.h"

//intentionally not calling gettext with this macro
#define N_(String) String

// tooltips that are used from multiple files

const char *level_descr =
  N_("Mic/Line or Instrument Level (Impedance)");

const char *air_descr =
  N_("Enabling Air will transform your recordings and inspire you while "
  "making music.");

const char *phantom_descr =
  N_("Enabling 48V sends “Phantom Power” to the XLR microphone input. "
  "This is required for some microphones (such as condensor "
  "microphones), and damaging to some microphones (particularly "
  "vintage ribbon microphones).");
