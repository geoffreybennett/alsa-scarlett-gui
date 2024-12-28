// SPDX-FileCopyrightText: 2022-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/inotify.h>

#include "alsa.h"
#include "scarlett2-firmware.h"
#include "stringhelper.h"
#include "window-iface.h"

#define MAX_TLV_RANGE_SIZE 256

// names for the port categories
const char *port_category_names[PC_COUNT] = {
  NULL,
  "Hardware Outputs",
  "Mixer Inputs",
  "DSP Inputs",
  "PCM Inputs"
};

// names for the hardware types
const char *hw_type_names[HW_TYPE_COUNT] = {
  "Analogue",
  "S/PDIF",
  "ADAT"
};

// global array of cards
static GArray *alsa_cards;

// static fd and wd for ALSA inotify
static int inotify_fd, inotify_wd;

struct reopen_callback {
  ReOpenCallback *callback;
  void           *data;
};

// hash table for cards being rebooted
GHashTable *reopen_callbacks;

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
struct alsa_elem *get_elem_by_name(GArray *elems, const char *name) {
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
struct alsa_elem *get_elem_by_prefix(GArray *elems, const char *prefix) {
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

// return the first element with a name containing the given substring
struct alsa_elem *get_elem_by_substr(GArray *elems, const char *substr) {
  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = &g_array_index(elems, struct alsa_elem, i);

    if (!elem->card)
      continue;

    if (strstr(elem->name, substr))
      return elem;
  }

