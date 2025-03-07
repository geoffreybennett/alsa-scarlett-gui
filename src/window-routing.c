// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkhelper.h"
#include "iface-mixer.h"
#include "routing-drag-line.h"
#include "routing-lines.h"
#include "stringhelper.h"
#include "widget-boolean.h"
#include "window-mixer.h"
#include "window-routing.h"

// clear all the routing sinks
static void routing_preset_clear(struct alsa_card *card) {
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    alsa_set_elem_value(r_snk->elem, 0);
  }
}

static void routing_preset_link(
  struct alsa_card *card,
  int               src_port_category,
  int               src_mod,
  int               snk_port_category
) {

  // find the first src port with the selected port category
  int start_src_idx;
  for (start_src_idx = 1;
       start_src_idx < card->routing_srcs->len;
       start_src_idx++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, start_src_idx
    );

    if (r_src->port_category == src_port_category)
      break;
  }

  // find the first snk port with the selected port category
  int snk_idx;
  for (snk_idx = 0;
       snk_idx < card->routing_snks->len;
       snk_idx++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, snk_idx
    );
    struct alsa_elem *elem = r_snk->elem;

    if (elem->port_category == snk_port_category)
      break;
  }

  // start assigning
  int src_idx = start_src_idx;
  int src_count = 0;
  while (src_idx < card->routing_srcs->len &&
         snk_idx < card->routing_snks->len) {

    // stop if no more of the selected src port category
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, src_idx
    );
    if (r_src->port_category != src_port_category)
      break;

    // stop if no more of the selected snk port category
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, snk_idx
    );
    struct alsa_elem *elem = r_snk->elem;

    if (elem->port_category != snk_port_category)
      break;

    // do the assignment
    alsa_set_elem_value(elem, r_src->id);

    // get the next index
    src_idx++;
    src_count++;
    snk_idx++;

    if (src_count == src_mod) {
      src_idx = start_src_idx;
      src_count = 0;
    }
  }
}

static void routing_preset_direct(struct alsa_card *card) {
  routing_preset_link(card, PC_HW, 0, PC_PCM);
  routing_preset_link(card, PC_PCM, 0, PC_HW);
}

static void routing_preset_preamp(struct alsa_card *card) {
  routing_preset_link(card, PC_HW, 0, PC_HW);
}

static void routing_preset_stereo_out(struct alsa_card *card) {
  routing_preset_link(card, PC_PCM, 2, PC_HW);
}

static void routing_preset(
  GSimpleAction    *action,
  GVariant         *value,
  struct alsa_card *card
) {
  const char *s = g_variant_get_string(value, NULL);

  if (strcmp(s, "clear") == 0) {
    routing_preset_clear(card);
  } else if (strcmp(s, "direct") == 0) {
    routing_preset_direct(card);
  } else if (strcmp(s, "preamp") == 0) {
    routing_preset_preamp(card);
  } else if (strcmp(s, "stereo_out") == 0) {
    routing_preset_stereo_out(card);
  }
}

static GtkWidget *make_preset_menu_button(struct alsa_card *card) {
  GMenu *menu = g_menu_new();

  g_menu_append(menu, "Clear", "routing.preset('clear')");
  g_menu_append(menu, "Direct", "routing.preset('direct')");
  g_menu_append(menu, "Preamp", "routing.preset('preamp')");
  g_menu_append(menu, "Stereo Out", "routing.preset('stereo_out')");

  GtkWidget *button = gtk_menu_button_new();
  gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
  gtk_menu_button_set_label(GTK_MENU_BUTTON(button), "Presets");
  gtk_menu_button_set_menu_model(
    GTK_MENU_BUTTON(button),
    G_MENU_MODEL(menu)
  );

  GSimpleActionGroup *action_group = g_simple_action_group_new();
  GSimpleAction *action = g_simple_action_new_stateful(
    "preset", G_VARIANT_TYPE_STRING, NULL
  );
  g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(action));
  g_signal_connect(
    action, "activate", G_CALLBACK(routing_preset), card
  );
  gtk_widget_insert_action_group(
    button, "routing", G_ACTION_GROUP(action_group)
  );

  return button;
}

