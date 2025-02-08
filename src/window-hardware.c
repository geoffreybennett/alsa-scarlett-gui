// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
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

struct hw_info gen_3_small_info[] = {
  { "Scarlett Solo 3rd Gen" },
  { "Scarlett 2i2 3rd Gen" },
  { }
};

struct hw_info gen_3_big_info[] = {
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

struct hw_cat hw_cat[] = {
  { "1st Gen",
    gen_1_info
  },
  { "2nd Gen",
    gen_2_info
  },
  { "Small 3rd Gen",
    gen_3_small_info
  },
  { "Big 3rd Gen",
    gen_3_big_info
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

  gtk_window_set_title(
    GTK_WINDOW(window_hardware),
    "ALSA Scarlett Supported Hardware"
  );

  GtkWidget *notebook = gtk_notebook_new();
  gtk_window_set_child(GTK_WINDOW(window_hardware), notebook);

  add_notebook_pages(notebook);
}
