// SPDX-FileCopyrightText: 2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <stdio.h>
#include "compressor-curve.h"

#define CURVE_SIZE 150
#define LABEL_MARGIN 18
#define CURVE_PADDING 3
#define DB_MIN -60.0
#define DB_MAX 0.0

struct _GtkCompressorCurve {
  GtkWidget parent_instance;

  int threshold;    // -40 to 0 dB
  int ratio;        // 2-100 (divide by 2 for actual ratio)
  int knee_width;   // 0-10 dB
  int makeup_gain;  // 0-24 dB
};

G_DEFINE_TYPE(GtkCompressorCurve, gtk_compressor_curve, GTK_TYPE_WIDGET)

// Calculate compressor output for a given input (both in dB)
static double calculate_output(GtkCompressorCurve *curve, double input) {
  double threshold = curve->threshold;
  double ratio = curve->ratio / 2.0;
  double knee = curve->knee_width;
  double makeup = curve->makeup_gain;

  double output;

  if (knee <= 0) {
    // Hard knee: no soft transition
    if (input < threshold)
      output = input;
    else
      output = threshold + (input - threshold) / ratio;
  } else if (input < threshold - knee / 2) {
    // Below knee region: unity gain
    output = input;
  } else if (input > threshold + knee / 2) {
    // Above knee region: full compression
    output = threshold + (input - threshold) / ratio;
  } else {
    // In knee region: smooth transition
    double x = input - threshold + knee / 2;
    double compression = (1.0 - 1.0 / ratio) * x * x / (2.0 * knee);
    output = input - compression;
  }

  return output + makeup;
}

// Graph area boundaries (accounting for label margin)
#define GRAPH_LEFT   LABEL_MARGIN
#define GRAPH_RIGHT  (CURVE_SIZE - CURVE_PADDING)
#define GRAPH_TOP    CURVE_PADDING
#define GRAPH_BOTTOM (CURVE_SIZE - LABEL_MARGIN)
#define GRAPH_WIDTH  (GRAPH_RIGHT - GRAPH_LEFT)
#define GRAPH_HEIGHT (GRAPH_BOTTOM - GRAPH_TOP)

// Convert dB value to pixel coordinate
static double db_to_x(double db) {
  return GRAPH_LEFT + (db - DB_MIN) / (DB_MAX - DB_MIN) * GRAPH_WIDTH;
}

static double db_to_y(double db) {
  return GRAPH_BOTTOM - (db - DB_MIN) / (DB_MAX - DB_MIN) * GRAPH_HEIGHT;
}

