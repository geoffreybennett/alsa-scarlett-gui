// SPDX-FileCopyrightText: 2022-2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <alsa/asoundlib.h>
#include <gtk/gtk.h>

#include "const.h"

// simulated cards have card->num set to -1
#define SIMULATED_CARD_NUM -1

// maximum number of monitor groups (Main, Alt 1, Alt 2)
#define MAX_MONITOR_GROUPS 3

// controls for one monitor group of one output (NULL if none)
struct monitor_group_elems {
  struct alsa_elem *enable;
  struct alsa_elem *source;
  struct alsa_elem *trim;
};

// forward definitions
struct alsa_elem;
struct alsa_card;
struct card_update_status;
struct card_download;
struct remote_server;

// typedef for callbacks to update widgets when the alsa element
// notifies of a change
typedef void (AlsaElemCallback)(struct alsa_elem *, void *);

// port categories for routing_src and routing_snk entries
// must match the level meter ordering from the driver
enum {
  PC_OFF,  // Off (the source when a sink is not connected)
  PC_HW,   // Hardware inputs/outputs
  PC_MIX,  // Mixer inputs/outputs
  PC_DSP,  // DSP inputs/outputs
  PC_PCM,  // PCM inputs/outputs
  PC_COUNT // number of port categories
};

// short names for the port categories (for debug output)
extern const char *port_category_short_names[PC_COUNT];

// hardware types
enum {
  HW_TYPE_ANALOGUE,
  HW_TYPE_SPDIF,
  HW_TYPE_ADAT,
  HW_TYPE_COUNT
};

// check if hardware type is digital I/O (S/PDIF or ADAT)
static inline int is_digital_io_type(int hw_type) {
  return hw_type == HW_TYPE_SPDIF || hw_type == HW_TYPE_ADAT;
}

// driver types
// NONE is 1st Gen or Scarlett2 before hwdep support was added
// (no erase config or firmware update support)
// HWDEP is the Scarlett2 driver after hwdep support was added
// SOCKET is the FCP driver
enum {
  DRIVER_TYPE_NONE,
  DRIVER_TYPE_HWDEP,
  DRIVER_TYPE_SOCKET,
  DRIVER_TYPE_SOCKET_UNINIT,
  DRIVER_TYPE_COUNT
};

// names for the hardware types
extern const char *hw_type_names[HW_TYPE_COUNT];

// is a drag active, and whether dragging from a routing source or a
// routing sink
enum {
  DRAG_TYPE_NONE = 0,
  DRAG_TYPE_SRC  = 1,
  DRAG_TYPE_SNK  = 2,
};

// entry in alsa_card routing_srcs (routing sources) array
// list of enums that are in the Mixer Input X Capture Enum elements
struct routing_src {

  // pointer back to the card this entry is associated with
  struct alsa_card *card;

  // the enum id of the alsa item
  int id;

  // PC_OFF, PC_DSP, PC_MIX, PC_PCM, or PC_HW
  int port_category;

  // 0-based count within port_category
  int port_num;

  // the alsa item name
  char *name;

  // for PC_HW, the hardware type
  int hw_type;

  // the number (or translated letter; A = 1) in the item name
  int lr_num;

  // on the routing page, the box widget containing the text and the
  // "socket" widget for this routing source
  GtkWidget *widget;

  // the socket widget
  GtkWidget *widget2;

  // the label widget (for updating text, e.g. PCM availability)
  GtkWidget *label_widget;

  // mixer outputs also get a label drawn at an angle, used in place of
  // the label above when the port has a name too long to sit under it
  GtkWidget *rotated_label_widget;

  // optional/simulated element for custom name
  struct alsa_elem *custom_name_elem;

  // optional/simulated element for enable/disable
  struct alsa_elem *enable_elem;

  // optional/simulated element for stereo link state (stored on left channel)
  struct alsa_elem *link_elem;

  // optional/simulated element for stereo pair name (stored on left channel)
  struct alsa_elem *pair_name_elem;

  // cached pointer to stereo partner (adjacent L/R channel)
  struct routing_src *partner;

  // cached display name (either custom or default)
  // updated by callback when custom name changes
  char *display_name;

  // mixer window labels for mixer outputs (Mix A, Mix B, etc.)
  GtkWidget *mixer_label_left;
  GtkWidget *mixer_label_right;

  // routing window talkback button widget (for mixer outputs with talkback)
  GtkWidget *talkback_widget;

  // talkback element for this mixer output (if talkback is supported)
  struct alsa_elem *talkback_elem;

  // cached level meter index (-1 if none)
  int level_index;
};

// entry in alsa_card routing_snks (routing sinks) array for alsa
// elements that are routing sinks like Analogue Output 01 Playback
// Enum
struct routing_snk {

