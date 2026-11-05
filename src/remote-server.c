// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <ctype.h>
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alsa.h"
#include "dialog-pairing-qr.h"
#include "optional-state.h"
#include "remote-crypto.h"
#include "remote-mdns.h"
#include "remote-oca.h"
#include "remote-server.h"

#define WS_GUID          "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define PORT_RANGE_START 58322
#define PORT_RANGE_END   59321

struct status_listener {
  remote_server_status_cb cb;
  void                   *user_data;
};

struct remote_client {
  int                              ref_count;
  bool                             closing;
  bool                             is_secure;
  struct remote_server            *server;
  GSocketConnection               *connection;
  GInputStream                    *input;
  GOutputStream                   *output;
  GCancellable                    *cancellable;

  uint8_t                          rx_tmp[4096];
  GByteArray                      *ws_buf;
  GByteArray                      *ws_frag_buf;
  uint8_t                          ws_frag_opcode;

  GQueue                          *write_queue; // queue of GBytes *
  GBytes                          *current_write;
  bool                             is_writing;

  bool                             handshake_done;
  bool                             authenticated;
  char                            *client_key;  // 64-char lowercase hex
  char                            *client_name; // display name
  uint8_t                          client_pk[REMOTE_CRYPTO_PK_BYTES];

  // Secure stream state
  uint8_t                          session_key[REMOTE_CRYPTO_KEY_BYTES];
  struct remote_secretstream_state tx_stream;
  struct remote_secretstream_state rx_stream;
  bool                             rx_header_received;
  int                              challenge_stage; // 0=wait C, 1=wait S, 2=OCA
  uint8_t                          server_challenge[REMOTE_CRYPTO_CHALLENGE_BYTES];
  GByteArray                      *cipher_buf;
  GByteArray                      *plain_buf;

  // AES70 OCA session
  struct remote_oca_session       *oca_session;
};

struct remote_server {
  struct alsa_card       *card;
  GSocketService         *insecure_service;
  GSocketService         *secure_service;
  struct remote_mdns     *mdns;
  struct remote_oca_tree *oca_tree;

  uint16_t                insecure_port;
  uint16_t                secure_port;
  bool                    running;
  bool                    auto_approve;

  uint8_t                 server_pk[REMOTE_CRYPTO_PK_BYTES];
  uint8_t                 server_sk[REMOTE_CRYPTO_SK_BYTES];
  char                    server_pk_hex[REMOTE_CRYPTO_PK_BYTES * 2 + 1];

  GList                  *clients;
  GHashTable             *approved_keys; // key: 64-hex -> val: client_name
  GList                  *listeners;     // list of struct status_listener *
  guint                   meter_timer_id;

  // Pending QR pairing state
  bool                    has_pending_approval;
  struct remote_client   *pending_client;
  uint32_t                pending_handle;
  uint8_t                 pending_client_pk[REMOTE_CRYPTO_PK_BYTES];
  char                    pending_client_key[REMOTE_CRYPTO_PK_BYTES * 2 + 1];
  char                   *pending_client_name;
  uint8_t                 pending_qr_secret[REMOTE_CRYPTO_QR_SECRET_BYTES];
  char                    pending_qr_hex[REMOTE_CRYPTO_QR_HEX_LEN + 1];
};

static void client_read_start(struct remote_client *client);
static void client_close(struct remote_client *client);
static void notify_status_listeners(struct remote_server *server);

// --- Reference Counting & Write Queue ---

static struct remote_client *client_ref(struct remote_client *client) {
  client->ref_count++;
  return client;
}

static void client_unref(struct remote_client *client) {
  if (!client)
    return;
  client->ref_count--;
  if (client->ref_count > 0)
    return;

  if (client->current_write)
    g_bytes_unref(client->current_write);
  if (client->write_queue)
    g_queue_free_full(client->write_queue, (GDestroyNotify)g_bytes_unref);
  if (client->cancellable)
    g_object_unref(client->cancellable);
  if (client->connection)
    g_object_unref(client->connection);
  if (client->ws_buf)
    g_byte_array_unref(client->ws_buf);
  if (client->ws_frag_buf)
    g_byte_array_unref(client->ws_frag_buf);
  if (client->cipher_buf)
    g_byte_array_unref(client->cipher_buf);
  if (client->plain_buf)
    g_byte_array_unref(client->plain_buf);
  if (client->oca_session)
    remote_oca_session_free(client->oca_session);

  g_free(client->client_key);
  g_free(client->client_name);
  g_free(client);
}

static void client_flush_writes(struct remote_client *client);

static void on_write_complete(
  GObject      *source,
  GAsyncResult *result,
  gpointer      user_data
) {
  struct remote_client *client = user_data;
  GError *err = NULL;
  gsize written = 0;
  gboolean ok = g_output_stream_write_all_finish(
    G_OUTPUT_STREAM(source), result, &written, &err
  );

  if (client->current_write) {
    g_bytes_unref(client->current_write);
    client->current_write = NULL;
  }
  client->is_writing = false;

  if (!ok || client->closing) {
    g_clear_error(&err);
    if (!client->closing)
      client_close(client);
    client_unref(client);
    return;
  }

  client_flush_writes(client);
  client_unref(client);
}

static void client_flush_writes(struct remote_client *client) {
  if (!client || client->closing || client->is_writing || !client->output)
    return;
  if (g_queue_is_empty(client->write_queue))
    return;

  client->current_write = g_queue_pop_head(client->write_queue);
  client->is_writing = true;

  gsize size = 0;
  const void *buf = g_bytes_get_data(client->current_write, &size);

  client_ref(client);
  g_output_stream_write_all_async(
    client->output,
    buf,
    size,
    G_PRIORITY_DEFAULT,
    client->cancellable,
    on_write_complete,
    client
  );
}

static void client_queue_bytes(struct remote_client *client, GBytes *bytes) {
  if (!client || client->closing || !bytes) {
    if (bytes)
      g_bytes_unref(bytes);
    return;
  }
  g_queue_push_tail(client->write_queue, bytes);
  client_flush_writes(client);
}

