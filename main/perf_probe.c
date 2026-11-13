#include "perf_probe.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
/* All measurements run on the display task. Nested PPA time is a subset
 * of flush callback time, not an additional cost. Log once per second. */
static bool active, rendered, cached;
void perf_probe_cache(bool value) {cached=value;}
static int64_t frame_start, flush_start, wait_start, scroll_at, last_log;
static uint64_t frame_flush, frame_wait, frame_ppa, frame_lcd, frame_pixels;
static unsigned frame_batches, scene_count;
static struct {
  unsigned frames, scroll_frames, batches, cached_frames;
  uint64_t total, flush, wait, ppa, lcd, pixels, max;
} sum;
void perf_probe_scroll(void) { scroll_at = esp_timer_get_time(); }
void perf_probe_scene(unsigned count) { scene_count = count; }
esp_err_t __real_lvgl_port_ppa_rotate(void *handle, void *config);
esp_err_t __wrap_lvgl_port_ppa_rotate(void *handle, void *config) {
  int64_t start = esp_timer_get_time();
  esp_err_t result = __real_lvgl_port_ppa_rotate(handle, config);
  if (active)
    frame_ppa += esp_timer_get_time() - start;
  return result;
}
esp_err_t __real_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int,
                                           int, int, const void *);
esp_err_t __wrap_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x1,
                                           int y1, int x2, int y2,
                                           const void *pixels) {
  int64_t start = esp_timer_get_time();
  esp_err_t result =
      __real_esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, pixels);
  if (active)
    frame_lcd += esp_timer_get_time() - start;
  return result;
}
static void measure(lv_event_t *event) {
  int64_t now = esp_timer_get_time();
  switch (lv_event_get_code(event)) {
  case LV_EVENT_REFR_START:
    active = true;
    rendered = false;
    frame_start = now;
    frame_flush = frame_wait = frame_ppa = frame_lcd = frame_pixels = 0;
    frame_batches = 0;
    break;
  case LV_EVENT_RENDER_START:
    rendered = true;
    break;
  case LV_EVENT_FLUSH_START: {
    flush_start = now;
    frame_batches++;
    const lv_area_t *a = lv_event_get_param(event);
    if (a)
      frame_pixels += (uint64_t)(a->x2 - a->x1 + 1) * (a->y2 - a->y1 + 1);
    break;
  }
  case LV_EVENT_FLUSH_FINISH:
    frame_flush += now - flush_start;
    break;
  case LV_EVENT_FLUSH_WAIT_START:
    wait_start = now;
    break;
  case LV_EVENT_FLUSH_WAIT_FINISH:
    frame_wait += now - wait_start;
    break;
  case LV_EVENT_REFR_READY:
    active = false;
    if (rendered) {
      uint64_t duration = now - frame_start;
      sum.frames++;
      sum.cached_frames += cached;
      sum.scroll_frames += scroll_at && now - scroll_at < 200000;
      sum.total += duration;
      sum.flush += frame_flush;
      sum.wait += frame_wait;
      sum.ppa += frame_ppa;
      sum.lcd += frame_lcd;
      sum.pixels += frame_pixels;
      sum.batches += frame_batches;
      if (duration > sum.max)
        sum.max = duration;
    }
    if (now - last_log >= 1000000 && sum.frames) {
      double n = sum.frames * 1000.0;
      ESP_LOGI("scroll_perf",
               "nodes=%u frames=%u scroll=%u cached=%u window=%.0fms avg=%.2fms "
               "max=%.2fms work=%.2fms flush=%.2fms ppa=%.2fms lcd=%.2fms "
               "wait=%.2fms batches=%.1f pixels=%.0f",
               scene_count, sum.frames, sum.scroll_frames, sum.cached_frames,
               (now - last_log) / 1000.0, sum.total / n, sum.max / 1000.0,
               (sum.total - sum.flush - sum.wait) / n, sum.flush / n,
               sum.ppa / n, sum.lcd / n, sum.wait / n,
               (double)sum.batches / sum.frames,
               (double)sum.pixels / sum.frames);
      memset(&sum, 0, sizeof sum);
      last_log = now;
    }
    break;
  default:
    break;
  }
}
void perf_probe_init(lv_display_t *display) {
  last_log = esp_timer_get_time();
  lv_display_add_event_cb(display, measure, LV_EVENT_ALL, NULL);
  ESP_LOGI("scroll_perf",
           "probe enabled; work=refresh minus flush/wait; PPA/LCD included in "
           "flush; times are wall clock, not CPU utilization");
}
