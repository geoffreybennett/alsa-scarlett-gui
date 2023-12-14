// SPDX-FileCopyrightText: 2021 Stiliyan Varbanov <https://www.fiverr.com/stilvar>
// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
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

static void set_value(GtkDial *dial, double newval);

static void gtk_dial_set_property(GObject        *object,
                                  guint           prop_id,
                                  const GValue   *value,
                                  GParamSpec     *pspec);
static void gtk_dial_get_property(GObject        *object,
                                  guint           prop_id,
                                  GValue         *value,
                                  GParamSpec     *pspec);

static void gtk_dial_move_slider(GtkDial     *dial,
                             GtkScrollType scroll);
static void
gtk_dial_drag_gesture_begin (GtkGestureDrag *gesture,
                              double          offset_x,
                              double          offset_y,
                              GtkDial       *dial);

static void
gtk_dial_drag_gesture_update (GtkGestureDrag *gesture,
                              double          offset_x,
                              double          offset_y,
                              GtkDial       *dial);

static void
gtk_dial_drag_gesture_end (GtkGestureDrag *gesture,
                              double          offset_x,
                              double          offset_y,
                              GtkDial       *dial);

static void
gtk_dial_click_gesture_pressed (GtkGestureClick *gesture,
                                 int              n_press,
                                 double           x,
                                 double           y,
                                 GtkDial        *dial);

static gboolean
gtk_dial_scroll_controller_scroll (GtkEventControllerScroll *scroll,
                                    double                    dx,
                                    double                    dy,
                                    GtkDial                  *dial);

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

typedef unsigned char guint8;
typedef size_t gsize;

struct DialColors {
  GdkRGBA trough_border,
          trough_bg,
          trough_fill,
          pointer;
};

struct _GtkDial {
  GtkWidget parent_instance;
  GtkAdjustment *adj;

  GtkGesture *drag_gesture, *click_gesture;
  GtkEventController *scroll_controller;
  e_grab grab;

  struct DialColors colors;

  int round_digits;
  double zero_db;

  double slider_cx, slider_cy, dvalp;
};

G_DEFINE_TYPE (GtkDial, gtk_dial, GTK_TYPE_WIDGET)

static void dial_snapshot (GtkWidget *widget, GtkSnapshot *snapshot);
static void dial_measure(GtkWidget *widget,
                    GtkOrientation orientation,
                    int for_size,
                    int *minimum,
                    int *natural,
                    int *minimum_baseline,
                    int *natural_baseline);

#define add_slider_binding(w_class, binding_set, keyval, mask, scroll) \
  gtk_widget_class_add_binding_signal (w_class,       \
                                       keyval, mask,  \
                                       "move-slider", \
                                       "(i)", scroll)

//BEGIN SECTION HELPERS

#define V1x 0.7316888688738209
#define V1y 0.6816387600233341
#define RAD_START (-M_PI-0.75)
#define RAD_END 0.75
#define RAD_SE_DIFF2 ( (2*M_PI+3)/2 )
#define DRAG_FACTOR 0.5

static inline double calc_valp(double val, double mn, double mx)
{
    return (val - mn)/(mx-mn);
}

static inline double calc_valp_log(double val, double mn, double mx)
{
    if (val <= mn)
        return 0.0;
    if (val >= mx)
        return 1.0;

    // convert val from mn..mx to 0..1
    val = (val - mn)/(mx-mn);

    // 10^(val - 1) converts it to 0.1..1 with a nice curve
    val = pow(10, val - 1);

    // convert to 0..1 again
    return (val - 0.1) / 0.9;
}

static inline double calc_val(double valp, double mn, double mx)
{
    return (mx-mn)*valp+mn;
}

struct dial_properties
{
    double w;
    double h;
    double radius;
    double thickness;
    double cx;
    double cy;
    double valp;
    double slider_radius;
    double slider_cx;
    double slider_cy;
    double start_x;
    double start_y;
    double end_x;
    double end_y;
};

static void get_dial_properties(GtkDial *dial,
                                struct dial_properties *props)
{
    props->w = gtk_widget_get_width (GTK_WIDGET(dial) );
    props->h = gtk_widget_get_height (GTK_WIDGET(dial) );

    props->cx = props->w / 2;
    props->cy = props->h / 2;
    props->radius = props->h < props->w ? props->h / 2 - 2 : props->w / 2 - 2;
    props->thickness = 10;
    props->slider_radius = props->thickness * 1.5;
    props->radius -= props->slider_radius / 2;

