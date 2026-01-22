// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stddef.h>

#include "alsa.h"
#include "device-port-names.h"
#include "hardware.h"

// Scarlett 2nd Gen 6i6 analogue inputs (sources)
static const char *scarlett_gen2_6i6_analogue_src[] = {
  "Mic/Line/Inst 1", "Mic/Line/Inst 2",
  "Line 3", "Line 4",
  NULL
};

// Scarlett 2nd Gen 6i6 analogue outputs (sinks)
static const char *scarlett_gen2_6i6_analogue_snk[] = {
  "Line 1/Headphones 1 (L)", "Line 2/Headphones 1 (R)",
  "Line 3/Headphones 2 (L)", "Line 4/Headphones 2 (R)",
  NULL
};

// Scarlett 2nd Gen 18i8 analogue inputs (sources)
static const char *scarlett_gen2_18i8_analogue_src[] = {
  "Mic/Line/Inst 1", "Mic/Line/Inst 2",
  "Mic/Line 3", "Mic/Line 4",
  "Line 5", "Line 6",
  "Line 7", "Line 8",
  NULL
};

// Scarlett 2nd Gen 18i8 analogue outputs (sinks)
static const char *scarlett_gen2_18i8_analogue_snk[] = {
  "Line 1", "Line 2",
  "Headphones 1 (L)", "Headphones 1 (R)",
  "Headphones 2 (L)", "Headphones 2 (R)",
  NULL
};

// Scarlett 2nd Gen 18i20 analogue inputs (sources)
static const char *scarlett_gen2_18i20_analogue_src[] = {
  "Mic/Line/Inst 1", "Mic/Line/Inst 2",
  "Mic/Line 3", "Mic/Line 4",
  "Mic/Line 5", "Mic/Line 6",
  "Mic/Line 7", "Mic/Line 8",
  NULL
};

// Scarlett 2nd Gen 18i20 analogue outputs (sinks)
static const char *scarlett_gen2_18i20_analogue_snk[] = {
  "Line 1 (Main L)", "Line 2 (Main R)",
  "Line 3 (Alt L)", "Line 4 (Alt R)",
  "Line 5", "Line 6",
  "Line 7/Headphones 1 (L)", "Line 8/Headphones 1 (R)",
  "Line 9/Headphones 2 (L)", "Line 10/Headphones 2 (R)",
  NULL
};

// Scarlett 3rd Gen 4i4 analogue inputs (sources)
static const char *scarlett_gen3_4i4_analogue_src[] = {
  "Mic/Line/Inst 1", "Mic/Line/Inst 2",
  "Line 3", "Line 4",
  NULL
};

// Scarlett 3rd Gen 4i4 analogue outputs (sinks)
static const char *scarlett_gen3_4i4_analogue_snk[] = {
  "Line 1", "Line 2",
  "Line 3/Headphones (L)", "Line 4/Headphones (R)",
  NULL
};

// Scarlett 3rd Gen 8i6 analogue inputs (sources)
static const char *scarlett_gen3_8i6_analogue_src[] = {
  "Mic/Line/Inst 1", "Mic/Line/Inst 2",
  "Line 3", "Line 4",
  "Line 5", "Line 6",
  NULL
};

// Scarlett 3rd Gen 8i6 analogue outputs (sinks)
static const char *scarlett_gen3_8i6_analogue_snk[] = {
  "Line 1/Headphones 1 (L)", "Line 2/Headphones 1 (R)",
  "Line 3/Headphones 2 (L)", "Line 4/Headphones 2 (R)",
  NULL
};

// Scarlett 3rd Gen 18i8 analogue inputs (sources)
static const char *scarlett_gen3_18i8_analogue_src[] = {
  "Mic/Line/Inst 1", "Mic/Line/Inst 2",
  "Mic/Line 3", "Mic/Line 4",
  "Line 5", "Line 6",
  "Line 7", "Line 8",
  NULL
};

// Scarlett 3rd Gen 18i8 analogue outputs (sinks)
static const char *scarlett_gen3_18i8_analogue_snk[] = {
  "Line 1", "Line 2",
  "Line 3", "Line 4",
  "Headphones 1 (L)", "Headphones 1 (R)",
  "Headphones 2 (L)", "Headphones 2 (R)",
  NULL
};

// Scarlett 3rd Gen 18i20 analogue inputs (sources)
static const char *scarlett_gen3_18i20_analogue_src[] = {
  "Mic/Line/Inst 1", "Mic/Line/Inst 2",
  "Mic/Line 3", "Mic/Line 4",
  "Mic/Line 5", "Mic/Line 6",
  "Mic/Line 7", "Mic/Line 8",
  "Talkback Mic",
  NULL
};