static void send_ws_frame(
  struct remote_client *client,
  uint8_t               opcode,
  const uint8_t        *payload,
  size_t                len
) {
  if (!client || client->closing)
    return;

  GByteArray *frame = g_byte_array_sized_new((guint)(len + 10));
  uint8_t b0 = 0x80 | (opcode & 0x0F);
  g_byte_array_append(frame, &b0, 1);

  if (len <= 125) {
    uint8_t b1 = (uint8_t)len;
    g_byte_array_append(frame, &b1, 1);
  } else if (len <= 65535) {
    uint8_t b1 = 126;
    uint16_t l16 = GUINT16_TO_BE((uint16_t)len);
    g_byte_array_append(frame, &b1, 1);
    g_byte_array_append(frame, (const uint8_t *)&l16, 2);
  } else {
    uint8_t b1 = 127;
    uint64_t l64 = GUINT64_TO_BE((uint64_t)len);
    g_byte_array_append(frame, &b1, 1);
    g_byte_array_append(frame, (const uint8_t *)&l64, 8);
  }

  if (len > 0 && payload)
    g_byte_array_append(frame, payload, (guint)len);

  client_queue_bytes(client, g_byte_array_free_to_bytes(frame));
}

static void client_send_secure_plain(
  struct remote_client *client,
  const uint8_t        *plain,
  size_t                len
) {
  if (!client || client->closing || !plain || len == 0)
    return;

  size_t offset = 0;
  uint8_t chunk_out[2 + REMOTE_CRYPTO_STREAM_MAX_PLAIN + REMOTE_CRYPTO_STREAM_ABYTES];

  while (offset < len) {
    size_t chunk_len = len - offset;
    if (chunk_len > REMOTE_CRYPTO_STREAM_MAX_PLAIN)
      chunk_len = REMOTE_CRYPTO_STREAM_MAX_PLAIN;

    size_t cipher_len = 0;
    if (!remote_crypto_stream_push(
          &client->tx_stream,
          chunk_out + 2,
          &cipher_len,
          plain + offset,
          chunk_len
        )) {
      client_close(client);
      return;
    }

    uint16_t clen_be = GUINT16_TO_BE((uint16_t)cipher_len);
    memcpy(chunk_out, &clen_be, 2);
    send_ws_frame(client, 0x02, chunk_out, 2 + cipher_len);
    offset += chunk_len;
  }
}

static void on_oca_send_pdu(
  const uint8_t *pdu,
  size_t         len,
  void          *user_data
) {
  struct remote_client *client = user_data;
  if (!client || client->closing)
    return;

  if (client->is_secure)
    client_send_secure_plain(client, pdu, len);
  else
    send_ws_frame(client, 0x02, pdu, len);
}

// --- State Persistence ---

static void save_approved_keys(struct remote_server *server) {
  if (!server || !server->card)
    return;

  GString *str = g_string_new("");
  GHashTableIter iter;
  gpointer k, v;
  g_hash_table_iter_init(&iter, server->approved_keys);
  while (g_hash_table_iter_next(&iter, &k, &v)) {
    const char *key_hex = k;
    const char *name = v ? (const char *)v : "";
    if (str->len > 0)
      g_string_append_c(str, ';');
    g_string_append(str, key_hex);
    if (*name) {
      g_string_append_c(str, ':');
      for (const char *p = name; *p; p++) {
        if (*p != ';' && *p != ':' && *p != '\n' && *p != '\r')
          g_string_append_c(str, *p);
      }
    }
  }

  optional_state_save(
    server->card, CONFIG_SECTION_DEVICE, "remote_approved_keys", str->str
  );
  g_string_free(str, TRUE);
}

static void load_persisted_state(struct remote_server *server) {
  bool have_keypair = false;

  GHashTable *dev_state = optional_state_load(
    server->card, CONFIG_SECTION_DEVICE
  );
  if (dev_state) {
    const char *pk_hex = g_hash_table_lookup(dev_state, "remote_server_pk");
    const char *sk_hex = g_hash_table_lookup(dev_state, "remote_server_sk");
    if (pk_hex && sk_hex &&
        remote_hex2bin(pk_hex, strlen(pk_hex), server->server_pk,
                       REMOTE_CRYPTO_PK_BYTES) &&
        remote_hex2bin(sk_hex, strlen(sk_hex), server->server_sk,
                       REMOTE_CRYPTO_SK_BYTES)) {
      remote_bin2hex(
        server->server_pk, REMOTE_CRYPTO_PK_BYTES, server->server_pk_hex
      );
      have_keypair = true;
    }

    const char *rem_auto = g_hash_table_lookup(
      dev_state, "remote_auto_approve"
    );
    if (rem_auto)
      server->auto_approve = (strcmp(rem_auto, "true") == 0);

    const char *approved = g_hash_table_lookup(
      dev_state, "remote_approved_keys"
    );
    if (approved && *approved) {
      char **entries = g_strsplit(approved, ";", -1);
      for (int i = 0; entries && entries[i]; i++) {
        char *colon = strchr(entries[i], ':');
        const char *name = "Dispositivo vinculado";
        size_t klen = strlen(entries[i]);
        if (colon) {
          klen = (size_t)(colon - entries[i]);
          if (colon[1])
            name = colon + 1;
        }
        uint8_t tmp_pk[REMOTE_CRYPTO_PK_BYTES];
        if (remote_hex2bin(entries[i], klen, tmp_pk, REMOTE_CRYPTO_PK_BYTES)) {
          char norm_hex[REMOTE_CRYPTO_PK_BYTES * 2 + 1];
          remote_bin2hex(tmp_pk, REMOTE_CRYPTO_PK_BYTES, norm_hex);
          g_hash_table_replace(
            server->approved_keys, g_strdup(norm_hex), g_strdup(name)
          );
        }
      }
      g_strfreev(entries);
    }

    g_hash_table_destroy(dev_state);
  }

  if (!have_keypair) {
    if (remote_crypto_kx_keypair(server->server_pk, server->server_sk)) {
      char sk_hex[REMOTE_CRYPTO_SK_BYTES * 2 + 1];
      remote_bin2hex(
        server->server_pk, REMOTE_CRYPTO_PK_BYTES, server->server_pk_hex
      );
      remote_bin2hex(server->server_sk, REMOTE_CRYPTO_SK_BYTES, sk_hex);
      if (server->card) {
        optional_state_save(
          server->card, CONFIG_SECTION_DEVICE,
          "remote_server_pk", server->server_pk_hex
        );
        optional_state_save(
          server->card, CONFIG_SECTION_DEVICE,
          "remote_server_sk", sk_hex
        );
      }
      memset(sk_hex, 0, sizeof(sk_hex));
    }
  }
}

