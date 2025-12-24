// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkhelper.h"
#include "hw-io-availability.h"
#include "iface-mixer.h"
#include "routing-drag-line.h"
#include "routing-lines.h"
#include "stringhelper.h"
#include "widget-boolean.h"
#include "window-mixer.h"
#include "window-routing.h"
#include "custom-names.h"
#include "port-enable.h"

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
  GtkAlign          align,
  GtkWidget       **heading_label
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

  if (heading_label)
    *heading_label = label;

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
    GTK_ORIENTATION_VERTICAL, GTK_ALIGN_END, NULL
  );
  card->routing_pcm_in_grid = create_routing_group_grid(
    card, "routing_pcm_in_grid", "PCM Outputs",
    "PCM Outputs are the digital audio channels sent from the PC to "
    "the interface over USB, used for audio playback",
    GTK_ORIENTATION_VERTICAL, GTK_ALIGN_END, NULL
  );
  card->routing_pcm_out_grid = create_routing_group_grid(
    card, "routing_pcm_out_grid", "PCM Inputs",
    "PCM Inputs are the digital audio channels sent from the interface "
    "to the PC over USB, use for audio recording",
    GTK_ORIENTATION_VERTICAL, GTK_ALIGN_START, NULL
  );
  card->routing_hw_out_grid = create_routing_group_grid(
    card, "routing_hw_out_grid", "Hardware Outputs",
    "Hardware Outputs are the physical outputs on the interface",
    GTK_ORIENTATION_VERTICAL, GTK_ALIGN_START, NULL
  );
  if (has_dsp) {
    card->routing_dsp_in_grid = create_routing_group_grid(
      card, "routing_dsp_in_grid", "DSP\nInputs",
      "DSP Inputs are used to send audio to the DSP, which is used for "
      "features such as the input level meters, Air mode, and Autogain",
      GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_CENTER, NULL
    );
    card->routing_dsp_out_grid = create_routing_group_grid(
      card, "routing_dsp_out_grid", "DSP\nOutputs",
      "DSP Outputs are used to send audio from the DSP after it has "
      "done its processing",
      GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_CENTER, NULL
    );
  }
  if (!card->has_fixed_mixer_inputs)
    card->routing_mixer_in_grid = create_routing_group_grid(
      card, "routing_mixer_in_grid", "Mixer\nInputs",
      "Mixer Inputs are used to mix multiple audio channels together",
      GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_CENTER,
      &card->routing_mixer_in_heading
    );
  card->routing_mixer_out_grid = create_routing_group_grid(
    card, "routing_mixer_out_grid",
    card->has_talkback ? "Mixer Outputs" : "Mixer\nOutputs",
    "Mixer Outputs are used to send audio from the mixer",
    GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_CENTER,
    &card->routing_mixer_out_heading
  );

  int left_col_num = 0;
  int dsp_col_num = has_dsp ? 1 : 0;
  int mix_col_num = dsp_col_num + 1;
  int right_col_num = mix_col_num + 1;

  // set minimum width on the mixer column to maintain spacing even when empty
  gtk_grid_set_column_homogeneous(routing_grid, FALSE);
  GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_size_request(spacer, 200, 1);
  gtk_grid_attach(routing_grid, spacer, mix_col_num, 0, 1, 1);

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

  card->routing_src_label = gtk_label_new("↑\nSources →");
  gtk_label_set_justify(GTK_LABEL(card->routing_src_label), GTK_JUSTIFY_CENTER);
  gtk_grid_attach(routing_grid, card->routing_src_label, left_col_num, 3, 1, 1);

  card->routing_snk_label = gtk_label_new(
    card->has_fixed_mixer_inputs ? "Sinks\n↓" : "← Sinks\n↓"
  );
  gtk_label_set_justify(GTK_LABEL(card->routing_snk_label), GTK_JUSTIFY_CENTER);
  gtk_grid_attach(routing_grid, card->routing_snk_label, right_col_num, 0, 1, 1);
}

