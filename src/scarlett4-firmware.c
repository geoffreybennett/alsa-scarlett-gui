// SPDX-FileCopyrightText: 2023-2025 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scarlett4-firmware.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <glib.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

const char *scarlett4_firmware_type_magic[] = {
  [SCARLETT4_FIRMWARE_CONTAINER] = "SCARLBOX",
  [SCARLETT4_FIRMWARE_APP]       = "SCARLET4",
  [SCARLETT4_FIRMWARE_ESP]       = "SCARLESP",
  [SCARLETT4_FIRMWARE_LEAPFROG]  = "SCARLEAP"
};

// Hash table to store the best firmware for each PID
struct found_firmware {
  char                         *fn;
  struct scarlett4_firmware_container *container;
};

static GHashTable *best_firmware = NULL;

static int verify_sha256(
  const unsigned char *data,
  size_t length,
  const unsigned char *expected_hash
) {
  unsigned char computed_hash[SHA256_DIGEST_LENGTH];
  SHA256(data, length, computed_hash);
  return memcmp(computed_hash, expected_hash, SHA256_DIGEST_LENGTH) == 0;
}

// Calculate MD5
static void calc_md5(
  const unsigned char *data,
  size_t               length,
  unsigned char       *md5_out
) {
  EVP_MD_CTX   *mdctx;
  const EVP_MD *md;
  unsigned int  md_len;

  mdctx = EVP_MD_CTX_new();
  if (mdctx == NULL) {
    fprintf(stderr, "Failed to create MD5 context\n");
    return;
  }
  md = EVP_md5();
  if (md == NULL) {
    fprintf(stderr, "Failed to get MD5 digest\n");
    EVP_MD_CTX_free(mdctx);
    return;
  }

  EVP_DigestInit_ex(mdctx, md, NULL);
  EVP_DigestUpdate(mdctx, data, length);
  EVP_DigestFinal_ex(mdctx, md5_out, &md_len);

  EVP_MD_CTX_free(mdctx);
}

// Convert magic string to enum
static int firmware_type_from_magic(const char *magic) {
  for (int i = 0; i < SCARLETT4_FIRMWARE_TYPE_COUNT; i++)
    if (strncmp(magic, scarlett4_firmware_type_magic[i], 8) == 0)
      return i;
  return -1;
}

static int read_magic(FILE *file, const char *fn) {
  char magic[8];
  if (fread(magic, sizeof(magic), 1, file) != 1) {
    perror("Failed to read magic");
    fprintf(stderr, "Error reading magic from %s\n", fn);
    return -1;
  }

  return firmware_type_from_magic(magic);
}

// Convert from disk format to memory format
static struct scarlett4_firmware *firmware_header_disk_to_mem(
  int type,
  const struct scarlett4_firmware_header_disk *disk
) {
  struct scarlett4_firmware *firmware = calloc(1, sizeof(struct scarlett4_firmware));
  if (!firmware) {
    perror("Failed to allocate memory for firmware");
    return NULL;
  }

  firmware->type = type;
  firmware->usb_vid = ntohs(disk->usb_vid);
  firmware->usb_pid = ntohs(disk->usb_pid);
  for (int i = 0; i < 4; i++)
    firmware->firmware_version[i] = ntohl(disk->firmware_version[i]);
  firmware->firmware_length = ntohl(disk->firmware_length);
  memcpy(firmware->sha256, disk->sha256, 32);

  return firmware;
}

static struct scarlett4_firmware_container *firmware_container_header_disk_to_mem(
  const struct scarlett4_firmware_container_header_disk *disk
) {
  struct scarlett4_firmware_container *container = calloc(
    1, sizeof(struct scarlett4_firmware_container)
  );
  if (!container) {
    perror("Failed to allocate memory for firmware container");
    return NULL;
  }

  container->usb_vid = ntohs(disk->usb_vid);
  container->usb_pid = ntohs(disk->usb_pid);
  for (int i = 0; i < 4; i++)
    container->firmware_version[i] = ntohl(disk->firmware_version[i]);
  container->num_sections = ntohl(disk->num_sections);

  return container;
}

static struct scarlett4_firmware *read_header(
  FILE       *file,
  const char *fn,
  int         type
) {
  struct scarlett4_firmware_header_disk disk_header;
  size_t read_count = fread(
    &disk_header, sizeof(struct scarlett4_firmware_header_disk), 1, file
  );

  if (read_count != 1) {
    fprintf(stderr, "Error reading firmware header\n");
    return NULL;
  }

  struct scarlett4_firmware *firmware = firmware_header_disk_to_mem(type, &disk_header);

  if (!firmware)
    fprintf(stderr, "Invalid firmware header\n");

  return firmware;
}