static GList *get_authenticated_secure_sessions(struct remote_server *server) {
  GList *list = NULL;
  for (GList *l = server->clients; l; l = l->next) {
    struct remote_client *c = l->data;
    if (!c->closing && c->is_secure && c->authenticated && c->oca_session)
      list = g_list_prepend(list, c->oca_session);
  }
  return list;
}

static GList *get_insecure_sessions(struct remote_server *server) {
  GList *list = NULL;
  for (GList *l = server->clients; l; l = l->next) {
    struct remote_client *c = l->data;
    if (!c->closing && !c->is_secure && c->oca_session)
      list = g_list_prepend(list, c->oca_session);
  }
  return list;
}

static void set_onboarding_state(
  struct remote_server *server,
  const char           *state
) {
  if (!server || !server->oca_tree || !state)
    return;

  GList *insec = get_insecure_sessions(server);
  remote_oca_set_onboarding_state(server->oca_tree, state, insec);
  g_list_free(insec);
}

// --- Authentication Agent Callbacks (Insecure Port) ---

static void approve_client_key(
  struct remote_server *server,
  const char           *norm_key_hex,
  const char           *client_name
) {
  const char *display = (client_name && *client_name)
                        ? client_name : "Focusrite Control 2 Mobile";
  g_hash_table_replace(
    server->approved_keys, g_strdup(norm_key_hex), g_strdup(display)
  );
  save_approved_keys(server);
}

static char *decode_client_name_blob(
  const uint8_t  server_sk[REMOTE_CRYPTO_SK_BYTES],
  const uint8_t  client_pk[REMOTE_CRYPTO_PK_BYTES],
  const uint8_t *blob,
  size_t         blob_len
) {
  if (!blob || blob_len == 0 || blob_len > 512)
    return g_strdup("Dispositivo móvil");

  uint8_t plain[512];
  size_t plain_len = 0;
  const char *src = NULL;
  size_t src_len = 0;

  if (remote_crypto_decrypt_box(
        server_sk, client_pk, blob, blob_len, plain, &plain_len
      ) && plain_len > 0) {
    src = (const char *)plain;
    src_len = plain_len;
  } else if (g_utf8_validate((const char *)blob, (gssize)blob_len, NULL)) {
    src = (const char *)blob;
    src_len = blob_len;
  }

  if (!src || src_len == 0 || !g_utf8_validate(src, (gssize)src_len, NULL))
    return g_strdup("Dispositivo móvil");

  GString *clean = g_string_sized_new(src_len + 1);
  for (size_t i = 0; i < src_len; i++) {
    unsigned char ch = (unsigned char)src[i];
    if (ch >= 0x20 && ch != 0x7F)
      g_string_append_c(clean, (char)ch);
  }

  if (clean->len == 0) {
    g_string_free(clean, TRUE);
    return g_strdup("Dispositivo móvil");
  }
  return g_string_free(clean, FALSE);
}

static void on_auth_request_approval(
  uint32_t       handle,
  const uint8_t *client_key_blob,
  size_t         client_key_len,
  const uint8_t *encrypted_name_blob,
  size_t         encrypted_name_len,
  void          *user_data
) {
  struct remote_client *client = user_data;
  struct remote_server *server = client->server;

  uint8_t client_pk[REMOTE_CRYPTO_PK_BYTES];
  if (client_key_blob && client_key_len == REMOTE_CRYPTO_PK_BYTES) {
    memcpy(client_pk, client_key_blob, REMOTE_CRYPTO_PK_BYTES);
  } else if (client_key_blob &&
             client_key_len == REMOTE_CRYPTO_PK_BYTES * 2 &&
             remote_hex2bin(
               (const char *)client_key_blob,
               client_key_len,
               client_pk,
               REMOTE_CRYPTO_PK_BYTES
             )) {
    // Accepted hex-encoded public key
  } else {
    remote_oca_send_empty_response(
      client->oca_session, handle, OCA_STATUS_PARAMETER_ERROR
    );
    return;
  }

  char norm_hex[REMOTE_CRYPTO_PK_BYTES * 2 + 1];
  remote_bin2hex(client_pk, REMOTE_CRYPTO_PK_BYTES, norm_hex);

  char *decoded_name = decode_client_name_blob(
    server->server_sk, client_pk, encrypted_name_blob, encrypted_name_len
  );

  g_free(client->client_key);
  client->client_key = g_strdup(norm_hex);
  g_free(client->client_name);
  client->client_name = g_strdup(decoded_name);

  // Generate 128-hex encrypted QR code for this client's X25519 public key
  if (!remote_crypto_create_qr_payload(
        server->server_sk,
        client_pk,
        server->pending_qr_secret,
        server->pending_qr_hex
      )) {
    g_free(decoded_name);
    remote_oca_send_empty_response(
      client->oca_session, handle, OCA_STATUS_DEVICE_ERROR
    );
    return;
  }

  server->has_pending_approval = true;
  server->pending_client = client;
  server->pending_handle = handle;
  memcpy(server->pending_client_pk, client_pk, REMOTE_CRYPTO_PK_BYTES);
  g_strlcpy(
    server->pending_client_key, norm_hex, sizeof(server->pending_client_key)
  );
  g_free(server->pending_client_name);
  server->pending_client_name = decoded_name;

  bool fast_qr = server->auto_approve ||
                 g_hash_table_contains(server->approved_keys, norm_hex);
  if (fast_qr)
    approve_client_key(server, norm_hex, server->pending_client_name);

  const char *next_state = fast_qr ? "scanning" : "pending";
  remote_oca_send_string_response(
    client->oca_session, handle, OCA_STATUS_OK, next_state
  );
  set_onboarding_state(server, next_state);
  notify_status_listeners(server);

  // Present the pairing / QR window automatically if the main GUI is running
  if (server->card && server->card->window_main &&
      gtk_widget_get_visible(server->card->window_main)) {
    dialog_pairing_qr_show(
      GTK_WINDOW(server->card->window_main), server
    );
  }
}

