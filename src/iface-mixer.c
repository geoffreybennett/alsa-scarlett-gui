// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkhelper.h"
#include "iface-mixer.h"
#include "stringhelper.h"
#include "tooltips.h"
#include "widget-boolean.h"
#include "widget-drop-down.h"
#include "widget-dual.h"
#include "widget-gain.h"
#include "widget-input-select.h"
#include "widget-label.h"
#include "widget-sample-rate.h"
#include "window-helper.h"
#include "window-levels.h"
#include "window-mixer.h"
#include "window-routing.h"
#include "window-startup.h"

static void add_clock_source_control(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  GArray *elems = card->elems;

  struct alsa_elem *clock_source = get_elem_by_prefix(elems, "Clock Source");

  if (!clock_source) {
    clock_source = get_elem_by_substr(elems, "Sync Clock Source");

    if (!clock_source)
      return;
  }

  GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_tooltip_text(
    b,
    "Clock Source selects where the interface receives its digital "
    "clock from. If you aren’t using S/PDIF or ADAT inputs, set this "
    "to Internal."
  );
  gtk_box_append(GTK_BOX(global_controls), b);

  GtkWidget *l = gtk_label_new("Clock Source");
  GtkWidget *w = make_drop_down_alsa_elem(clock_source, NULL);
  gtk_widget_add_css_class(w, "clock-source");
  gtk_widget_add_css_class(w, "fixed");

  gtk_box_append(GTK_BOX(b), l);
  gtk_box_append(GTK_BOX(b), w);
}

static void add_sync_status_control(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  GArray *elems = card->elems;

  struct alsa_elem *sync_status = get_elem_by_name(elems, "Sync Status");

  if (!sync_status) {
    sync_status = get_elem_by_name(elems, "Sample Clock Sync Status");
    if (!sync_status)
      return;
  }

  GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  if (get_elem_by_prefix(elems, "Clock Source")) {
    gtk_widget_set_tooltip_text(
      b,
      "Sync Status indicates if the interface is locked to a valid "
      "digital clock. If you aren’t using S/PDIF or ADAT inputs and "
      "the Sync Status is Unlocked, change the Clock Source to "
      "Internal."
    );
  } else {
    gtk_widget_set_tooltip_text(
      b,
      "Sync Status indicates if the interface is locked to a valid "
      "digital clock. Since the Clock Source is fixed to internal on "
      "this interface, this should stay locked."
    );
  }
  gtk_box_append(GTK_BOX(global_controls), b);

  GtkWidget *l = gtk_label_new("Sync Status");
  gtk_box_append(GTK_BOX(b), l);
  GtkWidget *w = make_boolean_alsa_elem(sync_status, "Unlocked", "Locked");
  gtk_widget_add_css_class(w, "sync-status");
  gtk_widget_add_css_class(w, "fixed");
  gtk_box_append(GTK_BOX(b), w);
}

static void add_power_status_control(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  GArray *elems = card->elems;

  struct alsa_elem *power_status = get_elem_by_name(
    elems, "Power Status Card Enum"
  );

  if (!power_status)
    return;

  GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_tooltip_text(
    b,
    "Power indicates if the interface is being powered by the USB "
    "bus, an external power supply, or if there is insufficient power "
    "available and the interface has shut down."
  );
  gtk_box_append(GTK_BOX(global_controls), b);

  GtkWidget *l = gtk_label_new("Power");
  gtk_box_append(GTK_BOX(b), l);
  GtkWidget *w = make_drop_down_alsa_elem(power_status, NULL);
  gtk_widget_add_css_class(w, "power-status");
  gtk_widget_add_css_class(w, "fixed");
  gtk_box_append(GTK_BOX(b), w);
}

