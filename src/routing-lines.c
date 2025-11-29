// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "alsa.h"
#include "glow.h"
#include "routing-lines.h"
#include "port-enable.h"

// dotted dash when a sink is going to be removed by a drag
static const double dash_dotted[] = { 1, 10 };

// dash when dragging and not connected
static const double dash[] = { 4 };

// is a port category a mixer or DSP port, therefore at the
// top/bottom?
#define IS_MIXER(x) ((x) == PC_MIX || (x) == PC_DSP)

static void hsl_to_rgb(
  double h, double s, double l,
  double *r, double *g, double *b
) {
  double c = (1 - fabs(2 * l - 1)) * s;
  double hp = h / 60;
  double x = c * (1 - fabs(fmod(hp, 2) - 1));
  double m = l - c / 2;

       if (hp < 1) { *r = c; *g = x; *b = 0; }
  else if (hp < 2) { *r = x; *g = c; *b = 0; }
  else if (hp < 3) { *r = 0; *g = c; *b = x; }
  else if (hp < 4) { *r = 0; *g = x; *b = c; }
  else if (hp < 5) { *r = x; *g = 0; *b = c; }
  else             { *r = c; *g = 0; *b = x; }

  *r += m;
  *g += m;
  *b += m;
}

static void choose_line_colour(
  int     i,
  int     count,
  double *r,
  double *g,
  double *b
) {
  if (count % 2)
    count++;
  hsl_to_rgb(
    ((i / (count / 2) * 360 + i * 720) / count) % 360,
    0.75,
    0.5,
    r, g, b
  );
}

// draw a bezier curve given the end and control points
static void curve(
  cairo_t *cr,
  double   x1,
  double   y1,
  double   x2,
  double   y2,
  double   x3,
  double   y3,
  double   x4,
  double   y4
) {
  cairo_move_to(cr, x1, y1);
  cairo_curve_to(cr, x2, y2, x3, y3, x4, y4);
}

// given the bezier end & control points and t-value, return the
// position and tangent angle at that point
static void point_and_angle_on_bezier(
  double x1,
  double y1,
  double x2,
  double y2,
  double x3,
  double y3,
  double x4,
  double y4,
  double t,
  double *x,
  double *y,
  double *a
) {
  double t2 = t * t;
  double t3 = t2 * t;
  double ti = 1 - t;
  double ti2 = ti * ti;

  *x = x1 +
       (-x1 * 3 + t * (3 * x1 - x1 * t)) * t +
       (3 * x2 + t * (-6 * x2 + x2 * 3 * t)) * t +
       (x3 * 3 - x3 * 3 * t) * t2 +
       x4 * t3;
  *y = y1 +
       (-y1 * 3 + t * (3 * y1 - y1 * t)) * t +
       (3 * y2 + t * (-6 * y2 + y2 * 3 * t)) * t +
       (y3 * 3 - y3 * 3 * t) * t2 +
       y4 * t3;

  double dx = ti2 * (x2 - x1) +
              2 * ti * t * (x3 - x2) +
              t2 * (x4 - x3);
  double dy = ti2 * (y2 - y1) +
              2 * ti * t * (y3 - y2) +
              t2 * (y4 - y3);
  *a = atan2(dy, dx);
}

// draw an arrow in the middle of the line drawn by curve()
static void arrow(
  cairo_t *cr,
  double   x1,
  double   y1,
  double   x2,
  double   y2,
  double   x3,
  double   y3,
  double   x4,
  double   y4
) {
  // get midpoint and angle
  double mx, my, a;
  point_and_angle_on_bezier(x1, y1, x2, y2, x3, y3, x4, y4, 0.5, &mx, &my, &a);

  // calculate point of arrow
  double px = mx + cos(a) * 12;
  double py = my + sin(a) * 12;

  // calculate sides of arrow
  double s1x = mx + cos(a - M_PI_2) * 2;
  double s1y = my + sin(a - M_PI_2) * 2;
  double s2x = mx + cos(a + M_PI_2) * 2;
  double s2y = my + sin(a + M_PI_2) * 2;

  // draw triangle
  cairo_move_to(cr, px, py);
  cairo_line_to(cr, s1x, s1y);
  cairo_line_to(cr, s2x, s2y);
  cairo_close_path(cr);
}

