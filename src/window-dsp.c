// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "biquad.h"
#include "compressor-curve.h"
#include "custom-names.h"
#include "dsp-state.h"
#include "gtkhelper.h"
#include "optional-state.h"
#include "peq-response.h"
#include "stringhelper.h"
#include "widget-boolean.h"
#include "widget-filter-type.h"
#include "window-dsp.h"

#define SAMPLE_RATE 48000.0

// Compressor curve update function type
typedef void (*curve_update_fn)(GtkCompressorCurve *curve, int value);

// Data for an integer slider control
struct int_slider {
  struct alsa_elem   *elem;
  GtkWidget          *scale;
  GtkWidget          *label;
  const char         *suffix;
  int                 divisor;  // for ratio display (divide value by this)
  GtkCompressorCurve *curve;    // optional curve to update
  curve_update_fn     curve_fn; // function to update curve
};

// Scaling factors for simulated element values
#define FREQ_SCALE 10    // Hz * 10 (1000.5 Hz = 10005)
#define Q_SCALE 1000     // Q * 1000 (0.707 = 707)
#define GAIN_SCALE 10    // dB * 10 (3.5 dB = 35)

// Data for a filter stage
struct filter_stage {
  struct alsa_elem    *coeff_elem;
  struct biquad_params params;
  gboolean             enabled;
  gboolean             editing;  // true while user is editing an entry
  GtkWidget           *enable_check;
  GtkWidget           *type_dropdown;
  GtkWidget           *freq_box;
  GtkWidget           *freq_entry;
  GtkWidget           *q_box;
  GtkWidget           *q_entry;
  GtkWidget           *gain_box;
  GtkWidget           *gain_entry;
  GtkFilterResponse   *response;
  int                  band_index;

  // Simulated elements for persisting filter state
  struct alsa_elem    *state_enable_elem;
  struct alsa_elem    *state_type_elem;
  struct alsa_elem    *state_freq_elem;
  struct alsa_elem    *state_q_elem;
  struct alsa_elem    *state_gain_elem;

  // Re-entrancy guard: set when updating coefficients from state elements
  gboolean             updating_from_state;
};

// Data for connecting filter stages to a response widget
#define MAX_FILTER_STAGES 8
struct filter_response_stages {
  struct filter_stage *stages[MAX_FILTER_STAGES];
  int                  num_stages;
};

static void int_slider_changed(GtkRange *range, struct int_slider *data) {
  int value = (int)gtk_range_get_value(range);
  alsa_set_elem_value(data->elem, value);
}

static void int_slider_updated(struct alsa_elem *elem, void *private) {
  struct int_slider *data = private;
  int value = alsa_get_elem_value(elem);

  gtk_range_set_value(GTK_RANGE(data->scale), value);

  char *text;
  if (data->divisor > 1) {
    // ratio display: value/divisor:1
    if (value % data->divisor == 0)
      text = g_strdup_printf("%d:1", value / data->divisor);
    else
      text = g_strdup_printf("%.1f:1", (double)value / data->divisor);
  } else {
    text = g_strdup_printf("%d%s", value, data->suffix ? data->suffix : "");
  }
  gtk_label_set_text(GTK_LABEL(data->label), text);
  g_free(text);

  gtk_widget_set_sensitive(data->scale, alsa_get_elem_writable(elem));

  // Update curve if connected
  if (data->curve && data->curve_fn)
    data->curve_fn(data->curve, value);
}

static void int_slider_destroy(struct int_slider *data) {
  g_free(data);
}

// Create an integer slider control with value label
static GtkWidget *make_int_slider_with_curve(
  struct alsa_elem   *elem,
  const char         *suffix,
  int                 divisor,
  GtkCompressorCurve *curve,
  curve_update_fn     curve_fn
) {
  struct int_slider *data = g_malloc0(sizeof(struct int_slider));
  data->elem = elem;
  data->suffix = suffix;
  data->divisor = divisor;
  data->curve = curve;
  data->curve_fn = curve_fn;

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

  // create scale
  data->scale = gtk_scale_new_with_range(
    GTK_ORIENTATION_HORIZONTAL,
    elem->min_val,
    elem->max_val,
    1.0
  );
  gtk_scale_set_draw_value(GTK_SCALE(data->scale), FALSE);
  gtk_widget_set_size_request(data->scale, 150, -1);
  gtk_widget_set_hexpand(data->scale, TRUE);

  // create value label
  data->label = gtk_label_new("");
  gtk_widget_set_size_request(data->label, 60, -1);
  gtk_label_set_xalign(GTK_LABEL(data->label), 1.0);

  gtk_box_append(GTK_BOX(box), data->scale);
  gtk_box_append(GTK_BOX(box), data->label);

  g_signal_connect(
    data->scale, "value-changed", G_CALLBACK(int_slider_changed), data
  );
  alsa_elem_add_callback(elem, int_slider_updated, data, NULL);
  int_slider_updated(elem, data);

  g_object_weak_ref(G_OBJECT(box), (GWeakNotify)int_slider_destroy, data);

  return box;
}

// Simple wrapper without curve
static GtkWidget *make_int_slider(
  struct alsa_elem *elem,
  const char       *suffix,
  int               divisor
) {
  return make_int_slider_with_curve(elem, suffix, divisor, NULL, NULL);
}

// Curve update wrapper functions
static void update_curve_threshold(GtkCompressorCurve *curve, int value) {
  gtk_compressor_curve_set_threshold(curve, value);
}

static void update_curve_ratio(GtkCompressorCurve *curve, int value) {
  gtk_compressor_curve_set_ratio(curve, value);
}

static void update_curve_knee(GtkCompressorCurve *curve, int value) {
  gtk_compressor_curve_set_knee_width(curve, value);
}

static void update_curve_makeup(GtkCompressorCurve *curve, int value) {
  gtk_compressor_curve_set_makeup_gain(curve, value);
}

// Update coefficients and send to ALSA
static void filter_stage_update_coeffs(struct filter_stage *stage) {
  long fixed[5];

  if (stage->enabled) {
    struct biquad_coeffs coeffs;
    biquad_calculate(&stage->params, SAMPLE_RATE, &coeffs);
    biquad_to_fixed_point(&coeffs, fixed);
  } else {
    // Identity filter: b0=1, b1=0, b2=0, a1=0, a2=0
    fixed[0] = BIQUAD_FIXED_POINT_SCALE;  // b0 = 1.0
    fixed[1] = 0;
    fixed[2] = 0;
    fixed[3] = 0;
    fixed[4] = 0;
  }

  alsa_set_elem_int_values(stage->coeff_elem, fixed, 5);

  // Update response graph
  if (stage->response) {
    gtk_filter_response_set_filter(
      stage->response, stage->band_index, &stage->params
    );
    gtk_filter_response_set_band_enabled(
      stage->response, stage->band_index, stage->enabled
    );
  }
}

// Show/hide gain control based on filter type
static void filter_stage_update_gain_visibility(struct filter_stage *stage) {
  gboolean show = biquad_type_uses_gain(stage->params.type);
  gtk_widget_set_visible(stage->gain_box, show);
}

// Show/hide freq/Q controls based on filter type
static void filter_stage_update_freq_q_visibility(struct filter_stage *stage) {
  // Freq is used by all except pure gain
  gboolean show_freq = stage->params.type != BIQUAD_TYPE_GAIN;
  // Q is only used by second-order filters
  gboolean show_q = biquad_type_uses_q(stage->params.type);
  gtk_widget_set_visible(stage->freq_box, show_freq);
  gtk_widget_set_visible(stage->q_box, show_q);
}

// Helper to save a single parameter to state file
static void save_filter_param(struct alsa_elem *elem, long value) {
  if (!elem)
    return;

  // Update simulated element value
  elem->value = value;

  // Save directly to state file
  char *str = g_strdup_printf("%ld", value);
  optional_state_save(elem->card, CONFIG_SECTION_CONTROLS, elem->name, str);
  g_free(str);
}

