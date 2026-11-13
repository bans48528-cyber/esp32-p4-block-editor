/* Bounded, scene-coordinate tile cache. LVGL 9.2.2 regional drawing follows
 * lv_snapshot.c, but allocates only a tile, never a full program bitmap. */
#include "src/core/lv_refr_private.h"
#include "src/core/lv_obj_event_private.h"
#include "src/display/lv_display_private.h"
#include "src/draw/lv_draw_private.h"
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
#define TILE_CF LV_COLOR_FORMAT_RGB565
#define TILE_BUDGET (3u * 1024u * 1024u)
#else
#define TILE_CF LV_COLOR_FORMAT_RGB888
#define TILE_BUDGET (4u * 1024u * 1024u)
#endif
#define TILE_W 200
#define TILE_H 160
#define TILE_COUNT 40
static struct {
  lv_draw_buf_t *pixels;
  int x, y;
  bool assigned, valid;
} tiles[TILE_COUNT];
static lv_timer_t *scroll_cache_timer;
static lv_obj_t *scroll_cache_source, *scroll_cache_image;
static Node cached_nodes[LIMIT];
static lv_area_t cached_bounds[LIMIT], cached_start;
static bool cached_present[LIMIT], cached_scene;
static unsigned cache_bytes, cache_rendered, cache_invalidated, cache_hits,
    cache_misses;
static uint32_t cache_retry_at;

