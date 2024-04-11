// SPDX-FileCopyrightText: 2023-2024 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <sys/stat.h>

#include "scarlett2-firmware.h"

// List of found firmware files
struct found_firmware {
  char                             *fn;
  struct scarlett2_firmware_header *firmware;
};

GHashTable *best_firmware = NULL;

static int verify_sha256(
  const unsigned char *data,
  size_t length,
  const unsigned char *expected_hash
) {
  unsigned char computed_hash[SHA256_DIGEST_LENGTH];
  SHA256(data, length, computed_hash);
  return memcmp(computed_hash, expected_hash, SHA256_DIGEST_LENGTH) == 0;
}

static struct scarlett2_firmware_file *read_header(FILE *file) {
  struct scarlett2_firmware_file *firmware = calloc(
    1, sizeof(struct scarlett2_firmware_file)
  );
  if (!firmware) {
    perror("Failed to allocate memory for firmware structure");
    goto error;
  }

  size_t read_count = fread(
    &firmware->header, sizeof(struct scarlett2_firmware_header), 1, file
  );

  if (read_count != 1) {
    if (feof(file))
      fprintf(stderr, "Unexpected end of file\n");
    else
      perror("Failed to read header");
    goto error;
  }

  if (strncmp(firmware->header.magic, MAGIC_STRING, 8) != 0) {
    fprintf(stderr, "Invalid magic number\n");
    goto error;
  }

  firmware->header.usb_vid = ntohs(firmware->header.usb_vid);
  firmware->header.usb_pid = ntohs(firmware->header.usb_pid);
  firmware->header.firmware_version = ntohl(firmware->header.firmware_version);
  firmware->header.firmware_length = ntohl(firmware->header.firmware_length);

  return firmware;

error:
  free(firmware);
  return NULL;
}

struct scarlett2_firmware_header *scarlett2_read_firmware_header(
  const char *fn
) {
  FILE *file = fopen(fn, "rb");
  if (!file) {
    perror("fopen");
    fprintf(stderr, "Unable to open %s\n", fn);
    return NULL;
  }

  struct scarlett2_firmware_file *firmware = read_header(file);
  if (!firmware) {
    fprintf(stderr, "Error reading firmware header from %s\n", fn);
    return NULL;
  }

  fclose(file);

  return realloc(firmware, sizeof(struct scarlett2_firmware_header));
}

struct scarlett2_firmware_file *scarlett2_read_firmware_file(const char *fn) {
  FILE *file = fopen(fn, "rb");
  if (!file) {
    perror("fopen");
    fprintf(stderr, "Unable to open %s\n", fn);
    return NULL;
  }

  struct scarlett2_firmware_file *firmware = read_header(file);
  if (!firmware) {
    fprintf(stderr, "Error reading firmware header from %s\n", fn);
    return NULL;
  }

  firmware->firmware_data = malloc(firmware->header.firmware_length);
  if (!firmware->firmware_data) {
    perror("Failed to allocate memory for firmware data");
    goto error;
  }

  size_t read_count = fread(
    firmware->firmware_data, 1, firmware->header.firmware_length, file
  );

  if (read_count != firmware->header.firmware_length) {
    if (feof(file))
      fprintf(stderr, "Unexpected end of file\n");
    else
      perror("Failed to read firmware data");
    fprintf(stderr, "Error reading firmware data from %s\n", fn);
    goto error;
  }

  if (!verify_sha256(
    firmware->firmware_data,
    firmware->header.firmware_length,
    firmware->header.sha256
  )) {
    fprintf(stderr, "Corrupt firmware (failed checksum) in %s\n", fn);
    goto error;
  }

  fclose(file);
  return firmware;

error:
  scarlett2_free_firmware_file(firmware);
  fclose(file);
  return NULL;
}

void scarlett2_free_firmware_header(struct scarlett2_firmware_header *firmware) {
  if (firmware)
    free(firmware);
}