  return NULL;
}

// find the maximum number in the matching elements
// search by element name prefix and substring
// e.g. get_max_elem_by_name(elems, "Line", "Pad Capture Switch")
// will return 8 when the last pad capture switch is
// "Line In 8 Pad Capture Switch"
int get_max_elem_by_name(
  GArray *elems,
  const char *prefix,
  const char *needle
) {
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

// add a callback to the list of callbacks for this element
void alsa_elem_add_callback(
  struct alsa_elem *elem,
  AlsaElemCallback *callback,
  void             *data
) {
  struct alsa_elem_callback *cb = calloc(1, sizeof(struct alsa_elem_callback));

  cb->callback = callback;
  cb->data = data;

  elem->callbacks = g_list_append(elem->callbacks, cb);
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
    return snd_ctl_elem_value_get_boolean(elem_value, elem->index);
  } else if (type == SND_CTL_ELEM_TYPE_ENUMERATED) {
    return snd_ctl_elem_value_get_enumerated(elem_value, elem->index);
  } else if (type == SND_CTL_ELEM_TYPE_INTEGER) {
    return snd_ctl_elem_value_get_integer(elem_value, elem->index);
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
  snd_ctl_elem_read(elem->card->handle, elem_value);

  int type = elem->type;
  if (type == SND_CTL_ELEM_TYPE_BOOLEAN) {
    snd_ctl_elem_value_set_boolean(elem_value, elem->index, value);
  } else if (type == SND_CTL_ELEM_TYPE_ENUMERATED) {
    snd_ctl_elem_value_set_enumerated(elem_value, elem->index, value);
  } else if (type == SND_CTL_ELEM_TYPE_INTEGER) {
    snd_ctl_elem_value_set_integer(elem_value, elem->index, value);
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
    return elem->is_writable;

  snd_ctl_elem_info_t *elem_info;

  snd_ctl_elem_info_alloca(&elem_info);
  snd_ctl_elem_info_set_numid(elem_info, elem->numid);
  snd_ctl_elem_info(elem->card->handle, elem_info);

  return snd_ctl_elem_info_is_writable(elem_info) &&
         !snd_ctl_elem_info_is_locked(elem_info);
}

// return whether the element is volatile (can change without
// notification)
int alsa_get_elem_volatile(struct alsa_elem *elem) {
  if (elem->card->num == SIMULATED_CARD_NUM)
    return elem->is_volatile;

  snd_ctl_elem_info_t *elem_info;

  snd_ctl_elem_info_alloca(&elem_info);
  snd_ctl_elem_info_set_numid(elem_info, elem->numid);
  snd_ctl_elem_info(elem->card->handle, elem_info);

  return snd_ctl_elem_info_is_volatile(elem_info);
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

static void alsa_get_elem_tlv(struct alsa_elem *elem) {
  if (elem->type != SND_CTL_ELEM_TYPE_INTEGER)
    return;

  snd_ctl_elem_info_t *elem_info;

  snd_ctl_elem_info_alloca(&elem_info);
  snd_ctl_elem_info_set_numid(elem_info, elem->numid);
  snd_ctl_elem_info(elem->card->handle, elem_info);

  if (!snd_ctl_elem_info_is_tlv_readable(elem_info))
    return;

  snd_ctl_elem_id_t *elem_id;
  unsigned int tlv[MAX_TLV_RANGE_SIZE];
  unsigned int *dbrec;
  int ret;
  long min_dB, max_dB;

  snd_ctl_elem_id_alloca(&elem_id);
  snd_ctl_elem_id_set_numid(elem_id, elem->numid);

  ret = snd_ctl_elem_tlv_read(
    elem->card->handle, elem_id, tlv, sizeof(tlv)
  );
  if (ret < 0) {
    fprintf(stderr, "TLV read error %d\n", ret);
    return;
  }

  ret = snd_tlv_parse_dB_info(tlv, sizeof(tlv), &dbrec);
  if (ret <= 0) {
    fprintf(stderr, "TLV parse error %d\n", ret);
    return;
  }

  int min_val = snd_ctl_elem_info_get_min(elem_info);
  int max_val = snd_ctl_elem_info_get_max(elem_info);

  ret = snd_tlv_get_dB_range(tlv, min_val, max_val, &min_dB, &max_dB);
  if (ret != 0) {
    fprintf(stderr, "TLV range error %d\n", ret);
    return;
  }

  elem->min_val = min_val;
  elem->max_val = max_val;
  elem->min_dB = min_dB / 100;
  elem->max_dB = max_dB / 100;
}

static void alsa_get_elem(struct alsa_card *card, int numid) {
  // allocate a temporary struct alsa_elem (will be copied later if
  // we want to keep it)
  struct alsa_elem alsa_elem = {};

  // keep a reference to the card in the element
  alsa_elem.card = card;

  // get the control's numeric identifier (different to the index
  // into this array)
  alsa_elem.numid = numid;

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
      return;
  }

  if (strstr(alsa_elem.name, "Validity"))
    return;
  if (strstr(alsa_elem.name, "Channel Map"))
    return;

  alsa_get_elem_tlv(&alsa_elem);

  // Scarlett 1st Gen driver puts two volume controls/mutes in the
  // same element, so split them out to match the other series
  int count = alsa_elem.count;

  if (strcmp(alsa_elem.name, "Level Meter") == 0)
    count = 1;

  if (count > 2) {
    fprintf(stderr, "element %s has count %d\n", alsa_elem.name, count);
    count = 1;
  }

  for (int i = 0; i < count; i++, alsa_elem.lr_num++) {
    alsa_elem.index = i;

    int array_len = card->elems->len;
    g_array_set_size(card->elems, array_len + 1);
    g_array_index(card->elems, struct alsa_elem, array_len) = alsa_elem;
  }
}

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
    int numid = snd_ctl_elem_list_get_numid(list, i);
    alsa_get_elem(card, numid);
  }

  // free the ALSA list
  snd_ctl_elem_list_free_space(list);
  snd_ctl_elem_list_free(list);
}

static void alsa_set_elem_lr_num(struct alsa_elem *elem) {
  const char *name = elem->name;
  char side;

  if (strncmp(name, "Master Playback", 15) == 0 ||
      strncmp(name, "Master HW Playback", 18) == 0)
    elem->lr_num = 0;

  else if (strncmp(name, "Master", 6) == 0)
    if (sscanf(name, "Master %d%c", &elem->lr_num, &side) != 2)
      printf("can't parse Master '%s'\n", name);
    else
      elem->lr_num = elem->lr_num * 2
                     - (side == 'L' || side == ' ')
                     + elem->index;

  else
    elem->lr_num = get_num_from_string(name);
}

void alsa_set_lr_nums(struct alsa_card *card) {
  for (int i = 0; i < card->elems->len; i++) {
    struct alsa_elem *elem = &g_array_index(card->elems, struct alsa_elem, i);

    alsa_set_elem_lr_num(elem);
  }
}

static void get_routing_srcs(struct alsa_card *card) {
  struct alsa_elem *elem = card->sample_capture_elem;

  int count = alsa_get_item_count(elem);
  card->routing_srcs = g_array_new(
    FALSE, TRUE, sizeof(struct routing_src)
  );
  g_array_set_size(card->routing_srcs, count);

  for (int i = 0; i < count; i++) {
    char *name = alsa_get_item_name(elem, i);

    struct routing_src *r = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    r->card = card;
    r->id = i;

    if (strcmp(name, "Off") == 0)
      r->port_category = PC_OFF;
    else if (strncmp(name, "Mix", 3) == 0)
      r->port_category = PC_MIX;
    else if (strncmp(name, "DSP", 3) == 0)
      r->port_category = PC_DSP;
    else if (strncmp(name, "PCM", 3) == 0)
      r->port_category = PC_PCM;
    else {
      r->port_category = PC_HW;

      if (strncmp(name, "Analog", 6) == 0)
        r->hw_type = HW_TYPE_ANALOGUE;
      else if (strncmp(name, "S/PDIF", 6) == 0)
        r->hw_type = HW_TYPE_SPDIF;
      else if (strncmp(name, "SPDIF", 5) == 0)
        r->hw_type = HW_TYPE_SPDIF;
      else if (strncmp(name, "ADAT", 4) == 0)
        r->hw_type = HW_TYPE_ADAT;
    }

    r->name = name;
    r->lr_num =
      r->port_category == PC_MIX
        ? name[4] - 'A' + 1
        : get_num_from_string(name);

    r->port_num = card->routing_in_count[r->port_category]++;
  }

  assert(card->routing_in_count[PC_MIX] <= MAX_MIX_OUT);
}

// return true if the element is an routing sink enum, e.g.:
// PCM xx Capture Enum
// Mixer Input xx Capture Enum
// Analogue Output xx Playback Enum
// S/PDIF Output xx Playback Enum
// ADAT Output xx Playback Enum
static int is_elem_routing_snk(struct alsa_elem *elem) {
  if (strstr(elem->name, "Capture Route") ||
      strstr(elem->name, "Input Playback Route") ||
      strstr(elem->name, "Source Playback Enu"))
    return 1;

  if (strstr(elem->name, "Capture Enum") && (
       strncmp(elem->name, "PCM ", 4) == 0 ||
       strncmp(elem->name, "Mixer Input ", 12) == 0 ||
       strncmp(elem->name, "DSP Input ", 10) == 0
     ))
    return 1;

  if (strstr(elem->name, "Output") &&
      strstr(elem->name, "Playback Enum"))
    return 1;

  return 0;
}

static void get_routing_snks(struct alsa_card *card) {
  GArray *elems = card->elems;

  int count = 0;

  // count and label routing snks
  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = &g_array_index(elems, struct alsa_elem, i);

    if (!elem->card)
      continue;

    if (!is_elem_routing_snk(elem))
      continue;

    elem->is_routing_snk = 1;

    if (strncmp(elem->name, "Mixer Input", 11) == 0 ||
        strncmp(elem->name, "Matrix", 6) == 0) {
      elem->port_category = PC_MIX;
    } else if (strncmp(elem->name, "DSP Input", 9) == 0) {
      elem->port_category = PC_DSP;
    } else if (strncmp(elem->name, "PCM", 3) == 0 ||
               strncmp(elem->name, "Input Source", 12) == 0) {
      elem->port_category = PC_PCM;
    } else if (strstr(elem->name, "Playback Enu")) {
      elem->port_category = PC_HW;

      if (strncmp(elem->name, "Analog", 6) == 0)
        elem->hw_type = HW_TYPE_ANALOGUE;
      else if (strncmp(elem->name, "S/PDIF", 6) == 0 ||
               strstr(elem->name, "SPDIF"))
        elem->hw_type = HW_TYPE_SPDIF;
      else if (strstr(elem->name, "ADAT"))
        elem->hw_type = HW_TYPE_ADAT;
    } else {
      printf("unknown mixer routing elem %s\n", elem->name);
      continue;
    }

    if (elem->lr_num <= 0) {
      fprintf(stderr, "routing sink %s had no number\n", elem->name);
      continue;
    }

    count++;
  }

