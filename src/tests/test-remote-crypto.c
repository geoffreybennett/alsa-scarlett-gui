// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include "remote-crypto.h"

typedef int (*box_open_fn)(
  uint8_t       *m,
  const uint8_t *c,
  unsigned long long clen,
  const uint8_t *n,
  const uint8_t *pk,
  const uint8_t *sk
);

typedef int (*box_easy_fn)(
  uint8_t       *c,
  const uint8_t *m,
  unsigned long long mlen,
  const uint8_t *n,
  const uint8_t *pk,
  const uint8_t *sk
);

typedef int (*kx_client_keys_fn)(
  uint8_t       rx[32],
  uint8_t       tx[32],
  const uint8_t client_pk[32],
  const uint8_t client_sk[32],
  const uint8_t server_pk[32]
);

int main(void) {
  assert(remote_crypto_init());

  // 1. Hex round-trip
  uint8_t rnd[32];
  char hex[65];
  uint8_t decoded[32];
  remote_crypto_randombytes(rnd, sizeof(rnd));
  remote_bin2hex(rnd, sizeof(rnd), hex);
  assert(strlen(hex) == 64);
  assert(remote_hex2bin(hex, 64, decoded, sizeof(decoded)));
  assert(memcmp(rnd, decoded, sizeof(rnd)) == 0);

  // 2. X25519 keypairs & QR pairing crypto
  uint8_t server_pk[32], server_sk[32];
  uint8_t client_pk[32], client_sk[32];
  assert(remote_crypto_kx_keypair(server_pk, server_sk));
  assert(remote_crypto_kx_keypair(client_pk, client_sk));

  uint8_t secret[REMOTE_CRYPTO_QR_SECRET_BYTES];
  char qr_hex[REMOTE_CRYPTO_QR_HEX_LEN + 1];
  assert(remote_crypto_create_qr_payload(
    server_sk, client_pk, secret, qr_hex
  ));
  assert(strlen(qr_hex) == REMOTE_CRYPTO_QR_HEX_LEN);

  // Simulate Android app scanning QR, decrypting secret, and re-encrypting
  void *lib = dlopen("libsodium.so.26", RTLD_LAZY | RTLD_LOCAL);
  if (!lib)
    lib = dlopen("libsodium.so", RTLD_LAZY | RTLD_LOCAL);
  assert(lib != NULL);

  box_open_fn fn_open = (box_open_fn)dlsym(lib, "crypto_box_open_easy");
  box_easy_fn fn_easy = (box_easy_fn)dlsym(lib, "crypto_box_easy");
  kx_client_keys_fn fn_client_kx = (kx_client_keys_fn)dlsym(
    lib, "crypto_kx_client_session_keys"
  );
  assert(fn_open && fn_easy && fn_client_kx);

  uint8_t qr_raw[64];
  assert(remote_hex2bin(qr_hex, 128, qr_raw, 64));

  uint8_t client_decrypted[24];
  assert(fn_open(
    client_decrypted, qr_raw + 24, 40, qr_raw, server_pk, client_sk
  ) == 0);
  assert(memcmp(client_decrypted, secret, 24) == 0);

  uint8_t resp_raw[64];
  remote_crypto_randombytes(resp_raw, 24);
  assert(fn_easy(
    resp_raw + 24, client_decrypted, 24, resp_raw, server_pk, client_sk
  ) == 0);

  char resp_hex[129];
  remote_bin2hex(resp_raw, 64, resp_hex);
  assert(remote_crypto_verify_qr_response(
    server_sk, client_pk, secret, resp_raw, 64
  ));
  assert(remote_crypto_verify_qr_response(
    server_sk, client_pk, secret, resp_hex, 128
  ));

  // Tampered QR response must fail
  resp_hex[10] = (resp_hex[10] == 'a') ? 'b' : 'a';
  assert(!remote_crypto_verify_qr_response(
    server_sk, client_pk, secret, resp_hex, 128
  ));

  // 2b. Encrypted client name OcaBlob decryption (RequestApproval)
  const char *phone_name = "motorola edge 60 pro";
  size_t name_len = strlen(phone_name);
  uint8_t name_box[128];
  remote_crypto_randombytes(name_box, 24);
  assert(fn_easy(
    name_box + 24, (const uint8_t *)phone_name, name_len,
    name_box, server_pk, client_sk
  ) == 0);

  uint8_t name_plain[128];
  size_t name_plain_len = 0;
  assert(remote_crypto_decrypt_box(
    server_sk, client_pk, name_box, 24 + name_len + 16,
    name_plain, &name_plain_len
  ));
  assert(name_plain_len == name_len);
  assert(memcmp(name_plain, phone_name, name_len) == 0);

  name_box[30] ^= 0xFF;
  assert(!remote_crypto_decrypt_box(
    server_sk, client_pk, name_box, 24 + name_len + 16,
    name_plain, &name_plain_len
  ));

  // 3. Session key derivation (server_tx == client_rx) & secretstream
  uint8_t server_key[32];
  uint8_t client_rx[32], client_tx[32];
  assert(remote_crypto_derive_server_key(
    server_key, server_pk, server_sk, client_pk
  ));
  assert(fn_client_kx(client_rx, client_tx, client_pk, client_sk, server_pk) == 0);
  assert(memcmp(server_key, client_rx, 32) == 0);

  struct remote_secretstream_state push_st, pull_st;
  uint8_t hdr[REMOTE_CRYPTO_STREAM_HDR_BYTES];
  assert(remote_crypto_stream_init_push(&push_st, hdr, server_key));
  assert(remote_crypto_stream_init_pull(&pull_st, hdr, client_rx));

  const char *msg = "Focusrite Control 2 AES70 secretstream test";
  uint8_t cipher[128];
  size_t clen = 0;
  assert(remote_crypto_stream_push(
    &push_st, cipher, &clen, (const uint8_t *)msg, strlen(msg) + 1
  ));

  uint8_t plain[128];
  size_t plen = 0;
  assert(remote_crypto_stream_pull(&pull_st, plain, &plen, cipher, clen));
  assert(plen == strlen(msg) + 1);
  assert(strcmp((const char *)plain, msg) == 0);

  printf("All remote-crypto tests passed!\n");
  return 0;
}