// Clarett 2Pre analogue inputs (sources)
static const char *clarett_2pre_analogue_src[] = {
  "Mic/Line/Inst 1", "Mic/Line/Inst 2",
  NULL
};

// Clarett 2Pre analogue outputs (sinks)
static const char *clarett_2pre_analogue_snk[] = {
  "Line 1", "Line 2",
  "Line 3/Headphones (L)", "Line 4/Headphones (R)",
  NULL
};

// Clarett 8Pre analogue inputs (sources)
static const char *clarett_8pre_analogue_src[] = {
  "Mic/Line/Inst 1", "Mic/Line/Inst 2",
  "Mic/Line 3", "Mic/Line 4",
  "Mic/Line 5", "Mic/Line 6",
  "Mic/Line 7", "Mic/Line 8",
  NULL
};

// Clarett 8Pre analogue outputs (sinks)
static const char *clarett_8pre_analogue_snk[] = {
  "Line 1", "Line 2",
  "Line 3", "Line 4",
  "Line 5", "Line 6",
  "Line 7/Headphones 1 (L)", "Line 8/Headphones 1 (R)",
  "Line 9/Headphones 2 (L)", "Line 10/Headphones 2 (R)",
  NULL
};

// Clarett 4Pre analogue inputs (sources)
static const char *clarett_4pre_analogue_src[] = {
  "Mic/Line/Inst 1", "Mic/Line/Inst 2",
  "Mic/Line 3", "Mic/Line 4",
  "Line 5", "Line 6",
  "Line 7", "Line 8",
  NULL
};

// Clarett 4Pre analogue outputs (sinks)
static const char *clarett_4pre_analogue_snk[] = {
  "Line 1", "Line 2",
  "Line 3/Headphones 1 (L)", "Line 4/Headphones 1 (R)",
  "Headphones 2 (L)", "Headphones 2 (R)",
  NULL
};

// Vocaster One analogue inputs (sources)
static const char *vocaster_one_analogue_src[] = {
  "Host", "Aux",
  NULL
};

// Vocaster One analogue outputs (sinks)
static const char *vocaster_one_analogue_snk[] = {
  "Spkr/Headphones (L)", "Spkr/Headphones (R)",
  "Aux (L)", "Aux (R)",
  NULL
};

// Vocaster One DSP outputs (sources)
static const char *vocaster_one_dsp_src[] = {
  "Host",
  NULL
};

// Vocaster One mixer outputs (sources)
static const char *vocaster_one_mix_src[] = {
  "Show Mix Pre (L)", "Show Mix Pre (R)",
  "Aux (L)", "Aux (R)",
  "Video Call (L)", "Video Call (R)",
  "Show Mix Post (L)", "Show Mix Post (R)",
  NULL
};

// Vocaster One PCM outputs (sources - from computer)
static const char *vocaster_one_pcm_src[] = {
  "Video Call (L)", "Video Call (R)",
  "Playback (L)", "Playback (R)",
  NULL
};

// Vocaster One PCM inputs (sinks - to computer)
static const char *vocaster_one_pcm_snk[] = {
  "Video Call (L)", "Video Call (R)",
  "Show Mix (L)", "Show Mix (R)",
  "Host Microphone", "Aux",
  "Loopback 1 (L)", "Loopback 1 (R)",
  "Loopback 2 (L)", "Loopback 2 (R)",
  NULL
};

// Vocaster Two analogue inputs (sources)
static const char *vocaster_two_analogue_src[] = {
  "Host", "Guest", "Aux (L)", "Aux (R)", "Bluetooth (L)", "Bluetooth (R)",
  NULL
};

// Vocaster Two analogue outputs (sinks)
static const char *vocaster_two_analogue_snk[] = {
  "Spkr/Headphones (L)", "Spkr/Headphones (R)",
  "Aux (L)", "Aux (R)",
  "Bluetooth (L)", "Bluetooth (R)",
  NULL
};

// Vocaster Two PCM outputs (sources - from computer)
static const char *vocaster_two_pcm_src[] = {
  "Video Call (L)", "Video Call (R)",
  "Playback (L)", "Playback (R)",
  NULL
};

// Vocaster Two DSP outputs (sources)
static const char *vocaster_two_dsp_src[] = {
  "Host", "Guest",
  NULL
};

// Vocaster Two mixer outputs (sources)
static const char *vocaster_two_mix_src[] = {
  "Show Mix Pre (L)", "Show Mix Pre (R)",
  "Aux (L)", "Aux (R)",
  "Bluetooth (L)", "Bluetooth (R)",
  "Video Call (L)", "Video Call (R)",
  "Show Mix Post (L)", "Show Mix Post (R)",
  NULL
};