static GtkWidget *make_socket_widget(void) {
  GtkWidget *w = gtk_picture_new_for_resource(
    "/vu/b4/alsa-scarlett-gui/icons/socket.svg"
  );
  gtk_widget_set_align(w, GTK_ALIGN_CENTER, GTK_ALIGN_CENTER);
  gtk_picture_set_can_shrink(GTK_PICTURE(w), FALSE);
  gtk_widget_set_margin(w, 2);
  gtk_widget_set_vexpand(w, FALSE);
  gtk_widget_set_hexpand(w, FALSE);
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

      if (r_snk->effective_source_idx == r_src->id)
        gtk_widget_add_css_class(r_snk->box_widget, "route-label-hover");
    }

  } else if (r_snk) {
    struct alsa_card *card = r_snk->elem->card;

    int r_src_idx = r_snk->effective_source_idx;

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

// Speaker switching states
#define SPEAKER_SWITCH_OFF  0
#define SPEAKER_SWITCH_MAIN 1
#define SPEAKER_SWITCH_ALT  2

// Check if any Alt Group Output is enabled (Gen 4)
static int has_any_alt_group_enabled(struct alsa_card *card) {
  for (int i = 1; i <= 8; i++) {
    char name[64];
    snprintf(name, sizeof(name), "Alt Group Output %d Playback Switch", i);
    struct alsa_elem *elem = get_elem_by_name(card->elems, name);
    if (elem && alsa_get_elem_value(elem))
      return 1;
  }
  return 0;
}

// Get speaker switching state: 0=off, 1=main, 2=alt
static int get_speaker_switching_state(struct alsa_card *card) {
  // Try enum version first (Gen 2/3 larger models)
  struct alsa_elem *elem = get_elem_by_name(
    card->elems, "Speaker Switching Playback Enum"
  );
  if (elem) {
    int val = alsa_get_elem_value(elem);
    // Enum values: 0=Off, 1=Main, 2=Alt
    if (val == 0)
      return SPEAKER_SWITCH_OFF;
    return val == 2 ? SPEAKER_SWITCH_ALT : SPEAKER_SWITCH_MAIN;
  }

  // Check for Main/Alt Group controls (Gen 4)
  struct alsa_elem *main_group = get_elem_by_prefix(
    card->elems, "Main Group Output"
  );
  if (main_group) {
    // Gen 4: speaker switching is implicitly enabled if any Alt output is enabled
    if (!has_any_alt_group_enabled(card))
      return SPEAKER_SWITCH_OFF;

    // Speaker Switching Alt switch selects Main (0) or Alt (1)
    struct alsa_elem *alt = get_elem_by_name(
      card->elems, "Speaker Switching Alt Playback Switch"
    );
    if (alt && alsa_get_elem_value(alt))
      return SPEAKER_SWITCH_ALT;
    return SPEAKER_SWITCH_MAIN;
  }

  // Try switch version (Gen 2/3 smaller models)
  struct alsa_elem *sw = get_elem_by_name(
    card->elems, "Speaker Switching Playback Switch"
  );
  struct alsa_elem *alt = get_elem_by_name(
    card->elems, "Speaker Switching Alt Playback Switch"
  );
  if (sw && alt) {
    if (!alsa_get_elem_value(sw))
      return SPEAKER_SWITCH_OFF;
    if (alsa_get_elem_value(alt))
      return SPEAKER_SWITCH_ALT;
    return SPEAKER_SWITCH_MAIN;
  }

  return SPEAKER_SWITCH_OFF;
}

// Check if a sink is currently muted (in inactive monitor group)
static int is_snk_muted(struct routing_snk *r_snk) {
  struct alsa_elem *elem = r_snk->elem;

  // Only HW analogue outputs can be muted by speaker switching
  if (elem->port_category != PC_HW || elem->hw_type != HW_TYPE_ANALOGUE)
    return 0;

  // If no group controls, not muted
  if (!r_snk->main_group_switch && !r_snk->alt_group_switch)
    return 0;

  int in_main = r_snk->main_group_switch &&
                alsa_get_elem_value(r_snk->main_group_switch);
  int in_alt = r_snk->alt_group_switch &&
               alsa_get_elem_value(r_snk->alt_group_switch);

  // If not in either group, not muted
  if (!in_main && !in_alt)
    return 0;

  int speaker_state = get_speaker_switching_state(elem->card);

  // OFF or MAIN active: muted if only in Alt group
  if (speaker_state == SPEAKER_SWITCH_OFF || speaker_state == SPEAKER_SWITCH_MAIN)
    return in_alt && !in_main;

  // ALT active: muted if only in Main group
  return in_main && !in_alt;
}

// Get the element to modify for routing changes to a sink.
// Returns the group source elem if sink is in active monitor group,
// otherwise returns the routing elem. Returns NULL if sink is muted.
static struct alsa_elem *get_snk_routing_elem(struct routing_snk *r_snk) {
  struct alsa_elem *elem = r_snk->elem;

  // Only HW analogue outputs can be affected by speaker switching
  if (elem->port_category != PC_HW || elem->hw_type != HW_TYPE_ANALOGUE)
    return elem;

  // If no group controls, use normal routing
  if (!r_snk->main_group_switch && !r_snk->alt_group_switch)
    return elem;

  int in_main = r_snk->main_group_switch &&
                alsa_get_elem_value(r_snk->main_group_switch);
  int in_alt = r_snk->alt_group_switch &&
               alsa_get_elem_value(r_snk->alt_group_switch);

  // If not in either group, use normal routing
  if (!in_main && !in_alt)
    return elem;

  int speaker_state = get_speaker_switching_state(elem->card);

  // OFF or MAIN active
  if (speaker_state == SPEAKER_SWITCH_OFF || speaker_state == SPEAKER_SWITCH_MAIN) {
    if (in_main)
      return r_snk->main_group_source;
    // Only in Alt group while Main is active - muted
    return NULL;
  }

  // ALT active
  if (in_alt)
    return r_snk->alt_group_source;
  // Only in Main group while Alt is active - muted
  return NULL;
}

// Convert routing source ID to monitor group source ID.
// Monitor group source enums have different indices, so we need to search.
static int routing_src_to_vg_src(struct alsa_card *card, int routing_src_id) {
  // Search the map for the routing source ID
  for (int i = 0; i < card->monitor_group_src_map_count; i++) {
    if (card->monitor_group_src_map[i] == routing_src_id)
      return i;
  }
  // Not found - return 0 (Off)
  return 0;
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

  // Get appropriate element (routing or group source)
  struct alsa_elem *target_elem = get_snk_routing_elem(r_snk);
  if (!target_elem)
    return FALSE;  // Sink is muted

  // If using group source, convert routing source ID to VG source ID
  int src_id = src->id;
  if (target_elem != r_snk->elem)
    src_id = routing_src_to_vg_src(src->card, src_id);

  alsa_set_elem_value(target_elem, src_id);

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
  struct routing_snk *r_snk = data;
  int src_id = g_value_get_int(value);

  // don't accept snk -> snk drops
  if (src_id & 0x8000)
    return FALSE;

  // Get appropriate element (routing or group source)
  struct alsa_elem *target_elem = get_snk_routing_elem(r_snk);
  if (!target_elem)
    return FALSE;  // Sink is muted

  // If using group source, convert routing source ID to VG source ID
  if (target_elem != r_snk->elem)
    src_id = routing_src_to_vg_src(r_snk->elem->card, src_id);

  alsa_set_elem_value(target_elem, src_id);
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

    // Check effective source (accounts for speaker switching)
    if (r_snk->effective_source_idx != r_src->id)
      continue;

    // Get appropriate element to clear - skip Main/Alt outputs
    struct alsa_elem *target_elem = get_snk_routing_elem(r_snk);
    if (target_elem && target_elem == r_snk->elem)
      alsa_set_elem_value(target_elem, 0);
  }
}

