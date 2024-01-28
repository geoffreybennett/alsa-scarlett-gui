// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkdial.h"
#include "widget-gain.h"

struct gain {
  struct alsa_elem *elem;
  GtkWidget        *vbox;
  GtkWidget        *dial;
  GtkWidget        *label;
  int               zero_is_off;
  float             scale;
};

static void gain_changed(GtkWidget *widget, struct gain *data) {
  int value = gtk_dial_get_value(GTK_DIAL(data->dial));

  alsa_set_elem_value(data->elem, value);
}

static void gain_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct gain *data = private;

  int is_writable = alsa_get_elem_writable(elem);
  gtk_widget_set_sensitive(data->dial, is_writable);

  int alsa_value = alsa_get_elem_value(elem);
  gtk_dial_set_value(GTK_DIAL(data->dial), alsa_value);

  char s[20];
  char *p = s;
  float value = (float)alsa_value * data->scale + elem->min_dB;

  if (value > elem->max_dB)
    value = elem->max_dB;
  else if (value < elem->min_dB)
    value = elem->min_dB;

  if (data->zero_is_off && alsa_value == 0) {
    p += sprintf(p, "−∞");
  } else {
    if (value < 0)
      p += sprintf(p, "−");
    if (data->scale < 1)
      p += sprintf(p, "%.1f", fabs(value));
    else
      p += sprintf(p, "%.0f", fabs(value));
  }
  if (data->scale >= 1)
    p += sprintf(p, "dB");

  gtk_label_set_text(GTK_LABEL(data->label), s);
}

//GList *make_gain_alsa_elem(struct alsa_elem *elem) {
GtkWidget *make_gain_alsa_elem(
  struct alsa_elem *elem,
  int               zero_is_off,
  int               widget_taper,
  int               can_control
) {
  struct gain *data = g_malloc(sizeof(struct gain));
  data->elem = elem;
  data->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(data->vbox, TRUE);
  gtk_widget_set_valign(data->vbox, GTK_ALIGN_START);
  gtk_widget_set_vexpand(data->vbox, TRUE);

  data->scale = (float)(elem->max_dB - elem->min_dB) /
                       (elem->max_val - elem->min_val);

  data->dial = gtk_dial_new_with_range(
    elem->min_val,
    elem->max_val,
    1,
    3 / data->scale
  );

  // calculate 0dB value
  int zero_db_value = (int)((0 - elem->min_dB) / data->scale + elem->min_val);

  gtk_dial_set_zero_db(GTK_DIAL(data->dial), zero_db_value);

  // convert from widget_taper to gtk_dial_taper
  int gtk_dial_taper;
  if (widget_taper == WIDGET_GAIN_TAPER_LINEAR)
    gtk_dial_taper = GTK_DIAL_TAPER_LINEAR;
  else if (widget_taper == WIDGET_GAIN_TAPER_LOG)
    gtk_dial_taper = GTK_DIAL_TAPER_LOG;
  else
    gtk_dial_taper = GTK_DIAL_TAPER_LINEAR;
  gtk_dial_set_taper(GTK_DIAL(data->dial), gtk_dial_taper);

  if (widget_taper == WIDGET_GAIN_TAPER_GEN4_VOLUME)
    gtk_dial_set_taper_linear_breakpoints(
      GTK_DIAL(data->dial),
      (const double[]){ 0.488, 0.76 },
      (const double[]){ 0.07, 0.4 },
      2
    );

  gtk_dial_set_can_control(GTK_DIAL(data->dial), can_control);

  data->label = gtk_label_new(NULL);
  gtk_widget_set_vexpand(data->dial, TRUE);

  data->zero_is_off = zero_is_off;

  g_signal_connect(
    data->dial, "value-changed", G_CALLBACK(gain_changed), data
  );

  alsa_elem_add_callback(elem, gain_updated, data);

  gain_updated(elem, data);

  gtk_box_append(GTK_BOX(data->vbox), data->dial);
  gtk_box_append(GTK_BOX(data->vbox), data->label);

  return data->vbox;
}
