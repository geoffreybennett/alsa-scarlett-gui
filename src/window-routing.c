// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkhelper.h"
#include "iface-mixer.h"
#include "routing-drag-line.h"
#include "routing-lines.h"
#include "stringhelper.h"
#include "widget-boolean.h"
#include "window-mixer.h"
#include "window-routing.h"

static void get_routing_srcs(struct alsa_card *card) {
  struct alsa_elem *elem = card->sample_capture_elem;

  int count = alsa_get_item_count(elem);
  card->routing_srcs = g_array_new(
    FALSE, TRUE, sizeof(struct routing_src)
  );
  g_array_set_size(card->routing_srcs, count);

  for (int i = 0; i < count; i++) {
    char *name = alsa_get_item_name(elem, i);

    struct routing_src *r = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    r->card = card;
    r->id = i;

    if (strncmp(name, "Mix", 3) == 0)
      r->port_category = PC_MIX;
    else if (strncmp(name, "PCM", 3) == 0)
      r->port_category = PC_PCM;
    else
      r->port_category = PC_HW;

    r->name = name;
    r->lr_num =
      r->port_category == PC_MIX
        ? name[4] - 'A' + 1
        : get_num_from_string(name);

    r->port_num = card->routing_in_count[r->port_category]++;
  }

  assert(card->routing_in_count[PC_MIX] <= MAX_MIX_OUT);
}

static void get_routing_snks(struct alsa_card *card) {
  GArray *elems = card->elems;

  int count = 0;

  // count and label routing snks
  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = &g_array_index(elems, struct alsa_elem, i);

    if (!elem->card)
      continue;

    if (!is_elem_routing_snk(elem))
      continue;

    int i = get_num_from_string(elem->name);
    if (i < 0)
      continue;

    elem->lr_num = i;
    count++;
  }

  // create an array of routing snks pointing to those elements
  card->routing_snks = g_array_new(
    FALSE, TRUE, sizeof(struct routing_snk)
  );
  g_array_set_size(card->routing_snks, count);

  // count through card->routing_snks
  int j = 0;

  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = &g_array_index(elems, struct alsa_elem, i);

    if (!elem->lr_num)
      continue;

    struct routing_snk *r = &g_array_index(
      card->routing_snks, struct routing_snk, j
    );
    r->idx = j;
    j++;
    r->elem = elem;
    if (strncmp(elem->name, "Mixer Input", 11) == 0) {
      r->port_category = PC_MIX;
    } else if (strncmp(elem->name, "PCM", 3) == 0) {
      r->port_category = PC_PCM;
    } else if (strstr(elem->name, "Playback Enum")) {
      r->port_category = PC_HW;
    } else {
      printf("unknown mixer routing elem %s\n", elem->name);
      continue;
    }
    r->port_num = card->routing_out_count[r->port_category]++;
  }

  assert(j == count);
}

static void routing_grid_label(char *s, GtkGrid *g, GtkAlign align) {
  GtkWidget *l = gtk_label_new(s);
  gtk_widget_set_halign(l, align);
  gtk_grid_attach(g, l, 0, 0, 1, 1);
}

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

    if (r_snk->port_category == snk_port_category)
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
    if (r_snk->port_category != snk_port_category)
      break;

    // do the assignment
    alsa_set_elem_value(r_snk->elem, r_src->id);

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

