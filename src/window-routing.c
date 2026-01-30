// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkhelper.h"
#include "hw-io-availability.h"
#include "iface-mixer.h"
#include "routing-drag-line.h"
#include "routing-lines.h"
#include "stereo-link.h"
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

// Forward declaration for highlight drawing
static void draw_group_highlights(
  GtkDrawingArea *drawing_area,
  cairo_t        *cr,
  int             width,
  int             height,
  void           *user_data
);

// Create a routing group with heading label and grid for widgets.
// Returns a container (hbox or vbox) with heading outside the grid.
// grid_out receives the inner grid for attaching socket/label widgets at row/col 0+
// The grid is wrapped in an overlay with a highlight drawing area below.
static GtkWidget *create_routing_group_grid(
  struct alsa_card *card,
  char             *name,
  char             *descr,
  char             *tooltip,
  GtkOrientation    orientation,
  GtkAlign          align,
  GtkWidget       **heading_label,
  GtkWidget       **grid_out
) {
  GtkWidget *grid = gtk_grid_new();
  gtk_widget_set_name(grid, name);
  gtk_grid_set_spacing(GTK_GRID(grid), 2);
  // Add margin for highlight padding (2px) to avoid clipping
  gtk_widget_set_margin_start(grid, 2);
  gtk_widget_set_margin_end(grid, 2);
  gtk_widget_set_margin_top(grid, 2);
  gtk_widget_set_margin_bottom(grid, 2);

  // Create overlay with highlight drawing area below the grid
  GtkWidget *overlay = gtk_overlay_new();
  GtkWidget *highlight_area = gtk_drawing_area_new();
  gtk_drawing_area_set_draw_func(
    GTK_DRAWING_AREA(highlight_area), draw_group_highlights, card, NULL
  );
  gtk_widget_set_can_target(highlight_area, FALSE);
  gtk_overlay_set_child(GTK_OVERLAY(overlay), highlight_area);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), grid);
  gtk_overlay_set_measure_overlay(GTK_OVERLAY(overlay), grid, TRUE);

  GtkWidget *label = gtk_label_new(descr);
  gtk_widget_set_tooltip_text(label, tooltip);
  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);

  if (heading_label)
    *heading_label = label;

  if (grid_out)
    *grid_out = grid;

  if (orientation == GTK_ORIENTATION_HORIZONTAL) {
    // Horizontal: hbox with label_vbox on left, overlay on right
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_add_css_class(hbox, "controls-content");
    gtk_widget_set_halign(hbox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(hbox, GTK_ALIGN_CENTER);

    // Wrap label in vbox so additional labels can be added (e.g. Talkback)
    GtkWidget *label_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(label_vbox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(label, align);
    gtk_box_append(GTK_BOX(label_vbox), label);

    gtk_box_append(GTK_BOX(hbox), label_vbox);
    gtk_box_append(GTK_BOX(hbox), overlay);
    return hbox;
  }

  // Vertical: vbox with label on top, overlay below
  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_add_css_class(vbox, "controls-content");
  gtk_widget_set_halign(vbox, GTK_ALIGN_FILL);
  gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand(vbox, TRUE);
  gtk_widget_set_halign(label, align);
  gtk_widget_set_halign(overlay, GTK_ALIGN_FILL);

  gtk_box_append(GTK_BOX(vbox), label);
  gtk_box_append(GTK_BOX(vbox), overlay);

  return vbox;
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

  // All grids: container is vbox/hbox, grid is separate for widget attachment
  GtkWidget *hw_in_container = create_routing_group_grid(
    card, "routing_hw_in_grid", "Hardware Inputs",
    "Hardware Inputs are the physical inputs on the interface",
    GTK_ORIENTATION_VERTICAL, GTK_ALIGN_END, NULL,
    &card->routing_hw_in_grid
  );
  GtkWidget *pcm_in_container = create_routing_group_grid(
    card, "routing_pcm_in_grid", "PCM Outputs",
    "PCM Outputs are the digital audio channels sent from the PC to "
    "the interface over USB, used for audio playback",
    GTK_ORIENTATION_VERTICAL, GTK_ALIGN_END, NULL,
    &card->routing_pcm_in_grid
  );
  GtkWidget *pcm_out_container = create_routing_group_grid(
    card, "routing_pcm_out_grid", "PCM Inputs",
    "PCM Inputs are the digital audio channels sent from the interface "
    "to the PC over USB, use for audio recording",
    GTK_ORIENTATION_VERTICAL, GTK_ALIGN_START, NULL,
    &card->routing_pcm_out_grid
  );
  GtkWidget *hw_out_container = create_routing_group_grid(
    card, "routing_hw_out_grid", "Hardware Outputs",
    "Hardware Outputs are the physical outputs on the interface",
    GTK_ORIENTATION_VERTICAL, GTK_ALIGN_START, NULL,
    &card->routing_hw_out_grid
  );

  GtkWidget *dsp_in_container = NULL;
  GtkWidget *dsp_out_container = NULL;
  GtkWidget *mixer_in_container = NULL;
  GtkWidget *mixer_out_container = NULL;

  if (has_dsp) {
    dsp_in_container = create_routing_group_grid(
      card, "routing_dsp_in_grid", "DSP\nInputs",
      "DSP Inputs are used to send audio to the DSP, which is used for "
      "features such as the input level meters, Air mode, and Autogain",
      GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_CENTER, NULL,
      &card->routing_dsp_in_grid
    );
    dsp_out_container = create_routing_group_grid(
      card, "routing_dsp_out_grid", "DSP\nOutputs",
      "DSP Outputs are used to send audio from the DSP after it has "
      "done its processing",
      GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_CENTER, NULL,
      &card->routing_dsp_out_grid
    );
  }
  if (!card->has_fixed_mixer_inputs)
    mixer_in_container = create_routing_group_grid(
      card, "routing_mixer_in_grid", "Mixer\nInputs",
      "Mixer Inputs are used to mix multiple audio channels together",
      GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_CENTER,
      &card->routing_mixer_in_heading, &card->routing_mixer_in_grid
    );
  mixer_out_container = create_routing_group_grid(
    card, "routing_mixer_out_grid",
    card->has_talkback ? "Mixer Outputs" : "Mixer\nOutputs",
    "Mixer Outputs are used to send audio from the mixer",
    GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_CENTER,
    &card->routing_mixer_out_heading, &card->routing_mixer_out_grid
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

  gtk_grid_attach(routing_grid, hw_in_container, left_col_num, 1, 1, 1);
  gtk_grid_attach(routing_grid, pcm_in_container, left_col_num, 2, 1, 1);
  gtk_grid_attach(routing_grid, pcm_out_container, right_col_num, 1, 1, 1);
  gtk_grid_attach(routing_grid, hw_out_container, right_col_num, 2, 1, 1);
  if (has_dsp) {
    gtk_grid_attach(routing_grid, dsp_in_container, dsp_col_num, 0, 1, 1);
    gtk_grid_attach(routing_grid, dsp_out_container, dsp_col_num, 3, 1, 1);
  }
  if (!card->has_fixed_mixer_inputs)
    gtk_grid_attach(routing_grid, mixer_in_container, mix_col_num, 0, 1, 1);
  gtk_grid_attach(routing_grid, mixer_out_container, mix_col_num, 3, 1, 1);
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

// Update socket widget to show mono or stereo based on link state
// orientation: GTK_ORIENTATION_HORIZONTAL for side-by-side sockets
//              GTK_ORIENTATION_VERTICAL for stacked sockets
static void update_socket_widget(
  GtkWidget      *socket,
  int             is_linked,
  GtkOrientation  orientation
) {
  const char *resource;

  if (is_linked) {
    resource = orientation == GTK_ORIENTATION_HORIZONTAL
      ? "/vu/b4/alsa-scarlett-gui/icons/socket-stereo-h.svg"
      : "/vu/b4/alsa-scarlett-gui/icons/socket-stereo-v.svg";
  } else {
    resource = "/vu/b4/alsa-scarlett-gui/icons/socket.svg";
  }

  gtk_picture_set_resource(GTK_PICTURE(socket), resource);
}

// Get the appropriate socket orientation for a port category
// This refers to how L/R are arranged in the stereo socket icon
static GtkOrientation get_socket_orientation(int port_category) {
  // Mixer and DSP ports are arranged horizontally, L/R side-by-side
  // HW and PCM ports are arranged vertically, L/R stacked
  return (port_category == PC_MIX || port_category == PC_DSP)
    ? GTK_ORIENTATION_HORIZONTAL
    : GTK_ORIENTATION_VERTICAL;
}

// Get stereo-aware base name for a source
// Returns newly allocated string that must be freed
static char *get_src_stereo_aware_name(struct routing_src *src) {
  if (is_src_linked(src) && is_src_left_channel(src)) {
    if (src->port_category == PC_MIX)
      return g_strdup_printf(
        "%c\xe2\x80\x93%c", src->port_num + 'A', src->port_num + 'B'
      );
    if (src->port_category == PC_DSP)
      return g_strdup_printf(
        "%d\xe2\x80\x93%d", src->lr_num, src->lr_num + 1
      );
    return get_src_pair_display_name(src);
  }
  return get_src_display_name_formatted(src);
}

// Get stereo-aware base name for a sink
// Returns newly allocated string that must be freed
static char *get_snk_stereo_aware_name(struct routing_snk *snk) {
  if (is_snk_linked(snk) && is_snk_left_channel(snk)) {
    // Mixer/DSP use simple abbreviated stereo labels in routing window
    int port_cat = snk->elem->port_category;
    if (port_cat == PC_MIX || port_cat == PC_DSP) {
      int lr = snk->elem->lr_num;
      return g_strdup_printf("%d–%d", lr, lr + 1);
    } else {
      return get_snk_pair_display_name(snk);
    }
  }
  return get_snk_display_name_formatted(snk);
}

// Label width for mono/stereo display
#define LABEL_WIDTH_MONO   3
#define LABEL_WIDTH_STEREO 4

// Helper to check if a source should be visible in routing grid
static int is_src_visible(struct routing_src *r_src) {
  return is_routing_src_enabled(r_src) && should_display_src(r_src);
}

// Helper to check if a sink should be visible in routing grid
static int is_snk_visible(struct routing_snk *r_snk) {
  return is_routing_snk_enabled(r_snk) && should_display_snk(r_snk);
}

// Helper to unparent a widget from a grid if it has a parent
static void unparent_from_grid(GtkWidget *grid, GtkWidget *widget) {
  if (widget && gtk_widget_get_parent(widget))
    gtk_grid_remove(GTK_GRID(grid), widget);
}

// Get the routing grid for a source port category
static GtkWidget *get_src_grid(struct alsa_card *card, int port_category) {
  switch (port_category) {
    case PC_MIX: return card->routing_mixer_out_grid;
    case PC_DSP: return card->routing_dsp_out_grid;
    case PC_HW:  return card->routing_hw_in_grid;
    case PC_PCM: return card->routing_pcm_in_grid;
    default:     return NULL;
  }
}

// Get the routing grid for a sink port category
static GtkWidget *get_snk_grid(struct alsa_card *card, int port_category) {
  switch (port_category) {
    case PC_MIX: return card->routing_mixer_in_grid;
    case PC_DSP: return card->routing_dsp_in_grid;
    case PC_HW:  return card->routing_hw_out_grid;
    case PC_PCM: return card->routing_pcm_out_grid;
    default:     return NULL;
  }
}

// Check if port category uses horizontal layout (MIX/DSP)
static int is_horiz_port_category(int port_category) {
  return port_category == PC_MIX || port_category == PC_DSP;
}

// Release refs held on routing widgets.
// Called before the routing window is destroyed.
void cleanup_routing_widgets(struct alsa_card *card) {
  // Clear hover state to prevent dangling pointers
  card->hovered_src = NULL;
  card->hovered_snk = NULL;

  // Clean up source widgets
  if (card->routing_srcs) {
    for (int i = 1; i < card->routing_srcs->len; i++) {
      struct routing_src *r_src = &g_array_index(
        card->routing_srcs, struct routing_src, i
      );

      if (r_src->widget) {
        g_object_unref(r_src->widget);
        r_src->widget = NULL;
        r_src->widget2 = NULL;
      }
      if (r_src->label_widget) {
        g_object_unref(r_src->label_widget);
        r_src->label_widget = NULL;
      }
      if (r_src->talkback_widget) {
        g_object_unref(r_src->talkback_widget);
        r_src->talkback_widget = NULL;
      }
    }
  }

  // Clean up sink widgets
  if (card->routing_snks) {
    for (int i = 0; i < card->routing_snks->len; i++) {
      struct routing_snk *r_snk = &g_array_index(
        card->routing_snks, struct routing_snk, i
      );

      if (r_snk->socket_widget) {
        g_object_unref(r_snk->socket_widget);
        r_snk->socket_widget = NULL;
      }
      if (r_snk->label_widget) {
        g_object_unref(r_snk->label_widget);
        r_snk->label_widget = NULL;
      }
    }
  }
}

// Arrange source widgets in their grid.
// Removes all widgets and re-attaches only visible ones at consecutive positions.
void arrange_src_grid(struct alsa_card *card, int port_category) {
  GtkWidget *grid = get_src_grid(card, port_category);
  if (!grid)
    return;

  int is_horiz = is_horiz_port_category(port_category);
  int pos = 0;

  for (int i = 1; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    if (r_src->port_category != port_category || !r_src->widget)
      continue;

    // Unparent all widgets
    unparent_from_grid(grid, r_src->widget);
    unparent_from_grid(grid, r_src->label_widget);
    unparent_from_grid(grid, r_src->talkback_widget);

    if (!is_src_visible(r_src))
      continue;

    if (is_horiz) {
      // Horizontal: socket at row 0, talkback/label at row 1
      gtk_grid_attach(GTK_GRID(grid), r_src->widget, pos, 0, 1, 1);
      if (r_src->talkback_widget)
        gtk_grid_attach(GTK_GRID(grid), r_src->talkback_widget, pos, 1, 1, 1);
      else if (r_src->label_widget)
        gtk_grid_attach(GTK_GRID(grid), r_src->label_widget, pos, 1, 1, 1);
    } else {
      // Vertical: label at col 0, socket at col 1
      gtk_grid_attach(GTK_GRID(grid), r_src->label_widget, 0, pos, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), r_src->widget, 1, pos, 1, 1);
    }
    pos++;
  }

  if (card->routing_lines)
    gtk_widget_queue_draw(card->routing_lines);
}

// Arrange sink widgets in their grid.
void arrange_snk_grid(struct alsa_card *card, int port_category) {
  GtkWidget *grid = get_snk_grid(card, port_category);
  if (!grid)
    return;

  // Skip for fixed mixer inputs (no routing widgets)
  if (port_category == PC_MIX && card->has_fixed_mixer_inputs)
    return;

  int is_horiz = is_horiz_port_category(port_category);
  int pos = 0;

  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (!r_snk->elem ||
        r_snk->elem->port_category != port_category ||
        !r_snk->socket_widget)
      continue;

    // Unparent all widgets
    unparent_from_grid(grid, r_snk->socket_widget);
    unparent_from_grid(grid, r_snk->label_widget);

    if (!is_snk_visible(r_snk))
      continue;

    if (is_horiz) {
      // Horizontal: label at row 0, socket at row 1 (sinks point down)
      gtk_grid_attach(GTK_GRID(grid), r_snk->label_widget, pos, 0, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), r_snk->socket_widget, pos, 1, 1, 1);
    } else {
      // Vertical: socket at col 0, label at col 1
      gtk_grid_attach(GTK_GRID(grid), r_snk->socket_widget, 0, pos, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), r_snk->label_widget, 1, pos, 1, 1);
    }
    pos++;
  }

  if (card->routing_lines)
    gtk_widget_queue_draw(card->routing_lines);
}

