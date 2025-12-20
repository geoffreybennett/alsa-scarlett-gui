// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "biquad.h"
#include "compressor-curve.h"
#include "gtkhelper.h"
#include "peq-response.h"
#include "stringhelper.h"
#include "widget-boolean.h"
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

// Data for a filter stage
struct filter_stage {
  struct alsa_elem    *coeff_elem;
  struct biquad_params params;
  gboolean             enabled;
  GtkWidget           *enable_check;
  GtkWidget           *type_dropdown;
  GtkWidget           *freq_scale;
  GtkWidget           *freq_label;
  GtkWidget           *q_scale;
  GtkWidget           *q_label;
  GtkWidget           *gain_scale;
  GtkWidget           *gain_label;
  GtkWidget           *gain_box;
  GtkFilterResponse   *response;
  int                  band_index;
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

// Enable checkbox callback
static void filter_enable_toggled(GtkCheckButton *check, struct filter_stage *stage) {
  stage->enabled = gtk_check_button_get_active(check);
  filter_stage_update_coeffs(stage);
}

// Format frequency for display
static void format_freq_label(GtkWidget *label, double freq) {
  char text[32];
  if (freq >= 1000)
    snprintf(text, sizeof(text), "%.1f kHz", freq / 1000.0);
  else
    snprintf(text, sizeof(text), "%.0f Hz", freq);
  gtk_label_set_text(GTK_LABEL(label), text);
}

// Callbacks for filter stage controls
static void filter_type_changed(GtkDropDown *dropdown, GParamSpec *pspec,
                                struct filter_stage *stage) {
  int selected = gtk_drop_down_get_selected(dropdown);
  if (selected >= 0 && selected < BIQUAD_TYPE_COUNT) {
    stage->params.type = (BiquadFilterType)selected;
    filter_stage_update_gain_visibility(stage);
    filter_stage_update_coeffs(stage);
  }
}

static void filter_freq_changed(GtkRange *range, struct filter_stage *stage) {
  // Log scale: slider value 0-1000 maps to 20-20000 Hz
  double slider_val = gtk_range_get_value(range);
  double log_min = log10(20.0);
  double log_max = log10(20000.0);
  double log_freq = log_min + (slider_val / 1000.0) * (log_max - log_min);
  stage->params.freq = pow(10.0, log_freq);

  format_freq_label(stage->freq_label, stage->params.freq);
  filter_stage_update_coeffs(stage);
}

static void filter_q_changed(GtkRange *range, struct filter_stage *stage) {
  // Log scale for Q: slider 0-1000 maps to 0.1-10
  double slider_val = gtk_range_get_value(range);
  double log_min = log10(0.1);
  double log_max = log10(10.0);
  double log_q = log_min + (slider_val / 1000.0) * (log_max - log_min);
  stage->params.q = pow(10.0, log_q);

  char text[16];
  snprintf(text, sizeof(text), "%.2f", stage->params.q);
  gtk_label_set_text(GTK_LABEL(stage->q_label), text);
  filter_stage_update_coeffs(stage);
}

static void filter_gain_changed(GtkRange *range, struct filter_stage *stage) {
  stage->params.gain_db = gtk_range_get_value(range);

  char text[16];
  snprintf(text, sizeof(text), "%+.1f dB", stage->params.gain_db);
  gtk_label_set_text(GTK_LABEL(stage->gain_label), text);
  filter_stage_update_coeffs(stage);
}

// Convert frequency to slider value (0-1000)
static double freq_to_slider(double freq) {
  double log_min = log10(20.0);
  double log_max = log10(20000.0);
  double log_freq = log10(freq);
  return (log_freq - log_min) / (log_max - log_min) * 1000.0;
}

// Convert Q to slider value (0-1000)
static double q_to_slider(double q) {
  double log_min = log10(0.1);
  double log_max = log10(10.0);
  double log_q = log10(q);
  return (log_q - log_min) / (log_max - log_min) * 1000.0;
}

// Forward declarations for response_filter_changed callback
static void filter_stage_update_coeffs(struct filter_stage *stage);
static void format_freq_label(GtkWidget *label, double freq);
static void filter_freq_changed(GtkRange *range, struct filter_stage *stage);
static void filter_q_changed(GtkRange *range, struct filter_stage *stage);
static void filter_gain_changed(GtkRange *range, struct filter_stage *stage);

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

