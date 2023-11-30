// SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "alsa.h"
#include "alsa-sim.h"
#include "error.h"
#include "window-iface.h"

// check that *config is a compound node, retrieve the first node
// within, check that that node is a compound node, optionally check
// its ID, and replace *config with the child
static void get_and_check_first_compound(
  snd_config_t **config,
  const char    *expected_id
) {
  const char *id, *child_id;
  int err;

  err = snd_config_get_id(*config, &id);
  if (err < 0)
    fatal_alsa_error("snd_config_get_id error", err);

  if (snd_config_get_type(*config) != SND_CONFIG_TYPE_COMPOUND) {
    printf("config node '%s' is not of type compound\n", id);
    exit(1);
  }

  snd_config_iterator_t i = snd_config_iterator_first(*config);
  if (i == snd_config_iterator_end(*config)) {
    printf("compound config node '%s' has no children\n", id);
    exit(1);
  }

  snd_config_t *config_child = snd_config_iterator_entry(i);
  err = snd_config_get_id(config_child, &child_id);
  if (err < 0)
    fatal_alsa_error("snd_config_get_id error", err);

  if (snd_config_get_type(config_child) != SND_CONFIG_TYPE_COMPOUND) {
    printf("config node %s->%s is not of type compound\n", id, child_id);
    exit(1);
  }

  *config = config_child;

  if (!expected_id)
    return;

  if (!child_id) {
    printf("config node has no id\n");
    exit(1);
  }

  if (strcmp(child_id, expected_id) != 0) {
    printf(
      "found config node %s->%s instead of %s\n",
      id, child_id, expected_id
    );
    exit(1);
  }
}

static void alsa_parse_enum_items(
  snd_config_t     *items,
  struct alsa_elem *elem
) {
  int count = snd_config_is_array(items);
  if (count < 0) {
    printf("error: parse enum items array value %d\n", count);
    return;
  }

  elem->item_count = count;
  elem->item_names = calloc(count, sizeof(char *));

  int item_num = 0;

  snd_config_iterator_t i, next;
  snd_config_for_each(i, next, items) {
    snd_config_t *node = snd_config_iterator_entry(i);

    const char *key;

    int err = snd_config_get_id(node, &key);
    if (err < 0)
      fatal_alsa_error("snd_config_get_id error", err);
    int type = snd_config_get_type(node);
    if (type != SND_CONFIG_TYPE_STRING) {
      printf("error: enum item %s type %d not string\n", key, type);
      return;
    }

    const char *s;
    err = snd_config_get_string(node, &s);
    if (err < 0)
      fatal_alsa_error("snd_config_get_string error", err);

    elem->item_names[item_num++] = strdup(s);
  }
}

