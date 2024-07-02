// SPDX-FileCopyrightText: 2021 Stiliyan Varbanov <https://www.fiverr.com/stilvar>
// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: LGPL-3.0-or-later

/*
 * A Dial widget for GTK-4 similar to GtkScale.
 * 2021 Stiliyan Varbanov www.fiverr.com/stilvar
 */

#include <stdlib.h>
#include <glib-2.0/glib.h>
#include <glib-object.h>
#include <cairo/cairo.h>
#include <stdio.h>
#include <graphene-1.0/graphene.h>

#include <math.h>

#include "gtkdial.h"

#define DIAL_MIN_WIDTH 50
#define DIAL_MAX_WIDTH 70

#define HISTORY_COUNT 50

static int set_value(GtkDial *dial, double newval);

static void gtk_dial_set_property(
  GObject      *object,
  guint         prop_id,
  const GValue *value,
  GParamSpec   *pspec
);

static void gtk_dial_get_property(
  GObject    *object,
  guint       prop_id,
  GValue     *value,
  GParamSpec *pspec
);

static void gtk_dial_move_slider(GtkDial *dial, GtkScrollType scroll);

static void gtk_dial_drag_gesture_begin(
  GtkGestureDrag *gesture,
  double          offset_x,
  double          offset_y,
  GtkDial        *dial
);

static void gtk_dial_drag_gesture_update(
  GtkGestureDrag *gesture,
  double          offset_x,
  double          offset_y,
  GtkDial        *dial
);

static void gtk_dial_drag_gesture_end(
  GtkGestureDrag *gesture,
  double          offset_x,
  double          offset_y,
  GtkDial        *dial
);

static void gtk_dial_click_gesture_pressed(
  GtkGestureClick *gesture,
  int              n_press,
  double           x,
  double           y,
  GtkDial         *dial
);

static gboolean gtk_dial_scroll_controller_scroll(
  GtkEventControllerScroll *scroll,
  double                    dx,
  double                    dy,
  GtkDial                  *dial
);

static void gtk_dial_dispose(GObject *o);

typedef enum {
  GRAB_NONE,
  GRAB_SLIDER
} e_grab;

enum {
  PROP_0,
  PROP_ADJUSTMENT,
  PROP_ROUND_DIGITS,
  PROP_ZERO_DB,
  PROP_OFF_DB,
  PROP_TAPER,
  PROP_CAN_CONTROL,
  PROP_PEAK_HOLD,
  LAST_PROP
};

enum {
  SIGNAL_0,
  VALUE_CHANGED,
  MOVE_SLIDER,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];
static GParamSpec *properties[LAST_PROP];

struct _GtkDial {
  GtkWidget parent_instance;
  GtkAdjustment *adj;

  GtkGesture *drag_gesture, *click_gesture;
  GtkEventController *scroll_controller;
  e_grab grab;
  double dvalp;

  int round_digits;
  double zero_db;
  double off_db;
  int taper;
  gboolean can_control;
  int peak_hold;

  int properties_updated;

  // linear taper breakpoints array
  double *taper_breakpoints;
  double *taper_outputs;
  int taper_breakpoints_count;

  // level meter colour breakpoints array
  const int *level_breakpoints;
  const double *level_colours;
  int level_breakpoints_count;

  // variables derived from the widget's dynamic properties (size and
  // configuration, excluding the value)
  int    dim;
  double w;
  double h;
  double radius;
  double slider_thickness;
  double knob_radius;
  double slider_radius;
  double background_radius;
  double cx;
  double cy;
  double zero_db_x;
  double zero_db_y;
  double *level_breakpoint_angles;

  // cairo patterns dependent on the above
  cairo_pattern_t *fill_pattern[2][2];
  cairo_pattern_t *outline_pattern[2];

  // pango resources for displaying the peak value
  PangoLayout *peak_layout;
  PangoFontDescription *peak_font_desc;

  // variables derived from the dial value
  double valp;
  double angle;
  double slider_cx;
  double slider_cy;

  // same for the peak angle
  double peak_angle;

  // value history for displaying peak
  double hist_values[HISTORY_COUNT];
  long long hist_time[HISTORY_COUNT];
  double current_peak;
  int hist_head, hist_tail, hist_count;
};

G_DEFINE_TYPE(GtkDial, gtk_dial, GTK_TYPE_WIDGET)

static void dial_snapshot(GtkWidget *widget, GtkSnapshot *snapshot);
static void dial_measure(
  GtkWidget      *widget,
  GtkOrientation  orientation,
  int             for_size,
  int            *minimum,
  int            *natural,
  int            *minimum_baseline,
  int            *natural_baseline
);

#define add_slider_binding(w_class, binding_set, keyval, mask, scroll) \
  gtk_widget_class_add_binding_signal(w_class,       \
                                      keyval, mask,  \
                                      "move-slider", \
                                      "(i)", scroll)

long long current_time = 0;

void gtk_dial_peak_tick(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_BOOTTIME, &ts) < 0)
    return;

  current_time = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// BEGIN SECTION HELPERS

#define TOTAL_ROTATION_DEGREES 290
#define TOTAL_ROTATION (2 * M_PI * TOTAL_ROTATION_DEGREES / 360)
#define ANGLE_START (-M_PI / 2 - TOTAL_ROTATION / 2)
#define ANGLE_END (-M_PI / 2 + TOTAL_ROTATION / 2)

#define DRAG_FACTOR 0.5

// convert val from mn..mx to 0..1 with clamp
static double calc_valp(double val, double mn, double mx) {
  if (val <= mn)
    return 0.0;
  if (val >= mx)
    return 1.0;

  return (val - mn) / (mx - mn);
}

static double taper_linear(double val, double *bp, double *out, int count) {
  if (count < 3)
    return val;

  if (val <= bp[0])
    return out[0];

  for (int i = 0; i < count - 1; i++) {
    if (val > bp[i + 1])
      continue;

    double scale = (out[i + 1] - out[i]) / (bp[i + 1] - bp[i]);
    return out[i] + scale * (val - bp[i]);
  }

  return out[count - 1];
}

