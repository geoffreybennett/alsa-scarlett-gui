// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>
#include <math.h>

#include "gtkhelper.h"
#include "db.h"
#include "stringhelper.h"
#include "widget-gain.h"
#include "window-mixer.h"

#define STACK_PAGE_SPLIT    "split"
#define STACK_PAGE_COMBINED "combined"
#define MIXER_PAN_RANGE     100.0

struct mixer_state;
struct mixer_pair;

struct mixer_combined_cell {
  struct mixer_pair *pair;
  struct alsa_elem  *left_elem;
  struct alsa_elem  *right_elem;
  GtkStack          *left_stack;
  GtkStack          *right_stack;
  GtkWidget         *left_widget;
  GtkWidget         *right_widget;
  GtkWidget         *volume_box;
  GtkWidget         *volume_dial;
  GtkWidget         *volume_label;
  GtkWidget         *pan_box;
  GtkWidget         *pan_dial;
  GtkWidget         *pan_label;
  int                min_val;
  int                max_val;
  int                min_cdB;
  int                max_cdB;
  int                min_db;
  int                max_db;
  int                zero_is_off;
  double             scale;
  double             volume_norm;
  double             pan_norm;
  gboolean           has_combined;
  gboolean           updating;
  gboolean           is_linear;
};

struct mixer_pair {
  struct mixer_state *state;
  int                 pair_index;
  int                 left_mix;
  int                 right_mix;
  GtkWidget          *toggle_box;
  GtkSwitch          *toggle_switch;
  GtkWidget          *toggle_label;
  gboolean            combined;
  struct mixer_combined_cell cells[MAX_MIX_OUT];
};

struct mixer_state {
  struct alsa_card *card;
  GtkWidget        *top;
  GtkWidget        *mixer_top;
  int               mix_count;
  int               input_count;
  int               pair_count;
  GtkWidget        *mix_labels_left[MAX_MIX_OUT];
  GtkWidget        *mix_labels_right[MAX_MIX_OUT];
  GtkWidget        *stacks[MAX_MIX_OUT][MAX_MIX_OUT];
  GtkWidget        *gain_widgets[MAX_MIX_OUT][MAX_MIX_OUT];
  struct alsa_elem *elems[MAX_MIX_OUT][MAX_MIX_OUT];
  struct mixer_pair pairs[MAX_MIX_OUT / 2];
};

static void mixer_pair_set_combined(struct mixer_pair *pair, gboolean combined);
static void mixer_combined_sync(struct mixer_combined_cell *cell);

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

static double mixer_value_to_norm(const struct mixer_combined_cell *cell, int value) {
  if (cell->max_val == cell->min_val)
    return 0.0;

  double norm =
    (double)(value - cell->min_val) /
    (double)(cell->max_val - cell->min_val);

  if (norm < 0.0)
    norm = 0.0;
  else if (norm > 1.0)
    norm = 1.0;

  return norm;
}

static int mixer_norm_to_value(const struct mixer_combined_cell *cell, double norm) {
  if (norm < 0.0)
    norm = 0.0;
  else if (norm > 1.0)
    norm = 1.0;

  double value =
    norm * (double)(cell->max_val - cell->min_val) + cell->min_val;

  int result = (int)round(value);

  if (result < cell->min_val)
    result = cell->min_val;
  else if (result > cell->max_val)
    result = cell->max_val;

  return result;
}

static void mixer_combined_update_volume_label(
  struct mixer_combined_cell *cell,
  int                         alsa_value
) {
  if (!cell->volume_label)
    return;

  char s[20];
  char *p = s;
  double value;

  if (cell->is_linear) {
    value = linear_value_to_db(
      alsa_value,
      cell->min_val,
      cell->max_val,
      cell->min_db,
      cell->max_db
    );
  } else {
    value =
      ((double)(alsa_value - cell->min_val)) * cell->scale +
      ((double)cell->min_cdB / 100.0);

    if (value > cell->max_db)
      value = cell->max_db;
    else if (value < cell->min_db)
      value = cell->min_db;
  }

  if (cell->zero_is_off && fabs(value - cell->min_db) < 0.0001) {
    p += sprintf(p, "−∞");
  } else {
    if (cell->scale <= 0.5)
      value = round(value * 10) / 10;

    if (value < 0)
      p += sprintf(p, "−");
    else if (value > 0)
      p += sprintf(p, "+");

    if (cell->scale <= 0.5)
      p += snprintf(p, sizeof(s) - (p - s), "%.1f", fabs(value));
    else
      p += snprintf(p, sizeof(s) - (p - s), "%.0f", fabs(value));

    if (cell->scale > 0.5)
      p += sprintf(p, "dB");
  }

  gtk_label_set_text(GTK_LABEL(cell->volume_label), s);
}

