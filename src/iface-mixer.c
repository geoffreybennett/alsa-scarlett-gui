// SPDX-FileCopyrightText: 2022-2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autogain-hold.h"
#include "custom-names.h"
#include "device-port-names.h"
#include "glow.h"
#include "gtkhelper.h"
#include "hardware.h"
#include "iface-mixer.h"
#include "presets.h"
#include "routing-lines.h"
#include "stereo-link.h"
#include "stringhelper.h"
#include "tooltips.h"
#include "widget-boolean.h"
#include "widget-switch-dial.h"
#include "widget-drop-down.h"
#include "widget-dual.h"
#include "widget-gain.h"
#include "widget-input-select.h"
#include "widget-sample-rate.h"
#include "window-autogain.h"
#include "window-configuration.h"
#include "window-dsp.h"
#include "window-geometry.h"
#include "window-helper.h"
#include "window-levels.h"
#include "window-mixer.h"
#include "window-routing.h"

// find the routing sink for a hardware output by port number
static struct routing_snk *get_output_r_snk(
  struct alsa_card *card,
  int               port_num
) {
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *r_snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    struct alsa_elem *elem = r_snk->elem;

    if (elem->port_category != PC_HW)
      continue;

    if (elem->lr_num == port_num)
      return r_snk;
  }
  return NULL;
}

static void add_clock_source_control(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  GPtrArray *elems = card->elems;

  struct alsa_elem *clock_source = get_elem_by_prefix(elems, "Clock Source");

  if (!clock_source) {
    clock_source = get_elem_by_substr(elems, "Sync Clock Source");

    if (!clock_source)
      return;
  }

  GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_tooltip_text(
    b,
    "Clock Source selects where the interface receives its digital "
    "clock from. If you aren’t using S/PDIF or ADAT inputs, set this "
    "to Internal."
  );
  gtk_box_append(GTK_BOX(global_controls), b);

  GtkWidget *l = gtk_label_new("Clock Source");
  GtkWidget *w = make_drop_down_alsa_elem(clock_source, NULL);
  gtk_widget_add_css_class(w, "clock-source");
  gtk_widget_add_css_class(w, "fixed");

  gtk_box_append(GTK_BOX(b), l);
  gtk_box_append(GTK_BOX(b), w);
}

static void add_sync_status_control(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  GPtrArray *elems = card->elems;

  struct alsa_elem *sync_status = get_elem_by_name(elems, "Sync Status");

  if (!sync_status) {
    sync_status = get_elem_by_name(elems, "Sample Clock Sync Status");
    if (!sync_status)
      return;
  }

  GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  if (get_elem_by_prefix(elems, "Clock Source")) {
    gtk_widget_set_tooltip_text(
      b,
      "Sync Status indicates if the interface is locked to a valid "
      "digital clock. If you aren’t using S/PDIF or ADAT inputs and "
      "the Sync Status is Unlocked, change the Clock Source to "
      "Internal."
    );
  } else {
    gtk_widget_set_tooltip_text(
      b,
      "Sync Status indicates if the interface is locked to a valid "
      "digital clock. Since the Clock Source is fixed to internal on "
      "this interface, this should stay locked."
    );
  }
  gtk_box_append(GTK_BOX(global_controls), b);

  GtkWidget *l = gtk_label_new("Sync Status");
  gtk_box_append(GTK_BOX(b), l);
  GtkWidget *w = make_boolean_alsa_elem(sync_status, "Unlocked", "Locked");
  gtk_widget_add_css_class(w, "sync-status");
  gtk_widget_add_css_class(w, "fixed");
  gtk_box_append(GTK_BOX(b), w);
}

static void add_power_status_control(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  GPtrArray *elems = card->elems;

  struct alsa_elem *power_status = get_elem_by_name(
    elems, "Power Status Card Enum"
  );

  if (!power_status)
    return;

  GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_tooltip_text(
    b,
    "Power indicates if the interface is being powered by the USB "
    "bus, an external power supply, or if there is insufficient power "
    "available and the interface has shut down."
  );
  gtk_box_append(GTK_BOX(global_controls), b);

  GtkWidget *l = gtk_label_new("Power");
  gtk_box_append(GTK_BOX(b), l);
  GtkWidget *w = make_drop_down_alsa_elem(power_status, NULL);
  gtk_widget_add_css_class(w, "power-status");
  gtk_widget_add_css_class(w, "fixed");
  gtk_box_append(GTK_BOX(b), w);
}

static void add_sample_rate_control(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  GtkWidget *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_tooltip_text(
    b,
    "The Sample Rate cannot be changed here because it is set by the "
    "application which is using the interface, usually a sound "
    "server like PulseAudio, JACK, or PipeWire. If this shows N/A, "
    "no application is currently using the interface.\n\n"
    "Note that not all features are available on all interfaces at "
    "sample rates above 48kHz. Please refer to the user guide for "
    "your interface for more information."
  );
  gtk_box_append(GTK_BOX(global_controls), b);

  GtkWidget *l = gtk_label_new("Sample Rate");
  gtk_box_append(GTK_BOX(b), l);
  GtkWidget *w = make_sample_rate_widget(card);
  gtk_widget_add_css_class(w, "sample-rate");
  gtk_box_append(GTK_BOX(b), w);
}

static void add_speaker_switching_controls_enum(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  GPtrArray *elems = card->elems;

  struct alsa_elem *speaker_switching = get_elem_by_name(
    elems, "Speaker Switching Playback Enum"
  );

  if (!speaker_switching)
    return;

  GtkWidget *w = make_dual_boolean_alsa_elems(
    speaker_switching,
    "Speaker Switching",
    "Off", "On", "speaker-switching-enable",
    "Main", "Alt", "speaker-switching-alt"
  );

  gtk_widget_set_tooltip_text(
    w,
    "Speaker Switching lets you swap between two pairs of "
    "monitoring speakers very easily."
  );

  gtk_box_append(GTK_BOX(global_controls), w);
}

static void add_speaker_switching_controls_switches(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  GPtrArray *elems = card->elems;

  struct alsa_elem *enable = get_elem_by_name(
    elems, "Speaker Switching Playback Switch"
  );
  struct alsa_elem *alt = get_elem_by_name(
    elems, "Speaker Switching Alt Playback Switch"
  );

  if (!enable || !alt)
    return;

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_tooltip_text(
    box,
    "Speaker Switching lets you swap between two pairs of "
    "monitoring speakers very easily."
  );

  GtkWidget *l = gtk_label_new("Speaker Switching");
  GtkWidget *w1 = make_boolean_alsa_elem(enable, "Off", "On");
  GtkWidget *w2 = make_boolean_alsa_elem(alt, "Main", "Alt");

  gtk_widget_add_css_class(w1, "speaker-switching-enable");
  gtk_widget_add_css_class(w2, "speaker-switching-alt");

  gtk_box_append(GTK_BOX(box), l);
  gtk_box_append(GTK_BOX(box), w1);
  gtk_box_append(GTK_BOX(box), w2);
  gtk_box_append(GTK_BOX(global_controls), box);
}

// Callback to update speaker switching button sensitivity
static void update_speaker_switching_sensitivity(
  struct alsa_elem *elem,
  void             *data
) {
  GtkWidget *button = data;
  struct alsa_card *card = elem->card;

  gtk_widget_set_sensitive(button, monitor_group_has_outputs(card, 1));
}

// Gen 4: Main/Alt selector only (speaker switching is implicit)
static void add_speaker_switching_controls_gen4(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  GPtrArray *elems = card->elems;

  // Check if this is Gen 4 (has Main/Alt Group controls)
  struct alsa_elem *main_group = get_elem_by_prefix(
    elems, "Main Group Output"
  );
  if (!main_group)
    return;

  struct alsa_elem *alt = get_elem_by_name(
    elems, "Speaker Switching Alt Playback Switch"
  );
  if (!alt)
    return;

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_tooltip_text(
    box,
    "Monitor Group lets you swap between Main and Alt monitor groups. "
    "Configure which outputs belong to each group in the Configuration window."
  );

  GtkWidget *l = gtk_label_new("Monitor Group");
  GtkWidget *w = make_boolean_alsa_elem(alt, "Main", "Alt");

  gtk_widget_add_css_class(w, "speaker-switching-alt");

  // Set initial sensitivity based on whether any Alt output is enabled
  gtk_widget_set_sensitive(w, monitor_group_has_outputs(card, 1));

  // Register callbacks on Alt Group enable switches to update sensitivity
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    struct alsa_elem *alt_out = snk->monitor_groups[1].enable;

    if (alt_out)
      alsa_elem_add_callback(
        alt_out, update_speaker_switching_sensitivity, w, NULL
      );
  }

  gtk_box_append(GTK_BOX(box), l);
  gtk_box_append(GTK_BOX(box), w);
  gtk_box_append(GTK_BOX(global_controls), box);
}

// Monitor group selector for devices with more than two monitor
// groups (e.g. ISA C8X Main/Alt 1/Alt 2), exposed as an enum control.
// As with the two-group Main/Alt toggle, it is only sensitive when an
// Alt group has outputs to switch to.

static int mg_select_value_to_row(int value, void *user_data) {
  return value;
}

static int mg_select_row_to_value(
  int   row,
  int   current_value,
  void *user_data
) {
  return row;
}

static char *mg_select_button_label(int value, void *user_data) {
  struct alsa_card *card = user_data;

  return g_strdup(card->monitor_group_names[value]);
}

// Main always available; an Alt group is only reachable once it has
// outputs assigned.
static int mg_select_row_enabled(int row, void *user_data) {
  struct alsa_card *card = user_data;

  return row == 0 || monitor_group_has_outputs(card, row);
}

static int mg_select_sensitive(int value, void *user_data) {
  struct alsa_card *card = user_data;

  for (int g = 1; g < card->monitor_group_count; g++)
    if (monitor_group_has_outputs(card, g))
      return 1;
  return 0;
}

static const struct drop_down_value_ops mg_select_ops = {
  .value_to_row = mg_select_value_to_row,
  .row_to_value = mg_select_row_to_value,
  .button_label = mg_select_button_label,
  .sensitive    = mg_select_sensitive,
  .row_enabled  = mg_select_row_enabled
};

// the selectable groups depend on the group enables, so poke the
// selector's callbacks when they change
static void mg_select_outputs_changed(
  struct alsa_elem *elem,
  void             *private
) {
  struct alsa_card *card = private;

  alsa_elem_change(card->monitor_group_select_elem);
}

static void add_monitor_group_enum_control(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  struct alsa_elem *elem = get_elem_by_name(
    card->elems, "Monitor Group Playback Enum"
  );

  if (!elem)
    return;

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_tooltip_text(
    box,
    "Monitor Group lets you swap between the Main, Alt 1, and Alt 2 "
    "monitor groups. Click to cycle through the groups, or use the "
    "arrow to select one. Configure which outputs belong to each group "
    "in the Configuration window."
  );

  GtkWidget *l = gtk_label_new("Monitor Group");

  GtkStringList *model = gtk_string_list_new(NULL);
  for (int g = 0; g < card->monitor_group_count; g++)
    gtk_string_list_append(model, card->monitor_group_names[g]);

  GtkWidget *w = make_cycle_drop_down_alsa_elem(
    elem, model, &mg_select_ops, card, NULL
  );

  // refresh the selectable groups when any group enable changes
  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );

    for (int g = 0; g < card->monitor_group_count; g++)
      if (snk->monitor_groups[g].enable)
        alsa_elem_add_callback(
          snk->monitor_groups[g].enable,
          mg_select_outputs_changed, card, NULL
        );
  }

  gtk_box_append(GTK_BOX(box), l);
  gtk_box_append(GTK_BOX(box), w);
  gtk_box_append(GTK_BOX(global_controls), box);
}

// Structure to track output volume widget and its monitor group elements
struct output_volume_group_data {
  GtkWidget          *widget;
  struct routing_snk *snk;
};

// Check if output is in a monitor group and update widget sensitivity
static void update_output_volume_sensitivity(
  struct alsa_elem *elem,
  void             *data
) {
  struct output_volume_group_data *d = data;

  gtk_widget_set_sensitive(d->widget, !snk_in_any_monitor_group(d->snk));
}

// Free output volume group data
static void free_output_volume_group_data(void *data) {
  g_free(data);
}

