// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "biquad.h"
#include "compressor-curve.h"
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
  GtkWidget           *box;
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
    if (data->stages[i] && data->stages[i]->box)
      gtk_widget_remove_css_class(data->stages[i]->box, "filter-stage-hover");
  }

  // Add highlight to the selected stage
  if (band >= 0 && band < data->num_stages &&
      data->stages[band] && data->stages[band]->box) {
    gtk_widget_add_css_class(data->stages[band]->box, "filter-stage-hover");
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

  // Save state
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

  GtkWidget *box = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
  gtk_widget_add_css_class(box, "filter-stage-hover");
}

static void filter_stage_leave(
  GtkEventControllerMotion *controller,
  struct filter_stage      *stage
) {
  if (stage->response)
    gtk_filter_response_set_highlight(stage->response, -1);

  GtkWidget *box = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
  gtk_widget_remove_css_class(box, "filter-stage-hover");
}

// Create controls for one filter stage
static GtkWidget *make_filter_stage(
  struct alsa_card   *card,
  struct alsa_elem   *coeff_elem,
  int                 band_index,
  GtkFilterResponse  *response,
  const char         *label_text,
  BiquadFilterType    default_type,
  const char         *filter_type,
  int                 channel,
  int                 stage_num,
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

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  stage->box = box;

  // Enable checkbox with stage label
  stage->enable_check = gtk_check_button_new_with_label(label_text);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(stage->enable_check), stage->enabled);
  gtk_widget_set_size_request(stage->enable_check, 70, -1);
  gtk_box_append(GTK_BOX(box), stage->enable_check);

  // Filter type dropdown with icons
  stage->type_dropdown = make_filter_type_dropdown(stage->params.type);
  gtk_box_append(GTK_BOX(box), stage->type_dropdown);

  // Frequency entry
  stage->freq_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  stage->freq_entry = gtk_entry_new();
  gtk_editable_set_max_width_chars(GTK_EDITABLE(stage->freq_entry), 5);
  gtk_entry_set_max_length(GTK_ENTRY(stage->freq_entry), 5);
  gtk_box_append(GTK_BOX(stage->freq_box), stage->freq_entry);
  gtk_box_append(GTK_BOX(stage->freq_box), gtk_label_new("Hz"));
  format_freq_entry(stage);
  gtk_box_append(GTK_BOX(box), stage->freq_box);

  // Q entry
  stage->q_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_box_append(GTK_BOX(stage->q_box), gtk_label_new("Q"));
  stage->q_entry = gtk_entry_new();
  gtk_editable_set_max_width_chars(GTK_EDITABLE(stage->q_entry), 4);
  gtk_entry_set_max_length(GTK_ENTRY(stage->q_entry), 4);
  format_q_entry(stage->q_entry, stage->params.q);
  gtk_box_append(GTK_BOX(stage->q_box), stage->q_entry);
  gtk_box_append(GTK_BOX(box), stage->q_box);

  // Gain entry (shown only for peaking/shelving types)
  stage->gain_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  stage->gain_entry = gtk_entry_new();
  gtk_editable_set_max_width_chars(GTK_EDITABLE(stage->gain_entry), 5);
  gtk_entry_set_max_length(GTK_ENTRY(stage->gain_entry), 5);
  format_gain_entry(stage->gain_entry, stage->params.gain_db);
  gtk_box_append(GTK_BOX(stage->gain_box), stage->gain_entry);
  gtk_box_append(GTK_BOX(stage->gain_box), gtk_label_new("dB"));
  gtk_box_append(GTK_BOX(box), stage->gain_box);

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

  // Hover detection for highlighting in the response graph
  GtkEventController *motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "enter", G_CALLBACK(filter_stage_enter), stage);
  g_signal_connect(motion, "leave", G_CALLBACK(filter_stage_leave), stage);
  gtk_widget_add_controller(box, motion);

  g_object_weak_ref(G_OBJECT(box), (GWeakNotify)filter_stage_destroy, stage);

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

  return box;
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

static void dsp_enable_destroy(struct dsp_enable_data *data) {
  g_free(data);
}

