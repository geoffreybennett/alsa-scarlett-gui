// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compressor-curve.h"
#include "gtkhelper.h"
#include "stringhelper.h"
#include "widget-boolean.h"
#include "window-dsp.h"

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

  g_signal_connect(data->scale, "value-changed",
                   G_CALLBACK(int_slider_changed), data);
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

  // channel header
  char *header = g_strdup_printf("Line In %d", channel);
  w = gtk_label_new(NULL);
  char *markup = g_strdup_printf("<b>%s</b>", header);
  gtk_label_set_markup(GTK_LABEL(w), markup);
  g_free(markup);
  g_free(header);
  gtk_widget_set_halign(w, GTK_ALIGN_START);
  gtk_widget_set_margin_top(w, *grid_y > 0 ? 15 : 0);
  gtk_grid_attach(GTK_GRID(grid), w, 0, (*grid_y)++, 3, 1);

  // DSP Capture Switch
  char *name = g_strdup_printf("%sDSP Capture Switch", prefix);
  struct alsa_elem *elem = get_elem_by_name(elems, name);
  g_free(name);
  if (elem) {
    w = gtk_label_new("DSP");
    gtk_widget_set_halign(w, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y, 1, 1);

    w = make_boolean_alsa_elem(elem, "Off", "On");
    gtk_grid_attach(GTK_GRID(grid), w, 1, (*grid_y)++, 1, 1);
  }

  // Pre-Comp Filter Enable
  name = g_strdup_printf("%sPre-Comp Filter Enable", prefix);
  elem = get_elem_by_name(elems, name);
  g_free(name);
  if (elem) {
    w = gtk_label_new("Pre-Comp Filter");
    gtk_widget_set_halign(w, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y, 1, 1);

    w = make_boolean_alsa_elem(elem, "Off", "On");
    gtk_grid_attach(GTK_GRID(grid), w, 1, (*grid_y)++, 1, 1);
  }

  // Compressor section with curve display
  name = g_strdup_printf("%sCompressor Enable", prefix);
  struct alsa_elem *comp_enable = get_elem_by_name(elems, name);
  g_free(name);

  if (comp_enable) {
    // Compressor header and enable
    w = gtk_label_new("Compressor");
    gtk_widget_set_halign(w, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y, 1, 1);

    w = make_boolean_alsa_elem(comp_enable, "Off", "On");
    gtk_grid_attach(GTK_GRID(grid), w, 1, (*grid_y)++, 1, 1);

    // Create curve widget
    GtkWidget *curve_widget = gtk_compressor_curve_new();
    GtkCompressorCurve *curve = GTK_COMPRESSOR_CURVE(curve_widget);

    // Create a horizontal box to hold curve and sliders
    GtkWidget *comp_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_grid_attach(GTK_GRID(grid), comp_box, 0, *grid_y, 3, 1);

    // Add curve to the left
    gtk_box_append(GTK_BOX(comp_box), curve_widget);

    // Create a grid for the sliders on the right
    GtkWidget *slider_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(slider_grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(slider_grid), 5);
    gtk_widget_set_hexpand(slider_grid, TRUE);
    gtk_box_append(GTK_BOX(comp_box), slider_grid);

    int slider_row = 0;

    // Threshold
    name = g_strdup_printf("%sCompressor Threshold", prefix);
    elem = get_elem_by_name(elems, name);
    g_free(name);
    if (elem) {
      w = gtk_label_new("Threshold");
      gtk_widget_set_halign(w, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 0, slider_row, 1, 1);

      w = make_int_slider_with_curve(elem, " dB", 1, curve, update_curve_threshold);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 1, slider_row++, 1, 1);
    }

    // Ratio
    name = g_strdup_printf("%sCompressor Ratio", prefix);
    elem = get_elem_by_name(elems, name);
    g_free(name);
    if (elem) {
      w = gtk_label_new("Ratio");
      gtk_widget_set_halign(w, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 0, slider_row, 1, 1);

      w = make_int_slider_with_curve(elem, NULL, 2, curve, update_curve_ratio);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 1, slider_row++, 1, 1);
    }

    // Knee Width
    name = g_strdup_printf("%sCompressor Knee Width", prefix);
    elem = get_elem_by_name(elems, name);
    g_free(name);
    if (elem) {
      w = gtk_label_new("Knee Width");
      gtk_widget_set_halign(w, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 0, slider_row, 1, 1);

      w = make_int_slider_with_curve(elem, " dB", 1, curve, update_curve_knee);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 1, slider_row++, 1, 1);
    }

    // Attack (no curve update)
    name = g_strdup_printf("%sCompressor Attack", prefix);
    elem = get_elem_by_name(elems, name);
    g_free(name);
    if (elem) {
      w = gtk_label_new("Attack");
      gtk_widget_set_halign(w, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 0, slider_row, 1, 1);

      w = make_int_slider(elem, " ms", 1);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 1, slider_row++, 1, 1);
    }

    // Release (no curve update)
    name = g_strdup_printf("%sCompressor Release", prefix);
    elem = get_elem_by_name(elems, name);
    g_free(name);
    if (elem) {
      w = gtk_label_new("Release");
      gtk_widget_set_halign(w, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 0, slider_row, 1, 1);

      w = make_int_slider(elem, " ms", 1);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 1, slider_row++, 1, 1);
    }

    // Makeup Gain
    name = g_strdup_printf("%sCompressor Makeup Gain", prefix);
    elem = get_elem_by_name(elems, name);
    g_free(name);
    if (elem) {
      w = gtk_label_new("Makeup");
      gtk_widget_set_halign(w, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 0, slider_row, 1, 1);

      w = make_int_slider_with_curve(elem, " dB", 1, curve, update_curve_makeup);
      gtk_grid_attach(GTK_GRID(slider_grid), w, 1, slider_row++, 1, 1);
    }

    (*grid_y)++;
  }

  // PEQ Filter Enable
  name = g_strdup_printf("%sPEQ Filter Enable", prefix);
  elem = get_elem_by_name(elems, name);
  g_free(name);
  if (elem) {
    w = gtk_label_new("PEQ Filter");
    gtk_widget_set_halign(w, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y, 1, 1);

    w = make_boolean_alsa_elem(elem, "Off", "On");
    gtk_grid_attach(GTK_GRID(grid), w, 1, (*grid_y)++, 1, 1);
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