static double taper_log(double val) {

  // 10^(val - 1) converts it to 0.1..1 with a nice curve
  val = pow(10, val - 1);

  // convert to 0..1 again
  return (val - 0.1) / 0.9;
}

static double calc_taper(GtkDial *dial, double val) {
  double mn = gtk_adjustment_get_lower(dial->adj);
  double mx = gtk_adjustment_get_upper(dial->adj);
  double off_db = gtk_dial_get_off_db(dial);

  // if off_db is set, then values below it are considered as
  // almost-silence, so we clamp them to 0.01
  if (off_db > mn) {
    if (val == mn)
      val = 0;
    else if (val <= off_db)
      val = 0.01;
    else
      val = calc_valp(val, off_db, mx) * 0.99 + 0.01;
  } else {
    val = calc_valp(val, mn, mx);
  }

  if (dial->taper == GTK_DIAL_TAPER_LINEAR)
    return taper_linear(
      val,
      dial->taper_breakpoints,
      dial->taper_outputs,
      dial->taper_breakpoints_count
    );

  if (dial->taper == GTK_DIAL_TAPER_LOG)
    return taper_log(val);

  g_warning("Invalid taper value: %d", dial->taper);

  return val;
}

static double calc_val(double valp, double mn, double mx) {
  return (mx - mn) * valp + mn;
}

static int calculate_dial_height(int width) {
  double radius = width / 2;
  double angle = (360 - TOTAL_ROTATION_DEGREES) / 2 * M_PI / 180;
  double height = radius + radius * cos(angle);

  return ceil(height);
}

static double calculate_dial_radius_from_height(int height) {
  double angle = (360 - TOTAL_ROTATION_DEGREES) / 2.0 * M_PI / 180.0;
  return height / (1 + cos(angle));
}

// internal replacement for cairo_pattern_add_color_stop_rgb() that
// dims the color if the widget is insensitive and brightens it by
// focus_mult
static void cairo_add_stop_rgb_dim(
  cairo_pattern_t *pat,
  double           offset,
  double           r,
  double           g,
  double           b,
  int              dim,
  double           focus_mult
) {
  double x = dim ? 0.5 : 1.0;
  x *= focus_mult;

  cairo_pattern_add_color_stop_rgb(pat, offset, r * x, g * x, b * x);
}

static int update_dial_properties(GtkDial *dial) {

  // always update
  dial->dim = !gtk_widget_is_sensitive(GTK_WIDGET(dial)) && dial->can_control;

  // the rest of the values only depend on the widget size and
  // configuration
  double width = gtk_widget_get_width(GTK_WIDGET(dial));
  double height = gtk_widget_get_height(GTK_WIDGET(dial));

  if (dial->w == width && dial->h == height && !dial->properties_updated)
    return 0;

  dial->w = width;
  dial->h = height;
  dial->properties_updated = 0;

  // calculate size of dial to fit within the given space
  if (width > DIAL_MAX_WIDTH)
    width = DIAL_MAX_WIDTH;

  double max_height = calculate_dial_height(DIAL_MAX_WIDTH);
  if (height > max_height)
    height = max_height;

  // calculate dial radius
  double radius_from_width = width / 2;
  double radius_from_height = calculate_dial_radius_from_height(height);

  dial->radius = radius_from_width < radius_from_height ?
    radius_from_width : radius_from_height;
  dial->radius -= 0.5;

  // calculate center of dial
  double angle = (360 - TOTAL_ROTATION_DEGREES) / 2.0 * M_PI / 180.0;
  double y_offset = dial->radius * cos(angle);

  dial->cx = dial->w / 2;
  dial->cy = (dial->h / 2.0) + (dial->radius - y_offset) / 2.0 - 0.5;

  dial->slider_thickness = dial->radius / 2.2;
  dial->knob_radius = dial->radius - dial->slider_thickness;
  dial->slider_radius = dial->radius - dial->slider_thickness / 2;
  dial->background_radius = dial->slider_radius + dial->slider_thickness / 4;

  // calculate zero_db marker position
  double zero_db = gtk_dial_get_zero_db(dial);

  if (zero_db != -G_MAXDOUBLE) {
    double zero_db_valp = calc_taper(dial, zero_db);
    double zero_db_angle = calc_val(zero_db_valp, ANGLE_START, ANGLE_END);

    dial->zero_db_x = cos(zero_db_angle) * dial->slider_radius + dial->cx;
    dial->zero_db_y = sin(zero_db_angle) * dial->slider_radius + dial->cy;
  }

  // generate cairo fill patterns
  for (int focus = 0; focus <= 1; focus++) {
    for (int dim = 0; dim <= 1; dim++) {
      if (dial->fill_pattern[focus][dim])
        cairo_pattern_destroy(dial->fill_pattern[focus][dim]);

      cairo_pattern_t *pat = cairo_pattern_create_radial(
        dial->cx + 5, dial->cy + 5, 0, dial->cx, dial->cy, dial->radius
      );
      cairo_add_stop_rgb_dim(pat, 0.0, 0.18, 0.18, 0.20, dim, focus ? 1.65 : 1);
      cairo_add_stop_rgb_dim(pat, 0.4, 0.18, 0.18, 0.20, dim, focus ? 1.65 : 1);
      cairo_add_stop_rgb_dim(pat, 1.0, 0.40, 0.40, 0.42, dim, focus ? 1.25 : 1);

      dial->fill_pattern[focus][dim] = pat;
    }
  }

  // generate cairo outline pattern
  for (int dim = 0; dim <= 1; dim++) {
    if (dial->outline_pattern[dim])
      cairo_pattern_destroy(dial->outline_pattern[dim]);

    cairo_pattern_t *pat = cairo_pattern_create_linear(
      dial->cx - dial->radius / 2,
      dial->cy - dial->radius / 2,
      dial->cx + dial->radius / 2,
      dial->cy + dial->radius / 2
    );
    cairo_add_stop_rgb_dim(pat, 0, 0.6, 0.6, 0.6, dim, 1);
    cairo_add_stop_rgb_dim(pat, 1, 0.2, 0.2, 0.2, dim, 1);

    dial->outline_pattern[dim] = pat;
  }

  // init pango layout for peak value
  if (dial->peak_layout)
    g_object_unref(dial->peak_layout);
  if (dial->peak_font_desc)
    pango_font_description_free(dial->peak_font_desc);

  PangoContext *context = gtk_widget_create_pango_context(GTK_WIDGET(dial));
  dial->peak_layout = pango_layout_new(context);
  dial->peak_font_desc = pango_context_get_font_description(context);
  int size = pango_font_description_get_size(dial->peak_font_desc) * 0.6;
  dial->peak_font_desc = pango_font_description_copy(dial->peak_font_desc);
  pango_font_description_set_size(dial->peak_font_desc, size);
  pango_layout_set_font_description(dial->peak_layout, dial->peak_font_desc);
  g_object_unref(context);

  // calculate level meter breakpoint angles
  if (dial->level_breakpoint_angles)
    free(dial->level_breakpoint_angles);

  if (dial->level_breakpoints_count) {
    dial->level_breakpoint_angles = malloc(
      dial->level_breakpoints_count * sizeof(double)
    );
    for (int i = 0; i < dial->level_breakpoints_count; i++) {
      double valp = calc_taper(dial, dial->level_breakpoints[i]);
      dial->level_breakpoint_angles[i] =
        calc_val(valp, ANGLE_START, ANGLE_END);
    }
  }

  return 1;
}

