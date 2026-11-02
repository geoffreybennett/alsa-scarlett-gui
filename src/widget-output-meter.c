// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <stdio.h>
#include <pango/pangocairo.h>

#include "widget-output-meter.h"

#define DB_MIN -60.0
#define DB_MAX 0.0
#define DB_CLIP -0.1
#define DECAY_RATE_DB_PER_SEC 36.0  // Broadcast fall rate
#define PEAK_HOLD_MS 5000           // Peak hold duration (5.0s)
#define CLIP_HOLD_MS 10000          // Clip indicator hold duration (10.0s)

struct _GtkOutputMeter {
  GtkWidget parent_instance;

  double left_level_db;
  double right_level_db;

  // Ballistics (decay)
  double left_display_db;
  double right_display_db;

  // Peak hold
  double left_peak_db;
  double right_peak_db;
  gint64 left_peak_time;
  gint64 right_peak_time;

  // Clip state
  gboolean left_clipped;
  gboolean right_clipped;
  gint64 left_clip_time;
  gint64 right_clip_time;

  gint64 last_update_time;

  PangoLayout *scale_layout;
  PangoLayout *peak_layout;
};

G_DEFINE_TYPE(GtkOutputMeter, gtk_output_meter, GTK_TYPE_WIDGET)

// Uniform visual spacing: each step occupies exactly 12.5% (1/8) of the bar height
static const struct {
  double db;
  double frac;
} scale_points[] = {
  {   0.0, 1.000 },
  {  -6.0, 0.875 },
  { -12.0, 0.750 },
  { -18.0, 0.625 },
  { -24.0, 0.500 },
  { -30.0, 0.375 },
  { -36.0, 0.250 },
  { -48.0, 0.125 },
  { -60.0, 0.000 }
};
#define NUM_SCALE_POINTS (sizeof(scale_points) / sizeof(scale_points[0]))

static double db_to_norm(double db) {
  if (db >= 0.0)
    return 1.0;
  if (db <= DB_MIN || isnan(db) || isinf(db))
    return 0.0;

  for (size_t i = 0; i < NUM_SCALE_POINTS - 1; i++) {
    if (db <= scale_points[i].db && db >= scale_points[i + 1].db) {
      double db_range = scale_points[i].db - scale_points[i + 1].db;
      double frac_range = scale_points[i].frac - scale_points[i + 1].frac;
      double t = (db - scale_points[i + 1].db) / db_range;
      return scale_points[i + 1].frac + t * frac_range;
    }
  }
  return 0.0;
}

static void draw_rounded_rect(
  cairo_t *cr,
  double   x,
  double   y,
  double   w,
  double   h,
  double   r
) {
  if (w < 2 * r)
    r = w / 2;
  if (h < 2 * r)
    r = h / 2;

  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
  cairo_close_path(cr);
}