static void mixer_combined_update_pan_label(struct mixer_combined_cell *cell) {
  if (!cell->pan_label)
    return;

  char s[20];
  double pan = cell->pan_norm;

  if (fabs(pan) < 0.01) {
    strcpy(s, "C");
  } else if (pan < 0) {
    int percent = (int)round(fabs(pan) * 100.0);
    snprintf(s, sizeof(s), "L%d", percent);
  } else {
    int percent = (int)round(pan * 100.0);
    snprintf(s, sizeof(s), "R%d", percent);
  }

  gtk_label_set_text(GTK_LABEL(cell->pan_label), s);
}

static void mixer_combined_sync(struct mixer_combined_cell *cell) {
  if (!cell->left_elem || !cell->right_elem)
    return;

  int left_value = alsa_get_elem_value(cell->left_elem);
  int right_value = alsa_get_elem_value(cell->right_elem);

  double left_norm = mixer_value_to_norm(cell, left_value);
  double right_norm = mixer_value_to_norm(cell, right_value);
  double volume_norm = MAX(left_norm, right_norm);

  if (volume_norm < 0.0001) {
    volume_norm = 0.0;
    cell->pan_norm = 0.0;
  } else {
    double pan = (right_norm - left_norm) / volume_norm;
    if (pan < -1.0)
      pan = -1.0;
    else if (pan > 1.0)
      pan = 1.0;
    cell->pan_norm = pan;
  }

  cell->volume_norm = volume_norm;

  if (cell->volume_dial) {
    cell->updating = TRUE;
    double volume_value =
      cell->min_val + volume_norm * (cell->max_val - cell->min_val);
    gtk_dial_set_value(GTK_DIAL(cell->volume_dial), volume_value);
    mixer_combined_update_volume_label(cell, (int)round(volume_value));
    cell->updating = FALSE;
  }

  if (cell->pan_dial) {
    cell->updating = TRUE;
    gtk_dial_set_value(
      GTK_DIAL(cell->pan_dial),
      cell->pan_norm * MIXER_PAN_RANGE
    );
    mixer_combined_update_pan_label(cell);
    cell->updating = FALSE;
  }
}

static void mixer_combined_elem_updated(
  struct alsa_elem        *elem,
  void                    *user_data
) {
  struct mixer_combined_cell *cell = user_data;

  if (!cell->has_combined || cell->updating)
    return;

  mixer_combined_sync(cell);
}

static void mixer_combined_volume_changed(
  GtkWidget *widget,
  gpointer   user_data
) {
  struct mixer_combined_cell *cell = user_data;

  if (cell->updating)
    return;

  double dial_value = gtk_dial_get_value(GTK_DIAL(cell->volume_dial));
  int alsa_value = (int)round(dial_value);
  double volume_norm = mixer_value_to_norm(cell, alsa_value);

  cell->volume_norm = volume_norm;

  double pan = cell->pan_norm;
  double left_norm = volume_norm;
  double right_norm = volume_norm;

  if (pan > 0)
    left_norm *= 1.0 - pan;
  else if (pan < 0)
    right_norm *= 1.0 + pan;

  int left_value = mixer_norm_to_value(cell, left_norm);
  int right_value = mixer_norm_to_value(cell, right_norm);

  cell->updating = TRUE;
  alsa_set_elem_value(cell->left_elem, left_value);
  alsa_set_elem_value(cell->right_elem, right_value);
  mixer_combined_update_volume_label(cell, alsa_value);
  cell->updating = FALSE;
}

static void mixer_combined_pan_changed(
  GtkWidget *widget,
  gpointer   user_data
) {
  struct mixer_combined_cell *cell = user_data;

  if (cell->updating)
    return;

  double dial_value = gtk_dial_get_value(GTK_DIAL(cell->pan_dial));
  double pan_norm = dial_value / MIXER_PAN_RANGE;

  if (pan_norm < -1.0)
    pan_norm = -1.0;
  else if (pan_norm > 1.0)
    pan_norm = 1.0;

  cell->pan_norm = pan_norm;

  double left_norm = cell->volume_norm;
  double right_norm = cell->volume_norm;

  if (pan_norm > 0)
    left_norm *= 1.0 - pan_norm;
  else if (pan_norm < 0)
    right_norm *= 1.0 + pan_norm;

  int left_value = mixer_norm_to_value(cell, left_norm);
  int right_value = mixer_norm_to_value(cell, right_norm);

  cell->updating = TRUE;
  alsa_set_elem_value(cell->left_elem, left_value);
  alsa_set_elem_value(cell->right_elem, right_value);
  mixer_combined_update_pan_label(cell);
  mixer_combined_update_volume_label(
    cell,
    (int)round(gtk_dial_get_value(GTK_DIAL(cell->volume_dial)))
  );
  cell->updating = FALSE;
}

