// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "alsa.h"

#define OCA_SYNC_BYTE          0x3b
#define OCA_PROTOCOL_VERSION   1

#define OCA_PDU_COMMAND        0
#define OCA_PDU_COMMAND_RRQ    1
#define OCA_PDU_NOTIFICATION   2
#define OCA_PDU_RESPONSE       3
#define OCA_PDU_KEEPALIVE      4

#define OCA_STATUS_OK               0
#define OCA_STATUS_PROTOCOL_VERSION 1
#define OCA_STATUS_DEVICE_ERROR     2
#define OCA_STATUS_LOCKED           3
#define OCA_STATUS_BAD_FORMAT       4
#define OCA_STATUS_BAD_ONO          5
#define OCA_STATUS_PARAMETER_ERROR  6
#define OCA_STATUS_OUT_OF_RANGE     7
#define OCA_STATUS_NOT_IMPLEMENTED  8
#define OCA_STATUS_INVALID_REQUEST  9
#define OCA_STATUS_BAD_METHOD      11

struct remote_oca_tree;
struct remote_oca_session;

typedef void (*remote_oca_send_pdu_fn)(
  const uint8_t *pdu,
  size_t         len,
  void          *user_data
);

struct remote_oca_auth_callbacks {
  void (*on_request_approval)(
    uint32_t       handle,
    const uint8_t *client_key_blob,
    size_t         client_key_len,
    const uint8_t *encrypted_name_blob,
    size_t         encrypted_name_len,
    void          *user_data
  );
  void (*on_send_qr_data)(
    uint32_t       handle,
    const uint8_t *qr_blob,
    size_t         qr_blob_len,
    void          *user_data
  );
};

// Build and manage the Focusrite AES70 device tree for an ALSA card
struct remote_oca_tree *remote_oca_tree_create(struct alsa_card *card);
void remote_oca_tree_free(struct remote_oca_tree *tree);

// Per-client OCA session (tracks subscriptions for notifications)
struct remote_oca_session *remote_oca_session_create(
  bool                   is_secure,
  remote_oca_send_pdu_fn send_fn,
  void                  *send_user_data
);
void remote_oca_session_free(struct remote_oca_session *session);

// Process buffered OCP.1 bytes; consumes complete PDUs from rx_buf.
// Returns false if stream framing is corrupted and connection should close.
bool remote_oca_process_buffer(
  struct remote_oca_session              *session,
  struct remote_oca_tree                 *tree,
  GList                                  *all_secure_sessions,
  GByteArray                             *rx_buf,
  const struct remote_oca_auth_callbacks *auth_cbs,
  void                                   *auth_user_data
);

// Helper to build and send a single-string OCP.1 Response (e.g. "approved")
void remote_oca_send_string_response(
  struct remote_oca_session *session,
  uint32_t                   handle,
  uint8_t                    status,
  const char                *str_val
);

// Helper to build and send a status-only OCP.1 Response (paramCount = 0)
void remote_oca_send_empty_response(
  struct remote_oca_session *session,
  uint32_t                   handle,
  uint8_t                    status
);

// Update OcaFocusriteAuthenticationAgent::OnboardingState and notify auth subscribers
void remote_oca_set_onboarding_state(
  struct remote_oca_tree *tree,
  const char             *state,
  GList                  *insecure_sessions
);

const char *remote_oca_get_onboarding_state(const struct remote_oca_tree *tree);

// Sync ALSA element change to subscribed secure sessions
void remote_oca_notify_elem_changed(
  struct remote_oca_tree *tree,
  struct alsa_elem       *elem,
  GList                  *secure_sessions
);

// Periodic meter tick: emit level/peak/clipping notifications if changed
void remote_oca_emit_meters(
  struct remote_oca_tree *tree,
  GList                  *secure_sessions
);