static GtkWidget *create_routing_group_grid(
  struct alsa_card *card,
  char             *name,
  char             *descr,
  char             *tooltip,
  GtkOrientation    orientation,
  GtkAlign          align
) {
  GtkWidget *grid = gtk_grid_new();
  gtk_widget_set_name(grid, name);
  gtk_widget_add_css_class(grid, "controls-content");

  gtk_grid_set_spacing(GTK_GRID(grid), 2);

  if (orientation == GTK_ORIENTATION_HORIZONTAL) {
    gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(grid, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(grid, TRUE);
  } else {
    gtk_widget_set_halign(grid, GTK_ALIGN_FILL);
    gtk_widget_set_valign(grid, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(grid, TRUE);
  }

  GtkWidget *label = gtk_label_new(descr);
  gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
  if (orientation == GTK_ORIENTATION_HORIZONTAL) {
    gtk_widget_set_valign(label, align);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
  } else {
    gtk_widget_set_halign(label, align);
  }
  gtk_widget_set_tooltip_text(label, tooltip);

  return grid;
}

static void create_routing_grid(struct alsa_card *card) {
  GtkGrid *routing_grid = GTK_GRID(card->routing_grid = gtk_grid_new());

  int has_dsp = !!card->routing_in_count[PC_DSP];

  gtk_widget_set_halign(card->routing_grid, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(card->routing_grid, GTK_ALIGN_CENTER);

  GtkWidget *preset_menu_button = make_preset_menu_button(card);
  gtk_grid_attach(
    routing_grid, preset_menu_button, 0, 0, 1, 1
  );

  card->routing_hw_in_grid = create_routing_group_grid(
    card, "routing_hw_in_grid", "Hardware Inputs",
    "Hardware Inputs are the physical inputs on the interface",
    GTK_ORIENTATION_VERTICAL, GTK_ALIGN_END
  );
  card->routing_pcm_in_grid = create_routing_group_grid(
    card, "routing_pcm_in_grid", "PCM Outputs",
    "PCM Outputs are the digital audio channels sent from the PC to "
    "the interface over USB, used for audio playback",
    GTK_ORIENTATION_VERTICAL, GTK_ALIGN_END
  );
  card->routing_pcm_out_grid = create_routing_group_grid(
    card, "routing_pcm_out_grid", "PCM Inputs",
    "PCM Inputs are the digital audio channels sent from the interface "
    "to the PC over USB, use for audio recording",
    GTK_ORIENTATION_VERTICAL, GTK_ALIGN_START
  );
  card->routing_hw_out_grid = create_routing_group_grid(
    card, "routing_hw_out_grid", "Hardware Outputs",
    "Hardware Outputs are the physical outputs on the interface",
    GTK_ORIENTATION_VERTICAL, GTK_ALIGN_START
  );
  if (has_dsp) {
    card->routing_dsp_in_grid = create_routing_group_grid(
      card, "routing_dsp_in_grid", "DSP\nInputs",
      "DSP Inputs are used to send audio to the DSP, which is used for "
      "features such as the input level meters, Air mode, and Autogain",
      GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_CENTER
    );
    card->routing_dsp_out_grid = create_routing_group_grid(
      card, "routing_dsp_out_grid", "DSP\nOutputs",
      "DSP Outputs are used to send audio from the DSP after it has "
      "done its processing",
      GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_CENTER
    );
  }
  if (!card->has_fixed_mixer_inputs)
    card->routing_mixer_in_grid = create_routing_group_grid(
      card, "routing_mixer_in_grid", "Mixer\nInputs",
      "Mixer Inputs are used to mix multiple audio channels together",
      GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_CENTER
    );
  card->routing_mixer_out_grid = create_routing_group_grid(
    card, "routing_mixer_out_grid",
    card->has_talkback ? "Mixer Outputs" : "Mixer\nOutputs",
    "Mixer Outputs are used to send audio from the mixer",
    GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_CENTER
  );

  int left_col_num = 0;
  int dsp_col_num = has_dsp ? 1 : 0;
  int mix_col_num = dsp_col_num + 1;
  int right_col_num = mix_col_num + 1;

  gtk_grid_attach(
    routing_grid, card->routing_hw_in_grid, left_col_num, 1, 1, 1
  );
  gtk_grid_attach(
    routing_grid, card->routing_pcm_in_grid, left_col_num, 2, 1, 1
  );
  gtk_grid_attach(
    routing_grid, card->routing_pcm_out_grid, right_col_num, 1, 1, 1
  );
  gtk_grid_attach(
    routing_grid, card->routing_hw_out_grid, right_col_num, 2, 1, 1
  );
  if (has_dsp) {
    gtk_grid_attach(
      routing_grid, card->routing_dsp_in_grid, dsp_col_num, 0, 1, 1
    );
    gtk_grid_attach(
      routing_grid, card->routing_dsp_out_grid, dsp_col_num, 3, 1, 1
    );
  }
  if (!card->has_fixed_mixer_inputs)
    gtk_grid_attach(
      routing_grid, card->routing_mixer_in_grid, mix_col_num, 0, 1, 1
    );
  gtk_grid_attach(
    routing_grid, card->routing_mixer_out_grid, mix_col_num, 3, 1, 1
  );
  gtk_grid_set_spacing(routing_grid, 10);

  GtkWidget *src_label = gtk_label_new("↑\nSources →");
  gtk_label_set_justify(GTK_LABEL(src_label), GTK_JUSTIFY_CENTER);
  gtk_grid_attach(routing_grid, src_label, left_col_num, 3, 1, 1);

  GtkWidget *snk_label = gtk_label_new(
    card->has_fixed_mixer_inputs ? "Sinks\n↓" : "← Sinks\n↓"
  );
  gtk_label_set_justify(GTK_LABEL(snk_label), GTK_JUSTIFY_CENTER);
  gtk_grid_attach(routing_grid, snk_label, right_col_num, 0, 1, 1);
}

static GtkWidget *make_socket_widget(void) {
  GtkWidget *w = gtk_picture_new_for_resource(
    "/vu/b4/alsa-scarlett-gui/icons/socket.svg"
  );
  gtk_widget_set_align(w, GTK_ALIGN_CENTER, GTK_ALIGN_CENTER);
  gtk_picture_set_can_shrink(GTK_PICTURE(w), FALSE);
  gtk_widget_set_margin(w, 2);
  return w;
}

static void routing_label_enter(
  GtkEventControllerMotion *controller,
  double x, double y,
  gpointer user_data
) {
  GtkWidget *widget = GTK_WIDGET(user_data);

  gtk_widget_add_css_class(widget, "route-label-hover");

  struct routing_src *r_src = g_object_get_data(G_OBJECT(widget), "routing_src");
  struct routing_snk *r_snk = g_object_get_data(G_OBJECT(widget), "routing_snk");

  if (r_src) {
    struct alsa_card *card = r_src->card;

    for (int i = 0; i < card->routing_snks->len; i++) {
      struct routing_snk *r_snk = &g_array_index(
        card->routing_snks, struct routing_snk, i
      );

      if (!r_snk->box_widget)
        continue;

      if (alsa_get_elem_value(r_snk->elem) == r_src->id)
        gtk_widget_add_css_class(r_snk->box_widget, "route-label-hover");
    }

  } else if (r_snk) {
    struct alsa_card *card = r_snk->elem->card;

    int r_src_idx = alsa_get_elem_value(r_snk->elem);

    for (int i = 0; i < card->routing_srcs->len; i++) {
      struct routing_src *r_src = &g_array_index(
        card->routing_srcs, struct routing_src, i
      );

      if (!r_src->widget)
        continue;

      if (r_src->id == r_src_idx)
        gtk_widget_add_css_class(r_src->widget, "route-label-hover");
    }
  }
}

static void routing_label_leave(
  GtkEventControllerMotion *controller,
  gpointer user_data
) {
  GtkWidget *widget = GTK_WIDGET(user_data);

  gtk_widget_remove_css_class(widget, "route-label-hover");

  struct routing_src *r_src = g_object_get_data(G_OBJECT(widget), "routing_src");
  struct routing_snk *r_snk = g_object_get_data(G_OBJECT(widget), "routing_snk");

  if (r_src) {
    struct alsa_card *card = r_src->card;

    for (int i = 0; i < card->routing_snks->len; i++) {
      struct routing_snk *r_snk = &g_array_index(
        card->routing_snks, struct routing_snk, i
      );

      if (!r_snk->box_widget)
        continue;

      gtk_widget_remove_css_class(r_snk->box_widget, "route-label-hover");
    }

  } else if (r_snk) {
    struct alsa_card *card = r_snk->elem->card;

    for (int i = 0; i < card->routing_srcs->len; i++) {
      struct routing_src *r_src = &g_array_index(
        card->routing_srcs, struct routing_src, i
      );

      if (!r_src->widget)
        continue;

      gtk_widget_remove_css_class(r_src->widget, "route-label-hover");
    }
  }
}

static void add_routing_hover_controller(GtkWidget *widget) {
  GtkEventController *motion = gtk_event_controller_motion_new();

  g_signal_connect(motion, "enter", G_CALLBACK(routing_label_enter), widget);
  g_signal_connect(motion, "leave", G_CALLBACK(routing_label_leave), widget);
  gtk_widget_add_controller(widget, motion);
}

// something was dropped on a routing source
static gboolean dropped_on_src(
  GtkDropTarget *dest,
  const GValue  *value,
  double         x,
  double         y,
  gpointer       data
) {
  struct routing_src *src = data;
  int snk_id = g_value_get_int(value);

  // don't accept src -> src drops
  if (!(snk_id & 0x8000))
    return FALSE;

  // convert the int to a r_snk_idx
  int r_snk_idx = snk_id & ~0x8000;

  // check the index is in bounds
  GArray *r_snks = src->card->routing_snks;
  if (r_snk_idx < 0 || r_snk_idx >= r_snks->len)
    return FALSE;

  struct routing_snk *r_snk = &g_array_index(
    r_snks, struct routing_snk, r_snk_idx
  );
  alsa_set_elem_value(r_snk->elem, src->id);

  return TRUE;
}

// something was dropped on a routing sink
static gboolean dropped_on_snk(
  GtkDropTarget *dest,
  const GValue  *value,
  double         x,
  double         y,
  gpointer       data
) {
  struct alsa_elem *elem = data;
  int src_id = g_value_get_int(value);

  // don't accept snk -> snk drops
  if (src_id & 0x8000)
    return FALSE;

  alsa_set_elem_value(elem, src_id);
  return TRUE;
}

static void src_routing_clicked(
  GtkWidget          *widget,
  int                 n_press,
  double              x,
  double              y,
  struct routing_src *r_src
) {
  struct alsa_card *card = r_src->card;

  // go through all the routing sinks
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    struct alsa_elem *elem = r_snk->elem;

    int r_src_idx = alsa_get_elem_value(elem);

    if (r_src_idx == r_src->id)
      alsa_set_elem_value(elem, 0);
  }
}

static void snk_routing_clicked(
  GtkWidget        *widget,
  int               n_press,
  double            x,
  double            y,
  struct alsa_elem *elem
) {
  alsa_set_elem_value(elem, 0);
}

static void src_drag_begin(
  GtkDragSource *source,
  GdkDrag       *drag,
  gpointer       user_data
) {
  struct routing_src *r_src = user_data;
  struct alsa_card *card = r_src->card;

  card->drag_type = DRAG_TYPE_SRC;
  card->src_drag = r_src;
}

static void snk_drag_begin(
  GtkDragSource *source,
  GdkDrag       *drag,
  gpointer       user_data
) {
  struct routing_snk *r_snk = user_data;
  struct alsa_card *card = r_snk->elem->card;

  card->drag_type = DRAG_TYPE_SNK;
  card->snk_drag = r_snk;
}

static void src_drag_end(
  GtkDragSource *source,
  GdkDrag       *drag,
  gboolean       delete_data,
  gpointer       user_data
) {
  struct routing_src *r_src = user_data;
  struct alsa_card *card = r_src->card;

  card->drag_type = DRAG_TYPE_NONE;
  card->src_drag = NULL;
  gtk_widget_queue_draw(card->drag_line);
  gtk_widget_queue_draw(card->routing_lines);
}

static void snk_drag_end(
  GtkDragSource *source,
  GdkDrag       *drag,
  gboolean       delete_data,
  gpointer       user_data
) {
  struct routing_snk *r_snk = user_data;
  struct alsa_card *card = r_snk->elem->card;

  card->drag_type = DRAG_TYPE_NONE;
  card->snk_drag = NULL;
  gtk_widget_queue_draw(card->drag_line);
  gtk_widget_queue_draw(card->routing_lines);
}

static gboolean src_drop_accept(
  GtkDropTarget *source,
  GdkDrop       *drop,
  gpointer       user_data
) {
  struct routing_src *r_src = user_data;
  struct alsa_card *card = r_src->card;

  return card->drag_type == DRAG_TYPE_SNK;
}

static gboolean snk_drop_accept(
  GtkDropTarget *source,
  GdkDrop       *drop,
  gpointer       user_data
) {
  struct routing_snk *r_snk = user_data;
  struct alsa_card *card = r_snk->elem->card;

  return card->drag_type == DRAG_TYPE_SRC;
}

static GdkDragAction src_drop_enter(
  GtkDropTarget *dest,
  gdouble        x,
  gdouble        y,
  gpointer       user_data
) {
  struct routing_src *r_src = user_data;
  struct alsa_card *card = r_src->card;

  if (card->drag_type != DRAG_TYPE_SNK)
    return 0;

  card->src_drag = r_src;

  return GDK_ACTION_COPY;
}

static GdkDragAction snk_drop_enter(
  GtkDropTarget *dest,
  gdouble        x,
  gdouble        y,
  gpointer       user_data
) {
  struct routing_snk *r_snk = user_data;
  struct alsa_card *card = r_snk->elem->card;

  if (card->drag_type != DRAG_TYPE_SRC)
    return 0;

  card->snk_drag = r_snk;

  return GDK_ACTION_COPY;
}

static void src_drop_leave(
  GtkDropTarget *dest,
  gpointer       user_data
) {
  struct routing_src *r_src = user_data;
  struct alsa_card *card = r_src->card;

  if (card->drag_type != DRAG_TYPE_SNK)
    return;

  card->src_drag = NULL;
}

static void snk_drop_leave(
  GtkDropTarget *dest,
  gpointer       user_data
) {
  struct routing_snk *r_snk = user_data;
  struct alsa_card *card = r_snk->elem->card;

  if (card->drag_type != DRAG_TYPE_SRC)
    return;

  card->snk_drag = NULL;
}

static void setup_src_drag(struct routing_src *r_src) {
  GtkWidget *box = r_src->widget;

  // handle drags on the box
  GtkDragSource *source = gtk_drag_source_new();
  g_signal_connect(source, "drag-begin", G_CALLBACK(src_drag_begin), r_src);
  g_signal_connect(source, "drag-end", G_CALLBACK(src_drag_end), r_src);

  // set the box as a drag source
  gtk_drag_source_set_actions(source, GDK_ACTION_COPY);
  gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(source));

  // set the content
  GdkContentProvider *content = gdk_content_provider_new_typed(
    G_TYPE_INT, r_src->id
  );
  gtk_drag_source_set_content(source, content);
  g_object_unref(content);

  // set a blank icon
  GdkPaintable *paintable = gdk_paintable_new_empty(1, 1);
  gtk_drag_source_set_icon(source, paintable, 0, 0);
  g_object_unref(paintable);

  // set the box as a drop target
  GtkDropTarget *dest = gtk_drop_target_new(G_TYPE_INT, GDK_ACTION_COPY);
  gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(dest));
  g_signal_connect(dest, "drop", G_CALLBACK(dropped_on_src), r_src);
  g_signal_connect(dest, "accept", G_CALLBACK(src_drop_accept), r_src);
  g_signal_connect(dest, "enter", G_CALLBACK(src_drop_enter), r_src);
  g_signal_connect(dest, "leave", G_CALLBACK(src_drop_leave), r_src);
}

