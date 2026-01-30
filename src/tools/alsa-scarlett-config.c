// SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

// alsa-scarlett-config: CLI tool to load/save Focusrite audio interface config

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <alsa/asoundlib.h>
#include <glib.h>

#define CONFIG_SECTION_CONTROLS "controls"

struct cli_elem {
  int    numid;
  char  *name;
  int    type;
  int    count;
};

static void print_usage(void) {
  fprintf(stderr, "alsa-scarlett-config: Load configuration to Focusrite device\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Usage: alsa-scarlett-config [-d <device>] <config.conf>\n");
  fprintf(stderr, "       alsa-scarlett-config -l\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -d, --device <device>  ALSA device (auto-detected if only one)\n");
  fprintf(stderr, "  -l, --list             List available Focusrite devices\n");
  fprintf(stderr, "  -v, --verbose          Show each control as it's set\n");
  fprintf(stderr, "  -n, --dry-run          Parse config but don't apply changes\n");
  fprintf(stderr, "  -h, --help             Show this help\n");
}

static int is_focusrite_card(const char *name) {
  return strncmp(name, "Scarlett", 8) == 0 ||
         strncmp(name, "Clarett", 7) == 0 ||
         strncmp(name, "Vocaster", 8) == 0;
}

// Find Focusrite devices. Returns count found.
// If out_device is not NULL and exactly one device found, writes "hw:N" to it.
static int find_focusrite_devices(char *out_device, size_t out_size) {
  snd_ctl_card_info_t *info;
  snd_ctl_t *ctl;
  int card_num = -1;
  int count = 0;
  int found_card = -1;

  snd_ctl_card_info_alloca(&info);

  while (snd_card_next(&card_num) >= 0 && card_num >= 0) {
    char device[32];
    snprintf(device, sizeof(device), "hw:%d", card_num);

    if (snd_ctl_open(&ctl, device, 0) < 0)
      continue;

    if (snd_ctl_card_info(ctl, info) >= 0) {
      const char *name = snd_ctl_card_info_get_name(info);
      if (is_focusrite_card(name)) {
        count++;
        found_card = card_num;
      }
    }
    snd_ctl_close(ctl);
  }

  if (out_device && count == 1)
    snprintf(out_device, out_size, "hw:%d", found_card);

  return count;
}

static void list_devices(void) {
  snd_ctl_card_info_t *info;
  snd_ctl_t *ctl;
  int card_num = -1;
  int found = 0;

  snd_ctl_card_info_alloca(&info);

  printf("Available Focusrite devices:\n");

  while (snd_card_next(&card_num) >= 0 && card_num >= 0) {
    char device[32];
    snprintf(device, sizeof(device), "hw:%d", card_num);

    if (snd_ctl_open(&ctl, device, 0) < 0)
      continue;

    if (snd_ctl_card_info(ctl, info) >= 0) {
      const char *name = snd_ctl_card_info_get_name(info);
      if (is_focusrite_card(name)) {
        printf("  hw:%d  %s\n", card_num, name);
        found = 1;
      }
    }
    snd_ctl_close(ctl);
  }

  if (!found)
    printf("  (none found)\n");
}

static struct cli_elem *find_elem_by_name(GArray *elems, const char *name) {
  for (guint i = 0; i < elems->len; i++) {
    struct cli_elem *elem = &g_array_index(elems, struct cli_elem, i);
    if (strcmp(elem->name, name) == 0)
      return elem;
  }
  return NULL;
}

static GArray *scan_elements(snd_ctl_t *handle) {
  snd_ctl_elem_list_t *list;
  snd_ctl_elem_info_t *elem_info;
  int count;

  snd_ctl_elem_list_alloca(&list);
  snd_ctl_elem_info_alloca(&elem_info);

  snd_ctl_elem_list(handle, list);
  count = snd_ctl_elem_list_get_count(list);
  snd_ctl_elem_list_alloc_space(list, count);
  snd_ctl_elem_list(handle, list);

  GArray *elems = g_array_new(FALSE, TRUE, sizeof(struct cli_elem));

  for (int i = 0; i < count; i++) {
    int numid = snd_ctl_elem_list_get_numid(list, i);

    snd_ctl_elem_info_set_numid(elem_info, numid);
    if (snd_ctl_elem_info(handle, elem_info) < 0)
      continue;

    int type = snd_ctl_elem_info_get_type(elem_info);

    if (type != SND_CTL_ELEM_TYPE_BOOLEAN &&
        type != SND_CTL_ELEM_TYPE_ENUMERATED &&
        type != SND_CTL_ELEM_TYPE_INTEGER &&
        type != SND_CTL_ELEM_TYPE_BYTES)
      continue;

    if (!snd_ctl_elem_info_is_writable(elem_info))
      continue;

    struct cli_elem elem = {
      .numid = numid,
      .name = strdup(snd_ctl_elem_info_get_name(elem_info)),
      .type = type,
      .count = snd_ctl_elem_info_get_count(elem_info)
    };

    g_array_append_val(elems, elem);
  }

  snd_ctl_elem_list_free_space(list);
  return elems;
}

