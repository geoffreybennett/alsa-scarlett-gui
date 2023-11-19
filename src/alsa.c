// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/inotify.h>

#include "alsa.h"
#include "stringhelper.h"
#include "window-iface.h"

// names for the port categories
const char *port_category_names[PC_COUNT] = {
  "Hardware Outputs",
  "Mixer Inputs",
  "PCM Inputs"
};

// global array of cards
GArray *alsa_cards;

// static fd and wd for ALSA inotify
static int inotify_fd, inotify_wd;

// forward declaration
static void alsa_elem_change(struct alsa_elem *elem);

void fatal_alsa_error(const char *msg, int err) {
  fprintf(stderr, "%s: %s\n", msg, snd_strerror(err));
  exit(1);
}

//
// functions to locate elements or get information about them
//

// return the element with the exact matching name
struct alsa_elem *get_elem_by_name(GArray *elems, char *name) {
  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = &g_array_index(elems, struct alsa_elem, i);

    if (!elem->card)
      continue;

    if (strcmp(elem->name, name) == 0)
      return elem;
  }

  return NULL;
}

// return the first element with a name starting with the given prefix
struct alsa_elem *get_elem_by_prefix(GArray *elems, char *prefix) {
  int prefix_len = strlen(prefix);

  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = &g_array_index(elems, struct alsa_elem, i);

    if (!elem->card)
      continue;

    if (strncmp(elem->name, prefix, prefix_len) == 0)
      return elem;
  }

  return NULL;
}

// find the maximum number in the matching elements
// search by element name prefix and substring
// e.g. get_max_elem_by_name(elems, "Line", "Pad Capture Switch")
// will return 8 when the last pad capture switch is
// "Line In 8 Pad Capture Switch"
int get_max_elem_by_name(GArray *elems, char *prefix, char *needle) {
  int max = 0;
  int l = strlen(prefix);

  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = &g_array_index(elems, struct alsa_elem, i);
    int num;

    if (!elem->card)
      continue;

    if (strncmp(elem->name, prefix, l) != 0)
      continue;

    if (!strstr(elem->name, needle))
      continue;

    num = get_num_from_string(elem->name);
    if (num > max)
      max = num;
  }

  return max;
}

// return true if the element is an routing sink enum, e.g.:
// PCM xx Capture Enum
// Mixer Input xx Capture Enum
// Analogue Output xx Playback Enum
// S/PDIF Output xx Playback Enum
// ADAT Output xx Playback Enum
int is_elem_routing_snk(struct alsa_elem *elem) {
  if (strstr(elem->name, "Capture Enum") &&
      !strstr(elem->name, "Level"))
    return 1;
  if (strstr(elem->name, "Output") &&
      strstr(elem->name, "Playback Enum"))
    return 1;
  return 0;
}

//
// alsa snd_ctl_elem_*() mediation functions
// for simulated elements, fake the ALSA element
// for real elements, pass through to snd_ctl_elem*()
//

// get the element type
int alsa_get_elem_type(struct alsa_elem *elem) {
  snd_ctl_elem_info_t *elem_info;

  snd_ctl_elem_info_alloca(&elem_info);
  snd_ctl_elem_info_set_numid(elem_info, elem->numid);
  snd_ctl_elem_info(elem->card->handle, elem_info);

  return snd_ctl_elem_info_get_type(elem_info);
}

// get the element name
char *alsa_get_elem_name(struct alsa_elem *elem) {
  snd_ctl_elem_info_t *elem_info;

  snd_ctl_elem_info_alloca(&elem_info);
  snd_ctl_elem_info_set_numid(elem_info, elem->numid);
  snd_ctl_elem_info(elem->card->handle, elem_info);

  const char *name = snd_ctl_elem_info_get_name(elem_info);
  return strdup(name);
}

// get the element value
// boolean, enum, or int all returned as long ints
long alsa_get_elem_value(struct alsa_elem *elem) {
  if (elem->card->num == SIMULATED_CARD_NUM)
    return elem->value;

  snd_ctl_elem_value_t *elem_value;

  snd_ctl_elem_value_alloca(&elem_value);
  snd_ctl_elem_value_set_numid(elem_value, elem->numid);
  snd_ctl_elem_read(elem->card->handle, elem_value);

  int type = elem->type;
  if (type == SND_CTL_ELEM_TYPE_BOOLEAN) {
    return snd_ctl_elem_value_get_boolean(elem_value, 0);
  } else if (type == SND_CTL_ELEM_TYPE_ENUMERATED) {
    return snd_ctl_elem_value_get_enumerated(elem_value, 0);
  } else if (type == SND_CTL_ELEM_TYPE_INTEGER) {
    return snd_ctl_elem_value_get_integer(elem_value, 0);
  } else {
    fprintf(
      stderr,
      "internal error: elem %s (%d) type %d not bool/enum/int\n",
      elem->name,
      elem->numid,
      elem->type
    );
    return 0;
  }
}

