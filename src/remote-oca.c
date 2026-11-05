// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db.h"
#include "glow.h"
#include "remote-oca.h"

#define ONO_DEVICE_MANAGER       1
#define ONO_SUBSCRIPTION_MANAGER 4
#define ONO_AUTH_AGENT           14
#define ONO_ROOT_BLOCK           100

enum oca_node_kind {
  NODE_DEVICE_MANAGER,
  NODE_SUBSCRIPTION_MANAGER,
  NODE_BLOCK,
  NODE_BOOL_ACTUATOR,
  NODE_BOOL_SENSOR,
  NODE_MUTE,
  NODE_SWITCH,
  NODE_GAIN,
  NODE_PAN,
  NODE_UINT32_ACTUATOR,
  NODE_UINT32_SENSOR,
  NODE_STRING_ACTUATOR,
  NODE_STRING_SENSOR,
  NODE_LEVEL_SENSOR
};

enum oca_binding_kind {
  BIND_NONE = 0,
  BIND_ALSA_BOOL,
  BIND_ALSA_SWITCH,
  BIND_ALSA_GAIN,
  BIND_ALSA_MUTE,
  BIND_AIR_ENABLED,
  BIND_AIR_MODE,
  BIND_AUTOGAIN_START,
  BIND_AUTOGAIN_CANCEL,
  BIND_AUTOGAIN_STATE,
  BIND_MC_AUTOGAIN_START,
  BIND_MC_AUTOGAIN_CANCEL,
  BIND_MC_AUTOGAIN_SELECT,
  BIND_MC_AUTOGAIN_STAGE,
  BIND_DIRECT_MON_ENABLED,
  BIND_DIRECT_MON_MODE,
  BIND_METER_HW_IN,
  BIND_METER_HW_PEAK,
  BIND_METER_HW_CLIP,
  BIND_METER_MIX_OUT,
  BIND_METER_MIX_CHAN
};

struct oca_class_def {
  uint16_t fields[6];
  uint16_t field_count;
  uint16_t version;
};

struct oca_node {
  uint32_t              ono;
  uint32_t              owner_ono;
  enum oca_node_kind    kind;
  char                 *role;
  char                 *label;
  bool                  enabled;

  // For NODE_BLOCK
  GArray               *children; // array of uint32_t child ONo

  // Cached values & limits
  uint8_t               u8_val;   // bool (0/1) or OcaMute (1=Muted, 2=Unmuted)
  uint16_t              u16_val;  // switch 0-based position
  uint32_t              u32_val;
  uint32_t              u32_min;
  uint32_t              u32_max;
  float                 f32_val;
  float                 f32_min;
  float                 f32_max;
  char                 *str_val;

  // For NODE_SWITCH
  char                **pos_names;
  uint16_t              pos_count;

  // Hardware ALSA binding
  enum oca_binding_kind bind_kind;
  struct alsa_elem     *elem;
  int                   channel_idx; // 0-based channel index
  char                  mix_letter;  // 'A', 'B', ... for mixer gains
  int                   mix_in_num;  // 1-based mixer input num
};

struct remote_oca_tree {
  struct alsa_card *card;
  GPtrArray        *nodes;
  GHashTable       *by_ono;
  uint32_t          next_ono;
  int               num_inputs;
  int               last_air_mode[16];
  int               last_dm_mode;
  char              onboarding_state[24];
  bool              autogain_dismissed[16];
  struct oca_node  *ag_state_nodes[16];
  struct oca_node  *ag_switch_nodes[16];
  struct oca_node  *ag_selected_nodes[16];
  struct oca_node  *mc_ag_stage_node;
};

struct oca_subscription {
  uint32_t emitter_ono;
  uint16_t event_def_level;
  uint16_t event_index;
  bool     filter_property;
  uint16_t prop_def_level;
  uint16_t prop_index;
  uint32_t sub_ono;
  uint16_t sub_def_level;
  uint16_t sub_method_idx;
  uint8_t *context;
  uint16_t context_len;
};

struct remote_oca_session {
  bool                   is_secure;
  uint16_t               proto_ver;
  remote_oca_send_pdu_fn send_fn;
  void                  *send_user_data;
  GList                 *subscriptions;
};

struct oca_reader {
  const uint8_t *data;
  size_t         len;
  size_t         pos;
};

// --- Binary Serialization Helpers ---

static void oca_write_u8(GByteArray *buf, uint8_t v) {
  g_byte_array_append(buf, &v, 1);
}

static void oca_write_u16(GByteArray *buf, uint16_t v) {
  uint16_t be = GUINT16_TO_BE(v);
  g_byte_array_append(buf, (const uint8_t *)&be, 2);
}

static void oca_write_u32(GByteArray *buf, uint32_t v) {
  uint32_t be = GUINT32_TO_BE(v);
  g_byte_array_append(buf, (const uint8_t *)&be, 4);
}

static void oca_write_u32_at(GByteArray *buf, size_t offset, uint32_t v) {
  uint32_t be = GUINT32_TO_BE(v);
  memcpy(buf->data + offset, &be, 4);
}

static void oca_write_f32(GByteArray *buf, float v) {
  union {
    float    f;
    uint32_t u;
  } conv;
  conv.f = v;
  oca_write_u32(buf, conv.u);
}

static void oca_write_string(GByteArray *buf, const char *s) {
  if (!s)
    s = "";
  size_t byte_len = strlen(s);
  uint16_t cp_len;
  if (g_utf8_validate(s, (gssize)byte_len, NULL))
    cp_len = (uint16_t)g_utf8_strlen(s, (gssize)byte_len);
  else
    cp_len = (uint16_t)byte_len;

  oca_write_u16(buf, cp_len);
  if (byte_len > 0)
    g_byte_array_append(buf, (const uint8_t *)s, (guint)byte_len);
}

static void oca_write_class_id(
  GByteArray                 *buf,
  const struct oca_class_def *cdef
) {
  oca_write_u16(buf, cdef->field_count);
  for (uint16_t i = 0; i < cdef->field_count; i++)
    oca_write_u16(buf, cdef->fields[i]);
  oca_write_u16(buf, cdef->version);
}

static bool oca_read_u8(struct oca_reader *r, uint8_t *out) {
  if (r->pos + 1 > r->len)
    return false;
  *out = r->data[r->pos++];
  return true;
}

static bool oca_read_u16(struct oca_reader *r, uint16_t *out) {
  if (r->pos + 2 > r->len)
    return false;
  uint16_t be;
  memcpy(&be, r->data + r->pos, 2);
  r->pos += 2;
  *out = GUINT16_FROM_BE(be);
  return true;
}

static bool oca_read_u32(struct oca_reader *r, uint32_t *out) {
  if (r->pos + 4 > r->len)
    return false;
  uint32_t be;
  memcpy(&be, r->data + r->pos, 4);
  r->pos += 4;
  *out = GUINT32_FROM_BE(be);
  return true;
}

static bool oca_read_f32(struct oca_reader *r, float *out) {
  uint32_t u;
  if (!oca_read_u32(r, &u))
    return false;
  union {
    uint32_t u;
    float    f;
  } conv;
  conv.u = u;
  *out = conv.f;
  return true;
}

static bool oca_read_string(struct oca_reader *r, char **out) {
  uint16_t cp_len;
  if (!oca_read_u16(r, &cp_len))
    return false;

  size_t start = r->pos;
  size_t cur = start;
  for (uint16_t i = 0; i < cp_len; i++) {
    if (cur >= r->len)
      return false;
    uint8_t c = r->data[cur];
    size_t char_bytes = 1;
    if ((c & 0x80) == 0)
      char_bytes = 1;
    else if ((c & 0xE0) == 0xC0)
      char_bytes = 2;
    else if ((c & 0xF0) == 0xE0)
      char_bytes = 3;
    else if ((c & 0xF8) == 0xF0)
      char_bytes = 4;
    if (cur + char_bytes > r->len)
      return false;
    cur += char_bytes;
  }

  *out = g_strndup((const char *)(r->data + start), cur - start);
  r->pos = cur;
  return true;
}

static bool oca_read_blob(
  struct oca_reader *r,
  uint8_t          **data_out,
  uint16_t          *len_out
) {
  uint16_t blen;
  if (!oca_read_u16(r, &blen))
    return false;
  if (r->pos + blen > r->len)
    return false;
  if (blen > 0) {
    *data_out = g_malloc(blen);
    memcpy(*data_out, r->data + r->pos, blen);
  } else {
    *data_out = NULL;
  }
  *len_out = blen;
  r->pos += blen;
  return true;
}

// --- Class Definitions ---

static struct oca_class_def get_node_class_def(enum oca_node_kind kind) {
  switch (kind) {
    case NODE_DEVICE_MANAGER:
      return (struct oca_class_def){ { 1, 3, 1 }, 3, 2 };
    case NODE_SUBSCRIPTION_MANAGER:
      return (struct oca_class_def){ { 1, 3, 4 }, 3, 2 };
    case NODE_BLOCK:
      return (struct oca_class_def){ { 1, 1, 3 }, 3, 2 };
    case NODE_BOOL_ACTUATOR:
      return (struct oca_class_def){ { 1, 1, 1, 1, 1 }, 5, 2 };
    case NODE_BOOL_SENSOR:
      return (struct oca_class_def){ { 1, 1, 2, 1, 1 }, 5, 2 };
    case NODE_MUTE:
      return (struct oca_class_def){ { 1, 1, 1, 2 }, 4, 2 };
    case NODE_SWITCH:
      return (struct oca_class_def){ { 1, 1, 1, 4 }, 4, 2 };
    case NODE_GAIN:
      return (struct oca_class_def){ { 1, 1, 1, 5 }, 4, 2 };
    case NODE_PAN:
      return (struct oca_class_def){ { 1, 1, 1, 6 }, 4, 2 };
    case NODE_UINT32_ACTUATOR:
      return (struct oca_class_def){ { 1, 1, 1, 1, 8 }, 5, 2 };
    case NODE_UINT32_SENSOR:
      return (struct oca_class_def){ { 1, 1, 2, 1, 8 }, 5, 2 };
    case NODE_STRING_ACTUATOR:
      return (struct oca_class_def){ { 1, 1, 1, 1, 12 }, 5, 2 };
    case NODE_STRING_SENSOR:
      return (struct oca_class_def){ { 1, 1, 2, 1, 12 }, 5, 2 };
    case NODE_LEVEL_SENSOR:
      return (struct oca_class_def){ { 1, 1, 2, 2 }, 4, 2 };
  }
  return (struct oca_class_def){ { 1, 1 }, 2, 2 };
}

static const struct oca_class_def AUTH_AGENT_CLASS_DEF = {
  { 1, 2, 65535, 0, 4878, 1 }, 6, 1
};

// --- ALSA Value Conversion Helpers ---

static float alsa_elem_to_db(struct alsa_elem *elem) {
  if (!elem)
    return 0.0f;

  long val = alsa_get_elem_value(elem);
  float min_db = (float)elem->min_cdB / 100.0f;
  float max_db = (float)elem->max_cdB / 100.0f;

  if (elem->max_val <= elem->min_val || max_db <= min_db)
    return (float)val;

  float db;
  if (elem->dB_type == SND_CTL_TLVT_DB_LINEAR) {
    db = (float)linear_value_to_cdb(
      (int)val, elem->min_val, elem->max_val, elem->min_cdB, elem->max_cdB
    ) / 100.0f;
  } else {
    float scale = (max_db - min_db) / (float)(elem->max_val - elem->min_val);
    db = (float)(val - elem->min_val) * scale + min_db;
  }

  if (db < min_db)
    db = min_db;
  else if (db > max_db)
    db = max_db;
  return db;
}

