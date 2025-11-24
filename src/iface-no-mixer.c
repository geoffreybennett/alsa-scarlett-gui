// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkhelper.h"
#include "iface-no-mixer.h"
#include "stringhelper.h"
#include "tooltips.h"
#include "widget-boolean.h"
#include "widget-drop-down.h"
#include "window-helper.h"
#include "window-startup.h"

GtkWidget *create_iface_no_mixer_main(struct alsa_card *card) {
  GPtrArray *elems = card->elems;

  GtkWidget *top = gtk_frame_new(NULL);
  gtk_widget_add_css_class(top, "window-frame");

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
  gtk_widget_add_css_class(content, "window-content");
  gtk_widget_add_css_class(content, "iface-no-mixer");
  gtk_frame_set_child(GTK_FRAME(top), content);

  GtkWidget *input_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  GtkWidget *output_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_box_append(GTK_BOX(content), input_box);
  gtk_box_append(GTK_BOX(content), output_box);

  GtkWidget *label_ic = gtk_label_new("Input Controls");
  GtkWidget *label_oc = gtk_label_new("Output Controls");

  gtk_widget_add_css_class(label_ic, "controls-label");
  gtk_widget_add_css_class(label_oc, "controls-label");

  gtk_widget_set_halign(label_ic, GTK_ALIGN_START);
  gtk_widget_set_halign(label_oc, GTK_ALIGN_START);

  gtk_box_append(GTK_BOX(input_box), label_ic);
  gtk_box_append(GTK_BOX(output_box), label_oc);

  GtkWidget *input_grid = gtk_grid_new();
  gtk_grid_set_spacing(GTK_GRID(input_grid), 10);
  gtk_widget_add_css_class(input_grid, "controls-content");
  gtk_widget_set_vexpand(input_grid, TRUE);
  gtk_box_append(GTK_BOX(input_box), input_grid);

  GtkWidget *output_grid = gtk_grid_new();
  gtk_grid_set_spacing(GTK_GRID(output_grid), 10);
  gtk_widget_add_css_class(output_grid, "controls-content");
  gtk_widget_set_vexpand(output_grid, TRUE);
  gtk_box_append(GTK_BOX(output_box), output_grid);

  // Solo or 2i2?
  // Solo Phantom Power is Line 1 only
  // 2i2 Phantom Power is Line 1-2
  int is_solo = !!get_elem_by_name(
    elems, "Line In 1 Phantom Power Capture Switch"
  );

  for (int i = 0; i < 2; i++) {
    char s[20];
    snprintf(s, 20, "%d", i + 1);
    GtkWidget *label = gtk_label_new(s);
    gtk_grid_attach(GTK_GRID(input_grid), label, i, 0, 1, 1);
  }

  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = g_ptr_array_index(elems, i);
    GtkWidget *w;

    // if no card entry, it's not a bool/enum/int elem
    if (!elem->card)
      continue;

    if (strstr(elem->name, "Validity"))
      continue;

    int line_num = get_num_from_string(elem->name);

    if (strstr(elem->name, "Level Capture Enum")) {
      w = make_boolean_alsa_elem(elem, "Inst", NULL);
      gtk_widget_add_css_class(w, "inst");
      gtk_widget_set_tooltip_text(w, level_descr);
      gtk_grid_attach(GTK_GRID(input_grid), w, line_num - 1, 1, 1, 1);
    } else if (strstr(elem->name, "Air Capture Switch")) {
      w = make_boolean_alsa_elem(elem, "Air", NULL);
      gtk_widget_add_css_class(w, "air");
      gtk_widget_set_tooltip_text(w, air_descr);
      gtk_grid_attach(
        GTK_GRID(input_grid), w, line_num - 1, 1 + !is_solo, 1, 1
      );
    } else if (strstr(elem->name, "Phantom Power Capture Switch")) {
      w = make_boolean_alsa_elem(elem, "48V", NULL);
      gtk_widget_add_css_class(w, "phantom");
      gtk_widget_set_tooltip_text(w, phantom_descr);
      gtk_grid_attach(GTK_GRID(input_grid), w, 0, 3, 1 + !is_solo, 1);
    } else if (strcmp(elem->name, "Direct Monitor Playback Switch") == 0) {
      w = make_boolean_alsa_elem(elem, "Direct Monitor", NULL);
      gtk_widget_add_css_class(w, "direct-monitor");
      gtk_widget_set_tooltip_text(
        w,
        "Direct Monitor sends the analogue input signals to the "
        "analogue outputs for zero-latency monitoring."
      );
      gtk_grid_attach(GTK_GRID(output_grid), w, 0, 0, 1, 1);
    } else if (strcmp(elem->name, "Direct Monitor Playback Enum") == 0) {
      w = make_drop_down_alsa_elem(elem, "Direct Monitor");
      gtk_widget_add_css_class(w, "direct-monitor");
      gtk_widget_set_tooltip_text(
        w,
        "Direct Monitor sends the analogue input signals to the "
        "analogue outputs for zero-latency monitoring. Mono sends "
        "both inputs to the left and right outputs. Stereo sends "
        "input 1 to the left, and input 2 to the right output."
      );
      gtk_grid_attach(GTK_GRID(output_grid), w, 0, 0, 1, 1);
    }
  }

  card->window_startup = create_subwindow(
    card, "Startup Configuration", G_CALLBACK(window_startup_close_request)
  );

  GtkWidget *startup = create_startup_controls(card);
  gtk_window_set_child(GTK_WINDOW(card->window_startup), startup);

  return top;
}