// for elements with multiple int values, return all the values
// the int array returned needs to be freed by the caller
int *alsa_get_elem_int_values(struct alsa_elem *elem) {
  int *values = calloc(elem->count, sizeof(int));

  if (elem->card->num == SIMULATED_CARD_NUM) {
    for (int i = 0; i < elem->count; i++)
      values[i] = 0;
    return values;
  }

  snd_ctl_elem_value_t *elem_value;

  snd_ctl_elem_value_alloca(&elem_value);
  snd_ctl_elem_value_set_numid(elem_value, elem->numid);
  snd_ctl_elem_read(elem->card->handle, elem_value);

  for (int i = 0; i < elem->count; i++)
    values[i] = snd_ctl_elem_value_get_integer(elem_value, i);

  return values;
}

// set the element value
// boolean, enum, or int all set from long ints
void alsa_set_elem_value(struct alsa_elem *elem, long value) {
  if (elem->card->num == SIMULATED_CARD_NUM) {
    if (elem->value != value) {
      elem->value = value;
      alsa_elem_change(elem);
    }
    return;
  }

  snd_ctl_elem_value_t *elem_value;

  snd_ctl_elem_value_alloca(&elem_value);
  snd_ctl_elem_value_set_numid(elem_value, elem->numid);

  int type = elem->type;
  if (type == SND_CTL_ELEM_TYPE_BOOLEAN) {
    snd_ctl_elem_value_set_boolean(elem_value, 0, value);
  } else if (type == SND_CTL_ELEM_TYPE_ENUMERATED) {
    snd_ctl_elem_value_set_enumerated(elem_value, 0, value);
  } else if (type == SND_CTL_ELEM_TYPE_INTEGER) {
    snd_ctl_elem_value_set_integer(elem_value, 0, value);
  } else {
    fprintf(
      stderr,
      "internal error: elem %s (%d) type %d not bool/enum/int\n",
      elem->name,
      elem->numid,
      elem->type
    );
    return;
  }

  snd_ctl_elem_write(elem->card->handle, elem_value);
}

// return whether the element can be modified (is writable)
int alsa_get_elem_writable(struct alsa_elem *elem) {
  if (elem->card->num == SIMULATED_CARD_NUM)
    return elem->writable;

  snd_ctl_elem_info_t *elem_info;

  snd_ctl_elem_info_alloca(&elem_info);
  snd_ctl_elem_info_set_numid(elem_info, elem->numid);
  snd_ctl_elem_info(elem->card->handle, elem_info);

  return snd_ctl_elem_info_is_writable(elem_info);
}

// get the number of values this element has
// (most are just 1; the levels element is the exception)
int alsa_get_elem_count(struct alsa_elem *elem) {
  snd_ctl_elem_info_t *elem_info;

  snd_ctl_elem_info_alloca(&elem_info);
  snd_ctl_elem_info_set_numid(elem_info, elem->numid);
  snd_ctl_elem_info(elem->card->handle, elem_info);

  return snd_ctl_elem_info_get_count(elem_info);
}

// get the number of items this enum element has
int alsa_get_item_count(struct alsa_elem *elem) {
  if (elem->card->num == SIMULATED_CARD_NUM)
    return elem->item_count;

  snd_ctl_elem_info_t *elem_info;

  snd_ctl_elem_info_alloca(&elem_info);
  snd_ctl_elem_info_set_numid(elem_info, elem->numid);
  snd_ctl_elem_info(elem->card->handle, elem_info);

  return snd_ctl_elem_info_get_items(elem_info);
}

// get the name of an item of the given enum element
char *alsa_get_item_name(struct alsa_elem *elem, int i) {
  if (elem->card->num == SIMULATED_CARD_NUM)
    return elem->item_names[i];

  snd_ctl_elem_info_t *elem_info;

  snd_ctl_elem_info_alloca(&elem_info);
  snd_ctl_elem_info_set_numid(elem_info, elem->numid);
  snd_ctl_elem_info_set_item(elem_info, i);
  snd_ctl_elem_info(elem->card->handle, elem_info);

  const char *name = snd_ctl_elem_info_get_item_name(elem_info);
  return strdup(name);
}

