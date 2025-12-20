// SPDX-FileCopyrightText: 2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <pango/pangocairo.h>
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
#define Q_MIN 0.1
#define Q_MAX 10.0

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
  gboolean dsp_enabled;  // overall DSP enable state
  int highlight_band;  // -1 for none
  int internal_highlight;  // -1 for none, set by hover

  // Drag state
  int drag_band;  // -1 for none
  double drag_offset_x;  // offset from handle center to click point
  double drag_offset_y;
};

G_DEFINE_TYPE(GtkFilterResponse, gtk_filter_response, GTK_TYPE_WIDGET)

enum {
  SIGNAL_FILTER_CHANGED,
  SIGNAL_HIGHLIGHT_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

// Calculate graph area from widget dimensions
static void calc_graph_area(int width, int height, struct graph_area *g) {
  g->left = LABEL_MARGIN_LEFT;
  g->right = width - PADDING;
  g->top = PADDING;
  g->bottom = height - LABEL_MARGIN_BOTTOM;
  g->width = g->right - g->left;
  g->height = g->bottom - g->top;
}

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

// Convert X coordinate to frequency (log scale)
static double x_to_freq(const struct graph_area *g, double x) {
  double log_min = log10(FREQ_MIN);
  double log_max = log10(FREQ_MAX);
  double t = (x - g->left) / g->width;
  double log_freq = log_min + t * (log_max - log_min);
  return pow(10.0, log_freq);
}

// Convert Y coordinate to dB
static double y_to_db(const struct graph_area *g, double y) {
  double t = (g->bottom - y) / g->height;
  return DB_MIN + t * (DB_MAX - DB_MIN);
}

// Convert Q to Y coordinate (log scale, higher Q = higher Y)
static double q_to_y(const struct graph_area *g, double q) {
  double log_min = log10(Q_MIN);
  double log_max = log10(Q_MAX);
  double log_q = log10(q);
  double t = (log_q - log_min) / (log_max - log_min);
  return g->bottom - t * g->height;
}

// Convert Y coordinate to Q (log scale, higher Y = higher Q)
static double y_to_q(const struct graph_area *g, double y) {
  double t = (g->bottom - y) / g->height;
  double log_min = log10(Q_MIN);
  double log_max = log10(Q_MAX);
  double log_q = log_min + t * (log_max - log_min);
  return pow(10.0, log_q);
}

// Draw a single filter response curve with shading to 0 dB line
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

  double y0 = db_to_y(g, 0);
  double x_start = freq_to_x(g, FREQ_MIN);
  double x_end = freq_to_x(g, FREQ_MAX);

  // Build closed path for fill
  cairo_move_to(cr, x_start, y0);
  for (double freq = FREQ_MIN; freq <= FREQ_MAX; freq *= 1.02) {
    double db = biquad_response_db(coeffs, freq, SAMPLE_RATE);
    double x = freq_to_x(g, freq);
    double y = db_to_y(g, db);
    cairo_line_to(cr, x, y);
  }
  cairo_line_to(cr, x_end, y0);
  cairo_close_path(cr);

  // Fill with translucent color
  cairo_set_source_rgba(cr, r, gc, b, alpha * 0.3);
  cairo_fill(cr);

  // Build new path for stroke (curve only)
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

  cairo_set_source_rgba(cr, r, gc, b, alpha);
  cairo_set_line_width(cr, 1.5);

  if (dashed) {
    double dashes[] = { 4.0, 4.0 };
    cairo_set_dash(cr, dashes, 2, 0);
  }

  cairo_stroke(cr);

  cairo_restore(cr);
}

