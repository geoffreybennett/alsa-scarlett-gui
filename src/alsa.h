// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
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
  // Hardware inputs/outputs
  PC_HW    = 0,

  // Mixer inputs/outputs
  PC_MIX   = 1,

  // DSP inputs/outputs
  PC_DSP   = 2,

  // PCM inputs/outputs
  PC_PCM   = 3,

  // number of port categories
  PC_COUNT = 4
};

// names for the port categories
extern const char *port_category_names[PC_COUNT];

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

  // PC_DSP, PC_MIX, PC_PCM, or PC_HW
  int port_category;

  // 0-based count within port_category
  int port_num;

  // the alsa item name
  char *name;

  // the number (or translated letter; A = 1) in the item name
  int lr_num;

  // on the routing page, the box widget containing the text and the
  // "socket" widget for this routing source
  GtkWidget *widget;

  // the socket widget
  GtkWidget *widget2;
};

// entry in alsa_card routing_snks (routing sinks) array for alsa
// elements that are routing sinks like Analogue Output 01 Playback
// Enum
// port_category is set to PC_DSP, PC_MIX, PC_PCM, PC_HW
// port_num is a count (0-based) within that category
struct routing_snk {

  // location within the array
  int idx;

  // pointer back to the element this entry is associated with
  struct alsa_elem *elem;

  // box widget on the routing page
  GtkWidget *box_widget;

  // socket widget on the routing page
  GtkWidget *socket_widget;

  // PC_DSP, PC_MIX, PC_PCM, or PC_HW
  int port_category;

  // 0-based count within port_category
  int port_num;

  // the mixer label widgets for this sink
  GtkWidget *mixer_label_top;
  GtkWidget *mixer_label_bottom;
};

// hold one callback & its data
struct alsa_elem_callback {
  AlsaElemCallback *callback;
  void             *data;
};

// entry in alsa_card elems (ALSA control elements) array
struct alsa_elem {

  // pointer back to the card
  struct alsa_card *card;

  // ALSA element information
  int         numid;
  const char *name;
  int         type;
  int         count;

  // for gain/volume elements, the dB range and step
  int min_val;
  int max_val;
  int min_dB;
  int max_dB;

  // for the number (or translated letter; A = 1) in the item name
  // TODO: move this to struct routing_snk?
  int lr_num;

  // the callback functions for this ALSA control element
  GList *callbacks;

  // for simulated elements, the current state
  int  is_writable;
  int  is_volatile;
  long value;

  // for simulated enumerated elements, the items
  int    item_count;
  char **item_names;
};

struct alsa_card {
  int                 num;
  char               *device;
  uint32_t            pid;
  char               *serial;
  char               *name;
  int                 best_firmware_version;
  snd_ctl_t          *handle;
  struct pollfd       pfd;
  GArray             *elems;
  struct alsa_elem   *sample_capture_elem;
  GArray             *routing_srcs;
  GArray             *routing_snks;
  GIOChannel         *io_channel;
  guint               event_source_id;
  GtkWidget          *window_main;
  GtkWidget          *window_routing;
  GtkWidget          *window_mixer;
  GtkWidget          *window_levels;
  GtkWidget          *window_startup;
  GtkWidget          *window_modal;
  GtkWidget          *window_main_contents;
  GtkWidget          *routing_grid;
  GtkWidget          *routing_lines;
  GtkWidget          *routing_hw_in_grid;
  GtkWidget          *routing_hw_out_grid;
  GtkWidget          *routing_pcm_in_grid;
  GtkWidget          *routing_pcm_out_grid;
  GtkWidget          *routing_dsp_in_grid;
  GtkWidget          *routing_dsp_out_grid;
  GtkWidget          *routing_mixer_in_grid;
  GtkWidget          *routing_mixer_out_grid;
  int                 has_speaker_switching;
  int                 has_talkback;
  int                 routing_out_count[PC_COUNT];
  int                 routing_in_count[PC_COUNT];
  GMenu              *routing_src_menu;
  GtkWidget          *drag_line;
  int                 drag_type;
  struct routing_src *src_drag;
  struct routing_snk *snk_drag;
  double              drag_x, drag_y;
};

// utility
void fatal_alsa_error(const char *msg, int err);

// locate elements or get information about them
struct alsa_elem *get_elem_by_name(GArray *elems, char *name);
struct alsa_elem *get_elem_by_prefix(GArray *elems, char *prefix);
int get_max_elem_by_name(GArray *elems, char *prefix, char *needle);
int is_elem_routing_snk(struct alsa_elem *elem);

// add callback to alsa_elem callback list
void alsa_elem_add_callback(
  struct alsa_elem *elem,
  AlsaElemCallback *callback,
  void             *data
);

// alsa snd_ctl_elem_*() functions
int alsa_get_elem_type(struct alsa_elem *elem);
char *alsa_get_elem_name(struct alsa_elem *elem);
long alsa_get_elem_value(struct alsa_elem *elem);
int *alsa_get_elem_int_values(struct alsa_elem *elem);
void alsa_set_elem_value(struct alsa_elem *elem, long value);
int alsa_get_elem_writable(struct alsa_elem *elem);
int alsa_get_elem_volatile(struct alsa_elem *elem);
int alsa_get_elem_count(struct alsa_elem *elem);
int alsa_get_item_count(struct alsa_elem *elem);
char *alsa_get_item_name(struct alsa_elem *elem, int i);

// add to alsa_cards array
struct alsa_card *card_create(int card_num);

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
