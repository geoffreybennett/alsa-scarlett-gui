// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <alsa/asoundlib.h>
#include <gtk/gtk.h>

#include "const.h"

// simulated cards have card->num set to -1
#define SIMULATED_CARD_NUM -1

// forward definitions
struct alsa_elem;
struct alsa_card;

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

// names for the port categories
extern const char *port_category_names[PC_COUNT];

// hardware types
enum {
  HW_TYPE_ANALOGUE,
  HW_TYPE_SPDIF,
  HW_TYPE_ADAT,
  HW_TYPE_COUNT
};

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

  // optional/simulated element for custom name
  struct alsa_elem *custom_name_elem;

  // optional/simulated element for enable/disable
  struct alsa_elem *enable_elem;

  // cached display name (either custom or default)
  // updated by callback when custom name changes
  char *display_name;

  // mixer window labels for mixer outputs (Mix A, Mix B, etc.)
  GtkWidget *mixer_label_left;
  GtkWidget *mixer_label_right;
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

  // the mixer label widgets for this sink
  GtkWidget *mixer_label_top;
  GtkWidget *mixer_label_bottom;

  // optional/simulated element for custom name
  struct alsa_elem *custom_name_elem;

  // optional/simulated element for enable/disable
  struct alsa_elem *enable_elem;

  // cached display name (either custom or default)
  // updated by callback when custom name changes
  char *display_name;
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
  long value;

  // for simulated enumerated elements, the items
  int    item_count;
  char **item_names;

  // for BYTES type elements
  void   *bytes_value;
  size_t  bytes_size;
};

struct alsa_card {
  int                 num;
  char               *device;
  uint32_t            pid;
  char               *serial;
  char               *name;
  int                 driver_type;
  char               *fcp_socket;
  int                 best_firmware_version;
  snd_ctl_t          *handle;
  struct pollfd       pfd;
  GPtrArray          *elems;
  struct alsa_elem   *sample_capture_elem;
  GArray             *routing_srcs;
  GArray             *routing_snks;
  GIOChannel         *io_channel;
  guint               event_source_id;
  GtkWidget          *window_main;
  GtkWidget          *window_routing;
  GtkWidget          *window_mixer;
  GtkWidget          *window_levels;
  GtkWidget          *window_configuration;
  GtkWidget          *window_startup;
  GtkWidget          *window_modal;
  GtkWidget          *window_main_contents;
  GtkWidget          *routing_grid;
  GtkWidget          *mixer_grid;
  GList              *mixer_gain_widgets;
  GtkWidget          *routing_lines;
  GtkWidget          *routing_hw_in_grid;
  GtkWidget          *routing_hw_out_grid;
  GtkWidget          *routing_pcm_in_grid;
  GtkWidget          *routing_pcm_out_grid;
  GtkWidget          *routing_dsp_in_grid;
  GtkWidget          *routing_dsp_out_grid;
  GtkWidget          *routing_mixer_in_grid;
  GtkWidget          *routing_mixer_out_grid;
  GtkWidget          *routing_src_label;
  GtkWidget          *routing_snk_label;
  int                 has_speaker_switching;
  int                 has_talkback;
  int                 has_fixed_mixer_inputs;
  int                 routing_out_count[PC_COUNT];
  int                 routing_in_count[PC_COUNT];
  GMenu              *routing_src_menu;
  GtkWidget          *drag_line;
  int                 drag_type;
  struct routing_src *src_drag;
  struct routing_snk *snk_drag;
  double              drag_x, drag_y;
  int                 pending_ui_updates;
  guint               pending_ui_update_timeout;
};

// flags for pending_ui_updates
#define PENDING_UI_UPDATE_ROUTING    (1 << 0)
#define PENDING_UI_UPDATE_MIXER_GRID (1 << 1)

// utility
void fatal_alsa_error(const char *msg, int err);

// locate elements or get information about them
struct alsa_elem *get_elem_by_name(GPtrArray *elems, const char *name);
struct alsa_elem *get_elem_by_prefix(GPtrArray *elems, const char *prefix);
struct alsa_elem *get_elem_by_substr(GPtrArray *elems, const char *substr);
int get_max_elem_by_name(
  GPtrArray *elems,
  const char *prefix,
  const char *needle
);

// add callback to alsa_elem callback list
void alsa_elem_add_callback(
  struct alsa_elem *elem,
  AlsaElemCallback *callback,
  void             *data,
  GDestroyNotify    destroy  // cleanup function, NULL if no cleanup needed
);

// trigger callbacks for an element (notify of value change)
void alsa_elem_change(struct alsa_elem *elem);

// alsa snd_ctl_elem_*() functions
int alsa_get_elem_type(struct alsa_elem *elem);
char *alsa_get_elem_name(struct alsa_elem *elem);
long alsa_get_elem_value(struct alsa_elem *elem);
long *alsa_get_elem_int_values(struct alsa_elem *elem);
void alsa_set_elem_value(struct alsa_elem *elem, long value);
int alsa_get_elem_writable(struct alsa_elem *elem);
int alsa_get_elem_volatile(struct alsa_elem *elem);
int alsa_get_elem_count(struct alsa_elem *elem);
int alsa_get_item_count(struct alsa_elem *elem);
char *alsa_get_item_name(struct alsa_elem *elem, int i);

// BYTES element support
const void *alsa_get_elem_bytes(struct alsa_elem *elem, size_t *size);
void alsa_set_elem_bytes(struct alsa_elem *elem, const void *data, size_t size);

// create simulated optional element
struct alsa_elem *alsa_create_optional_elem(
  struct alsa_card *card,
  const char       *name,
  int               type,
  size_t            max_size
);

// add to alsa_cards array
struct alsa_card *card_create(int card_num);

// parse elements (used by alsa-sim.c)
void alsa_set_lr_nums(struct alsa_card *card);
void alsa_get_routing_controls(struct alsa_card *card);

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
