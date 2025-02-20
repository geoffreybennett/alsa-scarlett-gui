// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "gtkhelper.h"
#include "stringhelper.h"
#include "widget-gain.h"
#include "window-mixer.h"

static void mixer_gain_enter(
  GtkEventControllerMotion *controller,
  double x, double y,
  gpointer user_data
) {
  GtkWidget *widget = GTK_WIDGET(user_data);
  GtkWidget *mix_left = g_object_get_data(G_OBJECT(widget), "mix_label_left");
  GtkWidget *mix_right = g_object_get_data(G_OBJECT(widget), "mix_label_right");
  GtkWidget *source_top = g_object_get_data(G_OBJECT(widget), "source_label_top");
  GtkWidget *source_bottom = g_object_get_data(G_OBJECT(widget), "source_label_bottom");

  if (mix_left)
    gtk_widget_add_css_class(mix_left, "mixer-label-hover");
  if (mix_right)
    gtk_widget_add_css_class(mix_right, "mixer-label-hover");
  if (source_top)
    gtk_widget_add_css_class(source_top, "mixer-label-hover");
  if (source_bottom)
    gtk_widget_add_css_class(source_bottom, "mixer-label-hover");
}

static void mixer_gain_leave(
  GtkEventControllerMotion *controller,
  gpointer user_data
) {
  GtkWidget *widget = GTK_WIDGET(user_data);
  GtkWidget *mix_left = g_object_get_data(G_OBJECT(widget), "mix_label_left");
  GtkWidget *mix_right = g_object_get_data(G_OBJECT(widget), "mix_label_right");
  GtkWidget *source_top = g_object_get_data(G_OBJECT(widget), "source_label_top");
  GtkWidget *source_bottom = g_object_get_data(G_OBJECT(widget), "source_label_bottom");

  if (mix_left)
    gtk_widget_remove_css_class(mix_left, "mixer-label-hover");
  if (mix_right)
    gtk_widget_remove_css_class(mix_right, "mixer-label-hover");
  if (source_top)
    gtk_widget_remove_css_class(source_top, "mixer-label-hover");
  if (source_bottom)
    gtk_widget_remove_css_class(source_bottom, "mixer-label-hover");
}

static void add_mixer_hover_controller(GtkWidget *widget) {
  GtkEventController *motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "enter", G_CALLBACK(mixer_gain_enter), widget);
  g_signal_connect(motion, "leave", G_CALLBACK(mixer_gain_leave), widget);
  gtk_widget_add_controller(widget, motion);
}

static struct routing_snk *get_mixer_r_snk(
  struct alsa_card *card,
  int               input_num
) {
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    struct alsa_elem *elem = r_snk->elem;

    if (elem->port_category != PC_MIX)
      continue;

    if (elem->lr_num == input_num)
      return r_snk;
  }
  return NULL;
}