static struct scarlett4_firmware *read_header_and_data(
  FILE       *file,
  const char *fn,
  int         type
) {
  struct scarlett4_firmware *firmware = read_header(file, fn, type);
  if (!firmware) {
    fprintf(stderr, "Error reading firmware header from %s\n", fn);
    return NULL;
  }

  // Read firmware data
  firmware->firmware_data = malloc(firmware->firmware_length);
  if (!firmware->firmware_data) {
    perror("Failed to allocate memory for firmware data");
    goto error;
  }
  size_t read_count = fread(
    firmware->firmware_data, 1, firmware->firmware_length, file
  );
  if (read_count != firmware->firmware_length) {
    if (feof(file))
      fprintf(stderr, "Unexpected end of file\n");
    else
      perror("Failed to read firmware data");
    fprintf(stderr, "Error reading firmware data from %s\n", fn);
    goto error;
  }

  // Verify the firmware data
  if (!verify_sha256(
    firmware->firmware_data,
    firmware->firmware_length,
    firmware->sha256
  )) {
    fprintf(stderr, "Corrupt firmware (failed checksum) in %s\n", fn);
    goto error;
  }

  // Add the MD5 for ESP firmware
  if (firmware->type == SCARLETT4_FIRMWARE_ESP) {
    calc_md5(
      firmware->firmware_data,
      firmware->firmware_length,
      firmware->md5
    );
  }

  return firmware;

error:
  free(firmware->firmware_data);
  free(firmware);
  return NULL;
}

static struct scarlett4_firmware *read_magic_and_header_and_data(
  FILE       *file,
  const char *fn,
  int         section
) {
  int type = read_magic(file, fn);

  if (type < 0 || type == SCARLETT4_FIRMWARE_CONTAINER) {
    fprintf(
      stderr,
      "Invalid firmware type %d in section %d of %s\n",
      type,
      section + 1,
      fn
    );
    return NULL;
  }

  return read_header_and_data(file, fn, type);
}

static struct scarlett4_firmware_container *read_firmware_container_header(
  FILE       *file,
  const char *fn
) {
  struct scarlett4_firmware_container_header_disk disk_header;

  // Read header
  if (fread(
    &disk_header,
    sizeof(struct scarlett4_firmware_container_header_disk),
    1,
    file
  ) != 1) {
    perror("Failed to read container header");
    fprintf(stderr, "Error reading container header from %s\n", fn);
    return NULL;
  }

  return firmware_container_header_disk_to_mem(&disk_header);
}

static struct scarlett4_firmware_container *read_firmware_container(
  FILE       *file,
  const char *fn
) {
  struct scarlett4_firmware_container *container =
    read_firmware_container_header(file, fn);
  if (!container) {
    fprintf(stderr, "Error reading container header from %s\n", fn);
    return NULL;
  }

  if (container->num_sections < 1 || container->num_sections > 3) {
    fprintf(
      stderr,
      "Invalid number of sections in %s: %d\n",
      fn,
      container->num_sections
    );
    goto error;
  }

  // Allocate memory for sections
  container->sections = calloc(
    container->num_sections, sizeof(struct scarlett4_firmware *)
  );
  if (!container->sections) {
    perror("Failed to allocate memory for firmware sections");
    goto error;
  }

  // Read sections
  for (uint32_t i = 0; i < container->num_sections; i++) {
    container->sections[i] = read_magic_and_header_and_data(file, fn, i);
    if (!container->sections[i]) {
      fprintf(stderr, "Error reading section %d from %s\n", i + 1, fn);
      goto error;
    }
  }

  return container;

error:
  scarlett4_free_firmware_container(container);
  return NULL;
}

struct scarlett4_firmware_container *scarlett4_read_firmware_header(const char *fn) {
  FILE *file = fopen(fn, "rb");
  if (!file) {
    perror("fopen");
    fprintf(stderr, "Unable to open %s\n", fn);
    return NULL;
  }

  int type = read_magic(file, fn);

  struct scarlett4_firmware_container *container = NULL;

  if (type == SCARLETT4_FIRMWARE_CONTAINER) {
    container = read_firmware_container_header(file, fn);
    fclose(file);
    return container;
  }