static void add_sample_rate_control(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_tooltip_text(
    b,
    "The Sample Rate cannot be changed here because it is set by the "
    "application which is using the interface, usually a sound "
    "server like PulseAudio, JACK, or PipeWire. If this shows N/A, "
    "no application is currently using the interface.\n\n"
    "Note that not all features are available on all interfaces at "
    "sample rates above 48kHz. Please refer to the user guide for "
    "your interface for more information."
  );
  gtk_box_append(GTK_BOX(global_controls), b);

  GtkWidget *l = gtk_label_new("Sample Rate");
  gtk_box_append(GTK_BOX(b), l);
  GtkWidget *w = make_sample_rate_widget(card);
  gtk_widget_add_css_class(w, "sample-rate");
  gtk_box_append(GTK_BOX(b), w);
}

static void add_speaker_switching_controls(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  GArray *elems = card->elems;

  struct alsa_elem *speaker_switching = get_elem_by_name(
    elems, "Speaker Switching Playback Enum"
  );

  if (!speaker_switching)
    return;

  GtkWidget *w = make_dual_boolean_alsa_elems(
    speaker_switching,
    "Speaker Switching",
    "Off", "On", "Main", "Alt"
  );

  gtk_widget_set_tooltip_text(
    w,
    "Speaker Switching lets you swap between two pairs of "
    "monitoring speakers very easily."
  );

  gtk_box_append(GTK_BOX(global_controls), w);
}

static void add_talkback_controls(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  GArray *elems = card->elems;

  struct alsa_elem *talkback = get_elem_by_name(
    elems, "Talkback Playback Enum"
  );

  if (!talkback)
    return;

  GtkWidget *w = make_dual_boolean_alsa_elems(
    talkback,
    "Talkback",
    "Disabled", "Enabled", "Off", "On"
  );

  gtk_widget_set_tooltip_text(
    w,
    "Talkback lets you add another channel (usually the talkback "
    "mic) to a mix with a button push, usually to talk to "
    "musicians, and without using an additional mic channel."
  );

  gtk_box_append(GTK_BOX(global_controls), w);
}

static GtkWidget *create_global_box(GtkWidget *grid, int *x, int orient) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_vexpand(box, TRUE);

  GtkWidget *label = gtk_label_new("Global");
  gtk_widget_add_css_class(label, "controls-label");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  GtkWidget *controls = gtk_box_new(orient, 15);
  gtk_widget_add_css_class(controls, "controls-content");
  gtk_widget_set_vexpand(controls, TRUE);

  gtk_box_append(GTK_BOX(box), label);
  gtk_box_append(GTK_BOX(box), controls);

  gtk_grid_attach(GTK_GRID(grid), box, *x, 0, 1, 1);

  (*x)++;

  return controls;
}

/* 4th Gen Solo Mix switch */
static void create_input_select_control(
  GArray    *elems,
  GtkWidget *input_grid,
  int       *current_row
) {
  struct alsa_elem *elem = get_elem_by_name(elems, "PCM Input Capture Switch");

  if (!elem)
    return;

  GtkWidget *w = make_boolean_alsa_elem(elem, "Mix", "Mix");
  gtk_widget_add_css_class(w, "pcm-input-mix");
  gtk_widget_set_tooltip_text(
    w,
    "Enabling Input Mix selects Mix E/F as the input source for "
    "the PCM 1/2 Inputs rather than the DSP 1/2 Inputs. This is "
    "useful to get a mono mix of both input channels."
  );
  gtk_grid_attach(GTK_GRID(input_grid), w, 0, *current_row, 2, 1);

  (*current_row)++;
}

static void create_input_link_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Link", NULL);
  gtk_widget_add_css_class(w, "input-link");
  gtk_widget_set_hexpand(w, TRUE);

  int from, to;
  get_two_num_from_string(elem->name, &from, &to);
  if (to == -1)
    to = from;

  gtk_grid_attach(GTK_GRID(grid), w, from - 1, current_row, to - from + 1, 1);
}