static void on_auth_send_qr_data(
  uint32_t       handle,
  const uint8_t *qr_blob,
  size_t         qr_blob_len,
  void          *user_data
) {
  struct remote_client *client = user_data;
  struct remote_server *server = client->server;

  if (!server->has_pending_approval || !qr_blob || qr_blob_len == 0) {
    remote_oca_send_string_response(
      client->oca_session, handle, OCA_STATUS_OK, "rejected"
    );
    return;
  }

  if (!remote_crypto_verify_qr_response(
        server->server_sk,
        server->pending_client_pk,
        server->pending_qr_secret,
        qr_blob,
        qr_blob_len
      )) {
    remote_oca_send_string_response(
      client->oca_session, handle, OCA_STATUS_OK, "rejected"
    );
    return;
  }

  approve_client_key(
    server, server->pending_client_key, server->pending_client_name
  );

  server->has_pending_approval = false;
  server->pending_client = NULL;
  memset(server->pending_qr_secret, 0, sizeof(server->pending_qr_secret));
  server->pending_qr_hex[0] = '\0';

  remote_oca_send_string_response(
    client->oca_session, handle, OCA_STATUS_OK, "approved"
  );
  set_onboarding_state(server, "ready");
  notify_status_listeners(server);
}

static const struct remote_oca_auth_callbacks AUTH_CALLBACKS = {
  .on_request_approval = on_auth_request_approval,
  .on_send_qr_data     = on_auth_send_qr_data
};

// --- Secure Stream Decryption & Challenge-Response ---

static bool process_secure_binary_payload(
  struct remote_client *client,
  const uint8_t        *data,
  size_t                len
) {
  struct remote_server *server = client->server;
  g_byte_array_append(client->cipher_buf, data, (guint)len);

  if (!client->rx_header_received) {
    if (client->cipher_buf->len < REMOTE_CRYPTO_STREAM_HDR_BYTES)
      return true;

    if (!remote_crypto_stream_init_pull(
          &client->rx_stream,
          client->cipher_buf->data,
          client->session_key
        )) {
      g_warning("remote_server: secretstream_init_pull failed");
      return false;
    }
    g_byte_array_remove_range(
      client->cipher_buf, 0, REMOTE_CRYPTO_STREAM_HDR_BYTES
    );
    client->rx_header_received = true;
  }

  while (client->cipher_buf->len >= 2) {
    uint16_t clen_be;
    memcpy(&clen_be, client->cipher_buf->data, 2);
    uint16_t clen = GUINT16_FROM_BE(clen_be);

    if (clen < REMOTE_CRYPTO_STREAM_ABYTES ||
        clen > REMOTE_CRYPTO_STREAM_MAX_PLAIN + REMOTE_CRYPTO_STREAM_ABYTES) {
      g_warning("remote_server: Invalid secretstream chunk length %u", clen);
      return false;
    }

    if (client->cipher_buf->len < (guint)(2 + clen))
      break;

    uint8_t plain_chunk[REMOTE_CRYPTO_STREAM_MAX_PLAIN];
    size_t plain_len = 0;
    if (!remote_crypto_stream_pull(
          &client->rx_stream,
          plain_chunk,
          &plain_len,
          client->cipher_buf->data + 2,
          clen
        )) {
      g_warning("remote_server: secretstream decryption failed");
      return false;
    }

    g_byte_array_remove_range(client->cipher_buf, 0, 2 + clen);
    if (plain_len > 0)
      g_byte_array_append(client->plain_buf, plain_chunk, (guint)plain_len);
  }

  // Stage 0: Expect 32-byte client challenge (must differ from server_challenge)
  if (client->challenge_stage == 0 &&
      client->plain_buf->len >= REMOTE_CRYPTO_CHALLENGE_BYTES) {
    if (memcmp(client->plain_buf->data, client->server_challenge,
               REMOTE_CRYPTO_CHALLENGE_BYTES) == 0) {
      g_warning("remote_server: Reflection attack in challenge stage 0");
      return false;
    }
    // Echo client's 32-byte challenge back encrypted
    client_send_secure_plain(
      client, client->plain_buf->data, REMOTE_CRYPTO_CHALLENGE_BYTES
    );
    g_byte_array_remove_range(
      client->plain_buf, 0, REMOTE_CRYPTO_CHALLENGE_BYTES
    );
    client->challenge_stage = 1;
  }

  // Stage 1: Expect our 32-byte server_challenge echoed back
  if (client->challenge_stage == 1 &&
      client->plain_buf->len >= REMOTE_CRYPTO_CHALLENGE_BYTES) {
    if (memcmp(client->plain_buf->data, client->server_challenge,
               REMOTE_CRYPTO_CHALLENGE_BYTES) != 0) {
      g_warning("remote_server: Challenge verification failed in stage 1");
      return false;
    }
    g_byte_array_remove_range(
      client->plain_buf, 0, REMOTE_CRYPTO_CHALLENGE_BYTES
    );
    client->challenge_stage = 2;
    client->authenticated = true;
    notify_status_listeners(server);
  }

  // Stage 2: Process standard AES70 OCP.1 PDUs
  if (client->challenge_stage == 2 && client->plain_buf->len > 0) {
    GList *sec_sessions = get_authenticated_secure_sessions(server);
    bool ok = remote_oca_process_buffer(
      client->oca_session,
      server->oca_tree,
      sec_sessions,
      client->plain_buf,
      NULL,
      NULL
    );
    g_list_free(sec_sessions);
    if (!ok)
      return false;
  }

  return true;
}

// --- WebSocket Handshake & Framing ---

