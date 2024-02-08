// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "gtkdial.h"
#include "gtkhelper.h"
#include "stringhelper.h"
#include "widget-gain.h"
#include "window-levels.h"

static const int level_breakpoints_out[] = { -80, -18, -12, -6, -3, -1 };

// inputs glow all-red when limit is reached
static const int level_breakpoints_in[]  = { -80, -18, -12, -6, -3,  0 };

static const double level_colours[] = {
  0.00, 1.00, 0.00, // -80
  0.75, 1.00, 0.00, // -18
  1.00, 1.00, 0.00, // -12
  1.00, 0.75, 0.00, //  -6
  1.00, 0.50, 0.00, //  -3
  1.00, 0.00, 0.00  //  -1/0
};

static int update_levels_controls(void *user_data) {
  struct alsa_card *card = user_data;

  struct alsa_elem *level_meter_elem = card->level_meter_elem;

  int *values = alsa_get_elem_int_values(level_meter_elem);

  int meter_num = 0;

  // go through the port categories
  for (int i = 0; i < PC_COUNT; i++) {

    // go through the ports in that category
    for (int j = 0; j < card->routing_out_count[i]; j++) {
      GtkWidget *meter = card->meters[meter_num];
      double value = 20 * log10(values[meter_num] / 4095.0);

      gtk_dial_set_value(GTK_DIAL(meter), value);
      meter_num++;
    }
  }

  free(values);

  return 1;
}

static GtkWidget *add_count_label(GtkGrid *grid, int count) {
  char s[20];

  sprintf(s, "%d", count + 1);
  GtkWidget *l = gtk_label_new(s);

  gtk_grid_attach(grid, l, count + 1, 0, 1, 1);

  return l;
}

static struct alsa_elem *get_level_meter_elem(struct alsa_card *card) {
  GArray *elems = card->elems;

  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = &g_array_index(elems, struct alsa_elem, i);

    if (!elem->card)
      continue;

    if (strcmp(elem->name, "Level Meter") == 0)
      return elem;
  }

  return NULL;
}

GtkWidget *create_levels_controls(struct alsa_card *card) {
  GtkWidget *top = gtk_frame_new(NULL);
  gtk_widget_add_css_class(top, "window-frame");

  GtkWidget *levels_top = gtk_grid_new();
  gtk_widget_add_css_class(levels_top, "window-content");
  gtk_widget_add_css_class(levels_top, "top-level-content");
  gtk_widget_add_css_class(levels_top, "window-levels");
  gtk_frame_set_child(GTK_FRAME(top), levels_top);

  GtkGrid *grid = GTK_GRID(levels_top);

  GtkWidget *count_labels[MAX_MUX_IN] = { NULL };

  int meter_num = 0;

  card->level_meter_elem = get_level_meter_elem(card);
  if (!card->level_meter_elem) {
    printf("Level Meter control not found\n");
    return NULL;
  }

  // go through the port categories
  for (int i = 0; i < PC_COUNT; i++) {
    GtkWidget *l = gtk_label_new(port_category_names[i]);
    gtk_widget_set_halign(l, GTK_ALIGN_END);

    // add the label
    gtk_grid_attach(GTK_GRID(grid), l, 0, i + 1, 1, 1);

    // go through the ports in that category
    for (int j = 0; j < card->routing_out_count[i]; j++) {

      // add a count label if that hasn't already been done
      if (!count_labels[j])
        count_labels[j] = add_count_label(grid, j);

      // create the meter widget and attach to the grid
      GtkWidget *meter = gtk_dial_new_with_range(-80, 0, 0, 0);
      gtk_dial_set_taper(GTK_DIAL(meter), GTK_DIAL_TAPER_LINEAR);
      gtk_dial_set_can_control(GTK_DIAL(meter), FALSE);
      gtk_dial_set_level_meter_colours(
        GTK_DIAL(meter),
        (i == PC_DSP || i == PC_PCM)
          ? level_breakpoints_in
          : level_breakpoints_out,
        level_colours,
        sizeof(level_breakpoints_out) / sizeof(int)
      );
      gtk_widget_set_sensitive(meter, FALSE);

      // HW Output off_db is -55db; otherwise -45db
      gtk_dial_set_off_db(GTK_DIAL(meter), i == PC_HW ? -55 : -45);

      card->meters[meter_num++] = meter;
      gtk_grid_attach(GTK_GRID(grid), meter, j + 1, i + 1, 1, 1);
    }
  }

  int elem_count = card->level_meter_elem->count;
  if (meter_num != elem_count) {
    printf("meter_num is %d but elem count is %d\n", meter_num, elem_count);
    return NULL;
  }
  card->level_meter_elem->count = elem_count;

  card->meter_gsource_timer = g_timeout_add(50, update_levels_controls, card);

  return top;
}