  // Update sliders (block signals to avoid feedback loop)
  g_signal_handlers_block_by_func(stage->freq_scale, filter_freq_changed, stage);
  g_signal_handlers_block_by_func(stage->q_scale, filter_q_changed, stage);
  g_signal_handlers_block_by_func(stage->gain_scale, filter_gain_changed, stage);

  gtk_range_set_value(GTK_RANGE(stage->freq_scale), freq_to_slider(params->freq));
  gtk_range_set_value(GTK_RANGE(stage->q_scale), q_to_slider(params->q));
  gtk_range_set_value(GTK_RANGE(stage->gain_scale), params->gain_db);

  g_signal_handlers_unblock_by_func(stage->freq_scale, filter_freq_changed, stage);
  g_signal_handlers_unblock_by_func(stage->q_scale, filter_q_changed, stage);
  g_signal_handlers_unblock_by_func(stage->gain_scale, filter_gain_changed, stage);

  // Update labels
  format_freq_label(stage->freq_label, params->freq);

  char text[16];
  snprintf(text, sizeof(text), "%.2f", params->q);
  gtk_label_set_text(GTK_LABEL(stage->q_label), text);

  snprintf(text, sizeof(text), "%+.1f dB", params->gain_db);
  gtk_label_set_text(GTK_LABEL(stage->gain_label), text);

  // Update hardware
  filter_stage_update_coeffs(stage);
}

static void filter_response_stages_destroy(struct filter_response_stages *data) {
  g_free(data);
}

static void filter_stage_destroy(struct filter_stage *stage) {
  g_free(stage);
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
}

static void filter_stage_leave(
  GtkEventControllerMotion *controller,
  struct filter_stage      *stage
) {
  if (stage->response)
    gtk_filter_response_set_highlight(stage->response, -1);
}