static void mixer_combined_create_widgets(struct mixer_combined_cell *cell) {
  if (!cell->left_stack || !cell->right_stack)
    return;

  cell->min_val = cell->left_elem->min_val;
  cell->max_val = cell->left_elem->max_val;
  cell->min_cdB = cell->left_elem->min_cdB;
  cell->max_cdB = cell->left_elem->max_cdB;
  cell->min_db = round(cell->min_cdB / 100.0);
  cell->max_db = round(cell->max_cdB / 100.0);
  cell->zero_is_off = 1;
  cell->is_linear =
    cell->left_elem->dB_type == SND_CTL_TLVT_DB_LINEAR;

  double step;

  if (cell->is_linear) {
    cell->scale = 0.5;
    step = 0.5;
  } else {
    cell->scale =
      (double)(cell->max_cdB - cell->min_cdB) / 100.0 /
      (cell->max_val - cell->min_val);
    step = 1.0;
  }

  cell->volume_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(cell->volume_box, TRUE);
  gtk_widget_set_vexpand(cell->volume_box, TRUE);

  cell->volume_dial = gtk_dial_new_with_range(
    cell->min_val,
    cell->max_val,
    step,
    3 / cell->scale
  );
  gtk_widget_set_vexpand(cell->volume_dial, TRUE);
  gtk_dial_set_is_linear(GTK_DIAL(cell->volume_dial), cell->is_linear);
  gtk_dial_set_taper(GTK_DIAL(cell->volume_dial), GTK_DIAL_TAPER_LOG);

  int zero_db_value;

  if (cell->is_linear) {
    zero_db_value = cdb_to_linear_value(
      0,
      cell->min_val,
      cell->max_val,
      cell->min_cdB,
      cell->max_cdB
    );
  } else {
    zero_db_value =
      (int)((0 - cell->min_cdB) / 100.0 / cell->scale + cell->min_val);
  }

  gtk_dial_set_zero_db(GTK_DIAL(cell->volume_dial), zero_db_value);
  gtk_dial_set_can_control(GTK_DIAL(cell->volume_dial), TRUE);

  cell->volume_label = gtk_label_new(NULL);
  gtk_widget_add_css_class(cell->volume_label, "gain");

  g_signal_connect(
    cell->volume_dial,
    "value-changed",
    G_CALLBACK(mixer_combined_volume_changed),
    cell
  );

  gtk_box_append(GTK_BOX(cell->volume_box), cell->volume_dial);
  gtk_box_append(GTK_BOX(cell->volume_box), cell->volume_label);

  cell->pan_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(cell->pan_box, TRUE);
  gtk_widget_set_vexpand(cell->pan_box, TRUE);

  cell->pan_dial = gtk_dial_new_with_range(
    -MIXER_PAN_RANGE,
    MIXER_PAN_RANGE,
    1.0,
    MIXER_PAN_RANGE / 5.0
  );
  gtk_widget_set_vexpand(cell->pan_dial, TRUE);
  gtk_dial_set_has_origin(GTK_DIAL(cell->pan_dial), TRUE);
  gtk_dial_set_can_control(GTK_DIAL(cell->pan_dial), TRUE);
  gtk_dial_set_round_digits(GTK_DIAL(cell->pan_dial), 0);

  cell->pan_label = gtk_label_new(NULL);
  gtk_widget_add_css_class(cell->pan_label, "gain");

  g_signal_connect(
    cell->pan_dial,
    "value-changed",
    G_CALLBACK(mixer_combined_pan_changed),
    cell
  );

  gtk_box_append(GTK_BOX(cell->pan_box), cell->pan_dial);
  gtk_box_append(GTK_BOX(cell->pan_box), cell->pan_label);

  gtk_stack_add_named(cell->left_stack, cell->volume_box, STACK_PAGE_COMBINED);
  gtk_stack_add_named(cell->right_stack, cell->pan_box, STACK_PAGE_COMBINED);

  GtkWidget *mix_label_left = g_object_get_data(
    G_OBJECT(cell->left_widget),
    "mix_label_left"
  );
  GtkWidget *mix_label_right = g_object_get_data(
    G_OBJECT(cell->left_widget),
    "mix_label_right"
  );
  GtkWidget *source_label_top = g_object_get_data(
    G_OBJECT(cell->left_widget),
    "source_label_top"
  );
  GtkWidget *source_label_bottom = g_object_get_data(
    G_OBJECT(cell->left_widget),
    "source_label_bottom"
  );

  if (cell->volume_box) {
    g_object_set_data(
      G_OBJECT(cell->volume_box),
      "mix_label_left",
      mix_label_left
    );
    g_object_set_data(
      G_OBJECT(cell->volume_box),
      "mix_label_right",
      mix_label_right
    );
    g_object_set_data(
      G_OBJECT(cell->volume_box),
      "source_label_top",
      source_label_top
    );
    g_object_set_data(
      G_OBJECT(cell->volume_box),
      "source_label_bottom",
      source_label_bottom
    );
    add_mixer_hover_controller(cell->volume_box);
  }

  if (cell->pan_box) {
    g_object_set_data(
      G_OBJECT(cell->pan_box),
      "mix_label_left",
      mix_label_left
    );
    g_object_set_data(
      G_OBJECT(cell->pan_box),
      "mix_label_right",
      mix_label_right
    );
    g_object_set_data(
      G_OBJECT(cell->pan_box),
      "source_label_top",
      source_label_top
    );
    g_object_set_data(
      G_OBJECT(cell->pan_box),
      "source_label_bottom",
      source_label_bottom
    );
    add_mixer_hover_controller(cell->pan_box);
  }

  cell->has_combined = TRUE;
}