static void alsa_elem_set_db(
  struct alsa_card *card,
  struct alsa_elem *elem,
  float             db,
  char              mix_letter,
  int               mix_in_num
) {
  if (!elem)
    return;

  float min_db = (float)elem->min_cdB / 100.0f;
  float max_db = (float)elem->max_cdB / 100.0f;
  if (db < min_db)
    db = min_db;
  else if (db > max_db)
    db = max_db;

  long raw;
  if (elem->max_val <= elem->min_val || max_db <= min_db) {
    raw = (long)lroundf(db);
  } else if (elem->dB_type == SND_CTL_TLVT_DB_LINEAR) {
    raw = cdb_to_linear_value(
      (int)lroundf(db * 100.0f),
      elem->min_val, elem->max_val, elem->min_cdB, elem->max_cdB
    );
  } else {
    float norm = (db - min_db) / (max_db - min_db);
    raw = (long)lroundf(
      norm * (float)(elem->max_val - elem->min_val) + (float)elem->min_val
    );
  }

  if (raw < elem->min_val)
    raw = elem->min_val;
  else if (raw > elem->max_val)
    raw = elem->max_val;

  alsa_set_elem_value(elem, raw);

  // Sync Direct Monitor Mix volume on 4th Gen Solo / 2i2
  if (card && card->elems && mix_letter != '\0' && mix_in_num > 0) {
    struct alsa_elem *dm_elem = get_elem_by_prefix(
      card->elems, "Direct Monitor Playback"
    );
    if (dm_elem) {
      long dm_val = alsa_get_elem_value(dm_elem);
      if (dm_val > 0) {
        char mon_name[80];
        if (strstr(dm_elem->name, "Switch")) {
          snprintf(
            mon_name, sizeof(mon_name),
            "Monitor Mix %c Input %02d Playback Volume",
            mix_letter, mix_in_num
          );
        } else {
          snprintf(
            mon_name, sizeof(mon_name),
            "Monitor %ld Mix %c Input %02d Playback Volume",
            dm_val, mix_letter, mix_in_num
          );
        }
        struct alsa_elem *mon_elem = get_elem_by_name(card->elems, mon_name);
        if (mon_elem)
          alsa_set_elem_value(mon_elem, raw);
      }
    }
  }
}

static const char *map_autogain_status(struct alsa_elem *elem) {
  if (!elem)
    return "off";
  int idx = (int)alsa_get_elem_value(elem);
  int count = alsa_get_item_count(elem);
  if (idx < 0 || idx >= count)
    return "off";
  const char *item = alsa_get_item_name(elem, idx);
  if (!item)
    return "off";
  if (strcmp(item, "Running") == 0)
    return "in-progress";
  if (strncmp(item, "Success", 7) == 0 || strncmp(item, "Warn", 4) == 0)
    return "success";
  if (strncmp(item, "Fail", 4) == 0)
    return "error";
  return "off";
}

static float read_port_meter_db(
  struct alsa_card *card,
  int               port_category,
  int               port_num
) {
  if (!card || !card->routing_srcs)
    return -128.0f;

  for (guint i = 0; i < card->routing_srcs->len; i++) {
    struct routing_src *r_src = &g_array_index(
      card->routing_srcs, struct routing_src, i
    );
    if (r_src->port_category == port_category && r_src->port_num == port_num) {
      double db = get_routing_src_level_db(card, r_src);
      if (!isfinite(db) || db < -128.0)
        return -128.0f;
      if (db > 12.0)
        return 12.0f;
      return (float)db;
    }
  }
  return -128.0f;
}

// Refresh node's cached value from bound ALSA element; returns true if changed
static bool sync_node_from_alsa(
  struct remote_oca_tree *tree,
  struct oca_node        *node
) {
  if (!tree || !node)
    return false;

  switch (node->bind_kind) {
    case BIND_NONE:
      return false;

    case BIND_ALSA_BOOL: {
      if (!node->elem)
        return false;
      uint8_t nv = alsa_get_elem_value(node->elem) ? 1 : 0;
      if (nv != node->u8_val) {
        node->u8_val = nv;
        return true;
      }
      return false;
    }

    case BIND_ALSA_SWITCH: {
      if (!node->elem)
        return false;
      long raw = alsa_get_elem_value(node->elem);
      if (raw < 0)
        raw = 0;
      if (node->pos_count > 0 && raw >= node->pos_count)
        raw = node->pos_count - 1;
      uint16_t nv = (uint16_t)raw;
      if (nv != node->u16_val) {
        node->u16_val = nv;
        return true;
      }
      return false;
    }

    case BIND_ALSA_GAIN: {
      if (!node->elem)
        return false;
      float nv = alsa_elem_to_db(node->elem);
      if (fabsf(nv - node->f32_val) >= 0.05f) {
        node->f32_val = nv;
        return true;
      }
      return false;
    }

    case BIND_ALSA_MUTE: {
      if (!node->elem)
        return false;
      // ALSA Playback Switch: 0=muted (OCA 1), 1=unmuted (OCA 2)
      uint8_t nv = alsa_get_elem_value(node->elem) ? 2 : 1;
      if (nv != node->u8_val) {
        node->u8_val = nv;
        return true;
      }
      return false;
    }

    case BIND_AIR_ENABLED: {
      if (!node->elem)
        return false;
      long raw = alsa_get_elem_value(node->elem);
      if (raw >= 1 && raw <= 2 && node->channel_idx >= 0 &&
          node->channel_idx < 16) {
        tree->last_air_mode[node->channel_idx] = (int)raw;
      }
      uint8_t nv = (raw > 0) ? 1 : 0;
      if (nv != node->u8_val) {
        node->u8_val = nv;
        return true;
      }
      return false;
    }

    case BIND_AIR_MODE: {
      if (!node->elem)
        return false;
      long raw = alsa_get_elem_value(node->elem);
      int ch = (node->channel_idx >= 0 && node->channel_idx < 16)
               ? node->channel_idx : 0;
      if (raw >= 1 && raw <= 2)
        tree->last_air_mode[ch] = (int)raw;
      uint16_t nv = (tree->last_air_mode[ch] >= 2) ? 1 : 0;
      if (nv != node->u16_val) {
        node->u16_val = nv;
        return true;
      }
      return false;
    }

    case BIND_AUTOGAIN_START:
    case BIND_AUTOGAIN_CANCEL:
    case BIND_MC_AUTOGAIN_START:
    case BIND_MC_AUTOGAIN_CANCEL:
    case BIND_MC_AUTOGAIN_SELECT:
    case BIND_MC_AUTOGAIN_STAGE:
      return false;

    case BIND_AUTOGAIN_STATE: {
      if (!node->elem)
        return false;
      int ch = (node->channel_idx >= 0 && node->channel_idx < 16)
               ? node->channel_idx : 0;
      const char *st = map_autogain_status(node->elem);
      if (strcmp(st, "in-progress") == 0) {
        tree->autogain_dismissed[ch] = false;
      } else if (tree->autogain_dismissed[ch]) {
        st = "off";
      }
      if (!node->str_val || strcmp(node->str_val, st) != 0) {
        g_free(node->str_val);
        node->str_val = g_strdup(st);
        return true;
      }
      return false;
    }

    case BIND_DIRECT_MON_ENABLED: {
      if (!node->elem)
        return false;
      long raw = alsa_get_elem_value(node->elem);
      if (raw >= 1 && raw <= 2)
        tree->last_dm_mode = (int)raw;
      uint8_t nv = (raw > 0) ? 1 : 0;
      if (nv != node->u8_val) {
        node->u8_val = nv;
        return true;
      }
      return false;
    }

    case BIND_DIRECT_MON_MODE: {
      if (!node->elem)
        return false;
      long raw = alsa_get_elem_value(node->elem);
      if (raw >= 1 && raw <= 2)
        tree->last_dm_mode = (int)raw;
      uint16_t nv = (tree->last_dm_mode >= 2) ? 1 : 0;
      if (nv != node->u16_val) {
        node->u16_val = nv;
        return true;
      }
      return false;
    }

    case BIND_METER_HW_IN: {
      float db = read_port_meter_db(tree->card, PC_HW, node->channel_idx);
      if (fabsf(db - node->f32_val) >= 0.25f) {
        node->f32_val = db;
        return true;
      }
      return false;
    }

    case BIND_METER_HW_PEAK: {
      float db = read_port_meter_db(tree->card, PC_HW, node->channel_idx);
      float decayed = node->f32_val - 0.5f;
      if (decayed < -128.0f)
        decayed = -128.0f;
      float nv = (db > decayed) ? db : decayed;
      if (fabsf(nv - node->f32_val) >= 0.25f) {
        node->f32_val = nv;
        return true;
      }
      return false;
    }

    case BIND_METER_HW_CLIP: {
      float db = read_port_meter_db(tree->card, PC_HW, node->channel_idx);
      uint8_t nv = (db >= -0.5f) ? 1 : 0;
      if (nv != node->u8_val) {
        node->u8_val = nv;
        return true;
      }
      return false;
    }

    case BIND_METER_MIX_OUT: {
      float db = read_port_meter_db(tree->card, PC_MIX, node->channel_idx);
      if (fabsf(db - node->f32_val) >= 0.25f) {
        node->f32_val = db;
        return true;
      }
      return false;
    }

    case BIND_METER_MIX_CHAN: {
      int cat = (node->channel_idx < 2) ? PC_HW : PC_PCM;
      int pnum = (node->channel_idx < 2)
                 ? node->channel_idx : (node->channel_idx - 2);
      float db = read_port_meter_db(tree->card, cat, pnum);
      if (node->elem && db > -120.0f)
        db += alsa_elem_to_db(node->elem);
      if (db < -128.0f)
        db = -128.0f;
      else if (db > 12.0f)
        db = 12.0f;
      if (fabsf(db - node->f32_val) >= 0.25f) {
        node->f32_val = db;
        return true;
      }
      return false;
    }
  }

  return false;
}

// --- Tree Construction Helpers ---

static struct oca_node *tree_add_node(
  struct remote_oca_tree *tree,
  uint32_t                ono,
  uint32_t                owner_ono,
  enum oca_node_kind      kind,
  const char             *role
) {
  struct oca_node *node = g_malloc0(sizeof(struct oca_node));
  node->ono = ono;
  node->owner_ono = owner_ono;
  node->kind = kind;
  node->role = g_strdup(role ? role : "");
  node->label = g_strdup(role ? role : "");
  node->enabled = true;

  if (kind == NODE_BLOCK)
    node->children = g_array_new(FALSE, FALSE, sizeof(uint32_t));

  g_ptr_array_add(tree->nodes, node);
  g_hash_table_insert(tree->by_ono, GUINT_TO_POINTER(ono), node);

  if (owner_ono != 0) {
    struct oca_node *parent = g_hash_table_lookup(
      tree->by_ono, GUINT_TO_POINTER(owner_ono)
    );
    if (parent && parent->children)
      g_array_append_val(parent->children, ono);
  }

  return node;
}

static struct oca_node *add_block(
  struct remote_oca_tree *tree,
  uint32_t                owner_ono,
  const char             *role
) {
  return tree_add_node(tree, tree->next_ono++, owner_ono, NODE_BLOCK, role);
}

static struct oca_node *add_string_sensor(
  struct remote_oca_tree *tree,
  uint32_t                owner_ono,
  const char             *role,
  const char             *initial_val
) {
  struct oca_node *n = tree_add_node(
    tree, tree->next_ono++, owner_ono, NODE_STRING_SENSOR, role
  );
  n->str_val = g_strdup(initial_val ? initial_val : "");
  return n;
}

static struct oca_node *add_string_actuator(
  struct remote_oca_tree *tree,
  uint32_t                owner_ono,
  const char             *role,
  const char             *initial_val
) {
  struct oca_node *n = tree_add_node(
    tree, tree->next_ono++, owner_ono, NODE_STRING_ACTUATOR, role
  );
  n->str_val = g_strdup(initial_val ? initial_val : "");
  return n;
}

static struct oca_node *add_uint32_sensor(
  struct remote_oca_tree *tree,
  uint32_t                owner_ono,
  const char             *role,
  uint32_t                val,
  uint32_t                min_v,
  uint32_t                max_v
) {
  struct oca_node *n = tree_add_node(
    tree, tree->next_ono++, owner_ono, NODE_UINT32_SENSOR, role
  );
  n->u32_val = val;
  n->u32_min = min_v;
  n->u32_max = max_v;
  return n;
}

static struct oca_node *add_bool_actuator(
  struct remote_oca_tree *tree,
  uint32_t                owner_ono,
  const char             *role,
  bool                    initial_val,
  enum oca_binding_kind   bind_kind,
  struct alsa_elem       *elem,
  int                     channel_idx
) {
  struct oca_node *n = tree_add_node(
    tree, tree->next_ono++, owner_ono, NODE_BOOL_ACTUATOR, role
  );
  n->u8_val = initial_val ? 1 : 0;
  n->bind_kind = bind_kind;
  n->elem = elem;
  n->channel_idx = channel_idx;
  sync_node_from_alsa(tree, n);
  return n;
}

static struct oca_node *add_bool_sensor(
  struct remote_oca_tree *tree,
  uint32_t                owner_ono,
  const char             *role,
  bool                    initial_val,
  enum oca_binding_kind   bind_kind,
  int                     channel_idx
) {
  struct oca_node *n = tree_add_node(
    tree, tree->next_ono++, owner_ono, NODE_BOOL_SENSOR, role
  );
  n->u8_val = initial_val ? 1 : 0;
  n->bind_kind = bind_kind;
  n->channel_idx = channel_idx;
  sync_node_from_alsa(tree, n);
  return n;
}