  // create an array of routing snks pointing to those elements
  card->routing_snks = g_array_new(
    FALSE, TRUE, sizeof(struct routing_snk)
  );
  g_array_set_size(card->routing_snks, count);

  // count through card->routing_snks
  int j = 0;

  for (int i = 0; i < elems->len; i++) {
    struct alsa_elem *elem = &g_array_index(elems, struct alsa_elem, i);

    if (!elem->is_routing_snk)
      continue;

    struct routing_snk *r = &g_array_index(
      card->routing_snks, struct routing_snk, j
    );
    r->idx = j;
    j++;
    r->elem = elem;
    elem->port_num = card->routing_out_count[elem->port_category]++;
  }

  assert(j == count);
}

void alsa_get_routing_controls(struct alsa_card *card) {

  // check that we can find a routing control
  card->sample_capture_elem =
    get_elem_by_name(card->elems, "PCM 01 Capture Enum");
  if (!card->sample_capture_elem) {
    card->sample_capture_elem =
      get_elem_by_name(card->elems, "Input Source 01 Capture Route");
  }

  if (!card->sample_capture_elem) {
    fprintf(
      stderr,
      "can't find routing control PCM 01 Capture Enum or "
      "Input Source 01 Capture Route\n"
    );

    return;
  }

  get_routing_srcs(card);
  get_routing_snks(card);
}

