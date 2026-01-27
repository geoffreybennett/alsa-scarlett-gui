// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <graphene.h>
#include <gtk/gtk.h>

#include "alsa.h"
#include "custom-names.h"
#include "glow.h"
#include "gtkhelper.h"
#include "hw-io-availability.h"
#include "port-enable.h"
#include "stereo-link.h"
#include "stringhelper.h"
#include "widget-gain.h"
#include "window-mixer.h"

// draw a horizontal glow bar at a specific position
static void draw_glow_at(
  cairo_t *cr,
  double   cx,
  double   cy,
  double   max_half_width,
  double   level_db
) {
  double intensity = get_glow_intensity(level_db);
  if (intensity <= 0)
    return;

  double r, g, b;
  level_to_colour(level_db, &r, &g, &b);

  double half_width = max_half_width * intensity;
  if (half_width < 5.0)
    half_width = 5.0;

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  for (int layer = GLOW_LAYERS - 1; layer >= 0; layer--) {
    double width, alpha;
    get_glow_layer_params(layer, intensity, &width, &alpha);

    width *= (0.5 + 0.75 * intensity);

    cairo_set_source_rgba(cr, r, g, b, alpha);
    cairo_set_line_width(cr, width);
    cairo_move_to(cr, cx - half_width, cy);
    cairo_line_to(cr, cx + half_width, cy);
    cairo_stroke(cr);
  }
}

// get label centre coordinates relative to parent
static gboolean get_label_centre(
  GtkWidget *label,
  GtkWidget *parent,
  double    *cx,
  double    *cy
) {
  if (!label || !gtk_widget_get_visible(label))
    return FALSE;

  graphene_point_t src = GRAPHENE_POINT_INIT(
    gtk_widget_get_width(label) / 2.0,
    gtk_widget_get_height(label) / 2.0
  );
  graphene_point_t dest;
  if (!gtk_widget_compute_point(label, parent, &src, &dest))
    return FALSE;

  *cx = dest.x;
  *cy = dest.y;
  return TRUE;
}

// draw a single mono glow bar centred on a label
static void draw_label_glow(
  cairo_t   *cr,
  GtkWidget *label,
  GtkWidget *parent,
  double     level_db
) {
  double cx, cy;
  if (!get_label_centre(label, parent, &cx, &cy))
    return;

  draw_glow_at(cr, cx, cy, 25.0, level_db);
}

// draw one side of a stereo glow bar growing outward from centre
static void draw_glow_bar_outward(
  cairo_t *cr,
  double   inner_x,
  double   cy,
  double   max_extent,
  double   level_db,
  int      direction  // -1 = left, +1 = right
) {
  double intensity = get_glow_intensity(level_db);
  if (intensity <= 0)
    return;

  double r, g, b;
  level_to_colour(level_db, &r, &g, &b);

  double extent = max_extent * intensity;
  if (extent < 5.0)
    extent = 5.0;

  double outer_x = inner_x + direction * extent;

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  for (int layer = GLOW_LAYERS - 1; layer >= 0; layer--) {
    double width, alpha;
    get_glow_layer_params(layer, intensity, &width, &alpha);

    width *= (0.5 + 0.75 * intensity);

    cairo_set_source_rgba(cr, r, g, b, alpha);
    cairo_set_line_width(cr, width);
    cairo_move_to(cr, inner_x, cy);
    cairo_line_to(cr, outer_x, cy);
    cairo_stroke(cr);
  }
}