// Vocaster Two PCM inputs (sinks - to computer)
static const char *vocaster_two_pcm_snk[] = {
  "Video Call (L)", "Video Call (R)",
  "Show Mix (L)", "Show Mix (R)",
  "Host Microphone", "Guest Microphone",
  "Aux (L)", "Aux (R)",
  "Bluetooth (L)", "Bluetooth (R)",
  "Loopback 1 (L)", "Loopback 1 (R)",
  "Loopback 2 (L)", "Loopback 2 (R)",
  NULL
};

// Scarlett 4th Gen Solo analogue inputs (sources)
static const char *scarlett_gen4_solo_analogue_src[] = {
  "Line/Inst", "Mic",
  NULL
};

// Scarlett 4th Gen Solo analogue outputs (sinks)
static const char *scarlett_gen4_solo_analogue_snk[] = {
  "Line 1/Headphones (L)", "Line 2/Headphones (R)",
  NULL
};

// Scarlett 4th Gen 2i2 analogue inputs (sources)
static const char *scarlett_gen4_2i2_analogue_src[] = {
  "Mic/Line/Inst 1", "Mic/Line/Inst 2",
  NULL
};

// Scarlett 4th Gen 2i2 analogue outputs (sinks)
static const char *scarlett_gen4_2i2_analogue_snk[] = {
  "Line 1/Headphones (L)", "Line 2/Headphones (R)",
  NULL
};

// Scarlett 4th Gen 4i4 analogue inputs (sources)
static const char *scarlett_gen4_4i4_analogue_src[] = {
  "Mic/Line/Inst 1", "Mic/Line/Inst 2",
  "Line 3", "Line 4",
  NULL
};

// Scarlett 4th Gen 4i4 analogue outputs (sinks)
static const char *scarlett_gen4_4i4_analogue_snk[] = {
  "Line 1", "Line 2",
  "Line 3", "Line 4",
  "Headphones (L)", "Headphones (R)",
  NULL
};

// Scarlett 4th Gen 16i16 analogue inputs (sources)
static const char *scarlett_gen4_16i16_analogue_src[] = {
  "Mic/Line/Inst 1", "Mic/Line/Inst 2",
  "Line 3", "Line 4",
  "Line 5", "Line 6",
  NULL
};

// Scarlett 4th Gen 16i16 analogue outputs (sinks)
static const char *scarlett_gen4_16i16_analogue_snk[] = {
  "Line 1", "Line 2",
  "Line 3", "Line 4",
  "Headphones 1 (L)", "Headphones 1 (R)",
  "Headphones 2 (L)", "Headphones 2 (R)",
  NULL
};

// Scarlett 4th Gen 18i16 analogue inputs (sources)
static const char *scarlett_gen4_18i16_analogue_src[] = {
  "Mic/Line/Inst 1", "Mic/Line/Inst 2",
  "Mic/Line 3", "Mic/Line 4",
  "Line 5", "Line 6",
  "Line 7", "Line 8",
  NULL
};

// Scarlett 4th Gen 18i16 analogue outputs (sinks)
static const char *scarlett_gen4_18i16_analogue_snk[] = {
  "Line 1", "Line 2",
  "Line 3", "Line 4",
  "Headphones 1 (L)", "Headphones 1 (R)",
  "Headphones 2 (L)", "Headphones 2 (R)",
  NULL
};

// Scarlett 4th Gen 18i20 analogue inputs (sources)
static const char *scarlett_gen4_18i20_analogue_src[] = {
  "Mic/Line/Inst 1", "Mic/Line/Inst 2",
  "Mic/Line 3", "Mic/Line 4",
  "Mic/Line 5", "Mic/Line 6",
  "Mic/Line 7", "Mic/Line 8",
  "Talkback Mic",
  NULL
};

// Scarlett 4th Gen 18i20 analogue outputs (sinks)
static const char *scarlett_gen4_18i20_analogue_snk[] = {
  "Line 1", "Line 2",
  "Line 3", "Line 4",
  "Line 5", "Line 6",
  "Line 7", "Line 8",
  "Line 9", "Line 10",
  "Headphones 1 (L)", "Headphones 1 (R)",
  "Headphones 2 (L)", "Headphones 2 (R)",
  NULL
};

struct device_port_names {
  int          pid;
  int          port_category;
  int          hw_type;
  int          is_snk;
  const char **names;
};

// Stereo pair names (indexed by pair number: 0 = ports 1-2, 1 = ports 3-4, etc.)

// Scarlett 2nd Gen 6i6 analogue output pairs
static const char *scarlett_gen2_6i6_analogue_snk_pairs[] = {
  "Line 1–2/Headphones 1", "Line 3–4/Headphones 2", NULL
};

// Scarlett 2nd Gen 18i8 analogue output pairs
static const char *scarlett_gen2_18i8_analogue_snk_pairs[] = {
  "Line 1–2", "Headphones 1", "Headphones 2", NULL
};

