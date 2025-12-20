// SPDX-FileCopyrightText: 2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include "peq-response.h"

// Preferred widget size
#define PREF_WIDTH  400
#define PREF_HEIGHT 300

// Margins (fixed in pixels)
#define LABEL_MARGIN_LEFT   25
#define LABEL_MARGIN_BOTTOM 15
#define PADDING 3

#define FREQ_MIN 20.0
#define FREQ_MAX 20000.0
#define DB_MIN -24.0
#define DB_MAX  24.0

#define SAMPLE_RATE 48000.0

// Band colors (up to 8)
static const double band_colors[][3] = {
  { 0.4, 0.7, 0.9 },  // blue
  { 0.4, 0.8, 0.4 },  // green
  { 0.9, 0.9, 0.3 },  // yellow
  { 0.9, 0.6, 0.3 },  // orange
  { 0.9, 0.4, 0.4 },  // red
  { 0.7, 0.4, 0.9 },  // purple
  { 0.4, 0.9, 0.9 },  // cyan
  { 0.9, 0.4, 0.7 },  // pink
};

// Graph area calculated from widget size
struct graph_area {
  double left, right, top, bottom;
  double width, height;
};

struct _GtkFilterResponse {
  GtkWidget parent_instance;

  int num_bands;
  struct biquad_params bands[FILTER_RESPONSE_MAX_BANDS];
  struct biquad_coeffs coeffs[FILTER_RESPONSE_MAX_BANDS];
  gboolean band_enabled[FILTER_RESPONSE_MAX_BANDS];
  gboolean enabled;
};

G_DEFINE_TYPE(GtkFilterResponse, gtk_filter_response, GTK_TYPE_WIDGET)

// Convert frequency to X coordinate (log scale)
static double freq_to_x(const struct graph_area *g, double freq) {
  double log_min = log10(FREQ_MIN);
  double log_max = log10(FREQ_MAX);
  double log_freq = log10(freq);
  return g->left + (log_freq - log_min) / (log_max - log_min) * g->width;
}

// Convert dB to Y coordinate
static double db_to_y(const struct graph_area *g, double db) {
  return g->bottom - (db - DB_MIN) / (DB_MAX - DB_MIN) * g->height;
}

// Draw a single filter response curve
static void draw_filter_response(
  cairo_t                    *cr,
  const struct graph_area    *g,
  const struct biquad_coeffs *coeffs,
  double                      r,
  double                      gc,
  double                      b,
  double                      alpha,
  gboolean                    dashed
) {
  cairo_save(cr);

  cairo_set_source_rgba(cr, r, gc, b, alpha);
  cairo_set_line_width(cr, 1.5);

  if (dashed) {
    double dashes[] = { 4.0, 4.0 };
    cairo_set_dash(cr, dashes, 2, 0);
  }

  gboolean first = TRUE;
  for (double freq = FREQ_MIN; freq <= FREQ_MAX; freq *= 1.02) {
    double db = biquad_response_db(coeffs, freq, SAMPLE_RATE);
    double x = freq_to_x(g, freq);
    double y = db_to_y(g, db);

    if (first) {
      cairo_move_to(cr, x, y);
      first = FALSE;
    } else {
      cairo_line_to(cr, x, y);
    }
  }
  cairo_stroke(cr);

  cairo_restore(cr);
}

// Calculate combined response at a frequency (includes all enabled bands)
static double combined_response_db(GtkFilterResponse *response, double freq) {
  double total_db = 0.0;

  for (int i = 0; i < response->num_bands; i++) {
    if (response->band_enabled[i])
      total_db += biquad_response_db(&response->coeffs[i], freq, SAMPLE_RATE);
  }

  return total_db;
}

