// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "alsa.h"
#include "config-helpers.h"
#include "config-remote.h"
#include "dialog-pairing-qr.h"
#include "optional-state.h"
#include "remote-server.h"
#include "window-configuration.h"

struct remote_tab_data {
  struct alsa_card *card;
  GtkWidget        *enable_switch;
  GtkWidget        *status_label;
  GtkWidget        *auto_approve_switch;
  GtkWidget        *qr_button;
  GtkWidget        *clients_box;
};

static void on_enable_toggled(
  GObject    *gobject,
  GParamSpec *pspec,
  gpointer    user_data
);

static void on_auto_approve_toggled(
  GObject    *gobject,
  GParamSpec *pspec,
  gpointer    user_data
);

static void on_approve_pending_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  struct remote_tab_data *data = user_data;
  if (data && data->card && data->card->remote_server) {
    remote_server_approve_pending_client(data->card->remote_server);
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    dialog_pairing_qr_show(GTK_WINDOW(root), data->card->remote_server);
  }
}

static void update_tab_ui(struct remote_tab_data *data) {
  if (!data || !data->card || !data->card->remote_server)
    return;

  struct remote_server *server = data->card->remote_server;
  bool running = remote_server_is_running(server);
  bool auto_app = remote_server_get_auto_approve(server);

  g_signal_handlers_block_by_func(
    data->enable_switch, G_CALLBACK(on_enable_toggled), data
  );
  gtk_switch_set_active(GTK_SWITCH(data->enable_switch), running);
  g_signal_handlers_unblock_by_func(
    data->enable_switch, G_CALLBACK(on_enable_toggled), data
  );

  g_signal_handlers_block_by_func(
    data->auto_approve_switch, G_CALLBACK(on_auto_approve_toggled), data
  );
  gtk_switch_set_active(GTK_SWITCH(data->auto_approve_switch), auto_app);
  g_signal_handlers_unblock_by_func(
    data->auto_approve_switch, G_CALLBACK(on_auto_approve_toggled), data
  );

  if (running) {
    char *escaped_mdns = g_markup_escape_text(
      remote_server_get_mdns_name(server), -1
    );
    char *text = g_strdup_printf(
      "<span foreground='#2ecc71' weight='bold'>● Activo</span> — "
      "Puertos TCP %u / %u (mDNS: <tt>%s</tt>)",
      remote_server_get_port(server),
      remote_server_get_secure_port(server),
      escaped_mdns
    );
    gtk_label_set_markup(GTK_LABEL(data->status_label), text);
    g_free(text);
    g_free(escaped_mdns);
    gtk_widget_set_sensitive(data->qr_button, TRUE);
  } else {
    gtk_label_set_markup(
      GTK_LABEL(data->status_label),
      "<span foreground='#e74c3c' weight='bold'>○ Inactivo</span> — "
      "El servidor de control remoto está detenido"
    );
    gtk_widget_set_sensitive(data->qr_button, FALSE);
  }

  // Rebuild connected / pending clients list
  GtkWidget *child = gtk_widget_get_first_child(data->clients_box);
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_box_remove(GTK_BOX(data->clients_box), child);
    child = next;
  }

  bool has_items = false;

  if (running && remote_server_has_pending_pairing(server)) {
    const char *pname = remote_server_get_pending_client_name(server);
    GtkWidget *prow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    char *esc = g_markup_escape_text(pname ? pname : "Dispositivo móvil", -1);
    char *pmarkup = g_strdup_printf(
      "<span foreground='#f39c12' weight='bold'>● Solicitud pendiente:</span> %s",
      esc
    );
    GtkWidget *plbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(plbl), pmarkup);
    g_free(pmarkup);
    g_free(esc);

    GtkWidget *pbtn = gtk_button_new_with_label("Mostrar QR");
    g_signal_connect(
      pbtn, "clicked", G_CALLBACK(on_approve_pending_clicked), data
    );

    gtk_box_append(GTK_BOX(prow), plbl);
    gtk_box_append(GTK_BOX(prow), pbtn);
    gtk_box_append(GTK_BOX(data->clients_box), prow);
    has_items = true;
  }

  GList *client_names = remote_server_get_client_names(server);
  if (client_names) {
    for (GList *l = client_names; l; l = l->next) {
      char *name = l->data;
      GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
      GtkWidget *dot = gtk_label_new(NULL);
      gtk_label_set_markup(
        GTK_LABEL(dot), "<span foreground='#2ecc71'>●</span>"
      );
      GtkWidget *lbl = gtk_label_new(name);
      gtk_box_append(GTK_BOX(row), dot);
      gtk_box_append(GTK_BOX(row), lbl);
      gtk_box_append(GTK_BOX(data->clients_box), row);
      g_free(name);
    }
    g_list_free(client_names);
    has_items = true;
  }

  if (!has_items) {
    GtkWidget *none_lbl = config_help_label(
      running ? "No hay dispositivos móviles conectados actualmente."
              : "Inicia el servidor para permitir conexiones."
    );
    gtk_box_append(GTK_BOX(data->clients_box), none_lbl);
  }
}

static void on_server_status_cb(
  struct remote_server *server,
  void                 *user_data
) {
  (void)server;
  struct remote_tab_data *data = user_data;
  update_tab_ui(data);
}

