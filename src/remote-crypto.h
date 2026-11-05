// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REMOTE_CRYPTO_PK_BYTES          32
#define REMOTE_CRYPTO_SK_BYTES          32
#define REMOTE_CRYPTO_KEY_BYTES         32
#define REMOTE_CRYPTO_CHALLENGE_BYTES   32
#define REMOTE_CRYPTO_QR_SECRET_BYTES   24
#define REMOTE_CRYPTO_BOX_NONCE_BYTES   24
#define REMOTE_CRYPTO_BOX_MAC_BYTES     16
#define REMOTE_CRYPTO_QR_RAW_BYTES      64
#define REMOTE_CRYPTO_QR_HEX_LEN        128
#define REMOTE_CRYPTO_STREAM_HDR_BYTES  24
#define REMOTE_CRYPTO_STREAM_ABYTES     17
#define REMOTE_CRYPTO_STREAM_MAX_PLAIN  1283

struct remote_secretstream_state {
  uint8_t opaque[128] __attribute__((aligned(16)));
};

bool remote_crypto_init(void);

void remote_crypto_randombytes(void *buf, size_t size);

bool remote_crypto_kx_keypair(
  uint8_t pk[REMOTE_CRYPTO_PK_BYTES],
  uint8_t sk[REMOTE_CRYPTO_SK_BYTES]
);

bool remote_crypto_derive_server_key(
  uint8_t       session_key[REMOTE_CRYPTO_KEY_BYTES],
  const uint8_t server_pk[REMOTE_CRYPTO_PK_BYTES],
  const uint8_t server_sk[REMOTE_CRYPTO_SK_BYTES],
  const uint8_t client_pk[REMOTE_CRYPTO_PK_BYTES]
);

bool remote_crypto_create_qr_payload(
  const uint8_t server_sk[REMOTE_CRYPTO_SK_BYTES],
  const uint8_t client_pk[REMOTE_CRYPTO_PK_BYTES],
  uint8_t       secret_out[REMOTE_CRYPTO_QR_SECRET_BYTES],
  char          qr_hex_out[REMOTE_CRYPTO_QR_HEX_LEN + 1]
);

bool remote_crypto_verify_qr_response(
  const uint8_t server_sk[REMOTE_CRYPTO_SK_BYTES],
  const uint8_t client_pk[REMOTE_CRYPTO_PK_BYTES],
  const uint8_t expected_secret[REMOTE_CRYPTO_QR_SECRET_BYTES],
  const void   *qr_data,
  size_t        qr_data_len
);

bool remote_crypto_decrypt_box(
  const uint8_t  server_sk[REMOTE_CRYPTO_SK_BYTES],
  const uint8_t  client_pk[REMOTE_CRYPTO_PK_BYTES],
  const uint8_t *box_data,
  size_t         box_len,
  uint8_t       *plain_out,
  size_t        *plain_len_out
);

bool remote_crypto_stream_init_push(
  struct remote_secretstream_state *state,
  uint8_t                           header[REMOTE_CRYPTO_STREAM_HDR_BYTES],
  const uint8_t                     key[REMOTE_CRYPTO_KEY_BYTES]
);

bool remote_crypto_stream_push(
  struct remote_secretstream_state *state,
  uint8_t                          *cipher_out,
  size_t                           *cipher_len_out,
  const uint8_t                    *plain,
  size_t                            plain_len
);

bool remote_crypto_stream_init_pull(
  struct remote_secretstream_state *state,
  const uint8_t                     header[REMOTE_CRYPTO_STREAM_HDR_BYTES],
  const uint8_t                     key[REMOTE_CRYPTO_KEY_BYTES]
);

bool remote_crypto_stream_pull(
  struct remote_secretstream_state *state,
  uint8_t                          *plain_out,
  size_t                           *plain_len_out,
  const uint8_t                    *cipher,
  size_t                            cipher_len
);

void remote_bin2hex(const uint8_t *bin, size_t len, char *hex_out);

bool remote_hex2bin(
  const char *hex,
  size_t      hex_len,
  uint8_t    *bin_out,
  size_t      expected_bin_len
);
