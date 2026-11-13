/* Cache only the completed program scene. Editing keeps the real LVGL tree.
 * Preparation is idle-only; a missing/oversized cache falls back to live drawing. */
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "esp_log.h"
#endif
static lv_timer_t *scroll_cache_timer;
static lv_draw_buf_t *scroll_cache_pixels;
static lv_obj_t *scroll_cache_image, *scroll_cache_source;
static bool scroll_cache_attempted;
static void scroll_cache_end(void) {
  if (scroll_cache_image) {
    lv_obj_remove_flag(scroll_cache_source, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete(scroll_cache_image);
    scroll_cache_image = NULL;
    perf_probe_cache(false);
  }
}
static void scroll_cache_invalidate(void) {
  scroll_cache_end();
  if (scroll_cache_pixels) {
    lv_image_cache_drop(scroll_cache_pixels);
    lv_draw_buf_destroy(scroll_cache_pixels);
    scroll_cache_pixels = NULL;
  }
  scroll_cache_attempted = false;
  if (scroll_cache_timer) lv_timer_reset(scroll_cache_timer);
}
static void scroll_cache_dispose(void) {
  scroll_cache_invalidate();
  if (scroll_cache_timer) {lv_timer_delete(scroll_cache_timer);scroll_cache_timer=NULL;}
  scroll_cache_source=NULL;
}
static void scroll_cache_prepare(lv_timer_t *timer) {
  (void)timer;
  if (scroll_cache_attempted || !scroll_cache_source || modal || dragging ||
      boxing || projection_layer || lv_obj_is_scrolling(canvas)) return;
  for(lv_indev_t *in=lv_indev_get_next(NULL);in;in=lv_indev_get_next(in))
    if(lv_indev_get_state(in)==LV_INDEV_STATE_PRESSED)return;
  lv_obj_update_layout(scroll_cache_source);
  int w=lv_obj_get_width(scroll_cache_source),h=lv_obj_get_height(scroll_cache_source);
#ifdef ESP_PLATFORM
  lv_color_format_t cf=LV_COLOR_FORMAT_RGB565;
  const unsigned bytes=2;
  int64_t start=esp_timer_get_time();
#else
  lv_color_format_t cf=LV_COLOR_FORMAT_RGB888;
  const unsigned bytes=3;
#endif
  scroll_cache_attempted=true;
  if(w<=0 || h<=0 || (uint64_t)(w+16)*(h+16)*bytes>6u*1024u*1024u)return;
  scroll_cache_pixels=lv_snapshot_take(scroll_cache_source,cf);
#ifdef ESP_PLATFORM
  ESP_LOGI("scroll_cache","prepare=%s size=%dx%d time=%.2fms",scroll_cache_pixels?"ok":"failed",w,h,(esp_timer_get_time()-start)/1000.0);
#endif
}
static void scroll_cache_event(lv_event_code_t code) {
  if(code==LV_EVENT_SCROLL_END) {scroll_cache_end();return;}
  if(code!=LV_EVENT_SCROLL_BEGIN || !scroll_cache_pixels || scroll_cache_image ||
      dragging || boxing || modal || projection_layer)return;
  scroll_cache_image=lv_image_create(canvas);
  lv_image_set_src(scroll_cache_image,scroll_cache_pixels);
  lv_obj_set_pos(scroll_cache_image,lv_obj_get_x(scroll_cache_source),lv_obj_get_y(scroll_cache_source));
  lv_obj_remove_flag(scroll_cache_image,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(scroll_cache_source,LV_OBJ_FLAG_HIDDEN);
  perf_probe_cache(true);
}
static void scroll_cache_init(void) {
  scroll_cache_source=content;
  scroll_cache_timer=lv_timer_create(scroll_cache_prepare,250,NULL);
}