// draw split L/R glow bars growing outward from label centre
static void draw_stereo_label_glow(
  cairo_t   *cr,
  GtkWidget *label,
  GtkWidget *parent,
  double     level_db_l,
  double     level_db_r
) {
  double cx, cy;
  if (!get_label_centre(label, parent, &cx, &cy))
    return;

  double gap = 3.0;
  double max_extent = 25.0;
  draw_glow_bar_outward(cr, cx - gap, cy, max_extent, level_db_l, -1);
  draw_glow_bar_outward(cr, cx + gap, cy, max_extent, level_db_r, +1);
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

    if (!is_routing_src_enabled(r_src) || !should_display_src(r_src))
      continue;

    double level_l = get_routing_src_level_db(card, r_src);

    if (is_src_linked(r_src)) {
      struct routing_src *r_src_r = get_src_partner(r_src);
      double level_r = r_src_r ?
        get_routing_src_level_db(card, r_src_r) : level_l;

      draw_stereo_label_glow(
        cr, r_src->mixer_label_left, parent, level_l, level_r
      );
      draw_stereo_label_glow(
        cr, r_src->mixer_label_right, parent, level_l, level_r
      );
    } else {
      draw_label_glow(
        cr, r_src->mixer_label_left, parent, level_l
      );
      draw_label_glow(
        cr, r_src->mixer_label_right, parent, level_l
      );
    }
  }

  // draw glow behind mixer input labels (top/bottom)
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    if (!r_snk->elem || r_snk->elem->port_category != PC_MIX)
      continue;

    if (!is_routing_snk_enabled(r_snk) || !should_display_snk(r_snk))
      continue;

    // get the source connected to this mixer input
    int r_src_idx = alsa_get_elem_value(r_snk->elem);
    if (!r_src_idx)
      continue;

    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, r_src_idx
    );

    double level_l = get_routing_src_level_db(card, r_src);

    if (is_snk_linked(r_snk)) {
      struct routing_snk *r_snk_r = get_snk_partner(r_snk);
      double level_r = level_l;

      if (r_snk_r && r_snk_r->elem) {
        int r_src_idx_r = alsa_get_elem_value(r_snk_r->elem);
        if (r_src_idx_r) {
          struct routing_src *r_src_r = &g_array_index(
            card->routing_srcs, struct routing_src, r_src_idx_r
          );
          level_r = get_routing_src_level_db(card, r_src_r);
        }
      }

      draw_stereo_label_glow(
        cr, r_snk->mixer_label_top, parent, level_l, level_r
      );
      draw_stereo_label_glow(
        cr, r_snk->mixer_label_bottom, parent, level_l, level_r
      );
    } else {
      draw_label_glow(
        cr, r_snk->mixer_label_top, parent, level_l
      );
      draw_label_glow(
        cr, r_snk->mixer_label_bottom, parent, level_l
      );
    }
  }
}

// mixer_gain_widgets is stored in card->mixer_gain_widgets
// struct mixer_gain_widget is declared in window-mixer.h

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

// Get mixer routing source by mix number (0-based: A=0, B=1, etc.)
static struct routing_src *get_mixer_r_src(
  struct alsa_card *card,
  int               mix_num
) {
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    if (r_src->port_category == PC_MIX && r_src->port_num == mix_num)
      return r_src;
  }
  return NULL;
}

