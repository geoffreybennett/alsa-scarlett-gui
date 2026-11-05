// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <glib.h>
#include <stdint.h>

struct remote_mdns;

struct remote_mdns *remote_mdns_start(
  const char *pubkey_hex,
  uint16_t    insecure_port,
  uint16_t    secure_port
);

const char *remote_mdns_get_instance_name(const struct remote_mdns *mdns);

void remote_mdns_stop(struct remote_mdns *mdns);
