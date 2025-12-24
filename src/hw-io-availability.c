// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include "hardware.h"
#include "hw-io-availability.h"

// Digital I/O Mode names (Gen 3 uses "S/PDIF" prefix, Gen 4 uses suffix)
#define MODE_ADAT           "ADAT"
#define MODE_NONE           "None"
#define MODE_OPTICAL        "Optical"
#define MODE_RCA            "RCA"
#define MODE_OPTICAL_SPDIF  "Optical S/PDIF"
#define MODE_SPDIF_OPTICAL  "S/PDIF Optical"
#define MODE_RCA_SPDIF      "RCA S/PDIF"
#define MODE_SPDIF_RCA      "S/PDIF RCA"
#define MODE_DUAL_ADAT      "Dual ADAT"

// Array indices are SR_LOW, SR_MID, SR_HIGH
struct io_limits {
  uint32_t    pid;
  const char *mode;
  int         spdif_in[SR_COUNT];
  int         spdif_out[SR_COUNT];
  int         adat_in[SR_COUNT];
  int         adat_out[SR_COUNT];
};

static const struct io_limits limits_table[] = {
  //                                          spdif_in       spdif_out      adat_in        adat_out
  // 2Pre has no Digital I/O mode selector (NULL mode)
  { PID_CLARETT_USB_2PRE,    NULL,               { 2, 2, 0 },   { 2, 2, 2 },   { 8, 4, 0 },   { 0, 0, 0 }   },
  { PID_CLARETT_PLUS_2PRE,   NULL,               { 2, 2, 0 },   { 2, 2, 2 },   { 8, 4, 0 },   { 0, 0, 0 }   },

  { PID_CLARETT_USB_4PRE,    MODE_NONE,          { 0, 0, 0 },   { 2, 2, 2 },   { 8, 4, 0 },   { 8, 4, 0 }   },
  { PID_CLARETT_USB_4PRE,    MODE_OPTICAL,       { 2, 2, 2 },   { 2, 2, 2 },   { 0, 0, 0 },   { 8, 4, 0 }   },
  { PID_CLARETT_USB_4PRE,    MODE_RCA,           { 2, 2, 2 },   { 2, 2, 2 },   { 8, 4, 0 },   { 8, 4, 0 }   },

  { PID_CLARETT_PLUS_4PRE,   MODE_NONE,          { 0, 0, 0 },   { 2, 2, 2 },   { 8, 4, 0 },   { 8, 4, 0 }   },
  { PID_CLARETT_PLUS_4PRE,   MODE_OPTICAL,       { 2, 2, 2 },   { 2, 2, 2 },   { 0, 0, 0 },   { 8, 4, 0 }   },
  { PID_CLARETT_PLUS_4PRE,   MODE_RCA,           { 2, 2, 2 },   { 2, 2, 2 },   { 8, 4, 0 },   { 8, 4, 0 }   },

  { PID_CLARETT_USB_8PRE,    MODE_NONE,          { 0, 0, 0 },   { 2, 2, 2 },   { 8, 4, 0 },   { 8, 4, 0 }   },
  { PID_CLARETT_USB_8PRE,    MODE_OPTICAL,       { 2, 2, 2 },   { 2, 2, 2 },   { 0, 0, 0 },   { 8, 4, 0 }   },
  { PID_CLARETT_USB_8PRE,    MODE_RCA,           { 2, 2, 2 },   { 2, 2, 2 },   { 8, 4, 0 },   { 8, 4, 0 }   },

  { PID_CLARETT_PLUS_8PRE,   MODE_NONE,          { 0, 0, 0 },   { 2, 2, 2 },   { 8, 4, 0 },   { 8, 4, 0 }   },
  { PID_CLARETT_PLUS_8PRE,   MODE_OPTICAL,       { 2, 2, 2 },   { 2, 2, 2 },   { 0, 0, 0 },   { 8, 4, 0 }   },
  { PID_CLARETT_PLUS_8PRE,   MODE_RCA,           { 2, 2, 2 },   { 2, 2, 2 },   { 8, 4, 0 },   { 8, 4, 0 }   },

  // Gen 2 18i8 has RCA S/PDIF and ADAT in, no mode selector
  { PID_SCARLETT_GEN2_18I8,  NULL,               { 2, 2, 2 },   { 2, 2, 2 },   { 8, 4, 0 },   { 0, 0, 0 }   },

  // Gen 2 18i20 has RCA S/PDIF and ADAT in/out, no mode selector
  { PID_SCARLETT_GEN2_18I20, NULL,               { 2, 2, 2 },   { 2, 2, 2 },   { 8, 4, 0 },   { 8, 4, 0 }   },

  { PID_SCARLETT_GEN3_18I8,  MODE_RCA,           { 2, 2, 2 },   { 2, 2, 2 },   { 8, 4, 0 },   { 0, 0, 0 }   },
  { PID_SCARLETT_GEN3_18I8,  MODE_OPTICAL,       { 2, 2, 2 },   { 2, 2, 2 },   { 0, 0, 0 },   { 0, 0, 0 }   },

  { PID_SCARLETT_GEN3_18I20, MODE_SPDIF_RCA,     { 2, 2, 2 },   { 2, 2, 2 },   { 8, 4, 0 },   { 16, 4, 0 }  },
  { PID_SCARLETT_GEN3_18I20, MODE_SPDIF_OPTICAL, { 2, 2, 0 },   { 4, 4, 2 },   { 8, 4, 0 },   { 8, 4, 0 }   },
  { PID_SCARLETT_GEN3_18I20, MODE_DUAL_ADAT,     { 0, 0, 0 },   { 2, 2, 2 },   { 8, 8, 0 },   { 16, 8, 0 }  },

  { PID_SCARLETT_GEN4_16I16, MODE_ADAT,          { 2, 2, 2 },   { 2, 2, 2 },   { 8, 4, 0 },   { 8, 4, 0 }   },
  { PID_SCARLETT_GEN4_16I16, MODE_OPTICAL_SPDIF, { 4, 4, 0 },   { 4, 4, 2 },   { 0, 0, 0 },   { 0, 0, 0 }   },

  { PID_SCARLETT_GEN4_18I16, MODE_ADAT,          { 2, 2, 2 },   { 2, 2, 2 },   { 8, 4, 0 },   { 8, 4, 0 }   },
  { PID_SCARLETT_GEN4_18I16, MODE_OPTICAL_SPDIF, { 4, 4, 0 },   { 4, 4, 2 },   { 0, 0, 0 },   { 0, 0, 0 }   },

  { PID_SCARLETT_GEN4_18I20, MODE_RCA_SPDIF,     { 2, 2, 0 },   { 2, 2, 2 },   { 8, 4, 0 },   { 8, 4, 0 }   },
  { PID_SCARLETT_GEN4_18I20, MODE_OPTICAL_SPDIF, { 4, 4, 0 },   { 4, 4, 2 },   { 8, 4, 0 },   { 8, 4, 0 }   },
  { PID_SCARLETT_GEN4_18I20, MODE_DUAL_ADAT,     { 0, 0, 0 },   { 2, 2, 2 },   { 16, 8, 0 },  { 16, 8, 0 }  },

  { 0 }
};