// Get the routing sink for an analogue output by lr_num
static struct routing_snk *get_analogue_output_snk(
  struct alsa_card *card,
  int               lr_num
) {
  if (!card->routing_snks)
    return NULL;

  for (int i = 0; i < card->routing_snks->len; i++) {
    struct routing_snk *snk = &g_array_index(
      card->routing_snks, struct routing_snk, i
    );
    if (snk->elem->port_category == PC_HW &&
        snk->elem->hw_type == HW_TYPE_ANALOGUE &&
        snk->elem->lr_num == lr_num) {
      return snk;
    }
  }
  return NULL;
}

// Get the analogue output number of the left channel of headphones
// headphones_num, or 0 if no output has that name
static int get_headphones_output(struct alsa_card *card, int headphones_num) {
  char name[32];
  snprintf(name, sizeof(name), "Headphones %d (L)", headphones_num);

  for (int port = 0; ; port++) {
    const char *port_name = get_device_port_name(
      card->pid, PC_HW, HW_TYPE_ANALOGUE, 1, port
    );

    if (!port_name)
      return 0;
    if (strcmp(port_name, name) == 0)
      return port + 1;
  }
}

// Set up monitor group sensitivity tracking for an output volume widget
static void setup_output_volume_monitor_group(
  struct alsa_card *card,
  GtkWidget        *widget,
  int               lr_num
) {
  struct routing_snk *snk = get_analogue_output_snk(card, lr_num);
  if (!snk)
    return;

  // Check if this output has monitor group controls
  if (!snk->monitor_groups[0].enable)
    return;

  // Create tracking data
  struct output_volume_group_data *data =
    g_malloc(sizeof(struct output_volume_group_data));
  data->widget = widget;
  data->snk = snk;

  // Register callbacks on the group enable switches
  for (int g = 0; g < card->monitor_group_count; g++)
    if (snk->monitor_groups[g].enable)
      alsa_elem_add_callback(
        snk->monitor_groups[g].enable,
        update_output_volume_sensitivity,
        data,
        NULL
      );

  // Attach cleanup to the widget
  g_object_weak_ref(
    G_OBJECT(widget),
    (GWeakNotify)free_output_volume_group_data,
    data
  );

  // Set initial sensitivity
  gtk_widget_set_sensitive(widget, !snk_in_any_monitor_group(snk));
}

// An output's mute button, which while the output is in a monitor
// group gives way to a disabled indicator of the master mute and dim
// that then apply to it
enum { MASTER_STATE_NORMAL, MASTER_STATE_DIM, MASTER_STATE_MUTE };

struct output_mute_data {
  GtkWidget          *stack;
  GtkWidget          *button;
  GtkWidget          *indicator;
  GtkWidget          *icons[3];
  struct alsa_card   *card;
  struct routing_snk *snk;
  struct alsa_elem   *master_mute;
  struct alsa_elem   *master_dim;
};

// Call func for each element whose change can change the mute display
static void for_each_output_mute_elem(
  struct output_mute_data *d,
  void (*func)(struct alsa_elem *elem, struct output_mute_data *d)
) {
  for (int g = 0; g < d->card->monitor_group_count; g++)
    if (d->snk->monitor_groups[g].enable)
      func(d->snk->monitor_groups[g].enable, d);
  if (d->master_mute)
    func(d->master_mute, d);
  if (d->master_dim)
    func(d->master_dim, d);
}

static void update_output_mute(struct alsa_elem *elem, void *private) {
  struct output_mute_data *d = private;

  if (!snk_in_any_monitor_group(d->snk)) {
    gtk_stack_set_visible_child(GTK_STACK(d->stack), d->button);
    return;
  }

  int state =
    d->master_mute && alsa_get_elem_value(d->master_mute)
      ? MASTER_STATE_MUTE
    : d->master_dim && alsa_get_elem_value(d->master_dim)
      ? MASTER_STATE_DIM
    : MASTER_STATE_NORMAL;

  gtk_button_set_child(GTK_BUTTON(d->indicator), d->icons[state]);
  gtk_widget_remove_css_class(d->indicator, "mute");
  gtk_widget_remove_css_class(d->indicator, "dim");
  gtk_widget_add_css_class(
    d->indicator, state == MASTER_STATE_DIM ? "dim" : "mute"
  );
  gtk_toggle_button_set_active(
    GTK_TOGGLE_BUTTON(d->indicator), state != MASTER_STATE_NORMAL
  );
  gtk_stack_set_visible_child(GTK_STACK(d->stack), d->indicator);
}

static void add_output_mute_callback(
  struct alsa_elem        *elem,
  struct output_mute_data *d
) {
  alsa_elem_add_callback(elem, update_output_mute, d, NULL);
}

static void remove_output_mute_callback(
  struct alsa_elem        *elem,
  struct output_mute_data *d
) {
  alsa_elem_remove_callbacks_by_data(elem, d);
}

static void free_output_mute_data(void *private) {
  struct output_mute_data *d = private;

  for_each_output_mute_elem(d, remove_output_mute_callback);
  for (int i = 0; i < 3; i++)
    g_object_unref(d->icons[i]);
  g_free(d);
}

static GtkWidget *load_icon(const char *name) {
  char *path = g_strdup_printf(
    "/vu/b4/alsa-scarlett-gui/icons/%s.svg", name
  );
  GtkWidget *icon = gtk_image_new_from_resource(path);

  g_free(path);
  gtk_widget_set_align(icon, GTK_ALIGN_CENTER, GTK_ALIGN_CENTER);
  g_object_ref_sink(icon);
  return icon;
}

static const char *line_mute_icons[] = {
  "audio-volume-high", "audio-volume-low", "audio-volume-muted"
};

static const char *headphones_mute_icons[] = {
  "audio-headphones-symbolic",
  "audio-headphones-symbolic",
  "audio-headphones-symbolic"
};

// Wrap an output's mute button so that it follows the master mute and
// dim while output lr_num is in a monitor group; icon_names are for
// the normal, dimmed, and muted states. Returns the button unchanged
// when the output has no monitor group controls.
static GtkWidget *make_output_mute(
  struct alsa_card *card,
  GtkWidget        *button,
  int               lr_num,
  const char      **icon_names
) {
  struct routing_snk *snk = get_analogue_output_snk(card, lr_num);

  if (!snk || !snk->monitor_groups[0].enable)
    return button;

  struct output_mute_data *d = g_malloc0(sizeof(*d));

  d->card = card;
  d->snk = snk;
  d->button = button;
  d->master_mute = get_elem_by_name(card->elems, "Mute Playback Switch");
  d->master_dim = get_elem_by_name(card->elems, "Dim Playback Switch");
  for (int i = 0; i < 3; i++)
    d->icons[i] = load_icon(icon_names[i]);

  d->indicator = gtk_toggle_button_new();
  gtk_widget_set_sensitive(d->indicator, FALSE);
  gtk_widget_set_tooltip_text(
    d->indicator, "Set by the master mute and dim of the monitor group"
  );

  d->stack = gtk_stack_new();
  gtk_stack_add_child(GTK_STACK(d->stack), button);
  gtk_stack_add_child(GTK_STACK(d->stack), d->indicator);

  for_each_output_mute_elem(d, add_output_mute_callback);

  g_object_weak_ref(
    G_OBJECT(d->stack), (GWeakNotify)free_output_mute_data, d
  );

  update_output_mute(NULL, d);
  return d->stack;
}

// Structure to track an output's tag widget
struct output_tag_label_data {
  GtkWidget          *label;
  struct routing_snk *snk;
  struct alsa_elem   *hw_vol_elem;
};

// A speaker switching device has no monitor group controls: its groups
// are fixed, analogue outputs 1-2 Main and 3-4 Alt, and the selection
// is the speaker switching setting (Off, Main, Alt).
static const char *speaker_switching_group_names[] = { "Main", "Alt" };

static struct alsa_elem *get_speaker_switching_elem(struct alsa_card *card) {
  return get_elem_by_name(card->elems, "Speaker Switching Playback Enum");
}

// The fixed group of an output, or -1 if it isn't in one
static int snk_speaker_switching_group(struct routing_snk *snk) {
  int lr_num = snk->elem->lr_num;

  if (lr_num <= 2)
    return 0;
  if (lr_num <= 4)
    return 1;

  return -1;
}

// Update an output's tag: the monitor group it belongs to, or "HW"
// while the hardware volume knob controls it. A grouped output on a
// speaker switching device is locked to the knob by the device, so the
// group name is the more informative of the two.
static void update_output_tag_label(
  struct alsa_elem *elem,
  void             *data
) {
  struct output_tag_label_data *d = data;
  struct alsa_card *card = d->snk->elem->card;
  struct alsa_elem *speaker_switching = get_speaker_switching_elem(card);

  int         group = -1;   // group the label names
  int         selected = 0; // whether that group is the selected one
  const char *name = NULL;

  if (speaker_switching) {
    int selection = alsa_get_elem_value(speaker_switching);

    group = snk_speaker_switching_group(d->snk);

    // While speaker switching is off the outputs aren't being used as
    // switched speaker pairs, so they have no group to report
    if (group >= 0 && selection) {
      selected = selection - 1 == group;
      name = speaker_switching_group_names[group];
    }
  } else if (snk_in_any_monitor_group(d->snk)) {
    int active = get_active_monitor_group(card);

    if (active >= 0 && snk_in_monitor_group(d->snk, active)) {
      group = active;
      selected = 1;
    } else {
      for (group = 0; !snk_in_monitor_group(d->snk, group); group++)
        ;
    }
    name = card->monitor_group_names[group];
  }

  // The selected group: green for Main, red for the Alt groups;
  // membership of an unselected group: dimmed
  const char *colour = !selected ? "#666" : group == 0 ? "#4a4" : "#f66";

  // Ungrouped, so report whether the knob controls this output
  if (!name && d->hw_vol_elem && alsa_get_elem_value(d->hw_vol_elem)) {
    name = "HW";
    colour = "#ffc000";
  }

  if (!name) {
    gtk_label_set_text(GTK_LABEL(d->label), "");
    return;
  }

  char *markup = g_markup_printf_escaped(
    "<span color=\"%s\">%s</span>", colour, name
  );
  gtk_label_set_markup(GTK_LABEL(d->label), markup);
  g_free(markup);
}

// Free output tag data
static void free_output_tag_label_data(void *data) {
  g_free(data);
}

// Create the tag shown above an output's volume dial
static GtkWidget *create_output_tag_label(
  struct alsa_card *card,
  int               lr_num
) {
  struct routing_snk *snk = get_analogue_output_snk(card, lr_num);
  if (!snk)
    return NULL;

  struct alsa_elem *speaker_switching = get_speaker_switching_elem(card);
  struct alsa_elem *hw_vol_elem = get_hw_vol_elem(card, lr_num);

  // Check if this output has anything to report
  int in_group = speaker_switching
    ? snk_speaker_switching_group(snk) >= 0
    : !!snk->monitor_groups[0].enable;

  if (!in_group && !hw_vol_elem)
    return NULL;

  /* Create label widget. The smaller font is a style rather than
   * markup so that the label is the same height whether or not it has
   * anything to say; markup would leave an empty one a full-size line
   * tall.
   */
  GtkWidget *label = gtk_label_new("");
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class(label, "output-tag");

  // Create tracking data
  struct output_tag_label_data *data =
    g_malloc(sizeof(struct output_tag_label_data));
  data->label = label;
  data->snk = snk;
  data->hw_vol_elem = hw_vol_elem;

  // Register callbacks on all relevant controls
  if (speaker_switching)
    alsa_elem_add_callback(
      speaker_switching, update_output_tag_label, data, NULL
    );

  if (hw_vol_elem)
    alsa_elem_add_callback(
      hw_vol_elem, update_output_tag_label, data, NULL
    );

  for (int g = 0; g < card->monitor_group_count; g++)
    if (snk->monitor_groups[g].enable)
      alsa_elem_add_callback(
        snk->monitor_groups[g].enable,
        update_output_tag_label, data, NULL
      );
  if (card->monitor_group_select_elem)
    alsa_elem_add_callback(
      card->monitor_group_select_elem,
      update_output_tag_label, data, NULL
    );

  // Attach cleanup to the widget
  g_object_weak_ref(
    G_OBJECT(label),
    (GWeakNotify)free_output_tag_label_data,
    data
  );

  // Set initial state
  update_output_tag_label(NULL, data);

  return label;
}