void editor_cache_stats(EditorCacheStats *s) {
  memset(s, 0, sizeof(*s));
  s->bytes = cache_bytes;
  s->budget = TILE_BUDGET;
  s->rendered = cache_rendered;
  s->invalidated = cache_invalidated;
  s->hits = cache_hits;
  s->misses = cache_misses;
  s->active = scroll_cache_image != NULL;
  for (int i = 0; i < TILE_COUNT; i++)
    s->tiles += tiles[i].pixels != NULL;
}
static bool cache_intersect(lv_area_t *out, const lv_area_t *a, const lv_area_t *b) {
  *out=(lv_area_t){LV_MAX(a->x1,b->x1),LV_MAX(a->y1,b->y1),LV_MIN(a->x2,b->x2),LV_MIN(a->y2,b->y2)};
  return out->x1<=out->x2 && out->y1<=out->y2;
}
static lv_area_t tile_area(int x, int y) {
  return (lv_area_t){x * TILE_W, y * TILE_H, (x + 1) * TILE_W - 1,
                     (y + 1) * TILE_H - 1};
}
static bool area_equal(lv_area_t a, lv_area_t b) {
  return a.x1 == b.x1 && a.x2 == b.x2 && a.y1 == b.y1 && a.y2 == b.y2;
}
static void scroll_cache_end(void) {
  if (!scroll_cache_image)
    return;
  lv_obj_remove_flag(scroll_cache_source, LV_OBJ_FLAG_HIDDEN);
  lv_obj_delete(scroll_cache_image);
  scroll_cache_image = NULL;
  perf_probe_cache(false);
}
static void scroll_cache_invalidate(void) {
  /* Called before replacing the live object tree. Preserve tiles until the
   * new geometry is available, then invalidate only old/new damage bounds. */
  scroll_cache_end();
}
static void scroll_cache_dispose(void) {
  scroll_cache_end();
  if (scroll_cache_timer)
    lv_timer_delete(scroll_cache_timer);
  scroll_cache_timer = NULL;
  scroll_cache_source = NULL;
  for (int i = 0; i < TILE_COUNT; i++)
    if (tiles[i].pixels) {
      lv_image_cache_drop(tiles[i].pixels);
      lv_draw_buf_destroy(tiles[i].pixels);
    }
  memset(tiles, 0, sizeof tiles);
  cached_scene = false;
  cache_bytes = cache_rendered = cache_invalidated = cache_hits = cache_misses =
      0;
  cache_retry_at = 0;
}
static void cache_damage(lv_area_t area) {
  lv_area_increase(&area, 4, 4);
  for (int i = 0; i < TILE_COUNT; i++)
    if (tiles[i].assigned && tiles[i].valid) {
      lv_area_t a = tile_area(tiles[i].x, tiles[i].y), intersection;
      if (cache_intersect(&intersection, &a, &area)) {
        tiles[i].valid = false;
        cache_invalidated++;
      }
    }
}
static lv_area_t cache_object_bounds(lv_obj_t *obj, lv_area_t origin) {
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  lv_area_move(&a, -origin.x1, -origin.y1);
  return a;
}
static void scroll_cache_scene_changed(void) {
  if (!scroll_cache_source || projection_drawing)
    return;
  lv_obj_update_layout(content);
  lv_area_t origin;
  lv_obj_get_coords(content, &origin);
  lv_area_t start = cache_object_bounds(lv_obj_get_child(content, 0), origin);
  if (!cached_scene || !area_equal(start, cached_start) ||
      model_count(cached_nodes, -1) != count(-1)) {
    cache_damage(cached_start);
    cache_damage(start);
    /* Empty-program help text is not associated with a node. */
    if (!cached_scene || !model_count(cached_nodes, -1) || !count(-1))
      cache_damage((lv_area_t){100, 80, 799, 160});
  }
  cached_start = start;
  for (int i = 0; i < LIMIT; i++) {
    bool present = cards[i] != NULL;
    lv_area_t a = {0};
    if (present) {
      a = cache_object_bounds(cards[i], origin);
      if (bubbles[i]) {
        lv_area_t b = cache_object_bounds(bubbles[i], origin);
        a.x1 = LV_MIN(a.x1, b.x1);
        a.y1 = LV_MIN(a.y1, b.y1);
        a.x2 = LV_MAX(a.x2, b.x2);
        a.y2 = LV_MAX(a.y2, b.y2);
      }
    }
    if (!cached_scene || present != cached_present[i] ||
        memcmp(&nodes[i], &cached_nodes[i], sizeof(Node)) ||
        (present && !area_equal(a, cached_bounds[i]))) {
      if (cached_present[i])
        cache_damage(cached_bounds[i]);
      if (present)
        cache_damage(a);
    }
    cached_bounds[i] = a;
    cached_present[i] = present;
  }
  memcpy(cached_nodes, nodes, sizeof nodes);
  cached_scene = true;
}
static void cache_window(lv_area_t *visible, lv_area_t *wanted) {
  lv_area_t v, origin;
  lv_obj_get_coords(canvas, &v);
  lv_obj_get_coords(scroll_cache_source, &origin);
  lv_area_move(&v, -origin.x1, -origin.y1);
  int w = lv_obj_get_width(scroll_cache_source),
      h = lv_obj_get_height(scroll_cache_source);
  v.x1 = LV_MAX(0, v.x1);
  v.y1 = LV_MAX(0, v.y1);
  v.x2 = LV_MIN(w - 1, v.x2);
  v.y2 = LV_MIN(h - 1, v.y2);
  *visible = v;
  *wanted = v;
  wanted->x1 = LV_MAX(0, v.x1 - 800);
  wanted->x2 = LV_MIN(w - 1, v.x2 + 800);
}
static bool cache_in_window(int x, int y, lv_area_t wanted) {
  lv_area_t a = tile_area(x, y), intersection;
  return cache_intersect(&intersection, &a, &wanted);
}
static int cache_find(int x, int y) {
  for (int i = 0; i < TILE_COUNT; i++)
    if (tiles[i].assigned && tiles[i].x == x && tiles[i].y == y)
      return i;
  return -1;
}
static int cache_slot(int x, int y, lv_area_t wanted) {
  int found = cache_find(x, y);
  if (found >= 0)
    return found;
  int slot = -1;
  for (int i = 0; i < TILE_COUNT; i++)
    if (!tiles[i].assigned ||
        !cache_in_window(tiles[i].x, tiles[i].y, wanted)) {
      slot = i;
      break;
    }
  if (slot < 0)
    return -1;
  if (!tiles[slot].pixels) {
    unsigned estimate =
        lv_draw_buf_width_to_stride(TILE_W, TILE_CF) * TILE_H + 128;
    if (cache_bytes + estimate > TILE_BUDGET)
      return -1;
    tiles[slot].pixels =
        lv_draw_buf_create(TILE_W, TILE_H, TILE_CF, LV_STRIDE_AUTO);
    if (!tiles[slot].pixels)
      return -1;
    cache_bytes += tiles[slot].pixels->data_size + sizeof(lv_draw_buf_t);
  }
  tiles[slot].x = x;
  tiles[slot].y = y;
  tiles[slot].assigned = true;
  tiles[slot].valid = false;
  return slot;
}
static void cache_draw_tile(int index) {
  lv_draw_buf_t *buf = tiles[index].pixels;
  lv_image_cache_drop(buf);
  lv_area_t origin;
  lv_obj_get_coords(scroll_cache_source, &origin);
  lv_area_t a = tile_area(tiles[index].x, tiles[index].y);
  lv_area_move(&a, origin.x1, origin.y1);
  lv_layer_t layer = {0};
  layer.draw_buf = buf;
  layer.buf_area = a;
  layer._clip_area = a;
  layer.phy_clip_area = a;
  layer.color_format = TILE_CF;
#if LV_DRAW_TRANSFORM_USE_MATRIX
  lv_matrix_identity(&layer.matrix);
#endif
  lv_display_t *display = lv_obj_get_display(scroll_cache_source);
  lv_display_t *previous = lv_refr_get_disp_refreshing();
  lv_layer_t *head = display->layer_head;
  display->layer_head = &layer;
  lv_refr_set_disp_refreshing(display);
  /* Edge tiles extend past the scene: initialize the same opaque background. */
  lv_draw_rect_dsc_t bg;
  lv_draw_rect_dsc_init(&bg);
  bg.bg_color = lv_color_hex(0xeeeeee);
  bg.bg_opa = LV_OPA_COVER;
  lv_draw_rect(&layer, &bg, &a);
  /* Selection is dynamic. It never becomes part of a cached tile. */
  if (selection_background)
    lv_obj_add_flag(selection_background, LV_OBJ_FLAG_HIDDEN);
  if (selection_outline)
    lv_obj_add_flag(selection_outline, LV_OBJ_FLAG_HIDDEN);
  lv_obj_redraw(&layer, scroll_cache_source);
  while (layer.draw_task_head) {
    lv_draw_dispatch_wait_for_request();
    lv_draw_dispatch();
  }
  if (selection_background)
    lv_obj_remove_flag(selection_background, LV_OBJ_FLAG_HIDDEN);
  if (selection_outline)
    lv_obj_remove_flag(selection_outline, LV_OBJ_FLAG_HIDDEN);
  display->layer_head = head;
  lv_refr_set_disp_refreshing(previous);
  tiles[index].valid = true;
  cache_rendered++;
}
static void cache_paint(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_COVER_CHECK) {
    /* Every tile (including a live fallback) paints opaque pixels. Avoid an
     * additional full-viewport background fill underneath those pixels. */
    lv_cover_check_info_t *info = lv_event_get_param(event);
    info->res = LV_COVER_RES_COVER;
    return;
  }
  if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN)
    return;
  lv_layer_t *layer = lv_event_get_layer(event);
  lv_area_t clip = layer->_clip_area, origin;
  lv_obj_get_coords(scroll_cache_source, &origin);
  lv_area_t relative = clip;
  lv_area_move(&relative, -origin.x1, -origin.y1);
  int x1 = LV_MAX(0, relative.x1) / TILE_W,
      y1 = LV_MAX(0, relative.y1) / TILE_H;
  int x2 = LV_MAX(0, relative.x2) / TILE_W,
      y2 = LV_MAX(0, relative.y2) / TILE_H;
  for (int y = y1; y <= y2; y++)
    for (int x = x1; x <= x2; x++) {
      lv_area_t a = tile_area(x, y), part;
      lv_area_move(&a, origin.x1, origin.y1);
      if (!cache_intersect(&part, &a, &clip))
        continue;
      int i = cache_find(x, y);
      layer->_clip_area = part;
      if (i >= 0 && tiles[i].valid) {
        lv_draw_image_dsc_t dsc;
        lv_draw_image_dsc_init(&dsc);
        dsc.src = tiles[i].pixels;
        lv_draw_image(layer, &dsc, &a);
        cache_hits++;
      } else {
        lv_obj_redraw(layer, scroll_cache_source);
        cache_misses++;
      }
    }
  /* Repaint only the selected rectangle using the live tree. This keeps the
   * blue fill behind the blocks, and the common border above them, without
   * alpha-blending every cached pixel or baking selection into the cache. */
  if (selection_outline) {
    lv_area_t a, part;
    lv_obj_get_coords(selection_outline, &a);
    lv_area_increase(&a, 2, 2);
    if (cache_intersect(&part, &a, &clip)) {
      layer->_clip_area = part;
      lv_obj_redraw(layer, scroll_cache_source);
    }
  }
  layer->_clip_area = clip;
}
static void scroll_cache_prepare(lv_timer_t *timer) {
  (void)timer;
  if (!scroll_cache_source || modal || dragging || boxing || projection_layer)
    return;
  if (cache_retry_at && lv_tick_elaps(cache_retry_at) < 1000)
    return;
  lv_obj_update_layout(scroll_cache_source);
  lv_area_t visible, wanted;
  cache_window(&visible, &wanted);
  uint32_t begin = lv_tick_get();
  unsigned limit = scroll_cache_image ? 1 : 4;
  for (unsigned n = 0; n < limit; n++) {
    int bx = -1, by = -1, best = INT32_MAX;
    for (int y = wanted.y1 / TILE_H; y <= wanted.y2 / TILE_H; y++)
      for (int x = wanted.x1 / TILE_W; x <= wanted.x2 / TILE_W; x++) {
        int i = cache_find(x, y);
        if (i >= 0 && tiles[i].valid)
          continue;
        lv_area_t a = tile_area(x, y), intersection;
        int score = cache_intersect(&intersection, &a, &visible)
                        ? 0
                        : LV_MAX(visible.x1 - a.x2, a.x1 - visible.x2) + 800;
        if (score < best) {
          best = score;
          bx = x;
          by = y;
        }
      }
    if (bx < 0)
      break;
    int slot = cache_slot(bx, by, wanted);
    if (slot < 0) {
      cache_retry_at = lv_tick_get();
      break;
    }
    cache_retry_at = 0;
    cache_draw_tile(slot);
    if (lv_tick_elaps(begin) >= 4)
      break;
  }
#ifdef ESP_PLATFORM
  static uint32_t logged;
  if (lv_tick_elaps(logged) >= 2000 && scroll_cache_image) {
    ESP_LOGI("scroll_tiles", "bytes=%u renders=%u dirty=%u hits=%u misses=%u",
             cache_bytes, cache_rendered, cache_invalidated, cache_hits,
             cache_misses);
    logged = lv_tick_get();
  }
#endif
}
static void scroll_cache_event(lv_event_code_t code) {
  if (code == LV_EVENT_SCROLL_END) {
    scroll_cache_end();
    return;
  }
  if (code != LV_EVENT_SCROLL_BEGIN || !scroll_cache_source ||
      scroll_cache_image || dragging || boxing || modal || projection_layer)
    return;
  scroll_cache_image = decoration(
      canvas, lv_obj_get_x(content), lv_obj_get_y(content),
      lv_obj_get_width(content), lv_obj_get_height(content), 0xeeeeee);
  lv_obj_set_style_radius(scroll_cache_image, 0, 0);
  lv_obj_set_style_bg_opa(scroll_cache_image, LV_OPA_TRANSP, 0);
  lv_obj_add_event_cb(scroll_cache_image, cache_paint, LV_EVENT_ALL, NULL);
  lv_obj_add_flag(scroll_cache_source, LV_OBJ_FLAG_HIDDEN);
  perf_probe_cache(true);
}
static void scroll_cache_init(void) {
  scroll_cache_source = content;
  scroll_cache_scene_changed();
  scroll_cache_timer = lv_timer_create(scroll_cache_prepare, 16, NULL);
}