static int get_enum_item_by_name(
  snd_ctl_t *handle, int numid, const char *item_name
) {
  snd_ctl_elem_info_t *info;
  snd_ctl_elem_info_alloca(&info);
  snd_ctl_elem_info_set_numid(info, numid);

  if (snd_ctl_elem_info(handle, info) < 0)
    return -1;

  int items = snd_ctl_elem_info_get_items(info);
  for (int i = 0; i < items; i++) {
    snd_ctl_elem_info_set_item(info, i);
    if (snd_ctl_elem_info(handle, info) < 0)
      continue;
    const char *name = snd_ctl_elem_info_get_item_name(info);
    if (strcmp(name, item_name) == 0)
      return i;
  }

  // try parsing as integer
  char *end;
  long v = strtol(item_name, &end, 10);
  if (end != item_name && *end == '\0')
    return (int)v;

  return -1;
}

static int set_elem_value(
  snd_ctl_t       *handle,
  struct cli_elem *elem,
  const char      *str,
  int              verbose,
  int              dry_run
) {
  snd_ctl_elem_value_t *value;
  snd_ctl_elem_value_alloca(&value);
  snd_ctl_elem_value_set_numid(value, elem->numid);

  if (elem->type == SND_CTL_ELEM_TYPE_BOOLEAN) {
    int v = (strcmp(str, "true") == 0 || strcmp(str, "1") == 0) ? 1 : 0;
    snd_ctl_elem_value_set_boolean(value, 0, v);

    if (verbose)
      printf("  %s = %s\n", elem->name, v ? "true" : "false");

  } else if (elem->type == SND_CTL_ELEM_TYPE_ENUMERATED) {
    int v = get_enum_item_by_name(handle, elem->numid, str);
    if (v < 0) {
      fprintf(stderr, "Warning: Unknown enum value '%s' for '%s'\n",
              str, elem->name);
      return -1;
    }
    snd_ctl_elem_value_set_enumerated(value, 0, v);

    if (verbose)
      printf("  %s = %s\n", elem->name, str);

  } else if (elem->type == SND_CTL_ELEM_TYPE_INTEGER) {
    // handle multi-valued integers (comma-separated)
    const char *p = str;
    int idx = 0;

    while (*p && idx < elem->count) {
      char *end;
      long v = strtol(p, &end, 10);
      if (end == p)
        break;
      snd_ctl_elem_value_set_integer(value, idx++, v);
      p = end;
      if (*p == ',')
        p++;
    }

    if (verbose)
      printf("  %s = %s\n", elem->name, str);

  } else if (elem->type == SND_CTL_ELEM_TYPE_BYTES) {
    size_t len = strlen(str);
    if (len > (size_t)elem->count)
      len = elem->count;

    // clear the buffer first
    char buf[256] = {0};
    memcpy(buf, str, len);
    snd_ctl_elem_set_bytes(value, buf, elem->count);

    if (verbose)
      printf("  %s = \"%s\"\n", elem->name, str);

  } else {
    return -1;
  }

  if (!dry_run) {
    int err = snd_ctl_elem_write(handle, value);
    if (err < 0) {
      fprintf(stderr, "Warning: Failed to set '%s': %s\n",
              elem->name, snd_strerror(err));
      return -1;
    }
  }

  return 0;
}