static void snk_routing_clicked(
  GtkWidget          *widget,
  int                 n_press,
  double              x,
  double              y,
  struct routing_snk *r_snk
) {
  // Get appropriate element (routing or group source)
  struct alsa_elem *target_elem = get_snk_routing_elem(r_snk);

  // Do nothing if muted or using group source (Main/Alt outputs)
  if (!target_elem || target_elem != r_snk->elem)
    return;

  alsa_set_elem_value(target_elem, 0);
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

  // Reject if not dragging a sink
  if (card->drag_type != DRAG_TYPE_SNK)
    return FALSE;

  // Reject if the sink being dragged is muted
  if (card->snk_drag && is_snk_muted(card->snk_drag))
    return FALSE;

  return TRUE;
}

static gboolean snk_drop_accept(
  GtkDropTarget *source,
  GdkDrop       *drop,
  gpointer       user_data
) {
  struct routing_snk *r_snk = user_data;
  struct alsa_card *card = r_snk->elem->card;

  // Reject if not dragging a source
  if (card->drag_type != DRAG_TYPE_SRC)
    return FALSE;

  // Reject drops on muted sinks
  if (is_snk_muted(r_snk))
    return FALSE;

  return TRUE;
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
  g_signal_connect(dest, "drop", G_CALLBACK(dropped_on_snk), r_snk);
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

  // create label inside the box, except for mixer outputs when
  // talkback is available (the talkback button serves as the label)
  int is_mixer_output_with_talkback =
    r_src->port_category == PC_MIX && card->has_talkback;

  if (!is_mixer_output_with_talkback) {
    GtkWidget *label = gtk_label_new(name);
    r_src->label_widget = label;
    gtk_box_append(GTK_BOX(box), label);
    gtk_widget_add_css_class(box, "route-label");

    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
      gtk_widget_set_halign(label, GTK_ALIGN_END);
      gtk_widget_set_hexpand(label, TRUE);
    } else {
      // for vertical orientation (mixer outputs), constrain label width
      gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
      gtk_label_set_max_width_chars(GTK_LABEL(label), 8);
      gtk_widget_set_tooltip_text(label, name);
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
  struct routing_src *r_src
) {
  // Use lr_num to construct the element name (lr_num 1='A', 2='B', etc.)
  char talkback_elem_name[80];
  snprintf(
    talkback_elem_name, 80,
    "Talkback Mix %c Playback Switch",
    'A' + r_src->lr_num - 1
  );
  struct alsa_elem *talkback_elem =
    get_elem_by_name(card->elems, talkback_elem_name);
  if (!talkback_elem)
    return NULL;

  // Use the formatted display name (e.g., "A" for defaults, custom name if set)
  char *display_name = get_src_display_name_formatted(r_src);
  GtkWidget *button = make_boolean_alsa_elem(
    talkback_elem, display_name, display_name
  );
  g_free(display_name);
  return button;
}

// Helper to set label styling for availability
static void set_availability_label(
  GtkWidget  *label,
  const char *base_name,
  int         available,
  const char *tooltip
) {
  if (!available) {
    char *markup = g_strdup_printf(
      "<span color=\"#808080\"><s>%s</s></span>", base_name
    );
    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_widget_set_tooltip_text(label, tooltip);
    g_free(markup);
  } else {
    gtk_label_set_text(GTK_LABEL(label), base_name);
    gtk_label_set_use_markup(GTK_LABEL(label), FALSE);
    gtk_widget_set_tooltip_text(label, NULL);
  }
}

// Update the cached effective source index for a routing sink.
// Uses cached element pointers for performance.
void update_snk_effective_source(struct routing_snk *r_snk) {
  struct alsa_elem *elem = r_snk->elem;
  struct alsa_card *card = elem->card;

  // Only HW analogue outputs can be affected by speaker switching
  if (elem->port_category != PC_HW || elem->hw_type != HW_TYPE_ANALOGUE) {
    r_snk->effective_source_idx = alsa_get_elem_value(elem);
    return;
  }

  // If no group controls, use normal routing
  if (!r_snk->main_group_switch && !r_snk->alt_group_switch) {
    r_snk->effective_source_idx = alsa_get_elem_value(elem);
    return;
  }

  int in_main = r_snk->main_group_switch &&
                alsa_get_elem_value(r_snk->main_group_switch);
  int in_alt = r_snk->alt_group_switch &&
               alsa_get_elem_value(r_snk->alt_group_switch);

  // If not in either group, use normal routing
  if (!in_main && !in_alt) {
    r_snk->effective_source_idx = alsa_get_elem_value(elem);
    return;
  }

  int speaker_state = get_speaker_switching_state(card);

  // Check based on which monitor group is active (OFF means Main is active)
  if (speaker_state == SPEAKER_SWITCH_OFF || speaker_state == SPEAKER_SWITCH_MAIN) {
    if (in_main && r_snk->main_group_source) {
      // Main active and in Main group - use Main Group Source
      int vg_idx = alsa_get_elem_value(r_snk->main_group_source);
      if (vg_idx < card->monitor_group_src_map_count) {
        r_snk->effective_source_idx = card->monitor_group_src_map[vg_idx];
        return;
      }
    } else if (in_alt) {
      // Main active but only in Alt group - muted
      r_snk->effective_source_idx = 0;
      return;
    }
  } else if (speaker_state == SPEAKER_SWITCH_ALT) {
    if (in_alt && r_snk->alt_group_source) {
      // Alt active and in Alt group - use Alt Group Source
      int vg_idx = alsa_get_elem_value(r_snk->alt_group_source);
      if (vg_idx < card->monitor_group_src_map_count) {
        r_snk->effective_source_idx = card->monitor_group_src_map[vg_idx];
        return;
      }
    } else if (in_main) {
      // Alt active but only in Main group - muted
      r_snk->effective_source_idx = 0;
      return;
    }
  }

  r_snk->effective_source_idx = alsa_get_elem_value(elem);
}

// Update hardware output label to show monitor group status
void update_hw_output_label(struct routing_snk *r_snk) {
  struct alsa_elem *elem = r_snk->elem;
  struct alsa_card *card = elem->card;

  if (!r_snk->label_widget)
    return;

  // Get the display name (handles custom names)
  char *base_name = get_snk_display_name_formatted(r_snk);

  // PCM outputs - check availability based on sample rate
  if (elem->port_category == PC_PCM) {
    // PCM sinks are "PCM Inputs" in routing = capture to PC
    // port_num is 0-based, capture_channels is count
    int available = card->pcm_capture_channels == 0 ||
                    elem->port_num < card->pcm_capture_channels;

    if (!available) {
      char *markup = g_strdup_printf(
        "<span color=\"#808080\"><s>%s</s></span>", base_name
      );
      gtk_label_set_markup(GTK_LABEL(r_snk->label_widget), markup);
      gtk_widget_set_tooltip_text(
        r_snk->label_widget,
        "Unavailable at current sample rate"
      );
      g_free(markup);
    } else {
      gtk_label_set_text(GTK_LABEL(r_snk->label_widget), base_name);
      gtk_label_set_use_markup(GTK_LABEL(r_snk->label_widget), FALSE);
      gtk_widget_set_tooltip_text(r_snk->label_widget, NULL);
    }
    g_free(base_name);
    return;
  }

  // Mixer/DSP sinks - update availability styling
  if (elem->port_category == PC_MIX || elem->port_category == PC_DSP) {
    int available = get_sample_rate_category(card->current_sample_rate) != SR_HIGH;
    set_availability_label(
      r_snk->label_widget, base_name, available,
      "Mixer unavailable at current sample rate"
    );
    g_free(base_name);
    return;
  }

  // Only hardware outputs get monitor group indicators
  if (elem->port_category != PC_HW) {
    g_free(base_name);
    return;
  }

  // Non-analogue outputs - check S/PDIF and ADAT availability
  if (elem->hw_type != HW_TYPE_ANALOGUE) {
    if (is_digital_io_type(elem->hw_type)) {
      int max_port = elem->hw_type == HW_TYPE_SPDIF
        ? card->max_spdif_out : card->max_adat_out;
      int available = max_port < 0 || elem->lr_num <= max_port;
      set_availability_label(
        r_snk->label_widget, base_name, available,
        "Unavailable with current Digital I/O mode and sample rate"
      );
    } else {
      gtk_label_set_text(GTK_LABEL(r_snk->label_widget), base_name);
      gtk_label_set_use_markup(GTK_LABEL(r_snk->label_widget), FALSE);
      gtk_widget_set_tooltip_text(r_snk->label_widget, NULL);
    }
    g_free(base_name);
    return;
  }

  // Check for Main/Alt Group controls
  char ctrl_name[64];
  snprintf(ctrl_name, sizeof(ctrl_name),
           "Main Group Output %d Playback Switch", elem->lr_num);
  struct alsa_elem *main_elem = get_elem_by_name(card->elems, ctrl_name);

  snprintf(ctrl_name, sizeof(ctrl_name),
           "Alt Group Output %d Playback Switch", elem->lr_num);
  struct alsa_elem *alt_elem = get_elem_by_name(card->elems, ctrl_name);

  // If no monitor group controls, just use base name
  if (!main_elem && !alt_elem) {
    gtk_label_set_text(GTK_LABEL(r_snk->label_widget), base_name);
    gtk_label_set_use_markup(GTK_LABEL(r_snk->label_widget), FALSE);
    g_free(base_name);
    return;
  }

  int speaker_state = get_speaker_switching_state(card);
  int in_main = main_elem && alsa_get_elem_value(main_elem);
  int in_alt = alt_elem && alsa_get_elem_value(alt_elem);

  char *label_text = NULL;

  if (speaker_state == SPEAKER_SWITCH_OFF) {
    // Speaker switching off - show normal label
    gtk_label_set_text(GTK_LABEL(r_snk->label_widget), base_name);
    gtk_label_set_use_markup(GTK_LABEL(r_snk->label_widget), FALSE);
  } else if (speaker_state == SPEAKER_SWITCH_MAIN) {
    if (in_main) {
      // Active in Main group - green indicator
      label_text = g_strdup_printf(
        "%s <span color=\"#4a4\"><small>Main</small></span>", base_name
      );
      gtk_label_set_markup(GTK_LABEL(r_snk->label_widget), label_text);
    } else if (in_alt) {
      // In Alt group but Main is active - strikethrough
      label_text = g_strdup_printf("<s>%s</s>", base_name);
      gtk_label_set_markup(GTK_LABEL(r_snk->label_widget), label_text);
    } else {
      // Not in either group - show normal label
      gtk_label_set_text(GTK_LABEL(r_snk->label_widget), base_name);
      gtk_label_set_use_markup(GTK_LABEL(r_snk->label_widget), FALSE);
    }
  } else {  // SPEAKER_SWITCH_ALT
    if (in_alt) {
      // Active in Alt group - red indicator
      label_text = g_strdup_printf(
        "%s <span color=\"#f66\"><small>Alt</small></span>", base_name
      );
      gtk_label_set_markup(GTK_LABEL(r_snk->label_widget), label_text);
    } else if (in_main) {
      // In Main group but Alt is active - strikethrough
      label_text = g_strdup_printf("<s>%s</s>", base_name);
      gtk_label_set_markup(GTK_LABEL(r_snk->label_widget), label_text);
    } else {
      // Not in either group - show normal label
      gtk_label_set_text(GTK_LABEL(r_snk->label_widget), base_name);
      gtk_label_set_use_markup(GTK_LABEL(r_snk->label_widget), FALSE);
    }
  }

  g_free(label_text);
  g_free(base_name);
}

// Update hardware input label for S/PDIF and ADAT availability
void update_hw_input_label(struct routing_src *r_src) {
  if (!r_src->label_widget)
    return;

  if (r_src->port_category != PC_HW)
    return;

  if (!is_digital_io_type(r_src->hw_type))
    return;

  struct alsa_card *card = r_src->card;

  int max_port = r_src->hw_type == HW_TYPE_SPDIF
    ? card->max_spdif_in : card->max_adat_in;
  int available = max_port < 0 || r_src->lr_num <= max_port;

  char *base_name = get_src_display_name_formatted(r_src);

  set_availability_label(
    r_src->label_widget, base_name, available,
    "Unavailable with current Digital I/O mode and sample rate"
  );

  g_free(base_name);
}

// Update mixer source label for availability at high sample rates
static void update_mixer_src_label(struct routing_src *r_src) {
  if (!r_src->label_widget || r_src->port_category != PC_MIX)
    return;

  struct alsa_card *card = r_src->card;
  int available = get_sample_rate_category(card->current_sample_rate) != SR_HIGH;

  char *base_name = get_src_display_name_formatted(r_src);

  set_availability_label(
    r_src->label_widget, base_name, available,
    "Mixer unavailable at current sample rate"
  );

  g_free(base_name);
}

// Update mixer heading labels for availability at high sample rates
static void update_mixer_headings(struct alsa_card *card) {
  int available = get_sample_rate_category(card->current_sample_rate) != SR_HIGH;

  if (card->routing_mixer_in_heading)
    set_availability_label(
      card->routing_mixer_in_heading, "Mixer\nInputs", available,
      "Mixer unavailable at current sample rate"
    );

  if (card->routing_mixer_out_heading) {
    const char *name = card->has_talkback ? "Mixer Outputs" : "Mixer\nOutputs";
    set_availability_label(
      card->routing_mixer_out_heading, name, available,
      "Mixer unavailable at current sample rate"
    );
  }
}

// Update PCM source label based on channel availability
static void update_pcm_src_label(struct routing_src *r_src) {
  if (!r_src->label_widget || r_src->port_category != PC_PCM)
    return;

  struct alsa_card *card = r_src->card;
  char *base_name = get_src_display_name_formatted(r_src);

  // PCM sources are "PCM Outputs" in routing = playback from PC
  // port_num is 0-based, playback_channels is count
  int available = card->pcm_playback_channels == 0 ||
                  r_src->port_num < card->pcm_playback_channels;

  if (!available) {
    char *markup = g_strdup_printf(
      "<span color=\"#808080\"><s>%s</s></span>", base_name
    );
    gtk_label_set_markup(GTK_LABEL(r_src->label_widget), markup);
    gtk_widget_set_tooltip_text(
      r_src->label_widget,
      "Unavailable at current sample rate"
    );
    g_free(markup);
  } else {
    gtk_label_set_text(GTK_LABEL(r_src->label_widget), base_name);
    gtk_label_set_use_markup(GTK_LABEL(r_src->label_widget), FALSE);
    gtk_widget_set_tooltip_text(r_src->label_widget, NULL);
  }

  g_free(base_name);
}

// Update all PCM labels when channel availability changes
void update_all_pcm_labels(struct alsa_card *card) {
  if (!card->window_routing)
    return;

  // Update PCM sources
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    if (r_src->port_category == PC_PCM)
      update_pcm_src_label(r_src);
  }

  // Update PCM sinks
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    if (r_snk->elem->port_category == PC_PCM)
      update_hw_output_label(r_snk);
  }
}