// Callback to update source socket and label when link state changes
static void src_socket_link_changed(struct alsa_elem *elem, void *private) {
  struct routing_src *src = private;

  if (!src->widget2)
    return;

  int linked = is_src_linked(src);
  GtkOrientation orient = get_socket_orientation(src->port_category);
  update_socket_widget(src->widget2, linked, orient);

  update_routing_src_label(src);

  // Rearrange grid (only after routing window is fully initialized)
  if (src->card->routing_lines)
    arrange_src_grid(src->card, src->port_category);
}

// Callback to update sink socket and label when link state changes
static void snk_socket_link_changed(struct alsa_elem *elem, void *private) {
  struct routing_snk *snk = private;

  if (!snk->socket_widget)
    return;

  int linked = is_snk_linked(snk);
  int port_category = snk->elem->port_category;
  GtkOrientation orient = get_socket_orientation(port_category);
  update_socket_widget(snk->socket_widget, linked, orient);

  update_hw_output_label(snk);

  // Rearrange grid (only after routing window is fully initialized)
  if (snk->elem->card->routing_lines)
    arrange_snk_grid(snk->elem->card, port_category);
}

// Helper to draw a rounded highlight rectangle
static void draw_highlight_rect(
  cairo_t               *cr,
  const graphene_rect_t *bounds
) {
  double x = graphene_rect_get_x(bounds);
  double y = graphene_rect_get_y(bounds);
  double w = graphene_rect_get_width(bounds);
  double h = graphene_rect_get_height(bounds);
  double radius = 4.0;

  // Add some padding
  double pad = 2.0;
  x -= pad;
  y -= pad;
  w += pad * 2;
  h += pad * 2;

  // Clamp radius to half the smallest dimension
  if (radius > w / 2) radius = w / 2;
  if (radius > h / 2) radius = h / 2;

  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - radius, y + radius, radius, -M_PI_2, 0);
  cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, M_PI_2);
  cairo_arc(cr, x + radius, y + h - radius, radius, M_PI_2, M_PI);
  cairo_arc(cr, x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);

  // #801010 colour
  cairo_set_source_rgb(cr, 0.5, 0.063, 0.063);
  cairo_fill(cr);
}