static void alsa_elem_change(struct alsa_elem *elem) {
  if (!elem || !elem->callbacks)
    return;

  for (GList *l = elem->callbacks; l; l = l->next) {
    struct alsa_elem_callback *cb = (struct alsa_elem_callback *)l->data;

    if (!cb || !cb->callback)
      continue;

    cb->callback(elem, cb->data);
  }
}

static gboolean alsa_card_callback(
  GIOChannel    *source,
  GIOCondition   condition,
  void          *data
) {
  struct alsa_card *card = data;
  snd_ctl_event_t *event;

  snd_ctl_event_alloca(&event);
  if (!card->handle) {
    printf("oops, no card handle??\n");
    return 0;
  }
  int err = snd_ctl_read(card->handle, event);
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

  int numid = snd_ctl_event_elem_get_numid(event);
  unsigned int mask = snd_ctl_event_elem_get_mask(event);

  if (!(mask & (SND_CTL_EVENT_MASK_VALUE | SND_CTL_EVENT_MASK_INFO)))
    return 1;

  for (int i = 0; i < card->elems->len; i++) {
    struct alsa_elem *elem = &g_array_index(card->elems, struct alsa_elem, i);

    if (elem->numid == numid)
      alsa_elem_change(elem);
  }

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
  free(card->serial);
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

static void alsa_subscribe(struct alsa_card *card) {
  int count = snd_ctl_poll_descriptors_count(card->handle);

  if (count != 1) {
    printf("poll descriptors %d != 1", count);
    exit(1);
  }
  snd_ctl_subscribe_events(card->handle, 1);
  snd_ctl_poll_descriptors(card->handle, &card->pfd, 1);
}

static void alsa_get_usbid(struct alsa_card *card) {
  char path[256];
  snprintf(path, 256, "/proc/asound/card%d/usbid", card->num);

  FILE *f = fopen(path, "r");
  if (!f) {
    fprintf(stderr, "can't open %s: %s\n", path, strerror(errno));
    return;
  }

  int vid, pid;
  int result = fscanf(f, "%04x:%04x", &vid, &pid);
  fclose(f);

  if (result != 2) {
    fprintf(stderr, "can't read %s\n", path);
    return;
  }

  if (vid != 0x1235) {
    fprintf(stderr, "VID %04x != expected 0x1235 for Focusrite\n", vid);
    return;
  }

  card->pid = pid;
}

// get the bus and device numbers from /proc/asound/cardxx/usbbus
// format is XXX/YYY
static int alsa_get_usbbus(struct alsa_card *card, int *bus, int *dev) {
  char path[256];
  snprintf(path, 256, "/proc/asound/card%d/usbbus", card->num);
  FILE *f = fopen(path, "r");
  if (!f) {
    fprintf(stderr, "can't open %s\n", path);
    return 0;
  }

  int result = fscanf(f, "%d/%d", bus, dev);
  fclose(f);

  if (result != 2) {
    fprintf(stderr, "can't read %s\n", path);
    return 0;
  }

  return 1;
}

// read the devnum file in bus_path
//   /sys/bus/usb/devices/usbBUS/BUS-PORT/devnum
// and return the value within
static int usb_get_devnum(const char *bus_path) {
  char devnum_path[512];
  snprintf(devnum_path, 512, "%s/devnum", bus_path);

  FILE *f = fopen(devnum_path, "r");
  if (!f) {
    if (errno == ENOENT)
      return -1;

    fprintf(stderr, "can't open %s: %s\n", devnum_path, strerror(errno));
    return -1;
  }

  int devnum;
  int result = fscanf(f, "%d", &devnum);
  int err = errno;
  fclose(f);

  if (result != 1) {
    fprintf(stderr, "can't read %s: %s\n", devnum_path, strerror(err));
    return -1;
  }

  return devnum;
}

// recursively search for the device with the given dev number
// in the /sys/bus/usb/devices/usbX/Y-Z hierarchy
// and return the path to the port
static int usb_find_device_port(
  const char *bus_path,
  int         bus,
  int         dev,
  char       *port_path,
  size_t      port_path_size
) {
  if (usb_get_devnum(bus_path) == dev) {
    snprintf(port_path, port_path_size, "%s", bus_path);
    return 1;
  }

  DIR *dir = opendir(bus_path);
  if (!dir) {
    fprintf(stderr, "can't open %s: %s\n", bus_path, strerror(errno));
    return 0;
  }

  // looking for d_name beginning with the bus number followed by a "-"
  char prefix[20];
  snprintf(prefix, 20, "%d-", bus);

  struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (entry->d_type != DT_DIR)
      continue;

    if (strncmp(entry->d_name, prefix, strlen(prefix)) != 0)
      continue;

    char next_path[512];
    snprintf(next_path, 512, "%s/%s", bus_path, entry->d_name);

    if (usb_find_device_port(next_path, bus, dev, port_path, port_path_size)) {
      closedir(dir);
      return 1;
    }
  }

  closedir(dir);
  return 0;
}

