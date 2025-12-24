// SPDX-FileCopyrightText: 2024-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "asound-stream-parser.h"
#include "gtkhelper.h"
#include "hw-io-availability.h"
#include "widget-boolean.h"
#include "window-routing.h"

struct sample_rate {
  struct alsa_card *card;
  GtkWidget        *button;
  guint             source;
  char             *path;
  int               sample_rate;
  int               is_current;  // showing current vs last-seen value
  int               last_valid_sample_rate;
  int               last_valid_playback_altset;
  int               last_valid_capture_altset;

  // max sample rate supported by each capture altset (for rate-based lookup)
  int               capture_altset_max_rate[4];
};

// Set the button text and optionally the glow CSS class
// If is_current is false, the sample rate is a last-seen value and
// shouldn't get the glow
static void button_set_text(
  GtkWidget *button,
  int        value,
  int        is_current
) {
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

  // only add glow CSS class if showing current value
  if (is_current) {
    char *css_class = g_strdup_printf("sample-rate-%d", value);
    gtk_widget_add_css_class(button, css_class);
    g_free(css_class);
    gtk_widget_remove_css_class(button, "inactive");
  } else {
    gtk_widget_add_css_class(button, "inactive");
  }
}

// Parse the max rate from a "Rates:" line like "Rates: 44100, 48000"
static int parse_max_rate(const char *line) {
  char *rates_str = strstr(line, "Rates: ");
  if (!rates_str)
    return 0;

  int max_rate = 0;
  char *p = rates_str + 7;

  while (*p) {
    // skip non-digits
    while (*p && (*p < '0' || *p > '9'))
      p++;
    if (!*p)
      break;

    int rate = atoi(p);
    if (rate > max_rate)
      max_rate = rate;

    // skip digits
    while (*p >= '0' && *p <= '9')
      p++;
  }

  return max_rate;
}

// Parse stream0 once at init to get channel counts and rates per altset
// Populates card->playback_altset_channels[], capture_altset_channels[],
// and data->capture_altset_max_rate[]
static void parse_stream_altset_channels(struct sample_rate *data) {
  struct alsa_card *card = data->card;

  if (!data->path)
    return;

  FILE *file = fopen(data->path, "r");
  if (!file)
    return;

  char *line = NULL;
  size_t len = 0;

  // 0 = looking for Playback/Capture, 1 = in Playback, 2 = in Capture
  int section = 0;
  int current_altset = 0;

  while (getline(&line, &len, file) != -1) {

    // detect section changes
    if (strncmp(line, "Playback:", 9) == 0) {
      section = 1;
      current_altset = 0;
      continue;
    }
    if (strncmp(line, "Capture:", 8) == 0) {
      section = 2;
      current_altset = 0;
      continue;
    }

    // skip if not in a section
    if (section == 0)
      continue;

    // look for "Altset N" lines (indented, within Interface block)
    char *altset_str = strstr(line, "Altset ");
    if (altset_str) {
      // skip Status block altset lines (they have "=" after)
      if (strstr(line, "Altset ="))
        continue;

      current_altset = atoi(altset_str + 7);
      if (current_altset > 0 && current_altset <= 3)
        card->altset_count = current_altset > card->altset_count
          ? current_altset : card->altset_count;
      continue;
    }

    // look for "Channels: N" lines
    char *channels_str = strstr(line, "Channels: ");
    if (channels_str && current_altset > 0 && current_altset <= 3) {
      int channels = atoi(channels_str + 10);
      if (section == 1)
        card->playback_altset_channels[current_altset] = channels;
      else
        card->capture_altset_channels[current_altset] = channels;
    }

    // look for "Rates:" lines (only for capture, for rate-based altset lookup)
    if (section == 2 && current_altset > 0 && current_altset <= 3) {
      int max_rate = parse_max_rate(line);
      if (max_rate > 0)
        data->capture_altset_max_rate[current_altset] = max_rate;
    }
  }

  free(line);
  fclose(file);
}

// Find the capture altset that supports a given sample rate
static int get_capture_altset_for_rate(struct sample_rate *data, int rate) {
  if (rate <= 0)
    return 0;

  int rate_cat = get_sample_rate_category(rate);

  // find the altset with the smallest max_rate category that is >= rate category
  int best_altset = 0;
  int best_max_rate_cat = -1;

  for (int i = 1; i <= 3; i++) {
    int max_rate = data->capture_altset_max_rate[i];
    if (max_rate <= 0)
      continue;

    int max_rate_cat = get_sample_rate_category(max_rate);
    if (max_rate_cat >= rate_cat) {
      if (best_altset == 0 || max_rate_cat < best_max_rate_cat) {
        best_altset = i;
        best_max_rate_cat = max_rate_cat;
      }
    }
  }

  return best_altset;
}