// Draw filter handle showing freq/gain and Q
static void draw_filter_handle(
  cairo_t                    *cr,
  const struct graph_area    *ga,
  const struct biquad_params *params,
  const struct biquad_coeffs *coeffs,
  int                         band_num,
  double                      r,
  double                      g,
  double                      b,
  double                      alpha,
  gboolean                    highlighted,
  gboolean                    enabled
) {
  cairo_save(cr);
  cairo_new_path(cr);

  double x = freq_to_x(ga, params->freq);
  double y;

  // Determine Y position based on filter type
  if (biquad_type_uses_gain(params->type)) {
    y = db_to_y(ga, params->gain_db);
  } else {
    // For LP/HP/BP/Notch, Y represents Q
    y = q_to_y(ga, params->q);
  }

  double radius = highlighted ? 12 : 10;
  double line_width = highlighted ? 2 : 1.5;

  // Draw Q whiskers for filters with gain
  if (biquad_type_uses_gain(params->type)) {
    // Calculate bandwidth as ratio for log scale symmetry
    double ratio = pow(2.0, 0.5 / params->q);
    double freq_lo = params->freq / ratio;
    double freq_hi = params->freq * ratio;
    if (freq_lo < FREQ_MIN) freq_lo = FREQ_MIN;
    if (freq_hi > FREQ_MAX) freq_hi = FREQ_MAX;
    double x_lo = freq_to_x(ga, freq_lo) - radius;
    double x_hi = freq_to_x(ga, freq_hi) + radius;

    cairo_set_source_rgba(cr, r, g, b, alpha * 0.8);
    cairo_set_line_width(cr, line_width);
    cairo_move_to(cr, x_lo, y);
    cairo_line_to(cr, x_hi, y);
    cairo_stroke(cr);

    // Draw small vertical ticks at bandwidth edges
    double tick_size = highlighted ? 4 : 3;
    cairo_move_to(cr, x_lo, y - tick_size);
    cairo_line_to(cr, x_lo, y + tick_size);
    cairo_move_to(cr, x_hi, y - tick_size);
    cairo_line_to(cr, x_hi, y + tick_size);
    cairo_stroke(cr);
  }

  // Draw center circle (dark grey background)
  cairo_arc(cr, x, y, radius, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, alpha);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, r, g, b, alpha);
  cairo_set_line_width(cr, 1.5);
  if (!enabled) {
    double dashes[] = { 3.0, 3.0 };
    cairo_set_dash(cr, dashes, 2, 0);
  }
  cairo_stroke(cr);
  cairo_set_dash(cr, NULL, 0, 0);

  // Draw band number in band color using Pango for proper centering
  char label[12];
  snprintf(label, sizeof(label), "%d", band_num);

  PangoLayout *layout = pango_cairo_create_layout(cr);
  PangoFontDescription *font_desc = pango_font_description_from_string(
    highlighted ? "Sans Bold 10" : "Sans Bold 9"
  );
  pango_layout_set_font_description(layout, font_desc);
  pango_layout_set_text(layout, label, -1);

  int text_width, text_height;
  pango_layout_get_pixel_size(layout, &text_width, &text_height);

  cairo_set_source_rgba(cr, r, g, b, alpha);
  cairo_move_to(cr, x - text_width / 2.0, y - text_height / 2.0);
  pango_cairo_show_layout(cr, layout);

  pango_font_description_free(font_desc);
  g_object_unref(layout);

  cairo_restore(cr);
}

// Find which band handle (if any) is at the given position
// Returns band index (0-based) or -1 if none
static int find_band_at_position(
  GtkFilterResponse *response,
  double             mx,
  double             my
) {
  int width = gtk_widget_get_width(GTK_WIDGET(response));
  int height = gtk_widget_get_height(GTK_WIDGET(response));

  struct graph_area g;
  calc_graph_area(width, height, &g);

  double hit_radius = 12;  // slightly larger than visual radius for easier targeting
  int closest_band = -1;
  double closest_dist = hit_radius * hit_radius;

  for (int i = 0; i < response->num_bands; i++) {
    const struct biquad_params *params = &response->bands[i];

    double x = freq_to_x(&g, params->freq);
    double y;

    if (biquad_type_uses_gain(params->type)) {
      y = db_to_y(&g, params->gain_db);
    } else {
      y = q_to_y(&g, params->q);
    }

    double dx = mx - x;
    double dy = my - y;
    double dist_sq = dx * dx + dy * dy;

    if (dist_sq < closest_dist) {
      closest_dist = dist_sq;
      closest_band = i;
    }
  }

  return closest_band;
}