  // location within the array
  int idx;

  // pointer back to the element this entry is associated with
  struct alsa_elem *elem;

  // box widget on the routing page
  GtkWidget *box_widget;

  // socket widget on the routing page
  GtkWidget *socket_widget;

  // label widget on the routing page (for updating text)
  GtkWidget *label_widget;

  // the mixer label widgets for this sink
  GtkWidget *mixer_label_top;
  GtkWidget *mixer_label_bottom;

  // config-io pair label (stored on left channel only)
  GtkWidget *config_io_pair_label;

  // optional/simulated element for custom name
  struct alsa_elem *custom_name_elem;

  // optional/simulated element for enable/disable
  struct alsa_elem *enable_elem;

  // optional/simulated element for stereo link state (stored on left channel)
  struct alsa_elem *link_elem;

  // optional/simulated element for stereo pair name (stored on left channel)
  struct alsa_elem *pair_name_elem;

  // cached pointer to stereo partner (adjacent L/R channel)
  struct routing_snk *partner;

  // cached display name (either custom or default)
  // updated by callback when custom name changes
  char *display_name;

  // cached effective source index (accounting for speaker switching)
  // updated by callback when monitor group controls change
  int effective_source_idx;

  // cached left channel status (for fixed mixer inputs, based on source)
  int is_left;

  // per-group monitor group controls, indexed to match the card's
  // monitor_group_names (NULL if not applicable)
  struct monitor_group_elems monitor_groups[MAX_MONITOR_GROUPS];

  // cached level meter index (-1 if none)
  int level_index;
};

// hold one callback & its data
struct alsa_elem_callback {
  AlsaElemCallback *callback;
  void             *data;
  GDestroyNotify    destroy;  // cleanup function, NULL if no cleanup needed
};

// entry in alsa_card elems (ALSA control elements) array
struct alsa_elem {

  // pointer back to the card
  struct alsa_card *card;

  // ALSA element information
  int   numid;
  char *name;
  int   type;
  int   count;
  int   index;

  // for gain/volume elements, the value range, dB type, and dB range
  int min_val;
  int max_val;
  int dB_type;
  int min_cdB;
  int max_cdB;

  // level meter labels
  char **meter_labels;

  // for routing sinks
  int is_routing_snk;
  int port_category;
  int port_num;
  int hw_type;
  int lr_num;

  // the callback functions for this ALSA control element
  GList *callbacks;

  // for simulated elements, the current state
  int  is_simulated;
  int  is_writable;
  int  is_volatile;
  long  value;
  long *values;  // cached multi-value integer state

  // for simulated enumerated elements, the items
  int    item_count;
  char **item_names;

  // for BYTES type elements
  void   *bytes_value;
  size_t  bytes_size;

  // pending idle callback for change notification
  guint pending_idle;
};

struct alsa_card {
  int                 num;
  char               *device;

  // USB vendor and product IDs, both zero if the card isn't a USB
  // device the GUI recognises
  uint32_t            vid;
  uint32_t            pid;
  char               *serial;
  char               *name;
  int                 driver_type;
  char               *fcp_socket;

  // For scarlett4 (big Gen 4) devices with 4-valued versions
  uint32_t            firmware_version_4[4];
  uint32_t            esp_firmware_version[4];

  // what the online index offers this card, owned by online-check.c
  struct card_update_status *update_status;

  // how its automatic firmware download is getting on, likewise
  struct card_download *download;
  snd_ctl_t          *handle;
  struct pollfd       pfd;
  GPtrArray          *elems;
  struct alsa_elem   *sample_capture_elem;
  struct alsa_elem   *level_meter_elem;
  double             *routing_levels;
  int                 routing_levels_count;
  GArray             *routing_srcs;
  GArray             *routing_snks;
  int                *monitor_group_src_map;
  int                 monitor_group_src_map_count;

  // monitor group names ("Main"/"Alt" or "Main"/"Alt 1"/"Alt 2")
  // detected from the group switch control names
  const char         *monitor_group_names[MAX_MONITOR_GROUPS];
  int                 monitor_group_count;