static char *compute_ws_accept(const char *key) {
  char *concat = g_strconcat(key, WS_GUID, NULL);
  GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA1);
  g_checksum_update(checksum, (const guchar *)concat, (gssize)strlen(concat));

  guint8 digest[20];
  gsize digest_len = sizeof(digest);
  g_checksum_get_digest(checksum, digest, &digest_len);
  char *accept_key = g_base64_encode(digest, digest_len);

  g_checksum_free(checksum);
  g_free(concat);
  return accept_key;
}

static void send_http_error_and_close(
  struct remote_client *client,
  const char           *status_line
) {
  char *resp = g_strdup_printf(
    "HTTP/1.1 %s\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n"
    "\r\n",
    status_line
  );
  client_queue_bytes(client, g_bytes_new_take(resp, strlen(resp)));
}

static bool handle_ws_handshake(struct remote_client *client) {
  const char *raw = (const char *)client->ws_buf->data;
  gssize raw_len = (gssize)client->ws_buf->len;

  const char *hdr_end = g_strstr_len(raw, raw_len, "\r\n\r\n");
  if (!hdr_end) {
    if (client->ws_buf->len > 16384)
      return false;
    return true;
  }

  size_t hdr_bytes = (size_t)(hdr_end - raw) + 4;
  char *headers = g_strndup(raw, hdr_end - raw);
  char **lines = g_strsplit(headers, "\r\n", -1);
  g_free(headers);

  if (!lines || !lines[0]) {
    g_strfreev(lines);
    return false;
  }

  // Parse request line: GET <path> HTTP/1.1
  char *req_path = NULL;
  if (g_ascii_strncasecmp(lines[0], "GET ", 4) == 0) {
    const char *p = lines[0] + 4;
    while (*p == ' ')
      p++;
    const char *end_p = strchr(p, ' ');
    if (end_p)
      req_path = g_strndup(p, (gsize)(end_p - p));
  }

  if (!req_path) {
    g_strfreev(lines);
    return false;
  }

  char *sec_key = NULL;
  char *sec_proto = NULL;

  for (int i = 1; lines[i]; i++) {
    if (g_ascii_strncasecmp(lines[i], "Sec-WebSocket-Key:", 18) == 0) {
      const char *v = lines[i] + 18;
      while (*v == ' ' || *v == '\t')
        v++;
      g_free(sec_key);
      sec_key = g_strstrip(g_strdup(v));
    } else if (g_ascii_strncasecmp(
                 lines[i], "Sec-WebSocket-Protocol:", 23
               ) == 0) {
      const char *v = lines[i] + 23;
      while (*v == ' ' || *v == '\t')
        v++;
      const char *comma = strchr(v, ',');
      g_free(sec_proto);
      if (comma)
        sec_proto = g_strstrip(g_strndup(v, (gsize)(comma - v)));
      else
        sec_proto = g_strstrip(g_strdup(v));
    }
  }
  g_strfreev(lines);

  if (!sec_key || !*sec_key) {
    g_free(req_path);
    g_free(sec_key);
    g_free(sec_proto);
    send_http_error_and_close(client, "426 Upgrade Required");
    return false;
  }

  struct remote_server *server = client->server;

  if (client->is_secure) {
    const char *kstart = req_path;
    while (*kstart == '/')
      kstart++;
    size_t klen = strcspn(kstart, "/?#");

    if (klen != REMOTE_CRYPTO_PK_BYTES * 2 ||
        !remote_hex2bin(kstart, klen, client->client_pk,
                        REMOTE_CRYPTO_PK_BYTES)) {
      g_free(req_path);
      g_free(sec_key);
      g_free(sec_proto);
      send_http_error_and_close(client, "400 Bad Request");
      return false;
    }

    char norm_hex[REMOTE_CRYPTO_PK_BYTES * 2 + 1];
    remote_bin2hex(client->client_pk, REMOTE_CRYPTO_PK_BYTES, norm_hex);
    g_free(client->client_key);
    client->client_key = g_strdup(norm_hex);

    const char *known_name = g_hash_table_lookup(
      server->approved_keys, norm_hex
    );
    if (!server->auto_approve && !known_name) {
      g_free(req_path);
      g_free(sec_key);
      g_free(sec_proto);
      send_http_error_and_close(client, "403 Forbidden");
      return false;
    }

    if (known_name && *known_name) {
      g_free(client->client_name);
      client->client_name = g_strdup(known_name);
    }

    if (!remote_crypto_derive_server_key(
          client->session_key,
          server->server_pk,
          server->server_sk,
          client->client_pk
        )) {
      g_free(req_path);
      g_free(sec_key);
      g_free(sec_proto);
      send_http_error_and_close(client, "500 Internal Server Error");
      return false;
    }
  }

  g_free(req_path);

  char *accept_key = compute_ws_accept(sec_key);
  GString *resp = g_string_new(
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
  );
  g_string_append_printf(resp, "Sec-WebSocket-Accept: %s\r\n", accept_key);
  if (sec_proto && *sec_proto)
    g_string_append_printf(resp, "Sec-WebSocket-Protocol: %s\r\n", sec_proto);
  g_string_append(resp, "\r\n");

  g_free(accept_key);
  g_free(sec_key);
  g_free(sec_proto);

  gsize resp_len = resp->len;
  char *resp_raw = g_string_free(resp, FALSE);
  client_queue_bytes(client, g_bytes_new_take(resp_raw, resp_len));

  g_byte_array_remove_range(client->ws_buf, 0, (guint)hdr_bytes);
  client->handshake_done = true;

  if (client->is_secure) {
    uint8_t stream_hdr[REMOTE_CRYPTO_STREAM_HDR_BYTES];
    if (!remote_crypto_stream_init_push(
          &client->tx_stream, stream_hdr, client->session_key
        )) {
      return false;
    }
    // 1. Send 24-byte secretstream header
    send_ws_frame(client, 0x02, stream_hdr, sizeof(stream_hdr));

    // 2. Send encrypted 32-byte server challenge
    remote_crypto_randombytes(
      client->server_challenge, sizeof(client->server_challenge)
    );
    client_send_secure_plain(
      client, client->server_challenge, sizeof(client->server_challenge)
    );
  }

  return true;
}