static void add_talkback_controls_enum(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  GPtrArray *elems = card->elems;

  struct alsa_elem *talkback = get_elem_by_name(
    elems, "Talkback Playback Enum"
  );

  if (!talkback)
    return;

  GtkWidget *w = make_dual_boolean_alsa_elems(
    talkback,
    "Talkback",
    "Disabled", "Enabled", "talkback-enable",
    "Off", "On", "talk"
  );

  gtk_widget_set_tooltip_text(
    w,
    "Talkback lets you add another channel (usually the talkback "
    "mic) to a mix with a button push, usually to talk to "
    "musicians, and without using an additional mic channel."
  );

  gtk_box_append(GTK_BOX(global_controls), w);
}

static void add_talkback_controls_switches(
  struct alsa_card *card,
  GtkWidget        *global_controls
) {
  GPtrArray *elems = card->elems;

  struct alsa_elem *enable = get_elem_by_name(
    elems, "Talkback Enable Playback Switch"
  );
  struct alsa_elem *talk = get_elem_by_name(
    elems, "Talk Playback Switch"
  );

  if (!enable || !talk)
    return;

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_tooltip_text(
    box,
    "Talkback lets you add another channel (usually the talkback "
    "mic) to a mix with a button push, usually to talk to "
    "musicians, and without using an additional mic channel."
  );

  GtkWidget *l = gtk_label_new("Talkback");
  GtkWidget *w1 = make_boolean_alsa_elem(enable, "Disabled", "Enabled");
  GtkWidget *w2 = make_boolean_alsa_elem(talk, "Talk", "Talk");

  gtk_widget_add_css_class(w1, "talkback-enable");
  gtk_widget_add_css_class(w2, "talk");

  gtk_box_append(GTK_BOX(box), l);
  gtk_box_append(GTK_BOX(box), w1);
  gtk_box_append(GTK_BOX(box), w2);
  gtk_box_append(GTK_BOX(global_controls), box);
}

static GtkWidget *create_global_box(GtkWidget *grid, int *x, int orient) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_vexpand(box, TRUE);

  GtkWidget *label = gtk_label_new("Global");
  gtk_widget_add_css_class(label, "controls-label");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  GtkWidget *controls = gtk_box_new(orient, 15);
  gtk_widget_add_css_class(controls, "controls-content");
  gtk_widget_set_vexpand(controls, TRUE);

  gtk_box_append(GTK_BOX(box), label);
  gtk_box_append(GTK_BOX(box), controls);

  widget_doc_register_group(box, "global", NULL, "Global");

  gtk_grid_attach(GTK_GRID(grid), box, *x, 0, 1, 1);

  (*x)++;

  return controls;
}

/* 4th Gen Solo Mix switch */
static void create_input_select_control(
  GPtrArray *elems,
  GtkWidget *input_grid,
  int       *current_row
) {
  struct alsa_elem *elem = get_elem_by_name(elems, "PCM Input Capture Switch");

  if (!elem)
    return;

  GtkWidget *w = make_boolean_alsa_elem(elem, "Mix", "Mix");
  gtk_widget_add_css_class(w, "pcm-input-mix");
  gtk_widget_set_tooltip_text(
    w,
    "Enabling Input Mix selects Mix E/F as the input source for "
    "the PCM 1/2 Inputs rather than the DSP 1/2 Inputs. This is "
    "useful to get a mono mix of both input channels."
  );
  gtk_grid_attach(GTK_GRID(input_grid), w, 0, *current_row, 2, 1);

  (*current_row)++;
}

// Input type (Mic/Line/Inst) values for devices with per-channel
// input selection (ISA C8X); several controls are only applicable to
// particular input types.
enum {
  INPUT_TYPE_MIC  = 0,
  INPUT_TYPE_LINE = 1,
  INPUT_TYPE_INST = 2
};

#define INPUT_TYPE_MASK_MIC      (1 << INPUT_TYPE_MIC)
#define INPUT_TYPE_MASK_MIC_INST \
  ((1 << INPUT_TYPE_MIC) | (1 << INPUT_TYPE_INST))

// find the channel's input type control, if the device has one
static struct alsa_elem *get_input_type_elem(struct alsa_elem *elem) {
  char name[64];

  snprintf(
    name, sizeof(name),
    "Line In %d Input Type Capture Enum",
    get_num_from_string(elem->name)
  );
  return get_elem_by_name(elem->card->elems, name);
}

// Grey out an input control while the channel's input type doesn't
// support it, or while autogain has the channel. type_mask has bit
// (1 << type) set for each input type value the control is available
// for, and is 0 for a control that every input type has.
struct input_control_gate {
  struct alsa_elem *input_type_elem;
  struct alsa_elem *autogain_elem;
  struct alsa_elem *elem;
  GtkWidget        *widget;
  unsigned int      type_mask;
};

static void input_control_gate_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct input_control_gate *gate = private;
  int available = 1;

  if (gate->input_type_elem) {
    int type = alsa_get_elem_value(gate->input_type_elem);

    available = !!(gate->type_mask & (1u << type));
  }

  if (gate->autogain_elem && alsa_get_elem_value(gate->autogain_elem))
    available = 0;

  gtk_widget_set_sensitive(gate->widget, available);
}

static void input_control_gate_destroy(
  GtkWidget *widget,
  gpointer   user_data
) {
  struct input_control_gate *gate = user_data;

  if (gate->input_type_elem)
    alsa_elem_remove_callbacks_by_data(gate->input_type_elem, gate);
  if (gate->autogain_elem)
    alsa_elem_remove_callbacks_by_data(gate->autogain_elem, gate);
  alsa_elem_remove_callbacks_by_data(gate->elem, gate);
  g_free(gate);
}

static void gate_input_control(
  GtkWidget        *widget,
  struct alsa_elem *elem,
  unsigned int      type_mask
) {
  struct alsa_elem *input_type_elem =
    type_mask ? get_input_type_elem(elem) : NULL;
  struct alsa_elem *autogain_elem = get_autogain_hold_elem(
    elem->card, get_num_from_string(elem->name)
  );

  // autogain itself stays live, so that a run can be cancelled
  if (autogain_elem == elem)
    autogain_elem = NULL;

  if (!input_type_elem && !autogain_elem)
    return;

  struct input_control_gate *gate = g_malloc0(sizeof(*gate));
  gate->input_type_elem = input_type_elem;
  gate->autogain_elem = autogain_elem;
  gate->elem = elem;
  gate->widget = widget;
  gate->type_mask = type_mask;

  // also registered on the control's own element, after the widget's
  // own callback, so the gate wins when the widget re-asserts
  // sensitivity from writability
  if (input_type_elem)
    alsa_elem_add_callback(
      input_type_elem, input_control_gate_updated, gate, NULL
    );
  if (autogain_elem)
    alsa_elem_add_callback(
      autogain_elem, input_control_gate_updated, gate, NULL
    );
  alsa_elem_add_callback(elem, input_control_gate_updated, gate, NULL);
  g_signal_connect(
    widget, "destroy", G_CALLBACK(input_control_gate_destroy), gate
  );

  input_control_gate_updated(elem, gate);
}

static void create_input_gain_control(
  struct alsa_card *card,
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_gain_alsa_elem(elem, 0, WIDGET_GAIN_TAPER_LINEAR, 1, TRUE);
  gate_input_control(w, elem, INPUT_TYPE_MASK_MIC_INST);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);

  // find the routing source for this input's port number
  // input gains correspond to hardware inputs (PC_HW)
  struct routing_src *r_src = NULL;
  for (int i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    if (src->port_category == PC_HW && src->port_num == elem->lr_num - 1) {
      r_src = src;
      break;
    }
  }

  // store widget in card's list
  struct input_gain_widget *ig = g_malloc(sizeof(struct input_gain_widget));
  ig->widget = w;
  ig->port_num = elem->lr_num;
  ig->r_src = r_src;
  card->input_gain_widgets = g_list_append(card->input_gain_widgets, ig);
}

static void create_input_autogain_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Autogain", NULL);
  gtk_widget_add_css_class(w, "autogain");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(
    w,
    "Autogain will listen to the input signal for 10 seconds and "
    "automatically set the gain of the input channel to get the "
    "best signal level. Progress and the result are shown in the "
    "Autogain window."
  );
  gate_input_control(w, elem, INPUT_TYPE_MASK_MIC_INST);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_safe_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Safe", NULL);
  gtk_widget_add_css_class(w, "safe");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(
    w,
    "Enabling Safe Mode prevents the input from clipping by "
    "automatically reducing the gain if the signal is too hot."
  );

  gate_input_control(w, elem, 0);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_level_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Inst", NULL);
  gtk_widget_add_css_class(w, "inst");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(w, level_descr);

  gate_input_control(w, elem, 0);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_input_type_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_drop_down_alsa_elem(elem, NULL);
  gtk_widget_add_css_class(w, "input-type");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(
    w,
    "Select which input (Mic, Line, or Inst) to use for this channel."
  );

  gate_input_control(w, elem, 0);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

// The impedance control has five values which are filtered and
// relabelled according to the channel's input type, matching the
// hardware: Mic offers Low(0)/110(1)/Med(2)/High(3), Inst offers
// Low(4)/High(1), and Line has no impedance selection. The firmware
// does not reject invalid combinations, so the GUI only offers the
// valid ones.

static const char *impedance_value_names[] = {
  "Low", "110", "Med", "High", "Inst Low"
};

static const int mic_impedance_values[] = { 0, 1, 2, 3 };
static const int inst_impedance_values[] = { 4, 1 };

struct impedance_ctx {
  struct alsa_elem *elem;
  struct alsa_elem *input_type_elem;
};

static void impedance_mode_values(
  struct impedance_ctx  *ctx,
  const int            **values,
  int                   *count
) {
  if (alsa_get_elem_value(ctx->input_type_elem) == INPUT_TYPE_INST) {
    *values = inst_impedance_values;
    *count = G_N_ELEMENTS(inst_impedance_values);
  } else {
    *values = mic_impedance_values;
    *count = G_N_ELEMENTS(mic_impedance_values);
  }
}

static const char *impedance_value_label(int mode, int value) {
  if (mode == INPUT_TYPE_INST) {
    if (value == 4)
      return "Low";
    if (value == 1)
      return "High";
  }
  return impedance_value_names[value];
}

static int impedance_value_to_row(int value, void *user_data) {
  struct impedance_ctx *ctx = user_data;

  const int *values;
  int count;
  impedance_mode_values(ctx, &values, &count);

  for (int i = 0; i < count; i++)
    if (values[i] == value)
      return i;

  // the extra row refresh_model appends for a value that is invalid
  // for the current input type
  return count;
}

static int impedance_row_to_value(
  int   row,
  int   current_value,
  void *user_data
) {
  struct impedance_ctx *ctx = user_data;

  const int *values;
  int count;
  impedance_mode_values(ctx, &values, &count);

  // the invalid-value row is a no-op
  if (row >= count)
    return current_value;

  return values[row];
}

static char *impedance_button_label(int value, void *user_data) {
  struct impedance_ctx *ctx = user_data;
  int mode = alsa_get_elem_value(ctx->input_type_elem);

  return g_strdup(impedance_value_label(mode, value));
}

static void impedance_refresh_model(
  GtkStringList *model,
  int            value,
  void          *user_data
) {
  struct impedance_ctx *ctx = user_data;
  int mode = alsa_get_elem_value(ctx->input_type_elem);

  const int *values;
  int count;
  impedance_mode_values(ctx, &values, &count);

  gtk_string_list_splice(
    model, 0, g_list_model_get_n_items(G_LIST_MODEL(model)), NULL
  );
  for (int i = 0; i < count; i++)
    gtk_string_list_append(model, impedance_value_label(mode, values[i]));

  // show a value that is invalid for the current input type rather
  // than lie about the hardware state
  if (impedance_value_to_row(value, ctx) == count)
    gtk_string_list_append(model, impedance_value_names[value]);
}

static int impedance_sensitive(int value, void *user_data) {
  struct impedance_ctx *ctx = user_data;

  return alsa_get_elem_value(ctx->input_type_elem) != INPUT_TYPE_LINE;
}

