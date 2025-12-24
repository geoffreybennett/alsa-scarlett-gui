// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "window-hardware.h"

GtkWidget *window_hardware;

struct hw_info {
  char *name;
};

struct hw_cat {
  char *name;
  struct hw_info *info;
};

struct hw_info gen_1_info[] = {
  { "Scarlett 6i6 1st Gen" },
  { "Scarlett 8i6 1st Gen" },
  { "Scarlett 18i6 1st Gen" },
  { "Scarlett 18i8 1st Gen" },
  { "Scarlett 18i20 1st Gen" },
  { }
};

struct hw_info gen_2_info[] = {
  { "Scarlett 6i6 2nd Gen" },
  { "Scarlett 18i8 2nd Gen" },
  { "Scarlett 18i20 2nd Gen" },
  { }
};

struct hw_info gen_3_info[] = {
  { "Scarlett Solo 3rd Gen" },
  { "Scarlett 2i2 3rd Gen" },
  { "Scarlett 4i4 3rd Gen" },
  { "Scarlett 8i6 3rd Gen" },
  { "Scarlett 18i8 3rd Gen" },
  { "Scarlett 18i20 3rd Gen" },
  { }
};

struct hw_info gen_4_info[] = {
  { "Scarlett Solo 4th Gen" },
  { "Scarlett 2i2 4th Gen" },
  { "Scarlett 4i4 4th Gen" },
  { "Scarlett 16i16 4th Gen" },
  { "Scarlett 18i16 4th Gen" },
  { "Scarlett 18i20 4th Gen" },
  { }
};

struct hw_info clarett_usb_info[] = {
  { "Clarett 2Pre USB" },
  { "Clarett 4Pre USB" },
  { "Clarett 8Pre USB" },
  { }
};

struct hw_info clarett_plus_info[] = {
  { "Clarett+ 2Pre" },
  { "Clarett+ 4Pre" },
  { "Clarett+ 8Pre" },
  { }
};

struct hw_info vocaster_info[] = {
  { "Vocaster One" },
  { "Vocaster Two" },
  { }
};

struct hw_cat hw_cat[] = {
  { "1st Gen",
    gen_1_info
  },
  { "2nd Gen",
    gen_2_info
  },
  { "3rd Gen",
    gen_3_info
  },
  { "4th Gen",
    gen_4_info
  },
  { "Clarett USB",
    clarett_usb_info
  },
  { "Clarett+",
    clarett_plus_info
  },
  { "Vocaster",
    vocaster_info
  },
  { }
};

gboolean window_hardware_close_request(
  GtkWindow *w,
  gpointer   data
) {
  GtkApplication *app = data;

  g_action_group_activate_action(
    G_ACTION_GROUP(app), "hardware", NULL
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
  GtkApplication *app = user_data;

  if (keyval == GDK_KEY_Escape) {
    g_action_group_activate_action(G_ACTION_GROUP(app), "hardware", NULL);
    return 1;
  }

  if (state & GDK_CONTROL_MASK) {
    const char *action = NULL;

    switch (keyval) {
      case GDK_KEY_q: action = "quit";     break;
      case GDK_KEY_h: action = "hardware"; break;
    }

    if (action) {
      g_action_group_activate_action(G_ACTION_GROUP(app), action, NULL);
      return 1;
    }
  }

  return 0;
}

GtkWidget *make_notebook_page(struct hw_cat *cat) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  for (struct hw_info *info = cat->info; info->name; info++) {
    GtkWidget *label = gtk_label_new(info->name);
    gtk_box_append(GTK_BOX(box), label);
  }
  return box;
}

void add_notebook_pages(GtkWidget *notebook) {
  for (struct hw_cat *cat = hw_cat; cat->name; cat++) {
    GtkWidget *page = make_notebook_page(cat);
    GtkWidget *label = gtk_label_new(cat->name);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, label);
  }
}

void create_hardware_window(GtkApplication *app) {
  window_hardware = gtk_window_new();
  g_signal_connect(
    window_hardware,
    "close_request",
    G_CALLBACK(window_hardware_close_request),
    app
  );

  GtkEventController *key_controller = gtk_event_controller_key_new();
  gtk_widget_add_controller(window_hardware, key_controller);
  g_signal_connect(
    key_controller, "key-pressed", G_CALLBACK(on_key_press), app
  );

  gtk_window_set_title(
    GTK_WINDOW(window_hardware),
    "ALSA Scarlett Supported Hardware"
  );

  GtkWidget *top = gtk_frame_new(NULL);
  gtk_widget_add_css_class(top, "window-frame");
  gtk_window_set_child(GTK_WINDOW(window_hardware), top);

  GtkWidget *notebook = gtk_notebook_new();
  gtk_widget_add_css_class(notebook, "window-content");
  gtk_frame_set_child(GTK_FRAME(top), notebook);

  add_notebook_pages(notebook);
}