static void curve_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  GtkCompressorCurve *curve = GTK_COMPRESSOR_CURVE(widget);

  int width = gtk_widget_get_width(widget);
  int height = gtk_widget_get_height(widget);
  int size = MIN(width, height);

  cairo_t *cr = gtk_snapshot_append_cairo(
    snapshot,
    &GRAPHENE_RECT_INIT(0, 0, width, height)
  );

  // Center the square drawing area
  double offset_x = (width - size) / 2.0;
  double offset_y = (height - size) / 2.0;
  cairo_translate(cr, offset_x, offset_y);
  double scale = size / (double)CURVE_SIZE;
  cairo_scale(cr, scale, scale);

  // Background (graph area only)
  cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
  cairo_rectangle(cr, GRAPH_LEFT, GRAPH_TOP, GRAPH_WIDTH, GRAPH_HEIGHT);
  cairo_fill(cr);

  // Grid lines (every 20 dB)
  cairo_set_source_rgba(cr, 1, 1, 1, 0.15);
  cairo_set_line_width(cr, 0.5);
  for (double db = -40; db <= 0; db += 20) {
    // Vertical
    double x = db_to_x(db);
    cairo_move_to(cr, x, GRAPH_TOP);
    cairo_line_to(cr, x, GRAPH_BOTTOM);
    // Horizontal
    double y = db_to_y(db);
    cairo_move_to(cr, GRAPH_LEFT, y);
    cairo_line_to(cr, GRAPH_RIGHT, y);
  }
  cairo_stroke(cr);

  // Axis labels
  cairo_set_source_rgba(cr, 1, 1, 1, 0.6);
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 8);

  for (double db = -60; db <= 0; db += 20) {
    char label[8];
    snprintf(label, sizeof(label), "%.0f", db);

    cairo_text_extents_t extents;
    cairo_text_extents(cr, label, &extents);

    // Y-axis labels (left side)
    double y = db_to_y(db);
    cairo_move_to(cr, GRAPH_LEFT - extents.width - 3,
                  y + extents.height / 2);
    cairo_show_text(cr, label);

    // X-axis labels (bottom)
    double x = db_to_x(db);
    cairo_move_to(cr, x - extents.width / 2,
                  GRAPH_BOTTOM + extents.height + 3);
    cairo_show_text(cr, label);
  }

  // 1:1 reference line (dotted)
  cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
  cairo_set_line_width(cr, 1);
  double dashes[] = {3.0, 3.0};
  cairo_set_dash(cr, dashes, 2, 0);
  cairo_move_to(cr, db_to_x(DB_MIN), db_to_y(DB_MIN));
  cairo_line_to(cr, db_to_x(DB_MAX), db_to_y(DB_MAX));
  cairo_stroke(cr);
  cairo_set_dash(cr, NULL, 0, 0);

  // Threshold indicator (vertical line)
  cairo_set_source_rgba(cr, 1, 0.8, 0.2, 0.4);
  cairo_set_line_width(cr, 1);
  double thresh_x = db_to_x(curve->threshold);
  cairo_move_to(cr, thresh_x, GRAPH_TOP);
  cairo_line_to(cr, thresh_x, GRAPH_BOTTOM);
  cairo_stroke(cr);

  // Transfer curve - draw in two passes: white for normal, red for clipped
  cairo_set_line_width(cr, 2);

  // First pass: white portion (output <= 0dB)
  cairo_set_source_rgb(cr, 1, 1, 1);
  gboolean first = TRUE;
  for (double in_db = DB_MIN; in_db <= DB_MAX; in_db += 0.5) {
    double out_db = calculate_output(curve, in_db);

    if (out_db > DB_MAX) {
      // End white segment here
      if (!first)
        cairo_stroke(cr);
      first = TRUE;
      continue;
    }

    if (out_db < DB_MIN)
      out_db = DB_MIN;

    double x = db_to_x(in_db);
    double y = db_to_y(out_db);

    if (first) {
      cairo_move_to(cr, x, y);
      first = FALSE;
    } else {
      cairo_line_to(cr, x, y);
    }
  }
  if (!first)
    cairo_stroke(cr);

  // Second pass: red portion (output > 0dB, clamped to 0dB line)
  cairo_set_source_rgb(cr, 1, 0.3, 0.3);
  first = TRUE;
  for (double in_db = DB_MIN; in_db <= DB_MAX; in_db += 0.5) {
    double out_db = calculate_output(curve, in_db);

    if (out_db <= DB_MAX) {
      // End red segment here
      if (!first)
        cairo_stroke(cr);
      first = TRUE;
      continue;
    }

    double x = db_to_x(in_db);
    double y = db_to_y(DB_MAX);  // Clamp to 0dB

    if (first) {
      cairo_move_to(cr, x, y);
      first = FALSE;
    } else {
      cairo_line_to(cr, x, y);
    }
  }
  if (!first)
    cairo_stroke(cr);

  cairo_destroy(cr);
}

static void curve_measure(
  GtkWidget      *widget,
  GtkOrientation  orientation,
  int             for_size,
  int            *minimum,
  int            *natural,
  int            *minimum_baseline,
  int            *natural_baseline
) {
  *minimum = CURVE_SIZE;
  *natural = CURVE_SIZE;
  *minimum_baseline = -1;
  *natural_baseline = -1;
}

static void gtk_compressor_curve_class_init(GtkCompressorCurveClass *klass) {
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  widget_class->snapshot = curve_snapshot;
  widget_class->measure = curve_measure;
}

static void gtk_compressor_curve_init(GtkCompressorCurve *curve) {
  // Default values
  curve->threshold = -22;
  curve->ratio = 8;      // 4:1
  curve->knee_width = 3;
  curve->makeup_gain = 5;
}

GtkWidget *gtk_compressor_curve_new(void) {
  return g_object_new(GTK_TYPE_COMPRESSOR_CURVE, NULL);
}

void gtk_compressor_curve_set_threshold(GtkCompressorCurve *curve, int threshold) {
  if (curve->threshold != threshold) {
    curve->threshold = threshold;
    gtk_widget_queue_draw(GTK_WIDGET(curve));
  }
}

void gtk_compressor_curve_set_ratio(GtkCompressorCurve *curve, int ratio) {
  if (curve->ratio != ratio) {
    curve->ratio = ratio;
    gtk_widget_queue_draw(GTK_WIDGET(curve));
  }
}

void gtk_compressor_curve_set_knee_width(GtkCompressorCurve *curve, int knee_width) {
  if (curve->knee_width != knee_width) {
    curve->knee_width = knee_width;
    gtk_widget_queue_draw(GTK_WIDGET(curve));
  }
}

void gtk_compressor_curve_set_makeup_gain(GtkCompressorCurve *curve, int makeup_gain) {
  if (curve->makeup_gain != makeup_gain) {
    curve->makeup_gain = makeup_gain;
    gtk_widget_queue_draw(GTK_WIDGET(curve));
  }
}