static void populate_mixer_gain_widgets(struct alsa_card *card) {
  int num_mixes = card->routing_in_count[PC_MIX];
  int num_inputs = card->routing_out_count[PC_MIX];

  for (int mix_num = 0; mix_num < num_mixes; mix_num++) {
    struct routing_src *mix_src = get_mixer_r_src(card, mix_num);
    if (!mix_src)
      continue;

    if (!should_display_src(mix_src))
      continue;

    int output_linked = is_src_linked(mix_src);
    struct routing_src *mix_src_r =
      output_linked ? get_src_partner(mix_src) : NULL;
    int mix_num_r = mix_src_r ? mix_src_r->port_num : -1;

    for (int input_num = 0; input_num < num_inputs; input_num++) {
      struct routing_snk *r_snk = get_mixer_r_snk(card, input_num + 1);
      if (!r_snk)
        continue;

      if (!should_display_snk(r_snk))
        continue;

      int input_linked = is_snk_linked(r_snk);
      struct routing_snk *r_snk_r =
        input_linked ? get_snk_partner(r_snk) : NULL;
      int input_num_r = r_snk_r && r_snk_r->elem ?
                        r_snk_r->elem->lr_num - 1 : -1;

      struct alsa_elem *elem_arr[4];
      int elem_count = 0;

      if (!input_linked && !output_linked) {
        struct alsa_elem *e = card->mixer_gains[mix_num][input_num];
        if (e)
          elem_arr[elem_count++] = e;
      } else if (input_linked && !output_linked) {
        struct alsa_elem *e1 = card->mixer_gains[mix_num][input_num];
        struct alsa_elem *e2 = card->mixer_gains[mix_num][input_num_r];
        if (e1) elem_arr[elem_count++] = e1;
        if (e2) elem_arr[elem_count++] = e2;
      } else if (!input_linked && output_linked) {
        struct alsa_elem *e1 = card->mixer_gains[mix_num][input_num];
        struct alsa_elem *e2 = card->mixer_gains[mix_num_r][input_num];
        if (e1) elem_arr[elem_count++] = e1;
        if (e2) elem_arr[elem_count++] = e2;
      } else {
        struct alsa_elem *e1 = card->mixer_gains[mix_num][input_num];
        struct alsa_elem *e2 = card->mixer_gains[mix_num_r][input_num_r];
        if (e1) elem_arr[elem_count++] = e1;
        if (e2) elem_arr[elem_count++] = e2;
      }

      if (elem_count == 0)
        continue;

      // Create the gain widget
      GtkWidget *w;
      if (elem_count == 1) {
        w = make_gain_alsa_elem(
          elem_arr[0], 1, WIDGET_GAIN_TAPER_LOG, 0, TRUE
        );
      } else {
        w = make_stereo_gain_alsa_elem(
          elem_arr, elem_count, 1,
          WIDGET_GAIN_TAPER_LOG, 0, TRUE
        );
      }

      if (!w)
        continue;

      // keep alive when removed from grid
      g_object_ref(w);

      struct mixer_gain_widget *mg =
        g_malloc0(sizeof(struct mixer_gain_widget));
      mg->widget = w;
      mg->mix_num = mix_num;
      mg->input_num = input_num;
      mg->r_snk = r_snk;
      mg->elem = elem_arr[0];

      mg->elem_count = elem_count;
      for (int i = 0; i < elem_count; i++)
        mg->elems[i] = elem_arr[i];

      mg->r_snks[0] = r_snk;
      mg->r_snk_count = 1;
      if (input_linked && r_snk_r) {
        mg->r_snks[1] = r_snk_r;
        mg->r_snk_count = 2;
      }

      card->mixer_gain_widgets =
        g_list_append(card->mixer_gain_widgets, mg);

      // Store label references for hover effect
      g_object_set_data(
        G_OBJECT(w), "mix_label_left",
        mix_src->mixer_label_left
      );
      g_object_set_data(
        G_OBJECT(w), "mix_label_right",
        mix_src->mixer_label_right
      );
      g_object_set_data(
        G_OBJECT(w), "source_label_top",
        r_snk->mixer_label_top
      );
      g_object_set_data(
        G_OBJECT(w), "source_label_bottom",
        r_snk->mixer_label_bottom
      );

      add_mixer_hover_controller(w);
    }
  }
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
  GtkWidget *mixer_overlay = card->mixer_overlay = gtk_overlay_new();
  gtk_widget_add_css_class(mixer_overlay, "window-content");
  gtk_widget_add_css_class(mixer_overlay, "top-level-content");
  gtk_widget_add_css_class(mixer_overlay, "window-mixer");
  gtk_frame_set_child(GTK_FRAME(top), mixer_overlay);

  // create grid as base child (determines size)
  GtkWidget *mixer_top = gtk_grid_new();
  gtk_overlay_set_child(GTK_OVERLAY(mixer_overlay), mixer_top);

  // create unavailable overlay label (hidden by default)
  GtkWidget *unavail = card->mixer_unavailable_label = gtk_label_new(
    "Mixer unavailable at\ncurrent sample rate"
  );
  gtk_widget_add_css_class(unavail, "mixer-unavailable");
  gtk_widget_set_halign(unavail, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(unavail, GTK_ALIGN_CENTER);
  gtk_widget_set_can_target(unavail, FALSE);
  gtk_widget_set_visible(unavail, FALSE);
  gtk_overlay_add_overlay(GTK_OVERLAY(mixer_overlay), unavail);

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

  // create the Mix X labels on the left and right of the grid
  for (int i = 0; i < card->routing_in_count[PC_MIX]; i++) {
    struct routing_src *r_src = get_mixer_r_src(card, i);

    char *name = r_src ?
      get_mixer_output_label_for_mixer_window(r_src) :
      g_strdup_printf("Mix %c", i + 'A');

    GtkWidget *l_left = gtk_label_new(name);
    gtk_label_set_ellipsize(GTK_LABEL(l_left), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(l_left), 12);
    gtk_widget_set_tooltip_text(l_left, name);
    g_object_ref(l_left);
    gtk_grid_attach(
      GTK_GRID(mixer_top), l_left, 0, i + 2, 1, 1
    );

    GtkWidget *l_right = gtk_label_new(name);
    gtk_label_set_ellipsize(GTK_LABEL(l_right), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(l_right), 12);
    gtk_widget_set_tooltip_text(l_right, name);
    g_object_ref(l_right);
    gtk_grid_attach(
      GTK_GRID(mixer_top), l_right,
      card->routing_out_count[PC_MIX] + 1, i + 2, 1, 1
    );

    g_free(name);

    if (r_src) {
      r_src->mixer_label_left = l_left;
      r_src->mixer_label_right = l_right;
    }
  }

  // Create corner label for mixer inputs/outputs legend
  card->mixer_corner_label = gtk_label_new(NULL);
  gtk_label_set_markup(
    GTK_LABEL(card->mixer_corner_label),
    "<span line_height=\"1.8\">Inputs →</span>\nOutputs ↓"
  );
  gtk_label_set_justify(GTK_LABEL(card->mixer_corner_label), GTK_JUSTIFY_CENTER);
  gtk_widget_add_css_class(card->mixer_corner_label, "mixer-corner-label");
  g_object_ref(card->mixer_corner_label);

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

  populate_mixer_gain_widgets(card);

  update_mixer_labels(card);

  // rebuild grid layout based on port enable states
  rebuild_mixer_grid(card);

  // set initial availability state
  int available = get_sample_rate_category(card->current_sample_rate) != SR_HIGH;
  update_mixer_availability(card, available);

  return top;
}

void update_mixer_labels(struct alsa_card *card) {
  // Update mixer output labels (Mix A, Mix B, etc.) for stereo linking
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );

    if (r_src->port_category != PC_MIX)
      continue;

    // Skip R channel of linked pair
    if (!should_display_src(r_src))
      continue;

    char *name;
    if (is_src_linked(r_src)) {
      name = get_src_pair_display_name(r_src);
    } else {
      name = get_mixer_output_label_for_mixer_window(r_src);
    }

    if (r_src->mixer_label_left) {
      gtk_label_set_text(GTK_LABEL(r_src->mixer_label_left), name);
      gtk_widget_set_tooltip_text(r_src->mixer_label_left, name);
    }
    if (r_src->mixer_label_right) {
      gtk_label_set_text(GTK_LABEL(r_src->mixer_label_right), name);
      gtk_widget_set_tooltip_text(r_src->mixer_label_right, name);
    }

    g_free(name);
  }

  // Update mixer input labels (source names)
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    struct alsa_elem *elem = r_snk->elem;

    if (elem->port_category != PC_MIX)
      continue;

    // Skip R channel of linked pair (label handled by L channel)
    if (!should_display_snk(r_snk))
      continue;

    int routing_src_idx = alsa_get_elem_value(elem);

    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, routing_src_idx
    );

    if (r_snk->mixer_label_top) {
      char *display_name;

      // If this mixer input is linked and the connected source is also linked,
      // show the stereo pair name
      if (is_snk_linked(r_snk) && is_src_linked(r_src)) {
        display_name = get_src_pair_display_name(r_src);
      } else {
        display_name = g_strdup(get_routing_src_display_name(r_src));
      }

      gtk_label_set_text(GTK_LABEL(r_snk->mixer_label_top), display_name);
      gtk_label_set_text(GTK_LABEL(r_snk->mixer_label_bottom), display_name);
      g_free(display_name);
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

    // Visible if enabled AND should be displayed (not R of linked pair)
    if (is_routing_src_enabled(src) && should_display_src(src)) {
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

    // Visible if enabled AND should be displayed (not R of linked pair)
    int visible = is_routing_snk_enabled(snk) && should_display_snk(snk);

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

  // Attach corner label spanning rows 0-1
  if (card->mixer_corner_label) {
    gtk_grid_attach(grid, card->mixer_corner_label, 0, 0, 1, 2);
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

// Update mixer window availability indication
void update_mixer_availability(struct alsa_card *card, int available) {
  if (!card->mixer_grid)
    return;

  gtk_widget_set_opacity(card->mixer_grid, available ? 1.0 : 0.3);

  if (card->mixer_unavailable_label)
    gtk_widget_set_visible(card->mixer_unavailable_label, !available);
}

// Recreate mixer widgets when stereo state changes
// This destroys existing widgets and creates new ones based on current stereo
void recreate_mixer_widgets(struct alsa_card *card) {
  if (!card || !card->mixer_grid)
    return;

  GtkGrid *grid = GTK_GRID(card->mixer_grid);

  // Clear existing mixer gain widgets
  for (GList *l = card->mixer_gain_widgets; l != NULL; l = l->next) {
    struct mixer_gain_widget *mg = l->data;
    if (mg->widget) {
      if (gtk_widget_get_parent(mg->widget) == GTK_WIDGET(grid))
        gtk_grid_remove(grid, mg->widget);
      cleanup_gain_widget(mg->widget);
      g_object_unref(mg->widget);
    }
    g_free(mg);
  }
  g_list_free(card->mixer_gain_widgets);
  card->mixer_gain_widgets = NULL;

  populate_mixer_gain_widgets(card);

  update_mixer_labels(card);
  rebuild_mixer_grid(card);
}