static void create_input_gain_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_gain_alsa_elem(elem, 0, WIDGET_GAIN_TAPER_LINEAR, 1);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_autogain_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Autogain", NULL);
  gtk_widget_add_css_class(w, "autogain");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(
    w,
    "Autogain will listen to the input signal for 10 seconds and "
    "automatically set the gain of the input channel to get the "
    "best signal level."
  );

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_autogain_status_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_label_alsa_elem(elem);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_safe_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Safe", NULL);
  gtk_widget_add_css_class(w, "safe");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(
    w,
    "Enabling Safe Mode prevents the input from clipping by "
    "automatically reducing the gain if the signal is too hot."
  );

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_level_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Inst", NULL);
  gtk_widget_add_css_class(w, "inst");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(w, level_descr);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_air_switch_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Air", NULL);
  gtk_widget_add_css_class(w, "air");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(w, air_descr);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_air_enum_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_drop_down_alsa_elem(elem, "Air");
  gtk_widget_add_css_class(w, "air");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(w, air_descr);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_dsp_switch_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Enhance", NULL);
  gtk_widget_add_css_class(w, "dsp");
  gtk_widget_set_hexpand(w, TRUE);
//  gtk_widget_set_tooltip_text(w, dsp_descr);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_dsp_preset_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_drop_down_alsa_elem(elem, NULL);
  gtk_widget_add_css_class(w, "dsp-preset");
  gtk_widget_set_hexpand(w, TRUE);
//  gtk_widget_set_tooltip_text(w, dsp_descr);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_mute_switch_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Mute", NULL);
  gtk_widget_add_css_class(w, "input-mute");
  gtk_widget_set_hexpand(w, TRUE);
//  gtk_widget_set_tooltip_text(w, dsp_descr);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_pad_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Pad", NULL);
  gtk_widget_add_css_class(w, "pad");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(
    w,
    "Enabling Pad engages a 10dB attenuator in the channel, giving "
    "you more headroom for very hot signals."
  );

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_gain_switch_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Gain", NULL);
  gtk_widget_add_css_class(w, "gain-switch");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(
    w,
    "Enabling Gain switches from Low gain input (0dBFS = +16dBu)\n"
    "to High gain input (0dBFS = −10dBV, approx −6dBu)."
  );

  // ignore current_row, always put it in the first row
  gtk_grid_attach(GTK_GRID(grid), w, column_num, 1, 1, 1);
}

static void create_input_phantom_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "48V", NULL);
  gtk_widget_add_css_class(w, "phantom");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(w, phantom_descr);

  int from, to;
  get_two_num_from_string(elem->name, &from, &to);
  if (to == -1)
    to = from;

  gtk_grid_attach(GTK_GRID(grid), w, from - 1, current_row, to - from + 1, 1);
}

static void create_input_controls_by_type(
  GArray *elems,
  GtkWidget *grid,
  int *current_row,
  char *control,
  void (*create_func)(struct alsa_elem *, GtkWidget *, int, int)
) {
  int count = 0;

  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = &g_array_index(elems, struct alsa_elem, i);

    // if no card entry, it's an empty slot
    if (!elem->card)
      continue;

    if (!strstr(elem->name, control))
      continue;

    int column_num = get_num_from_string(elem->name) - 1;
    create_func(elem, grid, *current_row, column_num);

    count++;
  }

  // Don't increment row for 4th Gen Solo Inst control so Air control
  // goes next to it
  if (!strcmp(control, "Level Capture Enum") && count == 1)
    return;

  if (count)
    (*current_row)++;
}

