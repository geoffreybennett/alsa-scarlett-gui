// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "alsa.h"
#include "alsa-sim.h"
#include "main.h"
#include "menu.h"
#include "scarlett2-firmware.h"
#include "window-hardware.h"
#include "window-iface.h"

GtkApplication *app;

// CSS

static void load_css(void) {
  GtkCssProvider *css = gtk_css_provider_new();
  GdkDisplay *display = gdk_display_get_default();

  gtk_style_context_add_provider_for_display(
    display, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
  );
  gtk_css_provider_load_from_resource(
    css,
    "/vu/b4/alsa-scarlett-gui/alsa-scarlett-gui.css"
  );

  g_object_unref(css);
}

// gtk init

static void startup(GtkApplication *app, gpointer user_data) {
  gtk_application_set_menubar(app, G_MENU_MODEL(create_app_menu(app)));

  load_css();

  scarlett2_enum_firmware();
  alsa_init();

  create_no_card_window();
  create_hardware_window(app);
}

// not called when any files are opened from the command-line so we do
// everything in startup(), but GTK wants this signal handled
// regardless
static void activate(GtkApplication *app, gpointer user_data) {
}

static void open_cb(
  GtkApplication  *app,
  GFile          **files,
  gint             n_files,
  const gchar     *hint
) {
  for (int i = 0; i < n_files; i++) {
    char *fn = g_file_get_path(files[i]);
    create_sim_from_file(NULL, fn);
    g_free(fn);
  }
}

int main(int argc, char **argv) {
  app = gtk_application_new(
    "vu.b4.alsa-scarlett-gui", G_APPLICATION_HANDLES_OPEN
  );
  g_signal_connect(app, "startup", G_CALLBACK(startup), NULL);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  g_signal_connect(app, "open", G_CALLBACK(open_cb), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);

  return status;
}