void scarlett2_free_firmware_file(struct scarlett2_firmware_file *firmware) {
  if (firmware) {
    free(firmware->firmware_data);
    free(firmware);
  }
}

static void free_found_firmware(gpointer data) {
  struct found_firmware *found = data;

  free(found->fn);
  scarlett2_free_firmware_header(found->firmware);
  free(found);
}

static void init_best_firmware(void) {
  if (best_firmware)
    return;

  best_firmware = g_hash_table_new_full(
    g_direct_hash, g_direct_equal, NULL, free_found_firmware
  );
}

// Add a firmware file to the list of found firmware
// files, if it's better than the one already found
// for the same device.
static void add_found_firmware(
  char                             *fn,
  struct scarlett2_firmware_header *firmware
) {
  gpointer key = GINT_TO_POINTER(firmware->usb_pid);
  struct found_firmware *found = g_hash_table_lookup(best_firmware, key);

  // already have a firmware file for this device?
  if (found) {

    // lower version number, ignore
    if (firmware->firmware_version <= found->firmware->firmware_version) {
      free(fn);
      scarlett2_free_firmware_header(firmware);
      return;
    }

    // higher version number, replace
    g_hash_table_remove(best_firmware, key);
  }

  found = malloc(sizeof(struct found_firmware));
  if (!found) {
    perror("Failed to allocate memory for firmware structure");
    return;
  }

  found->fn = fn;
  found->firmware = firmware;

  g_hash_table_insert(best_firmware, key, found);
}

// look for firmware files in the given directory
static void enum_firmware_dir(const char *dir_name) {
  DIR *dir = opendir(dir_name);

  if (!dir) {
    if (errno == ENOENT) {
      fprintf(stderr, "Firmware directory %s does not exist\n", dir_name);
      return;
    }
    fprintf(
      stderr, "Error opening directory %s: %s\n", dir_name, strerror(errno)
    );
    return;
  }

  struct dirent *entry;

  while ((entry = readdir(dir))) {
    char *full_fn;

    // check if the file is a .bin file
    if (strlen(entry->d_name) < 4 ||
        strcmp(entry->d_name + strlen(entry->d_name) - 4, ".bin") != 0)
      continue;

    // check if the file is a regular file
    if (entry->d_type == DT_UNKNOWN) {
      struct stat st;
      full_fn = g_build_filename(dir_name, entry->d_name, NULL);
      if (stat(full_fn, &st) < 0) {
        perror("stat");
        g_free(full_fn);
        continue;
      }
      if (!S_ISREG(st.st_mode)) {
        g_free(full_fn);
        continue;
      }
    } else if (entry->d_type != DT_REG) {
      continue;
    } else {
      full_fn = g_build_filename(dir_name, entry->d_name, NULL);
    }

    struct scarlett2_firmware_header *firmware =
      scarlett2_read_firmware_header(full_fn);

    if (!firmware) {
      fprintf(stderr, "Error reading firmware file %s\n", full_fn);
      g_free(full_fn);
      continue;
    }

    add_found_firmware(full_fn, firmware);
  }

  closedir(dir);
}

void scarlett2_enum_firmware(void) {
  init_best_firmware();

  const char *fw_dir = getenv("SCARLETT2_FIRMWARE_DIR");

  if (!fw_dir)
    fw_dir = SCARLETT2_FIRMWARE_DIR;
  enum_firmware_dir(fw_dir);
}

uint32_t scarlett2_get_best_firmware_version(uint32_t pid) {
  struct found_firmware *found = g_hash_table_lookup(
    best_firmware, GINT_TO_POINTER(pid)
  );
  if (!found)
    return 0;

  return found->firmware->firmware_version;
}

struct scarlett2_firmware_file *scarlett2_get_best_firmware(uint32_t pid) {
  struct found_firmware *found = g_hash_table_lookup(
    best_firmware, GINT_TO_POINTER(pid)
  );
  if (!found)
    return NULL;

  return scarlett2_read_firmware_file(found->fn);
}