static const struct drop_down_value_ops impedance_ops = {
  .value_to_row  = impedance_value_to_row,
  .row_to_value  = impedance_row_to_value,
  .button_label  = impedance_button_label,
  .refresh_model = impedance_refresh_model,
  .sensitive     = impedance_sensitive
};

// the impedance presentation depends on the input type, so poke the
// impedance element's callbacks when the input type changes
static void impedance_input_type_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct impedance_ctx *ctx = private;

  alsa_elem_change(ctx->elem);
}

static void impedance_destroy(GtkWidget *widget, gpointer user_data) {
  struct impedance_ctx *ctx = user_data;

  alsa_elem_remove_callbacks_by_data(ctx->input_type_elem, ctx);
  g_free(ctx);
}

static void create_input_impedance_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  struct alsa_elem *input_type_elem = get_input_type_elem(elem);

  GtkWidget *w;

  if (input_type_elem) {
    struct impedance_ctx *ctx = g_malloc0(sizeof(*ctx));
    ctx->elem = elem;
    ctx->input_type_elem = input_type_elem;

    w = make_value_mapped_drop_down_alsa_elem(
      elem, gtk_string_list_new(NULL), &impedance_ops, ctx, NULL
    );
    alsa_elem_add_callback(
      input_type_elem, impedance_input_type_updated, ctx, NULL
    );
    g_signal_connect(w, "destroy", G_CALLBACK(impedance_destroy), ctx);
  } else {
    w = make_drop_down_alsa_elem(elem, NULL);
  }

  gtk_widget_add_css_class(w, "impedance");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(
    w,
    "Select the input impedance for the Mic or Inst input."
  );

  gate_input_control(w, elem, 0);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_air_switch_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Air", NULL);
  gtk_widget_add_css_class(w, "air");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(w, air_descr);

  gate_input_control(w, elem, 0);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_air_enum_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w;
  const char *descr = air_descr;

  // a two-state air control presents as a toggle button labelled with
  // the "on" state's name (e.g. "Air", "430"); these are the ISA C8X
  // air modes, which have their own descriptions
  if (alsa_get_item_count(elem) == 2) {
    const char *on_name = alsa_get_item_name(elem, 1);

    w = make_boolean_alsa_elem(elem, on_name, NULL);
    descr = strcmp(on_name, "430") ? air_isa_descr : air_430_descr;
  } else {
    w = make_drop_down_alsa_elem(elem, "Air");
  }
  gtk_widget_add_css_class(w, "air");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(w, descr);
  gate_input_control(w, elem, INPUT_TYPE_MASK_MIC_INST);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

// create a switch button for the given element, paired with a popup
// level dial if the channel has a matching level control
static GtkWidget *make_input_switch_with_level(
  struct alsa_elem *elem,
  const char       *label,
  const char       *level_name_format
) {
  char level_name[64];
  snprintf(
    level_name, sizeof(level_name),
    level_name_format, get_num_from_string(elem->name)
  );
  struct alsa_elem *level_elem =
    get_elem_by_name(elem->card->elems, level_name);

  if (level_elem)
    return make_switch_dial_alsa_elem(elem, level_elem, label);
  return make_boolean_alsa_elem(elem, label, NULL);
}

static void create_input_console_switch_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_input_switch_with_level(
    elem, "Console", "Line In %d Console Level Capture Volume"
  );
  gtk_widget_add_css_class(w, "console");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(
    w,
    "Enabling Console engages an analogue soft-clip circuit in the "
    "ISA preamp: clean and transparent at lower levels, adding warm "
    "saturation and low-end punch as you drive the channel harder "
    "with hotter signals or more gain. The variable control dials in "
    "the effect amount."
  );

  gate_input_control(w, elem, 0);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

// Insert and HPF are icon buttons sharing one cell
static void create_input_insert_hpf_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_box_set_homogeneous(GTK_BOX(box), TRUE);

  GtkWidget *insert = make_boolean_alsa_elem(elem, "*insert", NULL);
  gtk_widget_add_css_class(insert, "insert");
  gtk_widget_set_tooltip_text(
    insert,
    "Enable the hardware insert, placing the send/return jacks in "
    "the signal path for this channel."
  );
  gtk_box_append(GTK_BOX(box), insert);

  char hpf_name[64];
  snprintf(
    hpf_name, sizeof(hpf_name),
    "Line In %d HPF Capture Switch",
    get_num_from_string(elem->name)
  );
  struct alsa_elem *hpf_elem =
    get_elem_by_name(elem->card->elems, hpf_name);

  if (hpf_elem) {
    GtkWidget *hpf = make_boolean_alsa_elem(hpf_elem, "*hpf", NULL);
    gtk_widget_add_css_class(hpf, "hpf");
    gtk_widget_set_tooltip_text(
      hpf,
      "Enable the high-pass filter for this channel."
    );
    gate_input_control(hpf, hpf_elem, INPUT_TYPE_MASK_MIC_INST);
    gtk_box_append(GTK_BOX(box), hpf);
  }

  gtk_widget_set_hexpand(box, TRUE);
  gate_input_control(box, elem, 0);

  gtk_grid_attach(GTK_GRID(grid), box, column_num, current_row, 1, 1);
}

static void create_input_drive_switch_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_input_switch_with_level(
    elem, "Drive", "Line In %d Drive Level Capture Volume"
  );
  gtk_widget_add_css_class(w, "drive");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(
    w,
    "Enable harmonic drive for this channel."
  );

  gate_input_control(w, elem, 0);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_dsp_switch_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Enhance", NULL);
  gtk_widget_add_css_class(w, "dsp");
  gtk_widget_set_hexpand(w, TRUE);
//  gtk_widget_set_tooltip_text(w, dsp_descr);

  gate_input_control(w, elem, 0);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_dsp_preset_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_drop_down_alsa_elem(elem, NULL);
  gtk_widget_add_css_class(w, "dsp-preset");
  gtk_widget_set_hexpand(w, TRUE);
//  gtk_widget_set_tooltip_text(w, dsp_descr);

  gate_input_control(w, elem, 0);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_mute_switch_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Mute", NULL);
  gtk_widget_add_css_class(w, "input-mute");
  gtk_widget_set_hexpand(w, TRUE);
//  gtk_widget_set_tooltip_text(w, dsp_descr);

  gate_input_control(w, elem, 0);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_pad_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Pad", NULL);
  gtk_widget_add_css_class(w, "pad");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(
    w,
    "Enabling Pad engages a 10dB attenuator in the channel, giving "
    "you more headroom for very hot signals."
  );

  gate_input_control(w, elem, 0);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, current_row, 1, 1);
}

static void create_input_gain_switch_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "Gain", NULL);
  gtk_widget_add_css_class(w, "gain-switch");
  gtk_widget_set_hexpand(w, TRUE);
  gtk_widget_set_tooltip_text(
    w,
    "Enabling Gain switches from Low gain input (0dBFS = +16dBu)\n"
    "to High gain input (0dBFS = −10dBV, approx −6dBu)."
  );

  // ignore current_row, always put it in the first row
  gate_input_control(w, elem, 0);

  gtk_grid_attach(GTK_GRID(grid), w, column_num, 1, 1, 1);
}

static void create_input_phantom_control(
  struct alsa_elem *elem,
  GtkWidget        *grid,
  int               current_row,
  int               column_num
) {
  GtkWidget *w = make_boolean_alsa_elem(elem, "48V", NULL);
  gtk_widget_add_css_class(w, "phantom");
  gtk_widget_set_tooltip_text(w, phantom_descr);
  gate_input_control(w, elem, INPUT_TYPE_MASK_MIC);

  int from, to;
  get_two_num_from_string(elem->name, &from, &to);
  if (to == -1)
    to = from;

  gtk_grid_attach(GTK_GRID(grid), w, from - 1, current_row, to - from + 1, 1);
}

// add the widgets for one control type to a row and return how many
// were added, leaving the row advance to the caller so that multiple
// control types can share a row
static int add_input_controls_by_type(
  GPtrArray *elems,
  GtkWidget *grid,
  int row,
  char *control,
  void (*create_func)(struct alsa_elem *, GtkWidget *, int, int)
) {
  int count = 0;

  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = g_ptr_array_index(elems, i);

    // if no card entry, it's an empty slot
    if (!elem->card)
      continue;

    if (!strstr(elem->name, control))
      continue;

    // global controls (e.g. "Autogain Max Gain Capture Volume") have
    // no channel number and aren't per-input controls
    int column_num = get_num_from_string(elem->name) - 1;
    if (column_num < 0)
      continue;

    create_func(elem, grid, row, column_num);

    count++;
  }

  return count;
}

static void create_input_controls_by_type(
  GPtrArray *elems,
  GtkWidget *grid,
  int *current_row,
  char *control,
  void (*create_func)(struct alsa_elem *, GtkWidget *, int, int)
) {
  int count = add_input_controls_by_type(
    elems, grid, *current_row, control, create_func
  );

  // Don't increment row for 4th Gen Solo Inst control so Air control
  // goes next to it
  if (!strcmp(control, "Level Capture Enum") && count == 1)
    return;

  if (count)
    (*current_row)++;
}

// variant that passes card to the callback
static void create_input_controls_by_type_with_card(
  struct alsa_card *card,
  GPtrArray *elems,
  GtkWidget *grid,
  int *current_row,
  char *control,
  void (*create_func)(struct alsa_card *, struct alsa_elem *, GtkWidget *, int, int)
) {
  int count = 0;

  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = g_ptr_array_index(elems, i);

    if (!elem->card)
      continue;

    if (!strstr(elem->name, control))
      continue;

    // skip global controls, which have no channel number
    int column_num = get_num_from_string(elem->name) - 1;
    if (column_num < 0)
      continue;

    create_func(card, elem, grid, *current_row, column_num);

    count++;
  }

  if (count)
    (*current_row)++;
}

// whether an input select enum has an item covering the given line
// number (item names are e.g. "Input 3" or "Input 1-2")
static int elem_has_input_item(struct alsa_elem *elem, int line_num) {
  int count = alsa_get_item_count(elem);

  for (int i = 0; i < count; i++) {
    int a, b;
    get_two_num_from_string(alsa_get_item_name(elem, i), &a, &b);

    if ((b == -1 && a == line_num) || (a <= line_num && b >= line_num))
      return 1;
  }

  return 0;
}

// make the channel number widget: a select button bound to whichever
// input select control covers the channel, or a plain label
static GtkWidget *make_channel_select_widget(
  struct alsa_elem *input_select_elem,
  struct alsa_elem *isa_input_select_elem,
  struct alsa_elem *hw_link_elem,
  struct alsa_elem *gui_link_elem,
  int               line_num
) {
  struct alsa_elem *select_elem = NULL;

  if (input_select_elem &&
      elem_has_input_item(input_select_elem, line_num))
    select_elem = input_select_elem;
  else if (isa_input_select_elem &&
           elem_has_input_item(isa_input_select_elem, line_num))
    select_elem = isa_input_select_elem;

  if (select_elem)
    return make_input_select_alsa_elem(
      select_elem, hw_link_elem, gui_link_elem, line_num
    );

  char s[20];
  snprintf(s, 20, "%d", line_num);
  return gtk_label_new(s);
}

// find a per-input control of the given type for the given channel
static struct alsa_elem *get_input_ctl_elem(
  GPtrArray  *elems,
  const char *control,
  int         ch
) {
  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = g_ptr_array_index(elems, i);

    if (!elem->card)
      continue;
    if (!strstr(elem->name, control))
      continue;
    if (get_num_from_string(elem->name) == ch)
      return elem;
  }

  return NULL;
}

// per-channel input controls ganged in software while a pair without
// a device link switch is GUI-linked (a device link gangs these
// itself, all except mute)
static const char *input_pair_sync_controls[] = {
  "Input Type Capture Enum",
  "Impedance Capture Enum",
  "Gain Capture Volume",
  "Autogain Capture Switch",
  "Safe Capture Switch",
  "Level Capture Enum",
  "Impedance Switch",
  "Air Capture Switch",
  "Air Capture Enum",
  "Mute Capture Switch",
  "Pad Capture Switch",
  "Pad Switch",
  "Gain Switch",
  "Phantom Power Capture Switch",
};