GtkWidget *create_mixer_controls(struct alsa_card *card) {
  GtkWidget *top = gtk_frame_new(NULL);
  gtk_widget_add_css_class(top, "window-frame");

  GtkWidget *mixer_top = gtk_grid_new();
  gtk_widget_add_css_class(mixer_top, "window-content");
  gtk_widget_add_css_class(mixer_top, "top-level-content");
  gtk_widget_add_css_class(mixer_top, "window-mixer");
  gtk_frame_set_child(GTK_FRAME(top), mixer_top);

  gtk_widget_set_halign(mixer_top, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(mixer_top, GTK_ALIGN_CENTER);
  gtk_grid_set_column_homogeneous(GTK_GRID(mixer_top), TRUE);

  GArray *elems = card->elems;

  GtkWidget *mix_labels_left[MAX_MIX_OUT];
  GtkWidget *mix_labels_right[MAX_MIX_OUT];

  // create the Mix X labels on the left and right of the grid
  for (int i = 0; i < card->routing_in_count[PC_MIX]; i++) {
    char name[10];
    snprintf(name, 10, "Mix %c", i + 'A');

    GtkWidget *l_left = mix_labels_left[i] = gtk_label_new(name);
    gtk_grid_attach(
      GTK_GRID(mixer_top), l_left,
      0, i + 2, 1, 1
    );

    GtkWidget *l_right = mix_labels_right[i] = gtk_label_new(name);
    gtk_grid_attach(
      GTK_GRID(mixer_top), l_right,
      card->routing_out_count[PC_MIX] + 1, i + 2, 1, 1
    );
  }

  // go through each element and create the mixer
  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = &g_array_index(elems, struct alsa_elem, i);

    // if no card entry, it's an empty slot
    if (!elem->card)
      continue;

    // looking for "Mix X Input Y Playback Volume"
    // or "Matrix Y Mix X Playback Volume" elements (Gen 1)
    if (!strstr(elem->name, "Playback Volume"))
      continue;
    if (strncmp(elem->name, "Mix ", 4) &&
        strncmp(elem->name, "Matrix ", 7))
      continue;

    char *mix_str = strstr(elem->name, "Mix ");
    if (!mix_str)
      continue;

    // extract the mix number and input number from the element name
    int mix_num = mix_str[4] - 'A';
    int input_num = get_num_from_string(elem->name) - 1;

    if (mix_num >= MAX_MIX_OUT) {
      printf("mix_num %d >= MAX_MIX_OUT %d\n", mix_num, MAX_MIX_OUT);
      continue;
    }

    // create the gain control and attach to the grid
    GtkWidget *w = make_gain_alsa_elem(elem, 1, WIDGET_GAIN_TAPER_LOG, 0);
    gtk_grid_attach(GTK_GRID(mixer_top), w, input_num + 1, mix_num + 2, 1, 1);

    // look up the r_snk entry for the mixer input number
    struct routing_snk *r_snk = get_mixer_r_snk(card, input_num + 1);
    if (!r_snk) {
      printf("missing mixer input %d\n", input_num);
      continue;
    }

    // lookup the top label for the mixer input
    GtkWidget *l_top = r_snk->mixer_label_top;

    // if the top label doesn't already exist the bottom doesn't
    // either; create them both and attach to the grid
    if (!l_top) {
      l_top = r_snk->mixer_label_top = gtk_label_new("");
      GtkWidget *l_bottom = r_snk->mixer_label_bottom = gtk_label_new("");
      gtk_widget_add_css_class(l_top, "mixer-label");
      gtk_widget_add_css_class(l_bottom, "mixer-label");

      gtk_grid_attach(
        GTK_GRID(mixer_top), l_top,
        input_num, (input_num + 1) % 2, 3, 1
      );
      gtk_grid_attach(
        GTK_GRID(mixer_top), l_bottom,
        input_num, card->routing_in_count[PC_MIX] + input_num % 2 + 2, 3, 1
      );
    }

    g_object_set_data(G_OBJECT(w), "mix_label_left", mix_labels_left[mix_num]);
    g_object_set_data(G_OBJECT(w), "mix_label_right", mix_labels_right[mix_num]);
    g_object_set_data(G_OBJECT(w), "source_label_top", r_snk->mixer_label_top);
    g_object_set_data(G_OBJECT(w), "source_label_bottom", r_snk->mixer_label_bottom);

    // add hover controller to the gain widget
    add_mixer_hover_controller(w);

  }

  update_mixer_labels(card);

  return top;
}

void update_mixer_labels(struct alsa_card *card) {
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    struct alsa_elem *elem = r_snk->elem;

    if (elem->port_category != PC_MIX)
      continue;

    int routing_src_idx = alsa_get_elem_value(elem);

    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, routing_src_idx
    );

    if (r_snk->mixer_label_top) {
      gtk_label_set_text(GTK_LABEL(r_snk->mixer_label_top), r_src->name);
      gtk_label_set_text(GTK_LABEL(r_snk->mixer_label_bottom), r_src->name);
    }
  }
}
