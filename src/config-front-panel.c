// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "alsa.h"
#include "config-front-panel.h"
#include "config-helpers.h"
#include "widget-drop-down.h"
#include "window-configuration.h"

struct sleep_preset {
  const char *label;
  int         seconds;
};

// Preset values for the Front Panel Sleep Time control.
// 0 = never sleep; remaining values are common timeouts up to the
// 86400-second maximum (24 hours).
static const struct sleep_preset sleep_presets[] = {
  { "Off (never sleep)",   0 },
  { "30 seconds",         30 },
  { "1 minute",           60 },
  { "5 minutes",         300 },
  { "10 minutes",        600 },
  { "30 minutes",       1800 },
  { "1 hour",           3600 },
  { "2 hours",          7200 },
  { "4 hours",         14400 },
  { "8 hours",         28800 },
  { "24 hours",        86400 },
};

#define SLEEP_PRESET_COUNT G_N_ELEMENTS(sleep_presets)

static int find_preset_index(int seconds) {
  for (guint i = 0; i < SLEEP_PRESET_COUNT; i++)
    if (sleep_presets[i].seconds == seconds)
      return (int)i;
  return -1;
}

static int sleep_value_to_row(int value, void *user_data) {
  int idx = find_preset_index(value);
  if (idx >= 0)
    return idx;

  // Custom row, when present, is always the last entry.
  return SLEEP_PRESET_COUNT;
}

static int sleep_row_to_value(int row, int current_value, void *user_data) {
  if (row < (int)SLEEP_PRESET_COUNT)
    return sleep_presets[row].seconds;

  // Click on the "Custom (N s)" row: no-op.
  return current_value;
}

static char *sleep_button_label(int value, void *user_data) {
  int idx = find_preset_index(value);
  if (idx >= 0)
    return g_strdup(sleep_presets[idx].label);
  return g_strdup_printf("Custom (%d s)", value);
}

// Keep the model in sync with the live value: a synthetic
// "Custom (N s)" row is appended whenever the value isn't a preset,
// and removed once it is.
static void sleep_refresh_model(
  GtkStringList *model,
  int            value,
  void          *user_data
) {
  guint n = g_list_model_get_n_items(G_LIST_MODEL(model));
  int has_custom = (n > SLEEP_PRESET_COUNT);
  int is_preset = find_preset_index(value) >= 0;

  if (!is_preset) {
    char *s = g_strdup_printf("Custom (%d s)", value);
    const char *additions[] = { s, NULL };
    if (has_custom)
      gtk_string_list_splice(
        model, SLEEP_PRESET_COUNT, 1, additions
      );
    else
      gtk_string_list_append(model, s);
    g_free(s);
  } else if (has_custom) {
    gtk_string_list_remove(model, SLEEP_PRESET_COUNT);
  }
}

static const struct drop_down_value_ops sleep_ops = {
  .value_to_row  = sleep_value_to_row,
  .row_to_value  = sleep_row_to_value,
  .button_label  = sleep_button_label,
  .refresh_model = sleep_refresh_model,
};

static GtkWidget *make_sleep_time_dropdown(struct alsa_elem *elem) {
  GtkStringList *model = gtk_string_list_new(NULL);
  for (guint i = 0; i < SLEEP_PRESET_COUNT; i++)
    gtk_string_list_append(model, sleep_presets[i].label);

  return make_value_mapped_drop_down_alsa_elem(
    elem, model, &sleep_ops, NULL, NULL
  );
}

// Layout for one control: heading and control sitting next to each
// other on top, description on its own line below.
static void add_control_row(
  GtkBox     *box,
  const char *heading,
  GtkWidget  *control,
  const char *description
) {
  GtkWidget *header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

  gtk_widget_set_halign(control, GTK_ALIGN_START);
  gtk_widget_set_valign(control, GTK_ALIGN_CENTER);

  gtk_box_append(GTK_BOX(header_row), config_bold_label(heading));
  gtk_box_append(GTK_BOX(header_row), control);

  gtk_box_append(box, header_row);
  gtk_box_append(box, config_wrapped_help_label(description, 60));
}

void add_front_panel_tab(GtkWidget *notebook, struct alsa_card *card) {
  struct alsa_elem *brightness = get_elem_by_name(
    card->elems, "Front Panel Brightness"
  );
  struct alsa_elem *sleep_time = get_elem_by_name(
    card->elems, "Front Panel Sleep Time"
  );

  if (!brightness && !sleep_time)
    return;

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_margin_top(content, 20);
  gtk_widget_set_margin_start(content, 20);
  gtk_widget_set_margin_end(content, 20);
  gtk_widget_set_margin_bottom(content, 20);

  if (brightness) {
    add_control_row(
      GTK_BOX(content),
      "Brightness",
      make_drop_down_alsa_elem(brightness, NULL),
      "Sets the brightness of the front panel LEDs."
    );
  }

  if (sleep_time) {
    if (brightness)
      config_append_separator(GTK_BOX(content));

    add_control_row(
      GTK_BOX(content),
      "Sleep Time",
      make_sleep_time_dropdown(sleep_time),
      "After this much inactivity (no front panel adjustments or "
      "passing audio), the front panel LEDs turn off. Select Off to "
      "keep them lit indefinitely."
    );
  }

  g_object_set_data(G_OBJECT(content), PAGE_ID_KEY, (gpointer)"front-panel");
  gtk_notebook_append_page(
    GTK_NOTEBOOK(notebook), content, gtk_label_new("Front Panel")
  );
}