// Save filter state to simulated elements and state file
static void filter_stage_save_state(struct filter_stage *stage) {
  save_filter_param(stage->state_enable_elem, stage->enabled ? 1 : 0);
  save_filter_param(stage->state_type_elem, stage->params.type);
  save_filter_param(stage->state_freq_elem, (long)(stage->params.freq * FREQ_SCALE));
  save_filter_param(stage->state_q_elem, (long)(stage->params.q * Q_SCALE));
  save_filter_param(stage->state_gain_elem, (long)(stage->params.gain_db * GAIN_SCALE));
}

// Enable checkbox callback
static void filter_enable_toggled(GtkCheckButton *check, struct filter_stage *stage) {
  stage->enabled = gtk_check_button_get_active(check);
  filter_stage_update_coeffs(stage);
  filter_stage_save_state(stage);
}

// Format frequency for display in entry
static void format_freq_entry(struct filter_stage *stage) {
  char text[16];
  snprintf(text, sizeof(text), "%.0f", stage->params.freq);
  gtk_editable_set_text(GTK_EDITABLE(stage->freq_entry), text);
}

// Format Q for display in entry
static void format_q_entry(GtkWidget *entry, double q) {
  char text[16];
  snprintf(text, sizeof(text), "%.2f", q);
  gtk_editable_set_text(GTK_EDITABLE(entry), text);
}

// Format gain for display in entry
static void format_gain_entry(GtkWidget *entry, double gain_db) {
  char text[16];
  snprintf(text, sizeof(text), "%+.1f", gain_db);
  gtk_editable_set_text(GTK_EDITABLE(entry), text);
}

// Parse frequency from entry text (Hz)
static double parse_freq_entry(const char *text) {
  double value;
  if (sscanf(text, "%lf", &value) == 1)
    return CLAMP(value, 20.0, 20000.0);
  return -1.0;
}

// Parse Q from entry text
static double parse_q_entry(const char *text) {
  double value;
  if (sscanf(text, "%lf", &value) == 1)
    return CLAMP(value, 0.1, 10.0);
  return -1.0;
}

// Parse gain from entry text (accepts "+3.5 dB", "-6 dB", or bare number)
static double parse_gain_entry(const char *text) {
  double value;
  if (sscanf(text, "%lf", &value) == 1)
    return CLAMP(value, -18.0, 18.0);
  return -1000.0;
}

// Callbacks for filter stage controls
static void filter_type_changed(
  GtkWidget           *widget,
  BiquadFilterType     type,
  struct filter_stage *stage
) {
  stage->params.type = type;
  filter_stage_update_gain_visibility(stage);
  filter_stage_update_freq_q_visibility(stage);
  filter_stage_update_coeffs(stage);
  filter_stage_save_state(stage);
}

static void filter_freq_changed(GtkEditable *editable, struct filter_stage *stage) {
  const char *text = gtk_editable_get_text(editable);
  double freq = parse_freq_entry(text);

  if (freq > 0 && freq != stage->params.freq) {
    stage->params.freq = freq;
    filter_stage_update_coeffs(stage);
    filter_stage_save_state(stage);
  }
}

static gboolean filter_freq_select_all(gpointer data) {
  struct filter_stage *stage = data;
  gtk_editable_select_region(GTK_EDITABLE(stage->freq_entry), 0, -1);
  return G_SOURCE_REMOVE;
}

static void filter_freq_focus_enter(
  GtkEventControllerFocus *controller,
  struct filter_stage     *stage
) {
  stage->editing = TRUE;
  g_idle_add(filter_freq_select_all, stage);
}

static void filter_freq_focus_leave(
  GtkEventControllerFocus *controller,
  struct filter_stage     *stage
) {
  stage->editing = FALSE;
  g_signal_handlers_block_by_func(stage->freq_entry, filter_freq_changed, stage);
  format_freq_entry(stage);
  g_signal_handlers_unblock_by_func(stage->freq_entry, filter_freq_changed, stage);
}

static void filter_q_changed(GtkEditable *editable, struct filter_stage *stage) {
  const char *text = gtk_editable_get_text(editable);
  double q = parse_q_entry(text);

  if (q > 0 && q != stage->params.q) {
    stage->params.q = q;
    filter_stage_update_coeffs(stage);
    filter_stage_save_state(stage);
  }
}

static gboolean filter_q_select_all(gpointer data) {
  struct filter_stage *stage = data;
  gtk_editable_select_region(GTK_EDITABLE(stage->q_entry), 0, -1);
  return G_SOURCE_REMOVE;
}

static void filter_q_focus_enter(
  GtkEventControllerFocus *controller,
  struct filter_stage     *stage
) {
  stage->editing = TRUE;
  g_idle_add(filter_q_select_all, stage);
}

static void filter_q_focus_leave(
  GtkEventControllerFocus *controller,
  struct filter_stage     *stage
) {
  stage->editing = FALSE;
  g_signal_handlers_block_by_func(stage->q_entry, filter_q_changed, stage);
  format_q_entry(stage->q_entry, stage->params.q);
  g_signal_handlers_unblock_by_func(stage->q_entry, filter_q_changed, stage);
}

static void filter_gain_changed(GtkEditable *editable, struct filter_stage *stage) {
  const char *text = gtk_editable_get_text(editable);
  double gain = parse_gain_entry(text);

  if (gain > -999.0 && gain != stage->params.gain_db) {
    stage->params.gain_db = gain;
    filter_stage_update_coeffs(stage);
    filter_stage_save_state(stage);
  }
}

static gboolean filter_gain_select_all(gpointer data) {
  struct filter_stage *stage = data;
  gtk_editable_select_region(GTK_EDITABLE(stage->gain_entry), 0, -1);
  return G_SOURCE_REMOVE;
}

static void filter_gain_focus_enter(
  GtkEventControllerFocus *controller,
  struct filter_stage     *stage
) {
  stage->editing = TRUE;
  g_idle_add(filter_gain_select_all, stage);
}

static void filter_gain_focus_leave(
  GtkEventControllerFocus *controller,
  struct filter_stage     *stage
) {
  stage->editing = FALSE;
  g_signal_handlers_block_by_func(stage->gain_entry, filter_gain_changed, stage);
  format_gain_entry(stage->gain_entry, stage->params.gain_db);
  g_signal_handlers_unblock_by_func(stage->gain_entry, filter_gain_changed, stage);
}

// Forward declarations for response_filter_changed callback
static void filter_stage_update_coeffs(struct filter_stage *stage);
static void format_freq_entry(struct filter_stage *stage);
static void format_q_entry(GtkWidget *entry, double q);
static void format_gain_entry(GtkWidget *entry, double gain_db);

// Callbacks for when state elements change externally (e.g., loading config)
static void state_enable_updated(struct alsa_elem *elem, void *private) {
  struct filter_stage *stage = private;

  stage->enabled = alsa_get_elem_value(elem) ? TRUE : FALSE;

  g_signal_handlers_block_by_func(stage->enable_check, filter_enable_toggled, stage);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(stage->enable_check), stage->enabled);
  g_signal_handlers_unblock_by_func(stage->enable_check, filter_enable_toggled, stage);

  stage->updating_from_state = TRUE;
  filter_stage_update_coeffs(stage);
  stage->updating_from_state = FALSE;
}

static void state_type_updated(struct alsa_elem *elem, void *private) {
  struct filter_stage *stage = private;

  int type = alsa_get_elem_value(elem);
  if (type >= 0 && type < BIQUAD_TYPE_COUNT) {
    stage->params.type = type;
    filter_type_dropdown_set_selected(stage->type_dropdown, type);
    filter_stage_update_gain_visibility(stage);
    filter_stage_update_freq_q_visibility(stage);
    stage->updating_from_state = TRUE;
    filter_stage_update_coeffs(stage);
    stage->updating_from_state = FALSE;
  }
}