static void output_meter_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  GtkOutputMeter *meter = GTK_OUTPUT_METER(widget);

  int width = gtk_widget_get_width(widget);
  int height = gtk_widget_get_height(widget);

  if (width < 20 || height < 30)
    return;

  cairo_t *cr = gtk_snapshot_append_cairo(
    snapshot,
    &GRAPHENE_RECT_INIT(0, 0, width, height)
  );

  // Background frame
  draw_rounded_rect(cr, 1.0, 1.0, width - 2.0, height - 2.0, 6.0);
  cairo_set_source_rgb(cr, 0.07, 0.07, 0.08); // #121214
  cairo_fill_preserve(cr);
  cairo_set_source_rgb(cr, 0.16, 0.16, 0.19); // #292930
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // Margins and dimensions (increased bottom_margin so -60 has comfortable space)
  double top_margin = 10.0;
  double bottom_margin = 16.0;
  double header_h = 48.0;

  double bar_top_y = top_margin + header_h;
  double bar_bottom_y = height - bottom_margin;
  double bar_h = bar_bottom_y - bar_top_y;

  if (bar_h < 10.0) {
    cairo_destroy(cr);
    return;
  }

  // Bar and scale dimensions
  double bar_w = 11.0;
  double scale_w = 34.0;
  double total_w = bar_w * 2.0 + scale_w + 14.0;

  double cx = width / 2.0;
  double left_bar_x = floor(cx - total_w / 2.0);
  double scale_x = left_bar_x + bar_w + 7.0;
  double right_bar_x = scale_x + scale_w + 7.0;

  // Header: Clean peak readout centered at top with generous separation from 0 dB
  double max_peak = fmax(meter->left_peak_db, meter->right_peak_db);
  char peak_text[24];
  if (meter->left_clipped || meter->right_clipped) {
    snprintf(peak_text, sizeof(peak_text), "CLIP");
    cairo_set_source_rgb(cr, 0.88, 0.09, 0.15); // Vibrant red for CLIP
  } else if (max_peak <= DB_MIN || isnan(max_peak) || isinf(max_peak)) {
    snprintf(peak_text, sizeof(peak_text), "--");
    cairo_set_source_rgb(cr, 0.50, 0.51, 0.55);
  } else {
    snprintf(peak_text, sizeof(peak_text), "%.1f", max_peak);
    cairo_set_source_rgb(cr, 0.88, 0.89, 0.92);
  }

  pango_layout_set_text(meter->peak_layout, peak_text, -1);
  int tw, th;
  pango_layout_get_pixel_size(meter->peak_layout, &tw, &th);
  double peak_y = floor(top_margin + 4.0);
  cairo_move_to(cr, floor(cx - tw / 2.0), peak_y);
  pango_cairo_show_layout(cr, meter->peak_layout);

  // Gradient pattern for meter bars matching the uniform scale
  cairo_pattern_t *pat = cairo_pattern_create_linear(
    0, bar_bottom_y, 0, bar_top_y
  );
  cairo_pattern_add_color_stop_rgb(pat, 0.000, 0.024, 0.753, 0.412); // #06c069 Green (-60 dB)
  cairo_pattern_add_color_stop_rgb(pat, 0.500, 0.024, 0.753, 0.412); // -24 dB Green
  cairo_pattern_add_color_stop_rgb(pat, 0.625, 0.267, 0.851, 0.518); // -18 dB Light Green
  cairo_pattern_add_color_stop_rgb(pat, 0.750, 0.961, 0.651, 0.137); // -12 dB Amber
  cairo_pattern_add_color_stop_rgb(pat, 0.875, 1.000, 0.478, 0.000); //  -6 dB Orange
  cairo_pattern_add_color_stop_rgb(pat, 0.950, 0.882, 0.090, 0.153); //  -2 dB Red
  cairo_pattern_add_color_stop_rgb(pat, 1.000, 0.882, 0.090, 0.153); //   0 dB Red

  // Draw Left and Right Meter Bars
  double bars_x[2] = { left_bar_x, right_bar_x };
  double levels_norm[2] = {
    db_to_norm(meter->left_display_db),
    db_to_norm(meter->right_display_db)
  };
  double peaks_norm[2] = {
    db_to_norm(meter->left_peak_db),
    db_to_norm(meter->right_peak_db)
  };

  for (int ch = 0; ch < 2; ch++) {
    double bx = bars_x[ch];

    // Track background
    draw_rounded_rect(cr, bx, bar_top_y, bar_w, bar_h, 3.0);
    cairo_set_source_rgb(cr, 0.09, 0.10, 0.12);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.17, 0.18, 0.21);
    cairo_set_line_width(cr, 0.8);
    cairo_stroke(cr);

    // Fill level
    double norm = levels_norm[ch];
    if (norm > 0.001) {
      double fill_h = norm * bar_h;
      double fill_y = bar_bottom_y - fill_h;

      cairo_save(cr);
      draw_rounded_rect(cr, bx, bar_top_y, bar_w, bar_h, 3.0);
      cairo_clip(cr);

      cairo_rectangle(cr, bx, fill_y, bar_w, fill_h);
      cairo_set_source(cr, pat);
      cairo_fill(cr);

      cairo_restore(cr);
    }

    // Peak hold line
    double pnorm = peaks_norm[ch];
    if (pnorm > 0.01) {
      double py = bar_bottom_y - pnorm * bar_h;

      cairo_save(cr);
      draw_rounded_rect(cr, bx, bar_top_y, bar_w, bar_h, 3.0);
      cairo_clip(cr);

      cairo_set_line_width(cr, 1.5);
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
      cairo_move_to(cr, bx, py);
      cairo_line_to(cr, bx + bar_w, py);
      cairo_stroke(cr);

      cairo_restore(cr);
    }
  }

  cairo_pattern_destroy(pat);

  // Center dB Scale ticks and labels - completely uniform 12.5% step distribution
  for (size_t i = 0; i < NUM_SCALE_POINTS; i++) {
    double y = bar_bottom_y - scale_points[i].frac * bar_h;

    // Ticks
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgb(cr, 0.28, 0.29, 0.33);

    // Left tick pointing towards left bar
    cairo_move_to(cr, scale_x, y);
    cairo_line_to(cr, scale_x + 2.5, y);
    cairo_stroke(cr);

    // Right tick pointing towards right bar
    cairo_move_to(cr, scale_x + scale_w - 2.5, y);
    cairo_line_to(cr, scale_x + scale_w, y);
    cairo_stroke(cr);

    // Label
    char lbl[12];
    snprintf(lbl, sizeof(lbl), "%d", (int)scale_points[i].db);
    pango_layout_set_text(meter->scale_layout, lbl, -1);
    pango_layout_get_pixel_size(meter->scale_layout, &tw, &th);

    if (scale_points[i].db == 0.0)
      cairo_set_source_rgb(cr, 0.88, 0.88, 0.90);
    else
      cairo_set_source_rgb(cr, 0.50, 0.51, 0.56);

    cairo_move_to(cr, scale_x + (scale_w - tw) / 2.0, y - th / 2.0);
    pango_cairo_show_layout(cr, meter->scale_layout);
  }

  cairo_destroy(cr);
}