// parse a comment node and update elem, e.g.:
//
// comment {
//   access read
//   type ENUMERATED
//   count 1
//   item.0 Line
//   item.1 Inst
// }
static void alsa_parse_comment_node(
  snd_config_t     *comment,
  struct alsa_elem *elem
) {
  snd_config_iterator_t i, next;
  snd_config_for_each(i, next, comment) {
    snd_config_t *node = snd_config_iterator_entry(i);

    const char *key;

    int err = snd_config_get_id(node, &key);
    if (err < 0)
      fatal_alsa_error("snd_config_get_id error", err);
    int type = snd_config_get_type(node);

    if (strcmp(key, "access") == 0) {
      if (type != SND_CONFIG_TYPE_STRING) {
        printf("access type not string\n");
        return;
      }
      const char *access;
      err = snd_config_get_string(node, &access);
      if (err < 0)
        fatal_alsa_error("snd_config_get_string error", err);
      if (strstr(access, "write"))
        elem->writable = 1;
    } else if (strcmp(key, "type") == 0) {
      if (type != SND_CONFIG_TYPE_STRING) {
        printf("type type not string\n");
        return;
      }
      const char *type;
      err = snd_config_get_string(node, &type);
      if (err < 0)
        fatal_alsa_error("snd_config_get_string error", err);
      if (strcmp(type, "BOOLEAN") == 0)
        elem->type = SND_CTL_ELEM_TYPE_BOOLEAN;
      else if (strcmp(type, "ENUMERATED") == 0)
        elem->type = SND_CTL_ELEM_TYPE_ENUMERATED;
      else if (strcmp(type, "INTEGER") == 0)
        elem->type = SND_CTL_ELEM_TYPE_INTEGER;
    } else if (strcmp(key, "item") == 0) {
      alsa_parse_enum_items(node, elem);
    } else if (strcmp(key, "range") == 0) {
      if (type != SND_CONFIG_TYPE_STRING) {
        printf("range type not string\n");
        return;
      }
      const char *range;
      err = snd_config_get_string(node, &range);
      if (err < 0)
        fatal_alsa_error("snd_config_get_string error", err);

      // Parse the range string and update elem->min_val and elem->max_val
      int min_val, max_val;
      if (sscanf(range, "%d - %d", &min_val, &max_val) == 2) {
        elem->min_val = min_val;
        elem->max_val = max_val;
      }
    } else if (strcmp(key, "dbmin") == 0) {
      if (type != SND_CONFIG_TYPE_INTEGER) {
        printf("dbmin type not integer\n");
        return;
      }
      long dbmin;
      err = snd_config_get_integer(node, &dbmin);
      if (err < 0)
        fatal_alsa_error("snd_config_get_integer error", err);
      elem->min_dB = dbmin / 100;
    } else if (strcmp(key, "dbmax") == 0) {
      if (type != SND_CONFIG_TYPE_INTEGER) {
        printf("dbmax type not integer\n");
        return;
      }
      long dbmax;
      err = snd_config_get_integer(node, &dbmax);
      if (err < 0)
        fatal_alsa_error("snd_config_get_integer error", err);
      elem->max_dB = dbmax / 100;
    }
  }
}

static int alsa_config_to_new_elem(
  snd_config_t     *config,
  struct alsa_elem *elem
) {
  const char *s;
  int id;
  char *iface = NULL, *name = NULL;
  int seen_value;
  int value_type = -1;
  char *string_value = NULL;
  long int_value;
  int err;

  err = snd_config_get_id(config, &s);
  if (err < 0)
    fatal_alsa_error("snd_config_get_id error", err);
  id = atoi(s);

  // loop through the nodes of the control element
  snd_config_iterator_t i, next;
  snd_config_for_each(i, next, config) {
    snd_config_t *node = snd_config_iterator_entry(i);

    const char *key;

    err = snd_config_get_id(node, &key);
    if (err < 0)
      fatal_alsa_error("snd_config_get_id error", err);
    int type = snd_config_get_type(node);

    // iface node?
    if (strcmp(key, "iface") == 0) {
      if (type != SND_CONFIG_TYPE_STRING) {
        printf("iface type for %d is %d not string", id, type);
        goto fail;
      }

      err = snd_config_get_string(node, &s);
      if (err < 0)
        fatal_alsa_error("snd_config_get_string error", err);
      iface = strdup(s);

    // name node?
    } else if (strcmp(key, "name") == 0) {
      if (type != SND_CONFIG_TYPE_STRING) {
        printf("name type for %d is %d not string", id, type);
        goto fail;
      }

      err = snd_config_get_string(node, &s);
      if (err < 0)
        fatal_alsa_error("snd_config_get_string error", err);
      name = strdup(s);

    // value node?
    } else if (strcmp(key, "value") == 0) {
      seen_value = 1;
      value_type = type;

      if (type == SND_CONFIG_TYPE_INTEGER) {
        err = snd_config_get_integer(node, &int_value);
        if (err < 0)
          fatal_alsa_error("snd_config_get_integer error", err);
      } else if (type == SND_CONFIG_TYPE_STRING) {
        err = snd_config_get_string(node, &s);
        if (err < 0)
          fatal_alsa_error("snd_config_get_string error", err);
        string_value = strdup(s);
      } else if (type == SND_CONFIG_TYPE_COMPOUND) {
        elem->count = snd_config_is_array(node);
        if (strcmp(name, "Level Meter") == 0) {
          seen_value = 1;
          value_type = SND_CONFIG_TYPE_INTEGER;
          int_value = 0;
        } else {
          goto fail;
        }
      } else {
        printf(
          "skipping value type for %d; is %d, not int or string\n",
          id, type
        );
        goto fail;
      }

    // comment node?
    } else if (strcmp(key, "comment") == 0) {
      alsa_parse_comment_node(node, elem);
    } else {
      printf("skipping unknown node %s for %d\n", key, id);
      goto fail;
    }
  }

  // check iface value; only interested in CARD, MIXER, and PCM
  if (!iface) {
    printf("missing iface node in control id %d\n", id);
    goto fail;
  }
  if (strcmp(iface, "CARD") != 0 &&
      strcmp(iface, "MIXER") != 0 &&
      strcmp(iface, "PCM") != 0)
    goto fail;

  // check for presence of name and value
  if (!name) {
    printf("missing name node in control id %d\n", id);
    goto fail;
  }
  if (!seen_value) {
    printf("missing value node in control id %d\n", id);
    goto fail;
  }

  // set the element value

  // integer in config
  if (value_type == SND_CONFIG_TYPE_INTEGER) {
    elem->value = int_value;

  // string in config
  } else if (value_type == SND_CONFIG_TYPE_STRING) {

    // translate boolean true/false
    if (elem->type == SND_CTL_ELEM_TYPE_BOOLEAN) {
      if (strcmp(string_value, "true") == 0)
        elem->value = 1;

    // translate enum string value to integer
    } else if (elem->type == SND_CTL_ELEM_TYPE_ENUMERATED) {
      for (int i = 0; i < elem->item_count; i++) {
        if (strcmp(string_value, elem->item_names[i]) == 0) {
          elem->value = i;
          break;
        }
      }

    // string value not boolean/enum
    } else {
      goto fail;
    }
  }

  elem->numid = id;
  elem->name = name;

  free(iface);
  free(string_value);

  return 0;

fail:
  free(iface);
  free(name);
  free(string_value);

  return -1;
}

