// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "about.h"
#include "file.h"
#include "menu.h"
#include "optional-state.h"
#include "window-hardware.h"
#include "window-configuration.h"

// mapping of action names to window offsets within struct alsa_card
struct window_action {
  const char *action_name;
  size_t      window_offset;
};

static const struct window_action window_actions[] = {
  { "routing",       offsetof(struct alsa_card, window_routing)       },
  { "mixer",         offsetof(struct alsa_card, window_mixer)         },
  { "levels",        offsetof(struct alsa_card, window_levels)        },
  { "configuration", offsetof(struct alsa_card, window_configuration) },
  { "startup",       offsetof(struct alsa_card, window_startup)       },
  { "dsp",           offsetof(struct alsa_card, window_dsp)           },
  { NULL, 0 }
};

static GtkWidget *get_card_window(struct alsa_card *card, size_t offset) {
  return *(GtkWidget **)((char *)card + offset);
}

static const struct window_action *find_window_action(const char *action_name) {
  for (const struct window_action *entry = window_actions;
       entry->action_name;
       entry++)
    if (strcmp(entry->action_name, action_name) == 0)
      return entry;
  return NULL;
}

static char *make_window_key(const char *action_name) {
  return g_strdup_printf("window-%s", action_name);
}

// set action state and window visibility (no persistence)
static void set_window_visible(
  GSimpleAction *action,
  GtkWidget     *window,
  gboolean       visible
) {
  g_action_change_state(G_ACTION(action), g_variant_new_boolean(visible));
  gtk_widget_set_visible(window, visible);
}

// toggle visibility; returns new state for callers that need to persist it
static gboolean toggle_visibility(GSimpleAction *action, GtkWidget *widget) {
  GVariant *state = g_action_get_state(G_ACTION(action));
  gboolean new_state = !g_variant_get_boolean(state);
  set_window_visible(action, widget, new_state);
  return new_state;
}

static void save_window_state(
  struct alsa_card *card,
  const char       *action_name,
  gboolean          visible
) {
  char *key = make_window_key(action_name);
  optional_state_save(card, CONFIG_SECTION_UI, key, visible ? "true" : "false");
  g_free(key);
}

// toggle visibility and save state (for card windows)
static void activate_window(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  struct alsa_card *card = data;
  const char *action_name = g_action_get_name(G_ACTION(action));

  const struct window_action *entry = find_window_action(action_name);
  if (!entry)
    return;

  GtkWidget *window = get_card_window(card, entry->window_offset);
  gboolean new_state = toggle_visibility(action, window);
  save_window_state(card, action_name, new_state);
}

static void activate_hardware(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  toggle_visibility(action, window_hardware);
}

static void activate_quit(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  g_application_quit(G_APPLICATION(data));
}

static const GActionEntry app_entries[] = {
  {"hardware", activate_hardware, NULL, "false"},
  {"quit",     activate_quit},
};

struct menu_item {
  const char *label;
  const char *action_name;
  const char *accelerators[2];
};

struct menu_data {
  const char       *label;
  struct menu_item *items;
};

static const struct menu_data menus[] = {
  {
    "_File",
    (struct menu_item[]){
      { "_Load Configuration",   "win.load", { "<Control>O", NULL } },
      { "_Save Configuration",   "win.save", { "<Control>S", NULL } },
      { "_Interface Simulation", "win.sim",  { "<Control>I", NULL } },
      { "E_xit",                 "app.quit", { "<Control>Q", NULL } },
      {}
    }
  },
  {
    "_View",
    (struct menu_item[]){
      { "_Routing",       "win.routing",       { "<Control>R", NULL } },
      { "_Mixer",         "win.mixer",         { "<Control>M", NULL } },
      { "_Levels",        "win.levels",        { "<Control>L", NULL } },
      { "_DSP",           "win.dsp",           { "<Control>D", NULL } },
      { "_Configuration", "win.configuration", { "<Control>G", NULL } },
      { "_Startup",       "win.startup",       { "<Control>T", NULL } },
      {}
    }
  },
  {
    "_Help",
    (struct menu_item[]){
      { "_Supported Hardware", "app.hardware", { "<Control>H",     NULL } },
      { "_About",              "win.about",    { "<Control>slash", NULL } },
      {}
    }
  },
  {}
};