// Main-window channel strip visibility for the analogue input and
// output grids. A strip covers one channel, or a pair of adjacent
// channels when the channels are linkable.
//
// A channel disabled in the I/O configuration has its strip hidden.
// A linked pair collapses to a single strip: the right channel's
// widgets are hidden and the left channel's widgets span both
// columns. Inputs collapse on the device's own link switch (the
// device gangs the controls of a linked pair, so the left channel's
// controls are authoritative); outputs collapse on the GUI stereo
// link, with volume and mute ganged in software.
struct io_strip {
  GtkWidget          *grid;
  GPtrArray          *left;      // control widgets per row, left channel
  GPtrArray          *right;     // control widgets per row, right channel
  GtkWidget          *box;       // row-0 shared pair box (inputs), or NULL
  GtkWidget          *head_l;    // row-0 channel widget, left
  GtkWidget          *head_r;    // row-0 channel widget, right

  // the label within each head carrying the channel name, which is the
  // head itself unless it is a box holding the output's tag as well;
  // NULL when the head isn't a label (inputs)
  GtkWidget          *head_label_l;
  GtkWidget          *head_label_r;
  GtkWidget          *link_widget;   // chain-icon device link toggle, or NULL
  struct alsa_elem   *hw_link_elem;  // device link, NULL if none
  struct alsa_elem   *gui_link_elem; // GUI link, NULL if not a linkable pair
  struct alsa_elem   *enable_l;      // I/O config enable, NULL if none
  struct alsa_elem   *enable_r;
  struct routing_snk *snk_l;     // sinks for label names (outputs only)
  struct routing_snk *snk_r;
  int                 num_l;     // channel number (for label text)
};

// Get the string value of a BYTES name element, or NULL if empty
static char *get_name_elem_string(struct alsa_elem *elem) {
  if (!elem)
    return NULL;

  size_t size;
  const void *bytes = alsa_get_elem_bytes(elem, &size);
  if (!bytes || !size)
    return NULL;

  size_t len = strnlen(bytes, size);
  if (!len || !g_utf8_validate(bytes, len, NULL))
    return NULL;

  return g_strndup(bytes, len);
}

// Label for one channel of an output strip: custom name if set,
// "HP<n> (L/R)" for a dedicated headphone channel, "N/HP<n> (L/R)"
// for a line output channel that also carries headphone output,
// otherwise the channel number
static char *output_channel_label_text(struct routing_snk *snk, int num) {
  if (snk) {
    char *custom = get_name_elem_string(snk->custom_name_elem);
    if (custom)
      return custom;

    char *def = get_snk_default_name_formatted(snk, 0);
    const char *lr = def && strstr(def, "(R)") ? "R" : "L";

    if (def && strncmp(def, "Headphones ", 11) == 0) {
      char *label = g_strdup_printf("HP%d (%s)", get_num_from_string(def), lr);
      g_free(def);
      return label;
    }

    char *hp = def ? strstr(def, "/Headphones") : NULL;
    if (hp) {
      int hp_num = get_num_from_string(hp + 1);
      char *label = hp_num >= 0
        ? g_strdup_printf("%d/HP%d (%s)", num, hp_num, lr)
        : g_strdup_printf("%d/HP (%s)", num, lr);
      g_free(def);
      return label;
    }
    g_free(def);
  }

  return g_strdup_printf("%d", num);
}

// Label for a collapsed output pair: custom pair name if set, "HP<n>"
// for a dedicated headphone pair, "N–N+1/HP<n>" for a line output
// pair that also carries headphone output, otherwise the channel
// number range
static char *output_pair_label_text(struct routing_snk *snk_l, int num) {
  if (snk_l) {
    char *custom = get_name_elem_string(get_snk_pair_name_elem(snk_l));
    if (custom)
      return custom;

    char *def = get_snk_default_pair_name(snk_l);
    if (def && strncmp(def, "Headphones ", 11) == 0) {
      char *label = g_strdup_printf("HP%d", get_num_from_string(def));
      g_free(def);
      return label;
    }

    char *hp = def ? strstr(def, "/Headphones") : NULL;
    if (hp) {
      int hp_num = get_num_from_string(hp + 1);
      char *label = hp_num >= 0
        ? g_strdup_printf("%d–%d/HP%d", num, num + 1, hp_num)
        : g_strdup_printf("%d–%d/HP", num, num + 1);
      g_free(def);
      return label;
    }
    g_free(def);
  }

  return g_strdup_printf("%d–%d", num, num + 1);
}

// Bind a hardware volume knob's label to an output pair's custom
// name; falls back to the pair's default name when no custom name is
// set
struct output_pair_label {
  GtkWidget          *label;
  struct routing_snk *snk_l;
};

static void output_pair_label_updated(
  struct alsa_elem *elem,
  void             *private
) {
  struct output_pair_label *d = private;
  char *name = get_name_elem_string(get_snk_pair_name_elem(d->snk_l));

  if (!name)
    name = get_snk_default_pair_name(d->snk_l);
  if (!name)
    return;

  gtk_label_set_text(GTK_LABEL(d->label), name);
  g_free(name);
}

static void bind_output_pair_label(
  GtkWidget          *label,
  struct routing_snk *snk_l
) {
  struct alsa_elem *pair_name_elem =
    snk_l ? get_snk_pair_name_elem(snk_l) : NULL;

  if (!pair_name_elem)
    return;

  struct output_pair_label *data = g_malloc(sizeof(*data));
  data->label = label;
  data->snk_l = snk_l;

  alsa_elem_add_callback(
    pair_name_elem, output_pair_label_updated, data, g_free
  );
  output_pair_label_updated(NULL, data);
}

// Find the left sink of the analogue output pair whose default name
// identifies it as the headphones pair
static struct routing_snk *get_headphones_snk(struct alsa_card *card) {
  for (int i = 1; ; i += 2) {
    struct routing_snk *snk = get_analogue_output_snk(card, i);
    if (!snk)
      return NULL;

    char *def = get_snk_default_pair_name(snk);
    int found = def && strncmp(def, "Headphones", 10) == 0;
    g_free(def);
    if (found)
      return snk;
  }
}

// Register an output gain widget so its dial's embedded level meter is
// fed from the given routing sink by the level update timer.
static void register_output_gain_widget(
  struct alsa_card   *card,
  GtkWidget          *widget,
  int                 port_num,
  struct routing_snk *r_snk
) {
  struct output_gain_widget *og = g_malloc(sizeof(struct output_gain_widget));

  og->widget = widget;
  og->port_num = port_num;
  og->r_snk = r_snk;
  card->output_gain_widgets = g_list_append(card->output_gain_widgets, og);
}

static void set_io_grid_span(GtkWidget *grid, GtkWidget *child, int span) {
  GtkLayoutManager *lm = gtk_widget_get_layout_manager(grid);
  GtkGridLayoutChild *lc =
    GTK_GRID_LAYOUT_CHILD(gtk_layout_manager_get_layout_child(lm, child));

  gtk_grid_layout_child_set_column_span(lc, span);
}

static void get_io_grid_position(
  GtkWidget *grid,
  GtkWidget *child,
  int       *col,
  int       *span
) {
  GtkLayoutManager *lm = gtk_widget_get_layout_manager(grid);
  GtkGridLayoutChild *lc =
    GTK_GRID_LAYOUT_CHILD(gtk_layout_manager_get_layout_child(lm, child));

  *col = gtk_grid_layout_child_get_column(lc);
  *span = gtk_grid_layout_child_get_column_span(lc);
}

static void io_strip_updated(struct alsa_elem *elem, void *private) {
  struct io_strip *d = private;

  int hw_linked =
    d->hw_link_elem && alsa_get_elem_value(d->hw_link_elem);
  int gui_linked =
    d->gui_link_elem && alsa_get_elem_value(d->gui_link_elem);

  // the device gangs the controls of a hw-linked pair, so one column
  // of controls suffices even when the pair is not GUI-linked
  int collapsed = hw_linked || gui_linked;

  int en_l = !d->enable_l || alsa_get_elem_value(d->enable_l);
  int en_r = !d->enable_r || alsa_get_elem_value(d->enable_r);

  // a collapsed pair follows the left channel's enable state
  int show_l = en_l;
  int show_r = en_r && !collapsed;
  int show_head_r = en_r && !gui_linked;

  for (int i = 0; i < d->left->len; i++) {
    GtkWidget *l = g_ptr_array_index(d->left, i);
    GtkWidget *r = g_ptr_array_index(d->right, i);

    if (l) {
      gtk_widget_set_visible(l, show_l);
      if (r)
        set_io_grid_span(d->grid, l, collapsed ? 2 : 1);
    }
    if (r)
      gtk_widget_set_visible(r, show_r);
  }

  if (d->head_l) {
    gtk_widget_set_visible(d->head_l, show_l);

    // heads placed directly in the grid span the collapsed pair;
    // heads in a shared pair box fill it by hiding their neighbours
    if (!d->box && d->head_r)
      set_io_grid_span(d->grid, d->head_l, gui_linked ? 2 : 1);

    // number labels show custom names, the pair range when
    // collapsed, or the channel number
    if (d->head_label_l) {
      char *text = gui_linked
        ? output_pair_label_text(d->snk_l, d->num_l)
        : output_channel_label_text(d->snk_l, d->num_l);

      gtk_label_set_text(GTK_LABEL(d->head_label_l), text);
      g_free(text);
    }
  }
  if (d->head_r) {
    gtk_widget_set_visible(d->head_r, show_head_r);

    if (d->head_label_r) {
      char *text = output_channel_label_text(d->snk_r, d->num_l + 1);

      gtk_label_set_text(GTK_LABEL(d->head_label_r), text);
      g_free(text);
    }
  }
  if (d->link_widget)
    gtk_widget_set_visible(d->link_widget, en_l && en_r && !gui_linked);
  if (d->box)
    gtk_widget_set_visible(d->box, en_l || en_r);
}

static void io_strip_free(void *p) {
  struct io_strip *d = p;

  g_ptr_array_free(d->left, TRUE);
  g_ptr_array_free(d->right, TRUE);
  g_free(d);
}

// Register the strip update on all elements that affect it and apply
// the initial state
static void register_io_strip(struct io_strip *d) {
  int owner = 1;
  struct alsa_elem *elems[7] = {
    d->hw_link_elem, d->gui_link_elem, d->enable_l, d->enable_r,
    d->snk_l ? d->snk_l->custom_name_elem : NULL,
    d->snk_r ? d->snk_r->custom_name_elem : NULL,
    d->snk_l ? get_snk_pair_name_elem(d->snk_l) : NULL,
  };

  for (int i = 0; i < 7; i++) {
    if (!elems[i])
      continue;
    alsa_elem_add_callback(
      elems[i], io_strip_updated, d, owner ? io_strip_free : NULL
    );
    owner = 0;
  }

  if (owner) {
    io_strip_free(d);
    return;
  }

  io_strip_updated(NULL, d);
}

// While a pair is linked, gang a pair of controls the device doesn't
// gang itself (input mute; output volume and mute) in software
struct pair_elem_sync {
  struct alsa_elem *link_elem;
  struct alsa_elem *from;
  struct alsa_elem *to;

  // Set for elements the device drives itself while the output is in
  // a monitor group; ganging them then would fight the device
  struct routing_snk *mg_snk;
};

static void pair_elem_sync_updated(struct alsa_elem *elem, void *private) {
  struct pair_elem_sync *d = private;

  if (!alsa_get_elem_value(d->link_elem))
    return;

  if (d->mg_snk && snk_in_any_monitor_group(d->mg_snk))
    return;

  long value = alsa_get_elem_value(d->from);
  if (alsa_get_elem_value(d->to) != value)
    alsa_set_elem_value(d->to, value);
}

static void register_pair_elem_sync(
  struct alsa_elem   *link_elem,
  struct alsa_elem   *left,
  struct alsa_elem   *right,
  struct routing_snk *mg_snk
) {
  if (!left || !right)
    return;

  struct pair_elem_sync *data = g_malloc(sizeof(*data));
  data->link_elem = link_elem;
  data->from = left;
  data->to = right;
  data->mg_snk = mg_snk;

  alsa_elem_add_callback(left, pair_elem_sync_updated, data, g_free);

  // left→right also fires on link change so the pair converges on
  // the left channel's state when linked
  alsa_elem_add_callback(link_elem, pair_elem_sync_updated, data, NULL);
  pair_elem_sync_updated(left, data);

  data = g_malloc(sizeof(*data));
  data->link_elem = link_elem;
  data->from = right;
  data->to = left;
  data->mg_snk = mg_snk;

  alsa_elem_add_callback(right, pair_elem_sync_updated, data, g_free);
}

