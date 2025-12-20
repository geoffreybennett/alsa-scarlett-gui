// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "about.h"
#include "file.h"
#include "menu.h"
#include "window-hardware.h"
#include "window-configuration.h"

// helper for common code of activate_*() functions
static void update_visibility(
  GSimpleAction *action,
  GtkWidget     *widget
) {
  GVariant *state = g_action_get_state(G_ACTION(action));
  gboolean new_state = !g_variant_get_boolean(state);

  g_action_change_state(G_ACTION(action), g_variant_new_boolean(new_state));
  gtk_widget_set_visible(widget, new_state);
}

static void activate_hardware(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  (void) data;
  update_visibility(action, window_hardware);
}

static void activate_quit(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  g_application_quit(G_APPLICATION(data));
}

static void activate_routing(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  struct alsa_card *card = data;

  update_visibility(action, card->window_routing);
}

static void activate_mixer(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  struct alsa_card *card = data;

  update_visibility(action, card->window_mixer);
}

static void activate_levels(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  struct alsa_card *card = data;

  update_visibility(action, card->window_levels);
}

static void activate_configuration(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  struct alsa_card *card = data;

  update_visibility(action, card->window_configuration);
}

static void activate_startup(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  struct alsa_card *card = data;

  update_visibility(action, card->window_startup);
}

static void activate_dsp(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  struct alsa_card *card = data;

  update_visibility(action, card->window_dsp);
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
  {"startup", activate_startup, NULL, "false"}
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
  {"routing", activate_routing, NULL, "false"},
  {"mixer",   activate_mixer,   NULL, "false"}
};

static const GActionEntry levels_entries[] = {
  {"levels",  activate_levels,  NULL, "false"}
};

static const GActionEntry configuration_entries[] = {
  {"configuration", activate_configuration, NULL, "false"}
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
  {"dsp", activate_dsp, NULL, "false"}
};

void add_dsp_action_map(struct alsa_card *card) {
  g_action_map_add_action_entries(
    G_ACTION_MAP(card->window_main),
    dsp_entries,
    G_N_ELEMENTS(dsp_entries),
    card
  );
}