static void output_meter_measure(
  GtkWidget      *widget,
  GtkOrientation  orientation,
  int             for_size,
  int            *minimum,
  int            *natural,
  int            *minimum_baseline,
  int            *natural_baseline
) {
  if (orientation == GTK_ORIENTATION_HORIZONTAL) {
    *minimum = 70;
    *natural = 100;
  } else {
    *minimum = 140;
    *natural = 200;
  }
  *minimum_baseline = -1;
  *natural_baseline = -1;
}

static void gtk_output_meter_dispose(GObject *object) {
  GtkOutputMeter *meter = GTK_OUTPUT_METER(object);
  g_clear_object(&meter->scale_layout);
  g_clear_object(&meter->peak_layout);
  G_OBJECT_CLASS(gtk_output_meter_parent_class)->dispose(object);
}

static void gtk_output_meter_class_init(GtkOutputMeterClass *klass) {
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  widget_class->snapshot = output_meter_snapshot;
  widget_class->measure = output_meter_measure;
  gobject_class->dispose = gtk_output_meter_dispose;
}

static void gtk_output_meter_init(GtkOutputMeter *meter) {
  meter->left_level_db = DB_MIN;
  meter->right_level_db = DB_MIN;
  meter->left_display_db = DB_MIN;
  meter->right_display_db = DB_MIN;
  meter->left_peak_db = DB_MIN;
  meter->right_peak_db = DB_MIN;
  meter->left_clipped = FALSE;
  meter->right_clipped = FALSE;
  meter->last_update_time = 0;

  meter->scale_layout = gtk_widget_create_pango_layout(GTK_WIDGET(meter), NULL);
  PangoFontDescription *desc = pango_font_description_from_string("Sans 7.5");
  pango_layout_set_font_description(meter->scale_layout, desc);
  pango_font_description_free(desc);

  meter->peak_layout = gtk_widget_create_pango_layout(GTK_WIDGET(meter), NULL);
  desc = pango_font_description_from_string("Sans Bold 12");
  pango_layout_set_font_description(meter->peak_layout, desc);
  pango_font_description_free(desc);

  GtkGesture *click = gtk_gesture_click_new();
  g_signal_connect_swapped(
    click, "pressed", G_CALLBACK(gtk_output_meter_reset_peaks), meter
  );
  gtk_widget_add_controller(GTK_WIDGET(meter), GTK_EVENT_CONTROLLER(click));

  gtk_widget_set_tooltip_text(
    GTK_WIDGET(meter),
    "Analogue Outputs Level Meter (Click to reset peak hold)"
  );
}