// Scarlett 2nd Gen 18i20 analogue output pairs
static const char *scarlett_gen2_18i20_analogue_snk_pairs[] = {
  "Main", "Alt", "Line 5–6", "Line 7–8/Headphones 1", "Line 9–10/Headphones 2",
  NULL
};

// Scarlett 3rd Gen 4i4 analogue output pairs
static const char *scarlett_gen3_4i4_analogue_snk_pairs[] = {
  "Line 1–2", "Line 3–4/Headphones", NULL
};

// Scarlett 3rd Gen 8i6 analogue output pairs
static const char *scarlett_gen3_8i6_analogue_snk_pairs[] = {
  "Line 1–2/Headphones 1", "Line 3–4/Headphones 2", NULL
};

// Scarlett 3rd Gen 18i8 analogue output pairs
static const char *scarlett_gen3_18i8_analogue_snk_pairs[] = {
  "Line 1–2", "Line 3–4", "Headphones 1", "Headphones 2", NULL
};

// Clarett 2Pre analogue output pairs
static const char *clarett_2pre_analogue_snk_pairs[] = {
  "Line 1–2", "Line 3–4/Headphones", NULL
};

// Clarett 4Pre analogue output pairs
static const char *clarett_4pre_analogue_snk_pairs[] = {
  "Line 1–2", "Line 3–4/Headphones 1", "Headphones 2", NULL
};

// Clarett 8Pre analogue output pairs
static const char *clarett_8pre_analogue_snk_pairs[] = {
  "Line 1–2", "Line 3–4", "Line 5–6", "Line 7–8/Headphones 1",
  "Line 9–10/Headphones 2", NULL
};

// Vocaster One analogue output pairs
static const char *vocaster_one_analogue_snk_pairs[] = {
  "Spkr/Headphones", "Aux", NULL
};

// Vocaster One PCM source pairs (from computer)
static const char *vocaster_one_pcm_src_pairs[] = {
  "Video Call", "Playback", NULL
};

// Vocaster One PCM sink pairs (to computer)
// Note: pair 2 (Host Microphone + Aux) are not a true stereo pair
static const char *vocaster_one_pcm_snk_pairs[] = {
  "Video Call", "Show Mix", "Host/Aux", "Loopback 1", "Loopback 2", NULL
};

// Vocaster One mixer output pairs
static const char *vocaster_one_mix_src_pairs[] = {
  "Show Mix Pre", "Aux", "Video Call", "Show Mix Post", NULL
};

// Vocaster Two analogue input pairs
static const char *vocaster_two_analogue_src_pairs[] = {
  "Mic 1-2", "Aux", "Bluetooth", NULL
};

// Vocaster Two analogue output pairs
static const char *vocaster_two_analogue_snk_pairs[] = {
  "Spkr/Headphones", "Aux", "Bluetooth", NULL
};

// Vocaster Two PCM source pairs (from computer)
static const char *vocaster_two_pcm_src_pairs[] = {
  "Video Call", "Playback", NULL
};

// Vocaster Two PCM sink pairs (to computer)
// Note: pair 2 (Host Microphone + Guest Microphone) are not a true stereo pair
static const char *vocaster_two_pcm_snk_pairs[] = {
  "Video Call", "Show Mix", "Mic 1-2", "Aux", "Bluetooth",
  "Loopback 1", "Loopback 2", NULL
};

// Vocaster Two DSP output pairs
static const char *vocaster_two_dsp_src_pairs[] = {
  "Mic 1-2", NULL
};

// Vocaster Two mixer output pairs
static const char *vocaster_two_mix_src_pairs[] = {
  "Show Mix Pre", "Aux", "Bluetooth", "Video Call", "Show Mix Post", NULL
};

// Scarlett 4th Gen Solo analogue output pairs
static const char *scarlett_gen4_solo_analogue_snk_pairs[] = {
  "Line 1–2/Headphones", NULL
};

// Scarlett 4th Gen 2i2 analogue output pairs
static const char *scarlett_gen4_2i2_analogue_snk_pairs[] = {
  "Line 1–2/Headphones", NULL
};

// Scarlett 4th Gen 4i4 analogue output pairs
static const char *scarlett_gen4_4i4_analogue_snk_pairs[] = {
  "Line 1–2", "Line 3–4", "Headphones", NULL
};

// Scarlett 4th Gen 16i16 analogue output pairs
static const char *scarlett_gen4_16i16_analogue_snk_pairs[] = {
  "Line 1–2", "Line 3–4", "Headphones 1", "Headphones 2", NULL
};

// Scarlett 4th Gen 18i16 analogue output pairs
static const char *scarlett_gen4_18i16_analogue_snk_pairs[] = {
  "Line 1–2", "Line 3–4", "Headphones 1", "Headphones 2", NULL
};

