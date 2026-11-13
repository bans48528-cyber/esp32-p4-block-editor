#include "esp_partition.h"
#include "thumbnails.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char flash[0xc0000];
static const esp_partition_t part = {sizeof flash};
static int fail_at, writes;
const esp_partition_t *esp_partition_find_first(int type, int subtype,
                                                const char *label) {
  assert(type == 1 && subtype == 0x40 && !strcmp(label, "thumbnails"));
  return &part;
}
int esp_partition_read(const esp_partition_t *p, size_t off, void *out,
                       size_t n) {
  assert(p == &part && off + n <= p->size);
  memcpy(out, flash + off, n);
  return 0;
}
int esp_partition_erase_range(const esp_partition_t *p, size_t off, size_t n) {
  assert(p == &part && off + n <= p->size && off % 4096 == 0 && n % 4096 == 0);
  if (++writes == fail_at)
    return -1;
  memset(flash + off, 255, n);
  return 0;
}
int esp_partition_write(const esp_partition_t *p, size_t off, const void *src,
                        size_t n) {
  assert(p == &part && off + n <= p->size);
  if (++writes == fail_at)
    return -1;
  const unsigned char *s = src;
  for (size_t i = 0; i < n; i++) {
    assert((flash[off + i] & s[i]) == s[i]);
    flash[off + i] = s[i];
  }
  return 0;
}
int main(void) {
  memset(flash, 255, sizeof flash);
  Node a[LIMIT] = {0}, b[LIMIT] = {0};
  a[0].type = MOTOR;
  b[0].type = WAIT;
  unsigned char *image = malloc(THUMB_BYTES), *out = malloc(THUMB_BYTES);
  assert(image && out);
  for (size_t i = 0; i < THUMB_BYTES; i++)
    image[i] = (unsigned char)i;
  assert(!thumbnail_load(0, a, out));
  assert(thumbnail_save(0, a, image));
  assert(thumbnail_load(0, a, out) && !memcmp(image, out, THUMB_BYTES));
  int oldwrites = writes;
  assert(thumbnail_save(0, a, image));
  assert(writes == oldwrites);
  assert(thumbnail_save(0, b, image));
  assert(thumbnail_load(0, b, out));
  image[0] ^= 255;
  assert(thumbnail_save(0, b, image));
  flash[sizeof(uint32_t) * 5 + sizeof(Node) * LIMIT] ^= 1;
  assert(thumbnail_load(0, b, out));
  assert(out[0] != image[0]);
  writes = 0;
  fail_at = 3;
  assert(!thumbnail_save(0, b, image));
  fail_at = 0;
  assert(thumbnail_load(0, b, out));
  for (int i = 1; i < PROJECTS; i++)
    assert(thumbnail_save(i, a, image));
  for (int i = 1; i < PROJECTS; i++)
    assert(thumbnail_load(i, a, out));
  assert(!thumbnail_save(PROJECTS, a, image));
  free(image);
  free(out);
  puts("PASS: original six flash thumbnail slots and CRC fallback");
  return 0;
}
