/* Six-work compatibility store. Projects are read and written as one Library
 * record, matching the original fast implementation. */
#include "work_store.h"
#include "thumbnails.h"
#include <stdlib.h>
#include <string.h>

static Library library;
static bool ready;

int works_open(void) {
  int result = projects_load(&library);
  ready = result >= 0;
  return result;
}

void works_catalog_free(WorkCatalog *catalog) {
  free(catalog->items);
  memset(catalog, 0, sizeof(*catalog));
}

bool works_catalog(WorkCatalog *catalog) {
  if (!ready)
    return false;
  WorkCatalog next = {0};
  for (uint32_t id = 0; id < PROJECTS; id++)
    next.count += library.items[id].used != 0;
  if (next.count) {
    next.items = calloc(next.count, sizeof(*next.items));
    if (!next.items)
      return false;
    size_t at = 0;
    for (uint32_t id = 0; id < PROJECTS; id++)
      if (library.items[id].used)
        next.items[at++].id = id;
  }
  works_catalog_free(catalog);
  *catalog = next;
  return true;
}

bool works_read(uint32_t id, Project *project) {
  if (!ready || id >= PROJECTS || !library.items[id].used)
    return false;
  *project = library.items[id];
  return true;
}

bool works_save(uint32_t id, const Project *project) {
  if (!ready || id >= PROJECTS || !project || !project->used)
    return false;
  Library next = library;
  next.items[id] = *project;
  if (!projects_save(&next))
    return false;
  library = next;
  return true;
}

bool works_create(const Project *project, uint32_t *id) {
  if (!ready || !project || !project->used || !id)
    return false;
  for (uint32_t slot = 0; slot < PROJECTS; slot++)
    if (!library.items[slot].used) {
      if (!works_save(slot, project))
        return false;
      *id = slot;
      return true;
    }
  return false;
}

bool works_delete(uint32_t id) {
  if (!ready || id >= PROJECTS || !library.items[id].used)
    return false;
  Library next = library;
  memset(&next.items[id], 0, sizeof(next.items[id]));
  if (!projects_save(&next))
    return false;
  library = next;
  return true;
}

bool works_thumbnail_load(uint32_t id, const Node *nodes, uint8_t *pixels) {
  return ready && id < PROJECTS && thumbnail_load((int)id, nodes, pixels);
}

bool works_thumbnail_save(uint32_t id, const Node *nodes,
                          const uint8_t *pixels) {
  return ready && id < PROJECTS && thumbnail_save((int)id, nodes, pixels);
}