  struct scarlett4_firmware *firmware = read_header(file, fn, type);
  if (!firmware) {
    fprintf(stderr, "Error reading firmware header from %s\n", fn);
    fclose(file);
    return NULL;
  }

  container = calloc(1, sizeof(struct scarlett4_firmware_container));
  if (!container) {
    perror("Failed to allocate memory for firmware container");
    free(firmware);
    fclose(file);
    return NULL;
  }

  container->usb_pid = firmware->usb_pid;
  container->usb_vid = firmware->usb_vid;
  memcpy(container->firmware_version, firmware->firmware_version,
         sizeof(container->firmware_version));
  container->num_sections = 1;
  container->sections = calloc(1, sizeof(struct scarlett4_firmware *));
  container->sections[0] = firmware;

  fclose(file);

  return container;
}

struct scarlett4_firmware_container *scarlett4_read_firmware_file(const char *fn) {
  // Open file
  FILE *file = fopen(fn, "rb");
  if (!file) {
    perror("fopen");
    fprintf(stderr, "Unable to open %s\n", fn);
    return NULL;
  }

  // Read magic string
  int type = read_magic(file, fn);

  if (type < 0) {
    fprintf(stderr, "Invalid firmware type\n");
    fclose(file);
    return NULL;
  }

  // Container?
  if (type == SCARLETT4_FIRMWARE_CONTAINER) {
    struct scarlett4_firmware_container *container =
      read_firmware_container(file, fn);
    fclose(file);
    return container;
  }

  // Not a container; read the firmware header and data
  struct scarlett4_firmware *firmware = read_header_and_data(file, fn, type);
  if (!firmware) {
    fprintf(stderr, "Error reading firmware from %s\n", fn);
    fclose(file);
    return NULL;
  }

  fclose(file);

  // Put the firmware header and data into a container
  struct scarlett4_firmware_container *container = calloc(
    1, sizeof(struct scarlett4_firmware_container)
  );
  if (!container) {
    perror("Failed to allocate memory for firmware container");
    free(firmware->firmware_data);
    free(firmware);
    return NULL;
  }

  container->usb_pid = firmware->usb_pid;
  container->usb_vid = firmware->usb_vid;
  memcpy(container->firmware_version, firmware->firmware_version,
         sizeof(container->firmware_version));
  container->num_sections = 1;
  container->sections = calloc(1, sizeof(struct scarlett4_firmware *));
  container->sections[0] = firmware;

  return container;
}

void scarlett4_free_firmware_container(struct scarlett4_firmware_container *container) {
  if (!container)
    return;

  if (container->sections) {
    for (uint32_t i = 0; i < container->num_sections; i++) {
      struct scarlett4_firmware *firmware = container->sections[i];
      if (!firmware)
        continue;

      free(firmware->firmware_data);
      free(firmware);
    }

    free(container->sections);
  }

  free(container);
}

const char *scarlett4_firmware_type_to_string(enum scarlett4_firmware_type type) {
  switch (type) {
    case SCARLETT4_FIRMWARE_CONTAINER: return "container";
    case SCARLETT4_FIRMWARE_APP:       return "App";
    case SCARLETT4_FIRMWARE_ESP:       return "ESP";
    case SCARLETT4_FIRMWARE_LEAPFROG:  return "Leapfrog";
    default:                           return "unknown";
  }
}

// Compare two 4-valued firmware versions
// Returns: -1 if v1 < v2, 0 if equal, 1 if v1 > v2
static int firmware_version_cmp(const uint32_t *v1, const uint32_t *v2) {
  for (int i = 0; i < 4; i++) {
    if (v1[i] < v2[i])
      return -1;
    if (v1[i] > v2[i])
      return 1;
  }
  return 0;
}

static void free_found_firmware(gpointer data) {
  struct found_firmware *found = data;
  free(found->fn);
  scarlett4_free_firmware_container(found->container);
  free(found);
}

static void init_best_firmware(void) {
  if (best_firmware)
    return;

  best_firmware = g_hash_table_new_full(
    g_direct_hash, g_direct_equal, NULL, free_found_firmware
  );
}