//
// create/destroy alsa cards
//

// scan the ALSA ctl element list container and put the useful
// elements into the cards->elems array of struct alsa_elem
static void alsa_get_elem_list(struct alsa_card *card) {
  snd_ctl_elem_list_t *list;
  int count;

  // get the list from ALSA
  snd_ctl_elem_list_malloc(&list);
  snd_ctl_elem_list(card->handle, list);
  count = snd_ctl_elem_list_get_count(list);
  snd_ctl_elem_list_alloc_space(list, count);
  snd_ctl_elem_list(card->handle, list);

  // for each element in the list
  for (int i = 0; i < count; i++) {

    // allocate a temporary struct alsa_elem (will be copied later if
    // we want to keep it)
    struct alsa_elem alsa_elem = {};

    // keep a reference to the card in the element
    alsa_elem.card = card;

    // get the control's numeric identifier (different to the index
    // into this array)
    alsa_elem.numid = snd_ctl_elem_list_get_numid(list, i);

    // get the control's info
    alsa_elem.type = alsa_get_elem_type(&alsa_elem);
    alsa_elem.name = alsa_get_elem_name(&alsa_elem);
    alsa_elem.count = alsa_get_elem_count(&alsa_elem);

    switch (alsa_elem.type) {
      case SND_CTL_ELEM_TYPE_BOOLEAN:
      case SND_CTL_ELEM_TYPE_ENUMERATED:
      case SND_CTL_ELEM_TYPE_INTEGER:
        break;
      default:
        continue;
    }

    if (strstr(alsa_elem.name, "Validity"))
      continue;
    if (strstr(alsa_elem.name, "Channel Map"))
      continue;

    if (card->elems->len <= alsa_elem.numid)
      g_array_set_size(card->elems, alsa_elem.numid + 1);
    g_array_index(card->elems, struct alsa_elem, alsa_elem.numid) = alsa_elem;
  }

  // free the ALSA list
  snd_ctl_elem_list_free_space(list);
  snd_ctl_elem_list_free(list);
}

static void alsa_elem_change(struct alsa_elem *elem) {
  if (!elem->widget)
    return;
  if (!elem->widget_callback)
    return;
  elem->widget_callback(elem);
}

static gboolean alsa_card_callback(
  GIOChannel    *source,
  GIOCondition   condition,
  void          *data
) {
  struct alsa_card *card = data;
  snd_ctl_event_t *event;
  unsigned int mask;
  int err, numid;
  struct alsa_elem *elem;

  snd_ctl_event_alloca(&event);
  if (!card->handle) {
    printf("oops, no card handle??\n");
    return 0;
  }
  err = snd_ctl_read(card->handle, event);
  if (err == 0) {
    printf("alsa_card_callback nothing to read??\n");
    return 0;
  }
  if (err < 0) {
    if (err == -ENODEV)
      return 0;
    printf("card_callback_error %d\n", err);
    exit(1);
  }
  if (snd_ctl_event_get_type(event) != SND_CTL_EVENT_ELEM)
    return 1;

  numid = snd_ctl_event_elem_get_numid(event);
  elem = &g_array_index(card->elems, struct alsa_elem, numid);
  if (elem->numid != numid)
    return 1;

  mask = snd_ctl_event_elem_get_mask(event);

  if (mask & (SND_CTL_EVENT_MASK_VALUE | SND_CTL_EVENT_MASK_INFO))
    alsa_elem_change(elem);

  return 1;
}

// go through the alsa_cards array and look for an entry with the
// matching card_num
static struct alsa_card *find_card_by_card_num(int card_num) {
  for (int i = 0; i < alsa_cards->len; i++) {
    struct alsa_card **card_ptr =
      &g_array_index(alsa_cards, struct alsa_card *, i);
    if (!*card_ptr)
      continue;
    if ((*card_ptr)->num == card_num)
      return *card_ptr;
  }

  return NULL;
}

// create a new entry in the alsa_cards array (either an unused entry
// or add a new entry to the end)
struct alsa_card *card_create(int card_num) {
  int i, found = 0;
  struct alsa_card **card_ptr;

  // look for an unused entry
  for (i = 0; i < alsa_cards->len; i++) {
    card_ptr = &g_array_index(alsa_cards, struct alsa_card *, i);
    if (!*card_ptr) {
      found = 1;
      break;
    }
  }

  // no unused entry? extend the array
  if (!found) {
    g_array_set_size(alsa_cards, i + 1);
    card_ptr = &g_array_index(alsa_cards, struct alsa_card *, i);
  }

