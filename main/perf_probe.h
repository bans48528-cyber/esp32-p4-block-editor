#pragma once
#include "lvgl.h"
#ifdef ESP_PLATFORM
void perf_probe_init(lv_display_t *display);
void perf_probe_scroll(void);
void perf_probe_scene(unsigned count);
void perf_probe_cache(bool active);
#else
static inline void perf_probe_scroll(void) {}
static inline void perf_probe_cache(bool active) {(void)active;}
static inline void perf_probe_scene(unsigned count) { (void)count; }
#endif