// Update all HW I/O and mixer labels when availability changes
void update_all_hw_io_labels(struct alsa_card *card) {
  if (!card->routing_snks || !card->routing_srcs)
    return;

  update_hw_io_limits(card);

  // Update routing sinks
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    update_hw_output_label(r_snk);
  }

  // Update routing sources
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    if (r_src->port_category == PC_HW)
      update_hw_input_label(r_src);
    else if (r_src->port_category == PC_MIX)
      update_mixer_src_label(r_src);
  }

  // Update mixer headings
  update_mixer_headings(card);
}

// Callback when monitor group related controls change
static void monitor_group_changed(struct alsa_elem *elem, void *data) {
  struct alsa_card *card = elem->card;

  // Update all hardware output labels and effective sources
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    update_snk_effective_source(r_snk);
    update_hw_output_label(r_snk);
  }
}

static void make_snk_routing_widget(
  struct routing_snk *r_snk,
  char               *name,
  GtkOrientation      orientation
) {
  // create a box, a "socket", and a label
  GtkWidget *box = r_snk->box_widget = gtk_box_new(orientation, 5);
  gtk_widget_add_css_class(box, "route-label");
  GtkWidget *label = r_snk->label_widget = gtk_label_new(name);
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
    gesture, "released", G_CALLBACK(snk_routing_clicked), r_snk
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
  struct routing_snk *r_snk = data;

  if (r_snk)
    update_snk_effective_source(r_snk);

  update_mixer_labels(card);
  gtk_widget_queue_draw(card->routing_lines);
}