// Get combined bounds of socket + label (or socket + talkback) relative to a widget
static int get_src_bounds_relative(
  struct routing_src *src,
  GtkWidget          *relative_to,
  graphene_rect_t    *bounds
) {
  if (!src->widget2)
    return 0;

  graphene_rect_t socket_bounds;
  if (!gtk_widget_compute_bounds(src->widget2, relative_to, &socket_bounds))
    return 0;

  // For talkback sources, union socket and talkback bounds, extend to cell top
  if (src->talkback_widget) {
    graphene_rect_t talkback_bounds;
    if (!gtk_widget_compute_bounds(
          src->talkback_widget, relative_to, &talkback_bounds))
      return 0;

    graphene_rect_union(&socket_bounds, &talkback_bounds, bounds);

    // Extend to cell top: compute how far socket is from grid top
    GtkWidget *grid = gtk_widget_get_parent(src->widget2);
    if (grid) {
      graphene_rect_t socket_in_grid;
      if (gtk_widget_compute_bounds(src->widget2, grid, &socket_in_grid)) {
        float extend = graphene_rect_get_y(&socket_in_grid);
        if (extend > 0) {
          graphene_rect_init(
            bounds,
            graphene_rect_get_x(bounds),
            graphene_rect_get_y(bounds) - extend,
            graphene_rect_get_width(bounds),
            graphene_rect_get_height(bounds) + extend
          );
        }
      }
    }
    return 1;
  }

  // Normal case: socket + label
  if (!src->label_widget)
    return 0;

  graphene_rect_t label_bounds;
  if (!gtk_widget_compute_bounds(src->label_widget, relative_to, &label_bounds))
    return 0;

  graphene_rect_union(&socket_bounds, &label_bounds, bounds);
  return 1;
}