static int load_config(
  snd_ctl_t  *handle,
  GArray     *elems,
  const char *path,
  int         verbose,
  int         dry_run
) {
  GKeyFile *key_file = g_key_file_new();
  GError *error = NULL;

  if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error)) {
    fprintf(stderr, "Error loading config: %s\n",
            error ? error->message : "unknown error");
    g_key_file_free(key_file);
    if (error)
      g_error_free(error);
    return -1;
  }

  gsize num_keys;
  gchar **keys = g_key_file_get_keys(
    key_file, CONFIG_SECTION_CONTROLS, &num_keys, NULL
  );

  if (!keys) {
    fprintf(stderr, "No [controls] section in config file\n");
    g_key_file_free(key_file);
    return -1;
  }

  int set_count = 0;
  int skip_count = 0;

  // two passes: some elements may become writable after others are set
  for (int pass = 0; pass < 2; pass++) {
    for (gsize i = 0; i < num_keys; i++) {
      gchar *value = g_key_file_get_string(
        key_file, CONFIG_SECTION_CONTROLS, keys[i], NULL
      );

      if (!value)
        continue;

      struct cli_elem *elem = find_elem_by_name(elems, keys[i]);
      if (!elem) {
        if (pass == 1 && verbose)
          fprintf(stderr, "Warning: Unknown control '%s'\n", keys[i]);
        skip_count++;
        g_free(value);
        continue;
      }

      if (set_elem_value(handle, elem, value, verbose && pass == 0, dry_run) == 0)
        set_count++;

      g_free(value);
    }
  }

  g_strfreev(keys);
  g_key_file_free(key_file);

  printf("Applied %d controls", set_count);
  if (skip_count > 0)
    printf(" (%d skipped)", skip_count / 2);  // divide by 2 for two passes
  if (dry_run)
    printf(" [dry-run]");
  printf("\n");

  return 0;
}

static void free_elements(GArray *elems) {
  for (guint i = 0; i < elems->len; i++) {
    struct cli_elem *elem = &g_array_index(elems, struct cli_elem, i);
    free(elem->name);
  }
  g_array_free(elems, TRUE);
}

int main(int argc, char **argv) {
  static struct option long_options[] = {
    {"device",  required_argument, 0, 'd'},
    {"list",    no_argument,       0, 'l'},
    {"verbose", no_argument,       0, 'v'},
    {"dry-run", no_argument,       0, 'n'},
    {"help",    no_argument,       0, 'h'},
    {0, 0, 0, 0}
  };

  char *device = NULL;
  int do_list = 0;
  int verbose = 0;
  int dry_run = 0;
  int opt;

  while ((opt = getopt_long(argc, argv, "d:lvnh", long_options, NULL)) != -1) {
    switch (opt) {
      case 'd':
        device = optarg;
        break;
      case 'l':
        do_list = 1;
        break;
      case 'v':
        verbose = 1;
        break;
      case 'n':
        dry_run = 1;
        break;
      case 'h':
        print_usage();
        return 0;
      default:
        print_usage();
        return 1;
    }
  }

  if (do_list) {
    list_devices();
    return 0;
  }

  if (optind >= argc) {
    print_usage();
    return 1;
  }

  const char *config_path = argv[optind];

  // auto-detect device if not specified
  char auto_device[32];
  if (!device) {
    int count = find_focusrite_devices(auto_device, sizeof(auto_device));
    if (count == 0) {
      fprintf(stderr, "Error: No Focusrite devices found\n");
      return 1;
    } else if (count > 1) {
      fprintf(stderr, "Error: Multiple Focusrite devices found, use -d to specify\n");
      list_devices();
      return 1;
    }
    device = auto_device;
  }

  // check config file exists
  if (!g_file_test(config_path, G_FILE_TEST_EXISTS)) {
    fprintf(stderr, "Error: Config file not found: %s\n", config_path);
    return 1;
  }

  // check it's a .conf file
  if (!g_str_has_suffix(config_path, ".conf")) {
    fprintf(stderr, "Error: Config file must have .conf extension\n");
    return 1;
  }

  // open ALSA control
  snd_ctl_t *handle;
  int err = snd_ctl_open(&handle, device, 0);
  if (err < 0) {
    fprintf(stderr, "Error opening device %s: %s\n", device, snd_strerror(err));
    return 1;
  }

  // verify it's a Focusrite device
  snd_ctl_card_info_t *info;
  snd_ctl_card_info_alloca(&info);
  if (snd_ctl_card_info(handle, info) >= 0) {
    const char *name = snd_ctl_card_info_get_name(info);
    if (!is_focusrite_card(name)) {
      fprintf(stderr, "Warning: %s is not a recognised Focusrite device\n",
              name);
    } else if (verbose) {
      printf("Device: %s\n", name);
    }
  }

  // scan elements
  GArray *elems = scan_elements(handle);
  if (elems->len == 0) {
    fprintf(stderr, "Error: No writable controls found on device\n");
    free_elements(elems);
    snd_ctl_close(handle);
    return 1;
  }

  if (verbose)
    printf("Found %d writable controls\n", elems->len);

  // load config
  int result = load_config(handle, elems, config_path, verbose, dry_run);

  free_elements(elems);
  snd_ctl_close(handle);

  return result < 0 ? 1 : 0;
}
