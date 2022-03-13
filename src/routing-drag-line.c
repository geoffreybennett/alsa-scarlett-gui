// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "routing-drag-line.h"
#include "routing-lines.h"

static void drag_enter(
  GtkDropControllerMotion *motion,
  gdouble                  x,
  gdouble                  y,
  gpointer                 data
) {
  struct alsa_card *card = data;

  card->drag_x = x;
  card->drag_y = y;
  gtk_widget_queue_draw(card->drag_line);
  gtk_widget_queue_draw(card->routing_lines);
}

static void drag_leave(
  GtkDropControllerMotion *motion,
  gpointer                 data
) {
  struct alsa_card *card = data;

  card->drag_x = -1;
  card->drag_y = -1;
  gtk_widget_queue_draw(card->drag_line);
  gtk_widget_queue_draw(card->routing_lines);
}

static void drag_motion(
  GtkDropControllerMotion *motion,
  gdouble                  x,
  gdouble                  y,
  gpointer                 data
) {
  struct alsa_card *card = data;

  card->drag_x = x;
  card->drag_y = y;
  gtk_widget_queue_draw(card->drag_line);
  gtk_widget_queue_draw(card->routing_lines);
}

void add_drop_controller_motion(
  struct alsa_card *card,
  GtkWidget *routing_overlay
) {

  // create an area to draw the drag line on
  card->drag_line = gtk_drawing_area_new();
  gtk_widget_set_can_target(card->drag_line, FALSE);
  gtk_drawing_area_set_draw_func(
    GTK_DRAWING_AREA(card->drag_line), draw_drag_line, card, NULL
  );
  gtk_overlay_add_overlay(
    GTK_OVERLAY(routing_overlay), card->drag_line
  );

  // create a controller to handle the dragging
  GtkEventController *controller = gtk_drop_controller_motion_new();
  g_signal_connect(controller, "enter", G_CALLBACK(drag_enter), card);
  g_signal_connect(controller, "leave", G_CALLBACK(drag_leave), card);
  g_signal_connect(controller, "motion", G_CALLBACK(drag_motion), card);
  gtk_widget_add_controller(card->routing_grid, controller);
}
