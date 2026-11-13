#pragma once
#include "lvgl.h"
void editor_create(void);
typedef struct {
  unsigned bytes, budget, tiles, rendered, invalidated, hits, misses;
  bool active;
} EditorCacheStats;
void editor_cache_stats(EditorCacheStats *stats);
void editor_scroll_benchmark_schedule(void);
