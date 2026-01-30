// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <glib.h>
#include <glib/gstdio.h>

#include "welcome.h"
#include "gtkhelper.h"

#define BETA_VERSION "1.0beta"
#define WELCOME_FILE "welcome.conf"

static char *get_config_dir(void) {
  const char *config_home = g_get_user_config_dir();
  return g_build_filename(config_home, "alsa-scarlett-gui", NULL);
}

static char *get_welcome_path(void) {
  char *config_dir = get_config_dir();
  char *path = g_build_filename(config_dir, WELCOME_FILE, NULL);
  g_free(config_dir);
  return path;
}

static char *get_today(void) {
  GDateTime *now = g_date_time_new_now_local();
  char *today = g_date_time_format(now, "%Y-%m-%d");
  g_date_time_unref(now);
  return today;
}

static gboolean is_snoozed_today(void) {
  char *path = get_welcome_path();
  GKeyFile *key_file = g_key_file_new();
  gboolean snoozed = FALSE;

  if (g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL)) {
    char *snooze_date = g_key_file_get_string(
      key_file, BETA_VERSION, "snooze_date", NULL
    );
    if (snooze_date) {
      char *today = get_today();
      snoozed = strcmp(snooze_date, today) == 0;
      g_free(today);
      g_free(snooze_date);
    }
  }

  g_key_file_free(key_file);
  g_free(path);
  return snoozed;
}

static void set_snooze_date(void) {
  char *config_dir = get_config_dir();

  if (g_mkdir_with_parents(config_dir, 0755) < 0) {
    g_warning("Failed to create config directory: %s", config_dir);
    g_free(config_dir);
    return;
  }
  g_free(config_dir);

  char *path = get_welcome_path();
  GKeyFile *key_file = g_key_file_new();

  g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL);
  char *today = get_today();
  g_key_file_set_string(key_file, BETA_VERSION, "snooze_date", today);
  g_free(today);
  g_key_file_save_to_file(key_file, path, NULL);

  g_key_file_free(key_file);
  g_free(path);
}

static void on_dismiss(GtkWidget *button, GtkWindow *window) {
  gtk_window_destroy(window);
}

static void on_remind_later(GtkWidget *button, GtkWindow *window) {
  set_snooze_date();
  gtk_window_destroy(window);
}

void show_welcome(GtkApplication *app) {
  if (is_snoozed_today())
    return;

  GtkWidget *window = gtk_window_new();
  char *title = g_strdup_printf("Welcome to ALSA Scarlett GUI %s", VERSION);
  gtk_window_set_title(GTK_WINDOW(window), title);
  g_free(title);
  gtk_window_set_application(GTK_WINDOW(window), app);
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
  gtk_widget_add_css_class(window, "window-frame");
  gtk_widget_add_css_class(window, "modal");

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
  gtk_window_set_child(GTK_WINDOW(window), content);
  gtk_widget_add_css_class(content, "window-content");
  gtk_widget_add_css_class(content, "top-level-content");
  gtk_widget_set_margin(content, 30);

  GtkWidget *heading = gtk_label_new(NULL);
  char *heading_markup = g_strdup_printf(
    "<span size='large' weight='bold'>"
      "Welcome to ALSA Scarlett GUI %s!"
    "</span>",
    VERSION
  );
  gtk_label_set_markup(GTK_LABEL(heading), heading_markup);
  g_free(heading_markup);
  gtk_box_append(GTK_BOX(content), heading);

  GtkWidget *message = gtk_label_new(
    "Thank you for testing this beta release.\n\n"
    "Please check the release notes for what's new, join the\n"
    "discussion, and if you encounter any issues, search for\n"
    "existing reports or raise a new issue on GitHub."
  );
  gtk_label_set_justify(GTK_LABEL(message), GTK_JUSTIFY_CENTER);
  gtk_box_append(GTK_BOX(content), message);

  GtkWidget *links_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_halign(links_box, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(content), links_box);

  GtkWidget *release_notes = gtk_link_button_new_with_label(
    "https://github.com/geoffreybennett/alsa-scarlett-gui/blob/"
      BETA_VERSION "/RELEASE-NOTES.md",
    "Release Notes"
  );
  gtk_box_append(GTK_BOX(links_box), release_notes);

  GtkWidget *discussion = gtk_link_button_new_with_label(
    "https://github.com/geoffreybennett/alsa-scarlett-gui/discussions/221",
    "Join the Discussion"
  );
  gtk_box_append(GTK_BOX(links_box), discussion);

  GtkWidget *issues = gtk_link_button_new_with_label(
    "https://github.com/geoffreybennett/alsa-scarlett-gui/issues",
    "Report or Search for Issues"
  );
  gtk_box_append(GTK_BOX(links_box), issues);

  GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
  gtk_widget_set_halign(button_box, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(content), button_box);

  GtkWidget *dismiss_button = gtk_button_new_with_label("Dismiss");
  g_signal_connect(
    dismiss_button, "clicked", G_CALLBACK(on_dismiss), window
  );
  gtk_box_append(GTK_BOX(button_box), dismiss_button);

  GtkWidget *remind_button = gtk_button_new_with_label("Remind Me Later");
  g_signal_connect(
    remind_button, "clicked", G_CALLBACK(on_remind_later), window
  );
  gtk_box_append(GTK_BOX(button_box), remind_button);

  gtk_window_present(GTK_WINDOW(window));
}