static void setup_snk_drag(struct routing_snk *r_snk) {
  GtkWidget *box = r_snk->box_widget;

  // handle drags on the box
  GtkDragSource *source = gtk_drag_source_new();
  g_signal_connect(source, "drag-begin", G_CALLBACK(snk_drag_begin), r_snk);
  g_signal_connect(source, "drag-end", G_CALLBACK(snk_drag_end), r_snk);

  // set the box as a drag source
  gtk_drag_source_set_actions(source, GDK_ACTION_COPY);
  gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(source));

  // set the content
  // 0x8000 flag indicates alsa_elem numid value
  GdkContentProvider *content = gdk_content_provider_new_typed(
    G_TYPE_INT, 0x8000 | r_snk->idx
  );
  gtk_drag_source_set_content(source, content);
  g_object_unref(content);

  // set a blank icon
  GdkPaintable *paintable = gdk_paintable_new_empty(1, 1);
  gtk_drag_source_set_icon(source, paintable, 0, 0);
  g_object_unref(paintable);

  // set the box as a drop target
  GtkDropTarget *dest = gtk_drop_target_new(G_TYPE_INT, GDK_ACTION_COPY);
  gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(dest));
  g_signal_connect(dest, "drop", G_CALLBACK(dropped_on_snk), r_snk->elem);
  g_signal_connect(dest, "accept", G_CALLBACK(snk_drop_accept), r_snk);
  g_signal_connect(dest, "enter", G_CALLBACK(snk_drop_enter), r_snk);
  g_signal_connect(dest, "leave", G_CALLBACK(snk_drop_leave), r_snk);
}