static void create_input_controls(
  struct alsa_card *card,
  GtkWidget        *top,
  int              *x,
  int              input_count
) {
  GArray *elems = card->elems;

  // Only the 18i20 Gen 2 has no input controls
  if (!input_count)
    return;

  struct alsa_elem *input_select_elem =
    get_elem_by_name(elems, "Input Select Capture Enum");

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

  GtkWidget *label_ic = gtk_label_new("Analogue Inputs");
  gtk_widget_add_css_class(label_ic, "controls-label");
  gtk_widget_set_halign(label_ic, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(box), label_ic);

  GtkWidget *input_grid = gtk_grid_new();
  gtk_widget_add_css_class(input_grid, "controls-content");
  gtk_grid_set_spacing(GTK_GRID(input_grid), 10);
  gtk_widget_set_hexpand(input_grid, TRUE);
  gtk_widget_set_halign(input_grid, GTK_ALIGN_FILL);
  gtk_widget_set_vexpand(input_grid, TRUE);
  gtk_box_append(GTK_BOX(box), input_grid);

  gtk_grid_attach(GTK_GRID(top), box, *x, 0, 1, 1);

  for (int i = 1; i <= input_count; i++) {
    GtkWidget *label;

    if (input_select_elem) {
      label = make_input_select_alsa_elem(input_select_elem, i);
    } else {
      char s[20];
      snprintf(s, 20, "%d", i);
      label = gtk_label_new(s);
    }
    gtk_grid_attach(GTK_GRID(input_grid), label, i - 1, 0, 1, 1);
  }

  int current_row = 1;

  // 4th Gen Solo, put the Phantom Power control above the Air control
  if (get_elem_by_name(elems, "Direct Monitor Playback Switch")) {
    create_input_controls_by_type(
      elems, input_grid, &current_row,
      "Level Capture Enum", create_input_level_control
    );
    create_input_controls_by_type(
      elems, input_grid, &current_row,
      "Phantom Power Capture Switch", create_input_phantom_control
    );
    create_input_controls_by_type(
      elems, input_grid, &current_row,
      "Air Capture Enum", create_input_air_enum_control
    );

    (*x)++;
    return;
  }

  create_input_select_control(elems, input_grid, &current_row);

  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Link Capture Switch", create_input_link_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Gain Capture Volume", create_input_gain_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Autogain Capture Switch", create_input_autogain_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Autogain Status Capture Enum", create_input_autogain_status_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Safe Capture Switch", create_input_safe_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Level Capture Enum", create_input_level_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Impedance Switch", create_input_level_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Air Capture Switch", create_input_air_switch_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Air Capture Enum", create_input_air_enum_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "DSP Capture Switch", create_input_dsp_switch_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "DSP Preset Capture Enum", create_input_dsp_preset_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Mute Capture Switch", create_input_mute_switch_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Pad Capture Switch", create_input_pad_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Pad Switch", create_input_pad_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Gain Switch", create_input_gain_switch_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Phantom Power Capture Switch", create_input_phantom_control
  );

  (*x)++;
}