static int get_snk_bounds_relative(
  struct routing_snk *snk,
  GtkWidget          *relative_to,
  graphene_rect_t    *bounds
) {
  if (!snk->socket_widget || !snk->label_widget)
    return 0;

  graphene_rect_t socket_bounds, label_bounds;
  if (!gtk_widget_compute_bounds(snk->socket_widget, relative_to, &socket_bounds))
    return 0;
  if (!gtk_widget_compute_bounds(snk->label_widget, relative_to, &label_bounds))
    return 0;

  graphene_rect_union(&socket_bounds, &label_bounds, bounds);
  return 1;
}

// Draw highlights for hovered sources/sinks in a group's overlay
static void draw_group_highlights(
  GtkDrawingArea *drawing_area,
  cairo_t        *cr,
  int             width,
  int             height,
  void           *user_data
) {
  struct alsa_card *card = user_data;
  GtkWidget *highlight_widget = GTK_WIDGET(drawing_area);

  // Get the grid (sibling in the overlay)
  GtkWidget *overlay = gtk_widget_get_parent(highlight_widget);
  if (!overlay)
    return;

  // Check hovered source
  if (card->hovered_src) {
    struct routing_src *src = card->hovered_src;

    // Check if this source's widget is in this group's grid
    GtkWidget *src_parent = src->widget2
      ? gtk_widget_get_parent(src->widget2) : NULL;
    if (src_parent && gtk_widget_get_parent(src_parent) == overlay) {
      graphene_rect_t bounds;
      if (get_src_bounds_relative(src, highlight_widget, &bounds))
        draw_highlight_rect(cr, &bounds);

      // Also draw partner if stereo-linked
      if (is_src_linked(src)) {
        struct routing_src *partner = get_src_partner(src);
        if (partner && get_src_bounds_relative(partner, highlight_widget, &bounds))
          draw_highlight_rect(cr, &bounds);
      }
    }
  }

  // Check hovered sink
  if (card->hovered_snk) {
    struct routing_snk *snk = card->hovered_snk;

    // Check if this sink's widget is in this group's grid
    GtkWidget *snk_parent = snk->socket_widget
      ? gtk_widget_get_parent(snk->socket_widget) : NULL;
    if (snk_parent && gtk_widget_get_parent(snk_parent) == overlay) {
      graphene_rect_t bounds;
      if (get_snk_bounds_relative(snk, highlight_widget, &bounds))
        draw_highlight_rect(cr, &bounds);

      // Also draw partner if stereo-linked
      if (is_snk_linked(snk)) {
        struct routing_snk *partner = get_snk_partner(snk);
        if (partner && get_snk_bounds_relative(partner, highlight_widget, &bounds))
          draw_highlight_rect(cr, &bounds);
      }
    }
  }
}

// Queue redraw on all group highlight areas
static void queue_redraw_group_highlights(struct alsa_card *card) {
  GtkWidget *grids[] = {
    card->routing_hw_in_grid,
    card->routing_hw_out_grid,
    card->routing_pcm_in_grid,
    card->routing_pcm_out_grid,
    card->routing_dsp_in_grid,
    card->routing_dsp_out_grid,
    card->routing_mixer_in_grid,
    card->routing_mixer_out_grid,
  };

  for (int i = 0; i < 8; i++) {
    if (!grids[i])
      continue;
    // Grid's parent is the overlay; overlay's child is the highlight area
    GtkWidget *overlay = gtk_widget_get_parent(grids[i]);
    if (overlay) {
      GtkWidget *highlight_area = gtk_overlay_get_child(GTK_OVERLAY(overlay));
      if (highlight_area)
        gtk_widget_queue_draw(highlight_area);
    }
  }
}

// Motion handler for overlay-based hover detection
static void routing_overlay_motion(
  GtkEventControllerMotion *controller,
  double                    x,
  double                    y,
  gpointer                  user_data
) {
  struct alsa_card *card = user_data;
  GtkWidget *overlay = gtk_event_controller_get_widget(
    GTK_EVENT_CONTROLLER(controller)
  );

  struct routing_src *new_hovered_src = NULL;
  struct routing_snk *new_hovered_snk = NULL;
  graphene_point_t point = GRAPHENE_POINT_INIT(x, y);

  // Check sources (skip "Off" at index 0)
  for (int i = 1; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    if (!is_routing_src_enabled(src) || !src->widget2)
      continue;

    // For linked pairs, only hit-test the left channel
    if (is_src_linked(src) && !is_src_left_channel(src))
      continue;

    graphene_rect_t bounds;
    if (!get_src_bounds_relative(src, overlay, &bounds))
      continue;

    if (graphene_rect_contains_point(&bounds, &point)) {
      new_hovered_src = src;
      break;
    }
  }

  // Check sinks if no source was hit
  if (!new_hovered_src) {
    for (int i = 0; i < card->routing_snks->len; i++) {
      struct routing_snk *snk = &g_array_index(
        card->routing_snks, struct routing_snk, i
      );

      if (!is_routing_snk_enabled(snk))
        continue;

      // For linked pairs, only hit-test the left channel
      if (is_snk_linked(snk) && !is_snk_left_channel(snk))
        continue;

      graphene_rect_t bounds;
      if (!get_snk_bounds_relative(snk, overlay, &bounds))
        continue;

      if (graphene_rect_contains_point(&bounds, &point)) {
        new_hovered_snk = snk;
        break;
      }
    }
  }

  // Update hover state if changed
  if (new_hovered_src != card->hovered_src ||
      new_hovered_snk != card->hovered_snk) {
    card->hovered_src = new_hovered_src;
    card->hovered_snk = new_hovered_snk;
    queue_redraw_group_highlights(card);
    gtk_widget_queue_draw(card->routing_lines);
  }
}