    double mn = dial->adj ? gtk_adjustment_get_lower(dial->adj) : 0;
    double mx = dial->adj ? gtk_adjustment_get_upper(dial->adj) : 1;
    double value = dial->adj ? gtk_adjustment_get_value(dial->adj) : 0.25;
    props->valp = calc_valp_log(value, mn, mx);

    double SIN = sin( (RAD_SE_DIFF2*(props->valp) ) );
    double COS = cos( (RAD_SE_DIFF2*(props->valp) ) );

    props->slider_cx = (-V1y*SIN - V1x*COS)*(2*(props->radius)-(props->thickness) )/2 + (props->cx);
    props->slider_cy = (V1y*COS - V1x*SIN)*(2*(props->radius)-(props->thickness) )/2 + (props->cy);

    props->start_x = V1x*(2*(props->radius)-(props->thickness) )/2 + (props->cx);
    props->start_y = V1y*(2*(props->radius)-(props->thickness) )/2 + (props->cy);

    SIN = -0.9974949866040545;
    COS = -0.07073720166770303;

    props->end_x = (-V1y*SIN - V1x*COS)*(2*(props->radius)-(props->thickness) )/2 + (props->cx);
    props->end_y = (V1y*COS - V1x*SIN)*(2*(props->radius)-(props->thickness) )/2 + (props->cy);
}

static inline double pdist2(double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1;
    double dy = y2 - y1;

    return dx*dx + dy*dy;
}

static inline gboolean circle_contains_point(double cx, double cy, double r, double px, double py)
{
    return pdist2(cx,cy, px,py) <= r*r;
}
//END SECTION HELPERS

