// SPDX-FileCopyrightText: 2024-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtk/gtk.h>
#include "gtkhelper.h"
#include "window-iface.h"
#include "window-modal.h"

static void modal_no_callback(GtkWidget *w, struct modal_data *modal_data) {
  GtkWidget *dialog = modal_data->dialog;

  alsa_unregister_reopen_callback(modal_data->serial);

  gtk_window_destroy(GTK_WINDOW(dialog));
  modal_data->card->window_modal = NULL;
  check_modal_window_closed();
}

static void modal_yes_callback(GtkWidget *w, struct modal_data *modal_data) {
  // remove the buttons
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(modal_data->button_box)))
    gtk_box_remove(GTK_BOX(modal_data->button_box), child);

  // add a progress bar
  modal_data->progress_bar = gtk_progress_bar_new();
  gtk_box_append(GTK_BOX(modal_data->button_box), modal_data->progress_bar);

  // change the title
  gtk_window_set_title(
    GTK_WINDOW(modal_data->dialog), modal_data->title_active
  );

  // if the card goes away, don't close this window
  modal_data->card->window_modal = NULL;

  modal_data->callback(modal_data);
}

static void free_modal_data(gpointer user_data) {
  struct modal_data *modal_data = user_data;

  g_free(modal_data->serial);
  g_free(modal_data);
}

void create_modal_window(
  GtkWidget        *w,
  struct alsa_card *card,
  const char       *title,
  const char       *title_active,
  const char       *message,
  modal_callback    callback
) {
  if (card->window_modal) {
    fprintf(stderr, "Error: Modal window already open\n");
    return;
  }

  GtkWidget *dialog = gtk_window_new();

  struct modal_data *modal_data = g_new0(struct modal_data, 1);
  modal_data->card = card;
  modal_data->serial = g_strdup(card->serial);
  modal_data->title_active = title_active;
  modal_data->dialog = dialog;
  modal_data->callback = callback;

  gtk_window_set_title(GTK_WINDOW(dialog), title);
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_widget_add_css_class(dialog, "window-frame");

  GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 50);
  gtk_window_set_child(GTK_WINDOW(dialog), content_box);
  gtk_widget_add_css_class(content_box, "window-content");
  gtk_widget_add_css_class(content_box, "top-level-content");
  gtk_widget_add_css_class(content_box, "big-padding");

  modal_data->label = gtk_label_new(message);
  gtk_box_append(GTK_BOX(content_box), modal_data->label);

  GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_margin(sep, 0);
  gtk_box_append(GTK_BOX(content_box), sep);

  modal_data->button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 50);
  gtk_widget_set_halign(modal_data->button_box, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(content_box), modal_data->button_box);

  g_object_set_data_full(
    G_OBJECT(dialog), "modal_data", modal_data, free_modal_data
  );

  GtkWidget *no_button = gtk_button_new_with_label("No");
  g_signal_connect(
    no_button, "clicked", G_CALLBACK(modal_no_callback), modal_data
  );
  gtk_box_append(GTK_BOX(modal_data->button_box), no_button);

  GtkWidget *yes_button = gtk_button_new_with_label("Yes");
  g_signal_connect(
    yes_button, "clicked", G_CALLBACK(modal_yes_callback), modal_data
  );
  gtk_box_append(GTK_BOX(modal_data->button_box), yes_button);

  gtk_widget_set_visible(dialog, TRUE);

  card->window_modal = dialog;
}

gboolean modal_update_progress(gpointer user_data) {
  struct progress_data *progress_data = user_data;
  struct modal_data *modal_data = progress_data->modal_data;

  // Done? Replace the progress bar with an Ok button.
  if (progress_data->progress < 0) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(modal_data->button_box)))
      gtk_box_remove(GTK_BOX(modal_data->button_box), child);

    GtkWidget *ok_button = gtk_button_new_with_label("Ok");
    g_signal_connect(
      ok_button, "clicked", G_CALLBACK(modal_no_callback), modal_data
    );
    gtk_box_append(GTK_BOX(modal_data->button_box), ok_button);
  } else {
    gtk_progress_bar_set_fraction(
      GTK_PROGRESS_BAR(modal_data->progress_bar),
      progress_data->progress / 100.0
    );
  }

  // Update the label text if we have a new message.
  if (progress_data->text)
    gtk_label_set_text(GTK_LABEL(modal_data->label), progress_data->text);

  g_free(progress_data->text);
  g_free(progress_data);
  return G_SOURCE_REMOVE;
}

// make the progress bar move along
// if it gets to the end twice, something probably went wrong
static gboolean update_progress_bar_reboot(gpointer user_data) {
  struct progress_data *progress_data = user_data;
  struct modal_data *modal_data = progress_data->modal_data;

  if (progress_data->progress >= 200) {
    // Done?
    gtk_label_set_text(
      GTK_LABEL(modal_data->label),
      "Reboot failed? Try unplugging/replugging/power-cycling the device."
    );

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(modal_data->button_box)))
      gtk_box_remove(GTK_BOX(modal_data->button_box), child);

    GtkWidget *ok_button = gtk_button_new_with_label("Ok");
    g_signal_connect(
      ok_button, "clicked", G_CALLBACK(modal_no_callback), modal_data
    );
    gtk_box_append(GTK_BOX(modal_data->button_box), ok_button);

    modal_data->timeout_id = 0;

    return G_SOURCE_REMOVE;
  }

  progress_data->progress++;
  gtk_progress_bar_set_fraction(
    GTK_PROGRESS_BAR(modal_data->progress_bar),
    (progress_data->progress % 100) / 100.0
  );

  return G_SOURCE_CONTINUE;
}

// this is called when the card is seen again so we can close the
// modal window
void modal_reopen_callback(void *user_data) {
  struct modal_data *modal_data = user_data;

  // stop the progress bar
  if (modal_data->timeout_id)
    g_source_remove(modal_data->timeout_id);

  // close the window
  gtk_window_destroy(GTK_WINDOW(modal_data->dialog));
}

// make a progress bar that moves while the device is rebooting
gboolean modal_start_reboot_progress(gpointer user_data) {
  struct modal_data *modal_data = user_data;

  gtk_label_set_text(GTK_LABEL(modal_data->label), "Rebooting...");

  struct progress_data *progress_data = g_new0(struct progress_data, 1);
  progress_data->modal_data = modal_data;
  progress_data->progress = 0;

  g_object_set_data_full(
    G_OBJECT(modal_data->progress_bar), "progress_data", progress_data, g_free
  );

  modal_data->timeout_id = g_timeout_add(
    55, update_progress_bar_reboot, progress_data
  );

  alsa_register_reopen_callback(
    modal_data->card->serial, modal_reopen_callback, modal_data
  );

  return G_SOURCE_REMOVE;
}
