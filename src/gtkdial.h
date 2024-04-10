// SPDX-FileCopyrightText: 2021 Stiliyan Varbanov <https://www.fiverr.com/stilvar>
// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: LGPL-3.0-or-later

/*
 * A Dial widget for GTK-4 similar to GtkScale.
 * 2021 Stiliyan Varbanov www.fiverr.com/stilvar
 */

#ifndef __GTK_DIAL_H__
#define __GTK_DIAL_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTK_TYPE_DIAL            (gtk_dial_get_type())
#define GTK_DIAL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GTK_TYPE_DIAL, GtkDial))
#define GTK_DIAL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GTK_TYPE_DIAL, GtkDialClass))
#define GTK_IS_DIAL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GTK_TYPE_DIAL))
#define GTK_IS_DIAL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GTK_TYPE_DIAL))
#define GTK_DIAL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GTK_TYPE_DIAL, GtkDialClass))

typedef struct _GtkDial      GtkDial;
typedef struct _GtkDialClass GtkDialClass;

struct _GtkDialClass {
  GtkWidgetClass parent_class;

  void (*value_changed)(GtkDial *dial);

  /* action signals for keybindings */
  void (*move_slider)(GtkDial *dial, GtkScrollType scroll);

  gboolean (*change_value)(
    GtkDial       *dial,
    GtkScrollType  scroll,
    double         new_value
  );
};

GType gtk_dial_get_type(void) G_GNUC_CONST;

GtkWidget *gtk_dial_new(GtkAdjustment *adjustment);

GtkWidget *gtk_dial_new_with_range(
  double min,
  double max,
  double step,
  double page
);

void gtk_dial_set_has_origin(GtkDial *dial, gboolean has_origin);
gboolean gtk_dial_get_has_origin(GtkDial *dial);

void gtk_dial_set_adjustment(GtkDial *dial, GtkAdjustment *adj);
GtkAdjustment *gtk_dial_get_adjustment(GtkDial *dial);

double gtk_dial_get_value(GtkDial *dial);
void gtk_dial_set_value(GtkDial *dial, double value);

void gtk_dial_set_round_digits(GtkDial *dial, int round_digits);
int gtk_dial_get_round_digits(GtkDial *dial);

void gtk_dial_set_zero_db(GtkDial *dial, double zero_db);
double gtk_dial_get_zero_db(GtkDial *dial);

void gtk_dial_set_off_db(GtkDial *dial, double off_db);
double gtk_dial_get_off_db(GtkDial *dial);

// taper functions
enum {
  GTK_DIAL_TAPER_LINEAR,
  GTK_DIAL_TAPER_LOG
};

void gtk_dial_set_taper(GtkDial *dial, int taper);
int gtk_dial_get_taper(GtkDial *dial);

void gtk_dial_set_taper_linear_breakpoints(
  GtkDial      *dial,
  const double *breakpoints,
  const double *outputs,
  int           count
);

void gtk_dial_set_can_control(GtkDial *dial, gboolean can_control);
gboolean gtk_dial_get_can_control(GtkDial *dial);

void gtk_dial_set_level_meter_colours(
  GtkDial      *dial,
  const int    *breakpoints,
  const double *colours,
  int           count
);

void gtk_dial_set_peak_hold(GtkDial *dial, int peak_hold);
int gtk_dial_get_peak_hold(GtkDial *dial);
void gtk_dial_peak_tick(void);

G_END_DECLS

#endif