static void create_routing_grid(struct alsa_card *card) {
  GtkWidget *routing_grid = card->routing_grid = gtk_grid_new();

  gtk_widget_set_halign(routing_grid, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(routing_grid, GTK_ALIGN_CENTER);

  GtkWidget *preset_menu_button = make_preset_menu_button(card);
  gtk_grid_attach(
    GTK_GRID(routing_grid), preset_menu_button, 0, 0, 1, 1
  );

  card->routing_hw_in_grid = gtk_grid_new();
  card->routing_pcm_in_grid = gtk_grid_new();
  card->routing_pcm_out_grid = gtk_grid_new();
  card->routing_hw_out_grid = gtk_grid_new();
  card->routing_mixer_in_grid = gtk_grid_new();
  card->routing_mixer_out_grid = gtk_grid_new();
  gtk_grid_attach(
    GTK_GRID(routing_grid), card->routing_hw_in_grid, 0, 1, 1, 1
  );
  gtk_grid_attach(
    GTK_GRID(routing_grid), card->routing_pcm_in_grid, 0, 2, 1, 1
  );
  gtk_grid_attach(
    GTK_GRID(routing_grid), card->routing_pcm_out_grid, 2, 1, 1, 1
  );
  gtk_grid_attach(
    GTK_GRID(routing_grid), card->routing_hw_out_grid, 2, 2, 1, 1
  );
  gtk_grid_attach(
    GTK_GRID(routing_grid), card->routing_mixer_in_grid, 1, 0, 1, 1
  );
  gtk_grid_attach(
    GTK_GRID(routing_grid), card->routing_mixer_out_grid, 1, 3, 1, 1
  );
  gtk_widget_set_margin(routing_grid, 10);
  gtk_grid_set_spacing(GTK_GRID(routing_grid), 10);
  gtk_grid_set_spacing(GTK_GRID(card->routing_hw_in_grid), 2);
  gtk_grid_set_spacing(GTK_GRID(card->routing_pcm_in_grid), 2);
  gtk_grid_set_spacing(GTK_GRID(card->routing_pcm_out_grid), 2);
  gtk_grid_set_spacing(GTK_GRID(card->routing_hw_out_grid), 2);
  gtk_grid_set_spacing(GTK_GRID(card->routing_mixer_in_grid), 2);
  gtk_grid_set_spacing(GTK_GRID(card->routing_mixer_out_grid), 10);
  gtk_grid_set_row_spacing(GTK_GRID(card->routing_mixer_out_grid), 2);
  gtk_grid_set_column_spacing(GTK_GRID(card->routing_mixer_out_grid), 10);
  gtk_widget_set_vexpand(card->routing_hw_in_grid, TRUE);
  gtk_widget_set_vexpand(card->routing_pcm_in_grid, TRUE);
  gtk_widget_set_vexpand(card->routing_pcm_out_grid, TRUE);
  gtk_widget_set_vexpand(card->routing_hw_out_grid, TRUE);
  gtk_widget_set_hexpand(card->routing_mixer_in_grid, TRUE);
  gtk_widget_set_hexpand(card->routing_mixer_out_grid, TRUE);
  gtk_widget_set_align(
    card->routing_hw_in_grid, GTK_ALIGN_FILL, GTK_ALIGN_CENTER
  );
  gtk_widget_set_align(
    card->routing_pcm_in_grid, GTK_ALIGN_FILL, GTK_ALIGN_CENTER
  );
  gtk_widget_set_align(
    card->routing_hw_out_grid, GTK_ALIGN_FILL, GTK_ALIGN_CENTER
  );
  gtk_widget_set_align(
    card->routing_pcm_out_grid, GTK_ALIGN_FILL, GTK_ALIGN_CENTER
  );
  gtk_widget_set_align(
    card->routing_mixer_in_grid, GTK_ALIGN_CENTER, GTK_ALIGN_END
  );
  gtk_widget_set_align(
    card->routing_mixer_out_grid, GTK_ALIGN_CENTER, GTK_ALIGN_START
  );

  routing_grid_label(
    "Hardware Inputs", GTK_GRID(card->routing_hw_in_grid), GTK_ALIGN_END
  );
  routing_grid_label(
    "Hardware Outputs", GTK_GRID(card->routing_hw_out_grid), GTK_ALIGN_START
  );
  routing_grid_label(
    "PCM Outputs", GTK_GRID(card->routing_pcm_in_grid), GTK_ALIGN_END
  );
  routing_grid_label(
    "PCM Inputs", GTK_GRID(card->routing_pcm_out_grid), GTK_ALIGN_START
  );

  GtkWidget *src_label = gtk_label_new("↑\nSources →");
  gtk_label_set_justify(GTK_LABEL(src_label), GTK_JUSTIFY_CENTER);
  gtk_grid_attach(GTK_GRID(routing_grid), src_label, 0, 3, 1, 1);

  GtkWidget *snk_label = gtk_label_new("← Sinks\n↓");
  gtk_label_set_justify(GTK_LABEL(snk_label), GTK_JUSTIFY_CENTER);
  gtk_grid_attach(GTK_GRID(routing_grid), snk_label, 2, 0, 1, 1);
}

static GtkWidget *make_socket_widget(void) {
  GtkWidget *w = gtk_picture_new_for_resource(
    "/vu/b4/alsa-scarlett-gui/icons/socket.svg"
  );
  gtk_widget_set_align(w, GTK_ALIGN_CENTER, GTK_ALIGN_CENTER);
  gtk_picture_set_can_shrink(GTK_PICTURE(w), FALSE);
  return w;
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

    int r_src_idx = alsa_get_elem_value(r_snk->elem);

    if (r_src_idx == r_src->id)
      alsa_set_elem_value(r_snk->elem, 0);
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
  struct alsa_elem *elem = r_snk->elem;
  GtkWidget *box = elem->widget;

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
  g_signal_connect(dest, "drop", G_CALLBACK(dropped_on_snk), elem);
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

  // create label for mixer inputs (length > 1) and mixer outputs if
  // not talkback (talkback has a button outside the box instead of a
  // label inside the box)
  if (strlen(name) > 1 || !card->has_talkback) {
    GtkWidget *label = gtk_label_new(name);
    gtk_box_append(GTK_BOX(box), label);
    gtk_widget_add_class(box, "route-label");

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
  GtkWidget *box = elem->widget = gtk_box_new(orientation, 5);
  gtk_widget_add_class(box, "route-label");
  GtkWidget *label = gtk_label_new(name);
  gtk_box_append(GTK_BOX(box), label);
  GtkWidget *socket = elem->widget2 = make_socket_widget();
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

  // handle dragging to or from the box
  setup_snk_drag(r_snk);
}

static void routing_updated(struct alsa_elem *elem) {
  struct alsa_card *card = elem->card;

  update_mixer_labels(card);
  gtk_widget_queue_draw(card->routing_lines);
}

static void make_routing_alsa_elem(struct routing_snk *r_snk) {
  struct alsa_elem *elem = r_snk->elem;
  struct alsa_card *card = elem->card;

  // "Mixer Input X Capture Enum" controls (Mixer Inputs) go along
  // the top, in card->routing_mixer_in_grid
  if (r_snk->port_category == PC_MIX) {

    char name[10];

    snprintf(name, 10, "%d", elem->lr_num);
    make_snk_routing_widget(r_snk, name, GTK_ORIENTATION_VERTICAL);
    gtk_grid_attach(
      GTK_GRID(card->routing_mixer_in_grid), elem->widget,
      r_snk->port_num + 1, 0, 1, 1
    );

  // "PCM X Capture Enum" controls (PCM Inputs) go along the right,
  // in card->routing_pcm_out_grid
  } else if (r_snk->port_category == PC_PCM) {
    char *name = strdup(elem->name);
    char *name_end = strchr(name, ' ');

    // in case the number is zero-padded
    if (name_end)
      snprintf(name_end, strlen(name_end) + 1, " %d", elem->lr_num);

    make_snk_routing_widget(r_snk, name, GTK_ORIENTATION_HORIZONTAL);
    free(name);

    gtk_grid_attach(
      GTK_GRID(card->routing_pcm_out_grid), elem->widget,
      0, r_snk->port_num + 1, 1, 1
    );

  // "* Output X Playback Enum" controls go along the right, in
  // card->routing_hw_out_grid
  } else if (r_snk->port_category == PC_HW) {

    // Convert "Analogue 01 Output Playback Enum" to "Analogue 1"
    char *name = strdup(elem->name);
    char *name_end = strstr(name, " Output ");

    // in case the number is zero-padded
    if (name_end)
      snprintf(name_end, strlen(name_end) + 1, " %d", elem->lr_num);

    make_snk_routing_widget(r_snk, name, GTK_ORIENTATION_HORIZONTAL);
    free(name);

    gtk_grid_attach(
      GTK_GRID(card->routing_hw_out_grid), elem->widget,
      0, r_snk->port_num + 1, 1, 1
    );
  } else {
    printf("invalid port category %d\n", r_snk->port_category);
  }

  elem->widget_callback = routing_updated;
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

  GtkWidget *l_mixer_in = gtk_label_new("Mixer\nInputs");
  gtk_label_set_justify(GTK_LABEL(l_mixer_in), GTK_JUSTIFY_CENTER);
  gtk_grid_attach(
    GTK_GRID(card->routing_mixer_in_grid), l_mixer_in,
    0, 0, 1, 1
  );

  // start at 1 to skip the "Off" input
  for (int i = 1; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    if (r_src->port_category == PC_MIX) {
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
      make_src_routing_widget(
        card, r_src, r_src->name, GTK_ORIENTATION_HORIZONTAL
      );
      gtk_grid_attach(
        GTK_GRID(card->routing_pcm_in_grid), r_src->widget,
        0, r_src->port_num + 1, 1, 1
      );
    } else if (r_src->port_category == PC_HW) {
      make_src_routing_widget(
        card, r_src, r_src->name, GTK_ORIENTATION_HORIZONTAL
      );
      gtk_grid_attach(
        GTK_GRID(card->routing_hw_in_grid), r_src->widget,
        0, r_src->port_num + 1, 1, 1
      );
    } else {
      printf("invalid port category %d\n", r_src->port_category);
    }
  }

  GtkWidget *l_mixer_out = gtk_label_new(
    card->has_talkback ? "Mixer Outputs" : "Mixer\nOutputs"
  );
  gtk_label_set_justify(GTK_LABEL(l_mixer_out), GTK_JUSTIFY_CENTER);
  gtk_grid_attach(
    GTK_GRID(card->routing_mixer_out_grid), l_mixer_out,
    0, 0, 1, 1
  );

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

  // check that we can find a routing control
  card->sample_capture_elem =
    get_elem_by_name(card->elems, "PCM 01 Capture Enum");
  if (!card->sample_capture_elem) {
    printf("couldn't find PCM 01 Capture Enum control; can't create GUI\n");
    return NULL;
  }

  get_routing_srcs(card);
  get_routing_snks(card);

  create_routing_grid(card);

  GtkWidget *routing_overlay = gtk_overlay_new();

  gtk_overlay_set_child(GTK_OVERLAY(routing_overlay), card->routing_grid);

  add_routing_widgets(card, routing_overlay);

  add_drop_controller_motion(card, routing_overlay);

  return routing_overlay;
}
