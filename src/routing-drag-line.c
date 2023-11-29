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

  // Retrieve the scrolled window and its child
  GtkWindow *win = GTK_WINDOW(card->window_routing);
  GtkScrolledWindow *sw = GTK_SCROLLED_WINDOW(gtk_window_get_child(win));
  GtkWidget *child = gtk_scrolled_window_get_child(sw);

  // Get horizontal and vertical adjustments for the scrolled window
  GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment(sw);
  GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(sw);

  // Calculate the total scrollable width and height
  double w = gtk_adjustment_get_upper(hadj) -
             gtk_adjustment_get_page_size(hadj);
  double h = gtk_adjustment_get_upper(vadj) -
             gtk_adjustment_get_page_size(vadj);

  // Determine the relative size of the scrollable area
  double rel_w = gtk_adjustment_get_upper(hadj) - gtk_widget_get_allocated_width(GTK_WIDGET(sw)) + gtk_widget_get_allocated_width(child);
  double rel_h = gtk_adjustment_get_upper(vadj) - gtk_widget_get_allocated_height(GTK_WIDGET(sw)) + gtk_widget_get_allocated_height(child);

  // Add margin
  rel_w -= 100;
  rel_h -= 100;
  x -= 50;
  y -= 50;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x > rel_w) x = rel_w;
  if (y > rel_h) y = rel_h;

  // Calculate new scroll positions based on mouse coordinates
  double new_hpos = (x / rel_w) * w;
  double new_vpos = (y / rel_h) * h;

  // Update the scrolled window's position
  gtk_adjustment_set_value(vadj, new_vpos);
  gtk_adjustment_set_value(hadj, new_hpos);

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