static void create_output_controls(
  struct alsa_card *card,
  GtkWidget        *top,
  int              *x,
  int              y,
  int              x_span,
  int              output_count
) {
  GArray *elems = card->elems;

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

  GtkWidget *label_oc = gtk_label_new("Analogue Outputs");
  gtk_widget_add_css_class(label_oc, "controls-label");
  gtk_widget_set_halign(label_oc, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(box), label_oc);

  GtkWidget *output_grid = gtk_grid_new();
  gtk_widget_add_css_class(output_grid, "controls-content");
  gtk_grid_set_spacing(GTK_GRID(output_grid), 10);
  gtk_widget_set_hexpand(output_grid, TRUE);
  gtk_widget_set_vexpand(output_grid, TRUE);
  gtk_box_append(GTK_BOX(box), output_grid);

  gtk_grid_attach(GTK_GRID(top), box, *x, y, x_span, 1);

  /* 4th Gen Solo/2i2 */
  if (get_elem_by_prefix(elems, "Direct Monitor Playback")) {
    struct alsa_elem *elem;

    for (int i = 0; i < 2; i++) {
      char s[20];
      snprintf(s, 20, "%d", i + 1);
      GtkWidget *label = gtk_label_new(s);
      gtk_grid_attach(GTK_GRID(output_grid), label, i, 0, 1, 1);
    }

    /* Solo */

    elem = get_elem_by_name(elems, "Direct Monitor Playback Switch");

    if (elem) {
      GtkWidget *w = make_boolean_alsa_elem(elem, "Direct Monitor", NULL);
      gtk_widget_add_css_class(w, "direct-monitor");
      gtk_widget_set_tooltip_text(
        w,
        "Direct Monitor sends the analogue input signals to the "
        "analogue outputs for zero-latency monitoring."
      );
      gtk_grid_attach(GTK_GRID(output_grid), w, 0, 1, 2, 1);
    }

    /* 2i2 */

    elem = get_elem_by_name(elems, "Direct Monitor Playback Enum");

    if (elem) {
      GtkWidget *w = make_drop_down_alsa_elem(elem, "Direct Monitor");
      gtk_widget_add_css_class(w, "direct-monitor");
      gtk_widget_set_tooltip_text(
        w,
        "Direct Monitor sends the analogue input signals to the "
        "analogue outputs for zero-latency monitoring."
      );
      gtk_grid_attach(GTK_GRID(output_grid), w, 0, 1, 2, 1);
    }

    return;
  }

  int has_hw_vol = !!get_elem_by_name(elems, "Master HW Playback Volume") ||
                   !!get_elem_by_name(elems, "Master Playback Volume");
  int line_1_col = has_hw_vol;

  for (int i = 0; i < output_count; i++) {
    char s[20];
    snprintf(s, 20, "%d", i + 1);
    GtkWidget *label = gtk_label_new(s);
    gtk_grid_attach(GTK_GRID(output_grid), label, i + line_1_col, 0, 1, 1);
  }

  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = &g_array_index(elems, struct alsa_elem, i);
    GtkWidget *w;

    // if no card entry, it's an empty slot
    if (!elem->card)
      continue;

    // output controls

    // Gen 1 master output control
    if (strcmp(elem->name, "Master Playback Volume") == 0) {
      GtkWidget *l = gtk_label_new("Master");
      gtk_grid_attach(GTK_GRID(output_grid), l, 0, 0, 1, 1);
      w = make_gain_alsa_elem(elem, 1, WIDGET_GAIN_TAPER_LOG, 0);
      gtk_widget_set_tooltip_text(w, "Master Volume Control");
      gtk_grid_attach(GTK_GRID(output_grid), w, 0, 1, 1, 1);

    } else if (strncmp(elem->name, "Line", 4) == 0 ||
               strncmp(elem->name, "Master", 4) == 0) {

      if (strstr(elem->name, "Playback Volume")) {
        w = make_gain_alsa_elem(elem, 1, WIDGET_GAIN_TAPER_LOG, 1);
        gtk_grid_attach(
          GTK_GRID(output_grid), w, elem->lr_num - 1 + line_1_col, 1, 1, 1
        );
      } else if (strstr(elem->name, "Playback Switch")) {
        w = make_boolean_alsa_elem(
          elem, "*audio-volume-high", "*audio-volume-muted"
        );
        gtk_widget_add_css_class(w, "mute");
        if (has_hw_vol) {
          gtk_widget_set_tooltip_text(
            w,
            "Mute (only available when under software control)"
          );
        } else {
          gtk_widget_set_tooltip_text(w, "Mute");
        }
        gtk_grid_attach(
          GTK_GRID(output_grid), w, elem->lr_num - 1 + line_1_col, 2, 1, 1
        );
      } else if (strstr(elem->name, "Volume Control Playback Enum")) {
        w = make_boolean_alsa_elem(elem, "SW", "HW");
        gtk_widget_add_css_class(w, "sw-hw");
        gtk_widget_set_tooltip_text(
          w,
          "Set software-controlled (SW) or hardware-controlled (HW) "
          "volume for this analogue output."
        );
        gtk_grid_attach(
          GTK_GRID(output_grid), w, elem->lr_num - 1 + line_1_col, 3, 1, 1
        );
      }

    // master output controls
    } else if (strcmp(elem->name, "Master HW Playback Volume") == 0) {
      int gen4 = !!strstr(card->name, "4th Gen");

      GtkWidget *l = gtk_label_new(gen4 ? "Line 1–2" : "HW");
      gtk_grid_attach(GTK_GRID(output_grid), l, 0, 0, 1, 1);
      if (gen4) {
        w = make_gain_alsa_elem(elem, 1, WIDGET_GAIN_TAPER_GEN4_VOLUME, 0);
      } else {
        w = make_gain_alsa_elem(elem, 1, WIDGET_GAIN_TAPER_LOG, 0);
      }
      gtk_widget_set_tooltip_text(
        w,
        gen4
          ? "This control shows the setting of the master volume "
            "knob, which controls the volume of the analogue line "
            "outputs 1 and 2."
          : "This control shows the setting of the physical "
            "(hardware) volume knob, which controls the volume of "
            "the analogue outputs which have been set to “HW”."
      );
      gtk_grid_attach(GTK_GRID(output_grid), w, 0, 1, 1, 1);
    } else if (strcmp(elem->name, "Headphone Playback Volume") == 0) {
      GtkWidget *l = gtk_label_new("Headphones");
      gtk_widget_set_tooltip_text(
        l,
        "This control shows the setting of the headphone volume knob."
      );
      gtk_grid_attach(GTK_GRID(output_grid), l, 1, 0, 1, 1);
      w = make_gain_alsa_elem(elem, 1, WIDGET_GAIN_TAPER_GEN4_VOLUME, 0);
      gtk_grid_attach(GTK_GRID(output_grid), w, 1, 1, 1, 1);
    } else if (strcmp(elem->name, "Mute Playback Switch") == 0) {
      w = make_boolean_alsa_elem(
        elem, "*audio-volume-high", "*audio-volume-muted"
      );
      gtk_widget_add_css_class(w, "mute");
      gtk_widget_set_tooltip_text(w, "Mute HW controlled outputs");
      gtk_grid_attach(GTK_GRID(output_grid), w, 0, 2, 1, 1);
    } else if (strcmp(elem->name, "Dim Playback Switch") == 0) {
      w = make_boolean_alsa_elem(
        elem, "*audio-volume-medium", "*audio-volume-low"
      );
      gtk_widget_add_css_class(w, "dim");
      gtk_widget_set_tooltip_text(
        w, "Dim (lower volume) of HW controlled outputs"
      );
      gtk_grid_attach(GTK_GRID(output_grid), w, 0, 3, 1, 1);
    }
  }

  (*x)++;
}