static void state_freq_updated(struct alsa_elem *elem, void *private) {
  struct filter_stage *stage = private;

  stage->params.freq = alsa_get_elem_value(elem) / (double)FREQ_SCALE;
  if (stage->params.freq < 20.0) stage->params.freq = 20.0;
  if (stage->params.freq > 20000.0) stage->params.freq = 20000.0;

  if (!stage->editing) {
    g_signal_handlers_block_by_func(stage->freq_entry, filter_freq_changed, stage);
    format_freq_entry(stage);
    g_signal_handlers_unblock_by_func(stage->freq_entry, filter_freq_changed, stage);
  }

  stage->updating_from_state = TRUE;
  filter_stage_update_coeffs(stage);
  stage->updating_from_state = FALSE;
}

static void state_q_updated(struct alsa_elem *elem, void *private) {
  struct filter_stage *stage = private;

  stage->params.q = alsa_get_elem_value(elem) / (double)Q_SCALE;
  if (stage->params.q < 0.1) stage->params.q = 0.1;
  if (stage->params.q > 10.0) stage->params.q = 10.0;

  if (!stage->editing) {
    g_signal_handlers_block_by_func(stage->q_entry, filter_q_changed, stage);
    format_q_entry(stage->q_entry, stage->params.q);
    g_signal_handlers_unblock_by_func(stage->q_entry, filter_q_changed, stage);
  }

  stage->updating_from_state = TRUE;
  filter_stage_update_coeffs(stage);
  stage->updating_from_state = FALSE;
}

static void state_gain_updated(struct alsa_elem *elem, void *private) {
  struct filter_stage *stage = private;

  stage->params.gain_db = alsa_get_elem_value(elem) / (double)GAIN_SCALE;
  if (stage->params.gain_db < -18.0) stage->params.gain_db = -18.0;
  if (stage->params.gain_db > 18.0) stage->params.gain_db = 18.0;

  if (!stage->editing) {
    g_signal_handlers_block_by_func(stage->gain_entry, filter_gain_changed, stage);
    format_gain_entry(stage->gain_entry, stage->params.gain_db);
    g_signal_handlers_unblock_by_func(stage->gain_entry, filter_gain_changed, stage);
  }

  stage->updating_from_state = TRUE;
  filter_stage_update_coeffs(stage);
  stage->updating_from_state = FALSE;
}

// Callback for filter-changed signal from response widget
static void response_filter_changed(
  GtkFilterResponse           *response,
  int                          band,
  const struct biquad_params  *params,
  struct filter_response_stages *data
) {
  if (band < 0 || band >= data->num_stages)
    return;

  struct filter_stage *stage = data->stages[band];
  if (!stage)
    return;

  // Update stage params
  stage->params.freq = params->freq;
  stage->params.q = params->q;
  stage->params.gain_db = params->gain_db;

  // Update entries (block signals to avoid feedback loop)
  g_signal_handlers_block_by_func(stage->freq_entry, filter_freq_changed, stage);
  g_signal_handlers_block_by_func(stage->q_entry, filter_q_changed, stage);
  g_signal_handlers_block_by_func(stage->gain_entry, filter_gain_changed, stage);

  format_freq_entry(stage);
  format_q_entry(stage->q_entry, params->q);
  format_gain_entry(stage->gain_entry, params->gain_db);

  g_signal_handlers_unblock_by_func(stage->freq_entry, filter_freq_changed, stage);
  g_signal_handlers_unblock_by_func(stage->q_entry, filter_q_changed, stage);
  g_signal_handlers_unblock_by_func(stage->gain_entry, filter_gain_changed, stage);

  // Update hardware and save state
  filter_stage_update_coeffs(stage);
  filter_stage_save_state(stage);
}

static void filter_response_stages_destroy(struct filter_response_stages *data) {
  g_free(data);
}

// Callback for highlight-changed signal from response widget
static void response_highlight_changed(
  GtkFilterResponse           *response,
  int                          band,
  struct filter_response_stages *data
) {
  // Remove highlight from all stages
  for (int i = 0; i < data->num_stages; i++) {
    if (data->stages[i] && data->stages[i]->enable_check)
      gtk_widget_remove_css_class(data->stages[i]->enable_check, "filter-stage-hover");
  }

  // Add highlight to the selected stage
  if (band >= 0 && band < data->num_stages &&
      data->stages[band] && data->stages[band]->enable_check) {
    gtk_widget_add_css_class(data->stages[band]->enable_check, "filter-stage-hover");
  }
}

static void filter_stage_destroy(struct filter_stage *stage) {
  g_free(stage);
}

// Callback for when biquad coefficients are changed externally
static void filter_stage_coeffs_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct filter_stage *stage = private;

  long *fixed = alsa_get_elem_int_values(elem);
  if (!fixed)
    return;

  struct biquad_coeffs coeffs;
  biquad_from_fixed_point(fixed, &coeffs);

  struct biquad_params new_params;
  biquad_analyze(&coeffs, SAMPLE_RATE, &new_params);
  free(fixed);

  // Check if it's a bypass (identity) filter
  gboolean new_enabled = !(new_params.type == BIQUAD_TYPE_GAIN &&
                           new_params.gain_db == 0.0);

  // If disabled, keep the previous filter parameters for the UI
  if (new_enabled) {
    stage->params = new_params;
  }
  stage->enabled = new_enabled;

  // Update enable checkbox
  g_signal_handlers_block_by_func(
    stage->enable_check, filter_enable_toggled, stage
  );
  gtk_check_button_set_active(
    GTK_CHECK_BUTTON(stage->enable_check), stage->enabled
  );
  g_signal_handlers_unblock_by_func(
    stage->enable_check, filter_enable_toggled, stage
  );

  // Update entries (skip if user is currently editing)
  if (!stage->editing) {
    g_signal_handlers_block_by_func(stage->freq_entry, filter_freq_changed, stage);
    g_signal_handlers_block_by_func(stage->q_entry, filter_q_changed, stage);
    g_signal_handlers_block_by_func(stage->gain_entry, filter_gain_changed, stage);

    format_freq_entry(stage);
    format_q_entry(stage->q_entry, stage->params.q);
    format_gain_entry(stage->gain_entry, stage->params.gain_db);

    g_signal_handlers_unblock_by_func(stage->freq_entry, filter_freq_changed, stage);
    g_signal_handlers_unblock_by_func(stage->q_entry, filter_q_changed, stage);
    g_signal_handlers_unblock_by_func(stage->gain_entry, filter_gain_changed, stage);
  }

  // Update type dropdown
  filter_type_dropdown_set_selected(stage->type_dropdown, stage->params.type);

  // Update control visibility based on filter type
  filter_stage_update_gain_visibility(stage);
  filter_stage_update_freq_q_visibility(stage);

  // Update response graph
  if (stage->response) {
    gtk_filter_response_set_filter(
      stage->response, stage->band_index, &stage->params
    );
    gtk_filter_response_set_band_enabled(
      stage->response, stage->band_index, stage->enabled
    );
  }

  // Save state (skip if this update originated from state elements to avoid loop)
  if (!stage->updating_from_state)
    filter_stage_save_state(stage);
}

// Hover callbacks for highlighting
static void filter_stage_enter(
  GtkEventControllerMotion *controller,
  double                    x,
  double                    y,
  struct filter_stage      *stage
) {
  if (stage->response)
    gtk_filter_response_set_highlight(stage->response, stage->band_index);
  gtk_widget_add_css_class(stage->enable_check, "filter-stage-hover");
}

static void filter_stage_leave(
  GtkEventControllerMotion *controller,
  struct filter_stage      *stage
) {
  if (stage->response)
    gtk_filter_response_set_highlight(stage->response, -1);
  gtk_widget_remove_css_class(stage->enable_check, "filter-stage-hover");
}

static void add_hover_controller(GtkWidget *widget, struct filter_stage *stage) {
  GtkEventController *motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "enter", G_CALLBACK(filter_stage_enter), stage);
  g_signal_connect(motion, "leave", G_CALLBACK(filter_stage_leave), stage);
  gtk_widget_add_controller(widget, motion);
}

