// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "alsa.h"
#include "gtkhelper.h"
#include "optional-controls.h"
#include "optional-state.h"
#include "custom-names.h"
#include "device-port-names.h"
#include "port-enable.h"
#include "widget-text-entry.h"
#include "window-configuration.h"
#include "config-autogain.h"
#include "config-device-name.h"
#include "config-device-settings.h"
#include "config-io.h"
#include "config-monitor-groups.h"

struct configuration_window {
  struct alsa_card *card;
  GtkWidget        *top;
};

static void on_destroy(
  struct configuration_window *data,
  GtkWidget                   *widget
) {
  g_free(data);
}

// Keys used to store the configuration window tabs in the state file
#define CONFIG_TAB_KEY "configuration-tab"
#define CONFIG_IO_TAB_KEY "configuration-io-tab"

// Data for notebook tab persistence
struct notebook_tab_data {
  struct alsa_card *card;
  const char       *key;
};

// Callback when a notebook tab changes
static void on_tab_changed(
  GtkNotebook *notebook,
  GtkWidget   *page,
  guint        page_num,
  gpointer     user_data
) {
  struct notebook_tab_data *data = user_data;

  const char *page_id = g_object_get_data(G_OBJECT(page), PAGE_ID_KEY);
  if (page_id)
    optional_state_save(data->card, CONFIG_SECTION_UI, data->key, page_id);
}

// Free notebook tab data
static void free_notebook_tab_data(gpointer data) {
  g_free(data);
}

// Find page index by ID, returns -1 if not found
static int find_page_by_id(GtkNotebook *notebook, const char *page_id) {
  int n_pages = gtk_notebook_get_n_pages(notebook);

  for (int i = 0; i < n_pages; i++) {
    GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
    const char *id = g_object_get_data(G_OBJECT(page), PAGE_ID_KEY);
    if (id && strcmp(id, page_id) == 0)
      return i;
  }

  return -1;
}

// Restore the saved tab selection and connect signal to save changes
void setup_notebook_tab_persistence(
  GtkNotebook      *notebook,
  struct alsa_card *card,
  const char       *key
) {
  // Restore saved tab from [ui] section
  GHashTable *state = optional_state_load(card, CONFIG_SECTION_UI);
  if (state) {
    const char *value = g_hash_table_lookup(state, key);
    if (value) {
      int page_num = find_page_by_id(notebook, value);
      if (page_num >= 0)
        gtk_notebook_set_current_page(notebook, page_num);
    }
    g_hash_table_destroy(state);
  }

  // Connect signal to save tab changes
  struct notebook_tab_data *data = g_malloc(sizeof(struct notebook_tab_data));
  data->card = card;
  data->key = key;

  g_signal_connect_data(
    notebook, "switch-page", G_CALLBACK(on_tab_changed), data,
    (GClosureNotify)free_notebook_tab_data, 0
  );
}

// Wrap tab content in a scrolled window for vertical scrolling
GtkWidget *wrap_tab_content_scrolled(GtkWidget *content) {
  GtkWidget *scrolled = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scrolled),
    GTK_POLICY_NEVER,
    GTK_POLICY_AUTOMATIC
  );
  gtk_scrolled_window_set_propagate_natural_height(
    GTK_SCROLLED_WINDOW(scrolled), TRUE
  );
  gtk_widget_set_vexpand(scrolled, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), content);
  return scrolled;
}

GtkWidget *create_configuration_controls(struct alsa_card *card) {
  struct configuration_window *data =
    g_malloc0(sizeof(struct configuration_window));
  data->card = card;

  // create main container
  GtkWidget *top = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
  gtk_widget_add_css_class(top, "window-frame");
  GtkWidget *vbox = top;

  data->top = top;

  // Create top-level notebook for configuration sections
  GtkWidget *top_notebook = gtk_notebook_new();
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(top_notebook), GTK_POS_TOP);
  gtk_widget_add_css_class(top_notebook, "outer-notebook");

  // Device Name tab
  add_device_name_tab(top_notebook, card);

  // Device Settings tab
  add_device_settings_tab(top_notebook, card);

  // I/O Configuration tab
  add_io_tab(top_notebook, card);

  // Autogain tab
  add_autogain_tab(top_notebook, card);

  // Monitor Groups tab
  add_monitor_groups_tab(top_notebook, card);

  // Add top notebook to main container (only if it has pages)
  if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(top_notebook)) > 0) {
    gtk_widget_set_vexpand(top_notebook, TRUE);
    gtk_box_append(GTK_BOX(vbox), top_notebook);

    // Restore saved tab and connect signal to save tab changes
    setup_notebook_tab_persistence(
      GTK_NOTEBOOK(top_notebook), card, CONFIG_TAB_KEY
    );
  }

  // cleanup on destroy
  g_object_weak_ref(
    G_OBJECT(data->top),
    (GWeakNotify)on_destroy,
    data
  );

  return data->top;
}
