// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>
#include <string.h>

#include "alsa.h"
#include "dialog-pairing-qr.h"
#include "qrcodegen.h"
#include "remote-server.h"

struct qr_dialog_data {
  GtkWidget            *dialog;
  GtkWidget            *desc_label;
  GtkWidget            *frame;
  GtkWidget            *picture;
  GtkWidget            *status_label;
  GtkWidget            *approve_btn;
  struct remote_server *server;
};

static struct qr_dialog_data *active_qr_dialog = NULL;

static GdkTexture *create_qr_texture(const char *text, int scale) {
  if (!text || !*text)
    return NULL;

  uint8_t qrcode[qrcodegen_BUFFER_LEN_MAX];
  uint8_t temp_buf[qrcodegen_BUFFER_LEN_MAX];

  bool ok = qrcodegen_encodeText(
    text, temp_buf, qrcode, qrcodegen_Ecc_LOW,
    qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
    qrcodegen_Mask_AUTO, true
  );
  if (!ok)
    return NULL;

  int qr_size = qrcodegen_getSize(qrcode);
  int border = 4;
  int full_size = qr_size + border * 2;
  int pixel_width = full_size * scale;
  int pixel_height = full_size * scale;
  gsize stride = (gsize)pixel_width * 4;

  uint8_t *pixels = g_malloc0(stride * (gsize)pixel_height);

  for (int y = 0; y < pixel_height; y++) {
    int qry = (y / scale) - border;
    for (int x = 0; x < pixel_width; x++) {
      int qrx = (x / scale) - border;
      bool black = false;
      if (qrx >= 0 && qrx < qr_size && qry >= 0 && qry < qr_size)
        black = qrcodegen_getModule(qrcode, qrx, qry);

      size_t offset = (size_t)y * stride + (size_t)x * 4;
      uint8_t val = black ? 0x00 : 0xFF;
      pixels[offset + 0] = val;
      pixels[offset + 1] = val;
      pixels[offset + 2] = val;
      pixels[offset + 3] = 0xFF;
    }
  }

  GBytes *bytes = g_bytes_new_take(pixels, stride * (gsize)pixel_height);
  GdkTexture *tex = gdk_memory_texture_new(
    pixel_width, pixel_height, GDK_MEMORY_R8G8B8A8, bytes, stride
  );
  g_bytes_unref(bytes);
  return tex;
}

static void refresh_qr_dialog(struct qr_dialog_data *data) {
  if (!data || !data->dialog || !data->server)
    return;

  struct remote_server *server = data->server;
  bool pending = remote_server_has_pending_pairing(server);
  bool scanning = remote_server_is_qr_scanning(server);
  const char *qr_payload = remote_server_get_pending_qr_payload(server);
  int clients = remote_server_get_connected_client_count(server);

  if (pending && qr_payload && *qr_payload) {
    const char *cname = remote_server_get_pending_client_name(server);
    if (scanning) {
      char *desc = g_strdup_printf(
        "Vinculando con «%s».\n"
        "Escanea este código QR con la cámara de Focusrite Control 2 "
        "en tu teléfono:",
        cname ? cname : "Dispositivo móvil"
      );
      gtk_label_set_text(GTK_LABEL(data->desc_label), desc);
      g_free(desc);

      GdkTexture *tex = create_qr_texture(qr_payload, 5);
      gtk_picture_set_paintable(
        GTK_PICTURE(data->picture), tex ? GDK_PAINTABLE(tex) : NULL
      );
      if (tex)
        g_object_unref(tex);

      gtk_widget_set_visible(data->frame, TRUE);
      gtk_widget_set_visible(data->approve_btn, FALSE);
      gtk_label_set_markup(
        GTK_LABEL(data->status_label),
        "<span foreground='#f39c12' weight='bold'>"
        "Esperando escaneo del código QR..."
        "</span>"
      );
    } else {
      char *desc = g_strdup_printf(
        "Solicitud de acceso recibida de «%s».\n"
        "Pulsa «Conectar» para mostrar el código QR cifrado de vinculación:",
        cname ? cname : "Dispositivo móvil"
      );
      gtk_label_set_text(GTK_LABEL(data->desc_label), desc);
      g_free(desc);

      gtk_widget_set_visible(data->frame, FALSE);
      gtk_button_set_label(GTK_BUTTON(data->approve_btn), "Conectar");
      gtk_widget_set_visible(data->approve_btn, TRUE);
      gtk_label_set_markup(
        GTK_LABEL(data->status_label),
        "<span foreground='#f39c12' weight='bold'>"
        "Esperando tu confirmación en este equipo..."
        "</span>"
      );
    }
  } else {
    gtk_widget_set_visible(data->frame, FALSE);
    gtk_widget_set_visible(data->approve_btn, FALSE);

    gtk_label_set_text(
      GTK_LABEL(data->desc_label),
      "1. Abre Focusrite Control 2 en tu teléfono Android o iOS.\n"
      "2. Asegúrate de estar conectado a la misma red local.\n"
      "3. El código QR cifrado específico para tu teléfono aparecerá "
      "aquí automáticamente en cuanto la app detecte este equipo."
    );

    if (clients > 0) {
      gtk_label_set_markup(
        GTK_LABEL(data->status_label),
        "<span foreground='#2ecc71' weight='bold'>"
        "✓ ¡Dispositivo conectado y autorizado!"
        "</span>"
      );
    } else {
      gtk_label_set_markup(
        GTK_LABEL(data->status_label),
        "<span foreground='#3498db'>"
        "Esperando conexión desde la app Focusrite Control 2..."
        "</span>"
      );
    }
  }
}