// Create controls for one filter stage in a grid row
static void make_filter_stage(
  struct alsa_card   *card,
  struct alsa_elem   *coeff_elem,
  int                 band_index,
  GtkFilterResponse  *response,
  const char         *label_text,
  BiquadFilterType    default_type,
  const char         *filter_type,
  int                 channel,
  int                 stage_num,
  GtkWidget          *grid,
  int                 row,
  struct filter_stage **stage_out
) {
  struct filter_stage *stage = g_malloc0(sizeof(struct filter_stage));
  if (stage_out)
    *stage_out = stage;
  stage->coeff_elem = coeff_elem;
  stage->band_index = band_index;
  stage->response = response;

  // Look up simulated state elements
  stage->state_enable_elem = dsp_state_get_enable_elem(
    card, filter_type, channel, stage_num
  );
  stage->state_type_elem = dsp_state_get_type_elem(
    card, filter_type, channel, stage_num
  );
  stage->state_freq_elem = dsp_state_get_freq_elem(
    card, filter_type, channel, stage_num
  );
  stage->state_q_elem = dsp_state_get_q_elem(
    card, filter_type, channel, stage_num
  );
  stage->state_gain_elem = dsp_state_get_gain_elem(
    card, filter_type, channel, stage_num
  );

  // Try to read and analyze existing coefficients from hardware
  stage->enabled = TRUE;
  stage->params.type = default_type;
  stage->params.freq = 1000.0;
  stage->params.q = 0.707;
  stage->params.gain_db = 0.0;

  long *fixed = alsa_get_elem_int_values(coeff_elem);
  if (fixed) {
    struct biquad_coeffs coeffs;
    biquad_from_fixed_point(fixed, &coeffs);
    biquad_analyze(&coeffs, SAMPLE_RATE, &stage->params);

    // If it's a bypass (0dB gain), load saved params from simulated elements
    if (stage->params.type == BIQUAD_TYPE_GAIN && stage->params.gain_db == 0.0) {
      stage->enabled = FALSE;

      // Load saved params from simulated elements
      if (stage->state_type_elem)
        stage->params.type = alsa_get_elem_value(stage->state_type_elem);
      if (stage->state_freq_elem)
        stage->params.freq = alsa_get_elem_value(stage->state_freq_elem) / (double)FREQ_SCALE;
      if (stage->state_q_elem)
        stage->params.q = alsa_get_elem_value(stage->state_q_elem) / (double)Q_SCALE;
      if (stage->state_gain_elem)
        stage->params.gain_db = alsa_get_elem_value(stage->state_gain_elem) / (double)GAIN_SCALE;

      // Clamp values to valid ranges
      if (stage->params.freq < 20.0) stage->params.freq = 20.0;
      if (stage->params.freq > 20000.0) stage->params.freq = 20000.0;
      if (stage->params.q < 0.1) stage->params.q = 0.1;
      if (stage->params.q > 10.0) stage->params.q = 10.0;
      if (stage->params.gain_db < -18.0) stage->params.gain_db = -18.0;
      if (stage->params.gain_db > 18.0) stage->params.gain_db = 18.0;
    }

    free(fixed);
  }

  // Save initial state to simulated elements
  filter_stage_save_state(stage);

  int col = 0;

  // Enable checkbox with stage label
  stage->enable_check = gtk_check_button_new_with_label(label_text);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(stage->enable_check), stage->enabled);
  gtk_grid_attach(GTK_GRID(grid), stage->enable_check, col++, row, 1, 1);
  add_hover_controller(stage->enable_check, stage);

  // Filter type dropdown with icons
  stage->type_dropdown = make_filter_type_dropdown(stage->params.type);
  gtk_grid_attach(GTK_GRID(grid), stage->type_dropdown, col++, row, 1, 1);
  add_hover_controller(stage->type_dropdown, stage);

  // Frequency entry
  stage->freq_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  stage->freq_entry = gtk_entry_new();
  gtk_editable_set_max_width_chars(GTK_EDITABLE(stage->freq_entry), 5);
  gtk_entry_set_max_length(GTK_ENTRY(stage->freq_entry), 5);
  gtk_box_append(GTK_BOX(stage->freq_box), stage->freq_entry);
  gtk_box_append(GTK_BOX(stage->freq_box), gtk_label_new("Hz"));
  format_freq_entry(stage);
  gtk_grid_attach(GTK_GRID(grid), stage->freq_box, col++, row, 1, 1);
  add_hover_controller(stage->freq_box, stage);

  // Q entry
  stage->q_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_box_append(GTK_BOX(stage->q_box), gtk_label_new("Q"));
  stage->q_entry = gtk_entry_new();
  gtk_editable_set_max_width_chars(GTK_EDITABLE(stage->q_entry), 4);
  gtk_entry_set_max_length(GTK_ENTRY(stage->q_entry), 4);
  format_q_entry(stage->q_entry, stage->params.q);
  gtk_box_append(GTK_BOX(stage->q_box), stage->q_entry);
  gtk_grid_attach(GTK_GRID(grid), stage->q_box, col++, row, 1, 1);
  add_hover_controller(stage->q_box, stage);

  // Gain entry (shown only for peaking/shelving types)
  stage->gain_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  stage->gain_entry = gtk_entry_new();
  gtk_editable_set_max_width_chars(GTK_EDITABLE(stage->gain_entry), 5);
  gtk_entry_set_max_length(GTK_ENTRY(stage->gain_entry), 5);
  format_gain_entry(stage->gain_entry, stage->params.gain_db);
  gtk_box_append(GTK_BOX(stage->gain_box), stage->gain_entry);
  gtk_box_append(GTK_BOX(stage->gain_box), gtk_label_new("dB"));
  gtk_grid_attach(GTK_GRID(grid), stage->gain_box, col++, row, 1, 1);
  add_hover_controller(stage->gain_box, stage);

  // Update control visibility based on filter type
  filter_stage_update_gain_visibility(stage);
  filter_stage_update_freq_q_visibility(stage);

  // Connect signals
  g_signal_connect(
    stage->enable_check, "toggled",
    G_CALLBACK(filter_enable_toggled), stage
  );
  filter_type_dropdown_connect_changed(
    stage->type_dropdown,
    (FilterTypeChangedCallback)filter_type_changed,
    stage
  );
  g_signal_connect(
    stage->freq_entry, "changed",
    G_CALLBACK(filter_freq_changed), stage
  );
  GtkEventController *freq_focus = gtk_event_controller_focus_new();
  g_signal_connect(freq_focus, "enter", G_CALLBACK(filter_freq_focus_enter), stage);
  g_signal_connect(freq_focus, "leave", G_CALLBACK(filter_freq_focus_leave), stage);
  gtk_widget_add_controller(stage->freq_entry, freq_focus);

  g_signal_connect(
    stage->q_entry, "changed",
    G_CALLBACK(filter_q_changed), stage
  );
  GtkEventController *q_focus = gtk_event_controller_focus_new();
  g_signal_connect(q_focus, "enter", G_CALLBACK(filter_q_focus_enter), stage);
  g_signal_connect(q_focus, "leave", G_CALLBACK(filter_q_focus_leave), stage);
  gtk_widget_add_controller(stage->q_entry, q_focus);

  g_signal_connect(
    stage->gain_entry, "changed",
    G_CALLBACK(filter_gain_changed), stage
  );
  GtkEventController *gain_focus = gtk_event_controller_focus_new();
  g_signal_connect(gain_focus, "enter", G_CALLBACK(filter_gain_focus_enter), stage);
  g_signal_connect(gain_focus, "leave", G_CALLBACK(filter_gain_focus_leave), stage);
  gtk_widget_add_controller(stage->gain_entry, gain_focus);

  // Register callbacks on state elements for external updates (e.g., loading config)
  if (stage->state_enable_elem)
    alsa_elem_add_callback(stage->state_enable_elem, state_enable_updated, stage, NULL);
  if (stage->state_type_elem)
    alsa_elem_add_callback(stage->state_type_elem, state_type_updated, stage, NULL);
  if (stage->state_freq_elem)
    alsa_elem_add_callback(stage->state_freq_elem, state_freq_updated, stage, NULL);
  if (stage->state_q_elem)
    alsa_elem_add_callback(stage->state_q_elem, state_q_updated, stage, NULL);
  if (stage->state_gain_elem)
    alsa_elem_add_callback(stage->state_gain_elem, state_gain_updated, stage, NULL);

  g_object_weak_ref(G_OBJECT(grid), (GWeakNotify)filter_stage_destroy, stage);

  // Register callback for external coefficient changes
  alsa_elem_add_callback(coeff_elem, filter_stage_coeffs_updated, stage, NULL);

  // Update response graph with loaded parameters
  if (stage->response) {
    gtk_filter_response_set_filter(
      stage->response, stage->band_index, &stage->params
    );
    gtk_filter_response_set_band_enabled(
      stage->response, stage->band_index, stage->enabled
    );
  }
}