// A control that spans several input channels (e.g. a 48V switch
// covering four inputs) is shared by more than one input strip, so its
// visibility can't be driven by any single strip's enable state. It is
// shown while any one of the channels it covers is enabled.
struct span_visibility {
  GtkWidget *widget;
  GPtrArray *enable_elems;
};

static void span_visibility_updated(struct alsa_elem *elem, void *private) {
  struct span_visibility *sv = private;

  int visible = 0;
  for (guint i = 0; i < sv->enable_elems->len; i++)
    if (alsa_get_elem_value(g_ptr_array_index(sv->enable_elems, i))) {
      visible = 1;
      break;
    }

  gtk_widget_set_visible(sv->widget, visible);
}

static void span_visibility_destroy(GtkWidget *widget, gpointer user_data) {
  struct span_visibility *sv = user_data;

  for (guint i = 0; i < sv->enable_elems->len; i++)
    alsa_elem_remove_callbacks_by_data(
      g_ptr_array_index(sv->enable_elems, i), sv
    );
  g_ptr_array_free(sv->enable_elems, TRUE);
  g_free(sv);
}

// Make a spanning control follow the enable state of the channels
// from..to. Does nothing (leaving the control visible) if the device
// has no per-input enables.
static void manage_span_visibility(
  struct alsa_card *card,
  GtkWidget        *widget,
  int               from,
  int               to
) {
  GPtrArray *enable_elems = g_ptr_array_new();
  for (int channel = from; channel <= to; channel++) {
    struct routing_src *src = get_analogue_input_src(card, channel);
    if (src && src->enable_elem)
      g_ptr_array_add(enable_elems, src->enable_elem);
  }

  if (!enable_elems->len) {
    g_ptr_array_free(enable_elems, TRUE);
    return;
  }

  struct span_visibility *sv = g_malloc0(sizeof(*sv));
  sv->widget = widget;
  sv->enable_elems = enable_elems;

  for (guint i = 0; i < enable_elems->len; i++)
    alsa_elem_add_callback(
      g_ptr_array_index(enable_elems, i), span_visibility_updated, sv, NULL
    );
  g_signal_connect(
    widget, "destroy", G_CALLBACK(span_visibility_destroy), sv
  );

  span_visibility_updated(NULL, sv);
}

static void setup_input_strips(
  struct alsa_card *card,
  GtkWidget        *grid,
  int               rows,
  int               input_count
) {
  GPtrArray *elems = card->elems;

  for (int i = 1; i <= input_count; i++) {
    struct routing_src *src_l = get_analogue_input_src(card, i);
    struct routing_src *src_r =
      (i % 2) && i < input_count
        ? get_analogue_input_src(card, i + 1) : NULL;
    struct alsa_elem *link_elem =
      src_l && src_r ? src_l->link_elem : NULL;
    int is_pair = !!link_elem;

    if (!is_pair)
      src_r = NULL;

    struct io_strip *d = g_malloc0(sizeof(*d));
    d->grid = grid;
    d->left = g_ptr_array_new();
    d->right = g_ptr_array_new();
    d->hw_link_elem = is_pair ? get_device_link_elem(elems, i) : NULL;
    d->gui_link_elem = link_elem;
    d->enable_l = src_l ? src_l->enable_elem : NULL;
    d->enable_r = src_r ? src_r->enable_elem : NULL;
    d->num_l = i;

    // pairs with a device link switch share a row-0 box holding
    // [select][chain][select]; otherwise heads sit in the grid
    GtkWidget *head = gtk_grid_get_child_at(GTK_GRID(grid), i - 1, 0);
    if (is_pair && GTK_IS_BOX(head)) {
      d->box = head;
      d->head_l = gtk_widget_get_first_child(head);
      d->link_widget = gtk_widget_get_next_sibling(d->head_l);
      d->head_r = gtk_widget_get_next_sibling(d->link_widget);
    } else {
      d->head_l = head;
      d->head_r =
        is_pair ? gtk_grid_get_child_at(GTK_GRID(grid), i, 0) : NULL;
    }

    for (int row = 1; row < rows; row++) {
      GtkWidget *l = gtk_grid_get_child_at(GTK_GRID(grid), i - 1, row);
      GtkWidget *r =
        is_pair ? gtk_grid_get_child_at(GTK_GRID(grid), i, row) : NULL;

      // a control spanning several columns (e.g. a 48V switch covering
      // four inputs) covers both of this pair's cells; the strips must
      // not drive its span or visibility. Register its visibility once,
      // on the first strip that reaches its leftmost column, then keep
      // it out of both channels' widget lists.
      if (l && r == l) {
        int col, span;
        get_io_grid_position(grid, l, &col, &span);
        if (col == i - 1)
          manage_span_visibility(card, l, col + 1, col + span);
        l = NULL;
        r = NULL;
      }

      g_ptr_array_add(d->left, l);
      g_ptr_array_add(d->right, r);
    }

    register_io_strip(d);

    if (is_pair) {
      // the device gangs everything of a hw-linked pair except mute;
      // without a device link, gang all the per-channel controls in
      // software while the pair is GUI-linked
      if (d->hw_link_elem)
        register_pair_elem_sync(
          d->hw_link_elem,
          get_input_ctl_elem(elems, "Mute Capture Switch", i),
          get_input_ctl_elem(elems, "Mute Capture Switch", i + 1),
          NULL
        );
      else
        for (int j = 0; j < G_N_ELEMENTS(input_pair_sync_controls); j++)
          register_pair_elem_sync(
            link_elem,
            get_input_ctl_elem(elems, input_pair_sync_controls[j], i),
            get_input_ctl_elem(elems, input_pair_sync_controls[j], i + 1),
            NULL
          );
      i++;
    }
  }
}

static void create_input_controls(
  struct alsa_card *card,
  GtkWidget        *top,
  int              *x,
  int              input_count
) {
  GPtrArray *elems = card->elems;

  // Only the 18i20 Gen 2 has no input controls
  if (!input_count)
    return;

  struct alsa_elem *input_select_elem =
    get_elem_by_name(elems, "Input Select Capture Enum");
  struct alsa_elem *isa_input_select_elem =
    get_elem_by_name(elems, "ISA Input Select Capture Enum");

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

  GtkWidget *label_ic = gtk_label_new("Analogue Inputs");
  gtk_widget_add_css_class(label_ic, "controls-label");
  gtk_widget_set_halign(label_ic, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(box), label_ic);

  widget_doc_register_group(box, "inputs", NULL, "Analogue Inputs");

  GtkWidget *input_grid = gtk_grid_new();
  gtk_widget_add_css_class(input_grid, "controls-content");
  gtk_widget_add_css_class(input_grid, "packed-columns");
  // column spacing comes from each control's own horizontal margins
  // (see .packed-columns in the CSS) so that columns left empty by a
  // disabled input collapse fully, taking their spacing with them,
  // rather than being held open by a control that spans across them
  gtk_grid_set_row_spacing(GTK_GRID(input_grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(input_grid), 0);
  gtk_widget_set_hexpand(input_grid, TRUE);
  gtk_widget_set_halign(input_grid, GTK_ALIGN_FILL);
  gtk_widget_set_vexpand(input_grid, TRUE);
  gtk_box_append(GTK_BOX(box), input_grid);

  gtk_grid_attach(GTK_GRID(top), box, *x, 0, 1, 1);

  // Each linkable pair shares one two-column cell: [1][link][2], with
  // a chain-icon toggle for the device link between the channel select
  // buttons. When the pair is GUI-linked, the chain and right button
  // hide and the left button shows the pair range.
  for (int i = 1; i <= input_count; i++) {
    struct alsa_elem *hw_link_elem =
      (i % 2) && i < input_count ? get_device_link_elem(elems, i) : NULL;
    struct routing_src *src_l =
      (i % 2) && i < input_count
        ? get_analogue_input_src(card, i) : NULL;
    struct alsa_elem *gui_link_elem = src_l ? src_l->link_elem : NULL;

    GtkWidget *sel = make_channel_select_widget(
      input_select_elem, isa_input_select_elem,
      hw_link_elem, gui_link_elem, i
    );

    if (hw_link_elem) {
      GtkWidget *sel2 = make_channel_select_widget(
        input_select_elem, isa_input_select_elem,
        hw_link_elem, gui_link_elem, i + 1
      );

      GtkWidget *link =
        make_boolean_alsa_elem(hw_link_elem, "*link-broken", "*link");
      gtk_widget_add_css_class(link, "input-link");
      gtk_widget_add_css_class(link, "flat");
      gtk_widget_set_tooltip_text(link, "Link as stereo pair");

      gtk_widget_set_hexpand(sel, TRUE);
      gtk_widget_set_hexpand(sel2, TRUE);

      GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
      gtk_box_append(GTK_BOX(box), sel);
      gtk_box_append(GTK_BOX(box), link);
      gtk_box_append(GTK_BOX(box), sel2);
      gtk_grid_attach(GTK_GRID(input_grid), box, i - 1, 0, 2, 1);

      autogain_hold_pair_control(link, card, i, hw_link_elem);

      i++;
    } else {
      gtk_grid_attach(GTK_GRID(input_grid), sel, i - 1, 0, 1, 1);
    }
  }

  int current_row = 1;

  // 4th Gen Solo, put the Phantom Power control above the Air control
  if (get_elem_by_name(elems, "Direct Monitor Playback Switch")) {
    create_input_controls_by_type(
      elems, input_grid, &current_row,
      "Level Capture Enum", create_input_level_control
    );
    create_input_controls_by_type(
      elems, input_grid, &current_row,
      "Phantom Power Capture Switch", create_input_phantom_control
    );
    create_input_controls_by_type(
      elems, input_grid, &current_row,
      "Air Capture Enum", create_input_air_enum_control
    );

    (*x)++;
    return;
  }

  create_input_select_control(elems, input_grid, &current_row);

  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Input Type Capture Enum", create_input_input_type_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Impedance Capture Enum", create_input_impedance_control
  );
  create_input_controls_by_type_with_card(
    card, elems, input_grid, &current_row,
    "Gain Capture Volume", create_input_gain_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Autogain Capture Switch", create_input_autogain_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Safe Capture Switch", create_input_safe_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Level Capture Enum", create_input_level_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Impedance Switch", create_input_level_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Air Capture Switch", create_input_air_switch_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Air Capture Enum", create_input_air_enum_control
  );
  // Console (channels 1-2) and Drive (channels 3-8) share a row
  {
    int count = add_input_controls_by_type(
      elems, input_grid, current_row,
      "Console Capture Switch", create_input_console_switch_control
    );
    count += add_input_controls_by_type(
      elems, input_grid, current_row,
      "Drive Capture Switch", create_input_drive_switch_control
    );
    if (count)
      current_row++;
  }
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Insert Capture Switch", create_input_insert_hpf_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "DSP Capture Switch", create_input_dsp_switch_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "DSP Preset Capture Enum", create_input_dsp_preset_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Mute Capture Switch", create_input_mute_switch_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Pad Capture Switch", create_input_pad_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Pad Switch", create_input_pad_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Gain Switch", create_input_gain_switch_control
  );
  create_input_controls_by_type(
    elems, input_grid, &current_row,
    "Phantom Power Capture Switch", create_input_phantom_control
  );

  setup_input_strips(card, input_grid, current_row, input_count);

  (*x)++;
}

// The mute and dim buttons for the outputs under hardware control
// share the cell below the master volume dial, sitting side by side at
// half its width. Mute prepends and dim appends so that the order
// doesn't depend on which element the loop reaches first.
static GtkWidget *get_master_button_box(GtkWidget *output_grid) {
  GtkWidget *box = gtk_grid_get_child_at(GTK_GRID(output_grid), 0, 3);

  if (box)
    return box;

  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_box_set_homogeneous(GTK_BOX(box), TRUE);
  gtk_grid_attach(GTK_GRID(output_grid), box, 0, 3, 1, 1);

  return box;
}

// An output's heading is wrapped in a box with its tag below it when
// it has one, so the label carrying the channel name is either the
// grid child or the first widget inside it.
static GtkWidget *get_head_label(GtkWidget *head) {
  if (GTK_IS_LABEL(head))
    return head;

  if (GTK_IS_BOX(head))
    return gtk_widget_get_first_child(head);

  return NULL;
}