static bool dispatch_ws_message(
  struct remote_client *client,
  uint8_t               opcode,
  const uint8_t        *payload,
  size_t                len
) {
  if (opcode == 0x02) { // Binary
    if (client->is_secure)
      return process_secure_binary_payload(client, payload, len);

    g_byte_array_append(client->plain_buf, payload, (guint)len);
    return remote_oca_process_buffer(
      client->oca_session,
      client->server->oca_tree,
      NULL,
      client->plain_buf,
      &AUTH_CALLBACKS,
      client
    );
  }

  if (opcode == 0x08) { // Close
    send_ws_frame(client, 0x08, NULL, 0);
    return false;
  }

  if (opcode == 0x09) { // Ping -> Pong
    send_ws_frame(client, 0x0A, payload, len);
    return true;
  }

  return true;
}

static bool parse_ws_frames(struct remote_client *client) {
  GByteArray *buf = client->ws_buf;

  while (!client->closing && buf->len >= 2) {
    uint8_t b0 = buf->data[0];
    uint8_t b1 = buf->data[1];
    bool fin = (b0 & 0x80) != 0;
    uint8_t opcode = b0 & 0x0F;
    bool masked = (b1 & 0x80) != 0;
    size_t payload_len = (size_t)(b1 & 0x7F);
    size_t header_len = 2;

    if (payload_len == 126) {
      if (buf->len < 4)
        return true;
      uint16_t l16;
      memcpy(&l16, buf->data + 2, 2);
      payload_len = GUINT16_FROM_BE(l16);
      header_len = 4;
    } else if (payload_len == 127) {
      if (buf->len < 10)
        return true;
      uint64_t l64;
      memcpy(&l64, buf->data + 2, 8);
      l64 = GUINT64_FROM_BE(l64);
      if (l64 > 2 * 1024 * 1024)
        return false;
      payload_len = (size_t)l64;
      header_len = 10;
    }

    size_t mask_len = masked ? 4 : 0;
    size_t total_len = header_len + mask_len + payload_len;
    if (buf->len < total_len)
      return true;

    uint8_t *payload_copy = NULL;
    if (payload_len > 0) {
      payload_copy = g_malloc(payload_len);
      memcpy(payload_copy, buf->data + header_len + mask_len, payload_len);
      if (masked) {
        const uint8_t *mask = buf->data + header_len;
        for (size_t i = 0; i < payload_len; i++)
          payload_copy[i] ^= mask[i & 3];
      }
    }

    g_byte_array_remove_range(buf, 0, (guint)total_len);

    bool ok = true;
    if (opcode >= 0x08) {
      // Control frame (Close / Ping / Pong)
      ok = dispatch_ws_message(client, opcode, payload_copy, payload_len);
    } else if (!fin) {
      if (opcode != 0x00) {
        g_byte_array_set_size(client->ws_frag_buf, 0);
        client->ws_frag_opcode = opcode;
      }
      if (payload_len > 0)
        g_byte_array_append(client->ws_frag_buf, payload_copy, (guint)payload_len);
    } else if (opcode == 0x00) {
      if (payload_len > 0)
        g_byte_array_append(client->ws_frag_buf, payload_copy, (guint)payload_len);
      ok = dispatch_ws_message(
        client,
        client->ws_frag_opcode,
        client->ws_frag_buf->data,
        client->ws_frag_buf->len
      );
      g_byte_array_set_size(client->ws_frag_buf, 0);
    } else {
      ok = dispatch_ws_message(client, opcode, payload_copy, payload_len);
    }

    g_free(payload_copy);
    if (!ok)
      return false;
  }

  return !client->closing;
}

static void on_read_complete(
  GObject      *source,
  GAsyncResult *result,
  gpointer      user_data
) {
  struct remote_client *client = user_data;
  GError *err = NULL;
  gssize bytes = g_input_stream_read_finish(
    G_INPUT_STREAM(source), result, &err
  );

  if (bytes <= 0 || client->closing) {
    g_clear_error(&err);
    if (!client->closing)
      client_close(client);
    client_unref(client);
    return;
  }

  g_byte_array_append(client->ws_buf, client->rx_tmp, (guint)bytes);

  bool ok = true;
  if (!client->handshake_done)
    ok = handle_ws_handshake(client);

  if (ok && client->handshake_done && client->ws_buf->len > 0)
    ok = parse_ws_frames(client);

  if (!ok && !client->closing) {
    client_close(client);
    client_unref(client);
    return;
  }

  if (!client->closing)
    client_read_start(client);

  client_unref(client);
}

static void client_read_start(struct remote_client *client) {
  if (!client || client->closing || !client->input)
    return;

  client_ref(client);
  g_input_stream_read_async(
    client->input,
    client->rx_tmp,
    sizeof(client->rx_tmp),
    G_PRIORITY_DEFAULT,
    client->cancellable,
    on_read_complete,
    client
  );
}

static void client_close(struct remote_client *client) {
  if (!client || client->closing)
    return;

  client->closing = true;
  struct remote_server *server = client->server;

  if (server) {
    server->clients = g_list_remove(server->clients, client);
    if (server->pending_client == client)
      server->pending_client = NULL;
  }

  if (client->cancellable)
    g_cancellable_cancel(client->cancellable);

  if (client->connection)
    g_io_stream_close(G_IO_STREAM(client->connection), NULL, NULL);

  if (server)
    notify_status_listeners(server);

  client_unref(client);
}

