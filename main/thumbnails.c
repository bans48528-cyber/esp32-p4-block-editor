#include "thumbnails.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t checksum(const uint8_t *p) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < THUMB_BYTES; i++)
    h = (h ^ p[i]) * 16777619u;
  return h;
}

#ifndef ESP_PLATFORM
#include <windows.h>
static bool thumb_path(char *out, size_t size, int slot) {
  const char *base = getenv("BOARD_PROJECTS_FILE");
  return slot >= 0 && slot < PROJECTS &&
         snprintf(out, size, "%s.thumb-%d.bin",
                  base ? base : "board-projects.bin", slot) < (int)size;
}
bool thumbnail_load(int slot, const Node *nodes, uint8_t *pixels) {
  char path[MAX_PATH];
  if (!thumb_path(path, sizeof path, slot))
    return false;
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  uint32_t header[4];
  Node saved[LIMIT];
  bool ok = fread(header, 1, sizeof header, f) == sizeof header &&
            header[0] == 0x54484d37u && header[1] == THUMB_WIDTH &&
            header[2] == THUMB_HEIGHT &&
            fread(saved, 1, sizeof saved, f) == sizeof saved &&
            !memcmp(saved, nodes, sizeof saved) &&
            fread(pixels, 1, THUMB_BYTES, f) == THUMB_BYTES &&
            fgetc(f) == EOF && header[3] == checksum(pixels);
  fclose(f);
  return ok;
}
bool thumbnail_save(int slot, const Node *nodes, const uint8_t *pixels) {
  char path[MAX_PATH], temporary[MAX_PATH];
  if (!thumb_path(path, sizeof path, slot) ||
      snprintf(temporary, sizeof temporary, "%s.tmp", path) >=
          (int)sizeof temporary)
    return false;
  FILE *f = fopen(temporary, "wb");
  if (!f)
    return false;
  uint32_t header[] = {0x54484d37u, THUMB_WIDTH, THUMB_HEIGHT,
                       checksum(pixels)};
  bool ok = fwrite(header, 1, sizeof header, f) == sizeof header &&
            fwrite(nodes, 1, sizeof(Node) * LIMIT, f) == sizeof(Node) * LIMIT &&
            fwrite(pixels, 1, THUMB_BYTES, f) == THUMB_BYTES;
  if (fclose(f))
    ok = false;
  if (ok)
    ok = MoveFileExA(temporary, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  if (!ok)
    DeleteFileA(temporary);
  return ok;
}
#else
#include "esp_log.h"
#include "esp_partition.h"
#define BANK_SIZE 65536u
#define THUMB_MAGIC 0x54484d38u
typedef struct {
  uint32_t magic, generation, width, height, crc;
  Node nodes[LIMIT];
} ThumbHeader;
_Static_assert(sizeof(ThumbHeader) + THUMB_BYTES <= BANK_SIZE,
               "thumbnail bank too small");
static const esp_partition_t *thumb_partition(void) {
  const esp_partition_t *p =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "thumbnails");
  return p && p->size >= PROJECTS * 2 * BANK_SIZE ? p : NULL;
}
static bool header_read(const esp_partition_t *p, int bank, ThumbHeader *h) {
  return esp_partition_read(p, bank * BANK_SIZE, h, sizeof(*h)) == ESP_OK &&
         h->magic == THUMB_MAGIC && h->width == THUMB_WIDTH &&
         h->height == THUMB_HEIGHT;
}
static bool newer(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }
bool thumbnail_load(int slot, const Node *nodes, uint8_t *pixels) {
  const esp_partition_t *p = thumb_partition();
  if (!p || slot < 0 || slot >= PROJECTS)
    return false;
  ThumbHeader h[2];
  bool valid[2];
  for (int i = 0; i < 2; i++)
    valid[i] = header_read(p, slot * 2 + i, &h[i]) &&
               !memcmp(nodes, h[i].nodes, sizeof h[i].nodes);
  int first = valid[1] && (!valid[0] || newer(h[1].generation, h[0].generation))
                  ? 1
                  : 0;
  for (int k = 0; k < 2; k++) {
    int i = first ^ k;
    if (valid[i] &&
        esp_partition_read(p, (slot * 2 + i) * BANK_SIZE + sizeof(ThumbHeader),
                           pixels, THUMB_BYTES) == ESP_OK &&
        checksum(pixels) == h[i].crc) {
      ESP_LOGI("thumbnail", "loaded slot=%d generation=%lu", slot,
               (unsigned long)h[i].generation);
      return true;
    }
  }
  return false;
}
bool thumbnail_save(int slot, const Node *nodes, const uint8_t *pixels) {
  const esp_partition_t *p = thumb_partition();
  if (!p || slot < 0 || slot >= PROJECTS)
    return false;
  uint8_t *verify = malloc(THUMB_BYTES);
  if (!verify)
    return false;
  if (thumbnail_load(slot, nodes, verify) &&
      !memcmp(verify, pixels, THUMB_BYTES)) {
    free(verify);
    return true;
  }
  ThumbHeader h[2];
  bool v[2];
  for (int i = 0; i < 2; i++)
    v[i] =
        header_read(p, slot * 2 + i, &h[i]) &&
        esp_partition_read(p, (slot * 2 + i) * BANK_SIZE + sizeof(ThumbHeader),
                           verify, THUMB_BYTES) == ESP_OK &&
        checksum(verify) == h[i].crc;
  int latest =
      v[1] && (!v[0] || newer(h[1].generation, h[0].generation)) ? 1 : 0;
  int target = (v[0] || v[1]) ? (latest ^ 1) : 0;
  ThumbHeader next = {0};
  next.magic = UINT32_MAX;
  next.generation = (v[0] || v[1]) ? h[latest].generation + 1 : 1;
  next.width = THUMB_WIDTH;
  next.height = THUMB_HEIGHT;
  next.crc = checksum(pixels);
  memcpy(next.nodes, nodes, sizeof next.nodes);
  size_t offset = (slot * 2 + target) * BANK_SIZE;
  ThumbHeader readback;
  bool ok =
      esp_partition_erase_range(p, offset, BANK_SIZE) == ESP_OK &&
      esp_partition_write(p, offset, &next, sizeof next) == ESP_OK &&
      esp_partition_write(p, offset + sizeof next, pixels, THUMB_BYTES) ==
          ESP_OK &&
      esp_partition_read(p, offset + sizeof next, verify, THUMB_BYTES) ==
          ESP_OK &&
      !memcmp(verify, pixels, THUMB_BYTES) &&
      esp_partition_read(p, offset, &readback, sizeof readback) == ESP_OK &&
      !memcmp(&readback, &next, sizeof next);
  uint32_t marker = THUMB_MAGIC;
  if (ok)
    ok = esp_partition_write(p, offset, &marker, sizeof marker) == ESP_OK;
  free(verify);
  if (ok)
    ESP_LOGI("thumbnail", "saved slot=%d generation=%lu", slot,
             (unsigned long)next.generation);
  return ok;
}
#endif