static void create_global_controls(
  struct alsa_card *card,
  GtkWidget        *top,
  int              *x
) {
  int orient = card->has_speaker_switching
    ? GTK_ORIENTATION_HORIZONTAL
    : GTK_ORIENTATION_VERTICAL;
  GtkWidget *global_controls = create_global_box(top, x, orient);
  GtkWidget *column[3];

  for (int i = 0; i < 3; i++)
    column[i] = global_controls;

  if (card->has_speaker_switching) {
    for (int i = 0; i < 3; i++) {
      column[i] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
      gtk_box_append(GTK_BOX(global_controls), column[i]);
    }
  }

  add_clock_source_control(card, column[0]);
  add_sync_status_control(card, column[1]);
  add_power_status_control(card, column[1]);
  add_sample_rate_control(card, column[2]);
  add_speaker_switching_controls(card, column[0]);
  add_talkback_controls(card, column[1]);
}

static GtkWidget *create_main_window_controls(struct alsa_card *card) {
  int x = 0;

  GtkWidget *top = gtk_grid_new();
  gtk_widget_add_css_class(top, "window-content");
  gtk_widget_add_css_class(top, "iface-mixer");

  if (strstr(card->name, "4th Gen") ||
      strstr(card->name, "Gen 4")) {
    gtk_widget_add_css_class(top, "gen4");
  } else if (strstr(card->name, "Scarlett")) {
    gtk_widget_add_css_class(top, "scarlett");
  } else if (strstr(card->name, "Clarett")) {
    gtk_widget_add_css_class(top, "clarett");
  } else if (strstr(card->name, "Vocaster")) {
    gtk_widget_add_css_class(top, "vocaster");
  }

  gtk_grid_set_spacing(GTK_GRID(top), 15);

  int input_count = get_max_elem_by_name(
    card->elems, "Line", "Capture Switch"
  );
  if (!input_count)
    input_count =
      get_max_elem_by_name(card->elems, "Input", "Switch");

  int output_count = get_max_elem_by_name(
    card->elems, "Line", "Playback Volume"
  );
  if (!output_count)
    output_count =
      get_max_elem_by_name(card->elems, "Master", "Playback Volume") * 2;

  create_global_controls(card, top, &x);
  create_input_controls(card, top, &x, input_count);

  if (input_count + output_count >= 12) {
    x = 0;
    create_output_controls(card, top, &x, 1, 2, output_count);
  } else {
    create_output_controls(card, top, &x, 0, 1, output_count);
  }

  return top;
}