static struct oca_node *add_level_sensor(
  struct remote_oca_tree *tree,
  uint32_t                owner_ono,
  const char             *role,
  enum oca_binding_kind   bind_kind,
  struct alsa_elem       *elem,
  int                     channel_idx
) {
  struct oca_node *n = tree_add_node(
    tree, tree->next_ono++, owner_ono, NODE_LEVEL_SENSOR, role
  );
  n->f32_val = -128.0f;
  n->f32_min = -128.0f;
  n->f32_max = 12.0f;
  n->bind_kind = bind_kind;
  n->elem = elem;
  n->channel_idx = channel_idx;
  sync_node_from_alsa(tree, n);
  return n;
}

static struct oca_node *add_switch(
  struct remote_oca_tree *tree,
  uint32_t                owner_ono,
  const char             *role,
  const char * const     *names,
  uint16_t                count,
  uint16_t                initial_pos,
  enum oca_binding_kind   bind_kind,
  struct alsa_elem       *elem,
  int                     channel_idx
) {
  struct oca_node *n = tree_add_node(
    tree, tree->next_ono++, owner_ono, NODE_SWITCH, role
  );
  n->pos_count = count;
  n->pos_names = g_malloc0(sizeof(char *) * (count > 0 ? count : 1));
  for (uint16_t i = 0; i < count; i++)
    n->pos_names[i] = g_strdup(names[i] ? names[i] : "");
  n->u16_val = initial_pos;
  n->bind_kind = bind_kind;
  n->elem = elem;
  n->channel_idx = channel_idx;
  sync_node_from_alsa(tree, n);
  return n;
}

static struct oca_node *add_gain(
  struct remote_oca_tree *tree,
  uint32_t                owner_ono,
  const char             *role,
  float                   val,
  float                   min_db,
  float                   max_db,
  struct alsa_elem       *elem,
  char                    mix_letter,
  int                     mix_in_num
) {
  struct oca_node *n = tree_add_node(
    tree, tree->next_ono++, owner_ono, NODE_GAIN, role
  );
  if (elem && elem->max_cdB > elem->min_cdB) {
    min_db = (float)elem->min_cdB / 100.0f;
    max_db = (float)elem->max_cdB / 100.0f;
  }
  n->f32_val = val;
  n->f32_min = min_db;
  n->f32_max = max_db;
  n->mix_letter = mix_letter;
  n->mix_in_num = mix_in_num;
  if (elem) {
    n->bind_kind = BIND_ALSA_GAIN;
    n->elem = elem;
    sync_node_from_alsa(tree, n);
  }
  return n;
}

static struct oca_node *add_mute(
  struct remote_oca_tree *tree,
  uint32_t                owner_ono,
  const char             *role,
  struct alsa_elem       *elem
) {
  struct oca_node *n = tree_add_node(
    tree, tree->next_ono++, owner_ono, NODE_MUTE, role
  );
  n->u8_val = 2; // 2 = Unmuted
  if (elem) {
    n->bind_kind = BIND_ALSA_MUTE;
    n->elem = elem;
    sync_node_from_alsa(tree, n);
  }
  return n;
}

static struct oca_node *add_pan(
  struct remote_oca_tree *tree,
  uint32_t                owner_ono,
  const char             *role,
  float                   initial_pan
) {
  struct oca_node *n = tree_add_node(
    tree, tree->next_ono++, owner_ono, NODE_PAN, role
  );
  n->f32_val = initial_pan;
  n->f32_min = -1.0f;
  n->f32_max = 1.0f;
  return n;
}

static char *read_card_firmware_string(struct alsa_card *card) {
  if (!card || !card->elems)
    return g_strdup("2452");

  struct alsa_elem *fw_elem = get_elem_by_name(card->elems, "Firmware Version");
  if (!fw_elem)
    return g_strdup("2452");

  if (fw_elem->count >= 4) {
    long *vals = alsa_get_elem_int_values(fw_elem);
    if (vals)
      return g_strdup_printf("%ld.%ld.%ld.%ld", vals[0], vals[1], vals[2], vals[3]);
  }
  return g_strdup_printf("%ld", alsa_get_elem_value(fw_elem));
}

static void oca_node_free(gpointer ptr) {
  struct oca_node *n = ptr;
  if (!n)
    return;
  if (n->children)
    g_array_free(n->children, TRUE);
  if (n->pos_names) {
    for (uint16_t i = 0; i < n->pos_count; i++)
      g_free(n->pos_names[i]);
    g_free(n->pos_names);
  }
  g_free(n->role);
  g_free(n->label);
  g_free(n->str_val);
  g_free(n);
}

struct remote_oca_tree *remote_oca_tree_create(struct alsa_card *card) {
  struct remote_oca_tree *tree = g_malloc0(sizeof(struct remote_oca_tree));
  tree->card = card;
  tree->nodes = g_ptr_array_new_with_free_func(oca_node_free);
  tree->by_ono = g_hash_table_new(g_direct_hash, g_direct_equal);
  tree->next_ono = 1000;
  for (int i = 0; i < 16; i++) {
    tree->last_air_mode[i] = 1;
    tree->autogain_dismissed[i] = true;
  }
  tree->last_dm_mode = 2;
  g_strlcpy(tree->onboarding_state, "ready", sizeof(tree->onboarding_state));

  GPtrArray *elems = card ? card->elems : NULL;

  // Standard OCA managers & Root Block
  tree_add_node(tree, ONO_DEVICE_MANAGER, 0, NODE_DEVICE_MANAGER, "DeviceManager");
  tree_add_node(
    tree, ONO_SUBSCRIPTION_MANAGER, 0,
    NODE_SUBSCRIPTION_MANAGER, "SubscriptionManager"
  );
  tree_add_node(tree, ONO_ROOT_BLOCK, 0, NODE_BLOCK, "Root");

  // 0. Root/Ping (used by Focusrite Control 2 roundTripTime$)
  add_bool_sensor(tree, ONO_ROOT_BLOCK, "Ping", false, BIND_NONE, 0);

  // 1. DeviceInfo
  struct oca_node *dev_info = add_block(tree, ONO_ROOT_BLOCK, "DeviceInfo");
  add_string_sensor(
    tree, dev_info->ono, "ProductName",
    (card && card->name) ? card->name : "Scarlett 2i2 4th Gen"
  );
  add_uint32_sensor(
    tree, dev_info->ono, "ProductId",
    (card && card->pid) ? card->pid : 0x8219, 0, 0xFFFFFFFFu
  );
  add_string_sensor(tree, dev_info->ono, "VariantAttributes", "");
  add_string_sensor(
    tree, dev_info->ono, "SerialNumber",
    (card && card->serial) ? card->serial : "00000000"
  );
  char *fw_str = read_card_firmware_string(card);
  add_string_sensor(tree, dev_info->ono, "FirmwareVersion", fw_str);
  g_free(fw_str);

  // 2. AutoGain
  struct oca_node *ag_block = add_block(tree, ONO_ROOT_BLOCK, "AutoGain");
  add_uint32_sensor(tree, ag_block->ono, "DurationMs", 10000, 0, 60000);
  struct oca_node *mc_ag = add_block(tree, ag_block->ono, "MultiChannelAutoGain");
  tree->mc_ag_stage_node = add_string_sensor(tree, mc_ag->ono, "AutoGainStage", "idle");
  tree->mc_ag_stage_node->bind_kind = BIND_MC_AUTOGAIN_STAGE;
  add_bool_actuator(
    tree, mc_ag->ono, "StartAutoGainCommand", false, BIND_MC_AUTOGAIN_START, NULL, 0
  );
  add_bool_actuator(
    tree, mc_ag->ono, "CancelAutoGainCommand", false, BIND_MC_AUTOGAIN_CANCEL, NULL, 0
  );
  add_bool_actuator(
    tree, mc_ag->ono, "BeginChannelSelectionCommand", false, BIND_MC_AUTOGAIN_SELECT, NULL, 0
  );

  // 3. Inputs
  struct oca_node *inputs_block = add_block(tree, ONO_ROOT_BLOCK, "Inputs");
  static const char * const link_names[] = { "mono", "stereo" };
  static const char * const air_names[] = { "presence", "presence-drive" };

  int num_inputs = 0;
  for (int ch = 1; ch <= 8; ch++) {
    char buf[80];
    snprintf(buf, sizeof(buf), "Line In %d Gain Capture Volume", ch);
    struct alsa_elem *e_gain = elems ? get_elem_by_name(elems, buf) : NULL;
    if (!e_gain) {
      snprintf(buf, sizeof(buf), "Line In %d Air Capture Enum", ch);
      if (!elems || !get_elem_by_name(elems, buf))
        break;
    }
    num_inputs = ch;
  }
  if (num_inputs == 0)
    num_inputs = 2;

  for (int ch = 1; ch <= num_inputs; ch++) {
    char role_idx[16];
    snprintf(role_idx, sizeof(role_idx), "%d", ch - 1);
    struct oca_node *in_ch = add_block(tree, inputs_block->ono, role_idx);

    char ch_name[32];
    snprintf(ch_name, sizeof(ch_name), "Analogue %d", ch);
    add_string_sensor(tree, in_ch->ono, "Name", ch_name);

    char buf[80];
    snprintf(buf, sizeof(buf), "Line In %d Level Capture Enum", ch);
    struct alsa_elem *e_inst = elems ? get_elem_by_name(elems, buf) : NULL;
    add_string_sensor(tree, in_ch->ono, "Type", "preamp");

    add_level_sensor(tree, in_ch->ono, "Level", BIND_METER_HW_IN, NULL, ch - 1);
    add_level_sensor(tree, in_ch->ono, "Peak", BIND_METER_HW_PEAK, NULL, ch - 1);
    add_bool_sensor(
      tree, in_ch->ono, "Clipping", false, BIND_METER_HW_CLIP, ch - 1
    );

    snprintf(buf, sizeof(buf), "Line In %d Autogain Status Capture Enum", ch);
    struct alsa_elem *e_ag_st = elems ? get_elem_by_name(elems, buf) : NULL;
    const char *initial_ag_st = "off";
    if (e_ag_st && strcmp(map_autogain_status(e_ag_st), "in-progress") == 0) {
      tree->autogain_dismissed[ch - 1] = false;
      initial_ag_st = "in-progress";
    }
    struct oca_node *ag_st_node = add_string_sensor(
      tree, in_ch->ono, "AutoGainState", initial_ag_st
    );
    if (e_ag_st) {
      ag_st_node->bind_kind = BIND_AUTOGAIN_STATE;
      ag_st_node->elem = e_ag_st;
      ag_st_node->channel_idx = ch - 1;
    }
    tree->ag_state_nodes[ch - 1] = ag_st_node;

    add_string_actuator(tree, in_ch->ono, "CustomName", "");

    snprintf(buf, sizeof(buf), "Line In %d Link Capture Switch", ch);
    struct alsa_elem *e_link = elems ? get_elem_by_name(elems, buf) : NULL;
    add_switch(
      tree, in_ch->ono, "LinkMode", link_names, 2, 0,
      e_link ? BIND_ALSA_SWITCH : BIND_NONE, e_link, ch - 1
    );

    tree->ag_selected_nodes[ch - 1] = add_bool_actuator(
      tree, in_ch->ono, "MultiChannelAutoGainSelected",
      false, BIND_NONE, NULL, ch - 1
    );

    // Phantom power: check per-pair ('Line In 1-2 Phantom Power Capture Switch')
    // or per-channel ('Line In N Phantom Power Capture Switch')
    int pair_base = ((ch - 1) / 2) * 2 + 1;
    snprintf(
      buf, sizeof(buf),
      "Line In %d-%d Phantom Power Capture Switch",
      pair_base, pair_base + 1
    );
    struct alsa_elem *e_p48 = elems ? get_elem_by_name(elems, buf) : NULL;
    if (!e_p48 && elems) {
      snprintf(buf, sizeof(buf), "Line In %d Phantom Power Capture Switch", ch);
      e_p48 = get_elem_by_name(elems, buf);
    }
    add_bool_actuator(
      tree, in_ch->ono, "PhantomPower", false,
      e_p48 ? BIND_ALSA_BOOL : BIND_NONE, e_p48, ch - 1
    );

    snprintf(buf, sizeof(buf), "Line In %d Pad Capture Switch", ch);
    struct alsa_elem *e_pad = elems ? get_elem_by_name(elems, buf) : NULL;
    if (e_pad) {
      add_bool_actuator(
        tree, in_ch->ono, "Pad", false, BIND_ALSA_BOOL, e_pad, ch - 1
      );
    }

    snprintf(buf, sizeof(buf), "Line In %d Safe Capture Switch", ch);
    struct alsa_elem *e_safe = elems ? get_elem_by_name(elems, buf) : NULL;
    add_bool_actuator(
      tree, in_ch->ono, "Safe", false,
      e_safe ? BIND_ALSA_BOOL : BIND_NONE, e_safe, ch - 1
    );

    snprintf(buf, sizeof(buf), "Line In %d Air Capture Enum", ch);
    struct alsa_elem *e_air = elems ? get_elem_by_name(elems, buf) : NULL;
    if (!e_air && elems) {
      snprintf(buf, sizeof(buf), "Line In %d Air Capture Switch", ch);
      e_air = get_elem_by_name(elems, buf);
    }
    add_bool_actuator(
      tree, in_ch->ono, "AirOn", false,
      e_air ? BIND_AIR_ENABLED : BIND_NONE, e_air, ch - 1
    );
    add_switch(
      tree, in_ch->ono, "AirMode", air_names, 2, 0,
      (e_air && alsa_get_item_count(e_air) >= 3) ? BIND_AIR_MODE : BIND_NONE,
      e_air, ch - 1
    );

    if (e_inst) {
      add_bool_actuator(
        tree, in_ch->ono, "Instrument", false, BIND_ALSA_BOOL, e_inst, ch - 1
      );
    }

    snprintf(buf, sizeof(buf), "Line In %d Autogain Capture Switch", ch);
    struct alsa_elem *e_ag = elems ? get_elem_by_name(elems, buf) : NULL;
    tree->ag_switch_nodes[ch - 1] = add_bool_actuator(
      tree, in_ch->ono, "StartAutoGainCommand", false,
      e_ag ? BIND_AUTOGAIN_START : BIND_NONE, e_ag, ch - 1
    );
    add_bool_actuator(
      tree, in_ch->ono, "CancelAutoGainCommand", false,
      e_ag ? BIND_AUTOGAIN_CANCEL : BIND_NONE, e_ag, ch - 1
    );

    snprintf(buf, sizeof(buf), "Line In %d Gain Capture Volume", ch);
    struct alsa_elem *e_gain = elems ? get_elem_by_name(elems, buf) : NULL;
    add_gain(tree, in_ch->ono, "Gain", 0.0f, 0.0f, 69.0f, e_gain, '\0', 0);
  }
  tree->num_inputs = num_inputs;