// Callback data for enable switches that update the response widget
struct enable_switch_data {
  GtkFilterResponse *response;
};

static void enable_switch_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct enable_switch_data *data = private;
  gboolean enabled = alsa_get_elem_value(elem);
  gtk_filter_response_set_enabled(data->response, enabled);
}

static void enable_switch_destroy(struct enable_switch_data *data) {
  g_free(data);
}

// Callback data for compressor enable switch
struct comp_enable_data {
  GtkCompressorCurve *curve;
};

static void comp_enable_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct comp_enable_data *data = private;
  gboolean enabled = alsa_get_elem_value(elem);
  gtk_compressor_curve_set_enabled(data->curve, enabled);
}

static void comp_enable_destroy(struct comp_enable_data *data) {
  g_free(data);
}

// Callback data for DSP enable switch (affects all visualizations)
struct dsp_enable_data {
  GtkFilterResponse  *precomp_response;
  GtkCompressorCurve *comp_curve;
  GtkFilterResponse  *peq_response;
};

static void dsp_enable_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct dsp_enable_data *data = private;
  gboolean enabled = alsa_get_elem_value(elem);
  if (data->precomp_response)
    gtk_filter_response_set_dsp_enabled(data->precomp_response, enabled);
  if (data->comp_curve)
    gtk_compressor_curve_set_dsp_enabled(data->comp_curve, enabled);
  if (data->peq_response)
    gtk_filter_response_set_dsp_enabled(data->peq_response, enabled);
}

// Callback data for DSP header name updates
struct dsp_header_data {
  GtkWidget         *button;  // toggle button (NULL if using label)
  GtkWidget         *label;   // label widget (NULL if using button)
  struct routing_src *src;
  int                channel;
};

static void dsp_header_name_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct dsp_header_data *data = private;
  const char *name = get_routing_src_display_name(data->src);

  if (!name) {
    char fallback[16];
    snprintf(fallback, sizeof(fallback), "DSP %d", data->channel);
    if (data->button)
      gtk_button_set_label(GTK_BUTTON(data->button), fallback);
    else if (data->label) {
      char *markup = g_strdup_printf("<b>%s</b>", fallback);
      gtk_label_set_markup(GTK_LABEL(data->label), markup);
      g_free(markup);
    }
    return;
  }

  if (data->button)
    gtk_button_set_label(GTK_BUTTON(data->button), name);
  else if (data->label) {
    char *markup = g_strdup_printf("<b>%s</b>", name);
    gtk_label_set_markup(GTK_LABEL(data->label), markup);
    g_free(markup);
  }
}

static void dsp_header_destroy(struct dsp_header_data *data) {
  g_free(data);
}

static void dsp_enable_destroy(struct dsp_enable_data *data) {
  g_free(data);
}

// Preset types
enum {
  PRESET_TYPE_PRECOMP,
  PRESET_TYPE_COMPRESSOR,
  PRESET_TYPE_PEQ
};

// Pre-comp preset coefficients (2 stages, 5 coefficients each)
struct precomp_preset {
  const char *name;
  long stage1[5];
  long stage2[5];
};

static const struct precomp_preset precomp_presets[] = {
  {
    "None",
    { 268435456, 0, 0, 0, 0 },
    { 268435456, 0, 0, 0, 0 }
  },
  {
    "Rumble Reduction Low",
    { 266497594, -532995188, 266497594, 532986969, -264567952 },
    { 267626948, -535253896, 267626948, 535245642, -266826694 }
  },
  {
    "Rumble Reduction High",
    { 265856028, -531712056, 265856028, 531697479, -263291177 },
    { 267356697, -534713394, 267356697, 534698735, -266292598 }
  }
};

#define NUM_PRECOMP_PRESETS (sizeof(precomp_presets) / sizeof(precomp_presets[0]))

// Compressor preset values (threshold, ratio*2, knee, attack, release, makeup)
struct compressor_preset {
  const char *name;
  int threshold;  // dB
  int ratio;      // stored as ratio * 2 (so 4:1 = 8)
  int knee;       // dB
  int attack;     // ms
  int release;    // ms
  int makeup;     // dB
};

static const struct compressor_preset compressor_presets[] = {
  { "Off",    0, 8, 3, 10, 30, 0 },
  { "Low",  -10, 8, 3, 10, 30, 2 },
  { "Med",  -20, 8, 3, 10, 30, 3 },
  { "High", -30, 8, 3, 10, 30, 5 }
};

#define NUM_COMPRESSOR_PRESETS (sizeof(compressor_presets) / sizeof(compressor_presets[0]))

// Compressor elements for presets
struct compressor_elems {
  struct alsa_elem *threshold;
  struct alsa_elem *ratio;
  struct alsa_elem *knee;
  struct alsa_elem *attack;
  struct alsa_elem *release;
  struct alsa_elem *makeup;
};

// PEQ preset coefficients (3 bands, 5 coefficients each)
struct peq_preset {
  const char *name;
  long band1[5];
  long band2[5];
  long band3[5];
};

static const struct peq_preset peq_presets[] = {
  {
    "Radio",
    { 268764056, -534822977, 266136367, 534822977, -266464967 },
    { 264343107, -475942323, 220923477, 475942323, -216831129 },
    { 280826929, -286659621, 124570994, 286659621, -136962468 }
  },
  {
    "Clean",
    { 268173186, -534243182, 266147357, 534243182, -265885087 },
    { 273994701, -484439285, 219935310, 484439285, -225494556 },
    { 285089098, -288665257, 123145223, 288665257, -139798866 }
  },
  {
    "Warm",
    { 268304503, -534385186, 266158065, 534385186, -266027113 },
    { 275408969, -485541124, 219644468, 485541124, -226617981 },
    { 260481651, -276190834, 130111172, 276190834, -122157368 }
  },
  {
    "Bright",
    { 268173186, -534243182, 266147357, 534243182, -265885087 },
    { 276834931, -486616512, 219314962, 486616512, -227714437 },
    { 293825201, -292586874, 119955124, 292586874, -145344869 }
  }
};

#define NUM_PEQ_PRESETS (sizeof(peq_presets) / sizeof(peq_presets[0]))

// Data passed to preset callbacks
struct preset_callback_data {
  int preset_type;
  struct filter_response_stages *stages;
  struct compressor_elems *comp_elems;
  GtkWidget *popover;
};

// Apply a pre-comp preset
static void apply_precomp_preset(
  struct filter_response_stages *stages,
  const struct precomp_preset   *preset
) {
  if (stages->num_stages >= 1 && stages->stages[0]) {
    alsa_set_elem_int_values(stages->stages[0]->coeff_elem, preset->stage1, 5);
  }
  if (stages->num_stages >= 2 && stages->stages[1]) {
    alsa_set_elem_int_values(stages->stages[1]->coeff_elem, preset->stage2, 5);
  }
}