GtkWidget *gtk_output_meter_new(void) {
  return g_object_new(GTK_TYPE_OUTPUT_METER, NULL);
}

void gtk_output_meter_set_levels(
  GtkOutputMeter *meter,
  double          left_db,
  double          right_db
) {
  gint64 now = g_get_monotonic_time();
  double dt = 0.05;

  if (meter->last_update_time > 0) {
    dt = (now - meter->last_update_time) / 1000000.0;
    if (dt <= 0.0 || dt > 1.0)
      dt = 0.05;
  }
  meter->last_update_time = now;

  meter->left_level_db = left_db;
  meter->right_level_db = right_db;

  // Ballistics - Left
  if (left_db > meter->left_display_db) {
    meter->left_display_db = left_db;
  } else {
    meter->left_display_db -= DECAY_RATE_DB_PER_SEC * dt;
    if (meter->left_display_db < left_db)
      meter->left_display_db = left_db;
    if (meter->left_display_db < DB_MIN)
      meter->left_display_db = DB_MIN;
  }

  // Ballistics - Right
  if (right_db > meter->right_display_db) {
    meter->right_display_db = right_db;
  } else {
    meter->right_display_db -= DECAY_RATE_DB_PER_SEC * dt;
    if (meter->right_display_db < right_db)
      meter->right_display_db = right_db;
    if (meter->right_display_db < DB_MIN)
      meter->right_display_db = DB_MIN;
  }

  // Peak hold - Left (5.0s hold)
  if (left_db >= meter->left_peak_db) {
    meter->left_peak_db = left_db;
    meter->left_peak_time = now;
  } else if ((now - meter->left_peak_time) > (PEAK_HOLD_MS * 1000)) {
    meter->left_peak_db -= DECAY_RATE_DB_PER_SEC * dt;
    if (meter->left_peak_db < meter->left_display_db)
      meter->left_peak_db = meter->left_display_db;
  }

  // Peak hold - Right (5.0s hold)
  if (right_db >= meter->right_peak_db) {
    meter->right_peak_db = right_db;
    meter->right_peak_time = now;
  } else if ((now - meter->right_peak_time) > (PEAK_HOLD_MS * 1000)) {
    meter->right_peak_db -= DECAY_RATE_DB_PER_SEC * dt;
    if (meter->right_peak_db < meter->right_display_db)
      meter->right_peak_db = meter->right_display_db;
  }

  // Clip detection (10.0s hold)
  if (left_db >= DB_CLIP) {
    meter->left_clipped = TRUE;
    meter->left_clip_time = now;
  } else if (meter->left_clipped && (now - meter->left_clip_time) > (CLIP_HOLD_MS * 1000)) {
    meter->left_clipped = FALSE;
  }

  if (right_db >= DB_CLIP) {
    meter->right_clipped = TRUE;
    meter->right_clip_time = now;
  } else if (meter->right_clipped && (now - meter->right_clip_time) > (CLIP_HOLD_MS * 1000)) {
    meter->right_clipped = FALSE;
  }

  gtk_widget_queue_draw(GTK_WIDGET(meter));
}

void gtk_output_meter_reset_peaks(GtkOutputMeter *meter) {
  meter->left_peak_db = meter->left_display_db;
  meter->right_peak_db = meter->right_display_db;
  meter->left_clipped = FALSE;
  meter->right_clipped = FALSE;
  gtk_widget_queue_draw(GTK_WIDGET(meter));
}
