// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <ctype.h>
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

struct levels {
  struct alsa_card *card;
  struct alsa_elem *level_meter_elem;
  GtkWidget        *top;
  GtkGrid          *grid;
  GtkWidget        *meters[MAX_METERS];
  guint             timer;
};

static int update_levels_controls(void *user_data) {
  struct levels *data = user_data;
  struct alsa_card *card = data->card;

  struct alsa_elem *level_meter_elem = data->level_meter_elem;

  // check if either window needs updates
  int levels_visible = gtk_widget_get_visible(GTK_WIDGET(card->window_levels));
  int routing_visible = card->window_routing &&
                        gtk_widget_get_visible(GTK_WIDGET(card->window_routing));

  // skip if neither window is visible
  if (!levels_visible && !routing_visible)
    return 1;

  long *values = alsa_get_elem_int_values(level_meter_elem);

  // update level meters if levels window is visible
  if (levels_visible) {
    gtk_dial_peak_tick();

    for (int i = 0; i < level_meter_elem->count; i++) {
      double value = 20 * log10(values[i] / 4095.0);
      gtk_dial_set_value(GTK_DIAL(data->meters[i]), value);
    }
  }

  // update routing levels array if routing window is visible
  if (routing_visible && card->routing_levels) {
    for (int i = 0; i < level_meter_elem->count; i++) {
      if (i < card->routing_levels_count) {
        double value = 20 * log10(values[i] / 4095.0);
        card->routing_levels[i] = value;
      }
    }
    gtk_widget_queue_draw(card->routing_lines);
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

static void on_destroy(struct levels *data, GtkWidget *widget) {
  if (data->timer)
    g_source_remove(data->timer);

  g_free(data);
}

static GtkWidget *create_levels_controls_with_labels(
  struct alsa_card *card,
  struct levels    *data
) {
  struct alsa_elem *level_meter_elem = data->level_meter_elem;
  int count = level_meter_elem->count;

  int row = 1;
  int max_count = 0;
  char *current_type = NULL;

  for (int meter_num = 0; meter_num < count; meter_num++) {
    char *label = strdup(level_meter_elem->meter_labels[meter_num]);

    if (!label) {
      fprintf(stderr, "Couldn't strdup label\n");
      exit(1);
    }

    if (strlen(label) < 3) {
      fprintf(stderr, "Label too short: %s\n", label);
      exit(1);
    }

    // Label is "Source Analogue 1" or "Source Mix A" or "Sink
    // Analogue 1", etc.
    // get the number part of the label looking from the end
    int label_idx = strlen(label) - 1;
    int label_num = 1;
    if (isdigit(label[label_idx])) {
      while (label_idx > 1 && isdigit(label[label_idx - 1]))
        label_idx--;
      label_num = atoi(&label[label_idx]);

      if (label[label_idx - 1] != ' ') {
        fprintf(stderr, "Label %s is not in the expected format\n", label);
        exit(1);
      }
      label[label_idx - 1] = '\0';

    } else if (label[label_idx] >= 'A' && label[label_idx] <= 'Z') {
      label_num = label[label_idx] - 'A' + 1;

      if (label[label_idx - 1] != ' ') {
        fprintf(stderr, "Label %s is not in the expected format\n", label);
        exit(1);
      }

      label[label_idx - 1] = '\0';
    }

    if (label_num > max_count)
      max_count = label_num;

    if (!current_type || strcmp(current_type, label)) {
      row++;

      free(current_type);
      current_type = strdup(label);

      GtkWidget *l = gtk_label_new(current_type);
      gtk_widget_set_halign(l, GTK_ALIGN_END);

      // add the type label
      gtk_grid_attach(GTK_GRID(data->grid), l, 0, row, 1, 1);
    }

    GtkWidget *meter = gtk_dial_new_with_range(-80, 0, 0, 0);
    gtk_dial_set_taper(GTK_DIAL(meter), GTK_DIAL_TAPER_LINEAR);
    gtk_dial_set_can_control(GTK_DIAL(meter), FALSE);
    gtk_dial_set_peak_hold(GTK_DIAL(meter), 1000);
    gtk_dial_set_level_meter_colours(
      GTK_DIAL(meter),
      level_breakpoints_out,
      level_colours,
      sizeof(level_breakpoints_out) / sizeof(int)
    );
    gtk_widget_set_sensitive(meter, FALSE);
    gtk_dial_set_off_db(GTK_DIAL(meter), -45);
    gtk_grid_attach(GTK_GRID(data->grid), meter, label_num, row, 1, 1);
    data->meters[meter_num] = meter;

    free(label);
  }

  free(current_type);

  for (int col = 1; col <= max_count; col++) {
    char s[20];
    sprintf(s, "%d", col);
    GtkWidget *l = gtk_label_new(s);
    gtk_grid_attach(GTK_GRID(data->grid), l, col, 0, 1, 1);
  }

  data->timer = g_timeout_add(50, update_levels_controls, data);
  g_object_weak_ref(G_OBJECT(data->grid), (GWeakNotify)on_destroy, data);

  return data->top;
}

GtkWidget *create_levels_controls(struct alsa_card *card) {
  struct levels *data = g_malloc0(sizeof(struct levels));

  data->card = card;

  GtkWidget *top = data->top = gtk_frame_new(NULL);
  gtk_widget_add_css_class(top, "window-frame");

  GtkWidget *grid_widget = gtk_grid_new();
  gtk_widget_add_css_class(grid_widget, "window-content");
  gtk_widget_add_css_class(grid_widget, "top-level-content");
  gtk_widget_add_css_class(grid_widget, "window-levels");
  gtk_frame_set_child(GTK_FRAME(top), grid_widget);

  GtkGrid *grid = data->grid = GTK_GRID(grid_widget);

  GtkWidget *count_labels[MAX_MUX_IN] = { NULL };

  int meter_num = 0;

  data->level_meter_elem = get_elem_by_name(card->elems, "Level Meter");
  if (!data->level_meter_elem) {
    printf("Level Meter control not found\n");
    return NULL;
  }

  if (data->level_meter_elem->meter_labels)
    return create_levels_controls_with_labels(card, data);

  // go through the port categories
  for (int i = 0, row = 1; i < PC_COUNT; i++) {

    if (card->routing_out_count[i] == 0)
      continue;

    GtkWidget *l = gtk_label_new(port_category_names[i]);
    gtk_widget_set_halign(l, GTK_ALIGN_END);

    // add the label
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);

    // go through the ports in that category
    for (int j = 0; j < card->routing_out_count[i]; j++) {

      // add a count label if that hasn't already been done
      if (!count_labels[j])
        count_labels[j] = add_count_label(grid, j);

      // create the meter widget and attach to the grid
      GtkWidget *meter = gtk_dial_new_with_range(-80, 0, 0, 0);
      gtk_dial_set_taper(GTK_DIAL(meter), GTK_DIAL_TAPER_LINEAR);
      gtk_dial_set_can_control(GTK_DIAL(meter), FALSE);
      gtk_dial_set_peak_hold(GTK_DIAL(meter), 1000);
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

      data->meters[meter_num++] = meter;
      gtk_grid_attach(GTK_GRID(grid), meter, j + 1, row, 1, 1);
    }

    row++;
  }

  int elem_count = data->level_meter_elem->count;
  if (meter_num != elem_count) {
    printf("meter_num is %d but elem count is %d\n", meter_num, elem_count);
  }
  data->level_meter_elem->count = elem_count;

  data->timer = g_timeout_add(50, update_levels_controls, data);
  g_object_weak_ref(G_OBJECT(grid), (GWeakNotify)on_destroy, data);

  return top;
}