  *card_ptr = calloc(1, sizeof(struct alsa_card));
  struct alsa_card *card = *card_ptr;
  card->num = card_num;
  card->elems = g_array_new(FALSE, TRUE, sizeof(struct alsa_elem));

  return card;
}

static void card_destroy_callback(void *data) {
  struct alsa_card *card = data;

  // close the windows associated with this card
  destroy_card_window(card);

  // TODO: there is more to free
  free(card->device);
  free(card->name);
  free(card);

  // go through the alsa_cards array and clear the entry for this card
  for (int i = 0; i < alsa_cards->len; i++) {
    struct alsa_card **card_ptr =
      &g_array_index(alsa_cards, struct alsa_card *, i);
    if (*card_ptr == card)
      *card_ptr = NULL;
  }
}

static void alsa_add_card_callback(struct alsa_card *card) {
  card->io_channel = g_io_channel_unix_new(card->pfd.fd);
  card->event_source_id = g_io_add_watch_full(
    card->io_channel, 0,
    G_IO_IN | G_IO_ERR | G_IO_HUP,
    alsa_card_callback, card, card_destroy_callback
  );
}

static void alsa_get_firmware_version(struct alsa_card *card) {
  struct alsa_elem *elem = get_elem_by_name(card->elems, "Firmware Version");

  if (!elem)
    return;
  card->firmware_version = alsa_get_elem_value(elem);
}

static void alsa_subscribe(struct alsa_card *card) {
  int count = snd_ctl_poll_descriptors_count(card->handle);

  if (count != 1) {
    printf("poll descriptors %d != 1", count);
    exit(1);
  }
  snd_ctl_subscribe_events(card->handle, 1);
  snd_ctl_poll_descriptors(card->handle, &card->pfd, 1);
}

void alsa_scan_cards(void) {
  snd_ctl_card_info_t *info;
  snd_ctl_t           *ctl;
  int                  card_num = -1;
  char                 device[32];
  struct alsa_card    *card;

  snd_ctl_card_info_alloca(&info);
  while (1) {
    int err = snd_card_next(&card_num);
    if (err < 0)
      fatal_alsa_error("snd_card_next", err);
    if (card_num < 0)
      break;

    snprintf(device, 32, "hw:%d", card_num);

    err = snd_ctl_open(&ctl, device, 0);
    if (err < 0)
      goto next;
    err = snd_ctl_card_info(ctl, info);
    if (err < 0)
      goto next;

    if (strncmp(snd_ctl_card_info_get_name(info), "Scarlett", 8) != 0 &&
        strncmp(snd_ctl_card_info_get_name(info), "Clarett", 7) != 0)
      goto next;

    // is there already an entry for this card in alsa_cards?
    card = find_card_by_card_num(card_num);

    // yes: skip
    if (card)
      goto next;

    // no: create
    card = card_create(card_num);

    card->device = strdup(device);
    card->name = strdup(snd_ctl_card_info_get_name(info));
    card->handle = ctl;

    alsa_get_elem_list(card);
    alsa_get_firmware_version(card);
    alsa_subscribe(card);

    create_card_window(card);
    alsa_add_card_callback(card);

    continue;

  next:
    snd_ctl_close(ctl);
  }
}

// inotify

static gboolean inotify_callback(
  GIOChannel    *source,
  GIOCondition  condition,
  void          *data
) {
  char buf[4096] __attribute__ ((aligned(__alignof__(struct inotify_event))));
  const struct inotify_event *event;
  int len;

  len = read(inotify_fd, &buf, sizeof(buf));
  if (len < 0) {
    perror("inotify read");
    exit(1);
  }

  for (
    event = (struct inotify_event *)buf;
    (char *)event < buf + len;
    event++
  ) {
    if (event->mask & IN_CREATE &&
        len &&
        strncmp(event->name, "control", 7) == 0) {

      // can't rescan for new cards too fast
      sleep(1);
      alsa_scan_cards();
    }
  }

  return TRUE;
}

void alsa_inotify_init(void) {
  GIOChannel *io_channel;

  inotify_fd = inotify_init();
  inotify_wd = inotify_add_watch(inotify_fd, "/dev/snd", IN_CREATE);
  io_channel = g_io_channel_unix_new(inotify_fd);
  g_io_add_watch_full(
    io_channel, 0,
    G_IO_IN | G_IO_ERR | G_IO_HUP,
    inotify_callback, NULL, NULL
  );
}