static void on_enable_toggled(
  GObject    *gobject,
  GParamSpec *pspec,
  gpointer    user_data
) {
  (void)pspec;
  struct remote_tab_data *data = user_data;
  gboolean active = gtk_switch_get_active(GTK_SWITCH(gobject));

  if (active) {
    if (remote_server_start(data->card->remote_server, 0)) {
      optional_state_save(
        data->card, CONFIG_SECTION_DEVICE, "remote_enabled", "true"
      );
    } else {
      optional_state_save(
        data->card, CONFIG_SECTION_DEVICE, "remote_enabled", "false"
      );
    }
  } else {
    remote_server_stop(data->card->remote_server);
    optional_state_save(
      data->card, CONFIG_SECTION_DEVICE, "remote_enabled", "false"
    );
  }

  update_tab_ui(data);
}

static void on_auto_approve_toggled(
  GObject    *gobject,
  GParamSpec *pspec,
  gpointer    user_data
) {
  (void)pspec;
  struct remote_tab_data *data = user_data;
  gboolean active = gtk_switch_get_active(GTK_SWITCH(gobject));

  remote_server_set_auto_approve(data->card->remote_server, active);
  optional_state_save(
    data->card, CONFIG_SECTION_DEVICE, "remote_auto_approve",
    active ? "true" : "false"
  );
  update_tab_ui(data);
}

static void on_qr_button_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  struct remote_tab_data *data = user_data;
  GtkRoot *root = gtk_widget_get_root(data->qr_button);
  dialog_pairing_qr_show(GTK_WINDOW(root), data->card->remote_server);
}

static void on_tab_destroy(GtkWidget *widget, gpointer user_data) {
  (void)widget;
  struct remote_tab_data *data = user_data;
  if (data->card && data->card->remote_server) {
    remote_server_remove_status_listener(
      data->card->remote_server, on_server_status_cb, data
    );
  }
  g_free(data);
}

void add_remote_tab(GtkWidget *notebook, struct alsa_card *card) {
  if (!card || !card->remote_server)
    return;

  struct remote_tab_data *data = g_malloc0(sizeof(struct remote_tab_data));
  data->card = card;

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_top(content, 20);
  gtk_widget_set_margin_start(content, 20);
  gtk_widget_set_margin_end(content, 20);
  gtk_widget_set_margin_bottom(content, 20);

  // 1. Enable Server Switch
  GtkWidget *row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(
    GTK_BOX(row1), config_bold_label("Servidor de Control Remoto")
  );

  GtkWidget *sw_enable = gtk_switch_new();
  data->enable_switch = sw_enable;
  gtk_widget_set_valign(sw_enable, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(row1), sw_enable);
  gtk_box_append(GTK_BOX(content), row1);

  gtk_box_append(
    GTK_BOX(content),
    config_wrapped_help_label(
      "Permite controlar tu tarjeta desde la app oficial Focusrite Control 2 "
      "en tu smartphone Android o iOS a través de la red Wi-Fi local.",
      60
    )
  );

  g_signal_connect(
    sw_enable, "notify::active", G_CALLBACK(on_enable_toggled), data
  );

  config_append_separator(GTK_BOX(content));

  // 2. Server Status Info
  GtkWidget *row_status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append(GTK_BOX(row_status), config_bold_label("Estado:"));
  GtkWidget *status_lbl = gtk_label_new(NULL);
  data->status_label = status_lbl;
  gtk_box_append(GTK_BOX(row_status), status_lbl);
  gtk_box_append(GTK_BOX(content), row_status);

  config_append_separator(GTK_BOX(content));

  // 3. Auto-approve LAN devices switch
  GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(
    GTK_BOX(row2),
    config_bold_label("Auto-aprobar clientes en la red local")
  );

  GtkWidget *sw_auto = gtk_switch_new();
  data->auto_approve_switch = sw_auto;
  gtk_switch_set_active(
    GTK_SWITCH(sw_auto),
    remote_server_get_auto_approve(card->remote_server)
  );
  gtk_widget_set_valign(sw_auto, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(row2), sw_auto);
  gtk_box_append(GTK_BOX(content), row2);

  gtk_box_append(
    GTK_BOX(content),
    config_wrapped_help_label(
      "Si está activo, cualquier teléfono conectado a tu red Wi-Fi podrá "
      "vincularse directamente con un toque sin necesidad de escanear el QR.",
      60
    )
  );

  g_signal_connect(
    sw_auto, "notify::active", G_CALLBACK(on_auto_approve_toggled), data
  );

  config_append_separator(GTK_BOX(content));

  // 4. QR Code Pairing Button
  GtkWidget *row3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(
    GTK_BOX(row3), config_bold_label("Vincular Dispositivo (Código QR)")
  );

  GtkWidget *btn_qr = gtk_button_new_with_label("Mostrar Código QR");
  data->qr_button = btn_qr;
  gtk_widget_set_valign(btn_qr, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(row3), btn_qr);
  gtk_box_append(GTK_BOX(content), row3);

  gtk_box_append(
    GTK_BOX(content),
    config_wrapped_help_label(
      "Muestra la ventana de vinculación por código QR cifrado para "
      "la app Focusrite Control 2.",
      60
    )
  );

  g_signal_connect(btn_qr, "clicked", G_CALLBACK(on_qr_button_clicked), data);

  config_append_separator(GTK_BOX(content));

  // 5. Connected Clients List
  gtk_box_append(
    GTK_BOX(content), config_bold_label("Dispositivos Conectados")
  );
  GtkWidget *clients_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  data->clients_box = clients_box;
  gtk_box_append(GTK_BOX(content), clients_box);

  remote_server_add_status_listener(
    card->remote_server, on_server_status_cb, data
  );
  g_signal_connect(content, "destroy", G_CALLBACK(on_tab_destroy), data);

  update_tab_ui(data);

  g_object_set_data(G_OBJECT(content), PAGE_ID_KEY, (gpointer)"remote");
  gtk_notebook_append_page(
    GTK_NOTEBOOK(notebook), content, gtk_label_new("Control remoto")
  );
}