// Read the sample rate and altsets from /proc/asound/cardN/stream0
// Returns sample rate, sets *playback_altset_out and *capture_altset_out
//
// Looking for lines containing:
//   Altset = 2
//   Momentary freq = 48000 Hz (0x6.0000)
static int get_sample_rate_and_altsets(
  struct sample_rate *data,
  int                *playback_altset_out,
  int                *capture_altset_out
) {
  if (playback_altset_out)
    *playback_altset_out = 0;
  if (capture_altset_out)
    *capture_altset_out = 0;

  if (!data->path)
    return 0;

  FILE *file = fopen(data->path, "r");
  if (!file)
    return 0;

  char *line = NULL;
  size_t len = 0;

  int sample_rate = 0;
  int playback_altset = 0;
  int capture_altset = 0;

  // 0 = before any section, 1 = in Playback, 2 = in Capture
  int section = 0;
  int in_status = 0;

  while (getline(&line, &len, file) != -1) {

    // track section changes
    if (strncmp(line, "Playback:", 9) == 0) {
      section = 1;
      in_status = 1;
      continue;
    }
    if (strncmp(line, "Capture:", 8) == 0) {
      section = 2;
      in_status = 1;
      continue;
    }

    // exit status block when we hit Interface block (not "Interface =")
    if (in_status && strstr(line, "Interface ") &&
        !strstr(line, "Interface =")) {
      in_status = 0;
    }

    // look for altset in status sections
    if (in_status) {
      char *altset_str = strstr(line, "Altset = ");
      if (altset_str) {
        int altset = atoi(altset_str + 9);
        if (section == 1)
          playback_altset = altset;
        else if (section == 2)
          capture_altset = altset;
      }
    }

    // look for sample rate (first Momentary freq line, in Playback section)
    if (section == 1 && strstr(line, "Momentary freq = ")) {
      char *start = strstr(line, "Momentary freq = ") + 17;
      char *end = strstr(start, " Hz");

      if (start && end) {
        *end = '\0';
        sample_rate = atoi(start);
      }
    }
  }

  free(line);
  fclose(file);

  if (playback_altset_out)
    *playback_altset_out = playback_altset;
  if (capture_altset_out)
    *capture_altset_out = capture_altset;

  return sample_rate;
}

static gboolean update_sample_rate(struct sample_rate *data) {
  struct alsa_card *card = data->card;

  int playback_altset = 0;
  int capture_altset = 0;
  int sample_rate = get_sample_rate_and_altsets(
    data, &playback_altset, &capture_altset
  );

  // track last valid values for when stream becomes inactive
  if (sample_rate > 0) {
    data->last_valid_sample_rate = sample_rate;
    if (playback_altset > 0)
      data->last_valid_playback_altset = playback_altset;
    if (capture_altset > 0)
      data->last_valid_capture_altset = capture_altset;
  }

  // determine what to display
  int display_rate;
  int is_current;

  if (sample_rate > 0) {
    display_rate = sample_rate;
    is_current = 1;
  } else if (data->last_valid_sample_rate > 0) {
    // stream inactive, show last-seen value without glow
    display_rate = data->last_valid_sample_rate;
    is_current = 0;
  } else {
    display_rate = 0;  // will show N/A
    is_current = 0;
  }

  if (display_rate != data->sample_rate || is_current != data->is_current) {
    data->sample_rate = display_rate;
    data->is_current = is_current;
    button_set_text(data->button, display_rate, is_current);
  }

  // update PCM channel counts based on current altsets
  // playback and capture may have different altset counts
  int use_playback_altset = playback_altset > 0
    ? playback_altset : data->last_valid_playback_altset;

  // for capture altset: prefer status value, then last valid, then derive from rate
  int use_capture_altset;
  if (capture_altset > 0)
    use_capture_altset = capture_altset;
  else if (data->last_valid_capture_altset > 0)
    use_capture_altset = data->last_valid_capture_altset;
  else {
    // derive from sample rate (e.g. when only playback is active)
    int use_rate = sample_rate > 0 ? sample_rate : data->last_valid_sample_rate;
    use_capture_altset = get_capture_altset_for_rate(data, use_rate);
  }

  int old_playback = card->pcm_playback_channels;
  int old_capture = card->pcm_capture_channels;

  if (use_playback_altset > 0 && use_playback_altset <= 3)
    card->pcm_playback_channels =
      card->playback_altset_channels[use_playback_altset];

  if (use_capture_altset > 0 && use_capture_altset <= 3)
    card->pcm_capture_channels =
      card->capture_altset_channels[use_capture_altset];

  // if channel counts changed, update routing window labels
  if (old_playback != card->pcm_playback_channels ||
      old_capture != card->pcm_capture_channels) {
    update_all_pcm_labels(card);
  }

  // update HW I/O labels if sample rate category changed
  int use_sample_rate = sample_rate > 0
    ? sample_rate : data->last_valid_sample_rate;
  int old_sample_rate_cat = get_sample_rate_category(card->current_sample_rate);
  int new_sample_rate_cat = get_sample_rate_category(use_sample_rate);

  card->current_sample_rate = use_sample_rate;

  if (old_sample_rate_cat != new_sample_rate_cat)
    update_all_hw_io_labels(card);

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

    // parse channel counts per altset once at init
    parse_stream_altset_channels(data);

    data->source =
      g_timeout_add_seconds(1, (GSourceFunc)update_sample_rate, data);
  }

  // initial update (will show "N/A" for simulated card)
  update_sample_rate(data);

  // cleanup when the button is destroyed
  g_object_weak_ref(G_OBJECT(data->button), (GWeakNotify)on_destroy, data);

  return data->button;
}