// Motion event callbacks
static void response_motion(
  GtkEventControllerMotion *controller,
  double                    x,
  double                    y,
  GtkFilterResponse        *response
) {
  int band = find_band_at_position(response, x, y);

  if (band != response->internal_highlight) {
    response->internal_highlight = band;
    response->highlight_band = band;
    g_signal_emit(response, signals[SIGNAL_HIGHLIGHT_CHANGED], 0, band);
    gtk_widget_queue_draw(GTK_WIDGET(response));
  }
}

static void response_leave(
  GtkEventControllerMotion *controller,
  GtkFilterResponse        *response
) {
  if (response->internal_highlight != -1) {
    response->internal_highlight = -1;
    response->highlight_band = -1;
    g_signal_emit(response, signals[SIGNAL_HIGHLIGHT_CHANGED], 0, -1);
    gtk_widget_queue_draw(GTK_WIDGET(response));
  }
}

// Drag gesture callbacks
static void response_drag_begin(
  GtkGestureDrag    *gesture,
  double             start_x,
  double             start_y,
  GtkFilterResponse *response
) {
  int band = find_band_at_position(response, start_x, start_y);

  if (band >= 0) {
    response->drag_band = band;
    response->highlight_band = band;

    // Calculate offset from handle center to click point
    int width = gtk_widget_get_width(GTK_WIDGET(response));
    int height = gtk_widget_get_height(GTK_WIDGET(response));
    struct graph_area g;
    calc_graph_area(width, height, &g);

    const struct biquad_params *params = &response->bands[band];
    double handle_x = freq_to_x(&g, params->freq);
    double handle_y;
    if (biquad_type_uses_gain(params->type)) {
      handle_y = db_to_y(&g, params->gain_db);
    } else {
      handle_y = q_to_y(&g, params->q);
    }

    response->drag_offset_x = start_x - handle_x;
    response->drag_offset_y = start_y - handle_y;

    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
  }
}

static void response_drag_update(
  GtkGestureDrag    *gesture,
  double             offset_x,
  double             offset_y,
  GtkFilterResponse *response
) {
  if (response->drag_band < 0)
    return;

  double start_x, start_y;
  gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);

  // Calculate handle center position (subtract click offset from mouse position)
  double x = start_x + offset_x - response->drag_offset_x;
  double y = start_y + offset_y - response->drag_offset_y;

  int width = gtk_widget_get_width(GTK_WIDGET(response));
  int height = gtk_widget_get_height(GTK_WIDGET(response));

  struct graph_area g;
  calc_graph_area(width, height, &g);

  // Convert position to freq
  double freq = x_to_freq(&g, x);
  if (freq < FREQ_MIN) freq = FREQ_MIN;
  if (freq > FREQ_MAX) freq = FREQ_MAX;

  // Update the band
  struct biquad_params *params = &response->bands[response->drag_band];
  params->freq = freq;

  if (biquad_type_uses_gain(params->type)) {
    // Y adjusts gain
    double gain_db = y_to_db(&g, y);
    if (gain_db < DB_MIN) gain_db = DB_MIN;
    if (gain_db > DB_MAX) gain_db = DB_MAX;
    params->gain_db = gain_db;
  } else {
    // Y adjusts Q (higher = narrower)
    double q = y_to_q(&g, y);
    if (q < Q_MIN) q = Q_MIN;
    if (q > Q_MAX) q = Q_MAX;
    params->q = q;
  }

  // Recalculate coefficients
  biquad_calculate(params, SAMPLE_RATE, &response->coeffs[response->drag_band]);

  // Emit signal to notify external code
  g_signal_emit(response, signals[SIGNAL_FILTER_CHANGED], 0,
                response->drag_band, params);

  gtk_widget_queue_draw(GTK_WIDGET(response));
}

