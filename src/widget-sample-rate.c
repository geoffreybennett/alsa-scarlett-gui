// SPDX-FileCopyrightText: 2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gtkhelper.h"
#include "widget-boolean.h"

struct sample_rate {
  struct alsa_card *card;
  GtkWidget        *button;
  guint             source;
  char             *path;
  int               sample_rate;
};

static void button_set_text(GtkWidget *button, int value) {
  gtk_widget_remove_css_classes_by_prefix(button, "sample-rate-");

  if (!value) {
    gtk_button_set_label(GTK_BUTTON(button), "N/A");
    return;
  }

  char *text;
  if (value % 1000 == 0)
    text = g_strdup_printf("%dkHz", value / 1000);
  else
    text = g_strdup_printf("%.1fkHz", value / 1000.0);
  gtk_button_set_label(GTK_BUTTON(button), text);
  g_free(text);

  char *css_class = g_strdup_printf(
    "sample-rate-%d", value
  );
  gtk_widget_add_css_class(button, css_class);
  g_free(css_class);
}

// Read the sample rate from /proc/asound/cardN/stream0
// and return it as an integer
//
// Looking for a line containing:
//   Momentary freq = 48000 Hz (0x6.0000)
static int get_sample_rate(struct sample_rate *data) {
  if (!data->path)
    return 0;

  FILE *file = fopen(data->path, "r");
  if (!file) {
    perror("fopen");
    return 0;
  }

  char *line = NULL;
  size_t len = 0;
  ssize_t read;

  int sample_rate = 0;

  while ((read = getline(&line, &len, file)) != -1) {
    if (strstr(line, "Momentary freq = ")) {
      char *start = strstr(line, "Momentary freq = ") + 17;
      char *end = strstr(start, " Hz");

      if (!start || !end)
        continue;

      *end = '\0';
      sample_rate = atoi(start);

      break;
    }
  }

  free(line);
  fclose(file);

  return sample_rate;
}

static gboolean update_sample_rate(struct sample_rate *data) {
  int sample_rate = get_sample_rate(data);

  if (sample_rate != data->sample_rate) {
    data->sample_rate = sample_rate;
    button_set_text(data->button, sample_rate);
  }

  return G_SOURCE_CONTINUE;
}

static void on_destroy(struct sample_rate *data, GObject *widget) {
  if (data->source)
    g_source_remove(data->source);
  g_free(data->path);
  g_free(data);
}

GtkWidget *make_sample_rate_widget(
  struct alsa_card *card
) {
  struct sample_rate *data = g_malloc0(sizeof(struct sample_rate));
  data->card = card;
  data->button = gtk_toggle_button_new();
  data->sample_rate = -1;

  gtk_widget_set_sensitive(data->button, FALSE);
  gtk_widget_add_css_class(data->button, "fixed");
  gtk_widget_add_css_class(data->button, "sample-rate");

  // can only update if it's a real card
  if (card->num != SIMULATED_CARD_NUM) {
    data->path = g_strdup_printf("/proc/asound/card%d/stream0", card->num);
    data->source =
      g_timeout_add_seconds(1, (GSourceFunc)update_sample_rate, data);
  }

  // initial update (will show "N/A" for simulated card)
  update_sample_rate(data);

  // cleanup when the button is destroyed
  g_object_weak_ref(G_OBJECT(data->button), (GWeakNotify)on_destroy, data);

  return data->button;
}