static void mixer_state_init_combined_cells(struct mixer_state *state) {
  for (int i = 0; i < state->pair_count; i++) {
    struct mixer_pair *pair = &state->pairs[i];

    pair->state = state;
    pair->pair_index = i;
    pair->left_mix = i * 2;
    pair->right_mix = pair->left_mix + 1;

    for (int j = 0; j < state->input_count; j++) {
      struct mixer_combined_cell *cell = &pair->cells[j];

      cell->pair = pair;
      cell->left_elem = state->elems[pair->left_mix][j];
      cell->right_elem = state->elems[pair->right_mix][j];
      cell->left_stack =
        GTK_STACK(state->stacks[pair->left_mix][j]);
      cell->right_stack =
        GTK_STACK(state->stacks[pair->right_mix][j]);
      cell->left_widget = state->gain_widgets[pair->left_mix][j];
      cell->right_widget = state->gain_widgets[pair->right_mix][j];

      if (!cell->left_elem || !cell->right_elem ||
          !cell->left_stack || !cell->right_stack)
        continue;

      mixer_combined_create_widgets(cell);

      alsa_elem_add_callback(
        cell->left_elem,
        mixer_combined_elem_updated,
        cell
      );
      alsa_elem_add_callback(
        cell->right_elem,
        mixer_combined_elem_updated,
        cell
      );

      mixer_combined_sync(cell);
    }
  }
}

static void mixer_pair_update_toggle_label(struct mixer_pair *pair) {
  if (!pair->toggle_label)
    return;

  gtk_label_set_text(
    GTK_LABEL(pair->toggle_label),
    pair->combined ? "Stereo" : "Dual"
  );
}

static void mixer_pair_set_combined(struct mixer_pair *pair, gboolean combined) {
  struct mixer_state *state = pair->state;

  if (!state)
    return;

  if (pair->combined == combined)
    return;

  pair->combined = combined;
  mixer_pair_update_toggle_label(pair);

  char label[16];

  if (combined) {
    snprintf(
      label,
      sizeof(label),
      "Mix %c/%c",
      'A' + pair->left_mix,
      'A' + pair->right_mix
    );
    gtk_label_set_text(
      GTK_LABEL(state->mix_labels_left[pair->left_mix]),
      label
    );
    gtk_label_set_text(
      GTK_LABEL(state->mix_labels_right[pair->left_mix]),
      label
    );
    gtk_label_set_text(
      GTK_LABEL(state->mix_labels_left[pair->right_mix]),
      "Pan"
    );
    gtk_label_set_text(
      GTK_LABEL(state->mix_labels_right[pair->right_mix]),
      "Pan"
    );
  } else {
    snprintf(
      label,
      sizeof(label),
      "Mix %c",
      'A' + pair->left_mix
    );
    gtk_label_set_text(
      GTK_LABEL(state->mix_labels_left[pair->left_mix]),
      label
    );
    gtk_label_set_text(
      GTK_LABEL(state->mix_labels_right[pair->left_mix]),
      label
    );
    snprintf(
      label,
      sizeof(label),
      "Mix %c",
      'A' + pair->right_mix
    );
    gtk_label_set_text(
      GTK_LABEL(state->mix_labels_left[pair->right_mix]),
      label
    );
    gtk_label_set_text(
      GTK_LABEL(state->mix_labels_right[pair->right_mix]),
      label
    );
  }

  for (int i = 0; i < state->input_count; i++) {
    struct mixer_combined_cell *cell = &pair->cells[i];

    if (!cell->has_combined)
      continue;

    const char *page = combined ? STACK_PAGE_COMBINED : STACK_PAGE_SPLIT;

    gtk_stack_set_visible_child_name(cell->left_stack, page);
    gtk_stack_set_visible_child_name(cell->right_stack, page);

    if (combined)
      mixer_combined_sync(cell);
  }
}