// Add a firmware file to the list of found firmware files,
// if it's better than the one already found for the same device.
static void add_found_firmware(
  char                          *fn,
  struct scarlett4_firmware_container *container
) {
  gpointer key = GINT_TO_POINTER(container->usb_pid);
  struct found_firmware *found = g_hash_table_lookup(best_firmware, key);

  // Already have a firmware file for this device?
  if (found) {
    // Lower or equal version, ignore
    if (firmware_version_cmp(container->firmware_version,
                             found->container->firmware_version) <= 0) {
      free(fn);
      scarlett4_free_firmware_container(container);
      return;
    }

    // Higher version, replace
    g_hash_table_remove(best_firmware, key);
  }

  found = malloc(sizeof(struct found_firmware));
  if (!found) {
    perror("Failed to allocate memory for firmware structure");
    free(fn);
    scarlett4_free_firmware_container(container);
    return;
  }

  found->fn = fn;
  found->container = container;

  g_hash_table_insert(best_firmware, key, found);
}

// Look for firmware files in the given directory
static void enum_firmware_dir(const char *dir_name) {
  DIR *dir = opendir(dir_name);

  if (!dir) {
    if (errno != ENOENT)
      fprintf(stderr, "Error opening directory %s: %s\n",
              dir_name, strerror(errno));
    return;
  }

  struct dirent *entry;

  while ((entry = readdir(dir))) {
    char *full_fn;

    // Check if the file is a .bin file
    if (strlen(entry->d_name) < 4 ||
        strcmp(entry->d_name + strlen(entry->d_name) - 4, ".bin") != 0)
      continue;

    // Check if the file is a regular file
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

    // Read the firmware header
    struct scarlett4_firmware_container *container = scarlett4_read_firmware_header(full_fn);
    if (!container) {
      g_free(full_fn);
      continue;
    }

    add_found_firmware(full_fn, container);
  }

  closedir(dir);
}

void scarlett4_enum_firmware(void) {
  init_best_firmware();

  // Check for override directory
  const char *override_dir = getenv("SCARLETT4_FIRMWARE_DIR");
  if (override_dir) {
    enum_firmware_dir(override_dir);
    return;
  }

  enum_firmware_dir(SCARLETT4_FIRMWARE_DIR);
}

uint32_t *scarlett4_get_best_firmware_version(uint32_t pid) {
  struct found_firmware *found = g_hash_table_lookup(
    best_firmware, GINT_TO_POINTER(pid)
  );
  if (!found)
    return NULL;
  return found->container->firmware_version;
}

struct scarlett4_firmware_container *scarlett4_get_best_firmware(uint32_t pid) {
  struct found_firmware *found = g_hash_table_lookup(
    best_firmware, GINT_TO_POINTER(pid)
  );
  if (!found)
    return NULL;

  // Read the full firmware file (with data)
  return scarlett4_read_firmware_file(found->fn);
}

// Compare two 4-valued firmware versions
// Returns: -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
static int version_cmp(const uint32_t *v1, const uint32_t *v2) {
  for (int i = 0; i < 4; i++) {
    if (v1[i] < v2[i]) return -1;
    if (v1[i] > v2[i]) return 1;
  }
  return 0;
}

// Find a firmware section by type in a container
static struct scarlett4_firmware *find_section(
  struct scarlett4_firmware_container *container,
  enum scarlett4_firmware_type type
) {
  for (uint32_t i = 0; i < container->num_sections; i++)
    if (container->sections[i]->type == type)
      return container->sections[i];
  return NULL;
}

int scarlett4_is_mid_upgrade(struct alsa_card *card) {
  struct found_firmware *found = g_hash_table_lookup(
    best_firmware, GINT_TO_POINTER(card->pid)
  );
  if (!found)
    return 0;

  // Read the full container (enumeration only stores metadata)
  struct scarlett4_firmware_container *container =
    scarlett4_read_firmware_file(found->fn);
  if (!container)
    return 0;

  // Find leapfrog and ESP sections
  struct scarlett4_firmware *leapfrog_fw =
    find_section(container, SCARLETT4_FIRMWARE_LEAPFROG);
  struct scarlett4_firmware *esp_fw =
    find_section(container, SCARLETT4_FIRMWARE_ESP);

  if (!leapfrog_fw || !esp_fw) {
    scarlett4_free_firmware_container(container);
    return 0;
  }

  // Check if leapfrog is loaded (device firmware == leapfrog version)
  int leapfrog_loaded =
    version_cmp(card->firmware_version_4, leapfrog_fw->firmware_version) == 0;

  // Check if ESP needs update
  int need_esp =
    version_cmp(card->esp_firmware_version, esp_fw->firmware_version) != 0;

  fprintf(stderr, "is_mid_upgrade: leapfrog_loaded=%d need_esp=%d\n",
    leapfrog_loaded, need_esp);

  scarlett4_free_firmware_container(container);

  return leapfrog_loaded && need_esp;
}