  // 4. Outputs
  struct oca_node *outputs_block = add_block(tree, ONO_ROOT_BLOCK, "Outputs");
  for (int out_i = 1; out_i <= 8; out_i++) {
    char buf[80];
    snprintf(buf, sizeof(buf), "Analogue Output %02d Playback Enum", out_i);
    struct alsa_elem *e_out = elems ? get_elem_by_name(elems, buf) : NULL;
    if (!e_out && out_i > 2)
      break;

    char role_idx[16];
    snprintf(role_idx, sizeof(role_idx), "%d", out_i - 1);
    struct oca_node *out_ch = add_block(tree, outputs_block->ono, role_idx);

    char out_name[32];
    snprintf(out_name, sizeof(out_name), "Output %d", out_i);
    add_string_sensor(tree, out_ch->ono, "Name", out_name);
    add_level_sensor(
      tree, out_ch->ono, "Level", BIND_METER_MIX_OUT, NULL, out_i - 1
    );
    add_bool_sensor(
      tree, out_ch->ono, "SplitToMono", false, BIND_NONE, out_i - 1
    );

    if (e_out && alsa_get_item_count(e_out) > 0) {
      int icount = alsa_get_item_count(e_out);
      const char **inames = g_malloc0(sizeof(char *) * (size_t)icount);
      for (int k = 0; k < icount; k++)
        inames[k] = alsa_get_item_name(e_out, k);
      add_switch(
        tree, out_ch->ono, "RoutedSource",
        (const char * const *)inames, (uint16_t)icount, 0,
        BIND_ALSA_SWITCH, e_out, out_i - 1
      );
      g_free(inames);
    } else {
      static const char * const def_src[] = { "Mix A", "Playback 1-2" };
      add_switch(
        tree, out_ch->ono, "RoutedSource", def_src, 2, 0,
        BIND_NONE, NULL, out_i - 1
      );
    }
  }

  // 5. Mixes (Mix A)
  struct oca_node *mixes_block = add_block(tree, ONO_ROOT_BLOCK, "Mixes");
  struct oca_node *mix0 = add_block(tree, mixes_block->ono, "0");
  add_string_sensor(tree, mix0->ono, "Name", "Mix A");
  add_level_sensor(tree, mix0->ono, "LeftMeter", BIND_METER_MIX_OUT, NULL, 0);
  add_level_sensor(tree, mix0->ono, "RightMeter", BIND_METER_MIX_OUT, NULL, 1);
  add_string_actuator(tree, mix0->ono, "CustomName", "");
  add_gain(tree, mix0->ono, "Gain", 0.0f, -80.0f, 6.0f, NULL, '\0', 0);
  add_mute(tree, mix0->ono, "Mute", NULL);

  struct oca_node *channels_block = add_block(tree, mix0->ono, "Channels");
  static const char * const def_mix_ch_names[4] = {
    "Analogue 1", "Analogue 2", "Playback 1", "Playback 2"
  };
  for (int m_in = 1; m_in <= 4; m_in++) {
    char buf[80];
    snprintf(buf, sizeof(buf), "Mix A Input %02d Playback Volume", m_in);
    struct alsa_elem *e_mg = elems ? get_elem_by_name(elems, buf) : NULL;

    char role_idx[16];
    snprintf(role_idx, sizeof(role_idx), "%d", m_in - 1);
    struct oca_node *mch = add_block(tree, channels_block->ono, role_idx);

    const char *ch_label = def_mix_ch_names[m_in - 1];
    snprintf(buf, sizeof(buf), "Mixer Input %02d Capture Enum", m_in);
    struct alsa_elem *e_msrc = elems ? get_elem_by_name(elems, buf) : NULL;
    if (e_msrc) {
      int sidx = (int)alsa_get_elem_value(e_msrc);
      if (sidx > 0 && sidx < alsa_get_item_count(e_msrc)) {
        const char *iname = alsa_get_item_name(e_msrc, sidx);
        if (iname && *iname)
          ch_label = iname;
      }
    }

    add_string_sensor(tree, mch->ono, "Name", ch_label);
    add_string_sensor(
      tree, mch->ono, "Group",
      (m_in <= 2) ? "Analogue" : "Playback"
    );
    add_level_sensor(
      tree, mch->ono, "Level", BIND_METER_MIX_CHAN, e_mg, m_in - 1
    );
    add_bool_sensor(
      tree, mch->ono, "EffectiveMute", false, BIND_NONE, m_in - 1
    );
    add_string_actuator(tree, mch->ono, "CustomName", "");
    add_switch(
      tree, mch->ono, "LinkMode", link_names, 2,
      (m_in >= 3) ? 1 : 0, BIND_NONE, NULL, m_in - 1
    );
    add_gain(
      tree, mch->ono, "Gain", 0.0f, -80.0f, 12.0f, e_mg, 'A', m_in
    );
    float def_pan = (m_in == 3) ? -1.0f : ((m_in == 4) ? 1.0f : 0.0f);
    add_pan(tree, mch->ono, "Pan", def_pan);
    add_mute(tree, mch->ono, "Mute", NULL);
    add_bool_actuator(
      tree, mch->ono, "Solo", false, BIND_NONE, NULL, m_in - 1
    );
  }

  // 6. Settings
  struct oca_node *settings_block = add_block(tree, ONO_ROOT_BLOCK, "Settings");
  struct alsa_elem *e_dm = elems ? get_elem_by_prefix(
    elems, "Direct Monitor Playback"
  ) : NULL;
  add_bool_actuator(
    tree, settings_block->ono, "DirectMonitorEnabled", false,
    e_dm ? BIND_DIRECT_MON_ENABLED : BIND_NONE, e_dm, 0
  );
  add_switch(
    tree, settings_block->ono, "DirectMonitorMode", link_names, 2, 1,
    (e_dm && alsa_get_item_count(e_dm) >= 3) ? BIND_DIRECT_MON_MODE : BIND_NONE,
    e_dm, 0
  );

  return tree;
}

void remote_oca_tree_free(struct remote_oca_tree *tree) {
  if (!tree)
    return;
  g_hash_table_destroy(tree->by_ono);
  g_ptr_array_free(tree->nodes, TRUE);
  g_free(tree);
}

// --- Session & Subscription Management ---

static void subscription_free(gpointer ptr) {
  struct oca_subscription *sub = ptr;
  if (!sub)
    return;
  g_free(sub->context);
  g_free(sub);
}

struct remote_oca_session *remote_oca_session_create(
  bool                   is_secure,
  remote_oca_send_pdu_fn send_fn,
  void                  *send_user_data
) {
  struct remote_oca_session *s = g_malloc0(sizeof(struct remote_oca_session));
  s->is_secure = is_secure;
  s->proto_ver = OCA_PROTOCOL_VERSION;
  s->send_fn = send_fn;
  s->send_user_data = send_user_data;
  return s;
}

void remote_oca_session_free(struct remote_oca_session *session) {
  if (!session)
    return;
  g_list_free_full(session->subscriptions, subscription_free);
  g_free(session);
}

static void session_send_bytes(
  struct remote_oca_session *session,
  const uint8_t             *data,
  size_t                     len
) {
  if (session && session->send_fn && data && len > 0)
    session->send_fn(data, len, session->send_user_data);
}

// --- Notification Builder ---

static bool get_node_property_info(
  const struct oca_node *node,
  uint16_t              *def_level_out,
  uint16_t              *prop_idx_out,
  GByteArray            *val_buf
) {
  switch (node->kind) {
    case NODE_BOOL_ACTUATOR:
    case NODE_BOOL_SENSOR:
      *def_level_out = 5;
      *prop_idx_out = 1;
      oca_write_u8(val_buf, node->u8_val);
      return true;

    case NODE_MUTE:
      *def_level_out = 4;
      *prop_idx_out = 1;
      oca_write_u8(val_buf, node->u8_val);
      return true;

    case NODE_SWITCH:
      *def_level_out = 4;
      *prop_idx_out = 1;
      oca_write_u16(val_buf, node->u16_val);
      return true;

    case NODE_GAIN:
    case NODE_PAN:
    case NODE_LEVEL_SENSOR:
      *def_level_out = 4;
      *prop_idx_out = 1;
      oca_write_f32(val_buf, node->f32_val);
      return true;

    case NODE_UINT32_ACTUATOR:
    case NODE_UINT32_SENSOR:
      *def_level_out = 5;
      *prop_idx_out = 1;
      oca_write_u32(val_buf, node->u32_val);
      return true;

    case NODE_STRING_ACTUATOR:
    case NODE_STRING_SENSOR:
      *def_level_out = 5;
      *prop_idx_out = 1;
      oca_write_string(val_buf, node->str_val);
      return true;

    default:
      return false;
  }
}

static void emit_raw_notification_to_session(
  struct remote_oca_session *session,
  uint32_t                   emitter_ono,
  bool                       is_level_sensor,
  uint16_t                   prop_def_level,
  uint16_t                   prop_idx,
  const uint8_t             *val_bytes,
  size_t                     val_len
) {
  if (!session || !session->subscriptions)
    return;

  uint16_t pver = session->proto_ver ? session->proto_ver : OCA_PROTOCOL_VERSION;

  for (GList *l = session->subscriptions; l; l = l->next) {
    struct oca_subscription *sub = l->data;
    if (sub->emitter_ono != emitter_ono)
      continue;

    uint16_t emit_prop_level = prop_def_level;
    uint16_t emit_prop_idx = prop_idx;

    if (sub->filter_property) {
      // Allow LevelSensor (4,1) or (5,1) subscription compatibility
      bool match = (sub->prop_def_level == prop_def_level &&
                    sub->prop_index == prop_idx);
      if (!match && is_level_sensor &&
          sub->prop_def_level == 5 && sub->prop_index == 1) {
        match = true;
      }
      if (!match)
        continue;
      emit_prop_level = sub->prop_def_level;
      emit_prop_idx = sub->prop_index;
    }

    GByteArray *pdu = g_byte_array_sized_new(64);
    oca_write_u8(pdu, OCA_SYNC_BYTE);
    oca_write_u16(pdu, pver);
    size_t pdu_size_off = pdu->len;
    oca_write_u32(pdu, 0);
    oca_write_u8(pdu, OCA_PDU_NOTIFICATION);
    oca_write_u16(pdu, 1); // msgCount = 1

    size_t msg_start = pdu->len;
    oca_write_u32(pdu, 0); // notificationSize placeholder
    oca_write_u32(pdu, sub->sub_ono);
    oca_write_u16(pdu, sub->sub_def_level);
    oca_write_u16(pdu, sub->sub_method_idx);
    oca_write_u8(pdu, 2); // paramCount = 2
    oca_write_u16(pdu, sub->context_len);
    if (sub->context_len > 0 && sub->context)
      g_byte_array_append(pdu, sub->context, sub->context_len);

    oca_write_u32(pdu, emitter_ono);
    oca_write_u16(pdu, 1); // EventID.DefLevel = 1 (OcaRoot::PropertyChanged)
    oca_write_u16(pdu, 1); // EventID.EventIndex = 1
    oca_write_u16(pdu, emit_prop_level);
    oca_write_u16(pdu, emit_prop_idx);
    if (val_len > 0 && val_bytes)
      g_byte_array_append(pdu, val_bytes, (guint)val_len);
    oca_write_u8(pdu, 1); // OcaPropertyChangeType::CurrentChanged = 1

    oca_write_u32_at(pdu, msg_start, (uint32_t)(pdu->len - msg_start));
    oca_write_u32_at(pdu, pdu_size_off, (uint32_t)(pdu->len - 1));

    session_send_bytes(session, pdu->data, pdu->len);
    g_byte_array_unref(pdu);
  }
}

