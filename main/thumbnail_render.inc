/* Render a clean program off-screen with the same shapes and labels as the
 *
 * editor, on both the desktop preview and the device. */
#define HOME_THUMB_CACHE_SLOTS PROJECTS
typedef struct {
  bool valid;
  uint32_t id, used;
  Node nodes[LIMIT];
  uint8_t *pixels;
} HomeThumbCache;
static HomeThumbCache home_thumb_cache[HOME_THUMB_CACHE_SLOTS];
static uint32_t home_thumb_cache_clock;
static void home_thumb_cache_clear(void) {
  for (int i = 0; i < HOME_THUMB_CACHE_SLOTS; i++)
    free(home_thumb_cache[i].pixels);
  memset(home_thumb_cache, 0, sizeof home_thumb_cache);
  home_thumb_cache_clock = 0;
}
static uint8_t *home_thumb_cache_alloc(void) {
#ifdef ESP_PLATFORM
  return heap_caps_malloc(THUMB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return malloc(THUMB_BYTES);
#endif
}
static bool home_thumb_cache_get(uint32_t id, const Node *project_nodes,
                                 uint8_t *pixels) {
  for (int i = 0; i < HOME_THUMB_CACHE_SLOTS; i++) {
    HomeThumbCache *entry = &home_thumb_cache[i];
    if (entry->valid && entry->id == id &&
        !memcmp(entry->nodes, project_nodes, sizeof(entry->nodes))) {
      memcpy(pixels, entry->pixels, THUMB_BYTES);
      entry->used = ++home_thumb_cache_clock;
      return true;
    }
  }
  return false;
}
static void home_thumb_cache_put(uint32_t id, const Node *project_nodes,
                                 const uint8_t *pixels) {
  int slot = -1;
  for (int i = 0; i < HOME_THUMB_CACHE_SLOTS; i++) {
    if (home_thumb_cache[i].valid && home_thumb_cache[i].id == id) {
      slot = i;
      break;
    }
    if (slot < 0 && !home_thumb_cache[i].valid)
      slot = i;
  }
  if (slot < 0)
    for (int i = 0; i < HOME_THUMB_CACHE_SLOTS; i++)
      if (slot < 0 || home_thumb_cache[i].used < home_thumb_cache[slot].used)
        slot = i;
  HomeThumbCache *entry = &home_thumb_cache[slot];
  if (!entry->pixels)
    entry->pixels = home_thumb_cache_alloc();
  if (!entry->pixels)
    return;
  entry->valid = true;
  entry->id = id;
  entry->used = ++home_thumb_cache_clock;
  memcpy(entry->nodes, project_nodes, sizeof(entry->nodes));
  memcpy(entry->pixels, pixels, THUMB_BYTES);
}
static bool render_thumbnail(const Project *project, uint8_t *pixels) {
#if LV_USE_SNAPSHOT
  Node old_nodes[LIMIT];
  Size old_sizes[LIMIT];
  lv_obj_t *old_cards[LIMIT], *old_cavities[LIMIT], *old_bubbles[LIMIT];
  bool old_projected[LIMIT], old_projection = projection_drawing;
  lv_obj_t *old_content = content, *old_undo = undo_btn;
  memcpy(old_nodes, nodes, sizeof nodes);
  memcpy(old_sizes, sizes, sizeof sizes);
  memcpy(old_cards, cards, sizeof cards);
  memcpy(old_cavities, cavities, sizeof cavities);
  memcpy(old_bubbles, bubbles, sizeof bubbles);
  memcpy(old_projected, projected, sizeof projected);
  lv_obj_t *scene = box(NULL, 0, 0, 800, 264, 0xeeeeee);
  lv_obj_set_style_radius(scene, 0, 0);
  lv_obj_set_style_text_font(scene, &board_font, 0);
  lv_obj_set_style_text_color(scene, lv_color_hex(0x263744), 0);
  content = scene;
  undo_btn = NULL;
  projection_drawing = true;
  memset(projected, 0, sizeof projected);
  memcpy(nodes, project->nodes, sizeof nodes);
  render();
  lv_obj_update_layout(scene);
  lv_obj_t *start = lv_obj_get_child(scene, 0);
  lv_area_t bounds;
  lv_obj_get_coords(start, &bounds);
  for (int i = 0; i < LIMIT; i++)
    if (cards[i]) {
      for (int part = 0; part < 2; part++) {
        lv_obj_t *o = part ? bubbles[i] : cards[i];
        if (!o)
          continue;
        lv_area_t a;
        lv_obj_get_coords(o, &a);
        if (!part)
          a.y2 = a.y1 + sizes[i].anchor + 35;
        bounds.x1 = LV_MIN(bounds.x1, a.x1);
        bounds.y1 = LV_MIN(bounds.y1, a.y1);
        bounds.x2 = LV_MAX(bounds.x2, a.x2);
        bounds.y2 = LV_MAX(bounds.y2, a.y2);
      }
    }
  lv_draw_buf_t *shot = lv_snapshot_take(scene, LV_COLOR_FORMAT_RGB888);
  bool ok = shot != NULL;
  if (shot) {
    int x0 = LV_MAX(0, bounds.x1 - 12), y0 = LV_MAX(0, bounds.y1 - 12);
    int sw = LV_MIN((int)shot->header.w - x0, bounds.x2 + 13 - x0);
    int sh = LV_MIN((int)shot->header.h - y0, bounds.y2 + 13 - y0);
    float scale =
        LV_MIN((float)(THUMB_WIDTH - 12) / sw, (float)(THUMB_HEIGHT - 12) / sh);
    scale = LV_MIN(scale, 1.f);
    int dw = LV_MAX(1, (int)(sw * scale)), dh = LV_MAX(1, (int)(sh * scale));
    int ox = (THUMB_WIDTH - dw) / 2, oy = (THUMB_HEIGHT - dh) / 2;
    memset(pixels, 0xee, THUMB_BYTES);
    /* Area averaging preserves narrow outlines when long programs shrink. */
    for (int y = 0; y < dh; y++)
      for (int x = 0; x < dw; x++) {
        int xa = x * sw / dw, xb = LV_MAX(xa + 1, (x + 1) * sw / dw);
        int ya = y * sh / dh, yb = LV_MAX(ya + 1, (y + 1) * sh / dh);
        unsigned sum[3] = {0}, samples = 0;
        for (int yy = ya; yy < yb; yy++)
          for (int xx = xa; xx < xb; xx++) {
            const uint8_t *src =
                shot->data + (y0 + yy) * shot->header.stride + (x0 + xx) * 3;
            for (int c = 0; c < 3; c++)
              sum[c] += src[c];
            samples++;
          }
        for (int c = 0; c < 3; c++)
          pixels[((oy + y) * THUMB_WIDTH + ox + x) * 3 + c] = sum[c] / samples;
      }
    lv_draw_buf_destroy(shot);
  }
  lv_obj_delete(scene);
  content = old_content;
  undo_btn = old_undo;
  projection_drawing = old_projection;
  memcpy(nodes, old_nodes, sizeof nodes);
  memcpy(sizes, old_sizes, sizeof sizes);
  memcpy(cards, old_cards, sizeof cards);
  memcpy(cavities, old_cavities, sizeof cavities);
  memcpy(bubbles, old_bubbles, sizeof bubbles);
  memcpy(projected, old_projected, sizeof projected);
  return ok;
#else
  return false;
#endif
}
static bool update_thumbnail(int slot) {
  uint8_t *pixels = malloc(THUMB_BYTES);
  if (!pixels)
    return false;
  bool ok =
      render_thumbnail(&library.items[slot], pixels) &&
      works_thumbnail_save(work_ids[slot], library.items[slot].nodes, pixels);
  if (ok)
    home_thumb_cache_put(work_ids[slot], library.items[slot].nodes, pixels);
  free(pixels);
  return ok;
}
static void thumbnail_destroy(lv_event_t *e) {
  lv_image_dsc_t *d = lv_event_get_user_data(e);
  lv_image_cache_drop(d);
  free((void *)d->data);
  free(d);
}
static bool show_thumbnail(lv_obj_t *card, int slot) {
  lv_image_dsc_t *d = calloc(1, sizeof(*d));
  if (!d)
    return false;
  uint8_t *pixels = malloc(THUMB_BYTES);
  if (!pixels) {
    free(d);
    return false;
  }
  if (!home_thumb_cache_get(work_ids[slot], library.items[slot].nodes,
                            pixels) &&
      !works_thumbnail_load(work_ids[slot], library.items[slot].nodes,
                            pixels)) {
    if (!render_thumbnail(&library.items[slot], pixels)) {
      free(pixels);
      free(d);
      return false;
    }
    works_thumbnail_save(work_ids[slot], library.items[slot].nodes, pixels);
  }
  home_thumb_cache_put(work_ids[slot], library.items[slot].nodes, pixels);
  d->header.magic = LV_IMAGE_HEADER_MAGIC;
  d->header.cf = LV_COLOR_FORMAT_RGB888;
  d->header.w = THUMB_WIDTH;
  d->header.h = THUMB_HEIGHT;
  d->header.stride = THUMB_WIDTH * 3;
  d->data_size = THUMB_BYTES;
  d->data = pixels;
  lv_obj_t *image = lv_image_create(card);
  lv_image_set_src(image, d);
  lv_obj_set_pos(image, 8, 8);
  lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(image, thumbnail_destroy, LV_EVENT_DELETE, d);
  return true;
}