static void accept_client_connection(
  struct remote_server *server,
  GSocketConnection    *connection,
  bool                  is_secure
) {
  struct remote_client *client = g_malloc0(sizeof(struct remote_client));
  client->ref_count = 1;
  client->is_secure = is_secure;
  client->server = server;
  client->connection = g_object_ref(connection);
  client->input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
  client->output = g_io_stream_get_output_stream(G_IO_STREAM(connection));
  client->cancellable = g_cancellable_new();
  client->ws_buf = g_byte_array_new();
  client->ws_frag_buf = g_byte_array_new();
  client->write_queue = g_queue_new();
  client->cipher_buf = g_byte_array_new();
  client->plain_buf = g_byte_array_new();
  client->oca_session = remote_oca_session_create(
    is_secure, on_oca_send_pdu, client
  );

  GSocketAddress *addr = g_socket_connection_get_remote_address(
    connection, NULL
  );
  if (addr && G_IS_INET_SOCKET_ADDRESS(addr)) {
    GInetAddress *inet = g_inet_socket_address_get_address(
      G_INET_SOCKET_ADDRESS(addr)
    );
    char *ip_str = g_inet_address_to_string(inet);
    client->client_name = g_strdup_printf("Focusrite Control 2 (%s)", ip_str);
    g_free(ip_str);
  } else {
    client->client_name = g_strdup("Focusrite Control 2 Mobile");
  }
  if (addr)
    g_object_unref(addr);

  server->clients = g_list_append(server->clients, client);
  client_read_start(client);
}

static gboolean on_incoming_insecure(
  GSocketService    *service,
  GSocketConnection *connection,
  GObject           *source_object,
  gpointer           user_data
) {
  (void)service;
  (void)source_object;
  accept_client_connection(user_data, connection, false);
  return TRUE;
}

static gboolean on_incoming_secure(
  GSocketService    *service,
  GSocketConnection *connection,
  GObject           *source_object,
  gpointer           user_data
) {
  (void)service;
  (void)source_object;
  accept_client_connection(user_data, connection, true);
  return TRUE;
}

static gboolean on_meter_timer_tick(gpointer user_data) {
  struct remote_server *server = user_data;
  if (!server->running || !server->oca_tree)
    return G_SOURCE_CONTINUE;

  GList *sec_sessions = get_authenticated_secure_sessions(server);
  if (sec_sessions) {
    remote_oca_emit_meters(server->oca_tree, sec_sessions);
    g_list_free(sec_sessions);
  }
  return G_SOURCE_CONTINUE;
}

static GSocketService *bind_port_in_range(
  uint16_t  start_port,
  uint16_t  end_port,
  uint16_t *out_port
) {
  for (uint16_t p = start_port; p <= end_port; p++) {
    GSocketService *svc = g_socket_service_new();
    GError *err = NULL;

    if (g_socket_listener_add_inet_port(
          G_SOCKET_LISTENER(svc), p, NULL, &err
        )) {
      *out_port = p;
      return svc;
    }

    g_clear_error(&err);
    g_object_unref(svc);
  }
  return NULL;
}

// --- Public API ---

static void notify_status_listeners(struct remote_server *server) {
  if (!server)
    return;

  GList *copy = g_list_copy(server->listeners);
  for (GList *l = copy; l; l = l->next) {
    struct status_listener *sl = l->data;
    if (sl && sl->cb && g_list_find(server->listeners, sl))
      sl->cb(server, sl->user_data);
  }
  g_list_free(copy);
}

struct remote_server *remote_server_create(struct alsa_card *card) {
  remote_crypto_init();

  struct remote_server *server = g_malloc0(sizeof(struct remote_server));
  server->card = card;
  server->insecure_port = PORT_RANGE_START;
  server->secure_port = PORT_RANGE_START + 1;
  server->auto_approve = true;
  server->approved_keys = g_hash_table_new_full(
    g_str_hash, g_str_equal, g_free, g_free
  );

  load_persisted_state(server);
  return server;
}

void remote_server_free(struct remote_server *server) {
  if (!server)
    return;

  remote_server_stop(server);
  g_list_free_full(server->listeners, g_free);
  server->listeners = NULL;
  g_hash_table_destroy(server->approved_keys);
  g_free(server->pending_client_name);
  memset(server->server_sk, 0, sizeof(server->server_sk));
  g_free(server);
}

bool remote_server_start(
  struct remote_server *server,
  uint16_t              requested_port
) {
  if (!server)
    return false;
  if (server->running)
    return true;

  if (!remote_crypto_init()) {
    g_warning("remote_server: libsodium is required for Focusrite Control 2");
    return false;
  }

  uint16_t start_p = (requested_port >= PORT_RANGE_START &&
                      requested_port < PORT_RANGE_END)
                     ? requested_port : PORT_RANGE_START;

  uint16_t insec_port = 0;
  GSocketService *insec_svc = bind_port_in_range(
    start_p, PORT_RANGE_END - 1, &insec_port
  );
  if (!insec_svc) {
    g_warning("remote_server: Failed to bind insecure port in [%u, %u]",
              PORT_RANGE_START, PORT_RANGE_END);
    return false;
  }

  uint16_t sec_port = 0;
  GSocketService *sec_svc = bind_port_in_range(
    insec_port + 1, PORT_RANGE_END, &sec_port
  );
  if (!sec_svc) {
    g_socket_listener_close(G_SOCKET_LISTENER(insec_svc));
    g_object_unref(insec_svc);
    g_warning("remote_server: Failed to bind secure port in [%u, %u]",
              insec_port + 1, PORT_RANGE_END);
    return false;
  }

  server->oca_tree = remote_oca_tree_create(server->card);
  server->insecure_service = insec_svc;
  server->secure_service = sec_svc;
  server->insecure_port = insec_port;
  server->secure_port = sec_port;

  g_signal_connect(
    server->insecure_service, "incoming",
    G_CALLBACK(on_incoming_insecure), server
  );
  g_signal_connect(
    server->secure_service, "incoming",
    G_CALLBACK(on_incoming_secure), server
  );
  g_socket_service_start(server->insecure_service);
  g_socket_service_start(server->secure_service);

  server->mdns = remote_mdns_start(
    server->server_pk_hex, server->insecure_port, server->secure_port
  );
  server->running = true;

  server->meter_timer_id = g_timeout_add(60, on_meter_timer_tick, server);

  g_message(
    "remote_server: Listening on insecure=%u secure=%u (mDNS: %s)",
    server->insecure_port,
    server->secure_port,
    remote_server_get_mdns_name(server)
  );

  notify_status_listeners(server);
  return true;
}

