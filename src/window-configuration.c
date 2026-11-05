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
#include "config-display.h"
#include "config-front-panel.h"
#include "config-io.h"
#include "config-monitor-groups.h"
#include "window-startup.h"
#include "config-remote.h"

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
  // During window destruction, the notebook is detached from the
  // hierarchy before tabs are removed, triggering this callback.
  if (!gtk_widget_get_root(GTK_WIDGET(notebook)))
    return;

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

// Search every notebook under root for a page whose PAGE_ID_KEY matches
// page_id. On a match, select that page and, as the recursion unwinds,
// select each ancestor notebook page that contains it, so a nested tab
// is revealed along with its outer tabs.
static gboolean select_tab(GtkWidget *root, const char *page_id) {
  if (GTK_IS_NOTEBOOK(root)) {
    GtkNotebook *nb = GTK_NOTEBOOK(root);
    int n = gtk_notebook_get_n_pages(nb);
    for (int i = 0; i < n; i++) {
      GtkWidget *page = gtk_notebook_get_nth_page(nb, i);
      const char *id = g_object_get_data(G_OBJECT(page), PAGE_ID_KEY);
      if ((id && strcmp(id, page_id) == 0) || select_tab(page, page_id)) {
        gtk_notebook_set_current_page(nb, i);
        return TRUE;
      }
    }
    return FALSE;
  }

  for (GtkWidget *c = gtk_widget_get_first_child(root);
       c;
       c = gtk_widget_get_next_sibling(c))
    if (select_tab(c, page_id))
      return TRUE;

  return FALSE;
}

gboolean configuration_select_tab(struct alsa_card *card, const char *page_id) {
  if (!card->window_configuration)
    return FALSE;

  GtkWidget *content =
    gtk_window_get_child(GTK_WINDOW(card->window_configuration));
  if (!content)
    return FALSE;

  return select_tab(content, page_id);
}

// Collect the PAGE_ID_KEY of every leaf notebook page under root, in
// display order, into ids. A page that itself contains a nested notebook
// (e.g. I/O Configuration) is skipped in favour of its inner pages, so
// each entry maps to a distinct capture. Returns TRUE if it added any id
// from within root. The strings are owned by the widgets.
static gboolean collect_tab_ids(GtkWidget *root, GPtrArray *ids) {
  gboolean added = FALSE;

  if (GTK_IS_NOTEBOOK(root)) {
    GtkNotebook *nb = GTK_NOTEBOOK(root);
    int n = gtk_notebook_get_n_pages(nb);
    for (int i = 0; i < n; i++) {
      GtkWidget *page = gtk_notebook_get_nth_page(nb, i);
      gboolean nested = collect_tab_ids(page, ids);
      const char *id = g_object_get_data(G_OBJECT(page), PAGE_ID_KEY);
      if (id && !nested)
        g_ptr_array_add(ids, (gpointer)id);
      if (id || nested)
        added = TRUE;
    }
    return added;
  }

  for (GtkWidget *c = gtk_widget_get_first_child(root);
       c;
       c = gtk_widget_get_next_sibling(c))
    if (collect_tab_ids(c, ids))
      added = TRUE;

  return added;
}

// Return a floating "as" GVariant listing the configuration tab ids in
// display order (empty if there is no configuration window), for use as
// the state of the win.config-tabs action.
GVariant *configuration_tab_ids(struct alsa_card *card) {
  GPtrArray *ids = g_ptr_array_new();

  if (card->window_configuration) {
    GtkWidget *content =
      gtk_window_get_child(GTK_WINDOW(card->window_configuration));
    if (content)
      collect_tab_ids(content, ids);
  }
  g_ptr_array_add(ids, NULL);

  GVariant *v = g_variant_new_strv((const char * const *)ids->pdata, -1);
  g_ptr_array_free(ids, TRUE);
  return v;
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

  // Front Panel tab
  add_front_panel_tab(top_notebook, card);

  // I/O Configuration tab
  add_io_tab(top_notebook, card);

  // Autogain tab
  add_autogain_tab(top_notebook, card);

  // Monitor Groups tab
  add_monitor_groups_tab(top_notebook, card);

  // Startup tab
  add_startup_tab(top_notebook, card);

  // Display tab
  add_display_tab(top_notebook, card);

  // Remote Control tab
  add_remote_tab(top_notebook, card);

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