static void populate_submenu(
  GtkApplication         *app,
  GMenu                  *menu,
  const struct menu_data *data
) {
  GMenu *submenu = g_menu_new();
  g_menu_append_submenu(menu, data->label, G_MENU_MODEL(submenu));

  // An empty-initialised menu_item marks the end
  for (struct menu_item *item = data->items; item->label; item++) {
    g_menu_append(submenu, item->label, item->action_name);
    gtk_application_set_accels_for_action(
      app, item->action_name, item->accelerators
    );
  }
}

GMenu *create_app_menu(GtkApplication *app) {
  g_action_map_add_action_entries(
    G_ACTION_MAP(app), app_entries, G_N_ELEMENTS(app_entries), app
  );

  GMenu *menu = g_menu_new();

  for (const struct menu_data *menu_data = menus;
       menu_data->label;
       menu_data++)
    populate_submenu(app, menu, menu_data);

  return menu;
}

static const GActionEntry win_entries[] = {
  {"about", activate_about},
  {"sim",   activate_sim}
};

void add_window_action_map(GtkWindow *w) {
  g_action_map_add_action_entries(
    G_ACTION_MAP(w), win_entries, G_N_ELEMENTS(win_entries), w
  );
}

static const GActionEntry load_save_entries[] = {
  {"load", activate_load},
  {"save", activate_save}
};

void add_load_save_action_map(struct alsa_card *card) {
  g_action_map_add_action_entries(
    G_ACTION_MAP(card->window_main),
    load_save_entries,
    G_N_ELEMENTS(load_save_entries),
    card
  );
}

static const GActionEntry startup_entry[] = {
  {"startup", activate_window, NULL, "false"}
};

void add_startup_action_map(struct alsa_card *card) {
  g_action_map_add_action_entries(
    G_ACTION_MAP(card->window_main),
    startup_entry,
    G_N_ELEMENTS(startup_entry),
    card
  );
}

static const GActionEntry mixer_entries[] = {
  {"routing", activate_window, NULL, "false"},
  {"mixer",   activate_window, NULL, "false"}
};

static const GActionEntry levels_entries[] = {
  {"levels", activate_window, NULL, "false"}
};

static const GActionEntry configuration_entries[] = {
  {"configuration", activate_window, NULL, "false"}
};

void add_mixer_action_map(struct alsa_card *card) {
  g_action_map_add_action_entries(
    G_ACTION_MAP(card->window_main),
    mixer_entries,
    G_N_ELEMENTS(mixer_entries),
    card
  );

  // Hide the levels menu item if there is no "Firmware Version"
  // control (working kernel support for level meters was added in the
  // same version as the "Firmware Version" control)
  if (get_elem_by_name(card->elems, "Firmware Version")) {
    g_action_map_add_action_entries(
      G_ACTION_MAP(card->window_main),
      levels_entries,
      G_N_ELEMENTS(levels_entries),
      card
    );
  }

  // Always show configuration menu
  g_action_map_add_action_entries(
    G_ACTION_MAP(card->window_main),
    configuration_entries,
    G_N_ELEMENTS(configuration_entries),
    card
  );
}

static const GActionEntry dsp_entries[] = {
  {"dsp", activate_window, NULL, "false"}
};

void add_dsp_action_map(struct alsa_card *card) {
  g_action_map_add_action_entries(
    G_ACTION_MAP(card->window_main),
    dsp_entries,
    G_N_ELEMENTS(dsp_entries),
    card
  );
}

static gboolean should_restore_window(
  GHashTable *state,
  const char *action_name
) {
  char *key = make_window_key(action_name);
  const char *value = g_hash_table_lookup(state, key);
  g_free(key);
  return value && strcmp(value, "true") == 0;
}

static void restore_single_window(
  struct alsa_card              *card,
  const struct window_action    *entry,
  GHashTable                    *state
) {
  if (!should_restore_window(state, entry->action_name))
    return;

  GtkWidget *window = get_card_window(card, entry->window_offset);
  GAction *action = g_action_map_lookup_action(
    G_ACTION_MAP(card->window_main), entry->action_name
  );
  if (action)
    set_window_visible(G_SIMPLE_ACTION(action), window, TRUE);
}

void restore_window_visibility(struct alsa_card *card) {
  GHashTable *state = optional_state_load(card, CONFIG_SECTION_UI);
  if (!state)
    return;

  for (const struct window_action *entry = window_actions;
       entry->action_name;
       entry++)
    restore_single_window(card, entry, state);

  g_hash_table_destroy(state);
}