static void emit_node_notification_to_session(
  struct remote_oca_session *session,
  const struct oca_node     *node,
  uint16_t                   prop_def_level,
  uint16_t                   prop_idx,
  const uint8_t             *val_bytes,
  size_t                     val_len
) {
  if (!session || !session->is_secure || !node)
    return;

  emit_raw_notification_to_session(
    session,
    node->ono,
    node->kind == NODE_LEVEL_SENSOR,
    prop_def_level,
    prop_idx,
    val_bytes,
    val_len
  );
}

void remote_oca_set_onboarding_state(
  struct remote_oca_tree *tree,
  const char             *state,
  GList                  *insecure_sessions
) {
  if (!tree || !state)
    return;

  if (strcmp(tree->onboarding_state, state) == 0)
    return;

  g_strlcpy(tree->onboarding_state, state, sizeof(tree->onboarding_state));

  if (!insecure_sessions)
    return;

  GByteArray *val_buf = g_byte_array_sized_new(24);
  oca_write_string(val_buf, tree->onboarding_state);

  for (GList *l = insecure_sessions; l; l = l->next) {
    struct remote_oca_session *sess = l->data;
    if (sess && !sess->is_secure) {
      emit_raw_notification_to_session(
        sess, ONO_AUTH_AGENT, false, 3, 1, val_buf->data, val_buf->len
      );
    }
  }

  g_byte_array_unref(val_buf);
}

const char *remote_oca_get_onboarding_state(
  const struct remote_oca_tree *tree
) {
  return tree ? tree->onboarding_state : "ready";
}

static void add_changed_node(GPtrArray *changed_nodes, struct oca_node *node) {
  if (!changed_nodes || !node)
    return;
  for (guint i = 0; i < changed_nodes->len; i++) {
    if (g_ptr_array_index(changed_nodes, i) == node)
      return;
  }
  g_ptr_array_add(changed_nodes, node);
}

static void emit_node_property_changed(
  const struct oca_node *node,
  GList                 *secure_sessions,
  struct remote_oca_session *origin_session
) {
  GByteArray *val_buf = g_byte_array_sized_new(32);
  uint16_t prop_level = 0, prop_idx = 0;

  if (get_node_property_info(node, &prop_level, &prop_idx, val_buf)) {
    if (origin_session) {
      emit_node_notification_to_session(
        origin_session, node, prop_level, prop_idx,
        val_buf->data, val_buf->len
      );
    }
    for (GList *l = secure_sessions; l; l = l->next) {
      struct remote_oca_session *sess = l->data;
      if (sess && sess != origin_session) {
        emit_node_notification_to_session(
          sess, node, prop_level, prop_idx,
          val_buf->data, val_buf->len
        );
      }
    }
  }
  g_byte_array_unref(val_buf);
}

void remote_oca_notify_elem_changed(
  struct remote_oca_tree *tree,
  struct alsa_elem       *elem,
  GList                  *secure_sessions
) {
  if (!tree || !elem || !secure_sessions)
    return;

  bool ag_state_changed = false;

  for (guint i = 0; i < tree->nodes->len; i++) {
    struct oca_node *node = g_ptr_array_index(tree->nodes, i);
    if (node->elem == elem) {
      if (node->bind_kind == BIND_AUTOGAIN_START) {
        int ch = (node->channel_idx >= 0 && node->channel_idx < 16)
                 ? node->channel_idx : 0;
        long val = alsa_get_elem_value(elem);
        if (val == 1) {
          tree->autogain_dismissed[ch] = false;
          if (tree->ag_state_nodes[ch] &&
              sync_node_from_alsa(tree, tree->ag_state_nodes[ch])) {
            emit_node_property_changed(
              tree->ag_state_nodes[ch], secure_sessions, NULL
            );
            ag_state_changed = true;
          }
        }
      }
      if (sync_node_from_alsa(tree, node)) {
        emit_node_property_changed(node, secure_sessions, NULL);
        if (node->bind_kind == BIND_AUTOGAIN_STATE)
          ag_state_changed = true;
      }
    }
  }

  if (ag_state_changed && tree->mc_ag_stage_node) {
    if (g_strcmp0(tree->mc_ag_stage_node->str_val, "in-progress") == 0) {
      bool any_running = false;
      for (int i = 0; i < tree->num_inputs; i++) {
        if (tree->ag_state_nodes[i] &&
            g_strcmp0(tree->ag_state_nodes[i]->str_val, "in-progress") == 0) {
          any_running = true;
          break;
        }
      }
      if (!any_running) {
        g_free(tree->mc_ag_stage_node->str_val);
        tree->mc_ag_stage_node->str_val = g_strdup("complete");
        emit_node_property_changed(
          tree->mc_ag_stage_node, secure_sessions, NULL
        );
      }
    }
  }
}

void remote_oca_emit_meters(
  struct remote_oca_tree *tree,
  GList                  *secure_sessions
) {
  if (!tree || !secure_sessions)
    return;

  for (guint i = 0; i < tree->nodes->len; i++) {
    struct oca_node *node = g_ptr_array_index(tree->nodes, i);
    if (node->bind_kind == BIND_METER_HW_IN ||
        node->bind_kind == BIND_METER_HW_PEAK ||
        node->bind_kind == BIND_METER_HW_CLIP ||
        node->bind_kind == BIND_METER_MIX_OUT ||
        node->bind_kind == BIND_METER_MIX_CHAN) {
      if (sync_node_from_alsa(tree, node))
        emit_node_property_changed(node, secure_sessions, NULL);
    }
  }
}

// --- Response Helpers ---

static void append_response_msg(
  GByteArray    *resp_msgs,
  uint32_t       handle,
  uint8_t        status,
  uint8_t        param_count,
  const uint8_t *params,
  size_t         params_len
) {
  size_t start = resp_msgs->len;
  oca_write_u32(resp_msgs, 0);
  oca_write_u32(resp_msgs, handle);
  oca_write_u8(resp_msgs, status);
  oca_write_u8(resp_msgs, param_count);
  if (params_len > 0 && params)
    g_byte_array_append(resp_msgs, params, (guint)params_len);
  oca_write_u32_at(resp_msgs, start, (uint32_t)(resp_msgs->len - start));
}

void remote_oca_send_string_response(
  struct remote_oca_session *session,
  uint32_t                   handle,
  uint8_t                    status,
  const char                *str_val
) {
  if (!session)
    return;

  GByteArray *params = g_byte_array_new();
  uint8_t pcount = 0;
  if (status == OCA_STATUS_OK && str_val) {
    oca_write_string(params, str_val);
    pcount = 1;
  }

  uint16_t pver = session->proto_ver ? session->proto_ver : OCA_PROTOCOL_VERSION;
  GByteArray *pdu = g_byte_array_sized_new(32 + params->len);
  oca_write_u8(pdu, OCA_SYNC_BYTE);
  oca_write_u16(pdu, pver);
  size_t pdu_size_off = pdu->len;
  oca_write_u32(pdu, 0);
  oca_write_u8(pdu, OCA_PDU_RESPONSE);
  oca_write_u16(pdu, 1);

  append_response_msg(pdu, handle, status, pcount, params->data, params->len);
  oca_write_u32_at(pdu, pdu_size_off, (uint32_t)(pdu->len - 1));

  session_send_bytes(session, pdu->data, pdu->len);
  g_byte_array_unref(pdu);
  g_byte_array_unref(params);
}

void remote_oca_send_empty_response(
  struct remote_oca_session *session,
  uint32_t                   handle,
  uint8_t                    status
) {
  remote_oca_send_string_response(session, handle, status, NULL);
}

// --- Method Dispatchers ---

static void collect_members_recursive(
  struct remote_oca_tree *tree,
  const struct oca_node  *block,
  GPtrArray              *out_pairs
) {
  if (!block || !block->children)
    return;

  for (guint i = 0; i < block->children->len; i++) {
    uint32_t child_ono = g_array_index(block->children, uint32_t, i);
    struct oca_node *child = g_hash_table_lookup(
      tree->by_ono, GUINT_TO_POINTER(child_ono)
    );
    if (!child)
      continue;
    g_ptr_array_add(out_pairs, child);
    if (child->kind == NODE_BLOCK)
      collect_members_recursive(tree, child, out_pairs);
  }
}