static void update_dial_values(GtkDial *dial) {
  dial->valp = calc_taper(dial, gtk_adjustment_get_value(dial->adj));
  dial->angle = calc_val(dial->valp, ANGLE_START, ANGLE_END);
  dial->slider_cx = cos(dial->angle) * dial->slider_radius + dial->cx;
  dial->slider_cy = sin(dial->angle) * dial->slider_radius + dial->cy;

  if (!dial->peak_hold)
    return;

  double peak_valp = calc_taper(dial, dial->current_peak);
  dial->peak_angle = calc_val(peak_valp, ANGLE_START, ANGLE_END);
}

static double pdist2(double x1, double y1, double x2, double y2) {
  double dx = x2 - x1;
  double dy = y2 - y1;

  return dx * dx + dy * dy;
}

static gboolean circle_contains_point(
  double cx,
  double cy,
  double r,
  double px,
  double py
) {
  return pdist2(cx, cy, px, py) <= r * r;
}

// END SECTION HELPERS

static void gtk_dial_class_init(GtkDialClass *klass) {
  GtkWidgetClass *w_class = GTK_WIDGET_CLASS(klass);
  GObjectClass *g_class = G_OBJECT_CLASS(klass);
  GtkWidgetClass *p_class = GTK_WIDGET_CLASS(gtk_dial_parent_class);

  g_class->set_property = &gtk_dial_set_property;
  g_class->get_property = &gtk_dial_get_property;
  g_class->dispose = &gtk_dial_dispose;

  w_class->size_allocate = p_class->size_allocate;
  w_class->measure = &dial_measure;
  w_class->snapshot = &dial_snapshot;
  w_class->grab_focus = p_class->grab_focus;
  w_class->focus = p_class->focus;

  klass->move_slider = &gtk_dial_move_slider;
  klass->value_changed = NULL;

  gtk_widget_class_set_css_name(w_class, "dial");

  /**
   * GtkDial:adjustment: (attributes org.gtk.Method.get=gtk_dial_get_adjustment org.gtk.Method.set=gtk_dial_set_adjustment)
   *
   * The GtkAdjustment that contains the current value of this dial object.
   */
  properties[PROP_ADJUSTMENT] = g_param_spec_object(
    "adjustment",
    "Adjustment",
    "The GtkAdjustment that contains the current value of this dial object",
    GTK_TYPE_ADJUSTMENT,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT
  );

  /**
   * GtkDial:round_digits: (attributes org.gtk.Method.get=gtk_dial_get_round_digits org.gtk.Method.set=gtk_dial_set_round_digits)
   *
   * Limits the number of decimal points this GtkDial will store (default 0: integers).
   */
  properties[PROP_ROUND_DIGITS] = g_param_spec_int(
    "round_digits",
    "RoundDigits",
    "Limits the number of decimal points this GtkDial will store",
    -1, 1000,
    -1,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT
  );

  /**
   * GtkDial:zero_db: (attributes org.gtk.Method.get=gtk_dial_get_zero_db org.gtk.Method.set=gtk_dial_set_zero_db)
   *
   * The zero-dB value of the dial.
   */
  properties[PROP_ZERO_DB] = g_param_spec_double(
    "zero_db",
    "ZerodB",
    "The zero-dB value of the dial",
    -G_MAXDOUBLE, G_MAXDOUBLE,
    -G_MAXDOUBLE,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT
  );

  /**
   * GtkDial:off_db: (attributes org.gtk.Method.get=gtk_dial_get_off_db org.gtk.Method.set=gtk_dial_set_off_db)
   *
   * Values above the lower value of the adjustment up to this value
   * will be considered as the minimum value + 1 (so will display as
   * just-above-zero).
   */
  properties[PROP_OFF_DB] = g_param_spec_double(
    "off_db",
    "OffdB",
    "Values up to this value will be considered as almost-silence",
    -G_MAXDOUBLE, G_MAXDOUBLE,
    -G_MAXDOUBLE,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT
  );

  /**
   * GtkDial:taper: (attributes org.gtk.Method.get=gtk_dial_get_taper org.gtk.Method.set=gtk_dial_set_taper)
   *
   * The taper of the dial.
   */
  properties[PROP_TAPER] = g_param_spec_int(
    "taper",
    "Taper",
    "The taper of the dial",
    GTK_DIAL_TAPER_LINEAR, GTK_DIAL_TAPER_LOG,
    GTK_DIAL_TAPER_LINEAR,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT
  );

  /**
   * GtkDial:can-control: (attributes org.gtk.Method.get=gtk_dial_get_can_control org.gtk.Method.set=gtk_dial_set_can_control)
   *
   * Whether the dial can be controlled by the user (even though it
   * might sometimes be insensitive).
   */
  properties[PROP_CAN_CONTROL] = g_param_spec_boolean(
    "can-control",
    "CanControl",
    "Whether the dial can be controlled by the user",
    TRUE,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT
  );

  /**
   * GtkDial:peak-hold: (attributes org.gtk.Method.get=gtk_dial_get_peak_hold org.gtk.Method.set=gtk_dial_set_peak_hold)
   *
   * The number of milliseconds to hold the peak value.
   */
  properties[PROP_PEAK_HOLD] = g_param_spec_int(
    "peak-hold",
    "PeakHold",
    "The number of milliseconds to hold the peak value",
    0, 1000,
    0,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT
  );

  g_object_class_install_properties(g_class, LAST_PROP, properties);

  /**
   * GtkDial::value-changed:
   * @dial: the `GtkDial` that received the signal
   *
   * Emitted when the dial value changes.
   */
  signals[VALUE_CHANGED] = g_signal_new(
    "value-changed",
    G_TYPE_FROM_CLASS(g_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(GtkDialClass, value_changed),
    NULL, NULL,
    NULL,
    G_TYPE_NONE, 0
  );

 /**
   * GtkDial::move-slider:
   * @Dial: the `GtkDial` that received the signal
   * @step: how to move the slider
   *
   * Virtual function that moves the slider.
   *
   * Used for keybindings.
   */
  signals[MOVE_SLIDER] = g_signal_new(
    "move-slider",
    G_TYPE_FROM_CLASS(g_class),
    G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
    G_STRUCT_OFFSET(GtkDialClass, move_slider),
    NULL, NULL,
    NULL,
    G_TYPE_NONE, 1,
    GTK_TYPE_SCROLL_TYPE
  );

  add_slider_binding(w_class, binding_set, GDK_KEY_Left, 0, GTK_SCROLL_STEP_LEFT);
  add_slider_binding(w_class, binding_set, GDK_KEY_Down, 0, GTK_SCROLL_STEP_LEFT);
  add_slider_binding(w_class, binding_set, GDK_KEY_Right, 0, GTK_SCROLL_STEP_RIGHT);
  add_slider_binding(w_class, binding_set, GDK_KEY_Up, 0, GTK_SCROLL_STEP_RIGHT);
  add_slider_binding(w_class, binding_set, GDK_KEY_Page_Up, 0, GTK_SCROLL_PAGE_RIGHT);
  add_slider_binding(w_class, binding_set, GDK_KEY_Page_Down, 0, GTK_SCROLL_PAGE_LEFT);
  add_slider_binding(w_class, binding_set, GDK_KEY_Home, 0, GTK_SCROLL_START);
  add_slider_binding(w_class, binding_set, GDK_KEY_End, 0, GTK_SCROLL_END);
}

static void gtk_dial_focus_change_cb(
  GtkEventControllerFocus *controller, GtkDial *dial
) {
  gtk_widget_queue_draw(GTK_WIDGET(dial));
}

static void gtk_dial_notify_sensitive_cb(
  GObject    *object,
  GParamSpec *pspec,
  GtkDial    *dial
) {
  gtk_widget_queue_draw(GTK_WIDGET(dial));
}

static void gtk_dial_init(GtkDial *dial) {
  gtk_widget_set_focusable(GTK_WIDGET(dial), TRUE);

  dial->adj = NULL;

  dial->grab = GRAB_NONE;
  dial->drag_gesture = gtk_gesture_drag_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dial->drag_gesture), 0);
  g_signal_connect(
    dial->drag_gesture, "drag-begin",
    G_CALLBACK(gtk_dial_drag_gesture_begin), dial
  );
  g_signal_connect(
    dial->drag_gesture, "drag-update",
    G_CALLBACK(gtk_dial_drag_gesture_update), dial
  );
  g_signal_connect(
    dial->drag_gesture, "drag-end",
    G_CALLBACK(gtk_dial_drag_gesture_end), dial
  );
  gtk_widget_add_controller(
    GTK_WIDGET(dial), GTK_EVENT_CONTROLLER(dial->drag_gesture)
  );

  dial->click_gesture = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dial->click_gesture), 0);
  g_signal_connect(
    dial->click_gesture, "pressed",
    G_CALLBACK(gtk_dial_click_gesture_pressed), dial
  );
  gtk_widget_add_controller(
    GTK_WIDGET(dial), GTK_EVENT_CONTROLLER(dial->click_gesture)
  );
  gtk_gesture_group(dial->click_gesture, dial->drag_gesture);

  dial->scroll_controller = gtk_event_controller_scroll_new(
    GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES
  );
  g_signal_connect(
    dial->scroll_controller, "scroll",
    G_CALLBACK(gtk_dial_scroll_controller_scroll), dial
  );
  gtk_widget_add_controller(GTK_WIDGET(dial), dial->scroll_controller);

  GtkEventController *controller = gtk_event_controller_focus_new();
  g_signal_connect(
    controller, "enter", G_CALLBACK(gtk_dial_focus_change_cb), dial
  );
  g_signal_connect(
    controller, "leave", G_CALLBACK(gtk_dial_focus_change_cb), dial
  );
  gtk_widget_add_controller(GTK_WIDGET(dial), controller);

  g_signal_connect(
    dial, "notify::sensitive", G_CALLBACK(gtk_dial_notify_sensitive_cb), dial
  );

  dial->current_peak = -INFINITY;
  dial->hist_head = 0;
  dial->hist_tail = 0;
  dial->hist_count = 0;
}