// Scarlett 4th Gen 18i20 analogue output pairs
static const char *scarlett_gen4_18i20_analogue_snk_pairs[] = {
  "Line 1–2", "Line 3–4", "Line 5–6", "Line 7–8", "Line 9–10",
  "Headphones 1", "Headphones 2", NULL
};

// Analogue input (source) pair names

// Mic/Line/Inst 1–2 only
static const char *analogue_src_pairs_inst_12[] = {
  "Mic/Line/Inst 1–2", NULL
};

// Mic/Line/Inst 1–2, Line 3–4
static const char *analogue_src_pairs_inst_12_line_34[] = {
  "Mic/Line/Inst 1–2", "Line 3–4", NULL
};

// Mic/Line/Inst 1–2, Line 3–4, Line 5–6
static const char *analogue_src_pairs_inst_12_line_3456[] = {
  "Mic/Line/Inst 1–2", "Line 3–4", "Line 5–6", NULL
};

// Mic/Line/Inst 1–2, Mic/Line 3–4, Line 5–6, Line 7–8
static const char *analogue_src_pairs_inst_12_mic_34_line_5678[] = {
  "Mic/Line/Inst 1–2", "Mic/Line 3–4", "Line 5–6", "Line 7–8",
  NULL
};

// Mic/Line/Inst 1–2, Mic/Line 3–4, Mic/Line 5–6, Mic/Line 7–8
static const char *analogue_src_pairs_inst_12_mic_345678[] = {
  "Mic/Line/Inst 1–2", "Mic/Line 3–4", "Mic/Line 5–6",
  "Mic/Line 7–8", NULL
};

struct device_pair_names {
  int          pid;
  int          port_category;
  int          hw_type;
  int          is_snk;
  const char **names;
};

