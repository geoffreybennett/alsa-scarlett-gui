// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "device-reset-config.h"
#include "device-update-firmware.h"
#include "fcp-socket.h"
#include "gtkhelper.h"
#include "scarlett2.h"
#include "scarlett2-ioctls.h"
#include "widget-boolean.h"
#include "widget-drop-down.h"
#include "window-startup.h"

static GtkWidget *small_label(const char *text) {
  GtkWidget *w = gtk_label_new(NULL);

  char *s = g_strdup_printf("<b>%s</b>", text);
  gtk_label_set_markup(GTK_LABEL(w), s);
  free(s);

  gtk_widget_set_valign(w, GTK_ALIGN_START);

  return w;
}

static GtkWidget *big_label(const char *text) {
  GtkWidget *view = gtk_text_view_new ();
  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));

  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (view), GTK_WRAP_WORD);
  gtk_widget_set_size_request (view, 600, -1);
  gtk_widget_set_sensitive (view, FALSE);

  gtk_text_buffer_set_text (buffer, text, -1);

  return view;
}

static void add_sep(GtkWidget *grid, int *grid_y) {
  if (!*grid_y)
    return;

  GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_margin_top(sep, 10);
  gtk_widget_set_margin_bottom(sep, 10);
  gtk_widget_set_margin_start(sep, 20);
  gtk_widget_set_margin_end(sep, 20);
  gtk_grid_attach(GTK_GRID(grid), sep, 0, (*grid_y)++, 3, 1);
}

static void add_standalone_control(
  GArray    *elems,
  GtkWidget *grid,
  int       *grid_y
) {
  struct alsa_elem *standalone = get_elem_by_name(elems, "Standalone Switch");

  if (!standalone)
    return;

  add_sep(grid, grid_y);

  GtkWidget *w;

  w = small_label("Standalone");
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y, 1, 1);

  w = make_boolean_alsa_elem(standalone, "Disabled", "Enabled");
  gtk_widget_set_valign(w, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y + 1, 1, 1);

  w = big_label(
    "When Standalone mode is enabled, the interface will continue to "
    "route audio as per the previous routing and mixer settings "
    "after it has been disconnected from a computer. By configuring "
    "the routing between the hardware and mixer inputs and outputs "
    "appropriately, the interface can act as a standalone preamp or "
    "mixer."
  );
  gtk_grid_attach(GTK_GRID(grid), w, 1, *grid_y, 1, 2);

  *grid_y += 2;
}

static void add_phantom_persistence_control(
  GArray    *elems,
  GtkWidget *grid,
  int       *grid_y
) {
  struct alsa_elem *phantom = get_elem_by_name(
    elems, "Phantom Power Persistence Capture Switch"
  );

  if (!phantom)
    return;

  add_sep(grid, grid_y);

  GtkWidget *w;

  w = small_label("Phantom Power Persistence");
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y, 1, 1);

  w = make_boolean_alsa_elem(phantom, "Disabled", "Enabled");
  gtk_widget_set_valign(w, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y + 1, 1, 1);

  w = big_label(
    "When Phantom Power Persistence is enabled, the interface will "
    "restore the previous Phantom Power/48V setting when the "
    "interface is turned on. For the safety of microphones which can "
    "be damaged by phantom power, the interface defaults to having "
    "phantom power disabled when it is turned on."
  );
  gtk_grid_attach(GTK_GRID(grid), w, 1, *grid_y, 1, 2);

  *grid_y += 2;
}

static int add_msd_control(
  GArray    *elems,
  GtkWidget *grid,
  int       *grid_y
) {
  struct alsa_elem *msd = get_elem_by_name(
    elems, "MSD Mode Switch"
  );

  if (!msd)
    return 0;

  add_sep(grid, grid_y);

  GtkWidget *w;

  w = small_label("MSD (Mass Storage Device) Mode");
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y, 1, 1);

  w = make_boolean_alsa_elem(msd, "Disabled", "Enabled");
  gtk_widget_set_valign(w, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y + 1, 1, 1);

  w = big_label(
    "When MSD Mode is enabled (as it is from the factory), the "
    "interface has reduced functionality. You’ll want to have this "
    "disabled. On the other hand, when MSD Mode is enabled, the "
    "interface presents itself as a Mass Storage Device (like a USB "
    "stick), containing a link to the Focusrite web site encouraging "
    "you to register your product and download the proprietary "
    "drivers which can’t be used on Linux."
  );
  gtk_grid_attach(GTK_GRID(grid), w, 1, *grid_y, 1, 2);

  *grid_y += 2;

  return 1;
}

static int add_spdif_mode_control(
  GArray    *elems,
  GtkWidget *grid,
  int       *grid_y
) {
  const char * const mode_names[] = {
    "S/PDIF Mode",
    "Digital I/O Mode",
    NULL
  };

  struct alsa_elem *spdif = NULL;
  int i = 0;

  while (mode_names[i]) {
    if ((spdif = get_elem_by_prefix(elems, mode_names[i])))
      break;

    i++;
  }

  if (!spdif)
    return 0;

  add_sep(grid, grid_y);

  GtkWidget *w;

  w = small_label(mode_names[i]);
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y, 1, 1);

  w = make_drop_down_alsa_elem(spdif, NULL);
  gtk_widget_set_valign(w, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y + 1, 1, 1);

  w = big_label(
    i == 0 ? (
      "The S/PDIF Mode selects whether the interface can receive "
      "S/PDIF input from the coaxial (RCA) input or the optical "
      "(TOSLINK) input. This requires a reboot to take effect."
    ) : (
      "The Digital I/O Mode selects whether the interface can "
      "receive S/PDIF input from the coaxial (RCA) input, the "
      "optical (TOSLINK) input, or whether dual-ADAT mode is "
      "enabled. This requires a reboot to take effect."
    )
  );
  gtk_grid_attach(GTK_GRID(grid), w, 1, *grid_y, 1, 2);

  *grid_y += 2;

  return 1;
}

