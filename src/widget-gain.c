// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkdial.h"
#include "stringhelper.h"
#include "widget-gain.h"

struct gain {
  struct alsa_elem *elem;
  struct alsa_elem *direct_monitor_elem;
  struct alsa_elem *monitor_mix_elem[2];
  GtkWidget        *vbox;
  GtkWidget        *dial;
  GtkWidget        *label;
  int               zero_is_off;
  float             scale;
};

static void gain_changed(GtkWidget *widget, struct gain *data) {
  int value = gtk_dial_get_value(GTK_DIAL(data->dial));
  alsa_set_elem_value(data->elem, value);

  // check if there is a corresponding Direct Monitor Mix control to
  // update as well

  // Direct Monitor control?
  if (!data->direct_monitor_elem)
    return;

  // Direct Monitor enabled?
  int direct_monitor = alsa_get_elem_value(data->direct_monitor_elem);

  if (!direct_monitor)
    return;

  // Get the corresponding Mix control
  struct alsa_elem *monitor_mix = data->monitor_mix_elem[direct_monitor - 1];
  if (!monitor_mix)
    return;

  // Update it
  alsa_set_elem_value(monitor_mix, value);
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
    else if (value > 0)
      p += sprintf(p, "+");
    if (data->scale <= 0.5)
      p += snprintf(p, 10, "%.1f", fabs(value));
    else
      p += snprintf(p, 10, "%.0f", fabs(value));
  }
  if (data->scale > 0.5)
    p += sprintf(p, "dB");

  gtk_label_set_text(GTK_LABEL(data->label), s);
}

// 4th Gen Solo and 2i2 have Mix & Direct Monitor controls which
// interact. If direct monitor is enabled and the Mix A/B controls are
// changed, then the Monitor Mix Playback Volume controls are changed
// too so that the mix settings are restored when direct monitor is
// later enabled again.
static void find_direct_monitor_controls(struct gain *data) {
  struct alsa_elem *elem = data->elem;
  GArray *elems = elem->card->elems;

  // Card has no direct monitor control?
  struct alsa_elem *direct_monitor_elem = get_elem_by_prefix(
    elems,
    "Direct Monitor Playback"
  );
  if (!direct_monitor_elem)
    return;

  // Card has no mixer?
  if (strncmp(elem->name, "Mix ", 4) != 0 ||
      !strstr(elem->name, "Playback Volume"))
    return;

  char mix_letter = elem->name[4];
  int input_num = get_num_from_string(elem->name);

  // Find the Monitor Mix control for the 4th Gen Solo
  if (strstr(direct_monitor_elem->name, "Switch")) {
    char s[80];
    sprintf(
      s,
      "Monitor Mix %c Input %02d Playback Volume",
      mix_letter, input_num
    );

    struct alsa_elem *monitor_mix_elem = get_elem_by_name(elems, s);
    if (!monitor_mix_elem)
      return;

    data->direct_monitor_elem = direct_monitor_elem;
    data->monitor_mix_elem[0] = monitor_mix_elem;

  // Find the Monitor Mix controls for the 4th Gen 2i2
  } else if (strstr(direct_monitor_elem->name, "Enum")) {
    for (int i = 0; i <= 1; i++) {
      char s[80];
      sprintf(
        s,
        "Monitor %d Mix %c Input %02d Playback Volume",
        i + 1, mix_letter, input_num
      );

      struct alsa_elem *monitor_mix_elem = get_elem_by_name(elems, s);
      if (!monitor_mix_elem)
        return;

      data->direct_monitor_elem = direct_monitor_elem;
      data->monitor_mix_elem[i] = monitor_mix_elem;
    }

  } else {
    fprintf(stderr, "Couldn't find direct monitor mix control\n");
  }
}

//GList *make_gain_alsa_elem(struct alsa_elem *elem) {
GtkWidget *make_gain_alsa_elem(
  struct alsa_elem *elem,
  int               zero_is_off,
  int               widget_taper,
  int               can_control
) {
  struct gain *data = calloc(1, sizeof(struct gain));
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
  gtk_widget_add_css_class(data->label, "gain");
  gtk_widget_set_vexpand(data->dial, TRUE);

  data->zero_is_off = zero_is_off;

  find_direct_monitor_controls(data);

  g_signal_connect(
    data->dial, "value-changed", G_CALLBACK(gain_changed), data
  );

  alsa_elem_add_callback(elem, gain_updated, data);

  gain_updated(elem, data);

  gtk_box_append(GTK_BOX(data->vbox), data->dial);
  gtk_box_append(GTK_BOX(data->vbox), data->label);

  return data->vbox;
}
