// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "alsa.h"
#include "custom-names.h"
#include "glow.h"
#include "gtkhelper.h"
#include "port-enable.h"
#include "stringhelper.h"
#include "widget-gain.h"
#include "window-mixer.h"

// draw a horizontal glow bar behind a label
static void draw_label_glow(
  cairo_t   *cr,
  GtkWidget *label,
  GtkWidget *parent,
  double     level_db
) {
  if (!label || !gtk_widget_get_visible(label))
    return;

  double intensity = get_glow_intensity(level_db);
  if (intensity <= 0)
    return;

  double r, g, b;
  level_to_colour(level_db, &r, &g, &b);

  // get label position and size
  double lw = gtk_widget_get_allocated_width(label);
  double lh = gtk_widget_get_allocated_height(label);
  double x, y;
  gtk_widget_translate_coordinates(label, parent, lw / 2.0, lh / 2.0, &x, &y);

  // scale glow length and height based on intensity
  double max_half_width = 25.0;
  double half_width = max_half_width * intensity;

  // minimum visible size
  if (half_width < 5.0)
    half_width = 5.0;

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  for (int layer = GLOW_LAYERS - 1; layer >= 0; layer--) {
    double width, alpha;
    get_glow_layer_params(layer, intensity, &width, &alpha);

    // scale the height too
    width *= (0.5 + 0.75 * intensity);

    cairo_set_source_rgba(cr, r, g, b, alpha);
    cairo_set_line_width(cr, width);
    cairo_move_to(cr, x - half_width, y);
    cairo_line_to(cr, x + half_width, y);
    cairo_stroke(cr);
  }
}

// draw function for mixer label glow overlay
static void draw_mixer_glow(
  GtkDrawingArea *drawing_area,
  cairo_t        *cr,
  int             width,
  int             height,
  void           *user_data
) {
  struct alsa_card *card = user_data;
  GtkWidget *parent = card->mixer_glow;

  if (!card->routing_srcs)
    return;

  // draw glow behind mixer output labels (Mix A, Mix B, etc.)
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    if (r_src->port_category != PC_MIX)
      continue;

    // skip disabled ports
    if (!is_routing_src_enabled(r_src))
      continue;

    double level_db = get_routing_src_level_db(card, r_src);

    draw_label_glow(cr, r_src->mixer_label_left, parent, level_db);
    draw_label_glow(cr, r_src->mixer_label_right, parent, level_db);
  }

  // draw glow behind mixer input labels (top/bottom)
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (!r_snk->elem || r_snk->elem->port_category != PC_MIX)
      continue;

    // skip disabled ports
    if (!is_routing_snk_enabled(r_snk))
      continue;

    // get the source connected to this mixer input
    int r_src_idx = alsa_get_elem_value(r_snk->elem);
    if (!r_src_idx)
      continue;

    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, r_src_idx
    );

    double level_db = get_routing_src_level_db(card, r_src);

    draw_label_glow(cr, r_snk->mixer_label_top, parent, level_db);
    draw_label_glow(cr, r_snk->mixer_label_bottom, parent, level_db);
  }
}

// Structure to store mixer gain widget and its coordinates
struct mixer_gain_widget {
  GtkWidget *widget;
  int mix_num;    // 0-based mix number (A=0, B=1, etc.)
  int input_num;  // 0-based input number
};