static const struct device_pair_names device_pair_names[] = {
  // Scarlett 2nd Gen 6i6
  { PID_SCARLETT_GEN2_6I6, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12_line_34 },
  { PID_SCARLETT_GEN2_6I6, PC_HW, HW_TYPE_ANALOGUE, 1, scarlett_gen2_6i6_analogue_snk_pairs },

  // Scarlett 2nd Gen 18i8
  { PID_SCARLETT_GEN2_18I8, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12_mic_34_line_5678 },
  { PID_SCARLETT_GEN2_18I8, PC_HW, HW_TYPE_ANALOGUE, 1, scarlett_gen2_18i8_analogue_snk_pairs },

  // Scarlett 2nd Gen 18i20
  { PID_SCARLETT_GEN2_18I20, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12_mic_345678 },
  { PID_SCARLETT_GEN2_18I20, PC_HW, HW_TYPE_ANALOGUE, 1, scarlett_gen2_18i20_analogue_snk_pairs },

  // Clarett USB 2Pre
  { PID_CLARETT_USB_2PRE, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12 },
  { PID_CLARETT_USB_2PRE, PC_HW, HW_TYPE_ANALOGUE, 1, clarett_2pre_analogue_snk_pairs },

  // Clarett USB 4Pre
  { PID_CLARETT_USB_4PRE, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12_mic_34_line_5678 },
  { PID_CLARETT_USB_4PRE, PC_HW, HW_TYPE_ANALOGUE, 1, clarett_4pre_analogue_snk_pairs },

  // Clarett USB 8Pre
  { PID_CLARETT_USB_8PRE, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12_mic_345678 },
  { PID_CLARETT_USB_8PRE, PC_HW, HW_TYPE_ANALOGUE, 1, clarett_8pre_analogue_snk_pairs },

  // Clarett+ 2Pre
  { PID_CLARETT_PLUS_2PRE, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12 },
  { PID_CLARETT_PLUS_2PRE, PC_HW, HW_TYPE_ANALOGUE, 1, clarett_2pre_analogue_snk_pairs },

  // Clarett+ 4Pre
  { PID_CLARETT_PLUS_4PRE, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12_mic_34_line_5678 },
  { PID_CLARETT_PLUS_4PRE, PC_HW, HW_TYPE_ANALOGUE, 1, clarett_4pre_analogue_snk_pairs },

  // Clarett+ 8Pre
  { PID_CLARETT_PLUS_8PRE, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12_mic_345678 },
  { PID_CLARETT_PLUS_8PRE, PC_HW, HW_TYPE_ANALOGUE, 1, clarett_8pre_analogue_snk_pairs },

  // Scarlett 3rd Gen 4i4
  { PID_SCARLETT_GEN3_4I4, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12_line_34 },
  { PID_SCARLETT_GEN3_4I4, PC_HW, HW_TYPE_ANALOGUE, 1, scarlett_gen3_4i4_analogue_snk_pairs },

  // Scarlett 3rd Gen 8i6
  { PID_SCARLETT_GEN3_8I6, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12_line_3456 },
  { PID_SCARLETT_GEN3_8I6, PC_HW, HW_TYPE_ANALOGUE, 1, scarlett_gen3_8i6_analogue_snk_pairs },

  // Scarlett 3rd Gen 18i8
  { PID_SCARLETT_GEN3_18I8, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12_mic_34_line_5678 },
  { PID_SCARLETT_GEN3_18I8, PC_HW, HW_TYPE_ANALOGUE, 1, scarlett_gen3_18i8_analogue_snk_pairs },

  // Scarlett 3rd Gen 18i20
  { PID_SCARLETT_GEN3_18I20, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12_mic_345678 },
  { PID_SCARLETT_GEN3_18I20, PC_HW, HW_TYPE_ANALOGUE, 1, scarlett_gen2_18i20_analogue_snk_pairs },

  // Vocaster One
  { PID_VOCASTER_ONE, PC_HW,  HW_TYPE_ANALOGUE, 1, vocaster_one_analogue_snk_pairs },
  { PID_VOCASTER_ONE, PC_PCM, 0,                0, vocaster_one_pcm_src_pairs },
  { PID_VOCASTER_ONE, PC_PCM, 0,                1, vocaster_one_pcm_snk_pairs },
  { PID_VOCASTER_ONE, PC_MIX, 0,                0, vocaster_one_mix_src_pairs },

  // Vocaster Two
  { PID_VOCASTER_TWO, PC_HW,  HW_TYPE_ANALOGUE, 0, vocaster_two_analogue_src_pairs },
  { PID_VOCASTER_TWO, PC_HW,  HW_TYPE_ANALOGUE, 1, vocaster_two_analogue_snk_pairs },
  { PID_VOCASTER_TWO, PC_PCM, 0,                0, vocaster_two_pcm_src_pairs },
  { PID_VOCASTER_TWO, PC_PCM, 0,                1, vocaster_two_pcm_snk_pairs },
  { PID_VOCASTER_TWO, PC_DSP, 0,                0, vocaster_two_dsp_src_pairs },
  { PID_VOCASTER_TWO, PC_MIX, 0,                0, vocaster_two_mix_src_pairs },

  // Scarlett 4th Gen Solo
  { PID_SCARLETT_GEN4_SOLO, PC_HW, HW_TYPE_ANALOGUE, 1, scarlett_gen4_solo_analogue_snk_pairs },

  // Scarlett 4th Gen 2i2
  { PID_SCARLETT_GEN4_2I2, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12 },
  { PID_SCARLETT_GEN4_2I2, PC_HW, HW_TYPE_ANALOGUE, 1, scarlett_gen4_2i2_analogue_snk_pairs },

  // Scarlett 4th Gen 4i4
  { PID_SCARLETT_GEN4_4I4, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12_line_34 },
  { PID_SCARLETT_GEN4_4I4, PC_HW, HW_TYPE_ANALOGUE, 1, scarlett_gen4_4i4_analogue_snk_pairs },

  // Scarlett 4th Gen 16i16
  { PID_SCARLETT_GEN4_16I16, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12_line_3456 },
  { PID_SCARLETT_GEN4_16I16, PC_HW, HW_TYPE_ANALOGUE, 1, scarlett_gen4_16i16_analogue_snk_pairs },

  // Scarlett 4th Gen 18i16
  { PID_SCARLETT_GEN4_18I16, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12_mic_34_line_5678 },
  { PID_SCARLETT_GEN4_18I16, PC_HW, HW_TYPE_ANALOGUE, 1, scarlett_gen4_18i16_analogue_snk_pairs },

  // Scarlett 4th Gen 18i20
  { PID_SCARLETT_GEN4_18I20, PC_HW, HW_TYPE_ANALOGUE, 0, analogue_src_pairs_inst_12_mic_345678 },
  { PID_SCARLETT_GEN4_18I20, PC_HW, HW_TYPE_ANALOGUE, 1, scarlett_gen4_18i20_analogue_snk_pairs },

  { 0 }
};