// Create controls for one filter stage
static GtkWidget *make_filter_stage(
  struct alsa_elem   *coeff_elem,
  int                 band_index,
  GtkFilterResponse  *response,
  const char         *label_text,
  BiquadFilterType    default_type,
  struct filter_stage **stage_out
) {
  struct filter_stage *stage = g_malloc0(sizeof(struct filter_stage));
  if (stage_out)
    *stage_out = stage;
  stage->coeff_elem = coeff_elem;
  stage->band_index = band_index;
  stage->response = response;

  // Default parameters
  stage->enabled = TRUE;
  stage->params.type = default_type;
  stage->params.freq = 1000.0;
  stage->params.q = 0.707;
  stage->params.gain_db = 0.0;

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

  // Enable checkbox with stage label
  stage->enable_check = gtk_check_button_new_with_label(label_text);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(stage->enable_check), TRUE);
  gtk_widget_set_size_request(stage->enable_check, 70, -1);
  gtk_box_append(GTK_BOX(box), stage->enable_check);

  // Filter type dropdown (NULL-terminated array for gtk_string_list_new)
  const char *type_names[BIQUAD_TYPE_COUNT + 1];
  for (int i = 0; i < BIQUAD_TYPE_COUNT; i++)
    type_names[i] = biquad_type_name(i);
  type_names[BIQUAD_TYPE_COUNT] = NULL;

  GtkStringList *type_list = gtk_string_list_new(type_names);
  stage->type_dropdown = gtk_drop_down_new(
    G_LIST_MODEL(type_list), NULL
  );
  gtk_drop_down_set_selected(
    GTK_DROP_DOWN(stage->type_dropdown), stage->params.type
  );
  gtk_widget_set_size_request(stage->type_dropdown, 90, -1);
  gtk_box_append(GTK_BOX(box), stage->type_dropdown);

  // Frequency slider
  GtkWidget *freq_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
  GtkWidget *freq_lbl = gtk_label_new("Freq");
  gtk_box_append(GTK_BOX(freq_box), freq_lbl);

  stage->freq_scale = gtk_scale_new_with_range(
    GTK_ORIENTATION_HORIZONTAL, 0, 1000, 1
  );
  gtk_scale_set_draw_value(GTK_SCALE(stage->freq_scale), FALSE);
  gtk_widget_set_size_request(stage->freq_scale, 100, -1);
  gtk_range_set_value(
    GTK_RANGE(stage->freq_scale), freq_to_slider(stage->params.freq)
  );
  gtk_box_append(GTK_BOX(freq_box), stage->freq_scale);

  stage->freq_label = gtk_label_new("");
  gtk_widget_set_size_request(stage->freq_label, 60, -1);
  format_freq_label(stage->freq_label, stage->params.freq);
  gtk_box_append(GTK_BOX(freq_box), stage->freq_label);
  gtk_box_append(GTK_BOX(box), freq_box);

  // Q slider
  GtkWidget *q_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
  GtkWidget *q_lbl = gtk_label_new("Q");
  gtk_box_append(GTK_BOX(q_box), q_lbl);

  stage->q_scale = gtk_scale_new_with_range(
    GTK_ORIENTATION_HORIZONTAL, 0, 1000, 1
  );
  gtk_scale_set_draw_value(GTK_SCALE(stage->q_scale), FALSE);
  gtk_widget_set_size_request(stage->q_scale, 80, -1);
  gtk_range_set_value(
    GTK_RANGE(stage->q_scale), q_to_slider(stage->params.q)
  );
  gtk_box_append(GTK_BOX(q_box), stage->q_scale);

  stage->q_label = gtk_label_new("0.71");
  gtk_widget_set_size_request(stage->q_label, 40, -1);
  gtk_box_append(GTK_BOX(q_box), stage->q_label);
  gtk_box_append(GTK_BOX(box), q_box);

  // Gain slider (shown only for peaking/shelving types)
  stage->gain_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
  GtkWidget *gain_lbl = gtk_label_new("Gain");
  gtk_box_append(GTK_BOX(stage->gain_box), gain_lbl);

  stage->gain_scale = gtk_scale_new_with_range(
    GTK_ORIENTATION_HORIZONTAL, -18, 18, 0.5
  );
  gtk_scale_set_draw_value(GTK_SCALE(stage->gain_scale), FALSE);
  gtk_widget_set_size_request(stage->gain_scale, 80, -1);
  gtk_range_set_value(GTK_RANGE(stage->gain_scale), stage->params.gain_db);
  gtk_box_append(GTK_BOX(stage->gain_box), stage->gain_scale);

  stage->gain_label = gtk_label_new("+0.0 dB");
  gtk_widget_set_size_request(stage->gain_label, 55, -1);
  gtk_box_append(GTK_BOX(stage->gain_box), stage->gain_label);
  gtk_box_append(GTK_BOX(box), stage->gain_box);

  // Update gain visibility
  filter_stage_update_gain_visibility(stage);

  // Connect signals
  g_signal_connect(
    stage->enable_check, "toggled",
    G_CALLBACK(filter_enable_toggled), stage
  );
  g_signal_connect(
    stage->type_dropdown, "notify::selected",
    G_CALLBACK(filter_type_changed), stage
  );
  g_signal_connect(
    stage->freq_scale, "value-changed",
    G_CALLBACK(filter_freq_changed), stage
  );
  g_signal_connect(
    stage->q_scale, "value-changed",
    G_CALLBACK(filter_q_changed), stage
  );
  g_signal_connect(
    stage->gain_scale, "value-changed",
    G_CALLBACK(filter_gain_changed), stage
  );

  // Hover detection for highlighting in the response graph
  GtkEventController *motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "enter", G_CALLBACK(filter_stage_enter), stage);
  g_signal_connect(motion, "leave", G_CALLBACK(filter_stage_leave), stage);
  gtk_widget_add_controller(box, motion);

  g_object_weak_ref(G_OBJECT(box), (GWeakNotify)filter_stage_destroy, stage);

  // Initial coefficient update
  filter_stage_update_coeffs(stage);

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