// mixer_gain_widgets is stored in card->mixer_gain_widgets

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

  // clear any existing mixer gain widgets from previous window
  for (GList *l = card->mixer_gain_widgets; l != NULL; l = l->next) {
    struct mixer_gain_widget *mg = l->data;
    if (mg->widget)
      g_object_unref(mg->widget);  // release the ref we added
    g_free(mg);
  }
  g_list_free(card->mixer_gain_widgets);
  card->mixer_gain_widgets = NULL;

  // create overlay to hold the grid and glow layer
  GtkWidget *mixer_overlay = gtk_overlay_new();
  gtk_widget_add_css_class(mixer_overlay, "window-content");
  gtk_widget_add_css_class(mixer_overlay, "top-level-content");
  gtk_widget_add_css_class(mixer_overlay, "window-mixer");
  gtk_frame_set_child(GTK_FRAME(top), mixer_overlay);

  // create grid as base child (determines size)
  GtkWidget *mixer_top = gtk_grid_new();
  gtk_overlay_set_child(GTK_OVERLAY(mixer_overlay), mixer_top);

  gtk_widget_set_halign(mixer_top, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(mixer_top, GTK_ALIGN_CENTER);
  gtk_grid_set_column_homogeneous(GTK_GRID(mixer_top), TRUE);

  // store the grid for later access
  card->mixer_grid = mixer_top;

  // create drawing area for glow effects as underlay
  // use measure callback to not affect sizing
  card->mixer_glow = gtk_drawing_area_new();
  gtk_widget_set_can_target(card->mixer_glow, FALSE);
  gtk_drawing_area_set_draw_func(
    GTK_DRAWING_AREA(card->mixer_glow), draw_mixer_glow, card, NULL
  );

  // insert at position 0 to be behind the grid
  // but GtkOverlay doesn't support ordering, so we need another approach
  gtk_overlay_add_overlay(GTK_OVERLAY(mixer_overlay), card->mixer_glow);

  // lower the glow below the grid by reordering
  gtk_widget_insert_before(card->mixer_glow, mixer_overlay, mixer_top);

  GArray *elems = card->elems;

  GtkWidget *mix_labels_left[MAX_MIX_OUT];
  GtkWidget *mix_labels_right[MAX_MIX_OUT];

  // create the Mix X labels on the left and right of the grid
  for (int i = 0; i < card->routing_in_count[PC_MIX]; i++) {
    // find the corresponding routing source for this mixer output
    struct routing_src *r_src = NULL;
    for (int j = 0; j < card->routing_srcs->len; j++) {
      struct routing_src *src = &g_array_index(
        card->routing_srcs, struct routing_src, j
      );
      if (src->port_category == PC_MIX && src->port_num == i) {
        r_src = src;
        break;
      }
    }

    char *name = r_src ?
      get_mixer_output_label_for_mixer_window(r_src) :
      g_strdup_printf("Mix %c", i + 'A');

    GtkWidget *l_left = mix_labels_left[i] = gtk_label_new(name);
    gtk_label_set_ellipsize(GTK_LABEL(l_left), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(l_left), 12);
    gtk_widget_set_tooltip_text(l_left, name);
    g_object_ref(l_left);  // keep alive when removed from grid
    gtk_grid_attach(
      GTK_GRID(mixer_top), l_left,
      0, i + 2, 1, 1
    );

    GtkWidget *l_right = mix_labels_right[i] = gtk_label_new(name);
    gtk_label_set_ellipsize(GTK_LABEL(l_right), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(l_right), 12);
    gtk_widget_set_tooltip_text(l_right, name);
    g_object_ref(l_right);  // keep alive when removed from grid
    gtk_grid_attach(
      GTK_GRID(mixer_top), l_right,
      card->routing_out_count[PC_MIX] + 1, i + 2, 1, 1
    );

    g_free(name);

    // store pointers to these labels in the routing source
    if (r_src) {
      r_src->mixer_label_left = l_left;
      r_src->mixer_label_right = l_right;
    }
  }

  // Create all mixer input labels upfront (top and bottom)
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (!r_snk->elem || r_snk->elem->port_category != PC_MIX)
      continue;

    // Create the labels if they don't exist
    if (!r_snk->mixer_label_top) {
      r_snk->mixer_label_top = gtk_label_new("");
      r_snk->mixer_label_bottom = gtk_label_new("");
      gtk_widget_add_css_class(r_snk->mixer_label_top, "mixer-label");
      gtk_widget_add_css_class(r_snk->mixer_label_bottom, "mixer-label");
      g_object_ref(r_snk->mixer_label_top);     // keep alive when removed from grid
      g_object_ref(r_snk->mixer_label_bottom);  // keep alive when removed from grid

      // Attach to grid initially (will be repositioned during rebuild)
      int input_num = r_snk->elem->lr_num - 1;
      gtk_grid_attach(
        GTK_GRID(mixer_top), r_snk->mixer_label_top,
        input_num, (input_num + 1) % 2, 3, 1
      );
      gtk_grid_attach(
        GTK_GRID(mixer_top), r_snk->mixer_label_bottom,
        input_num, card->routing_in_count[PC_MIX] + input_num % 2 + 2, 3, 1
      );
    }
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

    // create the gain control
    GtkWidget *w = make_gain_alsa_elem(elem, 1, WIDGET_GAIN_TAPER_LOG, 0);

    // store widget reference so it stays alive when removed from grid
    g_object_ref(w);

    // store widget in card's list with coordinates
    struct mixer_gain_widget *mg = g_malloc(sizeof(struct mixer_gain_widget));
    mg->widget = w;
    mg->mix_num = mix_num;
    mg->input_num = input_num;
    card->mixer_gain_widgets = g_list_append(card->mixer_gain_widgets, mg);

    // attach to the grid initially (will be rebuilt later)
    gtk_grid_attach(GTK_GRID(mixer_top), w, input_num + 1, mix_num + 2, 1, 1);

    // look up the r_snk entry for the mixer input number
    struct routing_snk *r_snk = get_mixer_r_snk(card, input_num + 1);
    if (!r_snk) {
      printf("missing mixer input %d\n", input_num);
      continue;
    }

    // Store label references in the gain widget for hover effect
    g_object_set_data(G_OBJECT(w), "mix_label_left", mix_labels_left[mix_num]);
    g_object_set_data(G_OBJECT(w), "mix_label_right", mix_labels_right[mix_num]);
    g_object_set_data(G_OBJECT(w), "source_label_top", r_snk->mixer_label_top);
    g_object_set_data(G_OBJECT(w), "source_label_bottom", r_snk->mixer_label_bottom);

    // add hover controller to the gain widget
    add_mixer_hover_controller(w);

  }

  update_mixer_labels(card);

  // rebuild grid layout based on port enable states
  rebuild_mixer_grid(card);

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
      const char *display_name = get_routing_src_display_name(r_src);
      gtk_label_set_text(GTK_LABEL(r_snk->mixer_label_top), display_name);
      gtk_label_set_text(GTK_LABEL(r_snk->mixer_label_bottom), display_name);
    }
  }
}