// Output strips: rows 2-5 of the output grid (volume and the mute
// buttons); the heading and its tag share row 0. Pairs collapse on the
// GUI stereo link with volume, mute, and SW/HW ganged in software,
// since the device has no output link switch.
static void setup_output_strips(
  struct alsa_card  *card,
  GtkWidget         *grid,
  int                line_1_col,
  int                output_count,
  struct alsa_elem **vol_elems,
  struct alsa_elem **mute_elems,
  struct alsa_elem **sw_hw_elems
) {
  for (int i = 1; i <= output_count; i++) {
    struct routing_snk *snk_l = get_analogue_output_snk(card, i);
    struct routing_snk *snk_r =
      i < output_count ? get_analogue_output_snk(card, i + 1) : NULL;
    int is_pair = (i % 2) && snk_l && snk_l->link_elem && snk_r;
    int col = i - 1 + line_1_col;

    struct io_strip *d = g_malloc0(sizeof(*d));
    d->grid = grid;
    d->left = g_ptr_array_new();
    d->right = g_ptr_array_new();
    d->head_l = gtk_grid_get_child_at(GTK_GRID(grid), col, 0);
    d->head_r =
      is_pair ? gtk_grid_get_child_at(GTK_GRID(grid), col + 1, 0) : NULL;
    d->head_label_l = get_head_label(d->head_l);
    d->head_label_r = get_head_label(d->head_r);
    d->gui_link_elem = is_pair ? snk_l->link_elem : NULL;
    d->enable_l = snk_l ? snk_l->enable_elem : NULL;
    d->enable_r = is_pair ? snk_r->enable_elem : NULL;
    d->snk_l = snk_l;
    d->snk_r = is_pair ? snk_r : NULL;
    d->num_l = i;

    for (int row = 2; row <= 5; row++) {
      GtkWidget *l = gtk_grid_get_child_at(GTK_GRID(grid), col, row);
      GtkWidget *r =
        is_pair ? gtk_grid_get_child_at(GTK_GRID(grid), col + 1, row) : NULL;

      // a widget spanning both columns belongs to the pair as a whole
      g_ptr_array_add(d->left, l);
      g_ptr_array_add(d->right, r != l ? r : NULL);
    }

    register_io_strip(d);

    if (is_pair) {
      // The device sets a grouped output's volume from the group
      // master and that output's trim, so the two channels of a pair
      // differ whenever their trims do: leave them to the device, and
      // show the higher of the two on the pair's knob
      register_pair_elem_sync(
        snk_l->link_elem, vol_elems[i], vol_elems[i + 1], snk_l
      );
      set_gain_display_peer(
        g_ptr_array_index(d->left, 0), vol_elems[i + 1]
      );

      struct alsa_elem **pair_elems[] = { mute_elems, sw_hw_elems };

      for (int j = 0; j < G_N_ELEMENTS(pair_elems); j++)
        register_pair_elem_sync(
          snk_l->link_elem, pair_elems[j][i], pair_elems[j][i + 1], NULL
        );
      i++;
    }
  }
}

// The FCP devices quantise the master volume to the steps of the
// volume knob taper; writes off the table are snapped to the nearest
// entry (note -78 is not accepted). Verified identical by sweeping
// the 18i20, 18i16, and 16i16 4th Gen, and the ISA C8X.
static const int fcp_master_volume_map[] = {
  -127, -118, -109, -100, -97, -95, -92, -90, -87, -85,
  -84, -83, -82, -81, -80, -79, -77, -76, -75, -74,
  -73, -72, -71, -70, -69, -68, -67, -66, -65, -64,
  -63, -62, -61, -60, -59, -58, -57, -56, -55, -54,
  -53, -52, -51, -50, -49, -48, -47, -46, -45, -44,
  -43, -42, -41, -40, -39, -38, -37, -36, -35, -34,
  -33, -32, -31, -30, -29, -28, -27, -26, -25, -24,
  -23, -22, -21, -20, -19, -18, -17, -16, -15, -14,
  -13, -12, -11, -10, -9, -8, -7, -6, -5, -4,
  -3, -2, -1, 0,
};

// Check if a device's master volume uses fcp_master_volume_map
static int pid_has_master_volume_map(uint32_t pid) {
  switch (pid) {
    case PID_SCARLETT_GEN4_16I16:
    case PID_SCARLETT_GEN4_18I16:
    case PID_SCARLETT_GEN4_18I20:
    case PID_ISA_C8X:
      return 1;
    default:
      return 0;
  }
}

static void create_output_controls(
  struct alsa_card *card,
  GtkWidget        *top,
  int              *x,
  int              y,
  int              x_span,
  int              output_count
) {
  GPtrArray *elems = card->elems;

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

  GtkWidget *label_oc = gtk_label_new("Analogue Outputs");
  gtk_widget_add_css_class(label_oc, "controls-label");
  gtk_widget_set_halign(label_oc, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(box), label_oc);

  widget_doc_register_group(box, "outputs", NULL, "Analogue Outputs");

  GtkWidget *output_grid = gtk_grid_new();
  gtk_widget_add_css_class(output_grid, "controls-content");
  gtk_grid_set_spacing(GTK_GRID(output_grid), 10);
  gtk_widget_set_hexpand(output_grid, TRUE);
  gtk_widget_set_vexpand(output_grid, TRUE);
  gtk_box_append(GTK_BOX(box), output_grid);

  gtk_grid_attach(GTK_GRID(top), box, *x, y, x_span, 1);

  /* 4th Gen Solo/2i2 */
  if (get_elem_by_prefix(elems, "Direct Monitor Playback")) {
    struct alsa_elem *elem;

    for (int i = 0; i < 2; i++) {
      char s[20];
      snprintf(s, 20, "%d", i + 1);
      GtkWidget *label = gtk_label_new(s);
      gtk_grid_attach(GTK_GRID(output_grid), label, i, 0, 1, 1);
    }

    /* Solo */

    elem = get_elem_by_name(elems, "Direct Monitor Playback Switch");

    if (elem) {
      GtkWidget *w = make_boolean_alsa_elem(elem, "Direct Monitor", NULL);
      gtk_widget_add_css_class(w, "direct-monitor");
      gtk_widget_set_tooltip_text(
        w,
        "Direct Monitor sends the analogue input signals to the "
        "analogue outputs for zero-latency monitoring."
      );
      gtk_grid_attach(GTK_GRID(output_grid), w, 0, 1, 2, 1);
    }

    /* 2i2 */

    elem = get_elem_by_name(elems, "Direct Monitor Playback Enum");

    if (elem) {
      GtkWidget *w = make_drop_down_alsa_elem(elem, "Direct Monitor");
      gtk_widget_add_css_class(w, "direct-monitor");
      gtk_widget_set_tooltip_text(
        w,
        "Direct Monitor sends the analogue input signals to the "
        "analogue outputs for zero-latency monitoring."
      );
      gtk_grid_attach(GTK_GRID(output_grid), w, 0, 1, 2, 1);
    }

    return;
  }

  int has_sw_hw_ctrls =
    !!get_elem_by_substr(elems, "Volume Control Playback Enum");
  int line_1_col =
    has_sw_hw_ctrls ||
    get_elem_by_name(elems, "Mute Playback Switch") ||
    get_elem_by_name(elems, "Master Playback Switch");

  for (int i = 0; i < output_count; i++) {
    char s[20];
    snprintf(s, 20, "%d", i + 1);
    GtkWidget *label = gtk_label_new(s);
    gtk_grid_attach(GTK_GRID(output_grid), label, i + line_1_col, 0, 1, 1);
  }

  struct alsa_elem **vol_elems =
    g_new0(struct alsa_elem *, output_count + 2);
  struct alsa_elem **mute_elems =
    g_new0(struct alsa_elem *, output_count + 2);
  struct alsa_elem **sw_hw_elems =
    g_new0(struct alsa_elem *, output_count + 2);

  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = g_ptr_array_index(elems, i);
    GtkWidget *w;

    // if no card entry, it's an empty slot
    if (!elem->card)
      continue;

    // output controls

    // Master output control (Gen 1 and FCP devices)
    if (strcmp(elem->name, "Master Playback Volume") == 0) {
      GtkWidget *l = gtk_label_new("Master");
      gtk_grid_attach(GTK_GRID(output_grid), l, 0, 0, 1, 1);
      if (pid_has_master_volume_map(card->pid))
        w = make_mapped_gain_alsa_elem(
          elem, 1, 1, FALSE,
          fcp_master_volume_map,
          sizeof(fcp_master_volume_map) / sizeof(int)
        );
      else
        w = make_gain_alsa_elem(elem, 1, WIDGET_GAIN_TAPER_LOG, 0, FALSE);
      gtk_widget_set_tooltip_text(w, "Master Volume Control");
      gtk_grid_attach(GTK_GRID(output_grid), w, 0, 2, 1, 1);

    } else if (strncmp(elem->name, "Line", 4) == 0 ||
               (strncmp(elem->name, "Master", 6) == 0 && elem->lr_num) ||
               strncmp(elem->name, "Analogue", 8) == 0) {

      if (strstr(elem->name, "Playback Volume")) {
        if (elem->lr_num <= output_count)
          vol_elems[elem->lr_num] = elem;
        w = make_gain_alsa_elem(elem, 1, WIDGET_GAIN_TAPER_LOG, 1, TRUE);
        setup_output_volume_monitor_group(card, w, elem->lr_num);
        gtk_grid_attach(
          GTK_GRID(output_grid), w, elem->lr_num - 1 + line_1_col, 2, 1, 1
        );

        register_output_gain_widget(
          card, w, elem->lr_num, get_output_r_snk(card, elem->lr_num)
        );

      } else if (strstr(elem->name, "Playback Switch")) {
        if (elem->lr_num <= output_count)
          mute_elems[elem->lr_num] = elem;
        w = make_boolean_alsa_elem(
          elem, "*audio-volume-high", "*audio-volume-muted"
        );
        gtk_widget_add_css_class(w, "mute");
        if (has_sw_hw_ctrls) {
          gtk_widget_set_tooltip_text(
            w,
            "Mute (only available when under software control)"
          );
        } else {
          gtk_widget_set_tooltip_text(w, "Mute");
        }
        w = make_output_mute(card, w, elem->lr_num, line_mute_icons);
        gtk_grid_attach(
          GTK_GRID(output_grid), w, elem->lr_num - 1 + line_1_col, 3, 1, 1
        );
      // The choice between hardware and software volume control lives
      // in the I/O configuration; the strip only needs the element so
      // a linked pair can be ganged
      } else if (strstr(elem->name, "Volume Control Playback Enum")) {
        if (elem->lr_num <= output_count)
          sw_hw_elems[elem->lr_num] = elem;
      }

    // master output controls
    } else if (strcmp(elem->name, "Master HW Playback Volume") == 0) {
      int gen4 = !!strstr(card->name, "4th Gen");

      GtkWidget *l = gtk_label_new(gen4 ? "Line 1–2" : "HW");
      gtk_grid_attach(GTK_GRID(output_grid), l, 0, 0, 1, 1);
      if (gen4) {
        struct routing_snk *snk = get_analogue_output_snk(card, 1);

        bind_output_pair_label(l, snk);
        w = make_gain_alsa_elem(elem, 1, WIDGET_GAIN_TAPER_GEN4_VOLUME, 0, TRUE);
        register_output_gain_widget(card, w, 1, snk);
      } else {
        w = make_gain_alsa_elem(elem, 1, WIDGET_GAIN_TAPER_LOG, 0, FALSE);
      }
      gtk_widget_set_tooltip_text(
        w,
        gen4
          ? "This control shows the setting of the master volume "
            "knob, which controls the volume of the analogue line "
            "outputs 1 and 2."
          : "This control shows the setting of the physical "
            "(hardware) volume knob, which controls the volume of "
            "the analogue outputs which have been set to “HW”."
      );
      gtk_grid_attach(GTK_GRID(output_grid), w, 0, 2, 1, 1);
    } else if (strcmp(elem->name, "Headphone Playback Volume") == 0) {
      GtkWidget *l = gtk_label_new("Headphones");
      struct routing_snk *snk = get_headphones_snk(card);

      bind_output_pair_label(l, snk);
      gtk_widget_set_tooltip_text(
        l,
        "This control shows the setting of the headphone volume knob."
      );
      gtk_grid_attach(GTK_GRID(output_grid), l, 1, 0, 1, 1);
      w = make_gain_alsa_elem(elem, 1, WIDGET_GAIN_TAPER_GEN4_VOLUME, 0, TRUE);
      register_output_gain_widget(card, w, snk ? snk->elem->lr_num : 0, snk);
      gtk_grid_attach(GTK_GRID(output_grid), w, 1, 2, 1, 1);
    } else if (strcmp(elem->name, "Mute Playback Switch") == 0) {
      w = make_boolean_alsa_elem(
        elem, "*audio-volume-high", "*audio-volume-muted"
      );
      gtk_widget_add_css_class(w, "mute");
      gtk_widget_set_tooltip_text(w, "Mute HW controlled outputs");
      gtk_box_prepend(GTK_BOX(get_master_button_box(output_grid)), w);
    } else if (strcmp(elem->name, "Dim Playback Switch") == 0) {
      w = make_boolean_alsa_elem(
        elem, "*audio-volume-medium", "*audio-volume-low"
      );
      gtk_widget_add_css_class(w, "dim");
      gtk_widget_set_tooltip_text(
        w, "Dim (lower volume) of HW controlled outputs"
      );
      gtk_box_append(GTK_BOX(get_master_button_box(output_grid)), w);
    } else if (strcmp(elem->name, "Speaker Mute Playback Switch") == 0) {
      w = make_boolean_alsa_elem(
        elem, "*audio-volume-high", "*audio-volume-muted"
      );
      gtk_widget_add_css_class(w, "mute");
      gtk_widget_set_tooltip_text(w, "Mute speaker output");
      gtk_grid_attach(GTK_GRID(output_grid), w, 0, 5, 1, 1);
    } else if (strstr(elem->name, "Headphones") &&
               strstr(elem->name, "Mute Playback Switch")) {
      int headphones_num = get_num_from_string(elem->name);
      int lr_num =
        headphones_num > 0 ? get_headphones_output(card, headphones_num) : 0;

      // With a named headphone output, the button goes with the line
      // mutes across both of that output's columns, since it mutes
      // both channels; otherwise below the first outputs
      int column = lr_num ? lr_num - 1 + line_1_col
                 : headphones_num > 0 ? headphones_num
                 : 1;
      int row = lr_num ? 3 : 5;
      int width = lr_num ? 2 : 1;

      w = make_boolean_alsa_elem(
        elem, "*audio-headphones-symbolic", "*audio-headphones-symbolic"
      );
      gtk_widget_add_css_class(w, "mute");

      if (headphones_num > 0) {
        char tooltip[50];
        snprintf(tooltip, 50, "Mute headphones %d output", headphones_num);
        gtk_widget_set_tooltip_text(w, tooltip);
      } else {
        gtk_widget_set_tooltip_text(w, "Mute headphones output");
      }

      if (lr_num)
        w = make_output_mute(card, w, lr_num, headphones_mute_icons);

      gtk_grid_attach(GTK_GRID(output_grid), w, column, row, width, 1);
    }
  }

  /* Add the monitor group / hardware volume knob tag for each output,
   * in a box below the output's heading rather than in a row of its
   * own, which would put the grid's row spacing above and below it.
   * Top-aligning every heading keeps them on one line with the tags
   * hanging below.
   */
  for (int col = 0; col < line_1_col + output_count; col++) {
    GtkWidget *heading =
      gtk_grid_get_child_at(GTK_GRID(output_grid), col, 0);

    if (!heading)
      continue;

    gtk_widget_set_valign(heading, GTK_ALIGN_START);

    // the columns before the outputs hold the master and headphone
    // dials, which are in no monitor group
    int lr_num = col - line_1_col + 1;
    GtkWidget *tag =
      lr_num >= 1 ? create_output_tag_label(card, lr_num) : NULL;

    if (!tag)
      continue;

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    gtk_widget_set_valign(box, GTK_ALIGN_START);

    g_object_ref(heading);
    gtk_grid_remove(GTK_GRID(output_grid), heading);
    gtk_box_append(GTK_BOX(box), heading);
    g_object_unref(heading);

    gtk_box_append(GTK_BOX(box), tag);
    gtk_grid_attach(GTK_GRID(output_grid), box, col, 0, 1, 1);
  }

  setup_output_strips(
    card, output_grid, line_1_col, output_count,
    vol_elems, mute_elems, sw_hw_elems
  );
  g_free(vol_elems);
  g_free(mute_elems);
  g_free(sw_hw_elems);

  (*x)++;
}

