// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "window-helper.h"
#include "window-iface.h"

gboolean window_startup_close_request(GtkWindow *w, gpointer data) {
  struct alsa_card *card = data;

  gtk_widget_activate_action(
    GTK_WIDGET(card->window_main), "win.startup", NULL
  );
  return true;
}

static gboolean on_key_press(
  GtkEventControllerKey *controller,
  guint                  keyval,
  guint                  keycode,
  GdkModifierType        state,
  gpointer               user_data
) {
  struct alsa_card *card = user_data;

  GtkWidget *widget = gtk_event_controller_get_widget(
    GTK_EVENT_CONTROLLER(controller)
  );

  if (keyval == GDK_KEY_Escape) {
    gtk_window_close(GTK_WINDOW(widget));
    return 1;
  }

  // Forward hotkeys to main window
  if (state & GDK_CONTROL_MASK) {
    const char *action = NULL;

    switch (keyval) {
      case GDK_KEY_r: action = "win.routing";       break;
      case GDK_KEY_m: action = "win.mixer";         break;
      case GDK_KEY_l: action = "win.levels";        break;
      case GDK_KEY_d: action = "win.dsp";           break;
      case GDK_KEY_g: action = "win.configuration"; break;
      case GDK_KEY_t: action = "win.startup";       break;
      case GDK_KEY_o: action = "win.load";          break;
      case GDK_KEY_s: action = "win.save";          break;
      case GDK_KEY_i: action = "win.sim";           break;
      case GDK_KEY_slash: action = "win.about";     break;
      case GDK_KEY_q: action = "app.quit";          break;
      case GDK_KEY_h: action = "app.hardware";      break;
    }

    if (action) {
      gtk_widget_activate_action(
        GTK_WIDGET(card->window_main), action, NULL
      );
      return 1;
    }
  }

  return 0;
}

GtkWidget *create_subwindow(
  struct alsa_card *card,
  const char       *name,
  GCallback         close_callback
) {
  char *base_title = get_card_window_title(card);
  char *title = g_strdup_printf("%s - %s", base_title, name);
  g_free(base_title);

  GtkWidget *w = gtk_window_new();
  gtk_window_set_resizable(GTK_WINDOW(w), FALSE);
  gtk_window_set_title(GTK_WINDOW(w), title);
  g_signal_connect(w, "close_request", G_CALLBACK(close_callback), card);

  GtkEventController *key_controller = gtk_event_controller_key_new();
  gtk_widget_add_controller(w, key_controller);
  g_signal_connect(
    key_controller, "key-pressed", G_CALLBACK(on_key_press), card
  );

  g_free(title);
  return w;
}