static const struct device_port_names device_port_names[] = {
  // Scarlett 2nd Gen 18i20
  { PID_SCARLETT_GEN2_18I20, PC_HW,  HW_TYPE_ANALOGUE, 0, scarlett_gen2_18i20_analogue_src },
  { PID_SCARLETT_GEN2_18I20, PC_HW,  HW_TYPE_ANALOGUE, 1, scarlett_gen2_18i20_analogue_snk },

  // Scarlett 2nd Gen 6i6
  { PID_SCARLETT_GEN2_6I6, PC_HW,  HW_TYPE_ANALOGUE, 0, scarlett_gen2_6i6_analogue_src },
  { PID_SCARLETT_GEN2_6I6, PC_HW,  HW_TYPE_ANALOGUE, 1, scarlett_gen2_6i6_analogue_snk },

  // Scarlett 2nd Gen 18i8
  { PID_SCARLETT_GEN2_18I8, PC_HW,  HW_TYPE_ANALOGUE, 0, scarlett_gen2_18i8_analogue_src },
  { PID_SCARLETT_GEN2_18I8, PC_HW,  HW_TYPE_ANALOGUE, 1, scarlett_gen2_18i8_analogue_snk },

  // Clarett USB 2Pre
  { PID_CLARETT_USB_2PRE, PC_HW,  HW_TYPE_ANALOGUE, 0, clarett_2pre_analogue_src },
  { PID_CLARETT_USB_2PRE, PC_HW,  HW_TYPE_ANALOGUE, 1, clarett_2pre_analogue_snk },

  // Clarett USB 4Pre
  { PID_CLARETT_USB_4PRE, PC_HW,  HW_TYPE_ANALOGUE, 0, clarett_4pre_analogue_src },
  { PID_CLARETT_USB_4PRE, PC_HW,  HW_TYPE_ANALOGUE, 1, clarett_4pre_analogue_snk },

  // Clarett USB 8Pre
  { PID_CLARETT_USB_8PRE, PC_HW,  HW_TYPE_ANALOGUE, 0, clarett_8pre_analogue_src },
  { PID_CLARETT_USB_8PRE, PC_HW,  HW_TYPE_ANALOGUE, 1, clarett_8pre_analogue_snk },

  // Clarett+ 2Pre (same as Clarett USB 2Pre)
  { PID_CLARETT_PLUS_2PRE, PC_HW,  HW_TYPE_ANALOGUE, 0, clarett_2pre_analogue_src },
  { PID_CLARETT_PLUS_2PRE, PC_HW,  HW_TYPE_ANALOGUE, 1, clarett_2pre_analogue_snk },

  // Clarett+ 4Pre (same as Clarett USB 4Pre)
  { PID_CLARETT_PLUS_4PRE, PC_HW,  HW_TYPE_ANALOGUE, 0, clarett_4pre_analogue_src },
  { PID_CLARETT_PLUS_4PRE, PC_HW,  HW_TYPE_ANALOGUE, 1, clarett_4pre_analogue_snk },

  // Clarett+ 8Pre (same as Clarett USB 8Pre)
  { PID_CLARETT_PLUS_8PRE, PC_HW,  HW_TYPE_ANALOGUE, 0, clarett_8pre_analogue_src },
  { PID_CLARETT_PLUS_8PRE, PC_HW,  HW_TYPE_ANALOGUE, 1, clarett_8pre_analogue_snk },

  // Scarlett 3rd Gen 4i4
  { PID_SCARLETT_GEN3_4I4, PC_HW,  HW_TYPE_ANALOGUE, 0, scarlett_gen3_4i4_analogue_src },
  { PID_SCARLETT_GEN3_4I4, PC_HW,  HW_TYPE_ANALOGUE, 1, scarlett_gen3_4i4_analogue_snk },

  // Scarlett 3rd Gen 8i6
  { PID_SCARLETT_GEN3_8I6, PC_HW,  HW_TYPE_ANALOGUE, 0, scarlett_gen3_8i6_analogue_src },
  { PID_SCARLETT_GEN3_8I6, PC_HW,  HW_TYPE_ANALOGUE, 1, scarlett_gen3_8i6_analogue_snk },

  // Scarlett 3rd Gen 18i8
  { PID_SCARLETT_GEN3_18I8, PC_HW,  HW_TYPE_ANALOGUE, 0, scarlett_gen3_18i8_analogue_src },
  { PID_SCARLETT_GEN3_18I8, PC_HW,  HW_TYPE_ANALOGUE, 1, scarlett_gen3_18i8_analogue_snk },

  // Scarlett 3rd Gen 18i20
  { PID_SCARLETT_GEN3_18I20, PC_HW,  HW_TYPE_ANALOGUE, 0, scarlett_gen3_18i20_analogue_src },
  { PID_SCARLETT_GEN3_18I20, PC_HW,  HW_TYPE_ANALOGUE, 1, scarlett_gen2_18i20_analogue_snk },

  // Vocaster One
  { PID_VOCASTER_ONE, PC_HW,  HW_TYPE_ANALOGUE, 0, vocaster_one_analogue_src },
  { PID_VOCASTER_ONE, PC_HW,  HW_TYPE_ANALOGUE, 1, vocaster_one_analogue_snk },
  { PID_VOCASTER_ONE, PC_DSP, 0,                0, vocaster_one_dsp_src },
  { PID_VOCASTER_ONE, PC_MIX, 0,                0, vocaster_one_mix_src },
  { PID_VOCASTER_ONE, PC_PCM, 0,                0, vocaster_one_pcm_src },
  { PID_VOCASTER_ONE, PC_PCM, 0,                1, vocaster_one_pcm_snk },

  // Vocaster Two
  { PID_VOCASTER_TWO, PC_HW,  HW_TYPE_ANALOGUE, 0, vocaster_two_analogue_src },
  { PID_VOCASTER_TWO, PC_HW,  HW_TYPE_ANALOGUE, 1, vocaster_two_analogue_snk },
  { PID_VOCASTER_TWO, PC_DSP, 0,                0, vocaster_two_dsp_src },
  { PID_VOCASTER_TWO, PC_MIX, 0,                0, vocaster_two_mix_src },
  { PID_VOCASTER_TWO, PC_PCM, 0,                0, vocaster_two_pcm_src },
  { PID_VOCASTER_TWO, PC_PCM, 0,                1, vocaster_two_pcm_snk },

  // Scarlett 4th Gen Solo
  { PID_SCARLETT_GEN4_SOLO, PC_HW,  HW_TYPE_ANALOGUE, 0, scarlett_gen4_solo_analogue_src },
  { PID_SCARLETT_GEN4_SOLO, PC_HW,  HW_TYPE_ANALOGUE, 1, scarlett_gen4_solo_analogue_snk },

  // Scarlett 4th Gen 2i2
  { PID_SCARLETT_GEN4_2I2, PC_HW,  HW_TYPE_ANALOGUE, 0, scarlett_gen4_2i2_analogue_src },
  { PID_SCARLETT_GEN4_2I2, PC_HW,  HW_TYPE_ANALOGUE, 1, scarlett_gen4_2i2_analogue_snk },

  // Scarlett 4th Gen 4i4
  { PID_SCARLETT_GEN4_4I4, PC_HW,  HW_TYPE_ANALOGUE, 0, scarlett_gen4_4i4_analogue_src },
  { PID_SCARLETT_GEN4_4I4, PC_HW,  HW_TYPE_ANALOGUE, 1, scarlett_gen4_4i4_analogue_snk },

  // Scarlett 4th Gen 16i16
  { PID_SCARLETT_GEN4_16I16, PC_HW,  HW_TYPE_ANALOGUE, 0, scarlett_gen4_16i16_analogue_src },
  { PID_SCARLETT_GEN4_16I16, PC_HW,  HW_TYPE_ANALOGUE, 1, scarlett_gen4_16i16_analogue_snk },

  // Scarlett 4th Gen 18i16
  { PID_SCARLETT_GEN4_18I16, PC_HW,  HW_TYPE_ANALOGUE, 0, scarlett_gen4_18i16_analogue_src },
  { PID_SCARLETT_GEN4_18I16, PC_HW,  HW_TYPE_ANALOGUE, 1, scarlett_gen4_18i16_analogue_snk },

  // Scarlett 4th Gen 18i20
  { PID_SCARLETT_GEN4_18I20, PC_HW,  HW_TYPE_ANALOGUE, 0, scarlett_gen4_18i20_analogue_src },
  { PID_SCARLETT_GEN4_18I20, PC_HW,  HW_TYPE_ANALOGUE, 1, scarlett_gen4_18i20_analogue_snk },

  { 0 }
};

