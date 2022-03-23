// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkhelper.h"
#include "iface-no-mixer.h"
#include "stringhelper.h"
#include "tooltips.h"
#include "widget-boolean.h"
#include "widget-combo.h"
#include "window-helper.h"
#include "window-startup.h"

#include <libintl.h>
#define _(String) gettext (String)

GtkWidget *create_iface_no_mixer_main(struct alsa_card *card) {
  GArray *elems = card->elems;

  GtkWidget *grid = gtk_grid_new();
  GtkWidget *label_ic = gtk_label_new(_("Input Controls"));
  GtkWidget *vert_sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
  GtkWidget *label_oc = gtk_label_new(_("Output Controls"));

  gtk_widget_set_margin(grid, 10);
  gtk_grid_set_spacing(GTK_GRID(grid), 10);

  gtk_grid_attach(GTK_GRID(grid), label_ic, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), vert_sep, 1, 0, 1, 3);
  gtk_grid_attach(GTK_GRID(grid), label_oc, 2, 0, 1, 1);

  GtkWidget *horiz_input_sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_grid_attach(GTK_GRID(grid), horiz_input_sep, 0, 1, 1, 1);

  GtkWidget *input_grid = gtk_grid_new();
  gtk_grid_set_spacing(GTK_GRID(input_grid), 10);
  gtk_grid_attach(GTK_GRID(grid), input_grid, 0, 2, 1, 1);

  GtkWidget *horiz_output_sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
  gtk_grid_attach(GTK_GRID(grid), horiz_output_sep, 2, 1, 1, 1);

  GtkWidget *output_grid = gtk_grid_new();
  gtk_grid_set_spacing(GTK_GRID(output_grid), 10);
  gtk_grid_attach(GTK_GRID(grid), output_grid, 2, 2, 1, 1);

  // Solo or 2i2?
  // Solo Phantom Power is Line 1 only
  // 2i2 Phantom Power is Line 1-2
  int is_solo = !!get_elem_by_name(
    elems, "Line In 1 Phantom Power Capture Switch"
  );

  for (int i = 0; i < 2; i++) {
    char s[20];
    snprintf(s, 20, _("Analogue %d"), i + 1);
    GtkWidget *label = gtk_label_new(s);
    gtk_grid_attach(GTK_GRID(input_grid), label, i, 0, 1, 1);
  }

  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = &g_array_index(elems, struct alsa_elem, i);
    GtkWidget *w;

    // if no card entry, it's not a bool/enum/int elem
    if (!elem->card)
      continue;

    if (strstr(elem->name, "Validity"))
      continue;

    int line_num = get_num_from_string(elem->name);

    if (strstr(elem->name, "Level Capture Enum")) {
      w = make_boolean_alsa_elem(elem, _("Line"), _("Inst"));
      gtk_widget_set_tooltip_text(w, _(level_descr));
      gtk_grid_attach(GTK_GRID(input_grid), w, line_num - 1, 1, 1, 1);
    } else if (strstr(elem->name, "Air Capture Switch")) {
      w = make_boolean_alsa_elem(elem, _("Air Off"), _("Air On"));
      gtk_widget_set_tooltip_text(w, _(air_descr));
      gtk_grid_attach(
        GTK_GRID(input_grid), w, line_num - 1, 1 + !is_solo, 1, 1
      );
    } else if (strstr(elem->name, "Phantom Power Capture Switch")) {
      w = make_boolean_alsa_elem(elem, _("48V Off"), _("48V On"));
      gtk_widget_set_tooltip_text(w, _(phantom_descr));
      gtk_grid_attach(GTK_GRID(input_grid), w, 0, 3, 1 + !is_solo, 1);
    } else if (strcmp(elem->name, "Direct Monitor Playback Switch") == 0) {
      w = make_boolean_alsa_elem(
        elem, _("Direct Monitor Off"), _("Direct Monitor On")
      );
      gtk_widget_set_tooltip_text(
        w,
        _("Direct Monitor sends the analogue input signals to the "
        "analogue outputs for zero-latency monitoring.")
      );
      gtk_grid_attach(GTK_GRID(output_grid), w, 0, 0, 1, 1);
    } else if (strcmp(elem->name, "Direct Monitor Playback Enum") == 0) {
      GtkWidget *l = gtk_label_new(_("Direct Monitor"));
      gtk_grid_attach(GTK_GRID(output_grid), l, 0, 0, 1, 1);
      w = make_combo_box_alsa_elem(elem);
      gtk_widget_set_tooltip_text(
        w,
        g_strdup_printf("%s %s %s",
        	_("Direct Monitor sends the analogue input signals to "
        		"the analogue outputs for zero-latency monitoring."),
        	_("Mono sends both inputs to the left and right outputs."),
        	_("Stereo sends input 1 to the left, and input 2 to the right output."))
      );
      gtk_grid_attach(GTK_GRID(output_grid), w, 0, 1, 1, 1);
    }
  }

  card->window_startup = create_subwindow(
    card, _("Startup Configuration"), G_CALLBACK(window_startup_close_request)
  );

  GtkWidget *startup = create_startup_controls(card);
  gtk_window_set_child(GTK_WINDOW(card->window_startup), startup);

  return grid;
}