// Leave handler for overlay-based hover detection
static void routing_overlay_leave(
  GtkEventControllerMotion *controller,
  gpointer                  user_data
) {
  struct alsa_card *card = user_data;

  if (card->hovered_src || card->hovered_snk) {
    card->hovered_src = NULL;
    card->hovered_snk = NULL;
    queue_redraw_group_highlights(card);
    gtk_widget_queue_draw(card->routing_lines);
  }
}

// Forward declarations for functions used by unified handlers
static struct alsa_elem *get_snk_routing_elem(struct routing_snk *r_snk);
static void route_src_to_snk(
  struct alsa_card   *card,
  struct routing_snk *r_snk,
  int                 src_id
);

// Find routing source at point (returns NULL if none)
static struct routing_src *find_src_at_point(
  struct alsa_card *card,
  GtkWidget        *relative_to,
  double            x,
  double            y
) {
  graphene_point_t point = GRAPHENE_POINT_INIT(x, y);

  for (int i = 1; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    if (!is_routing_src_enabled(src) || !src->widget2)
      continue;

    if (is_src_linked(src) && !is_src_left_channel(src))
      continue;

    graphene_rect_t bounds;
    if (!get_src_bounds_relative(src, relative_to, &bounds))
      continue;

    if (graphene_rect_contains_point(&bounds, &point))
      return src;
  }

  return NULL;
}

// Find routing sink at point (returns NULL if none)
static struct routing_snk *find_snk_at_point(
  struct alsa_card *card,
  GtkWidget        *relative_to,
  double            x,
  double            y
) {
  graphene_point_t point = GRAPHENE_POINT_INIT(x, y);

  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (!is_routing_snk_enabled(snk))
      continue;

    if (is_snk_linked(snk) && !is_snk_left_channel(snk))
      continue;

    graphene_rect_t bounds;
    if (!get_snk_bounds_relative(snk, relative_to, &bounds))
      continue;

    if (graphene_rect_contains_point(&bounds, &point))
      return snk;
  }

  return NULL;
}

// Unified click handler using hit-testing
static void routing_overlay_click(
  GtkGestureClick *gesture,
  int              n_press,
  double           x,
  double           y,
  gpointer         user_data
) {
  struct alsa_card *card = user_data;
  GtkWidget *widget = gtk_event_controller_get_widget(
    GTK_EVENT_CONTROLLER(gesture)
  );

  struct routing_src *r_src = find_src_at_point(card, widget, x, y);
  struct routing_snk *r_snk = find_snk_at_point(card, widget, x, y);

  if (r_src) {
    // Clear all sinks connected to this source
    for (int i = 0; i < card->routing_snks->len; i++) {
      struct routing_snk *snk = &g_array_index(
        card->routing_snks, struct routing_snk, i
      );

      int connected = snk->effective_source_idx == r_src->id;
      if (!connected && is_src_linked(r_src)) {
        struct routing_src *partner = get_src_partner(r_src);
        if (partner)
          connected = snk->effective_source_idx == partner->id;
      }

      if (!connected)
        continue;

      struct alsa_elem *target_elem = get_snk_routing_elem(snk);
      if (target_elem && target_elem == snk->elem)
        alsa_set_elem_value(target_elem, 0);
    }
  } else if (r_snk) {
    struct alsa_elem *target_elem = get_snk_routing_elem(r_snk);

    if (!target_elem || target_elem != r_snk->elem)
      return;

    if (is_snk_linked(r_snk)) {
      struct routing_snk *partner = get_snk_partner(r_snk);
      if (partner) {
        route_src_to_snk(card, r_snk, 0);
        route_src_to_snk(card, partner, 0);
        return;
      }
    }

    alsa_set_elem_value(target_elem, 0);
  }
}

// Drag source prepare handler - hit-test to find drag origin
static GdkContentProvider *routing_drag_prepare(
  GtkDragSource *source,
  double         x,
  double         y,
  gpointer       user_data
) {
  struct alsa_card *card = user_data;
  GtkWidget *widget = gtk_event_controller_get_widget(
    GTK_EVENT_CONTROLLER(source)
  );

  struct routing_src *r_src = find_src_at_point(card, widget, x, y);
  struct routing_snk *r_snk = find_snk_at_point(card, widget, x, y);

  if (r_src) {
    card->drag_type = DRAG_TYPE_SRC;
    card->src_drag = r_src;
    return gdk_content_provider_new_typed(G_TYPE_INT, r_src->id);
  } else if (r_snk) {
    card->drag_type = DRAG_TYPE_SNK;
    card->snk_drag = r_snk;
    return gdk_content_provider_new_typed(G_TYPE_INT, 0x8000 | r_snk->idx);
  }

  return NULL;
}

// Check if a routing operation from source to sink is valid
static int is_routing_valid(
  struct alsa_card   *card,
  struct routing_src *r_src,
  struct routing_snk *r_snk
) {
  // Reject drops on muted sinks
  if (is_snk_monitor_muted(r_snk))
    return 0;

  // Reject stereo source → mono sink
  if (r_src && is_src_linked(r_src) && !is_snk_linked(r_snk))
    return 0;

  // Reject mixer → mixer routing if device doesn't support it
  if (!card->mixer_has_mix_srcs &&
      r_src && r_src->port_category == PC_MIX &&
      r_snk->elem->port_category == PC_MIX)
    return 0;

  return 1;
}

// Drag source end handler
static void routing_drag_end(
  GtkDragSource *source,
  GdkDrag       *drag,
  gboolean       delete_data,
  gpointer       user_data
) {
  struct alsa_card *card = user_data;

  card->drag_type = DRAG_TYPE_NONE;
  card->src_drag = NULL;
  card->snk_drag = NULL;

  // Recompute hover from last known position (coords are relative to routing_grid)
  if (card->drag_x >= 0 && card->drag_y >= 0) {
    card->hovered_src = find_src_at_point(
      card, card->routing_grid, card->drag_x, card->drag_y
    );
    card->hovered_snk = find_snk_at_point(
      card, card->routing_grid, card->drag_x, card->drag_y
    );
  }

  // Clear drag coords now that we're done with them
  card->drag_x = -1;
  card->drag_y = -1;

  queue_redraw_group_highlights(card);
  gtk_widget_queue_draw(card->drag_line);
  gtk_widget_queue_draw(card->routing_lines);
}