// Create a section box with header, enable, and content
static GtkWidget *create_section_box(
  const char       *title,
  struct alsa_elem *enable_elem
) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_margin_start(box, 5);
  gtk_widget_set_margin_end(box, 5);

  // Header with enable toggle button
  if (enable_elem) {
    GtkWidget *enable = make_boolean_alsa_elem(enable_elem, title, NULL);
    gtk_widget_add_css_class(enable, "dsp");
    gtk_box_append(GTK_BOX(box), enable);
  } else {
    GtkWidget *label = gtk_label_new(NULL);
    char *markup = g_strdup_printf("<b>%s</b>", title);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    gtk_box_append(GTK_BOX(box), label);
  }

  return box;
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

  // DSP header row with enable
  name = g_strdup_printf("%sDSP Capture Switch", prefix);
  struct alsa_elem *dsp_enable = get_elem_by_name(elems, name);
  g_free(name);

  GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_margin_top(header_box, *grid_y > 0 ? 15 : 0);
  gtk_grid_attach(GTK_GRID(grid), header_box, 0, (*grid_y)++, 1, 1);

  if (dsp_enable) {
    char *header = g_strdup_printf("DSP %d", channel);
    w = make_boolean_alsa_elem(dsp_enable, header, NULL);
    g_free(header);
    gtk_widget_add_css_class(w, "dsp");
    gtk_box_append(GTK_BOX(header_box), w);
  } else {
    char *header = g_strdup_printf("DSP %d", channel);
    w = gtk_label_new(NULL);
    char *markup = g_strdup_printf("<b>%s</b>", header);
    gtk_label_set_markup(GTK_LABEL(w), markup);
    g_free(markup);
    g_free(header);
    gtk_box_append(GTK_BOX(header_box), w);
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
    GtkWidget *precomp_box = create_section_box("Pre-Compressor Filter", precomp_enable);
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

    // Filter stages
    struct filter_response_stages *precomp_stages = g_malloc0(
      sizeof(struct filter_response_stages)
    );

    for (int i = 1; i <= 2; i++) {
      name = g_strdup_printf("%sPre-Comp Coefficients %d", prefix, i);
      elem = get_elem_by_name(elems, name);
      g_free(name);

      if (elem) {
        char stage_label[16];
        snprintf(stage_label, sizeof(stage_label), "Stage %d", i);
        struct filter_stage *stage;
        w = make_filter_stage(
          card, elem, i - 1, precomp_response, stage_label, BIQUAD_TYPE_HIGHPASS,
          "Pre-Comp", channel, i, &stage
        );
        precomp_stages->stages[i - 1] = stage;
        precomp_stages->num_stages = i;
        gtk_box_append(GTK_BOX(precomp_box), w);
      }
    }

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
    GtkWidget *comp_box = create_section_box("Compressor", comp_enable);
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

    // Find routing elements for level display
    struct routing_snk *input_snk = NULL;
    struct routing_src *output_src = NULL;

    // Find DSP Input sink for this channel
    for (int i = 0; i < card->routing_snks->len; i++) {
      struct routing_snk *snk = &g_array_index(
        card->routing_snks, struct routing_snk, i
      );
      if (snk->elem &&
          snk->elem->port_category == PC_DSP &&
          snk->elem->port_num == channel - 1) {
        input_snk = snk;
        break;
      }
    }

    // Find DSP Output source for this channel
    for (int i = 0; i < card->routing_srcs->len; i++) {
      struct routing_src *src = &g_array_index(
        card->routing_srcs, struct routing_src, i
      );
      if (src->port_category == PC_DSP && src->port_num == channel - 1) {
        output_src = src;
        break;
      }
    }

    // Store for level updates
    struct dsp_comp_widget *dcw = g_malloc0(sizeof(struct dsp_comp_widget));
    dcw->curve = comp_curve;
    dcw->input_snk = input_snk;
    dcw->output_src = output_src;
    card->dsp_comp_widgets = g_list_append(card->dsp_comp_widgets, dcw);

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
      w = gtk_label_new("Makeup");
      gtk_widget_set_halign(w, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 0, row, 1, 1);
      w = make_int_slider_with_curve(elem, " dB", 1, comp_curve, update_curve_makeup);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 1, row++, 1, 1);
    }
  }

  // === PEQ Filter section ===
  name = g_strdup_printf("%sPEQ Filter Enable", prefix);
  struct alsa_elem *peq_enable = get_elem_by_name(elems, name);
  g_free(name);

  if (peq_enable) {
    GtkWidget *peq_box = create_section_box("Parametric EQ Filter", peq_enable);
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

    // Filter bands
    struct filter_response_stages *peq_stages = g_malloc0(
      sizeof(struct filter_response_stages)
    );

    for (int i = 1; i <= 3; i++) {
      name = g_strdup_printf("%sPEQ Coefficients %d", prefix, i);
      elem = get_elem_by_name(elems, name);
      g_free(name);

      if (elem) {
        char band_label[16];
        snprintf(band_label, sizeof(band_label), "Band %d", i);
        struct filter_stage *stage;
        w = make_filter_stage(
          card, elem, i - 1, peq_response, band_label, BIQUAD_TYPE_PEAKING,
          "PEQ", channel, i, &stage
        );
        peq_stages->stages[i - 1] = stage;
        peq_stages->num_stages = i;
        gtk_box_append(GTK_BOX(peq_box), w);
      }
    }

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
