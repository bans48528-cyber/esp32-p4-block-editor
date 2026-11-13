/* Diagnostic firmware only: same unsaved 32-block scene and wall-clock
 * trajectory in both A/B builds. No robot commands and no storage writes. */
#ifdef CONFIG_BOARD_SCROLL_BENCHMARK
#include "esp_log.h"
static uint32_t benchmark_started;
static int benchmark_phase;
static void benchmark_tick(lv_timer_t *timer) {
  if (benchmark_phase == 0) {
    memset(nodes, 0, sizeof nodes);
    for (int i = 0; i < 32; i++)
      model_add(nodes, i % 3 == 0 ? WAIT : MOTOR_STOP, -1, i);
    active_project = 0;
    open_editor();
    say("滑动性能测试中，请暂勿触摸屏幕");
    benchmark_phase = 1;
    benchmark_started = lv_tick_get();
    return;
  }
  uint32_t elapsed = lv_tick_elaps(benchmark_started);
  if (benchmark_phase == 1) {
    if (elapsed < 3000)
      return;
    benchmark_panning = true;
    scroll_cache_event(LV_EVENT_SCROLL_BEGIN);
    benchmark_phase = 2;
    benchmark_started = lv_tick_get();
#ifdef CONFIG_BOARD_SCROLL_LEGACY
    ESP_LOGI("scroll_bench",
             "BEGIN mode=legacy nodes=32 duration=24000 speed=400px/s");
#else
    ESP_LOGI("scroll_bench",
             "BEGIN mode=tiles nodes=32 duration=24000 speed=400px/s");
#endif
    return;
  }
  if (elapsed >= 24000) {
    benchmark_panning = false;
    scroll_cache_end();
    ESP_LOGI("scroll_bench", "END");
    lv_timer_delete(timer);
    home();
    return;
  }
  /* 0..1600..0 at 400 px/s; three identical round trips. */
  int t = elapsed % 8000;
  int x = (t <= 4000 ? t : 8000 - t) * 400 / 1000;
  lv_obj_scroll_to_x(canvas, x, LV_ANIM_OFF);
}
void editor_scroll_benchmark_schedule(void) {
  benchmark_phase = 0;
  lv_timer_t *timer = lv_timer_create(benchmark_tick, 16, NULL);
  /* Allow startup and the serial logger to settle before opening the scene. */
  lv_timer_set_period(timer, 16);
}
#else
void editor_scroll_benchmark_schedule(void) {}
#endif
