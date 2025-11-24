// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string.h>

#include "widget-text-entry.h"

struct text_entry {
  struct alsa_elem *elem;
  GtkWidget        *entry;
};

// User changed the text
static void text_entry_changed(GtkEditable *editable, struct text_entry *data) {
  const char *text = gtk_editable_get_text(editable);
  size_t len = strlen(text);

  // write to ALSA element (it will truncate if needed)
  alsa_set_elem_bytes(data->elem, text, len);
}

// ALSA element changed (from hardware or other source)
static void text_entry_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct text_entry *data = private;

  int is_writable = alsa_get_elem_writable(elem);
  gtk_widget_set_sensitive(data->entry, is_writable);

  size_t size;
  const void *bytes = alsa_get_elem_bytes(elem, &size);

  // get current text from widget
  const char *current_text = gtk_editable_get_text(GTK_EDITABLE(data->entry));

  // only update if the value is different
  if (bytes && size > 0) {
    // find actual string length (up to first null byte)
    size_t str_len = strnlen((const char *)bytes, size);

    // validate UTF-8 and create string
    gboolean is_valid = g_utf8_validate((const char *)bytes, str_len, NULL);
    if (str_len > 0 && is_valid) {
      // compare with current text
      if (strlen(current_text) != str_len ||
          memcmp(current_text, bytes, str_len) != 0) {
        char *str = g_strndup((const char *)bytes, str_len);
        gtk_editable_set_text(GTK_EDITABLE(data->entry), str);
        g_free(str);
      }
    } else {
      if (*current_text != '\0')
        gtk_editable_set_text(GTK_EDITABLE(data->entry), "");
    }
  } else {
    if (*current_text != '\0')
      gtk_editable_set_text(GTK_EDITABLE(data->entry), "");
  }
}

// Clean up when widget is destroyed
static void on_destroy(struct text_entry *data) {
  g_free(data);
}

// Create text entry widget bound to BYTES element
GtkWidget *make_text_entry_alsa_elem(struct alsa_elem *elem) {
  struct text_entry *data = g_malloc0(sizeof(struct text_entry));
  data->elem = elem;
  data->entry = gtk_entry_new();

  // connect signal for user changes
  g_signal_connect(
    data->entry, "changed", G_CALLBACK(text_entry_changed), data
  );

  // register callback for ALSA element changes
  alsa_elem_add_callback(elem, text_entry_updated, data, NULL);

  // initial update
  text_entry_updated(elem, data);

  // register cleanup callback
  g_object_weak_ref(G_OBJECT(data->entry), (GWeakNotify)on_destroy, data);

  return data->entry;
}