// Apply a compressor preset
static void apply_compressor_preset(
  struct compressor_elems       *elems,
  const struct compressor_preset *preset
) {
  if (elems->threshold)
    alsa_set_elem_value(elems->threshold, preset->threshold);
  if (elems->ratio)
    alsa_set_elem_value(elems->ratio, preset->ratio);
  if (elems->knee)
    alsa_set_elem_value(elems->knee, preset->knee);
  if (elems->attack)
    alsa_set_elem_value(elems->attack, preset->attack);
  if (elems->release)
    alsa_set_elem_value(elems->release, preset->release);
  if (elems->makeup)
    alsa_set_elem_value(elems->makeup, preset->makeup);
}

// Apply a PEQ preset
static void apply_peq_preset(
  struct filter_response_stages *stages,
  const struct peq_preset       *preset
) {
  if (stages->num_stages >= 1 && stages->stages[0]) {
    alsa_set_elem_int_values(stages->stages[0]->coeff_elem, preset->band1, 5);
  }
  if (stages->num_stages >= 2 && stages->stages[1]) {
    alsa_set_elem_int_values(stages->stages[1]->coeff_elem, preset->band2, 5);
  }
  if (stages->num_stages >= 3 && stages->stages[2]) {
    alsa_set_elem_int_values(stages->stages[2]->coeff_elem, preset->band3, 5);
  }
}

// Callback when a preset is selected from the list
static void preset_list_activated(
  GtkListView                 *listview,
  guint                        index,
  struct preset_callback_data *data
) {
  if (data->preset_type == PRESET_TYPE_PRECOMP && index < NUM_PRECOMP_PRESETS) {
    apply_precomp_preset(data->stages, &precomp_presets[index]);
  } else if (data->preset_type == PRESET_TYPE_COMPRESSOR && index < NUM_COMPRESSOR_PRESETS) {
    apply_compressor_preset(data->comp_elems, &compressor_presets[index]);
  } else if (data->preset_type == PRESET_TYPE_PEQ && index < NUM_PEQ_PRESETS) {
    apply_peq_preset(data->stages, &peq_presets[index]);
  }

  gtk_popover_popdown(GTK_POPOVER(data->popover));
}

// Setup callback for preset list items
static void preset_list_item_setup(
  GtkListItemFactory *factory,
  GtkListItem        *list_item,
  gpointer            user_data
) {
  GtkWidget *label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0);
  gtk_list_item_set_child(list_item, label);
}

// Bind callback for preset list items
static void preset_list_item_bind(
  GtkListItemFactory *factory,
  GtkListItem        *list_item,
  gpointer            user_data
) {
  GtkWidget *label = gtk_list_item_get_child(list_item);
  GtkStringObject *str_obj = gtk_list_item_get_item(list_item);
  gtk_label_set_text(GTK_LABEL(label), gtk_string_object_get_string(str_obj));
}

// Callback for preset button clicks - shows a popover with preset list
static void preset_button_clicked(GtkButton *button, gpointer user_data) {
  struct preset_callback_data *data = user_data;

  GtkWidget *popover = gtk_popover_new();
  gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
  gtk_widget_set_parent(popover, GTK_WIDGET(button));
  data->popover = popover;

  GtkStringList *model = NULL;

  if (data->preset_type == PRESET_TYPE_PRECOMP && data->stages) {
    model = gtk_string_list_new(NULL);
    for (size_t i = 0; i < NUM_PRECOMP_PRESETS; i++)
      gtk_string_list_append(model, precomp_presets[i].name);
  } else if (data->preset_type == PRESET_TYPE_COMPRESSOR && data->comp_elems) {
    model = gtk_string_list_new(NULL);
    for (size_t i = 0; i < NUM_COMPRESSOR_PRESETS; i++)
      gtk_string_list_append(model, compressor_presets[i].name);
  } else if (data->preset_type == PRESET_TYPE_PEQ && data->stages) {
    model = gtk_string_list_new(NULL);
    for (size_t i = 0; i < NUM_PEQ_PRESETS; i++)
      gtk_string_list_append(model, peq_presets[i].name);
  }

  if (model) {
    // Factory for list items
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(preset_list_item_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(preset_list_item_bind), NULL);

    GtkSingleSelection *selection = gtk_single_selection_new(G_LIST_MODEL(model));

    GtkWidget *listview = gtk_list_view_new(
      GTK_SELECTION_MODEL(selection), factory
    );
    gtk_list_view_set_single_click_activate(GTK_LIST_VIEW(listview), TRUE);
    gtk_widget_add_css_class(listview, "filter-type-list");
    gtk_popover_set_child(GTK_POPOVER(popover), listview);

    g_signal_connect(listview, "activate", G_CALLBACK(preset_list_activated), data);
  } else {
    GtkWidget *label = gtk_label_new("Presets TBD");
    gtk_widget_set_margin_start(label, 10);
    gtk_widget_set_margin_end(label, 10);
    gtk_widget_set_margin_top(label, 5);
    gtk_widget_set_margin_bottom(label, 5);
    gtk_popover_set_child(GTK_POPOVER(popover), label);
  }

  gtk_popover_popup(GTK_POPOVER(popover));
}

static void preset_callback_data_destroy(struct preset_callback_data *data) {
  if (data->comp_elems)
    g_free(data->comp_elems);
  g_free(data);
}

// Create a section box with header, enable, presets button, and content
// Returns the presets button via presets_button_out for later configuration
static GtkWidget *create_section_box(
  const char       *title,
  struct alsa_elem *enable_elem,
  int               preset_type,
  GtkWidget       **presets_button_out
) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_margin_start(box, 5);
  gtk_widget_set_margin_end(box, 5);

  // Header row with enable toggle and presets button
  GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_box_append(GTK_BOX(box), header_box);

  if (enable_elem) {
    GtkWidget *enable = make_boolean_alsa_elem(enable_elem, title, NULL);
    gtk_widget_add_css_class(enable, "dsp");
    gtk_widget_set_hexpand(enable, TRUE);
    gtk_box_append(GTK_BOX(header_box), enable);
  } else {
    GtkWidget *label = gtk_label_new(NULL);
    char *markup = g_strdup_printf("<b>%s</b>", title);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(header_box), label);
  }

  // Presets button - callback data will be set up after stages are created
  GtkWidget *presets_button = gtk_button_new_with_label("Presets");
  gtk_widget_add_css_class(presets_button, "presets-button");
  gtk_widget_set_valign(presets_button, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(header_box), presets_button);

  if (presets_button_out)
    *presets_button_out = presets_button;

  // Store preset type on the button for later
  g_object_set_data(G_OBJECT(presets_button), "preset_type", GINT_TO_POINTER(preset_type));

  // Default callback shows TBD until connect_presets_button is called
  struct preset_callback_data *default_data = g_malloc0(sizeof(struct preset_callback_data));
  default_data->preset_type = preset_type;
  default_data->stages = NULL;
  g_signal_connect(presets_button, "clicked", G_CALLBACK(preset_button_clicked), default_data);
  g_object_set_data_full(G_OBJECT(presets_button), "preset_data", default_data,
                         (GDestroyNotify)preset_callback_data_destroy);

  return box;
}

// Connect presets button to stages after stages are created
static void connect_presets_button(
  GtkWidget                     *presets_button,
  struct filter_response_stages *stages
) {
  int preset_type = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(presets_button), "preset_type"));

  // Get old data and disconnect old handler
  struct preset_callback_data *old_data = g_object_get_data(
    G_OBJECT(presets_button), "preset_data"
  );
  if (old_data) {
    g_signal_handlers_disconnect_by_func(
      presets_button, preset_button_clicked, old_data
    );
  }

  struct preset_callback_data *data = g_malloc0(sizeof(struct preset_callback_data));
  data->preset_type = preset_type;
  data->stages = stages;

  g_signal_connect(presets_button, "clicked", G_CALLBACK(preset_button_clicked), data);
  g_object_set_data_full(G_OBJECT(presets_button), "preset_data", data,
                         (GDestroyNotify)preset_callback_data_destroy);
}