static void make_src_routing_widget(
  struct alsa_card   *card,
  struct routing_src *r_src,
  char               *name,
  GtkOrientation      orientation
) {

  // create a box, a "socket", and a label
  GtkWidget *box = r_src->widget = gtk_box_new(orientation, 5);
  GtkWidget *socket = r_src->widget2 = make_socket_widget();

  g_object_set_data(G_OBJECT(box), "routing_src", r_src);

  // create label for mixer inputs (length > 1) and mixer outputs if
  // not talkback (talkback has a button outside the box instead of a
  // label inside the box)
  if (strlen(name) > 1 || !card->has_talkback) {
    GtkWidget *label = gtk_label_new(name);
    gtk_box_append(GTK_BOX(box), label);
    gtk_widget_add_css_class(box, "route-label");

    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
      gtk_widget_set_halign(label, GTK_ALIGN_END);
      gtk_widget_set_hexpand(label, TRUE);
    }
  }

  if (orientation == GTK_ORIENTATION_HORIZONTAL) {
    gtk_box_append(GTK_BOX(box), socket);
    gtk_widget_set_halign(box, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(box, TRUE);
  } else {
    gtk_box_prepend(GTK_BOX(box), socket);
    gtk_widget_set_margin_start(box, 5);
    gtk_widget_set_margin_end(box, 5);
  }

  // handle clicks on the box
  GtkGesture *gesture = gtk_gesture_click_new();
  g_signal_connect(
    gesture, "released", G_CALLBACK(src_routing_clicked), r_src
  );
  gtk_widget_add_controller(
    GTK_WIDGET(box), GTK_EVENT_CONTROLLER(gesture)
  );

  // handle hovering
  add_routing_hover_controller(box);

  // handle dragging to or from the box
  setup_src_drag(r_src);
}

