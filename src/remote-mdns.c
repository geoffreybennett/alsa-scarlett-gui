// SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remote-mdns.h"

#define AVAHI_IF_UNSPEC  (-1)
#define AVAHI_PROTO_INET 0

struct remote_mdns {
  GDBusConnection *bus;
  char            *group_path;
  char             instance_name[32];
};

static GVariant *make_txt_entry(const char *str) {
  return g_variant_new_fixed_array(
    G_VARIANT_TYPE_BYTE,
    str,
    strlen(str),
    sizeof(uint8_t)
  );
}

struct remote_mdns *remote_mdns_start(
  const char *pubkey_hex,
  uint16_t    insecure_port,
  uint16_t    secure_port
) {
  if (!pubkey_hex || strlen(pubkey_hex) != 64) {
    g_warning("remote_mdns: Invalid 64-hex public key");
    return NULL;
  }

  GError *err = NULL;
  GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
  if (!bus) {
    g_warning("remote_mdns: Failed to connect to system bus: %s",
              err ? err->message : "unknown");
    g_clear_error(&err);
    return NULL;
  }

  GVariant *res = g_dbus_connection_call_sync(
    bus,
    "org.freedesktop.Avahi",
    "/",
    "org.freedesktop.Avahi.Server",
    "EntryGroupNew",
    NULL,
    G_VARIANT_TYPE("(o)"),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &err
  );

  if (!res) {
    g_warning("remote_mdns: EntryGroupNew failed: %s",
              err ? err->message : "unknown");
    g_clear_error(&err);
    g_object_unref(bus);
    return NULL;
  }

  const char *group_path = NULL;
  g_variant_get(res, "(&o)", &group_path);

  char inst_name[32];
  snprintf(inst_name, sizeof(inst_name), "fc2-%.12s", pubkey_hex);

  // Build exact 3 TXT records expected by Focusrite Control 2 mobile app
  GVariantBuilder txt_builder;
  g_variant_builder_init(&txt_builder, G_VARIANT_TYPE("aay"));

  char *txt_pubkey = g_strdup_printf("publicKey=%s", pubkey_hex);
  char *txt_sec_port = g_strdup_printf("securePort=%u", (unsigned int)secure_port);

  g_variant_builder_add_value(&txt_builder, make_txt_entry("version=2"));
  g_variant_builder_add_value(&txt_builder, make_txt_entry(txt_pubkey));
  g_variant_builder_add_value(&txt_builder, make_txt_entry(txt_sec_port));

  g_free(txt_pubkey);
  g_free(txt_sec_port);

  GVariant *add_svc = g_dbus_connection_call_sync(
    bus,
    "org.freedesktop.Avahi",
    group_path,
    "org.freedesktop.Avahi.EntryGroup",
    "AddService",
    g_variant_new(
      "(iiussssqaay)",
      (gint32)AVAHI_IF_UNSPEC,
      (gint32)AVAHI_PROTO_INET,
      (guint32)0,
      inst_name,
      "_ocaws._tcp",
      "",
      "",
      (guint16)insecure_port,
      &txt_builder
    ),
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &err
  );

  if (!add_svc) {
    g_warning("remote_mdns: AddService failed: %s",
              err ? err->message : "unknown");
    g_clear_error(&err);
    g_dbus_connection_call_sync(
      bus, "org.freedesktop.Avahi", group_path,
      "org.freedesktop.Avahi.EntryGroup", "Free",
      NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL
    );
    g_variant_unref(res);
    g_object_unref(bus);
    return NULL;
  }
  g_variant_unref(add_svc);

  GVariant *add_sub = g_dbus_connection_call_sync(
    bus,
    "org.freedesktop.Avahi",
    group_path,
    "org.freedesktop.Avahi.EntryGroup",
    "AddServiceSubtype",
    g_variant_new(
      "(iiussss)",
      (gint32)AVAHI_IF_UNSPEC,
      (gint32)AVAHI_PROTO_INET,
      (guint32)0,
      inst_name,
      "_ocaws._tcp",
      "",
      "_oca-authentication-endpoint._sub._ocaws._tcp"
    ),
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &err
  );

  if (!add_sub) {
    g_warning("remote_mdns: AddServiceSubtype failed: %s",
              err ? err->message : "unknown");
    g_clear_error(&err);
    g_dbus_connection_call_sync(
      bus, "org.freedesktop.Avahi", group_path,
      "org.freedesktop.Avahi.EntryGroup", "Free",
      NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL
    );
    g_variant_unref(res);
    g_object_unref(bus);
    return NULL;
  }
  g_variant_unref(add_sub);

  GVariant *commit_res = g_dbus_connection_call_sync(
    bus,
    "org.freedesktop.Avahi",
    group_path,
    "org.freedesktop.Avahi.EntryGroup",
    "Commit",
    NULL,
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &err
  );

  if (!commit_res) {
    g_warning("remote_mdns: Commit failed: %s",
              err ? err->message : "unknown");
    g_clear_error(&err);
    g_dbus_connection_call_sync(
      bus, "org.freedesktop.Avahi", group_path,
      "org.freedesktop.Avahi.EntryGroup", "Free",
      NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL
    );
    g_variant_unref(res);
    g_object_unref(bus);
    return NULL;
  }
  g_variant_unref(commit_res);

  struct remote_mdns *mdns = g_malloc0(sizeof(struct remote_mdns));
  mdns->bus = bus;
  mdns->group_path = g_strdup(group_path);
  g_strlcpy(mdns->instance_name, inst_name, sizeof(mdns->instance_name));
  g_variant_unref(res);

  g_message(
    "remote_mdns: Published '%s' (_ocaws._tcp) insecure=%u secure=%u",
    mdns->instance_name, insecure_port, secure_port
  );
  return mdns;
}

const char *remote_mdns_get_instance_name(const struct remote_mdns *mdns) {
  return mdns ? mdns->instance_name : "";
}

void remote_mdns_stop(struct remote_mdns *mdns) {
  if (!mdns)
    return;

  if (mdns->bus && mdns->group_path) {
    GVariant *free_res = g_dbus_connection_call_sync(
      mdns->bus,
      "org.freedesktop.Avahi",
      mdns->group_path,
      "org.freedesktop.Avahi.EntryGroup",
      "Free",
      NULL,
      NULL,
      G_DBUS_CALL_FLAGS_NONE,
      -1,
      NULL,
      NULL
    );
    if (free_res)
      g_variant_unref(free_res);
  }

  g_free(mdns->group_path);
  if (mdns->bus)
    g_object_unref(mdns->bus);
  g_free(mdns);
}
