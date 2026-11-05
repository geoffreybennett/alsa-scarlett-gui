// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <dlfcn.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "remote-crypto.h"

typedef int (*fn_sodium_init)(void);
typedef void (*fn_randombytes_buf)(void *buf, size_t size);
typedef int (*fn_crypto_kx_keypair)(unsigned char *pk, unsigned char *sk);
typedef int (*fn_crypto_kx_server_session_keys)(
  unsigned char       *rx,
  unsigned char       *tx,
  const unsigned char *server_pk,
  const unsigned char *server_sk,
  const unsigned char *client_pk
);
typedef int (*fn_crypto_box_easy)(
  unsigned char       *c,
  const unsigned char *m,
  unsigned long long   mlen,
  const unsigned char *n,
  const unsigned char *pk,
  const unsigned char *sk
);
typedef int (*fn_crypto_box_open_easy)(
  unsigned char       *m,
  const unsigned char *c,
  unsigned long long   clen,
  const unsigned char *n,
  const unsigned char *pk,
  const unsigned char *sk
);
typedef int (*fn_stream_init_push)(
  void                *state,
  unsigned char       *header,
  const unsigned char *k
);
typedef int (*fn_stream_push)(
  void                *state,
  unsigned char       *c,
  unsigned long long  *clen_p,
  const unsigned char *m,
  unsigned long long   mlen,
  const unsigned char *ad,
  unsigned long long   adlen,
  unsigned char        tag
);
typedef int (*fn_stream_init_pull)(
  void                *state,
  const unsigned char *header,
  const unsigned char *k
);
typedef int (*fn_stream_pull)(
  void                *state,
  unsigned char       *m,
  unsigned long long  *mlen_p,
  unsigned char       *tag_p,
  const unsigned char *c,
  unsigned long long   clen,
  const unsigned char *ad,
  unsigned long long   adlen
);

static struct {
  bool                              loaded;
  bool                              attempted;
  void                             *handle;
  fn_sodium_init                    p_sodium_init;
  fn_randombytes_buf                p_randombytes_buf;
  fn_crypto_kx_keypair              p_kx_keypair;
  fn_crypto_kx_server_session_keys  p_kx_server_keys;
  fn_crypto_box_easy                p_box_easy;
  fn_crypto_box_open_easy           p_box_open_easy;
  fn_stream_init_push               p_stream_init_push;
  fn_stream_push                    p_stream_push;
  fn_stream_init_pull               p_stream_init_pull;
  fn_stream_pull                    p_stream_pull;
} sodium_api;

static void *load_sym(void *h, const char *name) {
  void *s = dlsym(h, name);
  if (!s)
    g_warning("remote_crypto: Missing symbol %s in libsodium", name);
  return s;
}

bool remote_crypto_init(void) {
  if (sodium_api.attempted)
    return sodium_api.loaded;

  sodium_api.attempted = true;

  const char *candidates[] = {
    "libsodium.so.26",
    "libsodium.so.23",
    "libsodium.so",
    NULL
  };

  void *h = NULL;
  for (int i = 0; candidates[i] != NULL; i++) {
    h = dlopen(candidates[i], RTLD_LAZY | RTLD_LOCAL);
    if (h)
      break;
  }

  if (!h) {
    g_warning("remote_crypto: Could not load libsodium shared library");
    return false;
  }

  sodium_api.handle = h;
  sodium_api.p_sodium_init =
    (fn_sodium_init)load_sym(h, "sodium_init");
  sodium_api.p_randombytes_buf =
    (fn_randombytes_buf)load_sym(h, "randombytes_buf");
  sodium_api.p_kx_keypair =
    (fn_crypto_kx_keypair)load_sym(h, "crypto_kx_keypair");
  sodium_api.p_kx_server_keys =
    (fn_crypto_kx_server_session_keys)load_sym(
      h, "crypto_kx_server_session_keys"
    );
  sodium_api.p_box_easy =
    (fn_crypto_box_easy)load_sym(h, "crypto_box_easy");
  sodium_api.p_box_open_easy =
    (fn_crypto_box_open_easy)load_sym(h, "crypto_box_open_easy");
  sodium_api.p_stream_init_push =
    (fn_stream_init_push)load_sym(
      h, "crypto_secretstream_xchacha20poly1305_init_push"
    );
  sodium_api.p_stream_push =
    (fn_stream_push)load_sym(
      h, "crypto_secretstream_xchacha20poly1305_push"
    );
  sodium_api.p_stream_init_pull =
    (fn_stream_init_pull)load_sym(
      h, "crypto_secretstream_xchacha20poly1305_init_pull"
    );
  sodium_api.p_stream_pull =
    (fn_stream_pull)load_sym(
      h, "crypto_secretstream_xchacha20poly1305_pull"
    );

  if (!sodium_api.p_sodium_init ||
      !sodium_api.p_randombytes_buf ||
      !sodium_api.p_kx_keypair ||
      !sodium_api.p_kx_server_keys ||
      !sodium_api.p_box_easy ||
      !sodium_api.p_box_open_easy ||
      !sodium_api.p_stream_init_push ||
      !sodium_api.p_stream_push ||
      !sodium_api.p_stream_init_pull ||
      !sodium_api.p_stream_pull) {
    dlclose(h);
    sodium_api.handle = NULL;
    return false;
  }

  if (sodium_api.p_sodium_init() < 0) {
    g_warning("remote_crypto: sodium_init() failed");
    return false;
  }

  sodium_api.loaded = true;
  return true;
}