// draw a small arrow indicator pointing in a direction from a port
// port_x, port_y: center of the port widget
// direction: 0 = right (→), 1 = left (←), 2 = up (↑), 3 = down (↓)
static void draw_arrow_indicator(
  cairo_t *cr,
  double   port_x,
  double   port_y,
  int      direction,
  double   r,
  double   g,
  double   b,
  double   level_db
) {
  double angle;
  switch (direction) {
    case 0: angle = 0;          break;  // right →
    case 1: angle = M_PI;       break;  // left ←
    case 2: angle = -M_PI_2;    break;  // up ↑
    case 3: angle = M_PI_2;     break;  // down ↓
    default: angle = 0;         break;
  }

  // arrow dimensions
  double arrow_len = 12;
  double arrow_width = 4;
  double line_len = 24;

  // calculate arrow base (end of line, start of triangle)
  double bx = port_x + cos(angle) * line_len;
  double by = port_y + sin(angle) * line_len;

  // calculate arrow tip
  double tx = bx + cos(angle) * arrow_len;
  double ty = by + sin(angle) * arrow_len;

  // calculate arrow base sides
  double s1x = bx + cos(angle - M_PI_2) * arrow_width;
  double s1y = by + sin(angle - M_PI_2) * arrow_width;
  double s2x = bx + cos(angle + M_PI_2) * arrow_width;
  double s2y = by + sin(angle + M_PI_2) * arrow_width;

  // draw glow behind arrow if level is high enough
  double intensity = get_glow_intensity(level_db);
  if (intensity > 0) {
    double gr, gg, gb;
    level_to_colour(level_db, &gr, &gg, &gb);

    cairo_set_dash(cr, NULL, 0, 0);
    for (int layer = GLOW_LAYERS - 1; layer >= 0; layer--) {
      double width, alpha;
      get_glow_layer_params(layer, intensity, &width, &alpha);

      cairo_set_source_rgba(cr, gr, gg, gb, alpha);
      cairo_set_line_width(cr, width);
      cairo_move_to(cr, port_x, port_y);
      cairo_line_to(cr, tx, ty);
      cairo_stroke(cr);
    }
  }

  cairo_set_source_rgb(cr, r, g, b);

  cairo_set_line_width(cr, 2);
  cairo_move_to(cr, port_x, port_y);
  cairo_line_to(cr, bx, by);
  cairo_stroke(cr);

  // draw filled triangle
  cairo_move_to(cr, tx, ty);
  cairo_line_to(cr, s1x, s1y);
  cairo_line_to(cr, s2x, s2y);
  cairo_close_path(cr);
  cairo_fill(cr);
}

// draw a glow around a source port that isn't connected to anything
static void draw_source_glow(
  cairo_t *cr,
  double   x,
  double   y,
  double   level_db
) {
  double intensity = get_glow_intensity(level_db);
  if (intensity <= 0)
    return;

  double r, g, b;
  level_to_colour(level_db, &r, &g, &b);

  // draw concentric circles as glow layers, scaled up for visibility
  cairo_set_dash(cr, NULL, 0, 0);
  for (int layer = GLOW_LAYERS - 1; layer >= 0; layer--) {
    double width, alpha;
    get_glow_layer_params(layer, intensity, &width, &alpha);

    // scale up for better visibility
    double radius = width * 1.2;
    cairo_set_source_rgba(cr, r, g, b, alpha * 0.7);
    cairo_arc(cr, x, y, radius, 0, 2 * M_PI);
    cairo_fill(cr);
  }
}