  // the control selecting the active monitor group; its value is the
  // group index (a bool control on two-group devices)
  struct alsa_elem   *monitor_group_select_elem;
  GIOChannel         *io_channel;
  guint               event_source_id;
  GtkWidget          *window_main;
  GtkWidget          *window_routing;
  GtkWidget          *window_mixer;
  GtkWidget          *window_levels;
  GtkWidget          *window_configuration;
  GtkWidget          *window_modal;
  GtkWidget          *window_dsp;
  GtkWidget          *window_autogain;
  struct remote_server *remote_server;
  GtkWidget          *window_main_contents;
  GtkWidget          *routing_grid;
  GtkWidget          *mixer_grid;
  GtkWidget          *monitor_groups_grid;
  GList              *monitor_group_cbs;
  GList              *monitor_group_gains;
  GtkWidget          *mixer_overlay;
  GtkWidget          *mixer_glow;
  GtkWidget          *mixer_label_overlay;
  GtkWidget          *mixer_right_spacer;
  GtkWidget          *mixer_unavailable_label;
  GtkWidget          *mixer_corner_label;
  GList              *mixer_gain_widgets;
  GList              *input_gain_widgets;
  GList              *output_gain_widgets;
  GList              *dsp_comp_widgets;
  GtkWidget          *routing_lines;
  GtkWidget          *routing_hw_in_grid;
  GtkWidget          *routing_hw_out_grid;
  GtkWidget          *routing_pcm_in_grid;
  GtkWidget          *routing_pcm_out_grid;
  GtkWidget          *routing_dsp_in_grid;
  GtkWidget          *routing_dsp_out_grid;
  GtkWidget          *routing_mixer_in_grid;
  GtkWidget          *routing_mixer_out_grid;
  GtkWidget          *routing_mixer_in_heading;
  GtkWidget          *routing_mixer_out_heading;
  GtkWidget          *routing_src_label;
  GtkWidget          *routing_snk_label;
  int                 has_speaker_switching;
  int                 has_talkback;
  int                 has_fixed_mixer_inputs;
  int                 mixer_has_mix_srcs;
  int                 routing_out_count[PC_COUNT];
  int                 routing_in_count[PC_COUNT];
  GtkWidget          *drag_line;
  int                 drag_type;
  struct routing_src *src_drag;
  struct routing_snk *snk_drag;
  double              drag_x, drag_y;
  GtkWidget          *routing_overlay;
  struct routing_src *hovered_src;
  struct routing_snk *hovered_snk;
  GtkWidget          *hidden_peer_popover;
  int                 pending_ui_updates;
  gboolean            pending_ui_update_idle;
  guint               levels_timer;

  // PCM channel availability based on sample rate
  int                 playback_altset_channels[4]; // channels per altset (1-3)
  int                 capture_altset_channels[4];  // channels per altset (1-3)
  int                 altset_count;                // number of altsets
  int                 pcm_playback_channels;       // current available (0=all)
  int                 pcm_capture_channels;        // current available (0=all)

  // HW I/O availability based on digital I/O mode and sample rate
  struct alsa_elem   *digital_io_mode_elem;  // "Digital I/O Mode", "S/PDIF Mode", or "S/PDIF Source"
  int                 digital_io_mode;       // cached mode index at init time
  int                 digital_io_mode_live;  // mode changes take effect immediately
  int                 current_sample_rate;   // current sample rate (Hz)
  int                 max_spdif_in;          // max S/PDIF input ports
  int                 max_spdif_out;         // max S/PDIF output ports
  int                 max_adat_in;           // max ADAT input ports
  int                 max_adat_out;          // max ADAT output ports

  struct alsa_elem   *mixer_gains[MAX_MIX_OUT][MAX_MUX_IN];

  // level meter support (set at init, not persisted)
  int                 has_levels;

  // per-card preferences (persisted via optional-state [ui] section)
  int                 pref_show_bottom_right_labels;
  int                 pref_levels_interval_ms;

  // levels timer callback data (for restart_levels_timer)
  void               *levels_data;

  // autogain window state (struct autogain_window in window-autogain.c)
  void               *autogain_data;
};

// flags for pending_ui_updates
#define PENDING_UI_UPDATE_MIXER_GRID     (1 << 0)
#define PENDING_UI_UPDATE_MONITOR_GROUPS (1 << 1)

// utility
void fatal_alsa_error(const char *msg, int err);

// locate elements or get information about them
struct alsa_elem *get_elem_by_name(GPtrArray *elems, const char *name);
gboolean alsa_set_elem_by_name(
  struct alsa_card *card, const char *name, const char *value
);
struct alsa_elem *get_elem_by_prefix(GPtrArray *elems, const char *prefix);
struct alsa_elem *get_elem_by_substr(GPtrArray *elems, const char *substr);
int get_max_elem_by_name(
  GPtrArray *elems,
  const char *prefix,
  const char *needle
);
struct alsa_elem *get_hw_vol_elem(struct alsa_card *card, int lr_num);

// add callback to alsa_elem callback list
void alsa_elem_add_callback(
  struct alsa_elem *elem,
  AlsaElemCallback *callback,
  void             *data,
  GDestroyNotify    destroy  // cleanup function, NULL if no cleanup needed
);