void remote_crypto_randombytes(void *buf, size_t size) {
  if (!buf || size == 0)
    return;
  if (remote_crypto_init()) {
    sodium_api.p_randombytes_buf(buf, size);
    return;
  }
  uint8_t *p = buf;
  for (size_t i = 0; i < size; i++)
    p[i] = (uint8_t)(g_random_int() & 0xFF);
}

bool remote_crypto_kx_keypair(
  uint8_t pk[REMOTE_CRYPTO_PK_BYTES],
  uint8_t sk[REMOTE_CRYPTO_SK_BYTES]
) {
  if (!remote_crypto_init())
    return false;
  return sodium_api.p_kx_keypair(pk, sk) == 0;
}

bool remote_crypto_derive_server_key(
  uint8_t       session_key[REMOTE_CRYPTO_KEY_BYTES],
  const uint8_t server_pk[REMOTE_CRYPTO_PK_BYTES],
  const uint8_t server_sk[REMOTE_CRYPTO_SK_BYTES],
  const uint8_t client_pk[REMOTE_CRYPTO_PK_BYTES]
) {
  if (!remote_crypto_init())
    return false;

  uint8_t rx[REMOTE_CRYPTO_KEY_BYTES];
  uint8_t tx[REMOTE_CRYPTO_KEY_BYTES];

  int rc = sodium_api.p_kx_server_keys(rx, tx, server_pk, server_sk, client_pk);
  memset(rx, 0, sizeof(rx));
  if (rc != 0) {
    memset(tx, 0, sizeof(tx));
    return false;
  }

  // Focusrite Control 2 aes70::secretstream uses server_tx (== client_rx)
  // as the symmetric XChaCha20-Poly1305 key for both push and pull directions.
  memcpy(session_key, tx, REMOTE_CRYPTO_KEY_BYTES);
  memset(tx, 0, sizeof(tx));
  return true;
}

bool remote_crypto_create_qr_payload(
  const uint8_t server_sk[REMOTE_CRYPTO_SK_BYTES],
  const uint8_t client_pk[REMOTE_CRYPTO_PK_BYTES],
  uint8_t       secret_out[REMOTE_CRYPTO_QR_SECRET_BYTES],
  char          qr_hex_out[REMOTE_CRYPTO_QR_HEX_LEN + 1]
) {
  if (!remote_crypto_init())
    return false;

  uint8_t raw[REMOTE_CRYPTO_QR_RAW_BYTES];
  uint8_t *nonce = raw;
  uint8_t *cipher = raw + REMOTE_CRYPTO_BOX_NONCE_BYTES;

  sodium_api.p_randombytes_buf(secret_out, REMOTE_CRYPTO_QR_SECRET_BYTES);
  sodium_api.p_randombytes_buf(nonce, REMOTE_CRYPTO_BOX_NONCE_BYTES);

  if (sodium_api.p_box_easy(
        cipher,
        secret_out,
        REMOTE_CRYPTO_QR_SECRET_BYTES,
        nonce,
        client_pk,
        server_sk
      ) != 0) {
    return false;
  }

  remote_bin2hex(raw, sizeof(raw), qr_hex_out);
  return true;
}

bool remote_crypto_verify_qr_response(
  const uint8_t server_sk[REMOTE_CRYPTO_SK_BYTES],
  const uint8_t client_pk[REMOTE_CRYPTO_PK_BYTES],
  const uint8_t expected_secret[REMOTE_CRYPTO_QR_SECRET_BYTES],
  const void   *qr_data,
  size_t        qr_data_len
) {
  if (!remote_crypto_init() || !qr_data)
    return false;

  uint8_t raw[REMOTE_CRYPTO_QR_RAW_BYTES];
  if (qr_data_len == REMOTE_CRYPTO_QR_HEX_LEN) {
    if (!remote_hex2bin(
          (const char *)qr_data, qr_data_len, raw, sizeof(raw)
        ))
      return false;
  } else if (qr_data_len == REMOTE_CRYPTO_QR_RAW_BYTES) {
    memcpy(raw, qr_data, sizeof(raw));
  } else {
    return false;
  }

  const uint8_t *nonce = raw;
  const uint8_t *cipher = raw + REMOTE_CRYPTO_BOX_NONCE_BYTES;
  size_t cipher_len =
    REMOTE_CRYPTO_QR_RAW_BYTES - REMOTE_CRYPTO_BOX_NONCE_BYTES;

  uint8_t decrypted[REMOTE_CRYPTO_QR_SECRET_BYTES];
  if (sodium_api.p_box_open_easy(
        decrypted,
        cipher,
        cipher_len,
        nonce,
        client_pk,
        server_sk
      ) != 0) {
    return false;
  }

  uint8_t diff = 0;
  for (size_t i = 0; i < REMOTE_CRYPTO_QR_SECRET_BYTES; i++)
    diff |= decrypted[i] ^ expected_secret[i];

  memset(decrypted, 0, sizeof(decrypted));
  return diff == 0;
}

