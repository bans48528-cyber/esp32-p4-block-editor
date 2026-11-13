/* Test fixtures use the old six-item shape for convenience. Runtime storage
 * is the independent-file catalog. Reset only this process's test directory. */
#include "work_store.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
static void test_clear_work_files(void) {
  const char *base = getenv("BOARD_PROJECTS_FILE");
  assert(base && !strncmp(base, "test-projects-", 14) && !strchr(base, '/') &&
         !strchr(base, '\\'));
  char dir[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH];
  snprintf(dir, sizeof dir, "%s.works", base);
  snprintf(pattern, sizeof pattern, "%s/*", dir);
  WIN32_FIND_DATAA entry;
  HANDLE scan = FindFirstFileA(pattern, &entry);
  if (scan != INVALID_HANDLE_VALUE) {
    do {
      if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        snprintf(path, sizeof path, "%s/%s", dir, entry.cFileName);
        assert(DeleteFileA(path));
      }
    } while (FindNextFileA(scan, &entry));
    FindClose(scan);
    assert(RemoveDirectoryA(dir));
  }
  snprintf(path, sizeof path, "%s.thumb-slots.bin", base);
  DeleteFileA(path);
}
static bool test_projects_save(const Library *l) {
  if (!projects_valid(l))
    return false;
  if (!projects_save(l))
    return false;
  test_clear_work_files();
  return works_open() >= 0;
}
static int test_projects_load(Library *l) {
  projects_empty(l);
  if (works_open() < 0)
    return -1;
  WorkCatalog c = {0};
  if (!works_catalog(&c))
    return -1;
  bool ok = true;
  for (size_t i = 0; i < c.count && i < PROJECTS; i++)
    ok = ok && works_read(c.items[i].id, &l->items[i]);
  int result = ok ? (c.count ? 1 : 0) : -1;
  works_catalog_free(&c);
  return result;
}
static bool test_thumbnail_load(int slot, const Node *nodes, uint8_t *pixels) {
  WorkCatalog c = {0};
  if (!works_catalog(&c))
    return false;
  bool ok = slot >= 0 && (size_t)slot < c.count &&
            works_thumbnail_load(c.items[slot].id, nodes, pixels);
  works_catalog_free(&c);
  return ok;
}
#define projects_load(l) test_projects_load(l)
#define projects_save(l) test_projects_save(l)
#define thumbnail_load(s, n, p) test_thumbnail_load(s, n, p)
