// SPDX-FileCopyrightText: 2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>

#include "alsa.h"
#include "iface-waiting.h"
#include "scarlett2-ioctls.h"
#include "window-iface.h"

// Structure to hold timeout-related widgets
struct timeout_data {
  GtkWidget *box;
  GtkWidget *spinner;
  GtkWidget *message_label;
  guint timeout_id;
};

// Timeout callback function
static gboolean on_timeout(gpointer user_data) {
  struct timeout_data *data = (struct timeout_data *)user_data;

  // Remove spinner
  gtk_box_remove(GTK_BOX(data->box), data->spinner);

  // Update message with clickable link
  if (data->message_label && GTK_IS_WIDGET(data->message_label))
    gtk_label_set_markup(
      GTK_LABEL(data->message_label),
      "Driver not detected. Please ensure "
      "<span font='monospace'>fcp-server</span> from "
      "<a href=\"https://github.com/geoffreybennett/fcp-support\">"
      "https://github.com/geoffreybennett/fcp-support</a> "
      "has been installed."
    );

  // Reset the timeout ID since it won't be called again
  data->timeout_id = 0;

  // Return FALSE to prevent the timeout from repeating
  return FALSE;
}

// Weak reference callback for cleanup
static void on_widget_dispose(gpointer data, GObject *where_the_object_was) {
  struct timeout_data *timeout_data = (struct timeout_data *)data;

  // Cancel the timeout if it's still active
  if (timeout_data->timeout_id > 0)
    g_source_remove(timeout_data->timeout_id);

  // Free the data structure
  g_free(timeout_data);
}

GtkWidget *create_iface_waiting_main(struct alsa_card *card) {
  struct timeout_data *data;

  // Main vertical box
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
  gtk_widget_set_margin_start(box, 40);
  gtk_widget_set_margin_end(box, 40);
  gtk_widget_set_margin_top(box, 40);
  gtk_widget_set_margin_bottom(box, 40);

  // Heading
  GtkWidget *label = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(label),
            "<span weight='bold' size='large'>Waiting for FCP Server</span>");
  gtk_box_append(GTK_BOX(box), label);

  // Add picture (scaled down properly)
  GtkWidget *picture_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(picture_box, TRUE);
  gtk_widget_set_halign(picture_box, GTK_ALIGN_CENTER);

  GtkWidget *picture = gtk_picture_new_for_resource(
    "/vu/b4/alsa-scarlett-gui/icons/vu.b4.alsa-scarlett-gui.png"
  );
  gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
  gtk_widget_set_size_request(picture, 128, 128);

  gtk_box_append(GTK_BOX(picture_box), picture);
  gtk_box_append(GTK_BOX(box), picture_box);

  // Add spinner
  GtkWidget *spinner = gtk_spinner_new();
  gtk_spinner_start(GTK_SPINNER(spinner));
  gtk_widget_set_size_request(spinner, 48, 48);
  gtk_box_append(GTK_BOX(box), spinner);

  // Description
  label = gtk_label_new(
    "Waiting for the user-space FCP driver to initialise..."
  );
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
  gtk_label_set_max_width_chars(GTK_LABEL(label), 1);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_widget_set_halign(label, GTK_ALIGN_FILL);

  gtk_box_append(GTK_BOX(box), label);

  // Setup timeout
  data = g_new(struct timeout_data, 1);
  data->box = box;
  data->spinner = spinner;
  data->message_label = label;

  // Set timeout
  data->timeout_id = g_timeout_add_seconds(5, on_timeout, data);

  // Ensure data is freed when the box is destroyed
  g_object_weak_ref(G_OBJECT(box), on_widget_dispose, data);

  return box;
}