// Drop target motion handler - hit-test for drop feedback
static GdkDragAction routing_drop_motion(
  GtkDropTarget *target,
  double         x,
  double         y,
  gpointer       user_data
) {
  struct alsa_card *card = user_data;
  GtkWidget *widget = gtk_event_controller_get_widget(
    GTK_EVENT_CONTROLLER(target)
  );

  struct routing_src *r_src = find_src_at_point(card, widget, x, y);
  struct routing_snk *r_snk = find_snk_at_point(card, widget, x, y);

  // Update drag target and hover state for visual feedback
  if (card->drag_type == DRAG_TYPE_SRC && r_snk &&
      is_routing_valid(card, card->src_drag, r_snk)) {
    card->snk_drag = r_snk;
    card->hovered_snk = r_snk;
    card->hovered_src = NULL;
    queue_redraw_group_highlights(card);
    gtk_widget_queue_draw(card->drag_line);
    gtk_widget_queue_draw(card->routing_lines);
    return GDK_ACTION_COPY;
  } else if (card->drag_type == DRAG_TYPE_SNK && r_src &&
             is_routing_valid(card, r_src, card->snk_drag)) {
    card->src_drag = r_src;
    card->hovered_src = r_src;
    card->hovered_snk = NULL;
    queue_redraw_group_highlights(card);
    gtk_widget_queue_draw(card->drag_line);
    gtk_widget_queue_draw(card->routing_lines);
    return GDK_ACTION_COPY;
  }

  // Clear any previous drop target and hover state
  if (card->drag_type == DRAG_TYPE_SRC) {
    card->snk_drag = NULL;
    card->hovered_snk = NULL;
  } else if (card->drag_type == DRAG_TYPE_SNK) {
    card->src_drag = NULL;
    card->hovered_src = NULL;
  }

  queue_redraw_group_highlights(card);
  gtk_widget_queue_draw(card->drag_line);
  gtk_widget_queue_draw(card->routing_lines);
  return 0;
}

// Drop target leave handler
static void routing_drop_leave(
  GtkDropTarget *target,
  gpointer       user_data
) {
  struct alsa_card *card = user_data;

  if (card->drag_type == DRAG_TYPE_SRC) {
    card->snk_drag = NULL;
    card->hovered_snk = NULL;
  } else if (card->drag_type == DRAG_TYPE_SNK) {
    card->src_drag = NULL;
    card->hovered_src = NULL;
  }

  queue_redraw_group_highlights(card);
  gtk_widget_queue_draw(card->drag_line);
  gtk_widget_queue_draw(card->routing_lines);
}

