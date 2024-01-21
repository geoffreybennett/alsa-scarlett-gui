// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkhelper.h"
#include "widget-boolean.h"
#include "window-startup.h"

static GtkWidget *small_label(char *text) {
  GtkWidget *w = gtk_label_new(NULL);

  char *s = g_strdup_printf("<b>%s</b>", text);
  gtk_label_set_markup(GTK_LABEL(w), s);
  free(s);

  gtk_widget_set_valign(w, GTK_ALIGN_START);

  return w;
}

static GtkWidget *big_label(char *text) {
  GtkWidget *w = gtk_label_new(text);

  gtk_widget_set_halign(w, GTK_ALIGN_START);

  gtk_label_set_wrap(GTK_LABEL(w), true);
  gtk_label_set_max_width_chars(GTK_LABEL(w), 60);

  return w;
}

static void add_sep(GtkWidget *grid, int *grid_y) {
  if (!*grid_y)
    return;

  GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_margin_top(sep, 10);
  gtk_widget_set_margin_bottom(sep, 10);
  gtk_widget_set_margin_start(sep, 20);
  gtk_widget_set_margin_end(sep, 20);
  gtk_grid_attach(GTK_GRID(grid), sep, 0, (*grid_y)++, 3, 1);
}

static void add_standalone_control(
  GArray    *elems,
  GtkWidget *grid,
  int       *grid_y
) {
  struct alsa_elem *standalone = get_elem_by_name(elems, "Standalone Switch");

  if (!standalone)
    return;

  add_sep(grid, grid_y);

  GtkWidget *w;

  w = small_label("Standalone");
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y, 1, 1);

  w = make_boolean_alsa_elem(standalone, "Disabled", "Enabled");
  gtk_widget_set_valign(w, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y + 1, 1, 1);

  w = big_label(
    "When Standalone mode is enabled, the interface will continue to "
    "route audio as per the previous routing and mixer settings "
    "after it has been disconnected from a computer. By configuring "
    "the routing between the hardware and mixer inputs and outputs "
    "appropriately, the interface can act as a standalone preamp or "
    "mixer."
  );
  gtk_grid_attach(GTK_GRID(grid), w, 1, *grid_y, 1, 2);

  *grid_y += 2;
}

static void add_phantom_persistence_control(
  GArray    *elems,
  GtkWidget *grid,
  int       *grid_y
) {
  struct alsa_elem *phantom = get_elem_by_name(
    elems, "Phantom Power Persistence Capture Switch"
  );

  if (!phantom)
    return;

  add_sep(grid, grid_y);

  GtkWidget *w;

  w = small_label("Phantom Power Persistence");
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y, 1, 1);

  w = make_boolean_alsa_elem(phantom, "Disabled", "Enabled");
  gtk_widget_set_valign(w, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y + 1, 1, 1);

  w = big_label(
    "When Phantom Power Persistence is enabled, the interface will "
    "restore the previous Phantom Power/48V setting when the "
    "interface is turned on. For the safety of microphones which can "
    "be damaged by phantom power, the interface defaults to having "
    "phantom power disabled when it is turned on."
  );
  gtk_grid_attach(GTK_GRID(grid), w, 1, *grid_y, 1, 2);

  *grid_y += 2;
}

static void add_msd_control(
  GArray    *elems,
  GtkWidget *grid,
  int       *grid_y
) {
  struct alsa_elem *msd = get_elem_by_name(
    elems, "MSD Mode Switch"
  );

  if (!msd)
    return;

  add_sep(grid, grid_y);

  GtkWidget *w;

  w = small_label("MSD (Mass Storage Device) Mode");
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y, 1, 1);

  w = make_boolean_alsa_elem(msd, "Disabled", "Enabled");
  gtk_widget_set_valign(w, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y + 1, 1, 1);

  w = big_label(
    "When MSD Mode is enabled (as it is from the factory), the "
    "interface has reduced functionality. You’ll want to have this "
    "disabled. On the other hand, when MSD Mode is enabled, the "
    "interface presents itself as a Mass Storage Device (like a USB "
    "stick), containing a link to the Focusrite web site encouraging "
    "you to register your product and download the proprietary "
    "drivers which can’t be used on Linux."
  );
  gtk_grid_attach(GTK_GRID(grid), w, 1, *grid_y, 1, 2);

  *grid_y += 2;
}

static void add_no_startup_controls_msg(GtkWidget *grid) {
  GtkWidget *w = big_label(
    "It appears that there are no startup controls. You probably "
    "need to upgrade your kernel to see something here."
  );
  gtk_grid_attach(GTK_GRID(grid), w, 0, 0, 1, 1);
}

GtkWidget *create_startup_controls(struct alsa_card *card) {
  GArray *elems = card->elems;

  GtkWidget *top = gtk_frame_new(NULL);
  gtk_widget_add_css_class(top, "window-frame");

  int grid_y = 0;

  GtkWidget *grid = gtk_grid_new();
  gtk_widget_add_css_class(grid, "window-content");
  gtk_widget_add_css_class(grid, "top-level-content");
  gtk_widget_add_css_class(grid, "window-startup");
  gtk_grid_set_column_spacing(GTK_GRID(grid), 20);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  gtk_frame_set_child(GTK_FRAME(top), grid);

  add_standalone_control(elems, grid, &grid_y);
  add_phantom_persistence_control(elems, grid, &grid_y);
  add_msd_control(elems, grid, &grid_y);

  if (!grid_y)
    add_no_startup_controls_msg(grid);

  return top;
}