void update_hw_io_limits(struct alsa_card *card) {
  const char *mode = NULL;

  if (card->digital_io_mode_elem)
    mode = alsa_get_item_name(card->digital_io_mode_elem, card->digital_io_mode);

  int sr_cat = get_sample_rate_category(card->current_sample_rate);

  // Default: -1 means unknown/all available
  card->max_spdif_in = -1;
  card->max_spdif_out = -1;
  card->max_adat_in = -1;
  card->max_adat_out = -1;

  for (const struct io_limits *l = limits_table; l->pid; l++) {
    if (l->pid != card->pid)
      continue;

    // match NULL mode (no selector) or matching mode string
    if (l->mode == NULL ? mode == NULL : mode && strcmp(l->mode, mode) == 0) {
      card->max_spdif_in = l->spdif_in[sr_cat];
      card->max_spdif_out = l->spdif_out[sr_cat];
      card->max_adat_in = l->adat_in[sr_cat];
      card->max_adat_out = l->adat_out[sr_cat];
      return;
    }
  }

  // No warning for simulated devices (no PID) or devices without a mode
  if (!mode || card->num == SIMULATED_CARD_NUM)
    return;

  fprintf(stderr, "Unknown Digital I/O config: pid=0x%04x mode=%s sr_cat=%d\n",
          card->pid, mode, sr_cat);
}