static void response_drag_end(
  GtkGestureDrag    *gesture,
  double             offset_x,
  double             offset_y,
  GtkFilterResponse *response
) {
  response->drag_band = -1;
}

// Scroll callback to adjust Q for filters with gain
static gboolean response_scroll(
  GtkEventControllerScroll *controller,
  double                    dx,
  double                    dy,
  GtkFilterResponse        *response
) {
  // Only adjust if hovering over a band
  if (response->internal_highlight < 0)
    return FALSE;

  int band = response->internal_highlight;
  struct biquad_params *params = &response->bands[band];

  // Only adjust Q for filters with gain
  if (!biquad_type_uses_gain(params->type))
    return FALSE;

  // Adjust Q logarithmically (scroll up = higher Q = narrower)
  double factor = (dy > 0) ? 0.9 : 1.1;
  double new_q = params->q * factor;

  // Clamp Q
  if (new_q < Q_MIN) new_q = Q_MIN;
  if (new_q > Q_MAX) new_q = Q_MAX;

  params->q = new_q;

  // Recalculate coefficients
  biquad_calculate(params, SAMPLE_RATE, &response->coeffs[band]);

  // Emit signal to notify external code
  g_signal_emit(response, signals[SIGNAL_FILTER_CHANGED], 0, band, params);

  gtk_widget_queue_draw(GTK_WIDGET(response));

  return TRUE;
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

  struct graph_area g;
  calc_graph_area(width, height, &g);

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

  // Draw individual filter curves (skip highlighted band for now)
  // When DSP is disabled, all curves are muted
  for (int i = 0; i < response->num_bands; i++) {
    if (i == response->highlight_band)
      continue;
    int color_idx = i % 8;
    gboolean band_on = response->band_enabled[i] && response->dsp_enabled;
    draw_filter_response(
      cr, &g, &response->coeffs[i],
      band_colors[color_idx][0],
      band_colors[color_idx][1],
      band_colors[color_idx][2],
      band_on ? 0.5 : 0.3,
      !band_on  // dashed if band or DSP disabled
    );
  }

  // Draw combined response curve
  // White solid if fully enabled, grey dashed if section or DSP disabled
  cairo_save(cr);
  cairo_set_line_width(cr, 2);
  if (response->enabled && response->dsp_enabled) {
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

  // Draw highlighted band last (on top of everything) with full opacity
  if (response->highlight_band >= 0 &&
      response->highlight_band < response->num_bands) {
    int i = response->highlight_band;
    int color_idx = i % 8;
    gboolean band_on = response->band_enabled[i] && response->dsp_enabled;
    draw_filter_response(
      cr, &g, &response->coeffs[i],
      band_colors[color_idx][0],
      band_colors[color_idx][1],
      band_colors[color_idx][2],
      band_on ? 1.0 : 0.6,
      !band_on
    );
  }

  // Draw filter handles (freq/gain bubbles with Q whiskers)
  if (response->highlight_band >= 0) {
    // When highlighted, draw non-highlighted first, then highlighted on top
    for (int i = 0; i < response->num_bands; i++) {
      if (i == response->highlight_band)
        continue;
      int color_idx = i % 8;
      gboolean band_on = response->band_enabled[i] && response->dsp_enabled;
      double alpha = band_on ? 0.7 : 0.4;
      draw_filter_handle(
        cr, &g, &response->bands[i], &response->coeffs[i],
        i + 1,
        band_colors[color_idx][0],
        band_colors[color_idx][1],
        band_colors[color_idx][2],
        alpha, FALSE, band_on
      );
    }
    // Highlighted handle last
    if (response->highlight_band < response->num_bands) {
      int i = response->highlight_band;
      int color_idx = i % 8;
      gboolean band_on = response->band_enabled[i] && response->dsp_enabled;
      double alpha = band_on ? 1.0 : 0.6;
      draw_filter_handle(
        cr, &g, &response->bands[i], &response->coeffs[i],
        i + 1,
        band_colors[color_idx][0],
        band_colors[color_idx][1],
        band_colors[color_idx][2],
        alpha, TRUE, band_on
      );
    }
  } else {
    // No highlight: draw in reverse order so 1 is on top
    for (int i = response->num_bands - 1; i >= 0; i--) {
      int color_idx = i % 8;
      gboolean band_on = response->band_enabled[i] && response->dsp_enabled;
      double alpha = band_on ? 0.7 : 0.4;
      draw_filter_handle(
        cr, &g, &response->bands[i], &response->coeffs[i],
        i + 1,
        band_colors[color_idx][0],
        band_colors[color_idx][1],
        band_colors[color_idx][2],
        alpha, FALSE, band_on
      );
    }
  }

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

  // Signal emitted when a filter is changed by dragging
  signals[SIGNAL_FILTER_CHANGED] = g_signal_new(
    "filter-changed",
    G_TYPE_FROM_CLASS(klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    2,
    G_TYPE_INT,     // band index
    G_TYPE_POINTER  // biquad_params pointer
  );

  // Signal emitted when highlight changes (from hover in response widget)
  signals[SIGNAL_HIGHLIGHT_CHANGED] = g_signal_new(
    "highlight-changed",
    G_TYPE_FROM_CLASS(klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    1,
    G_TYPE_INT  // band index (-1 for none)
  );
}

static void gtk_filter_response_init(GtkFilterResponse *response) {
  response->num_bands = 0;
  response->enabled = TRUE;
  response->dsp_enabled = TRUE;
  response->highlight_band = -1;
  response->internal_highlight = -1;
  response->drag_band = -1;

  for (int i = 0; i < FILTER_RESPONSE_MAX_BANDS; i++) {
    response->band_enabled[i] = TRUE;
    response->bands[i].type = BIQUAD_TYPE_PEAKING;
    response->bands[i].freq = 1000.0;
    response->bands[i].q = 1.0;
    response->bands[i].gain_db = 0.0;
  }

  // Add motion controller for hover detection
  GtkEventController *motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "motion", G_CALLBACK(response_motion), response);
  g_signal_connect(motion, "leave", G_CALLBACK(response_leave), response);
  gtk_widget_add_controller(GTK_WIDGET(response), motion);

  // Add drag gesture for handle dragging
  GtkGesture *drag = gtk_gesture_drag_new();
  g_signal_connect(drag, "drag-begin", G_CALLBACK(response_drag_begin), response);
  g_signal_connect(drag, "drag-update", G_CALLBACK(response_drag_update), response);
  g_signal_connect(drag, "drag-end", G_CALLBACK(response_drag_end), response);
  gtk_widget_add_controller(GTK_WIDGET(response), GTK_EVENT_CONTROLLER(drag));

  // Add scroll controller for Q adjustment
  GtkEventController *scroll = gtk_event_controller_scroll_new(
    GTK_EVENT_CONTROLLER_SCROLL_VERTICAL
  );
  g_signal_connect(scroll, "scroll", G_CALLBACK(response_scroll), response);
  gtk_widget_add_controller(GTK_WIDGET(response), scroll);
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

void gtk_filter_response_set_dsp_enabled(
  GtkFilterResponse *response,
  gboolean           enabled
) {
  if (response->dsp_enabled != enabled) {
    response->dsp_enabled = enabled;
    gtk_widget_queue_draw(GTK_WIDGET(response));
  }
}

void gtk_filter_response_set_highlight(
  GtkFilterResponse *response,
  int                band
) {
  if (response->highlight_band != band) {
    response->highlight_band = band;
    gtk_widget_queue_draw(GTK_WIDGET(response));
  }
}