// Rebuild the mixer grid layout based on current port enable states
void rebuild_mixer_grid(struct alsa_card *card) {
  if (!card || !card->mixer_grid)
    return;

  GtkGrid *grid = GTK_GRID(card->mixer_grid);

  // Remove all widgets from the grid (but keep references)
  GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(grid));
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_grid_remove(grid, child);
    child = next;
  }

  // Build list of visible mixer outputs (sources)
  int visible_mix_count = 0;
  int mix_num_to_row[MAX_MIX_OUT];  // map mix_num to row

  // initialize all to -1 (not visible)
  for (int i = 0; i < MAX_MIX_OUT; i++)
    mix_num_to_row[i] = -1;

  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    if (src->port_category != PC_MIX)
      continue;

    // bounds check
    if (src->port_num < 0 || src->port_num >= MAX_MIX_OUT)
      continue;

    if (is_routing_src_enabled(src)) {
      mix_num_to_row[src->port_num] = visible_mix_count;
      visible_mix_count++;
    } else {
      mix_num_to_row[src->port_num] = -1;  // not visible
    }
  }

  // Build list of visible mixer inputs (sinks)
  int visible_input_count = 0;
  int max_mixer_inputs = card->routing_out_count[PC_MIX];  // actual number of mixer inputs
  int input_num_to_col[max_mixer_inputs];  // map input_num to column

  // initialize all to -1 (not visible)
  for (int i = 0; i < max_mixer_inputs; i++)
    input_num_to_col[i] = -1;

  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (!snk->elem || snk->elem->port_category != PC_MIX)
      continue;

    int input_num = snk->elem->lr_num - 1;  // convert to 0-based

    // bounds check
    if (input_num < 0 || input_num >= max_mixer_inputs)
      continue;

    // Check the mixer input enable state
    int visible = is_routing_snk_enabled(snk);

    if (visible) {
      input_num_to_col[input_num] = visible_input_count;
      visible_input_count++;
    } else {
      input_num_to_col[input_num] = -1;  // not visible
    }
  }

  // Re-attach mixer output labels (left and right)
  int row_offset = 2;  // rows 0-1 are for input labels
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    if (src->port_category != PC_MIX)
      continue;

    // bounds check
    if (src->port_num < 0 || src->port_num >= MAX_MIX_OUT)
      continue;

    int row = mix_num_to_row[src->port_num];
    if (row < 0)  // not visible
      continue;

    if (src->mixer_label_left) {
      gtk_grid_attach(
        grid, src->mixer_label_left,
        0, row + row_offset, 1, 1
      );
    }

    if (src->mixer_label_right) {
      gtk_grid_attach(
        grid, src->mixer_label_right,
        visible_input_count + 1, row + row_offset, 1, 1
      );
    }
  }

  // Re-attach mixer input labels (top and bottom)
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (!snk->elem || snk->elem->port_category != PC_MIX)
      continue;

    int input_num = snk->elem->lr_num - 1;

    // bounds check
    if (input_num < 0 || input_num >= max_mixer_inputs)
      continue;

    int col = input_num_to_col[input_num];
    if (col < 0)  // not visible
      continue;

    if (snk->mixer_label_top) {
      gtk_grid_attach(
        grid, snk->mixer_label_top,
        col, (col + 1) % 2, 3, 1
      );
    }

    if (snk->mixer_label_bottom) {
      gtk_grid_attach(
        grid, snk->mixer_label_bottom,
        col, visible_mix_count + (col % 2) + row_offset, 3, 1
      );
    }
  }

  // Re-attach gain widgets
  for (GList *l = card->mixer_gain_widgets; l != NULL; l = l->next) {
    struct mixer_gain_widget *mg = l->data;

    // bounds check before accessing arrays
    if (mg->mix_num < 0 || mg->mix_num >= MAX_MIX_OUT ||
        mg->input_num < 0 || mg->input_num >= max_mixer_inputs)
      continue;

    int row = mix_num_to_row[mg->mix_num];
    int col = input_num_to_col[mg->input_num];

    // Only attach if both mixer output and input are visible
    if (row >= 0 && col >= 0) {
      gtk_grid_attach(
        grid, mg->widget,
        col + 1, row + row_offset, 1, 1
      );
    }
  }
}