// draw a nice curved line connecting a source at (x1, y1) and a sink
// at (x2, y2)
static void draw_connection(
  cairo_t *cr,
  double   x1,
  double   y1,
  int      src_port_category,
  double   x2,
  double   y2,
  int      snk_port_category,
  double   r,
  double   g,
  double   b,
  double   w
) {
  double x3 = x1, y3 = y1, x4 = x2, y4 = y2;

  int src_is_mixer = IS_MIXER(src_port_category);
  int snk_is_mixer = IS_MIXER(snk_port_category);

  // vertical/horizontal?
  if (src_is_mixer == snk_is_mixer) {
    double f1 = 0.3;
    double f2 = 1 - f1;

    // vertical
    if (src_is_mixer) {
      y3 = y1 * f2 + y2 * f1;
      y4 = y1 * f1 + y2 * f2;

    // horizontal
    } else {
      x3 = x1 * f2 + x2 * f1;
      x4 = x1 * f1 + x2 * f2;
    }

  // diagonal
  } else {
    // calculate a fraction f1 close to 0 when approaching 45°
    // and close to 0.5 when approaching 0°/90°
    double a = fmod((atan2(y1 - y2, x2 - x1) * 180 / M_PI) + 360, 360);
    double f1 = fabs(fmod(a, 90) - 45) / 90;
    double f2 = 1 - f1;

    // bottom to right
    if (src_is_mixer) {
      y3 = y1 * f2 + y2 * f1;
      x4 = x1 * f1 + x2 * f2;

    // left to top
    } else {
      x3 = x1 * f2 + x2 * f1;
      y4 = y1 * f1 + y2 * f2;
    }
  }

  cairo_set_source_rgb(cr, r, g, b);
  cairo_set_line_width(cr, w);
  curve(cr, x1, y1, x3, y3, x4, y4, x2, y2);
  arrow(cr, x1, y1, x3, y3, x4, y4, x2, y2);
  cairo_stroke(cr);
}

// draw a level-based glow behind a routing line
// level_db should be in dB (-80 to 0)
static void draw_connection_glow(
  cairo_t *cr,
  double   x1,
  double   y1,
  int      src_port_category,
  double   x2,
  double   y2,
  int      snk_port_category,
  double   level_db
) {
  double intensity = get_glow_intensity(level_db);
  if (intensity <= 0)
    return;

  double r, g, b;
  level_to_colour(level_db, &r, &g, &b);

  // calculate bezier control points (same logic as draw_connection)
  double x3 = x1, y3 = y1, x4 = x2, y4 = y2;

  int src_is_mixer = IS_MIXER(src_port_category);
  int snk_is_mixer = IS_MIXER(snk_port_category);

  if (src_is_mixer == snk_is_mixer) {
    double f1 = 0.3;
    double f2 = 1 - f1;

    if (src_is_mixer) {
      y3 = y1 * f2 + y2 * f1;
      y4 = y1 * f1 + y2 * f2;
    } else {
      x3 = x1 * f2 + x2 * f1;
      x4 = x1 * f1 + x2 * f2;
    }
  } else {
    double a = fmod((atan2(y1 - y2, x2 - x1) * 180 / M_PI) + 360, 360);
    double f1 = fabs(fmod(a, 90) - 45) / 90;
    double f2 = 1 - f1;

    if (src_is_mixer) {
      y3 = y1 * f2 + y2 * f1;
      x4 = x1 * f1 + x2 * f2;
    } else {
      x3 = x1 * f2 + x2 * f1;
      y4 = y1 * f1 + y2 * f2;
    }
  }

  cairo_set_dash(cr, NULL, 0, 0);
  for (int layer = GLOW_LAYERS - 1; layer >= 0; layer--) {
    double width, alpha;
    get_glow_layer_params(layer, intensity, &width, &alpha);

    cairo_set_source_rgba(cr, r, g, b, alpha);
    cairo_set_line_width(cr, width);
    curve(cr, x1, y1, x3, y3, x4, y4, x2, y2);
    cairo_stroke(cr);
  }
}

