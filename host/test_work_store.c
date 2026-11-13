#include "thumbnails.h"
#include "work_store.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
int main(void) {
  char base[80];
  snprintf(base, sizeof base, "test-projects-six-%lu.bin",
           GetCurrentProcessId());
  _putenv_s("BOARD_PROJECTS_FILE", base);
  DeleteFileA(base);
  Library empty;
  projects_empty(&empty);
  assert(projects_save(&empty));
  assert(works_open() == 1);
  Project p = {.used = 1};
  strcpy(p.name, "Work");
  model_add(p.nodes, MOTOR, -1, 0);
  for (uint32_t i = 0; i < PROJECTS; i++) {
    uint32_t id;
    assert(works_create(&p, &id) && id == i);
  }
  uint32_t id;
  assert(!works_create(&p, &id));
  WorkCatalog catalog = {0};
  assert(works_catalog(&catalog) && catalog.count == PROJECTS);
  assert(works_delete(2));
  assert(works_create(&p, &id) && id == 2);
  assert(works_open() == 1);
  assert(works_read(2, &p));
  works_catalog_free(&catalog);
  DeleteFileA(base);
  puts("PASS: original single-record six-work store and slot reuse");
  return 0;
}