bool remote_crypto_decrypt_box(
  const uint8_t  server_sk[REMOTE_CRYPTO_SK_BYTES],
  const uint8_t  client_pk[REMOTE_CRYPTO_PK_BYTES],
  const uint8_t *box_data,
  size_t         box_len,
  uint8_t       *plain_out,
  size_t        *plain_len_out
) {
  const size_t overhead =
    REMOTE_CRYPTO_BOX_NONCE_BYTES + REMOTE_CRYPTO_BOX_MAC_BYTES;
  if (!remote_crypto_init() || !box_data || !plain_out || box_len < overhead)
    return false;

  const uint8_t *nonce = box_data;
  const uint8_t *cipher = box_data + REMOTE_CRYPTO_BOX_NONCE_BYTES;
  size_t cipher_len = box_len - REMOTE_CRYPTO_BOX_NONCE_BYTES;

  if (sodium_api.p_box_open_easy(
        plain_out, cipher, cipher_len, nonce, client_pk, server_sk
      ) != 0) {
    return false;
  }

  if (plain_len_out)
    *plain_len_out = box_len - overhead;
  return true;
}

bool remote_crypto_stream_init_push(
  struct remote_secretstream_state *state,
  uint8_t                           header[REMOTE_CRYPTO_STREAM_HDR_BYTES],
  const uint8_t                     key[REMOTE_CRYPTO_KEY_BYTES]
) {
  if (!remote_crypto_init() || !state)
    return false;
  return sodium_api.p_stream_init_push(state->opaque, header, key) == 0;
}

bool remote_crypto_stream_push(
  struct remote_secretstream_state *state,
  uint8_t                          *cipher_out,
  size_t                           *cipher_len_out,
  const uint8_t                    *plain,
  size_t                            plain_len
) {
  if (!remote_crypto_init() || !state)
    return false;

  unsigned long long clen = 0;
  int rc = sodium_api.p_stream_push(
    state->opaque, cipher_out, &clen, plain, plain_len, NULL, 0, 0
  );
  if (rc != 0)
    return false;
  if (cipher_len_out)
    *cipher_len_out = (size_t)clen;
  return true;
}

bool remote_crypto_stream_init_pull(
  struct remote_secretstream_state *state,
  const uint8_t                     header[REMOTE_CRYPTO_STREAM_HDR_BYTES],
  const uint8_t                     key[REMOTE_CRYPTO_KEY_BYTES]
) {
  if (!remote_crypto_init() || !state)
    return false;
  return sodium_api.p_stream_init_pull(state->opaque, header, key) == 0;
}

bool remote_crypto_stream_pull(
  struct remote_secretstream_state *state,
  uint8_t                          *plain_out,
  size_t                           *plain_len_out,
  const uint8_t                    *cipher,
  size_t                            cipher_len
) {
  if (!remote_crypto_init() || !state)
    return false;

  unsigned long long mlen = 0;
  unsigned char tag = 0;
  int rc = sodium_api.p_stream_pull(
    state->opaque, plain_out, &mlen, &tag, cipher, cipher_len, NULL, 0
  );
  if (rc != 0 || tag != 0)
    return false;
  if (plain_len_out)
    *plain_len_out = (size_t)mlen;
  return true;
}

void remote_bin2hex(const uint8_t *bin, size_t len, char *hex_out) {
  static const char lut[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    hex_out[i * 2]     = lut[(bin[i] >> 4) & 0x0F];
    hex_out[i * 2 + 1] = lut[bin[i] & 0x0F];
  }
  hex_out[len * 2] = '\0';
}

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

bool remote_hex2bin(
  const char *hex,
  size_t      hex_len,
  uint8_t    *bin_out,
  size_t      expected_bin_len
) {
  if (!hex || !bin_out || hex_len != expected_bin_len * 2)
    return false;

  for (size_t i = 0; i < expected_bin_len; i++) {
    int hi = hex_nibble(hex[i * 2]);
    int lo = hex_nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0)
      return false;
    bin_out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}