static GtkWidget *make_talkback_mix_widget(
  struct alsa_card   *card,
  struct routing_src *r_src,
  char               *name
) {
  char talkback_elem_name[80];
  snprintf(talkback_elem_name, 80, "Talkback Mix %s Playback Switch", name);
  struct alsa_elem *talkback_elem =
    get_elem_by_name(card->elems, talkback_elem_name);
  if (!talkback_elem)
    return NULL;
  return make_boolean_alsa_elem(talkback_elem, name, name);
}

static void make_snk_routing_widget(
  struct routing_snk *r_snk,
  char               *name,
  GtkOrientation      orientation
) {

  struct alsa_elem *elem = r_snk->elem;

  // create a box, a "socket", and a label
  GtkWidget *box = r_snk->box_widget = gtk_box_new(orientation, 5);
  gtk_widget_add_css_class(box, "route-label");
  GtkWidget *label = gtk_label_new(name);
  gtk_box_append(GTK_BOX(box), label);
  GtkWidget *socket = r_snk->socket_widget = make_socket_widget();

  g_object_set_data(G_OBJECT(box), "routing_snk", r_snk);

  if (orientation == GTK_ORIENTATION_VERTICAL) {
    gtk_box_append(GTK_BOX(box), socket);
    gtk_widget_set_margin_start(box, 5);
    gtk_widget_set_margin_end(box, 5);
  } else {
    gtk_box_prepend(GTK_BOX(box), socket);
    gtk_widget_set_halign(box, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
  }

  // handle clicks on the box
  GtkGesture *gesture = gtk_gesture_click_new();
  g_signal_connect(
    gesture, "released", G_CALLBACK(snk_routing_clicked), elem
  );
  gtk_widget_add_controller(
    GTK_WIDGET(box), GTK_EVENT_CONTROLLER(gesture)
  );

  // handle hovering
  add_routing_hover_controller(box);

  // handle dragging to or from the box
  setup_snk_drag(r_snk);
}

static void routing_updated(struct alsa_elem *elem, void *data) {
  struct alsa_card *card = elem->card;

  update_mixer_labels(card);
  gtk_widget_queue_draw(card->routing_lines);
}

static void make_routing_alsa_elem(struct routing_snk *r_snk) {
  struct alsa_elem *elem = r_snk->elem;
  struct alsa_card *card = elem->card;

  // "DSP Input X Capture Enum" controls (DSP Inputs) go along
  // the top, in card->routing_mixer_in_grid
  if (elem->port_category == PC_DSP) {

    char name[10];

    snprintf(name, 10, "%d", elem->lr_num);
    make_snk_routing_widget(r_snk, name, GTK_ORIENTATION_VERTICAL);
    gtk_grid_attach(
      GTK_GRID(card->routing_dsp_in_grid), r_snk->box_widget,
      elem->port_num + 1, 0, 1, 1
    );

  // "Mixer Input X Capture Enum" controls (Mixer Inputs) go along
  // the top, in card->routing_mixer_in_grid after the DSP Inputs
  } else if (elem->port_category == PC_MIX) {

    if (card->has_fixed_mixer_inputs)
      return;

    char name[10];

    snprintf(name, 10, "%d", elem->lr_num);
    make_snk_routing_widget(r_snk, name, GTK_ORIENTATION_VERTICAL);
    gtk_grid_attach(
      GTK_GRID(card->routing_mixer_in_grid), r_snk->box_widget,
      elem->port_num + 1, 0, 1, 1
    );

  // "PCM X Capture Enum" controls (PCM Inputs) go along the right,
  // in card->routing_pcm_out_grid
  } else if (elem->port_category == PC_PCM) {
    char *name = g_strdup_printf("PCM %d", elem->lr_num);
    make_snk_routing_widget(r_snk, name, GTK_ORIENTATION_HORIZONTAL);
    g_free(name);

    gtk_grid_attach(
      GTK_GRID(card->routing_pcm_out_grid), r_snk->box_widget,
      0, elem->port_num + 1, 1, 1
    );

  // "* X Playback Enum" controls go along the right, in
  // card->routing_hw_out_grid
  } else if (elem->port_category == PC_HW) {

    char *name = g_strdup_printf(
      "%s %d", hw_type_names[elem->hw_type], elem->lr_num
    );
    make_snk_routing_widget(r_snk, name, GTK_ORIENTATION_HORIZONTAL);
    g_free(name);

    gtk_grid_attach(
      GTK_GRID(card->routing_hw_out_grid), r_snk->box_widget,
      0, elem->port_num + 1, 1, 1
    );
  } else {
    printf("invalid port category %d\n", elem->port_category);
  }

  alsa_elem_add_callback(elem, routing_updated, NULL);
}

static void add_routing_widgets(
  struct alsa_card *card,
  GtkWidget        *routing_overlay
) {
  GArray *r_snks = card->routing_snks;

  // go through each routing sink and create its control
  for (int i = 0; i < r_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(r_snks, struct routing_snk, i);

    make_routing_alsa_elem(r_snk);
  }

  if (!card->routing_out_count[PC_MIX]) {
    printf("no mixer inputs??\n");
    return;
  }

  // start at 1 to skip the "Off" input
  for (int i = 1; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    if (r_src->port_category == PC_DSP) {
      // r_src->name is "DSP X"
      // +4 to skip "DSP "
      make_src_routing_widget(
        card, r_src, r_src->name + 4, GTK_ORIENTATION_VERTICAL
      );
      gtk_grid_attach(
        GTK_GRID(card->routing_dsp_out_grid), r_src->widget,
        r_src->port_num + 1, 0, 1, 1
      );

    } else if (r_src->port_category == PC_MIX) {
      // r_src->name is "Mix X"
      // +4 to skip "Mix "
      make_src_routing_widget(
        card, r_src, r_src->name + 4, GTK_ORIENTATION_VERTICAL
      );
      gtk_grid_attach(
        GTK_GRID(card->routing_mixer_out_grid), r_src->widget,
        r_src->port_num + 1, 0, 1, 1
      );

      if (card->has_talkback) {
        GtkWidget *w = make_talkback_mix_widget(card, r_src, r_src->name + 4);

        gtk_grid_attach(
          GTK_GRID(card->routing_mixer_out_grid), w,
          r_src->port_num + 1, 1, 1, 1
        );
      }
    } else if (r_src->port_category == PC_PCM) {
      char *name = g_strdup_printf("PCM %d", r_src->lr_num);
      make_src_routing_widget(
        card, r_src, name, GTK_ORIENTATION_HORIZONTAL
      );
      g_free(name);
      gtk_grid_attach(
        GTK_GRID(card->routing_pcm_in_grid), r_src->widget,
        0, r_src->port_num + 1, 1, 1
      );
    } else if (r_src->port_category == PC_HW) {
      char *name = g_strdup_printf(
        "%s %d",
        hw_type_names[r_src->hw_type],
        r_src->lr_num
      );
      make_src_routing_widget(
        card, r_src, name, GTK_ORIENTATION_HORIZONTAL
      );
      g_free(name);
      gtk_grid_attach(
        GTK_GRID(card->routing_hw_in_grid), r_src->widget,
        0, r_src->port_num + 1, 1, 1
      );
    } else {
      printf("invalid port category %d\n", r_src->port_category);
    }
  }

  if (card->has_talkback) {
    GtkWidget *l_talkback = gtk_label_new("Talkback");
    gtk_widget_set_tooltip_text(
      l_talkback,
      "Mixer Outputs with Talkback enabled will have the level of "
      "Mixer Input 25 internally raised and lowered when the "
      "Talkback control is turned On and Off."
    );
    gtk_grid_attach(
      GTK_GRID(card->routing_mixer_out_grid), l_talkback,
      0, 1, 1, 1
    );
  }

  card->routing_lines = gtk_drawing_area_new();
  gtk_widget_set_can_target(card->routing_lines, FALSE);
  gtk_drawing_area_set_draw_func(
    GTK_DRAWING_AREA(card->routing_lines), draw_routing_lines, card, NULL
  );
  gtk_overlay_add_overlay(
    GTK_OVERLAY(routing_overlay), card->routing_lines
  );

  update_mixer_labels(card);
}

GtkWidget *create_routing_controls(struct alsa_card *card) {

  if (!card->sample_capture_elem) {
    printf("couldn't find sample capture control; can't create GUI\n");
    return NULL;
  }

  create_routing_grid(card);

  GtkWidget *top = gtk_frame_new(NULL);
  gtk_widget_add_css_class(top, "window-frame");
  gtk_widget_add_css_class(top, "window-routing");

  GtkWidget *routing_overlay = gtk_overlay_new();
  gtk_widget_add_css_class(routing_overlay, "window-content");
  gtk_widget_add_css_class(routing_overlay, "window-routing");
  gtk_frame_set_child(GTK_FRAME(top), routing_overlay);

  gtk_overlay_set_child(GTK_OVERLAY(routing_overlay), card->routing_grid);

  add_routing_widgets(card, routing_overlay);

  add_drop_controller_motion(card, routing_overlay);

  return top;
}