static void gtk_dial_class_init(GtkDialClass *klass)
{
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

  /**
   * GtkDial:adjustment: (attributes org.gtk.Method.get=gtk_dial_get_adjustment org.gtk.Method.set=gtk_dial_set_adjustment)
   *
   * The GtkAdjustment that contains the current value of this range object.
   */
    properties[PROP_ADJUSTMENT] =
        g_param_spec_object ("adjustment",
                        "Adjustment",
                        "The GtkAdjustment that contains the current value of this range object",
                        GTK_TYPE_ADJUSTMENT,
                        G_PARAM_READWRITE|G_PARAM_CONSTRUCT);

  /**
   * GtkDial:round_digits: (attributes org.gtk.Method.get=gtk_dial_get_round_digits org.gtk.Method.set=gtk_dial_set_round_digits)
   *
   * Limits the number of decimal points this GtkDial will store (default 0: integers).
   */
    properties[PROP_ROUND_DIGITS] =
        g_param_spec_int("round_digits",
                        "RoundDigits",
                        "Limits the number of decimal points this GtkDial will store",
                        -1, 1000,
                        -1,
                        G_PARAM_READWRITE|G_PARAM_CONSTRUCT);

  /**
   * GtkDial:zero_db: (attributes org.gtk.Method.get=gtk_dial_get_zero_db org.gtk.Method.set=gtk_dial_set_zero_db)
   *
   * Limits the number of decimal points this GtkDial will store (default 0: integers).
   */
    properties[PROP_ZERO_DB] =
        g_param_spec_double("zero_db",
                        "ZerodB",
                        "The zero-dB value of the dial",
                        -G_MAXDOUBLE, G_MAXDOUBLE,
                        0.0,
                        G_PARAM_READWRITE|G_PARAM_CONSTRUCT);

    g_object_class_install_properties(g_class, LAST_PROP, properties);

  /**
   * GtkRange::value-changed:
   * @range: the `GtkRange` that received the signal
   *
   * Emitted when the range value changes.
   */
  signals[VALUE_CHANGED] =
    g_signal_new ("value-changed",
                  G_TYPE_FROM_CLASS (g_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GtkDialClass, value_changed),
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 0);

      /**
   * GtkDial::move-slider:
   * @Dial: the `GtkDial` that received the signal
   * @step: how to move the slider
   *
   * Virtual function that moves the slider.
   *
   * Used for keybindings.
   */
  signals[MOVE_SLIDER] =
    g_signal_new ("move-slider",
                  G_TYPE_FROM_CLASS (g_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (GtkDialClass, move_slider),
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 1,
                  GTK_TYPE_SCROLL_TYPE);

    add_slider_binding (w_class, binding_set, GDK_KEY_Left, 0,
                      GTK_SCROLL_STEP_LEFT);
    add_slider_binding (w_class, binding_set, GDK_KEY_Down, 0,
                      GTK_SCROLL_STEP_LEFT);
    add_slider_binding (w_class, binding_set, GDK_KEY_Right, 0,
                      GTK_SCROLL_STEP_RIGHT);
    add_slider_binding (w_class, binding_set, GDK_KEY_Up, 0,
                      GTK_SCROLL_STEP_RIGHT);
}

static void gtk_dial_focus_change_cb(
  GtkEventControllerFocus *controller, GtkDial *dial
) {
  gtk_widget_queue_draw(GTK_WIDGET(dial));
}

static void gtk_dial_init(GtkDial *dial)
{
//    gtk_dial_set_style(dial, "#cdc7c2", "white", "#3584e4");
    gtk_dial_set_style(dial, "#cdc7c2", "#f0f0f0", "#3584e4", "#808080");
    //gdk_rgba_parse(&dial->colors.trough_border, "#cdc7c2");
    gtk_widget_set_focusable (GTK_WIDGET (dial), TRUE);
    //gtk_widget_set_parent(dial->slider_container, GTK_WIDGET(dial) );

    dial->adj = NULL;

    dial->slider_cx = gtk_widget_get_width(GTK_WIDGET(dial) ) / 2.0;
    dial->slider_cy = 0;

    dial->grab = GRAB_NONE;
    dial->drag_gesture = gtk_gesture_drag_new();
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (dial->drag_gesture), 0);
    g_signal_connect (dial->drag_gesture, "drag-begin",
                    G_CALLBACK (gtk_dial_drag_gesture_begin), dial);
    g_signal_connect (dial->drag_gesture, "drag-update",
                    G_CALLBACK (gtk_dial_drag_gesture_update), dial);
    g_signal_connect (dial->drag_gesture, "drag-end",
                    G_CALLBACK (gtk_dial_drag_gesture_end), dial);
    gtk_widget_add_controller (GTK_WIDGET (dial), GTK_EVENT_CONTROLLER (dial->drag_gesture) );

    dial->click_gesture = gtk_gesture_click_new();
    //gtk_gesture_long_press_set_delay_factor (GTK_GESTURE_LONG_PRESS (dial->click_gesture), 0.1);
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (dial->click_gesture), 0);
    g_signal_connect (dial->click_gesture, "pressed",
                    G_CALLBACK (gtk_dial_click_gesture_pressed), dial);
    gtk_widget_add_controller (GTK_WIDGET (dial), GTK_EVENT_CONTROLLER (dial->click_gesture) );
    gtk_gesture_group (dial->click_gesture, dial->drag_gesture);

    dial->scroll_controller = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    g_signal_connect (dial->scroll_controller, "scroll",
                        G_CALLBACK (gtk_dial_scroll_controller_scroll), dial);
    gtk_widget_add_controller (GTK_WIDGET (dial), dial->scroll_controller);

    GtkEventController *controller = gtk_event_controller_focus_new ();
    g_signal_connect (controller, "enter", G_CALLBACK (gtk_dial_focus_change_cb), dial);
    g_signal_connect (controller, "leave", G_CALLBACK (gtk_dial_focus_change_cb), dial);
    gtk_widget_add_controller (GTK_WIDGET (dial), controller);
}

static void dial_measure(GtkWidget *widget,
                    GtkOrientation orientation,
                    int for_size,
                    int *minimum,
                    int *natural,
                    int *minimum_baseline,
                    int *natural_baseline)
{
    *minimum = 50;
    *natural = 50;
    *minimum_baseline = for_size;
    *natural_baseline = for_size;
}

