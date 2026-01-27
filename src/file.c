// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "alsa.h"
#include "alsa-sim.h"
#include "error.h"
#include "file.h"
#include "stringhelper.h"

static void run_alsactl(
  struct alsa_card *card,
  char             *cmd,
  char             *fn
) {
  GtkWindow *w = GTK_WINDOW(card->window_main);

  gchar *alsactl_path = g_find_program_in_path("alsactl");

  if (!alsactl_path)
    alsactl_path = g_strdup("/usr/sbin/alsactl");

  gchar *argv[] = {
    alsactl_path, cmd, card->device, "-I", "-f", fn, NULL
  };

  gchar  *stdout;
  gchar  *stderr;
  gint    exit_status;
  GError *error = NULL;

  gboolean result = g_spawn_sync(
    NULL,
    argv,
    NULL,
    G_SPAWN_SEARCH_PATH,
    NULL,
    NULL,
    &stdout,
    &stderr,
    &exit_status,
    &error
  );

  if (result && WIFEXITED(exit_status) && WEXITSTATUS(exit_status) == 0)
    goto done;

  char *error_message =
    result
      ? g_strdup_printf("%s\n%s", stdout, stderr)
      : g_strdup_printf("%s", error->message);

  char *msg = g_strdup_printf(
    "Error running “alsactl %s %s -f %s”: %s",
    cmd, card->device, fn, error_message
    );
  show_error(w, msg);
  g_free(msg);
  g_free(error_message);

done:
  g_free(alsactl_path);
  g_free(stdout);
  g_free(stderr);
  if (error)
    g_error_free(error);
}

static GtkFileFilter *create_state_filter(void) {
  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "alsactl state file (.state)");
  gtk_file_filter_add_pattern(filter, "*.state");
  return filter;
}

static void load_ready(
  GObject      *source,
  GAsyncResult *result,
  gpointer      data
) {
  struct alsa_card *card = data;
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);

  GFile *file = gtk_file_dialog_open_finish(dialog, result, NULL);
  if (!file)
    return;

  char *fn = g_file_get_path(file);
  run_alsactl(card, "restore", fn);

  g_free(fn);
  g_object_unref(file);
}

void activate_load(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  struct alsa_card *card = data;

  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Load Configuration");

  GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  g_list_store_append(filters, create_state_filter());
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  g_object_unref(filters);

  gtk_file_dialog_open(
    dialog,
    GTK_WINDOW(card->window_main),
    NULL,
    load_ready,
    card
  );
  g_object_unref(dialog);
}

static void save_ready(
  GObject      *source,
  GAsyncResult *result,
  gpointer      data
) {
  struct alsa_card *card = data;
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);

  GFile *file = gtk_file_dialog_save_finish(dialog, result, NULL);
  if (!file)
    return;

  char *fn = g_file_get_path(file);

  // append .state if not present
  char *fn_with_ext;
  if (string_ends_with(fn, ".state"))
    fn_with_ext = g_strdup_printf("%s", fn);
  else
    fn_with_ext = g_strdup_printf("%s.state", fn);

  run_alsactl(card, "store", fn_with_ext);

  g_free(fn);
  g_free(fn_with_ext);
  g_object_unref(file);
}

void activate_save(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  struct alsa_card *card = data;

  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Save Configuration");

  GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  g_list_store_append(filters, create_state_filter());
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  g_object_unref(filters);

  gtk_file_dialog_save(
    dialog,
    GTK_WINDOW(card->window_main),
    NULL,
    save_ready,
    card
  );
  g_object_unref(dialog);
}

static void sim_ready(
  GObject      *source,
  GAsyncResult *result,
  gpointer      data
) {
  GtkWindow *w = data;
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source);

  GFile *file = gtk_file_dialog_open_finish(dialog, result, NULL);
  if (!file)
    return;

  char *fn = g_file_get_path(file);
  create_sim_from_file(w, fn);

  g_free(fn);
  g_object_unref(file);
}

void activate_sim(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  GtkWidget *w = data;

  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Load Configuration File for Interface Simulation");

  GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  g_list_store_append(filters, create_state_filter());
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  g_object_unref(filters);

  gtk_file_dialog_open(
    dialog,
    GTK_WINDOW(w),
    NULL,
    sim_ready,
    w
  );
  g_object_unref(dialog);
}
