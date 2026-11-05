// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

#include "alsa.h"

struct remote_server;

typedef void (*remote_server_status_cb)(
  struct remote_server *server,
  void                 *user_data
);

struct remote_server *remote_server_create(struct alsa_card *card);
void remote_server_free(struct remote_server *server);

bool remote_server_start(struct remote_server *server, uint16_t port);
void remote_server_stop(struct remote_server *server);
bool remote_server_is_running(struct remote_server *server);

uint16_t remote_server_get_port(struct remote_server *server);
uint16_t remote_server_get_secure_port(struct remote_server *server);
const char *remote_server_get_mdns_name(struct remote_server *server);
const char *remote_server_get_public_key_hex(struct remote_server *server);

bool remote_server_get_auto_approve(struct remote_server *server);
void remote_server_set_auto_approve(
  struct remote_server *server,
  bool                  auto_approve
);

bool remote_server_has_pending_pairing(struct remote_server *server);
bool remote_server_is_qr_scanning(struct remote_server *server);
const char *remote_server_get_pending_qr_payload(struct remote_server *server);
const char *remote_server_get_pending_client_name(struct remote_server *server);
void remote_server_begin_qr_scanning(struct remote_server *server);
void remote_server_cancel_pending_pairing(struct remote_server *server);
void remote_server_approve_pending_client(struct remote_server *server);

int remote_server_get_connected_client_count(struct remote_server *server);
GList *remote_server_get_client_names(struct remote_server *server);

void remote_server_add_status_listener(
  struct remote_server   *server,
  remote_server_status_cb cb,
  void                   *user_data
);

void remote_server_remove_status_listener(
  struct remote_server   *server,
  remote_server_status_cb cb,
  void                   *user_data
);

void remote_server_notify_elem_changed(
  struct remote_server *server,
  struct alsa_elem     *elem
);