static void dial_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    GtkDial *dial = GTK_DIAL(widget);

    struct dial_properties p;
    get_dial_properties(dial, &p);
    p.valp = CLAMP(p.valp, 0.0001, 1.0);

    cairo_t *cr = gtk_snapshot_append_cairo(snapshot, &GRAPHENE_RECT_INIT(0, 0, p.w, p.h) );

    // draw border
    cairo_set_line_width(cr, gtk_widget_has_focus(widget) ? 5 : 2);
    gdk_cairo_set_source_rgba(cr, &dial->colors.trough_border);
    cairo_arc(cr, p.cx, p.cy, p.radius-p.thickness, RAD_START, RAD_END/*8*M_PI/5*/);
    cairo_line_to(cr, V1x*(p.radius-p.thickness) + p.cx, V1y*(p.radius-p.thickness) + p.cy);
    cairo_arc_negative(cr, p.cx, p.cy, p.radius, RAD_END, RAD_START/*8*M_PI/5*/);
    cairo_close_path(cr);
    cairo_stroke(cr);

    // bg trough
    cairo_arc(cr, p.cx, p.cy, (2*p.radius-p.thickness)/2.0, RAD_START, RAD_END/*8*M_PI/5*/);
    cairo_set_line_width(cr, p.thickness);
    gdk_cairo_set_source_rgba(cr, &dial->colors.trough_bg);
    cairo_stroke(cr);

    // fill trough
    cairo_arc(cr, p.cx, p.cy, (2*p.radius-p.thickness)/2.0, RAD_START, RAD_END - (1.0-p.valp)*(RAD_END-RAD_START)/*8*M_PI/5*/);
    cairo_set_line_width(cr, p.thickness);
    gdk_cairo_set_source_rgba(cr, &dial->colors.trough_fill);
    cairo_stroke(cr);

    // pointer
    gdk_cairo_set_source_rgba(cr, &dial->colors.pointer);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, p.cx, p.cy);
    cairo_line_to(cr, p.slider_cx, p.slider_cy);
    cairo_stroke(cr);

    cairo_destroy(cr);

//    //FILL SLIDER
//    cairo_arc(cr, p.slider_cx, p.slider_cy, p.slider_radius, 0, 2*M_PI);
//    gdk_cairo_set_source_rgba(cr, &dial->colors.trough_bg);
//    cairo_fill(cr);
//
//    //STROKE SLIDER
//    GdkRGBA *scolor = dial->grab != GRAB_SLIDER ? &dial->colors.trough_border : &dial->colors.trough_fill;
//    cairo_arc(cr, p.slider_cx, p.slider_cy, p.slider_radius, 0, 2*M_PI);
//    cairo_set_line_width(cr, 1);
//    gdk_cairo_set_source_rgba(cr, scolor);
//    cairo_stroke(cr);
}

GtkWidget* gtk_dial_new(GtkAdjustment   *adjustment)
{
  g_return_val_if_fail (adjustment == NULL || GTK_IS_ADJUSTMENT (adjustment),
                        NULL);

  return g_object_new (GTK_TYPE_DIAL,
                       "adjustment",  adjustment,
                       NULL);
}

GtkWidget *
gtk_dial_new_with_range ( double         min,
                          double         max,
                          double         step)
{
  GtkAdjustment *adj;
  int digits;

  g_return_val_if_fail (min < max, NULL);
  g_return_val_if_fail (step != 0.0, NULL);

  adj = gtk_adjustment_new(min, min, max, step, 10 * step, 0);

  if (fabs(step) >= 1.0 || step == 0.0)
    {
      digits = 0;
    }
  else
    {
      digits = abs ( (int) floor(log10(fabs(step) ) ) );
      if (digits > 5)
        digits = 5;
    }
  return g_object_new (GTK_TYPE_DIAL,
                       "adjustment",  adj,
                       "round_digits", 0,
                       NULL);
}