// Connect compressor presets button to elements
static void connect_compressor_presets_button(
  GtkWidget             *presets_button,
  struct compressor_elems *comp_elems
) {
  int preset_type = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(presets_button), "preset_type"));

  // Get old data and disconnect old handler
  struct preset_callback_data *old_data = g_object_get_data(
    G_OBJECT(presets_button), "preset_data"
  );
  if (old_data) {
    g_signal_handlers_disconnect_by_func(
      presets_button, preset_button_clicked, old_data
    );
  }

  struct preset_callback_data *data = g_malloc0(sizeof(struct preset_callback_data));
  data->preset_type = preset_type;
  data->comp_elems = comp_elems;

  g_signal_connect(presets_button, "clicked", G_CALLBACK(preset_button_clicked), data);
  g_object_set_data_full(G_OBJECT(presets_button), "preset_data", data,
                         (GDestroyNotify)preset_callback_data_destroy);
}

// Create controls for one line input channel
static void add_channel_controls(
  struct alsa_card *card,
  GtkWidget        *grid,
  int              *grid_y,
  int               channel
) {
  GPtrArray *elems = card->elems;
  char *prefix = g_strdup_printf("Line In %d ", channel);
  GtkWidget *w;
  char *name;
  struct alsa_elem *elem;

  // Keep track of visualization widgets for DSP enable callback
  GtkFilterResponse *precomp_response = NULL;
  GtkCompressorCurve *comp_curve = NULL;
  GtkFilterResponse *peq_response = NULL;

  // Find DSP Output source for this channel to get custom name
  struct routing_src *dsp_src = NULL;
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    if (src->port_category == PC_DSP && src->port_num == channel - 1) {
      dsp_src = src;
      break;
    }
  }

  // Find DSP Input sink for this channel
  struct routing_snk *dsp_snk = NULL;
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    if (snk->elem &&
        snk->elem->port_category == PC_DSP &&
        snk->elem->port_num == channel - 1) {
      dsp_snk = snk;
      break;
    }
  }

  // DSP header row with enable
  name = g_strdup_printf("%sDSP Capture Switch", prefix);
  struct alsa_elem *dsp_enable = get_elem_by_name(elems, name);
  g_free(name);

  GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_margin_top(header_box, *grid_y > 0 ? 15 : 0);
  gtk_grid_attach(GTK_GRID(grid), header_box, 0, (*grid_y)++, 1, 1);

  const char *header = dsp_src
    ? get_routing_src_display_name(dsp_src)
    : NULL;
  char *header_fallback = NULL;
  if (!header) {
    header_fallback = g_strdup_printf("DSP %d", channel);
    header = header_fallback;
  }

  GtkWidget *header_button = NULL;
  GtkWidget *header_label = NULL;

  if (dsp_enable) {
    w = make_boolean_alsa_elem(dsp_enable, header, NULL);
    gtk_widget_add_css_class(w, "dsp");
    gtk_box_append(GTK_BOX(header_box), w);
    header_button = w;
  } else {
    w = gtk_label_new(NULL);
    char *markup = g_strdup_printf("<b>%s</b>", header);
    gtk_label_set_markup(GTK_LABEL(w), markup);
    g_free(markup);
    gtk_box_append(GTK_BOX(header_box), w);
    header_label = w;
  }
  g_free(header_fallback);

  // Register callback for custom name changes
  if (dsp_src && dsp_src->custom_name_elem) {
    struct dsp_header_data *hdr_data = g_malloc0(sizeof(struct dsp_header_data));
    hdr_data->button = header_button;
    hdr_data->label = header_label;
    hdr_data->src = dsp_src;
    hdr_data->channel = channel;
    alsa_elem_add_callback(
      dsp_src->custom_name_elem, dsp_header_name_updated, hdr_data,
      (GDestroyNotify)dsp_header_destroy
    );
  }

  // Horizontal box for the three sections
  GtkWidget *sections_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_hexpand(sections_box, TRUE);
  gtk_grid_attach(GTK_GRID(grid), sections_box, 0, (*grid_y)++, 1, 1);

  // === Pre-Comp Filter section ===
  name = g_strdup_printf("%sPre-Comp Filter Enable", prefix);
  struct alsa_elem *precomp_enable = get_elem_by_name(elems, name);
  g_free(name);

  if (precomp_enable) {
    GtkWidget *precomp_presets_button;
    GtkWidget *precomp_box = create_section_box(
      "Pre-Compressor Filter", precomp_enable,
      PRESET_TYPE_PRECOMP, &precomp_presets_button
    );
    gtk_box_append(GTK_BOX(sections_box), precomp_box);

    // Response widget
    GtkWidget *precomp_response_widget = gtk_filter_response_new(2);
    precomp_response = GTK_FILTER_RESPONSE(precomp_response_widget);
    gtk_box_append(GTK_BOX(precomp_box), precomp_response_widget);

    // Connect enable to response widget
    struct enable_switch_data *en_data = g_malloc0(
      sizeof(struct enable_switch_data)
    );
    en_data->response = precomp_response;
    alsa_elem_add_callback(precomp_enable, enable_switch_updated, en_data,
                           (GDestroyNotify)enable_switch_destroy);
    enable_switch_updated(precomp_enable, en_data);

    // Filter stages grid
    GtkWidget *precomp_grid = gtk_grid_new();
    gtk_widget_add_css_class(precomp_grid, "filter-stage");
    gtk_widget_set_hexpand(precomp_grid, TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(precomp_grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(precomp_grid), 3);
    gtk_box_append(GTK_BOX(precomp_box), precomp_grid);

    struct filter_response_stages *precomp_stages = g_malloc0(
      sizeof(struct filter_response_stages)
    );

    int precomp_row = 0;
    for (int i = 1; i <= 2; i++) {
      name = g_strdup_printf("%sPre-Comp Coefficients %d", prefix, i);
      elem = get_elem_by_name(elems, name);
      g_free(name);

      if (elem) {
        char stage_label[16];
        snprintf(stage_label, sizeof(stage_label), "Stage %d", i);
        struct filter_stage *stage;
        make_filter_stage(
          card, elem, i - 1, precomp_response, stage_label, BIQUAD_TYPE_HIGHPASS,
          "Pre-Comp", channel, i, precomp_grid, precomp_row++, &stage
        );
        precomp_stages->stages[i - 1] = stage;
        precomp_stages->num_stages = i;
      }
    }

    // Connect presets button now that stages are created
    connect_presets_button(precomp_presets_button, precomp_stages);

    // Connect signals for drag and highlight updates
    g_signal_connect(precomp_response, "filter-changed",
                     G_CALLBACK(response_filter_changed), precomp_stages);
    g_signal_connect(precomp_response, "highlight-changed",
                     G_CALLBACK(response_highlight_changed), precomp_stages);
    g_object_weak_ref(G_OBJECT(precomp_response_widget),
                      (GWeakNotify)filter_response_stages_destroy, precomp_stages);
  }

  // === Compressor section ===
  name = g_strdup_printf("%sCompressor Enable", prefix);
  struct alsa_elem *comp_enable = get_elem_by_name(elems, name);
  g_free(name);

  if (comp_enable) {
    GtkWidget *comp_presets_button;
    GtkWidget *comp_box = create_section_box(
      "Compressor", comp_enable,
      PRESET_TYPE_COMPRESSOR, &comp_presets_button
    );
    gtk_box_append(GTK_BOX(sections_box), comp_box);

    // Curve widget
    GtkWidget *curve_widget = gtk_compressor_curve_new();
    comp_curve = GTK_COMPRESSOR_CURVE(curve_widget);
    gtk_box_append(GTK_BOX(comp_box), curve_widget);

    // Connect enable to curve widget
    struct comp_enable_data *comp_en_data = g_malloc0(
      sizeof(struct comp_enable_data)
    );
    comp_en_data->curve = comp_curve;
    alsa_elem_add_callback(comp_enable, comp_enable_updated, comp_en_data,
                           (GDestroyNotify)comp_enable_destroy);
    comp_enable_updated(comp_enable, comp_en_data);

    // Store for level updates
    struct dsp_comp_widget *dcw = g_malloc0(sizeof(struct dsp_comp_widget));
    dcw->curve = comp_curve;
    dcw->input_snk = dsp_snk;
    dcw->output_src = dsp_src;
    card->dsp_comp_widgets = g_list_append(card->dsp_comp_widgets, dcw);

    // Collect compressor elements for presets
    struct compressor_elems *comp_elems = g_malloc0(sizeof(struct compressor_elems));

    // Sliders grid
    GtkWidget *slider_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(slider_grid), 5);
    gtk_grid_set_row_spacing(GTK_GRID(slider_grid), 3);
    gtk_box_append(GTK_BOX(comp_box), slider_grid);

    int row = 0;

    // Threshold
    name = g_strdup_printf("%sCompressor Threshold", prefix);
    elem = get_elem_by_name(elems, name);
    g_free(name);
    if (elem) {
      comp_elems->threshold = elem;
      w = gtk_label_new("Thresh");
      gtk_widget_set_halign(w, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 0, row, 1, 1);
      w = make_int_slider_with_curve(elem, " dB", 1, comp_curve, update_curve_threshold);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 1, row++, 1, 1);
    }

    // Ratio
    name = g_strdup_printf("%sCompressor Ratio", prefix);
    elem = get_elem_by_name(elems, name);
    g_free(name);
    if (elem) {
      comp_elems->ratio = elem;
      w = gtk_label_new("Ratio");
      gtk_widget_set_halign(w, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 0, row, 1, 1);
      w = make_int_slider_with_curve(elem, NULL, 2, comp_curve, update_curve_ratio);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 1, row++, 1, 1);
    }

    // Knee
    name = g_strdup_printf("%sCompressor Knee Width", prefix);
    elem = get_elem_by_name(elems, name);
    g_free(name);
    if (elem) {
      comp_elems->knee = elem;
      w = gtk_label_new("Knee");
      gtk_widget_set_halign(w, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 0, row, 1, 1);
      w = make_int_slider_with_curve(elem, " dB", 1, comp_curve, update_curve_knee);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 1, row++, 1, 1);
    }

    // Attack
    name = g_strdup_printf("%sCompressor Attack", prefix);
    elem = get_elem_by_name(elems, name);
    g_free(name);
    if (elem) {
      comp_elems->attack = elem;
      w = gtk_label_new("Attack");
      gtk_widget_set_halign(w, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 0, row, 1, 1);
      w = make_int_slider(elem, " ms", 1);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 1, row++, 1, 1);
    }

    // Release
    name = g_strdup_printf("%sCompressor Release", prefix);
    elem = get_elem_by_name(elems, name);
    g_free(name);
    if (elem) {
      comp_elems->release = elem;
      w = gtk_label_new("Release");
      gtk_widget_set_halign(w, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 0, row, 1, 1);
      w = make_int_slider(elem, " ms", 1);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 1, row++, 1, 1);
    }

    // Makeup
    name = g_strdup_printf("%sCompressor Makeup Gain", prefix);
    elem = get_elem_by_name(elems, name);
    g_free(name);
    if (elem) {
      comp_elems->makeup = elem;
      w = gtk_label_new("Makeup");
      gtk_widget_set_halign(w, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 0, row, 1, 1);
      w = make_int_slider_with_curve(elem, " dB", 1, comp_curve, update_curve_makeup);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 1, row++, 1, 1);
    }

    // Connect presets button now that elements are collected
    connect_compressor_presets_button(comp_presets_button, comp_elems);
  }

  // === PEQ Filter section ===
  name = g_strdup_printf("%sPEQ Filter Enable", prefix);
  struct alsa_elem *peq_enable = get_elem_by_name(elems, name);
  g_free(name);

  if (peq_enable) {
    GtkWidget *peq_presets_button;
    GtkWidget *peq_box = create_section_box(
      "Parametric EQ Filter", peq_enable,
      PRESET_TYPE_PEQ, &peq_presets_button
    );
    gtk_widget_set_hexpand(peq_box, TRUE);
    gtk_box_append(GTK_BOX(sections_box), peq_box);

    // Response widget
    GtkWidget *peq_response_widget = gtk_filter_response_new(3);
    peq_response = GTK_FILTER_RESPONSE(peq_response_widget);
    gtk_widget_set_hexpand(peq_response_widget, TRUE);
    gtk_box_append(GTK_BOX(peq_box), peq_response_widget);

    // Connect enable to response widget
    struct enable_switch_data *en_data = g_malloc0(
      sizeof(struct enable_switch_data)
    );
    en_data->response = peq_response;
    alsa_elem_add_callback(peq_enable, enable_switch_updated, en_data,
                           (GDestroyNotify)enable_switch_destroy);
    enable_switch_updated(peq_enable, en_data);

    // Filter bands grid
    GtkWidget *peq_grid = gtk_grid_new();
    gtk_widget_add_css_class(peq_grid, "filter-stage");
    gtk_widget_set_hexpand(peq_grid, TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(peq_grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(peq_grid), 3);
    gtk_box_append(GTK_BOX(peq_box), peq_grid);

    struct filter_response_stages *peq_stages = g_malloc0(
      sizeof(struct filter_response_stages)
    );

    int peq_row = 0;
    for (int i = 1; i <= 3; i++) {
      name = g_strdup_printf("%sPEQ Coefficients %d", prefix, i);
      elem = get_elem_by_name(elems, name);
      g_free(name);

      if (elem) {
        char band_label[16];
        snprintf(band_label, sizeof(band_label), "Band %d", i);
        struct filter_stage *stage;
        make_filter_stage(
          card, elem, i - 1, peq_response, band_label, BIQUAD_TYPE_PEAKING,
          "PEQ", channel, i, peq_grid, peq_row++, &stage
        );
        peq_stages->stages[i - 1] = stage;
        peq_stages->num_stages = i;
      }
    }

    // Connect presets button now that stages are created
    connect_presets_button(peq_presets_button, peq_stages);

    // Connect signals for drag and highlight updates
    g_signal_connect(peq_response, "filter-changed",
                     G_CALLBACK(response_filter_changed), peq_stages);
    g_signal_connect(peq_response, "highlight-changed",
                     G_CALLBACK(response_highlight_changed), peq_stages);
    g_object_weak_ref(G_OBJECT(peq_response_widget),
                      (GWeakNotify)filter_response_stages_destroy, peq_stages);
  }

  // Connect DSP enable switch to all visualization widgets
  if (dsp_enable) {
    struct dsp_enable_data *dsp_data = g_malloc0(sizeof(struct dsp_enable_data));
    dsp_data->precomp_response = precomp_response;
    dsp_data->comp_curve = comp_curve;
    dsp_data->peq_response = peq_response;
    alsa_elem_add_callback(dsp_enable, dsp_enable_updated, dsp_data,
                           (GDestroyNotify)dsp_enable_destroy);
    dsp_enable_updated(dsp_enable, dsp_data);
  }

  g_free(prefix);
}

GtkWidget *create_dsp_controls(struct alsa_card *card) {
  GtkWidget *top = gtk_frame_new(NULL);
  gtk_widget_add_css_class(top, "window-frame");

  GtkWidget *grid = gtk_grid_new();
  gtk_widget_add_css_class(grid, "window-content");
  gtk_widget_add_css_class(grid, "top-level-content");
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
  gtk_frame_set_child(GTK_FRAME(top), grid);

  int grid_y = 0;

  // add controls for each channel that has DSP controls
  for (int ch = 1; ch <= 2; ch++) {
    char *name = g_strdup_printf("Line In %d DSP Capture Switch", ch);
    struct alsa_elem *elem = get_elem_by_name(card->elems, name);
    g_free(name);

    if (elem)
      add_channel_controls(card, grid, &grid_y, ch);
  }

  return top;
}