static void on_status_updated(struct remote_server *server, void *user_data) {
  (void)server;
  refresh_qr_dialog(user_data);
}

static void on_approve_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  struct qr_dialog_data *data = user_data;
  if (data && data->server)
    remote_server_begin_qr_scanning(data->server);
}

static void on_dialog_destroy(GtkWidget *widget, gpointer user_data) {
  (void)widget;
  struct qr_dialog_data *data = user_data;
  if (active_qr_dialog == data)
    active_qr_dialog = NULL;
  if (data->server) {
    remote_server_remove_status_listener(
      data->server, on_status_updated, data
    );
    if (remote_server_has_pending_pairing(data->server))
      remote_server_cancel_pending_pairing(data->server);
  }
  g_free(data);
}

void dialog_pairing_qr_show(GtkWindow *parent, struct remote_server *server) {
  if (!server)
    return;

  if (active_qr_dialog && active_qr_dialog->server == server) {
    refresh_qr_dialog(active_qr_dialog);
    gtk_window_present(GTK_WINDOW(active_qr_dialog->dialog));
    return;
  }

  struct qr_dialog_data *data = g_malloc0(sizeof(struct qr_dialog_data));
  data->server = server;
  active_qr_dialog = data;

  GtkWidget *dialog = gtk_window_new();
  data->dialog = dialog;
  gtk_window_set_title(GTK_WINDOW(dialog), "Vincular Focusrite Control 2");
  if (parent)
    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 420);

  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_set_margin_top(vbox, 20);
  gtk_widget_set_margin_bottom(vbox, 20);
  gtk_widget_set_margin_start(vbox, 20);
  gtk_widget_set_margin_end(vbox, 20);
  gtk_window_set_child(GTK_WINDOW(dialog), vbox);

  GtkWidget *title_lbl = gtk_label_new(NULL);
  gtk_label_set_markup(
    GTK_LABEL(title_lbl),
    "<span size='large' weight='bold'>Conectar con Focusrite Control 2</span>"
  );
  gtk_box_append(GTK_BOX(vbox), title_lbl);

  GtkWidget *desc_lbl = gtk_label_new(NULL);
  gtk_label_set_wrap(GTK_LABEL(desc_lbl), TRUE);
  gtk_label_set_max_width_chars(GTK_LABEL(desc_lbl), 46);
  data->desc_label = desc_lbl;
  gtk_box_append(GTK_BOX(vbox), desc_lbl);

  GtkWidget *picture = gtk_picture_new();
  gtk_widget_set_size_request(picture, 240, 240);
  data->picture = picture;

  GtkWidget *frame = gtk_frame_new(NULL);
  gtk_widget_set_halign(frame, GTK_ALIGN_CENTER);
  gtk_frame_set_child(GTK_FRAME(frame), picture);
  data->frame = frame;
  gtk_box_append(GTK_BOX(vbox), frame);

  GtkWidget *status_lbl = gtk_label_new(NULL);
  gtk_label_set_wrap(GTK_LABEL(status_lbl), TRUE);
  data->status_label = status_lbl;
  gtk_box_append(GTK_BOX(vbox), status_lbl);

  GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_halign(btn_box, GTK_ALIGN_END);

  GtkWidget *approve_btn = gtk_button_new_with_label("Aprobar ahora");
  g_signal_connect(approve_btn, "clicked", G_CALLBACK(on_approve_clicked), data);
  data->approve_btn = approve_btn;
  gtk_box_append(GTK_BOX(btn_box), approve_btn);

  GtkWidget *close_btn = gtk_button_new_with_label("Cerrar");
  g_signal_connect_swapped(
    close_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog
  );
  gtk_box_append(GTK_BOX(btn_box), close_btn);
  gtk_box_append(GTK_BOX(vbox), btn_box);

  remote_server_add_status_listener(server, on_status_updated, data);
  g_signal_connect(dialog, "destroy", G_CALLBACK(on_dialog_destroy), data);

  refresh_qr_dialog(data);
  gtk_window_present(GTK_WINDOW(dialog));
}