static void  gtk_dial_set_property            (GObject        *object,
                                                   guint           prop_id,
                                                   const GValue   *value,
                                                   GParamSpec     *pspec)
{
    GtkDial *dial = GTK_DIAL(object);

    switch(prop_id)
    {
        case PROP_ADJUSTMENT:
            gtk_dial_set_adjustment(dial, g_value_get_object(value) );
            break;
        case PROP_ROUND_DIGITS:
            gtk_dial_set_round_digits(dial, g_value_get_int(value) );
            break;
        case PROP_ZERO_DB:
            gtk_dial_set_zero_db(dial, g_value_get_double(value) );
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void  gtk_dial_get_property            (GObject        *object,
                                                   guint           prop_id,
                                                GValue   *value,
                                                   GParamSpec     *pspec)
{
    GtkDial *dial = GTK_DIAL(object);

    switch(prop_id)
    {
        case PROP_ADJUSTMENT:
            g_value_set_object(value, dial->adj);
            break;
        case PROP_ROUND_DIGITS:
            g_value_set_int(value, dial->round_digits);
            break;
        case PROP_ZERO_DB:
            g_value_set_double(value, dial->zero_db);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

double            gtk_dial_get_value             (GtkDial      *dial)
{
    return gtk_adjustment_get_value(dial->adj);
}

void            gtk_dial_set_value             (GtkDial      *dial,
                                                  double        value)
{
    set_value(dial, value);
    gtk_widget_queue_draw(GTK_WIDGET(dial) );
}

void               gtk_dial_set_round_digits              (GtkDial      *dial,
                                                            int            round_digits)
{
    dial->round_digits = round_digits;
    gtk_dial_set_value(dial, gtk_dial_get_value(dial) );
}

int                 gtk_dial_get_round_digits              (GtkDial      *dial)
{
    return dial->round_digits;
}

void gtk_dial_set_zero_db(GtkDial *dial, double zero_db)
{
    dial->zero_db = zero_db;
}

double gtk_dial_get_zero_db(GtkDial *dial)
{
    return dial->zero_db;
}

gboolean gtk_dial_set_style(GtkDial *dial,
                        const char *trough_border,
                        const char *trough_bg,
                        const char *trough_fill,
                        const char *pointer)
{
    gboolean out = TRUE;
    if (trough_border)
       out = out && gdk_rgba_parse(&dial->colors.trough_border, trough_border);
    if (trough_bg)
        out = out && gdk_rgba_parse(&dial->colors.trough_bg, trough_bg);
    if (trough_fill)
        out = out && gdk_rgba_parse(&dial->colors.trough_fill, trough_fill);
    if (pointer)
        out = out && gdk_rgba_parse(&dial->colors.pointer, pointer);

    return out;
}

void              gtk_dial_set_adjustment     (GtkDial        *dial,
                                               GtkAdjustment  *adj)
{
    if (!(adj == NULL || GTK_IS_ADJUSTMENT (adj) ) )
        return;
    if (dial->adj)
        g_object_unref(dial->adj);
    dial->adj = adj;
    g_object_ref_sink(dial->adj);
    g_signal_emit(dial, signals[VALUE_CHANGED], 0);
    gtk_widget_queue_draw(GTK_WIDGET(dial) );
}

GtkAdjustment*    gtk_dial_get_adjustment     (GtkDial        *dial)
{
    return dial->adj;
}

static void
set_value (GtkDial *dial, double newval)
{
  if (dial->round_digits >= 0)
  {
      double power;
      int i;

      i = dial->round_digits;
      power = 1;
      while (i--)
        power *= 10;

      newval = floor( (newval * power) + 0.5) / power;
  }
  gtk_adjustment_set_value(dial->adj, newval);
  g_signal_emit(dial, signals[VALUE_CHANGED], 0);
}

static void
step_back (GtkDial *dial)
{
  double newval;

  newval = gtk_adjustment_get_value (dial->adj) - gtk_adjustment_get_step_increment (dial->adj);
  set_value(dial, newval);
}

static void
step_forward (GtkDial *dial)
{
  double newval;

  newval = gtk_adjustment_get_value (dial->adj) + gtk_adjustment_get_step_increment (dial->adj);
  set_value(dial, newval);
}

static void
page_back (GtkDial *dial)
{
  double newval;

  newval = gtk_adjustment_get_value (dial->adj) - gtk_adjustment_get_page_increment (dial->adj);
  set_value(dial, newval);
}

static void
page_forward (GtkDial *dial)
{
  double newval;

  newval = gtk_adjustment_get_value (dial->adj) + gtk_adjustment_get_page_increment (dial->adj);
  set_value(dial, newval);
}

static void
scroll_begin (GtkDial *dial)
{
}

static void scroll_end(GtkDial *dial)
{
 double newval = gtk_adjustment_get_upper (dial->adj) - gtk_adjustment_get_page_size (dial->adj);
 set_value(dial, newval);
}

static gboolean should_invert_move(GtkDial *dial, GtkOrientation o)
{
    return FALSE;
}

static void gtk_dial_move_slider(GtkDial     *dial,
                             GtkScrollType scroll)
{
  switch (scroll) {
    case GTK_SCROLL_STEP_LEFT:
      if (should_invert_move (dial, GTK_ORIENTATION_HORIZONTAL))
        step_forward (dial);
      else
        step_back (dial);
      break;

    case GTK_SCROLL_STEP_UP:
      if (should_invert_move (dial, GTK_ORIENTATION_VERTICAL))
        step_forward (dial);
      else
        step_back (dial);
      break;

    case GTK_SCROLL_STEP_RIGHT:
      if (should_invert_move (dial, GTK_ORIENTATION_HORIZONTAL))
        step_back (dial);
      else
        step_forward (dial);
      break;

    case GTK_SCROLL_STEP_DOWN:
      if (should_invert_move (dial, GTK_ORIENTATION_VERTICAL))
        step_back (dial);
      else
        step_forward (dial);
      break;

    case GTK_SCROLL_STEP_BACKWARD:
      step_back (dial);
      break;

    case GTK_SCROLL_STEP_FORWARD:
      step_forward (dial);
      break;

    case GTK_SCROLL_PAGE_LEFT:
      if (should_invert_move (dial, GTK_ORIENTATION_HORIZONTAL))
        page_forward (dial);
      else
        page_back (dial);
      break;

    case GTK_SCROLL_PAGE_UP:
      if (should_invert_move (dial, GTK_ORIENTATION_VERTICAL))
        page_forward (dial);
      else
        page_back (dial);
      break;

    case GTK_SCROLL_PAGE_RIGHT:
      if (should_invert_move (dial, GTK_ORIENTATION_HORIZONTAL))
        page_back (dial);
      else
        page_forward (dial);
      break;

    case GTK_SCROLL_PAGE_DOWN:
      if (should_invert_move (dial, GTK_ORIENTATION_VERTICAL))
        page_back (dial);
      else
        page_forward (dial);
      break;

    case GTK_SCROLL_PAGE_BACKWARD:
      page_back (dial);
      break;

    case GTK_SCROLL_PAGE_FORWARD:
      page_forward (dial);
      break;

    case GTK_SCROLL_START:
      scroll_begin (dial);
      break;

    case GTK_SCROLL_END:
      scroll_end (dial);
      break;

    case GTK_SCROLL_JUMP:
    case GTK_SCROLL_NONE:
    default:
      break;
    }

    gtk_widget_queue_draw(GTK_WIDGET(dial) );
}

static void
gtk_dial_drag_gesture_begin (GtkGestureDrag *gesture,
                              double          offset_x,
                              double          offset_y,
                              GtkDial       *dial)
{
    dial->dvalp = calc_valp(gtk_dial_get_value(dial), gtk_adjustment_get_lower(dial->adj), gtk_adjustment_get_upper(dial->adj) );
    gtk_gesture_set_state(dial->drag_gesture, GTK_EVENT_SEQUENCE_CLAIMED);
}

static void
gtk_dial_drag_gesture_update (GtkGestureDrag *gesture,
                              double          offset_x,
                              double          offset_y,
                              GtkDial       *dial)
{
    double start_x, start_y;

    gtk_gesture_drag_get_start_point (gesture, &start_x, &start_y);

    struct dial_properties p;
    get_dial_properties(dial, &p);

    double valp = dial->dvalp - DRAG_FACTOR*(offset_y/p.h);
    valp = CLAMP(valp, 0.0, 1.0);

    double val = calc_val(valp, gtk_adjustment_get_lower(dial->adj), gtk_adjustment_get_upper(dial->adj) );

    set_value(dial, val);
    gtk_widget_queue_draw(GTK_WIDGET(dial) );
}

static void
gtk_dial_drag_gesture_end (GtkGestureDrag *gesture,
                              double          offset_x,
                              double          offset_y,
                              GtkDial       *dial)
{
    dial->grab = GRAB_NONE;
    gtk_widget_queue_draw(GTK_WIDGET(dial) );
}

static void
gtk_dial_click_gesture_pressed (GtkGestureClick *gesture,
                                 int              n_press,
                                 double           x,
                                 double           y,
                                 GtkDial        *dial)
{
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

    struct dial_properties p;
    get_dial_properties(dial, &p);
    if (circle_contains_point(p.slider_cx, p.slider_cy, p.slider_radius, x, y) )
        dial->grab = GRAB_SLIDER;
    else
        dial->grab = GRAB_NONE;
    gtk_widget_queue_draw(GTK_WIDGET(dial) );
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static gboolean
gtk_dial_scroll_controller_scroll (GtkEventControllerScroll *scroll,
                                    double                    dx,
                                    double                    dy,
                                    GtkDial                  *dial)
{
    double delta = dx ? dx : dy, absolute_delta = fabs(delta);
    if (absolute_delta > 1)
      delta *= absolute_delta;
    double step = -gtk_adjustment_get_step_increment(dial->adj)*delta;

    set_value(dial, gtk_adjustment_get_value(dial->adj) + step);
    gtk_widget_queue_draw(GTK_WIDGET(dial) );

    return GDK_EVENT_STOP;
}

void gtk_dial_dispose(GObject *o)
{
    GtkDial *dial = GTK_DIAL(o);
    g_object_unref(dial->adj);
    dial->adj = NULL;
    G_OBJECT_CLASS (gtk_dial_parent_class)->dispose(o);
}