static void mixer_pair_toggle_notify(
  GtkSwitch *widget,
  GParamSpec *pspec,
  gpointer    user_data
) {
  struct mixer_pair *pair = user_data;

  mixer_pair_set_combined(pair, gtk_switch_get_active(widget));
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

  struct mixer_state *state = g_new0(struct mixer_state, 1);
  state->card = card;
  state->top = top;
  state->mixer_top = mixer_top;
  state->mix_count = card->routing_in_count[PC_MIX];
  state->input_count = card->routing_out_count[PC_MIX];
  state->pair_count = state->mix_count / 2;
  g_object_set_data_full(G_OBJECT(top), "mixer-state", state, g_free);

  // create the Mix X labels on the left and right of the grid
  for (int i = 0; i < card->routing_in_count[PC_MIX]; i++) {
    char name[10];
    snprintf(name, 10, "Mix %c", i + 'A');

    GtkWidget *l_left = state->mix_labels_left[i] = gtk_label_new(name);
    gtk_grid_attach(
      GTK_GRID(mixer_top), l_left,
      0, i + 2, 1, 1
    );

    GtkWidget *l_right = state->mix_labels_right[i] = gtk_label_new(name);
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
    GtkWidget *stack = gtk_stack_new();
    gtk_widget_set_hexpand(stack, TRUE);
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_stack_add_named(GTK_STACK(stack), w, STACK_PAGE_SPLIT);
    gtk_stack_set_visible_child_name(GTK_STACK(stack), STACK_PAGE_SPLIT);
    gtk_grid_attach(GTK_GRID(mixer_top), stack, input_num + 1, mix_num + 2, 1, 1);

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

    g_object_set_data(G_OBJECT(w), "mix_label_left", state->mix_labels_left[mix_num]);
    g_object_set_data(G_OBJECT(w), "mix_label_right", state->mix_labels_right[mix_num]);
    g_object_set_data(G_OBJECT(w), "source_label_top", r_snk->mixer_label_top);
    g_object_set_data(G_OBJECT(w), "source_label_bottom", r_snk->mixer_label_bottom);

    // add hover controller to the gain widget
    add_mixer_hover_controller(w);

    state->stacks[mix_num][input_num] = stack;
    state->gain_widgets[mix_num][input_num] = w;
    state->elems[mix_num][input_num] = elem;

  }

  mixer_state_init_combined_cells(state);

  for (int i = 0; i < state->pair_count; i++) {
    struct mixer_pair *pair = &state->pairs[i];

    GtkWidget *toggle_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_halign(toggle_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(toggle_box, GTK_ALIGN_CENTER);

    GtkWidget *label = gtk_label_new(NULL);
    gtk_widget_add_css_class(label, "mixer-label");
    pair->toggle_label = label;
    gtk_box_append(GTK_BOX(toggle_box), label);

    GtkWidget *toggle = gtk_switch_new();
    gtk_box_append(GTK_BOX(toggle_box), toggle);
    pair->toggle_box = toggle_box;
    pair->toggle_switch = GTK_SWITCH(toggle);

    g_signal_connect(
      toggle,
      "notify::active",
      G_CALLBACK(mixer_pair_toggle_notify),
      pair
    );

    char tooltip[64];
    snprintf(
      tooltip,
      sizeof(tooltip),
      "Combine Mix %c/%c into stereo",
      'A' + pair->left_mix,
      'A' + pair->right_mix
    );
    gtk_widget_set_tooltip_text(toggle_box, tooltip);

    gtk_grid_attach(
      GTK_GRID(mixer_top),
      toggle_box,
      state->input_count + 2,
      pair->left_mix + 2,
      1,
      2
    );

    gtk_switch_set_active(pair->toggle_switch, FALSE);
    pair->combined = TRUE;
    mixer_pair_set_combined(pair, FALSE);
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