// locate the center of a widget in the parent coordinates
// used for drawing lines to/from the "socket" widget of routing
// sources and sinks
static void get_widget_center(
  GtkWidget *w,
  GtkWidget *parent,
  double    *x,
  double    *y
) {
  double src_x = gtk_widget_get_allocated_width(w) / 2;
  double src_y = gtk_widget_get_allocated_height(w) / 2;
  gtk_widget_translate_coordinates(w, parent, src_x, src_y, x, y);
}

static void get_src_center(
  struct routing_src *r_src,
  GtkWidget          *parent,
  double             *x,
  double             *y
) {
  get_widget_center(r_src->widget2, parent, x, y);
  if (IS_MIXER(r_src->port_category))
    (*y)++;
}

static void get_snk_center(
  struct routing_snk *r_snk,
  GtkWidget          *parent,
  double             *x,
  double             *y
) {
  get_widget_center(r_snk->socket_widget, parent, x, y);
  if (IS_MIXER(r_snk->elem->port_category))
    (*y)++;
}

// redraw the overlay lines between the routing sources and sinks
void draw_routing_lines(
  GtkDrawingArea *drawing_area,
  cairo_t        *cr,
  int             width,
  int             height,
  void           *user_data
) {
  struct alsa_card *card = user_data;
  GtkWidget *parent = card->routing_lines;

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  int dragging = card->drag_type != DRAG_TYPE_NONE;

  // first pass: draw level glows behind all lines
  if (card->routing_levels) {
    for (int i = 0; i < card->routing_snks->len; i++) {
      struct routing_snk *r_snk = &g_array_index(
        card->routing_snks, struct routing_snk, i
      );
      struct alsa_elem *elem = r_snk->elem;

      // skip read-only mixer sinks
      if (elem->port_category == PC_MIX && card->has_fixed_mixer_inputs)
        continue;

      // skip disabled sinks
      if (!is_routing_snk_enabled(r_snk))
        continue;

      // skip if being dragged
      if (dragging && card->snk_drag == r_snk)
        continue;

      // get the source and skip if it's "Off"
      int r_src_idx = r_snk->effective_source_idx;
      if (!r_src_idx)
        continue;

      struct routing_src *r_src = &g_array_index(
        card->routing_srcs, struct routing_src, r_src_idx
      );

      // skip disabled sources
      if (!is_routing_src_enabled(r_src))
        continue;

      // get source level and skip if too low
      double level_db = get_routing_src_level_db(card, r_src);
      if (level_db < GLOW_MIN_DB)
        continue;

      // locate the source and sink coordinates
      double x1, y1, x2, y2;
      get_src_center(r_src, parent, &x1, &y1);
      get_snk_center(r_snk, parent, &x2, &y2);

      // draw the glow
      draw_connection_glow(
        cr,
        x1, y1, r_src->port_category,
        x2, y2, elem->port_category,
        level_db
      );
    }

    // draw glows for unconnected sources that have level meters
    // first, track which sources have at least one enabled connection
    int *src_connected = g_malloc0(card->routing_srcs->len * sizeof(int));

    for (int i = 0; i < card->routing_snks->len; i++) {
      struct routing_snk *r_snk = &g_array_index(
        card->routing_snks, struct routing_snk, i
      );
      struct alsa_elem *elem = r_snk->elem;

      // skip read-only mixer sinks
      if (elem->port_category == PC_MIX && card->has_fixed_mixer_inputs)
        continue;

      // skip disabled sinks
      if (!is_routing_snk_enabled(r_snk))
        continue;

      int r_src_idx = r_snk->effective_source_idx;
      if (r_src_idx > 0 && r_src_idx < card->routing_srcs->len) {
        struct routing_src *r_src = &g_array_index(
          card->routing_srcs, struct routing_src, r_src_idx
        );
        // only mark as connected if source is enabled
        if (is_routing_src_enabled(r_src))
          src_connected[r_src_idx] = 1;
      }
    }

    // now draw glows for unconnected sources
    for (int i = 1; i < card->routing_srcs->len; i++) {
      if (src_connected[i])
        continue;

      struct routing_src *r_src = &g_array_index(
        card->routing_srcs, struct routing_src, i
      );

      // skip disabled sources
      if (!is_routing_src_enabled(r_src))
        continue;

      // skip if no widget
      if (!r_src->widget2)
        continue;

      double level_db = get_routing_src_level_db(card, r_src);
      if (level_db < GLOW_MIN_DB)
        continue;

      double x, y;
      get_src_center(r_src, parent, &x, &y);
      draw_source_glow(cr, x, y, level_db);
    }

    g_free(src_connected);
  }

  // second pass: draw the routing lines on top
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    struct alsa_elem *elem = r_snk->elem;

    // don't draw lines to read-only mixer sinks
    if (elem->port_category == PC_MIX &&
        card->has_fixed_mixer_inputs)
      continue;

    // don't draw lines to disabled sinks
    if (!is_routing_snk_enabled(r_snk))
      continue;

    // if dragging and a routing sink is being reconnected then draw
    // it with dots
    int dragging_this = dragging && card->snk_drag == r_snk;
    if (dragging_this)
      cairo_set_dash(cr, dash_dotted, 2, 0);
    else
      cairo_set_dash(cr, NULL, 0, 0);

    // get the source and skip if it's "Off" or muted
    int r_src_idx = r_snk->effective_source_idx;
    if (!r_src_idx)
      continue;

    // look up the source
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, r_src_idx
    );

    // don't draw lines from disabled sources
    if (!is_routing_src_enabled(r_src))
      continue;

    // locate the source and sink coordinates
    double x1, y1, x2, y2;
    get_src_center(r_src, parent, &x1, &y1);
    get_snk_center(r_snk, parent, &x2, &y2);

    // pick a colour
    double r, g, b;
    choose_line_colour(i, card->routing_snks->len, &r, &g, &b);

    // make the colour lighter if it's being shown dotted
    if (dragging_this) {
      r = (r + 1) / 2;
      g = (g + 1) / 2;
      b = (b + 1) / 2;
    }

    // draw the connection
    draw_connection(
      cr,
      x1, y1, r_src->port_category,
      x2, y2, elem->port_category,
      r, g, b, 2
    );
  }

  // draw arrows for connections to/from disabled ports
  // this shows the user that something is connected but hidden

  // track which sources have connections to disabled sinks
  // (use a simple array since source IDs are small integers)
  int *src_has_disabled_snk = g_malloc0(card->routing_srcs->len * sizeof(int));

  // first pass: find enabled sinks connected to disabled sources
  // and disabled sinks (to mark their sources)
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    struct alsa_elem *elem = r_snk->elem;

    // skip read-only mixer sinks
    if (elem->port_category == PC_MIX && card->has_fixed_mixer_inputs)
      continue;

    // get the source connected to this sink
    int r_src_idx = r_snk->effective_source_idx;
    if (!r_src_idx)
      continue;

    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, r_src_idx
    );

    int snk_enabled = is_routing_snk_enabled(r_snk);
    int src_enabled = is_routing_src_enabled(r_src);

    // case 1: enabled sink connected to disabled source → draw arrow to sink
    if (snk_enabled && !src_enabled) {
      double x, y;
      get_snk_center(r_snk, parent, &x, &y);

      // mixer/DSP sinks are at bottom, others are on right side
      int direction = IS_MIXER(elem->port_category) ? 3 : 1;

      double level_db = get_routing_src_level_db(card, r_src);
      draw_arrow_indicator(cr, x, y, direction, 0.75, 0.25, 0.25, level_db);
    }

    // case 2: disabled sink → mark the source
    if (!snk_enabled && src_enabled) {
      src_has_disabled_snk[r_src_idx] = 1;
    }
  }

  // second pass: draw arrows from sources that have disabled sinks
  for (int i = 1; i < card->routing_srcs->len; i++) {
    if (!src_has_disabled_snk[i])
      continue;

    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    // skip if source itself is disabled
    if (!is_routing_src_enabled(r_src))
      continue;

    double x, y;
    get_src_center(r_src, parent, &x, &y);

    // mixer/DSP sources are at top, others are on left side
    int direction = IS_MIXER(r_src->port_category) ? 2 : 0;

    double level_db = get_routing_src_level_db(card, r_src);
    draw_arrow_indicator(cr, x, y, direction, 0.75, 0.25, 0.25, level_db);
  }

  g_free(src_has_disabled_snk);
}