static gboolean window_routing_close_request(GtkWindow *w, gpointer data) {
  struct alsa_card *card = data;

  gtk_widget_activate_action(
    GTK_WIDGET(card->window_main), "win.routing", NULL
  );
  return true;
}

static gboolean window_mixer_close_request(GtkWindow *w, gpointer data) {
  struct alsa_card *card = data;

  gtk_widget_activate_action(
    GTK_WIDGET(card->window_main), "win.mixer", NULL
  );
  return true;
}

static gboolean window_levels_close_request(GtkWindow *w, gpointer data) {
  struct alsa_card *card = data;

  gtk_widget_activate_action(
    GTK_WIDGET(card->window_main), "win.levels", NULL
  );
  return true;
}

// wrap a scrolled window around the controls
static void create_scrollable_window(GtkWidget *window, GtkWidget *controls) {
  GtkWidget *scrolled_window = gtk_scrolled_window_new();

  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scrolled_window),
    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC
  );
  gtk_scrolled_window_set_child(
    GTK_SCROLLED_WINDOW(scrolled_window), controls
  );
  gtk_scrolled_window_set_propagate_natural_height(
    GTK_SCROLLED_WINDOW(scrolled_window), TRUE
  );
  gtk_scrolled_window_set_propagate_natural_width(
    GTK_SCROLLED_WINDOW(scrolled_window), TRUE
  );

  gtk_window_set_child(GTK_WINDOW(window), scrolled_window);
  gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
}

GtkWidget *create_iface_mixer_main(struct alsa_card *card) {
  card->has_speaker_switching =
    !!get_elem_by_name(card->elems, "Speaker Switching Playback Enum");
  card->has_talkback =
    !!get_elem_by_name(card->elems, "Talkback Playback Enum");

  GtkWidget *top = gtk_frame_new(NULL);
  gtk_widget_add_css_class(top, "window-frame");
  GtkWidget *contents = create_main_window_controls(card);
  gtk_frame_set_child(GTK_FRAME(top), contents);

  GtkWidget *routing_top = create_routing_controls(card);
  if (!routing_top)
    return NULL;

  card->window_routing = create_subwindow(
    card, "Routing", G_CALLBACK(window_routing_close_request)
  );

  create_scrollable_window(card->window_routing, routing_top);

  GtkWidget *mixer_top = create_mixer_controls(card);

  card->window_mixer = create_subwindow(
    card, "Mixer", G_CALLBACK(window_mixer_close_request)
  );

  create_scrollable_window(card->window_mixer, mixer_top);

  GtkWidget *levels_top = create_levels_controls(card);

  card->window_levels = create_subwindow(
    card, "Levels", G_CALLBACK(window_levels_close_request)
  );

  gtk_window_set_child(GTK_WINDOW(card->window_levels), levels_top);

  card->window_startup = create_subwindow(
    card, "Startup Configuration", G_CALLBACK(window_startup_close_request)
  );

  GtkWidget *startup = create_startup_controls(card);
  gtk_window_set_child(GTK_WINDOW(card->window_startup), startup);

  return top;
}