/* The element's value in dB, from the element's own dB scaling
 * information. Elements whose scaling is a straight range report a
 * value clamped to it; linear-scaled elements convert through
 * linear_value_to_db().
 */
double alsa_get_elem_dB(struct alsa_elem *elem, int value);

// find the routing source for an analogue input by channel number
struct routing_src *get_analogue_input_src(
  struct alsa_card *card,
  int               lr_num
);

// monitor group queries
int snk_in_monitor_group(struct routing_snk *r_snk, int group);
int snk_in_any_monitor_group(struct routing_snk *r_snk);
int monitor_group_has_outputs(struct alsa_card *card, int group);
int get_active_monitor_group(struct alsa_card *card);

// remove all callbacks with matching data pointer from an element
void alsa_elem_remove_callbacks_by_data(struct alsa_elem *elem, void *data);

// trigger callbacks for an element (notify of value change)
void alsa_elem_change(struct alsa_elem *elem);

// alsa snd_ctl_elem_*() functions
int alsa_get_elem_type(struct alsa_elem *elem);
char *alsa_get_elem_name(struct alsa_elem *elem);
long alsa_get_elem_value(struct alsa_elem *elem);
long *alsa_get_elem_int_values(struct alsa_elem *elem);
void alsa_set_elem_int_values(struct alsa_elem *elem, const long *values, int count);
void alsa_set_elem_value(struct alsa_elem *elem, long value);
int alsa_get_elem_writable(struct alsa_elem *elem);
int alsa_get_elem_volatile(struct alsa_elem *elem);
int alsa_get_elem_count(struct alsa_elem *elem);
int alsa_get_item_count(struct alsa_elem *elem);
char *alsa_get_item_name(struct alsa_elem *elem, int i);

// BYTES element support
const void *alsa_get_elem_bytes(struct alsa_elem *elem, size_t *size);
void alsa_set_elem_bytes(struct alsa_elem *elem, const void *data, size_t size);

/* The firmware version the card is running: a bare number for
 * scarlett2-era devices, dotted components for scarlett4. NULL if the
 * card reports none. Free with g_free().
 */
char *card_firmware_version(struct alsa_card *card);

// The fcp-server version serving a card, or NULL. Free with g_free().
char *card_fcp_server_version(struct alsa_card *card);

// create simulated optional element
struct alsa_elem *alsa_create_optional_elem(
  struct alsa_card *card,
  const char       *name,
  int               type,
  size_t            max_size
);

// create simulated optional enumerated element with item names
struct alsa_elem *alsa_create_optional_enum_elem(
  struct alsa_card *card,
  const char       *name,
  const char      **item_names,
  int               item_count
);

// add to alsa_cards array
struct alsa_card *card_create(int card_num);

/* The array of known cards, as struct alsa_card * entries. Slots
 * belonging to cards that have gone away are cleared in place rather
 * than removed, so entries may be NULL and callers must skip them.
 */
GArray *alsa_get_cards(void);

// parse elements (used by alsa-sim.c)
void alsa_set_lr_nums(struct alsa_card *card);
void alsa_get_routing_controls(struct alsa_card *card);
void alsa_init_mixer_gains_cache(struct alsa_card *card);

// Card lifecycle observer. alsa.c reports what happened to a card and
// leaves the presentation decisions to whoever registered these; it
// never calls into the GUI itself.
//
// ready:    the card's controls are known and it can be presented
// closing:  the card's presentation must be torn down, either because
//           the card is going away (gone) or because it is about to be
//           reported ready again with a different interface (!gone).
//           A card going away is already out of the card list, so an
//           observer enumerating cards sees the set it is left with.
struct alsa_card_lifecycle {
  void (*ready)(struct alsa_card *card);
  void (*closing)(struct alsa_card *card, int gone);
};

void alsa_set_card_lifecycle(const struct alsa_card_lifecycle *lifecycle);

// notify the lifecycle observer that a card can be presented
void alsa_card_ready(struct alsa_card *card);

/* Called whenever a card has been added to or removed from the card
 * list, for anything derived from the set of connected cards rather than
 * from one of them. There may be any number of these, and each is called
 * from the moment it is added: cards are discovered before the
 * presentation observer above exists, so anything shown during startup
 * has to be told this way.
 */
void alsa_add_cards_changed_callback(void (*callback)(void));

// init
void alsa_init(void);

// register re-open callback
typedef void (ReOpenCallback)(void *);
void alsa_register_reopen_callback(
  const char     *serial,
  ReOpenCallback *callback,
  void           *data
);
void alsa_unregister_reopen_callback(const char *serial);
int alsa_has_reopen_callbacks(void);