// draw the overlay dragging line
void draw_drag_line(
  GtkDrawingArea *drawing_area,
  cairo_t        *cr,
  int             width,
  int             height,
  void           *user_data
) {
  struct alsa_card *card = user_data;
  GtkWidget *parent = card->drag_line;

  // if not dragging or routing src & snk not specified or drag out of
  // bounds then do nothing
  if (card->drag_type == DRAG_TYPE_NONE ||
      (!card->src_drag && !card->snk_drag) ||
      card->drag_x < 0 ||
      card->drag_y < 0)
    return;

  // the drag mouse position is relative to card->routing_grid
  // translate it to the overlay card->drag_line
  // (don't need to do this if both src_drag and snk_drag are set)
  double drag_x, drag_y;
  if (!card->src_drag || !card->snk_drag)
    gtk_widget_translate_coordinates(
      card->routing_grid, parent,
      card->drag_x, card->drag_y,
      &drag_x, &drag_y
    );

  // get the line start position; either a routing source socket
  // widget or the drag mouse position
  double x1, y1;
  if (card->src_drag) {
    get_src_center(card->src_drag, parent, &x1, &y1);
  } else {
    x1 = drag_x;
    y1 = drag_y;
  }

  // get the line end position; either a routing sink socket widget or
  // the drag mouse position
  double x2, y2;
  if (card->snk_drag) {
    get_snk_center(card->snk_drag, parent, &x2, &y2);
  } else {
    x2 = drag_x;
    y2 = drag_y;
  }

  // if routing src & snk both specified then draw a curved line as if
  // it was connected (except black)
  if (card->src_drag && card->snk_drag) {
    draw_connection(
      cr,
      x1, y1, card->src_drag->port_category,
      x2, y2, card->snk_drag->elem->port_category,
      1, 1, 1, 2
    );

  // otherwise draw a straight line
  } else {
    cairo_set_dash(cr, dash, 1, 0);

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
  }
}

// initialise level indication storage for routing lines
// the actual level updates come from the levels window timer
void routing_levels_init(struct alsa_card *card) {
  // find the level meter element
  card->level_meter_elem = get_elem_by_name(card->elems, "Level Meter");
  if (!card->level_meter_elem) {
    card->routing_levels = NULL;
    card->routing_levels_count = 0;
    return;
  }

  // allocate storage for level values (updated by levels window timer)
  card->routing_levels_count = card->level_meter_elem->count;
  card->routing_levels = g_malloc0(
    card->routing_levels_count * sizeof(double)
  );

  // initialise all levels to minimum
  for (int i = 0; i < card->routing_levels_count; i++)
    card->routing_levels[i] = -80.0;
}

// clean up level indication resources
void routing_levels_cleanup(struct alsa_card *card) {
  // free level storage
  if (card->routing_levels) {
    g_free(card->routing_levels);
    card->routing_levels = NULL;
  }

  card->routing_levels_count = 0;
  card->level_meter_elem = NULL;
}
