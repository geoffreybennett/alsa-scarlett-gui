// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "optional-state.h"
#include "window-levels.h"
#include "window-mixer.h"
#include "window-preferences.h"

// levels update rate options (Hz → ms)
static const struct {
  const char *label;
  int         hz;
  int         ms;
} levels_rates[] = {
  {  "5 Hz",  5, 200 },
  { "10 Hz", 10, 100 },
  { "20 Hz", 20,  50 },
};

#define LEVELS_RATE_COUNT \
  ((int)(sizeof(levels_rates) / sizeof(levels_rates[0])))

int levels_hz_to_ms(int hz) {
  for (int i = 0; i < LEVELS_RATE_COUNT; i++)
    if (levels_rates[i].hz == hz)
      return levels_rates[i].ms;
  return DEFAULT_LEVELS_INTERVAL_MS;
}

static int ms_to_rate_index(int ms) {
  for (int i = 0; i < LEVELS_RATE_COUNT; i++)
    if (levels_rates[i].ms == ms)
      return i;
  for (int i = 0; i < LEVELS_RATE_COUNT; i++)
    if (levels_rates[i].ms == DEFAULT_LEVELS_INTERVAL_MS)
      return i;
  return 0;
}

static int parse_bool_pref(
  GHashTable *state,
  const char *key,
  int         fallback
) {
  if (!state)
    return fallback;
  const char *val = g_hash_table_lookup(state, key);
  if (!val)
    return fallback;
  if (strcmp(val, "true") == 0)
    return 1;
  if (strcmp(val, "false") == 0)
    return 0;
  return fallback;
}

void load_preferences(struct alsa_card *card) {
  GHashTable *state = optional_state_load(
    card, CONFIG_SECTION_UI
  );

  card->pref_show_bottom_right_labels = parse_bool_pref(
    state, "mixer-show-bottom-right-labels", 0
  );

  const char *val = state
    ? g_hash_table_lookup(state, "levels-update-rate")
    : NULL;
  card->pref_levels_interval_ms =
    levels_hz_to_ms(val ? atoi(val) : 0);

  if (state)
    g_hash_table_destroy(state);
}

struct rate_buttons {
  struct alsa_card *card;
  GtkWidget        *buttons[LEVELS_RATE_COUNT];
};

static void on_bottom_right_labels_changed(
  GObject    *sw,
  GParamSpec *pspec,
  gpointer    data
) {
  struct alsa_card *card = data;

  card->pref_show_bottom_right_labels =
    gtk_switch_get_active(GTK_SWITCH(sw));
  optional_state_save(
    card, CONFIG_SECTION_UI,
    "mixer-show-bottom-right-labels",
    card->pref_show_bottom_right_labels ? "true" : "false"
  );
  rebuild_mixer_grid(card);
}

static void on_rate_button_toggled(
  GtkToggleButton *button,
  gpointer         data
) {
  struct rate_buttons *rb = data;

  if (!gtk_toggle_button_get_active(button))
    return;

  int idx = GPOINTER_TO_INT(
    g_object_get_data(G_OBJECT(button), "rate-index")
  );

  rb->card->pref_levels_interval_ms = levels_rates[idx].ms;

  char buf[8];
  snprintf(buf, sizeof(buf), "%d", levels_rates[idx].hz);
  optional_state_save(
    rb->card, CONFIG_SECTION_UI, "levels-update-rate", buf
  );

  restart_levels_timer(rb->card);
}

static GtkWidget *make_pref_row(
  const char *label_text,
  GtkWidget  *control
) {
  GtkWidget *row = gtk_box_new(
    GTK_ORIENTATION_HORIZONTAL, 10
  );
  gtk_widget_set_hexpand(row, TRUE);

  GtkWidget *label = gtk_label_new(label_text);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(row), label);

  gtk_widget_set_halign(control, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(row), control);

  return row;
}

static GtkWidget *make_section_label(const char *text) {
  GtkWidget *label = gtk_label_new(NULL);
  char *markup = g_strdup_printf("<b>%s</b>", text);
  gtk_label_set_markup(GTK_LABEL(label), markup);
  g_free(markup);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  return label;
}

GtkWidget *create_preferences_controls(
  struct alsa_card *card
) {
  GtkWidget *top = gtk_frame_new(NULL);
  gtk_widget_add_css_class(top, "window-frame");

  GtkWidget *content = gtk_box_new(
    GTK_ORIENTATION_VERTICAL, 10
  );
  gtk_widget_add_css_class(content, "window-content");
  gtk_widget_add_css_class(content, "top-level-content");
  gtk_frame_set_child(GTK_FRAME(top), content);

  // mixer section
  gtk_box_append(
    GTK_BOX(content), make_section_label("Mixer")
  );

  GtkWidget *sw = gtk_switch_new();
  gtk_switch_set_active(
    GTK_SWITCH(sw), card->pref_show_bottom_right_labels
  );
  g_signal_connect(
    sw, "notify::active",
    G_CALLBACK(on_bottom_right_labels_changed), card
  );
  gtk_box_append(
    GTK_BOX(content),
    make_pref_row("Show Bottom/Right Labels", sw)
  );

  // levels section (only if card has levels)
  if (card->has_levels) {
    gtk_box_append(
      GTK_BOX(content), make_section_label("Levels")
    );

    struct rate_buttons *rb = g_malloc0(
      sizeof(struct rate_buttons)
    );
    rb->card = card;

    int active = ms_to_rate_index(
      card->pref_levels_interval_ms
    );

    GtkWidget *rate_box = gtk_box_new(
      GTK_ORIENTATION_HORIZONTAL, 5
    );
    gtk_widget_set_halign(rate_box, GTK_ALIGN_END);

    for (int i = 0; i < LEVELS_RATE_COUNT; i++) {
      GtkWidget *btn = gtk_toggle_button_new_with_label(
        levels_rates[i].label
      );
      gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(btn), i == active
      );
      if (i > 0)
        gtk_toggle_button_set_group(
          GTK_TOGGLE_BUTTON(btn),
          GTK_TOGGLE_BUTTON(rb->buttons[0])
        );

      g_object_set_data(
        G_OBJECT(btn), "rate-index",
        GINT_TO_POINTER(i)
      );
      g_signal_connect(
        btn, "toggled",
        G_CALLBACK(on_rate_button_toggled), rb
      );

      rb->buttons[i] = btn;
      gtk_box_append(GTK_BOX(rate_box), btn);
    }

    gtk_box_append(
      GTK_BOX(content),
      make_pref_row("Update Rate", rate_box)
    );

    g_object_weak_ref(
      G_OBJECT(top), (GWeakNotify)g_free, rb
    );
  }

  return top;
}