static uint8_t dispatch_device_manager(
  struct remote_oca_tree *tree,
  uint16_t                def_level,
  uint16_t                method_idx,
  uint8_t                *out_pcount,
  GByteArray             *out_params
) {
  struct alsa_card *card = tree ? tree->card : NULL;

  if (def_level == 1) {
    if (method_idx == 1) {
      struct oca_class_def cdef = get_node_class_def(NODE_DEVICE_MANAGER);
      oca_write_class_id(out_params, &cdef);
      *out_pcount = 1;
      return OCA_STATUS_OK;
    }
    if (method_idx == 2) {
      oca_write_u8(out_params, 0);
      *out_pcount = 1;
      return OCA_STATUS_OK;
    }
    if (method_idx == 5) {
      oca_write_string(out_params, "DeviceManager");
      *out_pcount = 1;
      return OCA_STATUS_OK;
    }
  } else if (def_level == 2) {
    if (method_idx == 1) {
      oca_write_u8(out_params, 1);
      *out_pcount = 1;
      return OCA_STATUS_OK;
    }
    if (method_idx == 3) {
      oca_write_u32(out_params, 0);
      *out_pcount = 1;
      return OCA_STATUS_OK;
    }
    if (method_idx == 5) {
      oca_write_string(out_params, "DeviceManager");
      *out_pcount = 1;
      return OCA_STATUS_OK;
    }
  } else if (def_level == 3) {
    switch (method_idx) {
      case 1: // GetOcaVersion
        oca_write_u16(out_params, 2);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      case 2: { // GetModelGUID (8 bytes: 1B reserved, 3B Focusrite OUI, 4B model)
        uint32_t pid = (card && card->pid) ? card->pid : 0x8219;
        uint8_t guid[8] = {
          0x00, 0x00, 0x13, 0x0e,
          0x00, 0x00, (uint8_t)((pid >> 8) & 0xff), (uint8_t)(pid & 0xff)
        };
        g_byte_array_append(out_params, guid, 8);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      case 3: // GetSerialNumber
        oca_write_string(
          out_params, (card && card->serial) ? card->serial : "00000000"
        );
        *out_pcount = 1;
        return OCA_STATUS_OK;
      case 4: { // GetDeviceName -> Linux hostname
        const char *host = g_get_host_name();
        oca_write_string(out_params, (host && *host) ? host : "Linux");
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      case 5: // SetDeviceName
        *out_pcount = 0;
        return OCA_STATUS_OK;
      case 6: { // GetModelDescription -> (Manufacturer, Name, Version)
        // Focusrite Control 2 checks SemVer(">=2.0.0") against Version here;
        // actual hardware firmware version is exposed via DeviceInfo/FirmwareVersion.
        oca_write_string(out_params, "Focusrite");
        oca_write_string(
          out_params,
          (card && card->name) ? card->name : "Scarlett 2i2 4th Gen"
        );
        oca_write_string(out_params, "2.0.0");
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      case 7: // GetDeviceRole
        oca_write_string(out_params, "Audio Interface");
        *out_pcount = 1;
        return OCA_STATUS_OK;
      case 9: // GetUserInventoryCode
        oca_write_string(out_params, "");
        *out_pcount = 1;
        return OCA_STATUS_OK;
      case 11: // GetEnabled
        oca_write_u8(out_params, 1);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      case 13: // GetState
        oca_write_u16(out_params, 1);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      default:
        break;
    }
  }

  return OCA_STATUS_NOT_IMPLEMENTED;
}

static uint8_t dispatch_subscription_manager(
  struct remote_oca_session *session,
  uint16_t                   def_level,
  uint16_t                   method_idx,
  struct oca_reader         *r,
  uint8_t                   *out_pcount,
  GByteArray                *out_params
) {
  if (def_level == 1) {
    if (method_idx == 1) {
      struct oca_class_def cdef = get_node_class_def(NODE_SUBSCRIPTION_MANAGER);
      oca_write_class_id(out_params, &cdef);
      *out_pcount = 1;
      return OCA_STATUS_OK;
    }
    if (method_idx == 2) {
      oca_write_u8(out_params, 0);
      *out_pcount = 1;
      return OCA_STATUS_OK;
    }
    if (method_idx == 5) {
      oca_write_string(out_params, "SubscriptionManager");
      *out_pcount = 1;
      return OCA_STATUS_OK;
    }
  } else if (def_level == 3) {
    if (method_idx == 1) { // AddSubscription
      uint32_t emitter_ono, sub_ono;
      uint16_t ev_level, ev_idx, sub_level, sub_idx, ctx_len = 0;
      uint8_t *ctx = NULL;
      if (!oca_read_u32(r, &emitter_ono) ||
          !oca_read_u16(r, &ev_level) ||
          !oca_read_u16(r, &ev_idx) ||
          !oca_read_u32(r, &sub_ono) ||
          !oca_read_u16(r, &sub_level) ||
          !oca_read_u16(r, &sub_idx) ||
          !oca_read_blob(r, &ctx, &ctx_len)) {
        g_free(ctx);
        return OCA_STATUS_PARAMETER_ERROR;
      }

      struct oca_subscription *sub = g_malloc0(sizeof(struct oca_subscription));
      sub->emitter_ono = emitter_ono;
      sub->event_def_level = ev_level;
      sub->event_index = ev_idx;
      sub->filter_property = false;
      sub->sub_ono = sub_ono;
      sub->sub_def_level = sub_level;
      sub->sub_method_idx = sub_idx;
      sub->context = ctx;
      sub->context_len = ctx_len;
      session->subscriptions = g_list_append(session->subscriptions, sub);

      *out_pcount = 0;
      return OCA_STATUS_OK;
    }
    if (method_idx == 2) { // RemoveSubscription
      uint32_t emitter_ono, sub_ono;
      uint16_t ev_level, ev_idx, sub_level, sub_idx;
      if (!oca_read_u32(r, &emitter_ono) ||
          !oca_read_u16(r, &ev_level) ||
          !oca_read_u16(r, &ev_idx) ||
          !oca_read_u32(r, &sub_ono) ||
          !oca_read_u16(r, &sub_level) ||
          !oca_read_u16(r, &sub_idx))
        return OCA_STATUS_PARAMETER_ERROR;

      GList *l = session->subscriptions;
      while (l) {
        GList *next = l->next;
        struct oca_subscription *sub = l->data;
        if (!sub->filter_property &&
            sub->emitter_ono == emitter_ono &&
            sub->sub_ono == sub_ono) {
          session->subscriptions = g_list_delete_link(session->subscriptions, l);
          subscription_free(sub);
        }
        l = next;
      }
      *out_pcount = 0;
      return OCA_STATUS_OK;
    }
    if (method_idx == 5) { // AddPropertyChangeSubscription
      uint32_t emitter_ono, sub_ono;
      uint16_t prop_level, prop_idx, sub_level, sub_idx, ctx_len = 0;
      uint8_t *ctx = NULL;
      if (!oca_read_u32(r, &emitter_ono) ||
          !oca_read_u16(r, &prop_level) ||
          !oca_read_u16(r, &prop_idx) ||
          !oca_read_u32(r, &sub_ono) ||
          !oca_read_u16(r, &sub_level) ||
          !oca_read_u16(r, &sub_idx) ||
          !oca_read_blob(r, &ctx, &ctx_len)) {
        g_free(ctx);
        return OCA_STATUS_PARAMETER_ERROR;
      }

      struct oca_subscription *sub = g_malloc0(sizeof(struct oca_subscription));
      sub->emitter_ono = emitter_ono;
      sub->event_def_level = 1;
      sub->event_index = 1;
      sub->filter_property = true;
      sub->prop_def_level = prop_level;
      sub->prop_index = prop_idx;
      sub->sub_ono = sub_ono;
      sub->sub_def_level = sub_level;
      sub->sub_method_idx = sub_idx;
      sub->context = ctx;
      sub->context_len = ctx_len;
      session->subscriptions = g_list_append(session->subscriptions, sub);

      *out_pcount = 0;
      return OCA_STATUS_OK;
    }
    if (method_idx == 6) { // RemovePropertyChangeSubscription
      uint32_t emitter_ono, sub_ono;
      uint16_t prop_level, prop_idx, sub_level, sub_idx;
      if (!oca_read_u32(r, &emitter_ono) ||
          !oca_read_u16(r, &prop_level) ||
          !oca_read_u16(r, &prop_idx) ||
          !oca_read_u32(r, &sub_ono) ||
          !oca_read_u16(r, &sub_level) ||
          !oca_read_u16(r, &sub_idx))
        return OCA_STATUS_PARAMETER_ERROR;

      GList *l = session->subscriptions;
      while (l) {
        GList *next = l->next;
        struct oca_subscription *sub = l->data;
        if (sub->filter_property &&
            sub->emitter_ono == emitter_ono &&
            sub->prop_def_level == prop_level &&
            sub->prop_index == prop_idx &&
            sub->sub_ono == sub_ono) {
          session->subscriptions = g_list_delete_link(session->subscriptions, l);
          subscription_free(sub);
        }
        l = next;
      }
      *out_pcount = 0;
      return OCA_STATUS_OK;
    }
  }

  return OCA_STATUS_NOT_IMPLEMENTED;
}

// Returns true if response should be sent immediately; false if deferred (async)
static bool dispatch_insecure_command(
  struct remote_oca_session              *session,
  struct remote_oca_tree                 *tree,
  uint32_t                                handle,
  uint32_t                                target_ono,
  uint16_t                                def_level,
  uint16_t                                method_idx,
  struct oca_reader                      *r,
  const struct remote_oca_auth_callbacks *auth_cbs,
  void                                   *auth_user_data,
  uint8_t                                *out_status,
  uint8_t                                *out_pcount,
  GByteArray                             *out_params
) {
  if (target_ono == ONO_DEVICE_MANAGER) {
    *out_status = dispatch_device_manager(
      tree, def_level, method_idx, out_pcount, out_params
    );
    return true;
  }

  if (target_ono == ONO_SUBSCRIPTION_MANAGER) {
    *out_status = dispatch_subscription_manager(
      session, def_level, method_idx, r, out_pcount, out_params
    );
    return true;
  }

  if (target_ono == ONO_ROOT_BLOCK) {
    if (def_level == 1) {
      if (method_idx == 1) {
        struct oca_class_def cdef = get_node_class_def(NODE_BLOCK);
        oca_write_class_id(out_params, &cdef);
        *out_pcount = 1;
        *out_status = OCA_STATUS_OK;
        return true;
      }
      if (method_idx == 2) {
        oca_write_u8(out_params, 0);
        *out_pcount = 1;
        *out_status = OCA_STATUS_OK;
        return true;
      }
      if (method_idx == 5) {
        oca_write_string(out_params, "Root");
        *out_pcount = 1;
        *out_status = OCA_STATUS_OK;
        return true;
      }
    } else if (def_level == 2) { // OcaWorker
      if (method_idx == 1) { // GetEnabled
        oca_write_u8(out_params, 1);
        *out_pcount = 1;
        *out_status = OCA_STATUS_OK;
        return true;
      }
      if (method_idx == 8) { // GetLabel
        oca_write_string(out_params, "Root");
        *out_pcount = 1;
        *out_status = OCA_STATUS_OK;
        return true;
      }
      if (method_idx == 10) { // GetOwner
        oca_write_u32(out_params, 0);
        *out_pcount = 1;
        *out_status = OCA_STATUS_OK;
        return true;
      }
      if (method_idx == 13) { // GetPath -> (["Root"], [100])
        oca_write_u16(out_params, 1);
        oca_write_string(out_params, "Root");
        oca_write_u16(out_params, 1);
        oca_write_u32(out_params, ONO_ROOT_BLOCK);
        *out_pcount = 2;
        *out_status = OCA_STATUS_OK;
        return true;
      }
    } else if (def_level == 3) { // OcaBlock
      if (method_idx == 5) { // GetActionObjects / GetMembers -> [ONo=14]
        oca_write_u16(out_params, 1);
        oca_write_u32(out_params, ONO_AUTH_AGENT);
        oca_write_class_id(out_params, &AUTH_AGENT_CLASS_DEF);
        *out_pcount = 1;
        *out_status = OCA_STATUS_OK;
        return true;
      }
      if (method_idx == 6) { // GetActionObjectsRecursive / GetMembersRecursive
        oca_write_u16(out_params, 1);
        oca_write_u32(out_params, ONO_AUTH_AGENT);
        oca_write_class_id(out_params, &AUTH_AGENT_CLASS_DEF);
        oca_write_u32(out_params, ONO_ROOT_BLOCK);
        *out_pcount = 1;
        *out_status = OCA_STATUS_OK;
        return true;
      }
    }
    *out_status = OCA_STATUS_NOT_IMPLEMENTED;
    return true;
  }

  if (target_ono == ONO_AUTH_AGENT) {
    if (def_level == 1) {
      if (method_idx == 1) {
        oca_write_class_id(out_params, &AUTH_AGENT_CLASS_DEF);
        *out_pcount = 1;
        *out_status = OCA_STATUS_OK;
        return true;
      }
      if (method_idx == 2) {
        oca_write_u8(out_params, 0);
        *out_pcount = 1;
        *out_status = OCA_STATUS_OK;
        return true;
      }
      if (method_idx == 5) {
        oca_write_string(out_params, "AuthenticationAgent");
        *out_pcount = 1;
        *out_status = OCA_STATUS_OK;
        return true;
      }
    } else if (def_level == 2) { // OcaAgent (1.2)
      if (method_idx == 1) { // GetLabel
        oca_write_string(out_params, "AuthenticationAgent");
        *out_pcount = 1;
        *out_status = OCA_STATUS_OK;
        return true;
      }
      if (method_idx == 2) { // SetLabel
        char *lbl = NULL;
        if (!oca_read_string(r, &lbl)) {
          *out_status = OCA_STATUS_PARAMETER_ERROR;
          return true;
        }
        g_free(lbl);
        *out_pcount = 0;
        *out_status = OCA_STATUS_OK;
        return true;
      }
      if (method_idx == 3) { // GetOwner
        oca_write_u32(out_params, ONO_ROOT_BLOCK);
        *out_pcount = 1;
        *out_status = OCA_STATUS_OK;
        return true;
      }
      if (method_idx == 4) { // GetPath -> (["Root", "AuthenticationAgent"], [100, 14])
        oca_write_u16(out_params, 2);
        oca_write_string(out_params, "Root");
        oca_write_string(out_params, "AuthenticationAgent");
        oca_write_u16(out_params, 2);
        oca_write_u32(out_params, ONO_ROOT_BLOCK);
        oca_write_u32(out_params, ONO_AUTH_AGENT);
        *out_pcount = 2;
        *out_status = OCA_STATUS_OK;
        return true;
      }
    } else if (def_level == 3) {
      if (method_idx == 1) { // RequestApproval(OcaBlob clientKey, OcaBlob encName)
        uint8_t *pk_blob = NULL;
        uint8_t *name_blob = NULL;
        uint16_t pk_len = 0, name_len = 0;
        if (!oca_read_blob(r, &pk_blob, &pk_len) ||
            !oca_read_blob(r, &name_blob, &name_len)) {
          g_free(pk_blob);
          g_free(name_blob);
          *out_status = OCA_STATUS_PARAMETER_ERROR;
          return true;
        }
        if (auth_cbs && auth_cbs->on_request_approval) {
          auth_cbs->on_request_approval(
            handle, pk_blob, pk_len, name_blob, name_len, auth_user_data
          );
          g_free(pk_blob);
          g_free(name_blob);
          return false; // Callback sends response
        }
        g_free(pk_blob);
        g_free(name_blob);
        *out_status = OCA_STATUS_INVALID_REQUEST;
        return true;
      }
      if (method_idx == 2) { // SendQRData(OcaBlob qrData)
        uint8_t *qr_blob = NULL;
        uint16_t qr_len = 0;
        if (!oca_read_blob(r, &qr_blob, &qr_len)) {
          *out_status = OCA_STATUS_PARAMETER_ERROR;
          return true;
        }
        if (auth_cbs && auth_cbs->on_send_qr_data) {
          auth_cbs->on_send_qr_data(handle, qr_blob, qr_len, auth_user_data);
          g_free(qr_blob);
          return false; // Callback sends response
        }
        g_free(qr_blob);
        *out_status = OCA_STATUS_INVALID_REQUEST;
        return true;
      }
      if (method_idx == 3) { // GetOnboardingState()
        oca_write_string(
          out_params, tree ? tree->onboarding_state : "ready"
        );
        *out_pcount = 1;
        *out_status = OCA_STATUS_OK;
        return true;
      }
      if (method_idx == 4) { // GetApiVersion() -> uint16(1)
        oca_write_u16(out_params, 1);
        *out_pcount = 1;
        *out_status = OCA_STATUS_OK;
        return true;
      }
    }
    *out_status = OCA_STATUS_NOT_IMPLEMENTED;
    return true;
  }

  *out_status = OCA_STATUS_BAD_ONO;
  return true;
}

static uint8_t dispatch_secure_node(
  struct remote_oca_session *session,
  struct remote_oca_tree    *tree,
  struct oca_node           *node,
  uint16_t                   def_level,
  uint16_t                   method_idx,
  struct oca_reader         *r,
  uint8_t                   *out_pcount,
  GByteArray                *out_params,
  GPtrArray                 *changed_nodes
) {

  if (node->ono == ONO_DEVICE_MANAGER) {
    return dispatch_device_manager(
      tree, def_level, method_idx, out_pcount, out_params
    );
  }
  if (node->ono == ONO_SUBSCRIPTION_MANAGER) {
    return dispatch_subscription_manager(
      session, def_level, method_idx, r, out_pcount, out_params
    );
  }

  // Refresh live ALSA state before getters
  sync_node_from_alsa(tree, node);

  // Level 1: OcaRoot
  if (def_level == 1) {
    switch (method_idx) {
      case 1: { // GetClassIdentification
        struct oca_class_def cdef = get_node_class_def(node->kind);
        oca_write_class_id(out_params, &cdef);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      case 2: // GetLockable
        oca_write_u8(out_params, 0);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      case 5: // GetRole
        oca_write_string(out_params, node->role);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      default:
        return OCA_STATUS_NOT_IMPLEMENTED;
    }
  }

  // Level 2: OcaWorker
  if (def_level == 2) {
    switch (method_idx) {
      case 1: // GetEnabled
        oca_write_u8(out_params, node->enabled ? 1 : 0);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      case 2: { // SetEnabled
        uint8_t en;
        if (!oca_read_u8(r, &en))
          return OCA_STATUS_PARAMETER_ERROR;
        node->enabled = (en != 0);
        *out_pcount = 0;
        return OCA_STATUS_OK;
      }
      case 8: // GetLabel
        oca_write_string(out_params, node->label);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      case 9: { // SetLabel
        char *lbl = NULL;
        if (!oca_read_string(r, &lbl))
          return OCA_STATUS_PARAMETER_ERROR;
        g_free(node->label);
        node->label = lbl;
        *out_pcount = 0;
        return OCA_STATUS_OK;
      }
      case 10: // GetOwner
        oca_write_u32(out_params, node->owner_ono);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      case 13: { // GetPath -> (NamePath, ONoPath)
        const struct oca_node *chain[16];
        int depth = 0;
        const struct oca_node *cur = node;
        while (cur && depth < 16) {
          chain[depth++] = cur;
          if (cur->owner_ono == 0)
            break;
          cur = g_hash_table_lookup(
            tree->by_ono, GUINT_TO_POINTER(cur->owner_ono)
          );
        }
        oca_write_u16(out_params, (uint16_t)depth);
        for (int i = depth - 1; i >= 0; i--)
          oca_write_string(out_params, chain[i]->role);
        oca_write_u16(out_params, (uint16_t)depth);
        for (int i = depth - 1; i >= 0; i--)
          oca_write_u32(out_params, chain[i]->ono);
        *out_pcount = 2;
        return OCA_STATUS_OK;
      }
      default:
        return OCA_STATUS_NOT_IMPLEMENTED;
    }
  }

  // Class-specific methods
  switch (node->kind) {
    case NODE_BLOCK: {
      if (def_level == 3 && method_idx == 5) { // GetActionObjects / GetMembers
        uint16_t cnt = node->children ? (uint16_t)node->children->len : 0;
        oca_write_u16(out_params, cnt);
        for (uint16_t i = 0; i < cnt; i++) {
          uint32_t cono = g_array_index(node->children, uint32_t, i);
          struct oca_node *child = g_hash_table_lookup(
            tree->by_ono, GUINT_TO_POINTER(cono)
          );
          struct oca_class_def cdef = get_node_class_def(
            child ? child->kind : NODE_BLOCK
          );
          oca_write_u32(out_params, cono);
          oca_write_class_id(out_params, &cdef);
        }
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      if (def_level == 3 && method_idx == 6) { // GetActionObjectsRecursive
        GPtrArray *list = g_ptr_array_new();
        collect_members_recursive(tree, node, list);
        oca_write_u16(out_params, (uint16_t)list->len);
        for (guint i = 0; i < list->len; i++) {
          struct oca_node *m = g_ptr_array_index(list, i);
          struct oca_class_def cdef = get_node_class_def(m->kind);
          oca_write_u32(out_params, m->ono);
          oca_write_class_id(out_params, &cdef);
          oca_write_u32(out_params, m->owner_ono);
        }
        g_ptr_array_free(list, TRUE);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      break;
    }

    case NODE_BOOL_ACTUATOR: {
      if (def_level == 5 && method_idx == 1) { // GetSetting
        oca_write_u8(out_params, node->u8_val);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      if (def_level == 5 && method_idx == 2) { // SetSetting
        uint8_t val;
        if (!oca_read_u8(r, &val))
          return OCA_STATUS_PARAMETER_ERROR;
        uint8_t b = val ? 1 : 0;
        node->u8_val = b;

        if (node->bind_kind == BIND_ALSA_BOOL && node->elem) {
          alsa_set_elem_value(node->elem, b);
        } else if (node->bind_kind == BIND_AIR_ENABLED && node->elem) {
          int ch = (node->channel_idx >= 0 && node->channel_idx < 16)
                   ? node->channel_idx : 0;
          if (b) {
            int mode = tree->last_air_mode[ch];
            if (mode < 1 || mode > 2)
              mode = 1;
            if (alsa_get_item_count(node->elem) < 3)
              mode = 1;
            alsa_set_elem_value(node->elem, mode);
          } else {
            long cur = alsa_get_elem_value(node->elem);
            if (cur >= 1 && cur <= 2)
              tree->last_air_mode[ch] = (int)cur;
            alsa_set_elem_value(node->elem, 0);
          }
        } else if (node->bind_kind == BIND_AUTOGAIN_START) {
          int ch = (node->channel_idx >= 0 && node->channel_idx < 16)
                   ? node->channel_idx : 0;
          if (b) {
            tree->autogain_dismissed[ch] = false;
            if (node->elem)
              alsa_set_elem_value(node->elem, 1);
            if (tree->ag_state_nodes[ch]) {
              g_free(tree->ag_state_nodes[ch]->str_val);
              tree->ag_state_nodes[ch]->str_val = g_strdup("in-progress");
              add_changed_node(changed_nodes, tree->ag_state_nodes[ch]);
            }
          }
          node->u8_val = 0;
        } else if (node->bind_kind == BIND_AUTOGAIN_CANCEL) {
          int ch = (node->channel_idx >= 0 && node->channel_idx < 16)
                   ? node->channel_idx : 0;
          if (b) {
            if (tree->ag_state_nodes[ch] &&
                g_strcmp0(tree->ag_state_nodes[ch]->str_val, "in-progress") == 0 &&
                node->elem) {
              alsa_set_elem_value(node->elem, 0);
            }
            tree->autogain_dismissed[ch] = true;
            if (tree->ag_state_nodes[ch]) {
              g_free(tree->ag_state_nodes[ch]->str_val);
              tree->ag_state_nodes[ch]->str_val = g_strdup("off");
              add_changed_node(changed_nodes, tree->ag_state_nodes[ch]);
            }
            if (tree->mc_ag_stage_node &&
                g_strcmp0(tree->mc_ag_stage_node->str_val, "idle") != 0) {
              bool any_active = false;
              for (int i = 0; i < tree->num_inputs; i++) {
                if (tree->ag_state_nodes[i] &&
                    g_strcmp0(tree->ag_state_nodes[i]->str_val, "off") != 0) {
                  any_active = true;
                  break;
                }
              }
              if (!any_active) {
                g_free(tree->mc_ag_stage_node->str_val);
                tree->mc_ag_stage_node->str_val = g_strdup("idle");
                add_changed_node(changed_nodes, tree->mc_ag_stage_node);
              }
            }
          }
          node->u8_val = 0;
        } else if (node->bind_kind == BIND_MC_AUTOGAIN_SELECT) {
          if (b && tree->mc_ag_stage_node) {
            g_free(tree->mc_ag_stage_node->str_val);
            tree->mc_ag_stage_node->str_val = g_strdup("channel-selection");
            add_changed_node(changed_nodes, tree->mc_ag_stage_node);
          }
          node->u8_val = 0;
        } else if (node->bind_kind == BIND_MC_AUTOGAIN_START) {
          if (b) {
            if (tree->mc_ag_stage_node) {
              g_free(tree->mc_ag_stage_node->str_val);
              tree->mc_ag_stage_node->str_val = g_strdup("in-progress");
              add_changed_node(changed_nodes, tree->mc_ag_stage_node);
            }
            bool any_sel = false;
            for (int i = 0; i < tree->num_inputs; i++) {
              if (tree->ag_selected_nodes[i] && tree->ag_selected_nodes[i]->u8_val) {
                any_sel = true;
                break;
              }
            }
            for (int i = 0; i < tree->num_inputs; i++) {
              bool run = !any_sel ||
                         (tree->ag_selected_nodes[i] && tree->ag_selected_nodes[i]->u8_val);
              if (run) {
                tree->autogain_dismissed[i] = false;
                if (tree->ag_switch_nodes[i] && tree->ag_switch_nodes[i]->elem)
                  alsa_set_elem_value(tree->ag_switch_nodes[i]->elem, 1);
                if (tree->ag_state_nodes[i]) {
                  g_free(tree->ag_state_nodes[i]->str_val);
                  tree->ag_state_nodes[i]->str_val = g_strdup("in-progress");
                  add_changed_node(changed_nodes, tree->ag_state_nodes[i]);
                }
              }
            }
          }
          node->u8_val = 0;
        } else if (node->bind_kind == BIND_MC_AUTOGAIN_CANCEL) {
          if (b) {
            if (tree->mc_ag_stage_node) {
              g_free(tree->mc_ag_stage_node->str_val);
              tree->mc_ag_stage_node->str_val = g_strdup("idle");
              add_changed_node(changed_nodes, tree->mc_ag_stage_node);
            }
            for (int i = 0; i < tree->num_inputs; i++) {
              if (tree->ag_switch_nodes[i] && tree->ag_switch_nodes[i]->elem) {
                if (alsa_get_elem_value(tree->ag_switch_nodes[i]->elem))
                  alsa_set_elem_value(tree->ag_switch_nodes[i]->elem, 0);
              }
              tree->autogain_dismissed[i] = true;
              if (tree->ag_state_nodes[i]) {
                g_free(tree->ag_state_nodes[i]->str_val);
                tree->ag_state_nodes[i]->str_val = g_strdup("off");
                add_changed_node(changed_nodes, tree->ag_state_nodes[i]);
              }
            }
          }
          node->u8_val = 0;
        } else if (node->bind_kind == BIND_DIRECT_MON_ENABLED && node->elem) {
          if (b) {
            int mode = tree->last_dm_mode;
            if (alsa_get_item_count(node->elem) < 3)
              mode = 1;
            else if (mode < 1 || mode > 2)
              mode = 2;
            alsa_set_elem_value(node->elem, mode);
          } else {
            long cur = alsa_get_elem_value(node->elem);
            if (cur >= 1 && cur <= 2)
              tree->last_dm_mode = (int)cur;
            alsa_set_elem_value(node->elem, 0);
          }
        }

        *out_pcount = 0;
        add_changed_node(changed_nodes, node);
        return OCA_STATUS_OK;
      }
      break;
    }

    case NODE_BOOL_SENSOR: {
      if (def_level == 3 && method_idx == 1) { // OcaSensor::GetReadingState
        oca_write_u8(out_params, 1);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      if (def_level == 5 && method_idx == 1) { // GetReading
        oca_write_u8(out_params, node->u8_val);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      break;
    }

    case NODE_MUTE: {
      if (def_level == 4 && method_idx == 1) { // GetState
        oca_write_u8(out_params, node->u8_val);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      if (def_level == 4 && method_idx == 2) { // SetState (1=Muted, 2=Unmuted)
        uint8_t st;
        if (!oca_read_u8(r, &st))
          return OCA_STATUS_PARAMETER_ERROR;
        if (st != 1 && st != 2)
          return OCA_STATUS_OUT_OF_RANGE;
        node->u8_val = st;
        if (node->bind_kind == BIND_ALSA_MUTE && node->elem)
          alsa_set_elem_value(node->elem, (st == 2) ? 1 : 0);
        *out_pcount = 0;
        add_changed_node(changed_nodes, node);
        return OCA_STATUS_OK;
      }
      break;
    }

    case NODE_SWITCH: {
      if (def_level == 4 && method_idx == 1) { // GetPosition -> (pos, min, max)
        uint16_t max_pos = node->pos_count > 0 ? (node->pos_count - 1) : 0;
        oca_write_u16(out_params, node->u16_val);
        oca_write_u16(out_params, 0);
        oca_write_u16(out_params, max_pos);
        *out_pcount = 3;
        return OCA_STATUS_OK;
      }
      if (def_level == 4 && method_idx == 2) { // SetPosition(uint16)
        uint16_t pos;
        if (!oca_read_u16(r, &pos))
          return OCA_STATUS_PARAMETER_ERROR;
        if (node->pos_count > 0 && pos >= node->pos_count)
          return OCA_STATUS_OUT_OF_RANGE;
        node->u16_val = pos;

        if (node->bind_kind == BIND_ALSA_SWITCH && node->elem) {
          alsa_set_elem_value(node->elem, (long)pos);
        } else if (node->bind_kind == BIND_AIR_MODE && node->elem) {
          int ch = (node->channel_idx >= 0 && node->channel_idx < 16)
                   ? node->channel_idx : 0;
          tree->last_air_mode[ch] = (int)pos + 1;
          if (alsa_get_elem_value(node->elem) > 0)
            alsa_set_elem_value(node->elem, (long)pos + 1);
        } else if (node->bind_kind == BIND_DIRECT_MON_MODE && node->elem) {
          tree->last_dm_mode = (int)pos + 1;
          if (alsa_get_elem_value(node->elem) > 0)
            alsa_set_elem_value(node->elem, (long)pos + 1);
        }

        *out_pcount = 0;
        add_changed_node(changed_nodes, node);
        return OCA_STATUS_OK;
      }
      if (def_level == 4 && method_idx == 3) { // GetPositionName(uint16)
        uint16_t pos;
        if (!oca_read_u16(r, &pos))
          return OCA_STATUS_PARAMETER_ERROR;
        if (pos >= node->pos_count)
          return OCA_STATUS_OUT_OF_RANGE;
        oca_write_string(out_params, node->pos_names[pos]);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      if (def_level == 4 && method_idx == 5) { // GetPositionNames
        oca_write_u16(out_params, node->pos_count);
        for (uint16_t i = 0; i < node->pos_count; i++)
          oca_write_string(out_params, node->pos_names[i]);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      if (def_level == 4 && method_idx == 7) { // GetPositionEnableFlag(uint16)
        uint16_t pos;
        if (!oca_read_u16(r, &pos))
          return OCA_STATUS_PARAMETER_ERROR;
        if (pos >= node->pos_count)
          return OCA_STATUS_OUT_OF_RANGE;
        oca_write_u8(out_params, 1);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      if (def_level == 4 && method_idx == 9) { // GetPositionEnableFlags -> OcaList<OcaBoolean>
        oca_write_u16(out_params, node->pos_count);
        for (uint16_t i = 0; i < node->pos_count; i++)
          oca_write_u8(out_params, 1);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      break;
    }

    case NODE_GAIN:
    case NODE_PAN: {
      if (def_level == 4 && method_idx == 1) { // GetGain / GetPosition
        oca_write_f32(out_params, node->f32_val);
        oca_write_f32(out_params, node->f32_min);
        oca_write_f32(out_params, node->f32_max);
        *out_pcount = 3;
        return OCA_STATUS_OK;
      }
      if (def_level == 4 && method_idx == 2) { // SetGain / SetPosition
        float val;
        if (!oca_read_f32(r, &val) || !isfinite(val))
          return OCA_STATUS_PARAMETER_ERROR;
        if (val < node->f32_min)
          val = node->f32_min;
        else if (val > node->f32_max)
          val = node->f32_max;
        node->f32_val = val;

        if (node->kind == NODE_GAIN &&
            node->bind_kind == BIND_ALSA_GAIN &&
            node->elem) {
          alsa_elem_set_db(
            tree->card, node->elem, val, node->mix_letter, node->mix_in_num
          );
          node->f32_val = alsa_elem_to_db(node->elem);
        }

        *out_pcount = 0;
        add_changed_node(changed_nodes, node);
        return OCA_STATUS_OK;
      }
      break;
    }

    case NODE_UINT32_ACTUATOR:
    case NODE_UINT32_SENSOR: {
      if (node->kind == NODE_UINT32_SENSOR &&
          def_level == 3 && method_idx == 1) {
        oca_write_u8(out_params, 1);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      if (def_level == 5 && method_idx == 1) { // GetSetting / GetReading
        oca_write_u32(out_params, node->u32_val);
        oca_write_u32(out_params, node->u32_min);
        oca_write_u32(out_params, node->u32_max);
        *out_pcount = 3;
        return OCA_STATUS_OK;
      }
      if (node->kind == NODE_UINT32_ACTUATOR &&
          def_level == 5 && method_idx == 2) {
        uint32_t val;
        if (!oca_read_u32(r, &val))
          return OCA_STATUS_PARAMETER_ERROR;
        node->u32_val = val;
        *out_pcount = 0;
        add_changed_node(changed_nodes, node);
        return OCA_STATUS_OK;
      }
      break;
    }

    case NODE_STRING_ACTUATOR: {
      if (def_level == 5 && method_idx == 1) { // GetSetting
        oca_write_string(out_params, node->str_val);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      if (def_level == 5 && method_idx == 2) { // SetSetting
        char *s = NULL;
        if (!oca_read_string(r, &s))
          return OCA_STATUS_PARAMETER_ERROR;
        g_free(node->str_val);
        node->str_val = s;
        *out_pcount = 0;
        add_changed_node(changed_nodes, node);
        return OCA_STATUS_OK;
      }
      if (def_level == 5 && method_idx == 3) { // GetMaxLen
        oca_write_u16(out_params, 64);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      break;
    }

    case NODE_STRING_SENSOR: {
      if (def_level == 3 && method_idx == 1) {
        oca_write_u8(out_params, 1);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      if (def_level == 5 && method_idx == 1) { // GetReading
        oca_write_string(out_params, node->str_val);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      break;
    }

    case NODE_LEVEL_SENSOR: {
      if (def_level == 3 && method_idx == 1) {
        oca_write_u8(out_params, 1);
        *out_pcount = 1;
        return OCA_STATUS_OK;
      }
      if ((def_level == 4 || def_level == 5) && method_idx == 1) { // GetReading
        oca_write_f32(out_params, node->f32_val);
        oca_write_f32(out_params, node->f32_min);
        oca_write_f32(out_params, node->f32_max);
        *out_pcount = 3;
        return OCA_STATUS_OK;
      }
      break;
    }

    default:
      break;
  }

  return OCA_STATUS_NOT_IMPLEMENTED;
}

// --- OCP.1 Frame Stream Processor ---

bool remote_oca_process_buffer(
  struct remote_oca_session              *session,
  struct remote_oca_tree                 *tree,
  GList                                  *all_secure_sessions,
  GByteArray                             *rx_buf,
  const struct remote_oca_auth_callbacks *auth_cbs,
  void                                   *auth_user_data
) {
  if (!session || !rx_buf)
    return false;

  while (rx_buf->len >= 8) {
    if (rx_buf->data[0] != OCA_SYNC_BYTE)
      return false;

    uint16_t proto_ver;
    uint32_t pdu_size;
    memcpy(&proto_ver, rx_buf->data + 1, 2);
    memcpy(&pdu_size, rx_buf->data + 3, 4);
    proto_ver = GUINT16_FROM_BE(proto_ver);
    pdu_size = GUINT32_FROM_BE(pdu_size);

    if (proto_ver < 1 || proto_ver > 4 ||
        pdu_size < 7 ||
        pdu_size > 1024 * 1024)
      return false;

    session->proto_ver = proto_ver;

    size_t total_len = (size_t)pdu_size + 1;
    if (rx_buf->len < total_len)
      return true; // Wait for more bytes

    uint8_t pdu_type = rx_buf->data[7];

    if (pdu_type == OCA_PDU_KEEPALIVE) {
      // Echo identical KeepAlive PDU back immediately
      session_send_bytes(session, rx_buf->data, total_len);
      g_byte_array_remove_range(rx_buf, 0, (guint)total_len);
      continue;
    }

    if (total_len < 10)
      return false;

    uint16_t msg_count;
    memcpy(&msg_count, rx_buf->data + 8, 2);
    msg_count = GUINT16_FROM_BE(msg_count);

    if (pdu_type == OCA_PDU_COMMAND || pdu_type == OCA_PDU_COMMAND_RRQ) {
      bool want_resp = (pdu_type == OCA_PDU_COMMAND_RRQ);
      GByteArray *resp_msgs = g_byte_array_new();
      uint16_t resp_count = 0;
      GPtrArray *changed_nodes = g_ptr_array_new();

      size_t offset = 10;
      for (uint16_t m = 0; m < msg_count; m++) {
        if (offset + 17 > total_len)
          break;

        uint32_t msg_size, handle, target_ono;
        uint16_t def_level, method_idx;
        memcpy(&msg_size, rx_buf->data + offset, 4);
        memcpy(&handle, rx_buf->data + offset + 4, 4);
        memcpy(&target_ono, rx_buf->data + offset + 8, 4);
        memcpy(&def_level, rx_buf->data + offset + 12, 2);
        memcpy(&method_idx, rx_buf->data + offset + 14, 2);
        msg_size = GUINT32_FROM_BE(msg_size);
        handle = GUINT32_FROM_BE(handle);
        target_ono = GUINT32_FROM_BE(target_ono);
        def_level = GUINT16_FROM_BE(def_level);
        method_idx = GUINT16_FROM_BE(method_idx);

        if (msg_size < 17 || offset + msg_size > total_len)
          break;

        struct oca_reader param_reader = {
          .data = rx_buf->data + offset + 17,
          .len  = msg_size - 17,
          .pos  = 0
        };

        GByteArray *out_params = g_byte_array_new();
        uint8_t status = OCA_STATUS_BAD_ONO;
        uint8_t out_pcount = 0;
        bool send_now = true;

        if (!session->is_secure) {
          send_now = dispatch_insecure_command(
            session, tree, handle, target_ono, def_level, method_idx,
            &param_reader, auth_cbs, auth_user_data,
            &status, &out_pcount, out_params
          );
        } else if (tree) {
          struct oca_node *node = g_hash_table_lookup(
            tree->by_ono, GUINT_TO_POINTER(target_ono)
          );
          if (node) {
            status = dispatch_secure_node(
              session, tree, node, def_level, method_idx,
              &param_reader, &out_pcount, out_params, changed_nodes
            );
          } else {
            status = OCA_STATUS_BAD_ONO;
          }
        }

        if (want_resp && send_now) {
          append_response_msg(
            resp_msgs, handle, status, out_pcount,
            out_params->data, out_params->len
          );
          resp_count++;
        }

        g_byte_array_unref(out_params);
        offset += msg_size;
      }

      if (want_resp && resp_count > 0) {
        GByteArray *resp_pdu = g_byte_array_sized_new(10 + resp_msgs->len);
        oca_write_u8(resp_pdu, OCA_SYNC_BYTE);
        oca_write_u16(resp_pdu, session->proto_ver);
        oca_write_u32(resp_pdu, (uint32_t)(9 + resp_msgs->len));
        oca_write_u8(resp_pdu, OCA_PDU_RESPONSE);
        oca_write_u16(resp_pdu, resp_count);
        g_byte_array_append(resp_pdu, resp_msgs->data, resp_msgs->len);

        session_send_bytes(session, resp_pdu->data, resp_pdu->len);
        g_byte_array_unref(resp_pdu);
      }

      for (guint i = 0; i < changed_nodes->len; i++) {
        struct oca_node *cn = g_ptr_array_index(changed_nodes, i);
        emit_node_property_changed(cn, all_secure_sessions, session);
      }

      g_ptr_array_free(changed_nodes, TRUE);
      g_byte_array_unref(resp_msgs);
    }

    g_byte_array_remove_range(rx_buf, 0, (guint)total_len);
  }

  return true;
}