// Create a section box with header, enable, and content
static GtkWidget *create_section_box(
  const char       *title,
  struct alsa_elem *enable_elem
) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_margin_start(box, 5);
  gtk_widget_set_margin_end(box, 5);

  // Header with enable
  GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_box_append(GTK_BOX(box), header_box);

  GtkWidget *label = gtk_label_new(NULL);
  char *markup = g_strdup_printf("<b>%s</b>", title);
  gtk_label_set_markup(GTK_LABEL(label), markup);
  g_free(markup);
  gtk_box_append(GTK_BOX(header_box), label);

  if (enable_elem) {
    GtkWidget *enable = make_boolean_alsa_elem(enable_elem, "Off", "On");
    gtk_box_append(GTK_BOX(header_box), enable);
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

  // DSP header row with enable
  name = g_strdup_printf("%sDSP Capture Switch", prefix);
  struct alsa_elem *dsp_enable = get_elem_by_name(elems, name);
  g_free(name);

  GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_margin_top(header_box, *grid_y > 0 ? 15 : 0);
  gtk_grid_attach(GTK_GRID(grid), header_box, 0, (*grid_y)++, 1, 1);

  char *header = g_strdup_printf("DSP %d", channel);
  w = gtk_label_new(NULL);
  char *markup = g_strdup_printf("<b>%s</b>", header);
  gtk_label_set_markup(GTK_LABEL(w), markup);
  g_free(markup);
  g_free(header);
  gtk_box_append(GTK_BOX(header_box), w);

  if (dsp_enable) {
    w = make_boolean_alsa_elem(dsp_enable, "Off", "On");
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
    GtkFilterResponse *precomp_response =
      GTK_FILTER_RESPONSE(precomp_response_widget);
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
          elem, i - 1, precomp_response, stage_label, BIQUAD_TYPE_HIGHPASS,
          &stage
        );
        precomp_stages->stages[i - 1] = stage;
        precomp_stages->num_stages = i;
        gtk_box_append(GTK_BOX(precomp_box), w);
      }
    }

    // Connect signal for drag updates
    g_signal_connect(precomp_response, "filter-changed",
                     G_CALLBACK(response_filter_changed), precomp_stages);
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
    GtkCompressorCurve *curve = GTK_COMPRESSOR_CURVE(curve_widget);
    gtk_box_append(GTK_BOX(comp_box), curve_widget);

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
      w = make_int_slider_with_curve(elem, " dB", 1, curve, update_curve_threshold);
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
      w = make_int_slider_with_curve(elem, NULL, 2, curve, update_curve_ratio);
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
      w = make_int_slider_with_curve(elem, " dB", 1, curve, update_curve_knee);
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
      w = make_int_slider_with_curve(elem, " dB", 1, curve, update_curve_makeup);
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
    GtkFilterResponse *peq_response =
      GTK_FILTER_RESPONSE(peq_response_widget);
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
          elem, i - 1, peq_response, band_label, BIQUAD_TYPE_PEAKING,
          &stage
        );
        peq_stages->stages[i - 1] = stage;
        peq_stages->num_stages = i;
        gtk_box_append(GTK_BOX(peq_box), w);
      }
    }

    // Connect signal for drag updates
    g_signal_connect(peq_response, "filter-changed",
                     G_CALLBACK(response_filter_changed), peq_stages);
    g_object_weak_ref(G_OBJECT(peq_response_widget),
                      (GWeakNotify)filter_response_stages_destroy, peq_stages);
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