void remote_server_stop(struct remote_server *server) {
  if (!server || !server->running)
    return;

  if (server->meter_timer_id) {
    g_source_remove(server->meter_timer_id);
    server->meter_timer_id = 0;
  }

  if (server->mdns) {
    remote_mdns_stop(server->mdns);
    server->mdns = NULL;
  }

  while (server->clients)
    client_close(server->clients->data);

  if (server->insecure_service) {
    g_socket_service_stop(server->insecure_service);
    g_socket_listener_close(G_SOCKET_LISTENER(server->insecure_service));
    g_object_unref(server->insecure_service);
    server->insecure_service = NULL;
  }

  if (server->secure_service) {
    g_socket_service_stop(server->secure_service);
    g_socket_listener_close(G_SOCKET_LISTENER(server->secure_service));
    g_object_unref(server->secure_service);
    server->secure_service = NULL;
  }

  if (server->oca_tree) {
    remote_oca_tree_free(server->oca_tree);
    server->oca_tree = NULL;
  }

  server->has_pending_approval = false;
  server->pending_client = NULL;
  server->pending_qr_hex[0] = '\0';
  server->running = false;

  g_message("remote_server: Stopped");
  notify_status_listeners(server);
}

bool remote_server_is_running(struct remote_server *server) {
  return server ? server->running : false;
}

uint16_t remote_server_get_port(struct remote_server *server) {
  return server ? server->insecure_port : PORT_RANGE_START;
}

uint16_t remote_server_get_secure_port(struct remote_server *server) {
  return server ? server->secure_port : (PORT_RANGE_START + 1);
}

const char *remote_server_get_mdns_name(struct remote_server *server) {
  if (!server)
    return "";
  if (server->mdns)
    return remote_mdns_get_instance_name(server->mdns);
  return server->server_pk_hex;
}

const char *remote_server_get_public_key_hex(struct remote_server *server) {
  return server ? server->server_pk_hex : "";
}

bool remote_server_get_auto_approve(struct remote_server *server) {
  return server ? server->auto_approve : false;
}

void remote_server_set_auto_approve(
  struct remote_server *server,
  bool                  auto_approve
) {
  if (!server)
    return;
  server->auto_approve = auto_approve;
  if (auto_approve && server->has_pending_approval)
    remote_server_begin_qr_scanning(server);
}

bool remote_server_has_pending_pairing(struct remote_server *server) {
  return server ? server->has_pending_approval : false;
}

bool remote_server_is_qr_scanning(struct remote_server *server) {
  if (!server || !server->has_pending_approval || !server->oca_tree)
    return false;
  return strcmp(
    remote_oca_get_onboarding_state(server->oca_tree), "scanning"
  ) == 0;
}

const char *remote_server_get_pending_qr_payload(struct remote_server *server) {
  if (!server || !server->has_pending_approval)
    return NULL;
  return server->pending_qr_hex;
}

const char *remote_server_get_pending_client_name(struct remote_server *server) {
  if (!server || !server->has_pending_approval || !server->pending_client_name)
    return NULL;
  return server->pending_client_name;
}

void remote_server_begin_qr_scanning(struct remote_server *server) {
  if (!server || !server->has_pending_approval)
    return;

  set_onboarding_state(server, "scanning");
  notify_status_listeners(server);
}

void remote_server_cancel_pending_pairing(struct remote_server *server) {
  if (!server || !server->has_pending_approval)
    return;

  server->has_pending_approval = false;
  server->pending_client = NULL;
  memset(server->pending_qr_secret, 0, sizeof(server->pending_qr_secret));
  server->pending_qr_hex[0] = '\0';

  set_onboarding_state(server, "ready");
  notify_status_listeners(server);
}

void remote_server_approve_pending_client(struct remote_server *server) {
  if (!server || !server->has_pending_approval)
    return;

  approve_client_key(
    server, server->pending_client_key, server->pending_client_name
  );
  remote_server_begin_qr_scanning(server);
}

int remote_server_get_connected_client_count(struct remote_server *server) {
  if (!server)
    return 0;

  int count = 0;
  for (GList *l = server->clients; l; l = l->next) {
    struct remote_client *c = l->data;
    if (!c->closing && c->is_secure && c->authenticated)
      count++;
  }
  return count;
}

GList *remote_server_get_client_names(struct remote_server *server) {
  if (!server)
    return NULL;

  GList *names = NULL;
  for (GList *l = server->clients; l; l = l->next) {
    struct remote_client *c = l->data;
    if (!c->closing && c->is_secure && c->authenticated) {
      names = g_list_append(
        names,
        g_strdup(c->client_name ? c->client_name : "Focusrite Control 2 Mobile")
      );
    }
  }
  return names;
}

void remote_server_add_status_listener(
  struct remote_server   *server,
  remote_server_status_cb cb,
  void                   *user_data
) {
  if (!server || !cb)
    return;

  for (GList *l = server->listeners; l; l = l->next) {
    struct status_listener *sl = l->data;
    if (sl->cb == cb && sl->user_data == user_data)
      return;
  }

  struct status_listener *sl = g_malloc0(sizeof(struct status_listener));
  sl->cb = cb;
  sl->user_data = user_data;
  server->listeners = g_list_append(server->listeners, sl);
}

void remote_server_remove_status_listener(
  struct remote_server   *server,
  remote_server_status_cb cb,
  void                   *user_data
) {
  if (!server || !cb)
    return;

  GList *l = server->listeners;
  while (l) {
    GList *next = l->next;
    struct status_listener *sl = l->data;
    if (sl->cb == cb && sl->user_data == user_data) {
      server->listeners = g_list_delete_link(server->listeners, l);
      g_free(sl);
    }
    l = next;
  }
}

void remote_server_notify_elem_changed(
  struct remote_server *server,
  struct alsa_elem     *elem
) {
  if (!server || !server->running || !server->oca_tree || !elem)
    return;

  GList *sec_sessions = get_authenticated_secure_sessions(server);
  if (sec_sessions) {
    remote_oca_notify_elem_changed(server->oca_tree, elem, sec_sessions);
    g_list_free(sec_sessions);
  }
}
