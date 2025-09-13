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

static void add_state_filter(GtkFileChooserNative *native) {
  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "alsactl state file (.state)");
  gtk_file_filter_add_pattern(filter, "*.state");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), filter);
}

static void load_response(
  GtkNativeDialog *native,
  int              response,
  gpointer         data
) {
  struct alsa_card *card = data;

  if (response != GTK_RESPONSE_ACCEPT)
    goto done;

  GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(native));
  char *fn = g_file_get_path(file);

  run_alsactl(card, "restore", fn);

  g_free(fn);
  g_object_unref(file);

done:
  g_object_unref(native);
}

void activate_load(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  struct alsa_card *card = data;

  GtkFileChooserNative *native = gtk_file_chooser_native_new(
    "Load Configuration",
    GTK_WINDOW(card->window_main),
    GTK_FILE_CHOOSER_ACTION_OPEN,
    "_Load",
    "_Cancel"
  );

  add_state_filter(native);

  g_signal_connect(native, "response", G_CALLBACK(load_response), card);
  gtk_native_dialog_show(GTK_NATIVE_DIALOG(native));
}

static void save_response(
  GtkNativeDialog *native,
  int              response,
  gpointer         data
) {
  struct alsa_card *card = data;

  if (response != GTK_RESPONSE_ACCEPT)
    goto done;

  GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(native));
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

done:
  g_object_unref(native);
}

void activate_save(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  struct alsa_card *card = data;

  GtkFileChooserNative *native = gtk_file_chooser_native_new(
    "Save Configuration",
    GTK_WINDOW(card->window_main),
    GTK_FILE_CHOOSER_ACTION_SAVE,
    "_Save",
    "_Cancel"
  );

  add_state_filter(native);

  g_signal_connect(native, "response", G_CALLBACK(save_response), card);
  gtk_native_dialog_show(GTK_NATIVE_DIALOG(native));
}

static void sim_response(
  GtkNativeDialog *native,
  int              response,
  gpointer         data
) {
  GtkWindow *w = data;

  if (response != GTK_RESPONSE_ACCEPT)
    goto done;

  GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(native));
  char *fn = g_file_get_path(file);

  create_sim_from_file(w, fn);

  g_free(fn);
  g_object_unref(file);

done:
  g_object_unref(native);
}

void activate_sim(
  GSimpleAction *action,
  GVariant      *parameter,
  gpointer       data
) {
  GtkWidget *w = data;

  GtkFileChooserNative *native = gtk_file_chooser_native_new(
    "Load Configuration File for Interface Simulation",
    GTK_WINDOW(w),
    GTK_FILE_CHOOSER_ACTION_OPEN,
    "_Load",
    "_Cancel"
  );

  add_state_filter(native);

  g_signal_connect(native, "response", G_CALLBACK(sim_response), w);
  gtk_native_dialog_show(GTK_NATIVE_DIALOG(native));
}