static void alsa_config_to_new_card(
  snd_config_t     *top,
  struct alsa_card *card
) {
  snd_config_t *config = top;

  // go down through the compound nodes state.X (usually USB), control
  get_and_check_first_compound(&config, "state");
  get_and_check_first_compound(&config, NULL);
  get_and_check_first_compound(&config, "control");

  // loop through the controls
  snd_config_iterator_t i, next;
  snd_config_for_each(i, next, config) {
    snd_config_t *node = snd_config_iterator_entry(i);

    // ignore non-compound controls
    if (snd_config_get_type(config) != SND_CONFIG_TYPE_COMPOUND)
      continue;

    struct alsa_elem elem = {};
    elem.card = card;

    // create the element
    int err = alsa_config_to_new_elem(node, &elem);

    if (err)
      continue;

    if (card->elems->len <= elem.numid)
      g_array_set_size(card->elems, elem.numid + 1);
    g_array_index(card->elems, struct alsa_elem, elem.numid) = elem;
  }
}

// return the basename of fn (no path, no extension)
// e.g. "/home/user/file.ext" -> "file"
static char *sim_card_name(const char *fn) {

  // strdup fn and remove path (if any)
  char *name = strrchr(fn, '/');
  if (name)
    name = strdup(name + 1);
  else
    name = strdup(fn);

  // remove extension
  char *dot = strrchr(name, '.');
  if (dot)
    *dot = '\0';

  return name;
}

void create_sim_from_file(GtkWindow *w, char *fn) {
  snd_config_t *config;
  snd_input_t  *in;

  int err;

  err = snd_config_top(&config);
  if (err < 0)
    fatal_alsa_error("snd_config_top error", err);

  err = snd_input_stdio_open(&in, fn, "r");
  if (err < 0) {
    char *s = g_strdup_printf("Error opening %s: %s", fn, snd_strerror(err));
    show_error(w, s);
    free(s);
    return;
  }

  err = snd_config_load(config, in);
  snd_input_close(in);
  if (err < 0)
    fatal_alsa_error("snd_config_load error", err);

  struct alsa_card *card = card_create(SIMULATED_CARD_NUM);
  card->name = sim_card_name(fn);
  alsa_config_to_new_card(config, card);

  snd_config_delete(config);

  create_card_window(card);
}