static void response_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  GtkFilterResponse *response = GTK_FILTER_RESPONSE(widget);

  int width = gtk_widget_get_width(widget);
  int height = gtk_widget_get_height(widget);

  // Calculate graph area from actual widget size
  struct graph_area g;
  g.left = LABEL_MARGIN_LEFT;
  g.right = width - PADDING;
  g.top = PADDING;
  g.bottom = height - LABEL_MARGIN_BOTTOM;
  g.width = g.right - g.left;
  g.height = g.bottom - g.top;

  cairo_t *cr = gtk_snapshot_append_cairo(
    snapshot,
    &GRAPHENE_RECT_INIT(0, 0, width, height)
  );

  // Background
  cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
  cairo_rectangle(cr, g.left, g.top, g.width, g.height);
  cairo_fill(cr);

  // Grid lines - horizontal (dB)
  cairo_set_source_rgba(cr, 1, 1, 1, 0.15);
  cairo_set_line_width(cr, 0.5);
  for (double db = -18; db <= 18; db += 6) {
    double y = db_to_y(&g, db);
    cairo_move_to(cr, g.left, y);
    cairo_line_to(cr, g.right, y);
  }
  cairo_stroke(cr);

  // Grid lines - vertical (frequency) at octave points
  double freqs[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
  for (int i = 0; i < 8; i++) {
    double x = freq_to_x(&g, freqs[i]);
    cairo_move_to(cr, x, g.top);
    cairo_line_to(cr, x, g.bottom);
  }
  cairo_stroke(cr);

  // 0dB reference line
  cairo_set_source_rgba(cr, 1, 1, 1, 0.3);
  cairo_set_line_width(cr, 1);
  double y0 = db_to_y(&g, 0);
  cairo_move_to(cr, g.left, y0);
  cairo_line_to(cr, g.right, y0);
  cairo_stroke(cr);

  // Axis labels
  cairo_set_source_rgba(cr, 1, 1, 1, 0.6);
  cairo_select_font_face(
    cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL
  );
  cairo_set_font_size(cr, 8);

  // Y-axis labels (dB)
  for (double db = -18; db <= 18; db += 12) {
    char label[8];
    snprintf(label, sizeof(label), "%+.0f", db);
    cairo_text_extents_t extents;
    cairo_text_extents(cr, label, &extents);
    double y = db_to_y(&g, db);
    cairo_move_to(cr, g.left - extents.width - 3, y + extents.height / 2);
    cairo_show_text(cr, label);
  }

  // X-axis labels (frequency)
  const char *freq_labels[] = { "100", "1k", "10k" };
  double freq_values[] = { 100, 1000, 10000 };
  for (int i = 0; i < 3; i++) {
    cairo_text_extents_t extents;
    cairo_text_extents(cr, freq_labels[i], &extents);
    double x = freq_to_x(&g, freq_values[i]);
    cairo_move_to(cr, x - extents.width / 2, g.bottom + extents.height + 3);
    cairo_show_text(cr, freq_labels[i]);
  }

  // Clip to graph area for curves
  cairo_save(cr);
  cairo_rectangle(cr, g.left, g.top, g.width, g.height);
  cairo_clip(cr);

  // Draw individual filter curves (dashed only if individual band disabled)
  for (int i = 0; i < response->num_bands; i++) {
    int color_idx = i % 8;
    gboolean band_on = response->band_enabled[i];
    draw_filter_response(
      cr, &g, &response->coeffs[i],
      band_colors[color_idx][0],
      band_colors[color_idx][1],
      band_colors[color_idx][2],
      band_on ? 0.5 : 0.3,
      !band_on  // dashed if band disabled
    );
  }

  // Draw combined response curve (white solid if enabled, grey dashed if disabled)
  cairo_set_line_width(cr, 2);
  if (response->enabled) {
    cairo_set_source_rgb(cr, 1, 1, 1);
  } else {
    cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
    double dashes[] = { 6.0, 4.0 };
    cairo_set_dash(cr, dashes, 2, 0);
  }

  gboolean first = TRUE;
  for (double freq = FREQ_MIN; freq <= FREQ_MAX; freq *= 1.02) {
    double db = combined_response_db(response, freq);
    double x = freq_to_x(&g, freq);
    double y = db_to_y(&g, db);

    if (first) {
      cairo_move_to(cr, x, y);
      first = FALSE;
    } else {
      cairo_line_to(cr, x, y);
    }
  }
  cairo_stroke(cr);

  cairo_restore(cr);

  cairo_destroy(cr);
}

static void response_measure(
  GtkWidget      *widget,
  GtkOrientation  orientation,
  int             for_size,
  int            *minimum,
  int            *natural,
  int            *minimum_baseline,
  int            *natural_baseline
) {
  if (orientation == GTK_ORIENTATION_HORIZONTAL) {
    *minimum = PREF_WIDTH / 2;
    *natural = PREF_WIDTH;
  } else {
    *minimum = PREF_HEIGHT / 2;
    *natural = PREF_HEIGHT;
  }
  *minimum_baseline = -1;
  *natural_baseline = -1;
}

static void gtk_filter_response_class_init(GtkFilterResponseClass *klass) {
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  widget_class->snapshot = response_snapshot;
  widget_class->measure = response_measure;
}

static void gtk_filter_response_init(GtkFilterResponse *response) {
  response->num_bands = 0;
  response->enabled = TRUE;

  for (int i = 0; i < FILTER_RESPONSE_MAX_BANDS; i++) {
    response->band_enabled[i] = TRUE;
    response->bands[i].type = BIQUAD_TYPE_PEAKING;
    response->bands[i].freq = 1000.0;
    response->bands[i].q = 1.0;
    response->bands[i].gain_db = 0.0;
  }
}

GtkWidget *gtk_filter_response_new(int num_bands) {
  GtkFilterResponse *response = g_object_new(GTK_TYPE_FILTER_RESPONSE, NULL);

  if (num_bands > FILTER_RESPONSE_MAX_BANDS)
    num_bands = FILTER_RESPONSE_MAX_BANDS;
  if (num_bands < 0)
    num_bands = 0;

  response->num_bands = num_bands;

  // Initialize coefficients for all bands
  for (int i = 0; i < num_bands; i++) {
    biquad_calculate(
      &response->bands[i], SAMPLE_RATE, &response->coeffs[i]
    );
  }

  return GTK_WIDGET(response);
}

void gtk_filter_response_set_filter(
  GtkFilterResponse          *response,
  int                         band,
  const struct biquad_params *params
) {
  if (band < 0 || band >= response->num_bands)
    return;

  response->bands[band] = *params;
  biquad_calculate(params, SAMPLE_RATE, &response->coeffs[band]);
  gtk_widget_queue_draw(GTK_WIDGET(response));
}

void gtk_filter_response_set_band_enabled(
  GtkFilterResponse *response,
  int                band,
  gboolean           enabled
) {
  if (band < 0 || band >= response->num_bands)
    return;

  if (response->band_enabled[band] != enabled) {
    response->band_enabled[band] = enabled;
    gtk_widget_queue_draw(GTK_WIDGET(response));
  }
}

void gtk_filter_response_set_enabled(
  GtkFilterResponse *response,
  gboolean           enabled
) {
  if (response->enabled != enabled) {
    response->enabled = enabled;
    gtk_widget_queue_draw(GTK_WIDGET(response));
  }
}