static void dial_measure(
  GtkWidget      *widget,
  GtkOrientation  orientation,
  int             for_size,
  int            *minimum,
  int            *natural,
  int            *minimum_baseline,
  int            *natural_baseline
) {
  if (orientation == GTK_ORIENTATION_HORIZONTAL) {
    *minimum = DIAL_MIN_WIDTH;
    *natural = DIAL_MAX_WIDTH;
  } else {
    *minimum = calculate_dial_height(DIAL_MIN_WIDTH);
    *natural = calculate_dial_height(DIAL_MAX_WIDTH);
  }
  *minimum_baseline = -1;
  *natural_baseline = -1;
}

// internal replacement for cairo_set_source_rgba() that dims the
// color if the widget is insensitive
static void cairo_set_source_rgba_dim(
  cairo_t *cr,
  double   r,
  double   g,
  double   b,
  double   a,
  int      dim
) {
  if (dim)
    cairo_set_source_rgba(cr, r * 0.5, g * 0.5, b * 0.5, a);
  else
    cairo_set_source_rgba(cr, r, g, b, a);
}

static void draw_peak(GtkDial *dial, cairo_t *cr, double radius) {

  double angle_start = dial->peak_angle - M_PI / 180;
  if (angle_start < ANGLE_START)
    return;

  // determine the colour of the peak
  int count = dial->level_breakpoints_count;

  // if there are no colours, don't draw the peak
  if (!count)
    return;

  int i;

  for (i = 0; i < count - 1; i++)
    if (dial->current_peak < dial->level_breakpoints[i + 1])
      break;

  const double *colours = &dial->level_colours[i * 3];

  cairo_set_source_rgba_dim(
    cr, colours[0], colours[1], colours[2], 0.5, dial->dim
  );
  cairo_set_line_width(cr, 2);
  cairo_arc(cr, dial->cx, dial->cy, radius, ANGLE_START, dial->peak_angle);
  cairo_stroke(cr);

  cairo_set_source_rgba_dim(
    cr, colours[0], colours[1], colours[2], 1, dial->dim
  );
  cairo_set_line_width(cr, 4);
  cairo_arc(cr, dial->cx, dial->cy, radius, angle_start, dial->peak_angle);
  cairo_stroke(cr);
}