static void add_reset_action(
  struct alsa_card *card,
  GtkWidget        *grid,
  int              *grid_y,
  const char       *label,
  const char       *button_label,
  const char       *description,
  GCallback        callback
) {
  add_sep(grid, grid_y);

  GtkWidget *w;

  w = small_label(label);
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y, 1, 1);

  w = gtk_button_new_with_label(button_label);
  gtk_grid_attach(GTK_GRID(grid), w, 0, *grid_y + 1, 1, 1);
  g_signal_connect(w, "clicked", callback, card);

  w = big_label(description);
  gtk_grid_attach(GTK_GRID(grid), w, 1, *grid_y, 1, 2);

  *grid_y += 2;
}

static void reboot_device(GtkWidget *button, struct alsa_card *card) {
  int err = 0;

  // HWDEP (Scarlett2) driver type
  if (card->driver_type == DRIVER_TYPE_HWDEP) {
    snd_hwdep_t *hwdep;

    err = scarlett2_open_card(card->device, &hwdep);
    if (err < 0) {
      fprintf(stderr, "unable to open hwdep interface: %s\n", snd_strerror(err));
      return;
    }

    err = scarlett2_reboot(hwdep);
    if (err < 0) {
      fprintf(stderr, "unable to reboot device: %s\n", snd_strerror(err));
      return;
    }

    scarlett2_close(hwdep);

  // Socket (FCP) driver type
  } else if (card->driver_type == DRIVER_TYPE_SOCKET) {
    err = fcp_socket_reboot_device(card);
    if (err < 0)
      fprintf(stderr, "unable to reboot device via socket\n");
  }
}

static void add_reset_actions(
  struct alsa_card *card,
  GtkWidget        *grid,
  int              *grid_y,
  int               show_reboot_option
) {
  if (card->driver_type != DRIVER_TYPE_HWDEP &&
      card->driver_type != DRIVER_TYPE_SOCKET)
    return;

  // Add reboot action if there is a control that requires a reboot
  // to take effect
  if (show_reboot_option) {
    add_reset_action(
      card,
      grid,
      grid_y,
      "Reboot Device",
      "Reboot",
      "Rebooting the interface will apply changes made to the "
      "startup configuration. This will take a few seconds.",
      G_CALLBACK(reboot_device)
    );
  }

  // Reset Configuration
  add_reset_action(
    card,
    grid,
    grid_y,
    "Reset Configuration",
    "Reset",
    "Resetting the configuration will reset the interface to its "
    "factory default settings. The firmware will be left unchanged.",
    G_CALLBACK(create_reset_config_window)
  );

  // Update Firmware
  struct alsa_elem *firmware_elem =
    get_elem_by_name(card->elems, "Firmware Version");

  if (!firmware_elem)
    return;

  int firmware_version = alsa_get_elem_value(firmware_elem);

  if (firmware_version >= card->best_firmware_version)
    return;

  char *s = g_strdup_printf(
    "Updating the firmware will reset the interface to its "
    "factory default settings and update the firmware from version "
    "%d to %d.",
    firmware_version,
    card->best_firmware_version
  );
  add_reset_action(
    card,
    grid,
    grid_y,
    "Update Firmware",
    "Update",
    s,
    G_CALLBACK(create_update_firmware_window)
  );

  g_free(s);
}

static void add_no_startup_controls_msg(GtkWidget *grid) {
  GtkWidget *w = big_label(
    "This device/driver combo appears to have no startup controls."
  );
  gtk_grid_attach(GTK_GRID(grid), w, 0, 0, 1, 1);
}

GtkWidget *create_startup_controls(struct alsa_card *card) {
  GArray *elems = card->elems;

  GtkWidget *top = gtk_frame_new(NULL);
  gtk_widget_add_css_class(top, "window-frame");

  int grid_y = 0;

  GtkWidget *grid = gtk_grid_new();
  gtk_widget_add_css_class(grid, "window-content");
  gtk_widget_add_css_class(grid, "top-level-content");
  gtk_widget_add_css_class(grid, "window-startup");
  gtk_grid_set_column_spacing(GTK_GRID(grid), 20);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  gtk_frame_set_child(GTK_FRAME(top), grid);

  add_standalone_control(elems, grid, &grid_y);
  add_phantom_persistence_control(elems, grid, &grid_y);
  int has_msd = add_msd_control(elems, grid, &grid_y);
  int has_spdif_mode = add_spdif_mode_control(elems, grid, &grid_y);
  int show_reboot_option = has_msd || has_spdif_mode;
  add_reset_actions(card, grid, &grid_y, show_reboot_option);

  if (!grid_y)
    add_no_startup_controls_msg(grid);

  return top;
}