static void alsa_get_serial_number(struct alsa_card *card) {

  int result, bus, dev;
  if (!alsa_get_usbbus(card, &bus, &dev))
    return;

  // recurse through /sys/bus/usb/devices/usbBUS/BUS-.../devnum
  // to find the device with the matching dev number
  char bus_path[80];
  snprintf(bus_path, 80, "/sys/bus/usb/devices/usb%d", bus);

  char port_path[512];

  if (!usb_find_device_port(bus_path, bus, dev, port_path, sizeof(port_path))) {
    fprintf(
      stderr,
      "can't find port name in %s for dev %d (%s)\n",
      bus_path, dev, card->name
    );
    return;
  }

  // read the serial number
  char serial_path[520];
  snprintf(serial_path, 520, "%s/serial", port_path);
  FILE *f = fopen(serial_path, "r");
  if (!f) {
    fprintf(stderr, "can't open %s\n", serial_path);
    return;
  }

  char serial[40];
  result = fscanf(f, "%39s", serial);
  fclose(f);

  if (result != 1) {
    fprintf(stderr, "can't read %s\n", serial_path);
    return;
  }

  card->serial = strdup(serial);
}

static void alsa_scan_cards(void) {
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
        strncmp(snd_ctl_card_info_get_name(info), "Clarett", 7) != 0 &&
        strncmp(snd_ctl_card_info_get_name(info), "Vocaster", 8) != 0)
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
    alsa_set_lr_nums(card);
    alsa_get_routing_controls(card);

    alsa_subscribe(card);
    alsa_get_usbid(card);
    alsa_get_serial_number(card);
    card->best_firmware_version =
      scarlett2_get_best_firmware_version(card->pid);

    if (card->serial) {

      // call the reopen callbacks for this card
      struct reopen_callback *rc = g_hash_table_lookup(
        reopen_callbacks, card->serial
      );
      if (rc)
        rc->callback(rc->data);

      g_hash_table_remove(reopen_callbacks, card->serial);
    }

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

static void alsa_inotify_init(void) {
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

void alsa_init(void) {
  alsa_cards = g_array_new(FALSE, TRUE, sizeof(struct alsa_card *));
  reopen_callbacks = g_hash_table_new_full(
    g_str_hash, g_str_equal, g_free, g_free
  );
  alsa_inotify_init();
  alsa_scan_cards();
}

void alsa_register_reopen_callback(
  const char     *serial,
  ReOpenCallback *callback,
  void           *data
) {
  struct reopen_callback *rc = g_new0(struct reopen_callback, 1);
  rc->callback = callback;
  rc->data = data;

  g_hash_table_insert(reopen_callbacks, g_strdup(serial), rc);
}

void alsa_unregister_reopen_callback(const char *serial) {
  g_hash_table_remove(reopen_callbacks, serial);
}

int alsa_has_reopen_callbacks(void) {
  return g_hash_table_size(reopen_callbacks);
}