static void show_peak_value(GtkDial *dial, cairo_t *cr) {
  double value = round(dial->current_peak);

  if (value <= gtk_adjustment_get_lower(dial->adj))
    return;

  char s[20];
  char *p = s;
  if (value < 0)
    p += sprintf(p, "−");
  snprintf(p, 10, "%.0f", fabs(value));

  pango_layout_set_text(dial->peak_layout, s, -1);

  int width, height;
  pango_layout_get_pixel_size(dial->peak_layout, &width, &height);

  cairo_set_source_rgba_dim(cr, 1, 1, 1, 0.5, dial->dim);

  cairo_move_to(
    cr,
    dial->cx - width / 2 - 1,
    dial->cy - height / 2
  );

  pango_cairo_show_layout(cr, dial->peak_layout);
}

static void draw_slider(
  GtkDial *dial,
  cairo_t *cr,
  double   radius,
  double   thickness,
  double   alpha
) {
  cairo_set_line_width(cr, thickness);

  int count = dial->level_breakpoints_count;

  if (!count) {
    cairo_arc(cr, dial->cx, dial->cy, radius, ANGLE_START, dial->angle);
    cairo_set_source_rgba_dim(cr, 1, 1, 1, alpha, dial->dim);
    cairo_stroke(cr);
    return;
  }

  // if the last breakpoint is at the upper limit, then the maximum
  // value is displayed with the whole slider that colours
  if (dial->level_breakpoint_angles[count - 1] == ANGLE_END &&
      dial->angle == ANGLE_END) {
    const double *colours = &dial->level_colours[(count - 1) * 3];

    cairo_set_source_rgba_dim(
      cr,
      colours[0], colours[1], colours[2],
      alpha,
      dial->dim
    );

    cairo_arc(cr, dial->cx, dial->cy, radius, ANGLE_START, ANGLE_END);
    cairo_stroke(cr);
    return;
  }

  for (int i = 0; i < count; i++) {
    const double *colours = &dial->level_colours[i * 3];

    cairo_set_source_rgba_dim(
      cr,
      colours[0], colours[1], colours[2],
      alpha,
      dial->dim
    );

    double angle_start = dial->level_breakpoint_angles[i];
    double angle_end =
      i == count - 1
        ? ANGLE_END
        : dial->level_breakpoint_angles[i + 1];

    if (dial->angle < angle_end) {
      cairo_arc(cr, dial->cx, dial->cy, radius, angle_start, dial->angle);
      cairo_stroke(cr);
      return;
    }

    cairo_arc(cr, dial->cx, dial->cy, radius, angle_start, angle_end);
    cairo_stroke(cr);
  }
}

static void dial_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  GtkDial *dial = GTK_DIAL(widget);

  if (update_dial_properties(dial))
    update_dial_values(dial);

  cairo_t *cr = gtk_snapshot_append_cairo(
    snapshot,
    &GRAPHENE_RECT_INIT(0, 0, dial->w, dial->h)
  );

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

  // background line
  cairo_arc(
    cr, dial->cx, dial->cy, dial->slider_radius, ANGLE_START, ANGLE_END
  );
  cairo_set_line_width(cr, 2);
  cairo_set_source_rgba_dim(cr, 1, 1, 1, 0.17, dial->dim);
  cairo_stroke(cr);

  if (dial->valp > 0.0) {
    // outside value shadow
    draw_slider(
      dial, cr, dial->background_radius, dial->slider_thickness / 2, 0.1
    );

    // value blur 2
    draw_slider(dial, cr, dial->slider_radius, 6, 0.3);
  }

  // peak hold
  if (dial->peak_hold)
    draw_peak(dial, cr, dial->slider_radius);

  // draw line to zero db
  double zero_db = gtk_dial_get_zero_db(dial);
  if (zero_db != -G_MAXDOUBLE) {
    cairo_move_to(cr, dial->cx, dial->cy);
    cairo_line_to(cr, dial->zero_db_x, dial->zero_db_y);
    cairo_set_line_width(cr, 2);
    cairo_set_source_rgba_dim(cr, 1, 1, 1, 0.17, dial->dim);
    cairo_stroke(cr);
  }

  // marker when at min or max
  if (gtk_dial_get_value(dial) == gtk_adjustment_get_lower(dial->adj) ||
      gtk_dial_get_value(dial) == gtk_adjustment_get_upper(dial->adj)) {
    cairo_move_to(cr, dial->cx, dial->cy);
    cairo_line_to(cr, dial->slider_cx, dial->slider_cy);
    cairo_set_line_width(cr, 2);
    cairo_set_source_rgba_dim(cr, 1, 1, 1, 0.5, dial->dim);
    cairo_stroke(cr);
  }

  if (dial->valp > 0.0) {
    // value blur 1
    draw_slider(dial, cr, dial->slider_radius, 4, 0.5);

    // value
    draw_slider(dial, cr, dial->slider_radius, 2, 1);
  }

  // fill the circle
  int has_focus = gtk_widget_has_focus(GTK_WIDGET(dial));
  cairo_set_source(cr, dial->fill_pattern[has_focus][dial->dim]);
  cairo_arc(cr, dial->cx, dial->cy, dial->knob_radius, 0, 2 * M_PI);
  cairo_fill(cr);

  // draw the circle
  cairo_set_source(cr, dial->outline_pattern[dial->dim]);
  cairo_arc(cr, dial->cx, dial->cy, dial->knob_radius, 0, 2 * M_PI);
  cairo_set_line_width(cr, 2);
  cairo_stroke(cr);

  // show the peak value
  if (dial->peak_hold)
    show_peak_value(dial, cr);

  // if focussed
  if (has_focus) {
    cairo_set_source_rgba(cr, 1, 0.125, 0.125, 0.5);
    cairo_set_line_width(cr, 2);
    cairo_arc(cr, dial->cx, dial->cy, dial->knob_radius + 2, 0, 2 * M_PI);
    cairo_stroke(cr);
  }

  cairo_destroy(cr);
}