// Drop target drop handler - perform the routing connection
static gboolean routing_drop(
  GtkDropTarget *target,
  const GValue  *value,
  double         x,
  double         y,
  gpointer       user_data
) {
  struct alsa_card *card = user_data;
  GtkWidget *widget = gtk_event_controller_get_widget(
    GTK_EVENT_CONTROLLER(target)
  );

  struct routing_src *r_src = find_src_at_point(card, widget, x, y);
  struct routing_snk *r_snk = find_snk_at_point(card, widget, x, y);

  // Dropped on sink while dragging from source
  if (card->drag_type == DRAG_TYPE_SRC && r_snk && card->src_drag) {
    struct routing_src *drag_src = card->src_drag;

    if (!is_routing_valid(card, drag_src, r_snk))
      return FALSE;

    if (is_snk_linked(r_snk)) {
      struct routing_snk *snk_partner = get_snk_partner(r_snk);
      if (snk_partner) {
        if (is_src_linked(drag_src)) {
          // Stereo → Stereo: route L→L, R→R
          struct routing_src *src_partner = get_src_partner(drag_src);
          if (src_partner) {
            route_src_to_snk(card, r_snk, drag_src->id);
            route_src_to_snk(card, snk_partner, src_partner->id);
            return TRUE;
          }
        } else {
          // Mono → Stereo: route same source to both
          route_src_to_snk(card, r_snk, drag_src->id);
          route_src_to_snk(card, snk_partner, drag_src->id);
          return TRUE;
        }
      }
    }

    route_src_to_snk(card, r_snk, drag_src->id);
    return TRUE;
  }

  // Dropped on source while dragging from sink
  if (card->drag_type == DRAG_TYPE_SNK && r_src && card->snk_drag) {
    struct routing_snk *drag_snk = card->snk_drag;

    if (!is_routing_valid(card, r_src, drag_snk))
      return FALSE;

    if (is_snk_linked(drag_snk)) {
      struct routing_snk *snk_partner = get_snk_partner(drag_snk);
      if (snk_partner) {
        if (is_src_linked(r_src)) {
          // Stereo → Stereo: route L→L, R→R
          struct routing_src *src_partner = get_src_partner(r_src);
          if (src_partner) {
            route_src_to_snk(card, drag_snk, r_src->id);
            route_src_to_snk(card, snk_partner, src_partner->id);
            return TRUE;
          }
        } else {
          // Mono → Stereo: route same source to both
          route_src_to_snk(card, drag_snk, r_src->id);
          route_src_to_snk(card, snk_partner, r_src->id);
          return TRUE;
        }
      }
    }

    route_src_to_snk(card, drag_snk, r_src->id);
    return TRUE;
  }

  return FALSE;
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

static int has_any_main_group_enabled(struct alsa_card *card) {
  for (int i = 1; i <= 8; i++) {
    char name[64];
    snprintf(name, sizeof(name),
             "Main Group Output %d Playback Switch", i);
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
    if (has_any_alt_group_enabled(card)) {
      // Alt outputs configured: check which group is active
      struct alsa_elem *alt = get_elem_by_name(
        card->elems, "Speaker Switching Alt Playback Switch"
      );
      if (alt && alsa_get_elem_value(alt))
        return SPEAKER_SWITCH_ALT;
      return SPEAKER_SWITCH_MAIN;
    }

    // No alt outputs: show Main indicator if main outputs exist
    if (has_any_main_group_enabled(card))
      return SPEAKER_SWITCH_MAIN;

    return SPEAKER_SWITCH_OFF;
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
int is_snk_monitor_muted(struct routing_snk *r_snk) {
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

// Route a source to a sink, handling VG source conversion
static void route_src_to_snk(
  struct alsa_card   *card,
  struct routing_snk *snk,
  int                 src_id
) {
  struct alsa_elem *target = get_snk_routing_elem(snk);
  if (!target)
    return;

  if (target != snk->elem)
    src_id = routing_src_to_vg_src(card, src_id);

  alsa_set_elem_value(target, src_id);
}

// Set up a source socket widget (stereo handling only - click/drag at overlay level)
static void setup_src_socket(struct routing_src *r_src) {
  GtkWidget *socket = make_socket_widget();
  r_src->widget = g_object_ref(socket);
  r_src->widget2 = socket;

  g_object_set_data(G_OBJECT(socket), "routing_src", r_src);
  gtk_widget_add_css_class(socket, "route-label");

  // Stereo link handling
  struct alsa_elem *link_elem = get_src_link_elem(r_src);
  if (link_elem) {
    alsa_elem_add_callback(link_elem, src_socket_link_changed, r_src, NULL);
    if (is_src_linked(r_src)) {
      GtkOrientation orient = get_socket_orientation(r_src->port_category);
      update_socket_widget(socket, 1, orient);
    }
  }

  // Pair name change handling
  struct alsa_elem *pair_name_elem = get_src_pair_name_elem(r_src);
  if (pair_name_elem)
    alsa_elem_add_callback(pair_name_elem, src_socket_link_changed, r_src, NULL);
}

// Create and configure a routing label widget
// Returns a ref'd widget; caller should store the pointer
static GtkWidget *make_routing_label(
  const char *name,
  const char *data_key,
  void       *data_value,
  int         port_category,
  int         is_linked,
  float       vert_xalign
) {
  GtkWidget *label = gtk_label_new(name);
  g_object_ref(label);

  gtk_widget_add_css_class(label, "route-label");
  g_object_set_data(G_OBJECT(label), data_key, data_value);

  if (is_horiz_port_category(port_category)) {
    int width = is_linked ? LABEL_WIDTH_STEREO : LABEL_WIDTH_MONO;
    gtk_label_set_width_chars(GTK_LABEL(label), width);
  } else {
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_FILL);
    gtk_label_set_xalign(GTK_LABEL(label), vert_xalign);
  }

  return label;
}

// Set up a source label widget
static void setup_src_label(struct routing_src *r_src, const char *name) {
  r_src->label_widget = make_routing_label(
    name, "routing_src", r_src,
    r_src->port_category, is_src_linked(r_src), 1.0
  );
}

// Set up a sink socket widget (stereo handling only - click/drag at overlay level)
static void setup_snk_socket(struct routing_snk *r_snk) {
  GtkWidget *socket = make_socket_widget();
  r_snk->socket_widget = g_object_ref(socket);

  g_object_set_data(G_OBJECT(socket), "routing_snk", r_snk);
  gtk_widget_add_css_class(socket, "route-label");

  // Stereo link handling
  struct alsa_elem *link_elem = get_snk_link_elem(r_snk);
  if (link_elem) {
    alsa_elem_add_callback(link_elem, snk_socket_link_changed, r_snk, NULL);
    if (is_snk_linked(r_snk)) {
      GtkOrientation orient = get_socket_orientation(r_snk->elem->port_category);
      update_socket_widget(socket, 1, orient);
    }
  }

  // Pair name change handling
  struct alsa_elem *pair_name_elem = get_snk_pair_name_elem(r_snk);
  if (pair_name_elem)
    alsa_elem_add_callback(pair_name_elem, snk_socket_link_changed, r_snk, NULL);
}

// Set up a sink label widget
static void setup_snk_label(struct routing_snk *r_snk, const char *name) {
  r_snk->label_widget = make_routing_label(
    name, "routing_snk", r_snk,
    r_snk->elem->port_category, is_snk_linked(r_snk), 0.0
  );
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

  // Store the element in the routing source for sync callbacks
  r_src->talkback_elem = talkback_elem;

  // Register talkback sync callback for stereo linking
  stereo_link_register_talkback_callback(r_src);

  // Use stereo-aware name (e.g., "A" or "A–B" when linked)
  char *display_name = get_src_stereo_aware_name(r_src);
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

  // Get the display name (stereo-aware, handles custom names)
  char *base_name = get_snk_stereo_aware_name(r_snk);

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

// Update routing source label (stereo-aware name with availability)
void update_routing_src_label(struct routing_src *r_src) {
  if (!r_src->label_widget)
    return;

  char *name = get_src_stereo_aware_name(r_src);
  int available = 1;
  const char *tooltip = NULL;
  struct alsa_card *card = r_src->card;

  switch (r_src->port_category) {
    case PC_MIX:
      available =
        get_sample_rate_category(card->current_sample_rate) != SR_HIGH;
      tooltip = "Mixer unavailable at current sample rate";
      break;
    case PC_HW:
      if (is_digital_io_type(r_src->hw_type)) {
        int max_port = r_src->hw_type == HW_TYPE_SPDIF
          ? card->max_spdif_in : card->max_adat_in;
        available = max_port < 0 || r_src->lr_num <= max_port;
        tooltip =
          "Unavailable with current Digital I/O mode and sample rate";
      }
      break;
    case PC_PCM:
      available = card->pcm_playback_channels == 0 ||
                  r_src->port_num < card->pcm_playback_channels;
      tooltip = "Unavailable at current sample rate";
      break;
  }

  set_availability_label(r_src->label_widget, name, available, tooltip);

  // For horizontal grids, set width based on mono/stereo
  if (is_horiz_port_category(r_src->port_category)) {
    int width = is_src_linked(r_src) ? LABEL_WIDTH_STEREO : LABEL_WIDTH_MONO;
    gtk_label_set_width_chars(GTK_LABEL(r_src->label_widget), width);
  }

  // Preserve tooltip for ellipsised labels (vertical orientation)
  if (available &&
      gtk_label_get_ellipsize(
        GTK_LABEL(r_src->label_widget)
      ) != PANGO_ELLIPSIZE_NONE)
    gtk_widget_set_tooltip_text(r_src->label_widget, name);

  g_free(name);
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
      update_routing_src_label(r_src);
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
  if (!card->window_routing || !card->routing_snks || !card->routing_srcs)
    return;

  update_hw_io_limits(card);

  // Update routing sinks
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    update_hw_output_label(r_snk);
  }

  // Update routing sources (only categories with availability logic)
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    if (r_src->port_category == PC_HW ||
        r_src->port_category == PC_MIX)
      update_routing_src_label(r_src);
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

// Callback when digital I/O mode changes
static void digital_io_mode_changed(struct alsa_elem *elem, void *data) {
  update_all_hw_io_labels(elem->card);
}

static void routing_updated(struct alsa_elem *elem, void *data) {
  struct alsa_card *card = elem->card;
  struct routing_snk *r_snk = data;

  if (r_snk)
    update_snk_effective_source(r_snk);

  update_mixer_labels(card);
  gtk_widget_queue_draw(card->routing_lines);
}

// Create sink widgets (socket and label). Grid attachment handled by arrange functions.
static void make_routing_alsa_elem(struct routing_snk *r_snk) {
  struct alsa_elem *elem = r_snk->elem;
  struct alsa_card *card = elem->card;

  // Skip fixed mixer inputs (no routing widgets)
  if (elem->port_category == PC_MIX && card->has_fixed_mixer_inputs)
    return;

  char *name = get_snk_display_name_formatted(r_snk);
  setup_snk_socket(r_snk);
  setup_snk_label(r_snk, name);
  g_free(name);

  // Initialise Main/Alt and availability label state
  update_hw_output_label(r_snk);

  alsa_elem_add_callback(elem, routing_updated, r_snk, NULL);
}

static void add_routing_widgets(
  struct alsa_card *card,
  GtkWidget        *routing_overlay
) {
  GArray *r_snks = card->routing_snks;

  // Clear widget pointers (handles window reopen after close)
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    r_src->widget = NULL;
    r_src->widget2 = NULL;
    r_src->label_widget = NULL;
    r_src->talkback_widget = NULL;
  }
  for (int i = 0; i < r_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(r_snks, struct routing_snk, i);
    r_snk->socket_widget = NULL;
    r_snk->label_widget = NULL;
  }

  // Create sink widgets (grid attachment handled by arrange functions)
  for (int i = 0; i < r_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(r_snks, struct routing_snk, i);
    make_routing_alsa_elem(r_snk);
  }

  if (!card->routing_out_count[PC_MIX])
    return;

  // Create source widgets (grid attachment handled by arrange functions)
  // Start at 1 to skip the "Off" input
  for (int i = 1; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    char *name = get_src_display_name_formatted(r_src);
    setup_src_socket(r_src);
    setup_src_label(r_src, name);
    g_free(name);

    // Create talkback widget for mixer outputs if device has talkback
    if (r_src->port_category == PC_MIX && card->has_talkback) {
      GtkWidget *w = make_talkback_mix_widget(card, r_src);
      if (w)
        r_src->talkback_widget = g_object_ref(w);
    }
  }

  // Arrange all grids (handles visibility and removes empty rows/columns)
  arrange_src_grid(card, PC_DSP);
  arrange_src_grid(card, PC_MIX);
  arrange_src_grid(card, PC_HW);
  arrange_src_grid(card, PC_PCM);
  arrange_snk_grid(card, PC_DSP);
  arrange_snk_grid(card, PC_MIX);
  arrange_snk_grid(card, PC_HW);
  arrange_snk_grid(card, PC_PCM);

  // Add talkback label after mixer outputs heading
  if (card->has_talkback) {
    GtkWidget *l_talkback = gtk_label_new("Talkback");
    gtk_widget_set_tooltip_text(
      l_talkback,
      "Mixer Outputs with Talkback enabled will have the level of "
      "Mixer Input 25 internally raised and lowered when the "
      "Talkback control is turned On and Off."
    );
    GtkWidget *container = gtk_widget_get_parent(card->routing_mixer_out_heading);
    gtk_box_append(GTK_BOX(container), l_talkback);
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
  if (!card->sample_capture_elem)
    return NULL;

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

  // Trigger link state callbacks for linked pairs to update socket icons
  // and labels (must be after all other label updates)
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    if (!is_src_left_channel(src))
      continue;
    struct alsa_elem *link_elem = get_src_link_elem(src);
    if (link_elem && alsa_get_elem_value(link_elem))
      alsa_elem_change(link_elem);
  }
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    if (!is_snk_left_channel(snk))
      continue;
    struct alsa_elem *link_elem = get_snk_link_elem(snk);
    if (link_elem && alsa_get_elem_value(link_elem))
      alsa_elem_change(link_elem);
  }

  // register callback for digital I/O mode changes
  if (card->digital_io_mode_elem)
    alsa_elem_add_callback(
      card->digital_io_mode_elem, digital_io_mode_changed, NULL, NULL
    );

  add_drop_controller_motion(card, routing_overlay);

  // Add hover detection via motion controller on overlay
  GtkEventController *motion = gtk_event_controller_motion_new();
  g_signal_connect(motion, "motion", G_CALLBACK(routing_overlay_motion), card);
  g_signal_connect(motion, "leave", G_CALLBACK(routing_overlay_leave), card);
  gtk_widget_add_controller(routing_overlay, motion);

  // Add unified click handler (hit-tests against combined socket+label bounds)
  GtkGesture *click = gtk_gesture_click_new();
  g_signal_connect(click, "released", G_CALLBACK(routing_overlay_click), card);
  gtk_widget_add_controller(routing_overlay, GTK_EVENT_CONTROLLER(click));

  // Add unified drag source (hit-tests on prepare)
  GtkDragSource *drag_source = gtk_drag_source_new();
  gtk_drag_source_set_actions(drag_source, GDK_ACTION_COPY);
  g_signal_connect(drag_source, "prepare", G_CALLBACK(routing_drag_prepare), card);
  g_signal_connect(drag_source, "drag-end", G_CALLBACK(routing_drag_end), card);
  GdkPaintable *paintable = gdk_paintable_new_empty(1, 1);
  gtk_drag_source_set_icon(drag_source, paintable, 0, 0);
  g_object_unref(paintable);
  gtk_widget_add_controller(routing_overlay, GTK_EVENT_CONTROLLER(drag_source));

  // Add unified drop target (hit-tests on motion/drop)
  GtkDropTarget *drop_target = gtk_drop_target_new(G_TYPE_INT, GDK_ACTION_COPY);
  g_signal_connect(drop_target, "motion", G_CALLBACK(routing_drop_motion), card);
  g_signal_connect(drop_target, "leave", G_CALLBACK(routing_drop_leave), card);
  g_signal_connect(drop_target, "drop", G_CALLBACK(routing_drop), card);
  gtk_widget_add_controller(routing_overlay, GTK_EVENT_CONTROLLER(drop_target));

  return top;
}
