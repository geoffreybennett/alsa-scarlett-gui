// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "about.h"
#include "file.h"
#include "menu.h"
#include "window-hardware.h"

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

static void activate_startup(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  struct alsa_card *card = data;

  update_visibility(action, card->window_startup);
}

static const GActionEntry app_entries[] = {
  {"hardware", activate_hardware, NULL, "false"},
  {"quit",     activate_quit},
};

GMenu *create_app_menu(GtkApplication *app) {
  g_action_map_add_action_entries(
    G_ACTION_MAP(app), app_entries, G_N_ELEMENTS(app_entries), app
  );

  GMenu *menu = g_menu_new();

  GMenu *file_menu = g_menu_new();
  g_menu_append_submenu(menu, "_File", G_MENU_MODEL(file_menu));
  g_menu_append(file_menu, "_Load Configuration",   "win.load");
  g_menu_append(file_menu, "_Save Configuration",   "win.save");
  g_menu_append(file_menu, "_Interface Simulation", "win.sim");
  g_menu_append(file_menu, "E_xit", "app.quit");

  GMenu *view_menu = g_menu_new();
  g_menu_append_submenu(menu, "_View", G_MENU_MODEL(view_menu));
  g_menu_append(view_menu, "_Routing", "win.routing");
  g_menu_append(view_menu, "_Mixer",   "win.mixer");
  g_menu_append(view_menu, "_Levels",  "win.levels");
  g_menu_append(view_menu, "_Startup", "win.startup");

  GMenu *help_menu = g_menu_new();
  g_menu_append_submenu(menu, "_Help", G_MENU_MODEL(help_menu));
  g_menu_append(help_menu, "_Supported Hardware", "app.hardware");
  g_menu_append(help_menu, "_About",              "win.about");

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
  {"mixer",   activate_mixer,   NULL, "false"},
  {"levels",  activate_levels,  NULL, "false"}
};

void add_mixer_action_map(struct alsa_card *card) {
  g_action_map_add_action_entries(
    G_ACTION_MAP(card->window_main),
    mixer_entries,
    G_N_ELEMENTS(mixer_entries),
    card
  );
}