static void create_global_controls(
  struct alsa_card *card,
  GtkWidget        *top,
  int              *x
) {
  int orient = card->has_speaker_switching
    ? GTK_ORIENTATION_HORIZONTAL
    : GTK_ORIENTATION_VERTICAL;
  GtkWidget *global_controls = create_global_box(top, x, orient);
  GtkWidget *column[3];

  for (int i = 0; i < 3; i++)
    column[i] = global_controls;

  if (card->has_speaker_switching) {
    for (int i = 0; i < 3; i++) {
      column[i] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
      gtk_box_append(GTK_BOX(global_controls), column[i]);
    }
  }

  add_clock_source_control(card, column[0]);
  add_sync_status_control(card, column[1]);
  add_power_status_control(card, column[1]);
  add_sample_rate_control(card, column[2]);
  add_speaker_switching_controls_enum(card, column[0]);
  add_speaker_switching_controls_switches(card, column[0]);
  add_speaker_switching_controls_gen4(card, column[0]);
  add_monitor_group_enum_control(card, column[0]);
  add_talkback_controls_enum(card, column[1]);
  add_talkback_controls_switches(card, column[1]);

  // Add Presets button if device has a serial number
  if (card->serial && *card->serial) {
    GtkWidget *presets_button = create_presets_button(card);
    gtk_widget_set_valign(presets_button, GTK_ALIGN_END);
    gtk_widget_set_vexpand(presets_button, TRUE);
    gtk_box_append(GTK_BOX(column[2]), presets_button);
  }
}

static GtkWidget *create_main_window_controls(struct alsa_card *card) {
  int x = 0;

  GtkWidget *top = gtk_grid_new();
  gtk_widget_add_css_class(top, "window-content");
  gtk_widget_add_css_class(top, "iface-mixer");

  add_card_style_class(top, card);

  gtk_grid_set_spacing(GTK_GRID(top), 15);

  int input_count = get_max_elem_by_name(
    card->elems, "Line", "Capture Switch"
  );
  if (!input_count)
    input_count =
      get_max_elem_by_name(card->elems, "Input", "Switch");

  int output_count = get_max_elem_by_name(
    card->elems, "Line", "Playback Volume"
  );
  if (!output_count)
    output_count =
      get_max_elem_by_name(card->elems, "Master", "Playback Volume") * 2;
  if (!output_count)
    output_count =
      get_max_elem_by_name(card->elems, "Analogue", "Playback Volume");

  create_global_controls(card, top, &x);
  create_input_controls(card, top, &x, input_count);

  if (input_count + output_count >= 12) {
    x = 0;
    create_output_controls(card, top, &x, 1, 2, output_count);
  } else {
    create_output_controls(card, top, &x, 0, 1, output_count);
  }

  return top;
}

static gboolean window_routing_close_request(GtkWindow *w, gpointer data) {
  struct alsa_card *card = data;

  gtk_widget_activate_action(
    GTK_WIDGET(card->window_main), "win.routing", NULL
  );
  return true;
}

static gboolean window_mixer_close_request(GtkWindow *w, gpointer data) {
  struct alsa_card *card = data;

  gtk_widget_activate_action(
    GTK_WIDGET(card->window_main), "win.mixer", NULL
  );
  return true;
}

static gboolean window_levels_close_request(GtkWindow *w, gpointer data) {
  struct alsa_card *card = data;

  gtk_widget_activate_action(
    GTK_WIDGET(card->window_main), "win.levels", NULL
  );
  return true;
}

static gboolean window_dsp_close_request(GtkWindow *w, gpointer data) {
  struct alsa_card *card = data;

  gtk_widget_activate_action(
    GTK_WIDGET(card->window_main), "win.dsp", NULL
  );
  return true;
}

// wrap a scrolled window around the controls
static void create_scrollable_window(GtkWidget *window, GtkWidget *controls) {
  GtkWidget *scrolled_window = gtk_scrolled_window_new();

  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scrolled_window),
    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC
  );
  gtk_scrolled_window_set_child(
    GTK_SCROLLED_WINDOW(scrolled_window), controls
  );
  gtk_scrolled_window_set_propagate_natural_height(
    GTK_SCROLLED_WINDOW(scrolled_window), TRUE
  );
  gtk_scrolled_window_set_propagate_natural_width(
    GTK_SCROLLED_WINDOW(scrolled_window), TRUE
  );

  gtk_window_set_child(GTK_WINDOW(window), scrolled_window);
  gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
}

void cleanup_input_gain_widgets(struct alsa_card *card) {
  for (GList *l = card->input_gain_widgets; l != NULL; l = l->next) {
    struct input_gain_widget *ig = l->data;
    cleanup_gain_widget(ig->widget);
    g_free(ig);
  }
  g_list_free(card->input_gain_widgets);
  card->input_gain_widgets = NULL;
}

void cleanup_output_gain_widgets(struct alsa_card *card) {
  for (GList *l = card->output_gain_widgets; l != NULL; l = l->next) {
    struct output_gain_widget *og = l->data;
    cleanup_gain_widget(og->widget);
    g_free(og);
  }
  g_list_free(card->output_gain_widgets);
  card->output_gain_widgets = NULL;
}

GtkWidget *create_iface_mixer_main(struct alsa_card *card) {

  cleanup_input_gain_widgets(card);
  cleanup_output_gain_widgets(card);

  card->has_speaker_switching =
    get_elem_by_name(card->elems, "Speaker Switching Playback Enum") ||
    get_elem_by_name(card->elems, "Speaker Switching Playback Switch");

  card->has_talkback =
    get_elem_by_name(card->elems, "Talkback Playback Enum") ||
    get_elem_by_name(card->elems, "Talkback Enable Playback Switch");

  GtkWidget *top = gtk_frame_new(NULL);
  gtk_widget_add_css_class(top, "window-frame");
  GtkWidget *contents = create_main_window_controls(card);
  gtk_frame_set_child(GTK_FRAME(top), contents);

  GtkWidget *routing_top = create_routing_controls(card);
  if (!routing_top)
    return NULL;

  // initialise level indication for routing lines
  routing_levels_init(card);

  card->window_routing = create_subwindow(
    card, "Routing", G_CALLBACK(window_routing_close_request)
  );

  create_scrollable_window(card->window_routing, routing_top);

  GtkWidget *mixer_top = create_mixer_controls(card);

  card->window_mixer = create_subwindow(
    card, "Mixer", G_CALLBACK(window_mixer_close_request)
  );

  create_scrollable_window(card->window_mixer, mixer_top);

  GtkWidget *levels_top = create_levels_controls(card);

  card->window_levels = create_subwindow(
    card, "Levels", G_CALLBACK(window_levels_close_request)
  );

  // the meters are laid out in fixed rows, so the window is exactly as
  // big as they are
  gtk_window_set_child(GTK_WINDOW(card->window_levels), levels_top);
  gtk_window_set_resizable(GTK_WINDOW(card->window_levels), FALSE);

  card->window_configuration = create_subwindow(
    card, "Configuration", G_CALLBACK(window_configuration_close_request)
  );
  gtk_window_set_resizable(GTK_WINDOW(card->window_configuration), TRUE);

  GtkWidget *configuration = create_configuration_controls(card);
  gtk_window_set_child(GTK_WINDOW(card->window_configuration), configuration);

  create_autogain_window(card);

  // create DSP window if DSP controls are available
  if (get_elem_by_name(card->elems, "Line In 1 DSP Capture Switch")) {
    card->window_dsp = create_subwindow(
      card, "DSP", G_CALLBACK(window_dsp_close_request)
    );

    GtkWidget *dsp = create_dsp_controls(card);
    gtk_window_set_child(GTK_WINDOW(card->window_dsp), dsp);
  }

  return top;
}
