// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "debug.h"

static GHashTable *debug_categories = NULL;

void debug_init(void) {
  if (debug_categories)
    return;

  debug_categories = g_hash_table_new_full(
    g_str_hash, g_str_equal, g_free, NULL
  );

  const char *env = getenv("ALSA_SCARLETT_GUI_DEBUG");
  if (!env || !*env)
    return;

  char *copy = g_strdup(env);
  char *saveptr = NULL;
  char *token = strtok_r(copy, ",", &saveptr);

  while (token) {
    // Trim whitespace
    while (*token == ' ')
      token++;
    char *end = token + strlen(token) - 1;
    while (end > token && *end == ' ')
      *end-- = '\0';

    if (*token)
      g_hash_table_insert(debug_categories, g_strdup(token), (void *)1);

    token = strtok_r(NULL, ",", &saveptr);
  }

  g_free(copy);
}

int debug_enabled(const char *category) {
  if (!debug_categories || !category)
    return 0;

  return g_hash_table_contains(debug_categories, category);
}