const char *get_device_port_name(
  int pid,
  int port_category,
  int hw_type,
  int is_snk,
  int port_num
) {
  for (int i = 0; device_port_names[i].names; i++) {
    const struct device_port_names *entry = &device_port_names[i];

    if (entry->pid != pid)
      continue;
    if (entry->port_category != port_category)
      continue;
    if (port_category == PC_HW && entry->hw_type != hw_type)
      continue;
    if (entry->is_snk != is_snk)
      continue;

    // found matching entry, look up port name by index
    for (int j = 0; entry->names[j]; j++) {
      if (j == port_num)
        return entry->names[j];
    }

    // port_num out of range for this entry
    return NULL;
  }

  return NULL;
}

const char *get_device_pair_name(
  int pid,
  int port_category,
  int hw_type,
  int is_snk,
  int pair_num
) {
  for (int i = 0; device_pair_names[i].names; i++) {
    const struct device_pair_names *entry = &device_pair_names[i];

    if (entry->pid != pid)
      continue;
    if (entry->port_category != port_category)
      continue;
    if (port_category == PC_HW && entry->hw_type != hw_type)
      continue;
    if (entry->is_snk != is_snk)
      continue;

    // found matching entry, look up pair name by index
    for (int j = 0; entry->names[j]; j++) {
      if (j == pair_num)
        return entry->names[j];
    }

    // pair_num out of range for this entry
    return NULL;
  }

  return NULL;
}