static void make_routing_alsa_elem(struct routing_snk *r_snk) {
  struct alsa_elem *elem = r_snk->elem;
  struct alsa_card *card = elem->card;

  // "DSP Input X Capture Enum" controls (DSP Inputs) go along
  // the top, in card->routing_mixer_in_grid
  if (elem->port_category == PC_DSP) {

    char *name = get_snk_display_name_formatted(r_snk);
    make_snk_routing_widget(r_snk, name, GTK_ORIENTATION_VERTICAL);
    g_free(name);
    gtk_grid_attach(
      GTK_GRID(card->routing_dsp_in_grid), r_snk->box_widget,
      elem->port_num + 1, 0, 1, 1
    );

  // "Mixer Input X Capture Enum" controls (Mixer Inputs) go along
  // the top, in card->routing_mixer_in_grid after the DSP Inputs
  } else if (elem->port_category == PC_MIX) {

    if (card->has_fixed_mixer_inputs)
      return;

    char *name = get_snk_display_name_formatted(r_snk);
    make_snk_routing_widget(r_snk, name, GTK_ORIENTATION_VERTICAL);
    g_free(name);
    gtk_grid_attach(
      GTK_GRID(card->routing_mixer_in_grid), r_snk->box_widget,
      elem->port_num + 1, 0, 1, 1
    );

  // "PCM X Capture Enum" controls (PCM Inputs) go along the right,
  // in card->routing_pcm_out_grid
  } else if (elem->port_category == PC_PCM) {
    make_snk_routing_widget(r_snk, "", GTK_ORIENTATION_HORIZONTAL);
    update_hw_output_label(r_snk);

    gtk_grid_attach(
      GTK_GRID(card->routing_pcm_out_grid), r_snk->box_widget,
      0, elem->port_num + 1, 1, 1
    );

  // "* X Playback Enum" controls go along the right, in
  // card->routing_hw_out_grid
  } else if (elem->port_category == PC_HW) {

    make_snk_routing_widget(r_snk, "", GTK_ORIENTATION_HORIZONTAL);
    update_hw_output_label(r_snk);

    gtk_grid_attach(
      GTK_GRID(card->routing_hw_out_grid), r_snk->box_widget,
      0, elem->port_num + 1, 1, 1
    );
  } else {
    printf("invalid port category %d\n", elem->port_category);
  }

  alsa_elem_add_callback(elem, routing_updated, r_snk, NULL);
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

    // set initial visibility based on enable state
    if (r_snk->box_widget) {
      int enabled = is_routing_snk_enabled(r_snk);
      gtk_widget_set_visible(r_snk->box_widget, enabled);
    }
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

    char *name = get_src_display_name_formatted(r_src);

    if (r_src->port_category == PC_DSP) {
      make_src_routing_widget(
        card, r_src, name, GTK_ORIENTATION_VERTICAL
      );
      gtk_grid_attach(
        GTK_GRID(card->routing_dsp_out_grid), r_src->widget,
        r_src->port_num + 1, 0, 1, 1
      );

    } else if (r_src->port_category == PC_MIX) {
      make_src_routing_widget(
        card, r_src, name, GTK_ORIENTATION_VERTICAL
      );
      gtk_grid_attach(
        GTK_GRID(card->routing_mixer_out_grid), r_src->widget,
        r_src->port_num + 1, 0, 1, 1
      );

      if (card->has_talkback) {
        GtkWidget *w = make_talkback_mix_widget(card, r_src);
        if (!w)
          continue;

        r_src->talkback_widget = w;
        gtk_grid_attach(
          GTK_GRID(card->routing_mixer_out_grid), w,
          r_src->port_num + 1, 1, 1, 1
        );
      }
    } else if (r_src->port_category == PC_PCM) {
      make_src_routing_widget(
        card, r_src, name, GTK_ORIENTATION_HORIZONTAL
      );
      gtk_grid_attach(
        GTK_GRID(card->routing_pcm_in_grid), r_src->widget,
        0, r_src->port_num + 1, 1, 1
      );
    } else if (r_src->port_category == PC_HW) {
      make_src_routing_widget(
        card, r_src, name, GTK_ORIENTATION_HORIZONTAL
      );
      gtk_grid_attach(
        GTK_GRID(card->routing_hw_in_grid), r_src->widget,
        0, r_src->port_num + 1, 1, 1
      );
    } else {
      printf("invalid port category %d\n", r_src->port_category);
    }

    g_free(name);

    // set initial visibility based on enable state
    if (r_src->widget) {
      int enabled = is_routing_src_enabled(r_src);
      gtk_widget_set_visible(r_src->widget, enabled);
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

  // Set up monitor group label updates
  // Register callbacks on speaker switching controls
  struct alsa_elem *ss_enum = get_elem_by_name(
    card->elems, "Speaker Switching Playback Enum"
  );
  struct alsa_elem *ss_switch = get_elem_by_name(
    card->elems, "Speaker Switching Playback Switch"
  );
  struct alsa_elem *ss_alt = get_elem_by_name(
    card->elems, "Speaker Switching Alt Playback Switch"
  );

  if (ss_enum)
    alsa_elem_add_callback(ss_enum, monitor_group_changed, NULL, NULL);
  if (ss_switch)
    alsa_elem_add_callback(ss_switch, monitor_group_changed, NULL, NULL);
  if (ss_alt)
    alsa_elem_add_callback(ss_alt, monitor_group_changed, NULL, NULL);

  // Register callbacks on Main/Alt Group controls (switches and sources)
  for (int i = 1; i <= 8; i++) {
    char name[64];

    snprintf(name, sizeof(name), "Main Group Output %d Playback Switch", i);
    struct alsa_elem *main_sw = get_elem_by_name(card->elems, name);
    if (main_sw)
      alsa_elem_add_callback(main_sw, monitor_group_changed, NULL, NULL);

    snprintf(name, sizeof(name), "Alt Group Output %d Playback Switch", i);
    struct alsa_elem *alt_sw = get_elem_by_name(card->elems, name);
    if (alt_sw)
      alsa_elem_add_callback(alt_sw, monitor_group_changed, NULL, NULL);

    snprintf(name, sizeof(name), "Main Group Output %d Source Playback Enum", i);
    struct alsa_elem *main_src = get_elem_by_name(card->elems, name);
    if (main_src)
      alsa_elem_add_callback(main_src, monitor_group_changed, NULL, NULL);

    snprintf(name, sizeof(name), "Alt Group Output %d Source Playback Enum", i);
    struct alsa_elem *alt_src = get_elem_by_name(card->elems, name);
    if (alt_src)
      alsa_elem_add_callback(alt_src, monitor_group_changed, NULL, NULL);
  }

  // Initialize effective source indices for all sinks
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    update_snk_effective_source(r_snk);
  }

  update_mixer_labels(card);

  // set initial visibility of routing sections based on port enable states
  update_routing_section_visibility(card);
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

  // update HW I/O labels for availability based on Digital I/O mode
  // and sample rate (must be after widgets are created)
  update_all_hw_io_labels(card);

  add_drop_controller_motion(card, routing_overlay);

  return top;
}
