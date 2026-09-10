#include "projects.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "nvs.h"
#include "nvs_flash.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#endif
#define MAGIC 0x43505231u
void projects_empty(Library *l) {
  memset(l, 0, sizeof(*l));
  l->magic = MAGIC;
  l->version = 2;
}
bool projects_valid(const Library *l) {
  if (l->magic != MAGIC || l->version != 2)
    return false;
  for (int i = 0; i < PROJECTS; i++) {
    const Project *p = &l->items[i];
    if (p->used > 1)
      return false;
    if (p->used &&
        (!p->name[0] || !memchr(p->name, 0, 64) || !model_valid(p->nodes)))
      return false;
  }
  return true;
}
bool projects_decode(Library *l, const void *data, size_t size) {
  typedef struct {
    int32_t type, parent, order, value, port;
  } OldNode;
  typedef struct {
    uint32_t used;
    char name[64];
    OldNode nodes[24];
  } OldProject;
  typedef struct {
    uint32_t magic, version;
    OldProject items[6];
  } OldLibrary;
  projects_empty(l);
  if (size == sizeof(Library)) {
    const Library *v = data;
    if (!projects_valid(v))
      return false;
    *l = *v;
    return true;
  }
  if (size != sizeof(OldLibrary))
    return false;
  const OldLibrary *old = data;
  if (old->magic != MAGIC || old->version != 1)
    return false;
  for (int p = 0; p < PROJECTS; p++) {
    if (old->items[p].used > 1)
      goto invalid;
    l->items[p].used = old->items[p].used;
    memcpy(l->items[p].name, old->items[p].name, 64);
    if (!old->items[p].used)
      continue;
    for (int i = 0; i < 24; i++) {
      OldNode v = old->items[p].nodes[i];
      if (!v.type)
        continue;
      if (v.type < 1 || v.type > 3 || v.value < 1 || v.value > 99 ||
          v.parent < -1 || v.parent >= 24 || v.order < 0 || v.port < 0 ||
          v.port > 3)
        goto invalid;
      if (v.parent >= 0 &&
          (v.type == LOOP || old->items[p].nodes[v.parent].type != LOOP ||
           old->items[p].nodes[v.parent].parent != -1))
        goto invalid;
      int count = 0;
      for (int j = 0; j < 24; j++)
        if (old->items[p].nodes[j].type &&
            old->items[p].nodes[j].parent == v.parent)
          count++;
      if (count > (v.parent < 0 ? 8 : 6))
        goto invalid;
      l->items[p].nodes[i] = (Node){
          v.type, v.parent, v.order, v.type == LOOP ? v.value : v.value * 10,
          v.port, 0,        0};
    }
  }
  if (projects_valid(l))
    return true;
invalid:
  projects_empty(l);
  return false;
}
#ifdef ESP_PLATFORM
int projects_load(Library *l) {
  projects_empty(l);
  if (nvs_flash_init() != ESP_OK)
    return -1;
  nvs_handle_t h;
  esp_err_t err = nvs_open("card_projects", NVS_READONLY, &h);
  if (err == ESP_ERR_NVS_NOT_FOUND)
    return 0;
  if (err != ESP_OK)
    return -1;
  const char *key = "library_v2";
  size_t size = 0;
  err = nvs_get_blob(h, key, NULL, &size);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    key = "library_v1";
    err = nvs_get_blob(h, key, NULL, &size);
  }
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(h);
    return 0;
  }
  if (err != ESP_OK || !size || size > sizeof(Library)) {
    nvs_close(h);
    return -1;
  }
  void *data = malloc(size);
  if (!data) {
    nvs_close(h);
    return -1;
  }
  err = nvs_get_blob(h, key, data, &size);
  nvs_close(h);
  bool ok = err == ESP_OK && projects_decode(l, data, size);
  free(data);
  return ok ? 1 : -1;
}
bool projects_save(const Library *l) {
  if (!projects_valid(l))
    return false;
  nvs_handle_t h;
  if (nvs_open("card_projects", NVS_READWRITE, &h) != ESP_OK)
    return false;
  esp_err_t err = nvs_set_blob(h, "library_v2", l, sizeof(*l));
  if (err == ESP_OK)
    err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}
#else
static const char *store_path(void) {
  const char *p = getenv("BOARD_PROJECTS_FILE");
  return p ? p : "board-projects.bin";
}
int projects_load(Library *l) {
  projects_empty(l);
  const char *path = store_path();
  FILE *f = fopen(path, "rb");
  if (!f)
    return GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES &&
                   GetLastError() == ERROR_FILE_NOT_FOUND
               ? 0
               : -1;
  void *data = malloc(sizeof(Library));
  if (!data) {
    fclose(f);
    return -1;
  }
  size_t n = fread(data, 1, sizeof(Library), f);
  int extra = fgetc(f);
  fclose(f);
  bool ok = extra == EOF && projects_decode(l, data, n);
  free(data);
  return ok ? 1 : -1;
}
bool projects_save(const Library *l) {
  if (!projects_valid(l))
    return false;
  char temporary[MAX_PATH];
  if (snprintf(temporary, sizeof temporary, "%s.tmp", store_path()) >=
      (int)sizeof temporary)
    return false;
  FILE *f = fopen(temporary, "wb");
  if (!f)
    return false;
  bool ok = fwrite(l, 1, sizeof(*l), f) == sizeof(*l);
  if (fclose(f) != 0)
    ok = false;
  if (ok)
    ok = MoveFileExA(temporary, store_path(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  return ok;
}
#endif
