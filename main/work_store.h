#pragma once
#include "projects.h"
typedef struct {
  uint32_t id;
  char name[64];
} WorkSummary;
typedef struct {
  WorkSummary *items;
  size_t count;
} WorkCatalog;
/* Original single-page library: six cards and six thumbnail slots. */
#define WORK_LIMIT PROJECTS
int works_open(void);
void works_catalog_free(WorkCatalog *catalog);
bool works_catalog(WorkCatalog *catalog);
bool works_read(uint32_t id, Project *project);
bool works_save(uint32_t id, const Project *project);
bool works_create(const Project *project, uint32_t *id);
bool works_delete(uint32_t id);
bool works_thumbnail_load(uint32_t id, const Node *nodes, uint8_t *pixels);
bool works_thumbnail_save(uint32_t id, const Node *nodes,
                          const uint8_t *pixels);