GtkWidget *gtk_dial_new(GtkAdjustment *adjustment) {
  g_return_val_if_fail(
    adjustment == NULL || GTK_IS_ADJUSTMENT(adjustment),
    NULL
  );

  return g_object_new(
    GTK_TYPE_DIAL,
    "adjustment", adjustment,
    NULL
  );
}

GtkWidget *gtk_dial_new_with_range(
  double min,
  double max,
  double step,
  double page
) {
  GtkAdjustment *adj;
  int digits;

  g_return_val_if_fail(min < max, NULL);

  adj = gtk_adjustment_new(min, min, max, step, page, 0);

  if (step == 0.0) {
    digits = -1;
  } else if (fabs(step) >= 1.0) {
    digits = 0;
  } else {
    digits = abs((int)floor(log10(fabs(step))));
    if (digits > 5)
      digits = 5;
  }

  return g_object_new(
    GTK_TYPE_DIAL,
    "adjustment",   adj,
    "round_digits", digits,
    NULL
  );
}

static void gtk_dial_set_property(
  GObject      *object,
  guint         prop_id,
  const GValue *value,
  GParamSpec   *pspec
) {
  GtkDial *dial = GTK_DIAL(object);

  switch (prop_id) {
    case PROP_ADJUSTMENT:
      gtk_dial_set_adjustment(dial, g_value_get_object(value));
      break;
    case PROP_ROUND_DIGITS:
      gtk_dial_set_round_digits(dial, g_value_get_int(value));
      break;
    case PROP_ZERO_DB:
      gtk_dial_set_zero_db(dial, g_value_get_double(value));
      break;
    case PROP_OFF_DB:
      gtk_dial_set_off_db(dial, g_value_get_double(value));
      break;
    case PROP_TAPER:
      gtk_dial_set_taper(dial, g_value_get_int(value));
      break;
    case PROP_CAN_CONTROL:
      gtk_dial_set_can_control(dial, g_value_get_boolean(value));
      break;
    case PROP_PEAK_HOLD:
      gtk_dial_set_peak_hold(dial, g_value_get_int(value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void gtk_dial_get_property(
  GObject    *object,
  guint       prop_id,
  GValue     *value,
  GParamSpec *pspec
) {
  GtkDial *dial = GTK_DIAL(object);

  switch (prop_id) {
    case PROP_ADJUSTMENT:
      g_value_set_object(value, dial->adj);
      break;
    case PROP_ROUND_DIGITS:
      g_value_set_int(value, dial->round_digits);
      break;
    case PROP_ZERO_DB:
      g_value_set_double(value, dial->zero_db);
      break;
    case PROP_OFF_DB:
      g_value_set_double(value, dial->off_db);
      break;
    case PROP_TAPER:
      g_value_set_int(value, dial->taper);
      break;
    case PROP_CAN_CONTROL:
      g_value_set_boolean(value, dial->can_control);
      break;
    case PROP_PEAK_HOLD:
      g_value_set_int(value, dial->peak_hold);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

double gtk_dial_get_value(GtkDial *dial) {
  return gtk_adjustment_get_value(dial->adj);
}

void gtk_dial_set_value(GtkDial *dial, double value) {
  if (set_value(dial, value))
    gtk_widget_queue_draw(GTK_WIDGET(dial));
}

void gtk_dial_set_round_digits(GtkDial *dial, int round_digits) {
  dial->round_digits = round_digits;
  gtk_dial_set_value(dial, gtk_dial_get_value(dial));
}

int gtk_dial_get_round_digits(GtkDial *dial) {
  return dial->round_digits;
}

void gtk_dial_set_zero_db(GtkDial *dial, double zero_db) {
  dial->zero_db = zero_db;
  dial->properties_updated = 1;
}

double gtk_dial_get_zero_db(GtkDial *dial) {
  return dial->zero_db;
}

void gtk_dial_set_off_db(GtkDial *dial, double off_db) {
  dial->off_db = off_db;
  dial->properties_updated = 1;
}

double gtk_dial_get_off_db(GtkDial *dial) {
  return dial->off_db;
}

void gtk_dial_set_taper(GtkDial *dial, int taper) {
  dial->taper = taper;
  dial->properties_updated = 1;
}

int gtk_dial_get_taper(GtkDial *dial) {
  return dial->taper;
}

void gtk_dial_set_taper_linear_breakpoints(
  GtkDial      *dial,
  const double *breakpoints,
  const double *outputs,
  int           count
) {
  dial->properties_updated = 1;

  free(dial->taper_breakpoints);
  free(dial->taper_outputs);
  dial->taper_breakpoints = NULL;
  dial->taper_outputs = NULL;
  dial->taper_breakpoints_count = 0;

  if (count < 1)
    return;

  int total_count = count + 2;

  dial->taper_breakpoints = malloc(total_count * sizeof(double));
  dial->taper_outputs = malloc(total_count * sizeof(double));

  dial->taper_breakpoints[0] = 0;
  dial->taper_outputs[0] = 0;

  for (int i = 0; i < count; i++) {
    dial->taper_breakpoints[i + 1] = breakpoints[i];
    dial->taper_outputs[i + 1] = outputs[i];
  }

  dial->taper_breakpoints[total_count - 1] = 1;
  dial->taper_outputs[total_count - 1] = 1;

  dial->taper_breakpoints_count = total_count;
}

void gtk_dial_set_can_control(GtkDial *dial, gboolean can_control) {
  dial->can_control = can_control;
  dial->properties_updated = 1;
}

gboolean gtk_dial_get_can_control(GtkDial *dial) {
  return dial->can_control;
}

void gtk_dial_set_level_meter_colours(
  GtkDial      *dial,
  const int    *breakpoints,
  const double *colours,
  int           count
) {
  dial->level_breakpoints = breakpoints;
  dial->level_colours = colours;
  dial->level_breakpoints_count = count;
  dial->properties_updated = 1;
}

void gtk_dial_set_peak_hold(GtkDial *dial, int peak_hold) {
  dial->peak_hold = peak_hold;
}

int gtk_dial_get_peak_hold(GtkDial *dial) {
  return dial->peak_hold;
}

void gtk_dial_set_adjustment(GtkDial *dial, GtkAdjustment *adj) {
  if (!(adj == NULL || GTK_IS_ADJUSTMENT(adj)))
    return;
  if (dial->adj)
    g_object_unref(dial->adj);
  dial->adj = adj;
  g_object_ref_sink(dial->adj);
  g_signal_emit(dial, signals[VALUE_CHANGED], 0);
  gtk_widget_queue_draw(GTK_WIDGET(dial));
}

GtkAdjustment *gtk_dial_get_adjustment(GtkDial *dial) {
  return dial->adj;
}

static void gtk_dial_add_hist_value(GtkDial *dial, double value) {

  int need_peak_update = 0;

  // remove the oldest value(s) if they are too old or if the history
  // is full
  while (dial->hist_count > 0 &&
         (dial->hist_time[dial->hist_head] < current_time - dial->peak_hold ||
          dial->hist_count == HISTORY_COUNT)) {

    // check if the value removed is the current peak
    if (dial->hist_values[dial->hist_head] >= dial->current_peak)
      need_peak_update = 1;

    // move the head forward
    dial->hist_head = (dial->hist_head + 1) % HISTORY_COUNT;
    dial->hist_count--;
  }

  // recalculate the peak if needed
  if (need_peak_update) {
    dial->current_peak = -INFINITY;
    for (int i = dial->hist_head;
         i != dial->hist_tail;
         i = (i + 1) % HISTORY_COUNT)
      if (dial->hist_values[i] > dial->current_peak)
        dial->current_peak = dial->hist_values[i];
  }

  // add the new value
  dial->hist_values[dial->hist_tail] = value;
  dial->hist_time[dial->hist_tail] = current_time;
  dial->hist_tail = (dial->hist_tail + 1) % HISTORY_COUNT;
  dial->hist_count++;

  // update the peak if needed
  if (value > dial->current_peak)
    dial->current_peak = value;
}

static int set_value(GtkDial *dial, double newval) {
  if (dial->round_digits >= 0) {
    double power;
    int i;

    i = dial->round_digits;
    power = 1;
    while (i--)
      power *= 10;

    newval = floor((newval * power) + 0.5) / power;
  }

  if (newval < gtk_adjustment_get_lower(dial->adj))
    newval = gtk_adjustment_get_lower(dial->adj);
  else if (newval > gtk_adjustment_get_upper(dial->adj))
    newval = gtk_adjustment_get_upper(dial->adj);

  double oldval = gtk_adjustment_get_value(dial->adj);

  double old_peak = dial->current_peak;
  gtk_dial_add_hist_value(dial, newval);

  if (oldval == newval && old_peak == dial->current_peak)
    return 0;

  gtk_adjustment_set_value(dial->adj, newval);
  g_signal_emit(dial, signals[VALUE_CHANGED], 0);

  double old_valp = dial->valp;
  update_dial_values(dial);

  return old_valp != dial->valp || old_peak != dial->current_peak;
}

static void step_back(GtkDial *dial) {
  double newval;

  newval = gtk_adjustment_get_value(dial->adj) - gtk_adjustment_get_step_increment(dial->adj);
  set_value(dial, newval);
}

static void step_forward(GtkDial *dial) {
  double newval;

  newval = gtk_adjustment_get_value(dial->adj) + gtk_adjustment_get_step_increment(dial->adj);
  set_value(dial, newval);
}

static void page_back(GtkDial *dial) {
  double newval;

  newval = gtk_adjustment_get_value(dial->adj) - gtk_adjustment_get_page_increment(dial->adj);
  set_value(dial, newval);
}

static void page_forward(GtkDial *dial) {
  double newval;

  newval = gtk_adjustment_get_value(dial->adj) + gtk_adjustment_get_page_increment(dial->adj);
  set_value(dial, newval);
}

static void scroll_begin(GtkDial *dial) {
  double newval = gtk_adjustment_get_lower(dial->adj);

  set_value(dial, newval);
}

static void scroll_end(GtkDial *dial) {
  double newval = gtk_adjustment_get_upper(dial->adj) - gtk_adjustment_get_page_size(dial->adj);

  set_value(dial, newval);
}

static gboolean should_invert_move(GtkDial *dial, GtkOrientation o) {
  return FALSE;
}

static void gtk_dial_move_slider(GtkDial *dial, GtkScrollType scroll) {
  switch (scroll) {
    case GTK_SCROLL_STEP_LEFT:
      if (should_invert_move(dial, GTK_ORIENTATION_HORIZONTAL))
        step_forward(dial);
      else
        step_back(dial);
      break;

    case GTK_SCROLL_STEP_UP:
      if (should_invert_move(dial, GTK_ORIENTATION_VERTICAL))
        step_forward(dial);
      else
        step_back(dial);
      break;

    case GTK_SCROLL_STEP_RIGHT:
      if (should_invert_move(dial, GTK_ORIENTATION_HORIZONTAL))
        step_back(dial);
      else
        step_forward(dial);
      break;

    case GTK_SCROLL_STEP_DOWN:
      if (should_invert_move(dial, GTK_ORIENTATION_VERTICAL))
        step_back(dial);
      else
        step_forward(dial);
      break;

    case GTK_SCROLL_STEP_BACKWARD:
      step_back(dial);
      break;

    case GTK_SCROLL_STEP_FORWARD:
      step_forward(dial);
      break;

    case GTK_SCROLL_PAGE_LEFT:
      if (should_invert_move(dial, GTK_ORIENTATION_HORIZONTAL))
        page_forward(dial);
      else
        page_back(dial);
      break;

    case GTK_SCROLL_PAGE_UP:
      if (should_invert_move(dial, GTK_ORIENTATION_VERTICAL))
        page_forward(dial);
      else
        page_back(dial);
      break;

    case GTK_SCROLL_PAGE_RIGHT:
      if (should_invert_move(dial, GTK_ORIENTATION_HORIZONTAL))
        page_back(dial);
      else
        page_forward(dial);
      break;

    case GTK_SCROLL_PAGE_DOWN:
      if (should_invert_move(dial, GTK_ORIENTATION_VERTICAL))
        page_back(dial);
      else
        page_forward(dial);
      break;

    case GTK_SCROLL_PAGE_BACKWARD:
      page_back(dial);
      break;

    case GTK_SCROLL_PAGE_FORWARD:
      page_forward(dial);
      break;

    case GTK_SCROLL_START:
      scroll_begin(dial);
      break;

    case GTK_SCROLL_END:
      scroll_end(dial);
      break;

    case GTK_SCROLL_JUMP:
    case GTK_SCROLL_NONE:
    default:
      break;
  }

  gtk_widget_queue_draw(GTK_WIDGET(dial));
}

static void gtk_dial_drag_gesture_begin(
  GtkGestureDrag *gesture,
  double          offset_x,
  double          offset_y,
  GtkDial        *dial
) {
  dial->dvalp = calc_valp(
    gtk_dial_get_value(dial),
    gtk_adjustment_get_lower(dial->adj),
    gtk_adjustment_get_upper(dial->adj)
  );
  gtk_gesture_set_state(dial->drag_gesture, GTK_EVENT_SEQUENCE_CLAIMED);
}

static void gtk_dial_drag_gesture_update(
  GtkGestureDrag *gesture,
  double          offset_x,
  double          offset_y,
  GtkDial        *dial
) {
  double start_x, start_y;

  gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);

  double valp = dial->dvalp - DRAG_FACTOR * (offset_y / dial->h);
  valp = CLAMP(valp, 0.0, 1.0);

  double val = calc_val(
    valp,
    gtk_adjustment_get_lower(dial->adj),
    gtk_adjustment_get_upper(dial->adj)
  );

  set_value(dial, val);
  gtk_widget_queue_draw(GTK_WIDGET(dial));
}

static void gtk_dial_drag_gesture_end(
  GtkGestureDrag *gesture,
  double          offset_x,
  double          offset_y,
  GtkDial        *dial
) {
  dial->grab = GRAB_NONE;
  gtk_widget_queue_draw(GTK_WIDGET(dial));
}

static void gtk_dial_click_gesture_pressed(
  GtkGestureClick *gesture,
  int              n_press,
  double           x,
  double           y,
  GtkDial         *dial
) {

  // on double (or more) click, toggle between lower and zero_db value
  if (n_press >= 2) {
    double lower = gtk_adjustment_get_lower(dial->adj);

    if (gtk_dial_get_value(dial) != lower)
      set_value(dial, lower);
    else
      set_value(dial, dial->zero_db);

    return;
  }

  if (gtk_widget_get_focus_on_click(GTK_WIDGET(dial)) &&
      !gtk_widget_has_focus(GTK_WIDGET(dial)))
    gtk_widget_grab_focus(GTK_WIDGET(dial));

  if (circle_contains_point(
    dial->slider_cx, dial->slider_cy, dial->radius, x, y
  ))
    dial->grab = GRAB_SLIDER;
  else
    dial->grab = GRAB_NONE;

  gtk_widget_queue_draw(GTK_WIDGET(dial));
  gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static gboolean gtk_dial_scroll_controller_scroll(
  GtkEventControllerScroll *scroll,
  double                    dx,
  double                    dy,
  GtkDial                  *dial
) {
  double delta = dx ? dx : dy;
  double absolute_delta = fabs(delta);

  if (absolute_delta > 1)
    delta *= absolute_delta;

  double step = -gtk_adjustment_get_step_increment(dial->adj) * delta;

  set_value(dial, gtk_adjustment_get_value(dial->adj) + step);
  gtk_widget_queue_draw(GTK_WIDGET(dial));

  return GDK_EVENT_STOP;
}

void gtk_dial_dispose(GObject *o) {
  GtkDial *dial = GTK_DIAL(o);

  free(dial->taper_breakpoints);
  dial->taper_breakpoints = NULL;
  free(dial->taper_outputs);
  dial->taper_outputs = NULL;
  dial->taper_breakpoints_count = 0;
  free(dial->level_breakpoint_angles);
  dial->level_breakpoint_angles = NULL;

  for (int focus = 0; focus <= 1; focus++)
    for (int dim = 0; dim <= 1; dim++)
      if (dial->fill_pattern[focus][dim])
        cairo_pattern_destroy(dial->fill_pattern[focus][dim]);

  for (int dim = 0; dim <= 1; dim++)
    if (dial->outline_pattern[dim])
      cairo_pattern_destroy(dial->outline_pattern[dim]);

  if (dial->peak_layout)
    g_object_unref(dial->peak_layout);
  if (dial->peak_font_desc)
    pango_font_description_free(dial->peak_font_desc);

  g_object_unref(dial->adj);
  dial->adj = NULL;
  G_OBJECT_CLASS(gtk_dial_parent_class)->dispose(o);
}
