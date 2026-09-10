#include "block_shape.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
  float x, y;
} Point;
typedef struct {
  Point p[320];
  int n;
} Path;
static void line(Path *p, float x, float y) {
  if (p->n < 320)
    p->p[p->n++] = (Point){x, y};
}
static void curve(Path *p, float x1, float y1, float x2, float y2, float x,
                  float y) {
  Point a = p->p[p->n - 1];
  for (int i = 1; i <= 12; i++) {
    float t = i / 12.f, u = 1 - t;
    line(p,
         u * u * u * a.x + 3 * u * u * t * x1 + 3 * u * t * t * x2 +
             t * t * t * x,
         u * u * u * a.y + 3 * u * u * t * y1 + 3 * u * t * t * y2 +
             t * t * t * y);
  }
}
static void quad(Path *p, float x1, float y1, float x, float y) {
  Point a = p->p[p->n - 1];
  curve(p, a.x + 2 * (x1 - a.x) / 3, a.y + 2 * (y1 - a.y) / 3,
        x + 2 * (x1 - x) / 3, y + 2 * (y1 - y) / 3, x, y);
}
static void edge(Path *p, float x, float y, bool down) {
  if (down) {
    line(p, x, y - 17);
    curve(p, x, y - 12, x + 8, y - 11, x + 8, y - 6);
    line(p, x + 8, y + 6);
    curve(p, x + 8, y + 11, x, y + 12, x, y + 17);
  } else {
    line(p, x, y + 17);
    curve(p, x, y + 12, x + 8, y + 11, x + 8, y + 6);
    line(p, x + 8, y - 6);
    curve(p, x + 8, y - 11, x, y - 12, x, y - 17);
  }
}
/* Bounded shared raster cache. Live images are never evicted. All access is
 * on the LVGL task, including the delete callback. */
#define SHAPE_CACHE_COUNT 64
#define SHAPE_CACHE_BYTES (2u * 1024u * 1024u)
typedef struct {
  lv_image_dsc_t *image;
  int width, anchor;
  bool loop, start;
  uint32_t color, refs, used;
} ShapeCache;
static ShapeCache cache[SHAPE_CACHE_COUNT];
static uint32_t cache_clock;
static size_t cache_bytes;
static void destroy(lv_event_t *e) {
  lv_image_dsc_t *d = lv_event_get_user_data(e);
  for (int i = 0; i < SHAPE_CACHE_COUNT; i++) {
    if (cache[i].image == d) {
      cache[i].refs--;
      return;
    }
  }
  lv_image_cache_drop(d);
  free((void *)d->data);
  free(d);
}
static lv_obj_t *attach(lv_obj_t *parent, lv_image_dsc_t *d) {
  lv_obj_t *o = lv_image_create(parent);
  lv_image_set_src(o, d);
  lv_obj_set_pos(o, -1, -1);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(o, destroy, LV_EVENT_DELETE, d);
  return o;
}
static void retain(lv_image_dsc_t *d, int w, int anchor, bool loop, bool start,
                   uint32_t color) {
  if (d->data_size > SHAPE_CACHE_BYTES)
    return;
  int slot = -1;
  for (;;) {
    for (int i = 0; i < SHAPE_CACHE_COUNT; i++)
      if (!cache[i].image) {
        slot = i;
        break;
      }
    if (slot >= 0 && cache_bytes + d->data_size <= SHAPE_CACHE_BYTES)
      break;
    int oldest = -1;
    for (int i = 0; i < SHAPE_CACHE_COUNT; i++)
      if (cache[i].image && !cache[i].refs &&
          (oldest < 0 || cache[i].used < cache[oldest].used))
        oldest = i;
    if (oldest < 0)
      return; /* All remaining rasters are in use. */
    lv_image_dsc_t *old = cache[oldest].image;
    cache_bytes -= old->data_size;
    lv_image_cache_drop(old);
    free((void *)old->data);
    free(old);
    memset(&cache[oldest], 0, sizeof cache[oldest]);
  }
  cache[slot] =
      (ShapeCache){d, w, anchor, loop, start, color, 1, ++cache_clock};
  cache_bytes += d->data_size;
}
lv_obj_t *block_shape(lv_obj_t *parent, int w, int anchor, bool loop,
                      bool start, uint32_t color) {
  for (int i = 0; i < SHAPE_CACHE_COUNT; i++) {
    ShapeCache *c = &cache[i];
    if (c->image && c->width == w && c->anchor == anchor && c->loop == loop &&
        c->start == start && c->color == color) {
      c->refs++;
      c->used = ++cache_clock;
      return attach(parent, c->image);
    }
  }
  int h = anchor + 36, iw = w + 10, ih = h + 2;
  Path path = {0};
  float right = w - 60;
  line(&path, start ? 36 : 5, 0);
  line(&path, w - 5, 0);
  quad(&path, w, 0, w, 5);
  edge(&path, (float)w, (float)anchor, true);
  line(&path, w, h - 5);
  quad(&path, w, h, w - 5, h);
  if (loop) {
    line(&path, right + 5, h);
    quad(&path, right, h, right, h - 5);
    edge(&path, right, (float)anchor, false);
    line(&path, right, 19);
    quad(&path, right, 14, right - 5, 14);
    line(&path, 31, 14);
    quad(&path, 26, 14, 26, 19);
    edge(&path, 26, (float)anchor, true);
    line(&path, 26, h - 5);
    quad(&path, 26, h, 21, h);
  }
  if (start) {
    line(&path, 36, 72);
    for (int i = 1; i <= 36; i++) {
      float a = 1.5707963f + i * 3.14159265f / 36;
      line(&path, 36 + 36 * cosf(a), 36 + 36 * sinf(a));
    }
  } else {
    line(&path, 5, h);
    quad(&path, 0, h, 0, h - 5);
    edge(&path, 0, (float)anchor, false);
    line(&path, 0, 5);
    quad(&path, 0, 0, 5, 0);
  }
  line(&path, path.p[0].x, path.p[0].y);
  lv_image_dsc_t *d = calloc(1, sizeof(*d));
  if (!d)
    return NULL;
  uint32_t *pixels = calloc((size_t)iw * ih, 4);
  if (!pixels) {
    free(d);
    return NULL;
  }
  /* Two vertical samples; exact horizontal interval coverage for smooth edges.
   */
  for (int y = 0; y < ih; y++)
    for (int sy = 0; sy < 2; sy++) {
      float scan = y - 1 + (sy + .5f) / 2, intersections[320];
      int n = 0;
      for (int i = 1; i < path.n; i++) {
        Point a = path.p[i - 1], b = path.p[i];
        if ((a.y <= scan && b.y > scan) || (b.y <= scan && a.y > scan))
          intersections[n++] =
              a.x + (scan - a.y) * (b.x - a.x) / (b.y - a.y) + 1;
      }
      for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
          if (intersections[j] < intersections[i]) {
            float t = intersections[i];
            intersections[i] = intersections[j];
            intersections[j] = t;
          }
      for (int i = 0; i + 1 < n; i += 2) {
        float a = intersections[i], b = intersections[i + 1];
        int left = (int)floorf(a), right_edge = (int)ceilf(b);
        for (int x = left; x < right_edge; x++)
          if (x >= 0 && x < iw) {
            float lo = a > x ? a : x, hi = b < x + 1 ? b : x + 1;
            unsigned coverage = (unsigned)((hi - lo) * 127.5f + .5f);
            uint32_t *pixel = &pixels[y * iw + x];
            unsigned alpha = (*pixel >> 24) + coverage;
            if (alpha > 255)
              alpha = 255;
            *pixel = (alpha << 24) | color;
          }
      }
    }
  /* Match blocks.css: 1.2 px centered SVG stroke on the shared path.
   * Shared connectors coincide geometrically, so no edge deletion is needed. */
  uint32_t edge_color = start                       ? 0xb09400
                        : loop || color == 0xffb515 ? 0xc98a01
                        : color == 0xff4ccd         ? 0xb2248a
                        : color == 0x34ccf1         ? 0x368fa9
                        : color == 0x9b6af6         ? 0x571cc1
                        : color == 0xc061f1         ? 0x642e7f
                                                    : 0x0062a6;
  uint8_t *fill = malloc((size_t)iw * ih);
  if (!fill) {
    free(pixels);
    free(d);
    return NULL;
  }
  for (int i = 0; i < iw * ih; i++)
    fill[i] = (uint8_t)(pixels[i] >> 24);
  for (int y = 0; y < ih; y++)
    for (int x = 0; x < iw; x++) {
      unsigned original = fill[y * iw + x];
      bool boundary = original > 0 && original < 255;
      for (int dy = -2; dy <= 2 && !boundary; dy += 2)
        for (int dx = -2; dx <= 2; dx += 2) {
          int sx = x + dx, sy = y + dy;
          unsigned sample =
              sx >= 0 && sx < iw && sy >= 0 && sy < ih ? fill[sy * iw + sx] : 0;
          if (sample != original) {
            boundary = true;
            break;
          }
        }
      if (!boundary)
        continue;
      float qx = x - .5f, qy = y - .5f, min_squared = 1.21f;
      for (int k = 1; k < path.n; k++) {
        Point a = path.p[k - 1], b = path.p[k];
        if (qx < fminf(a.x, b.x) - 1.1f || qx > fmaxf(a.x, b.x) + 1.1f ||
            qy < fminf(a.y, b.y) - 1.1f || qy > fmaxf(a.y, b.y) + 1.1f)
          continue;
        float dx = b.x - a.x, dy = b.y - a.y, length = dx * dx + dy * dy;
        float t = length > 0 ? ((qx - a.x) * dx + (qy - a.y) * dy) / length : 0;
        if (t < 0)
          t = 0;
        if (t > 1)
          t = 1;
        float ex = qx - a.x - t * dx, ey = qy - a.y - t * dy,
              distance = ex * ex + ey * ey;
        if (distance < min_squared)
          min_squared = distance;
      }
      float stroke = 1.1f - sqrtf(min_squared);
      if (stroke < 0)
        stroke = 0;
      if (stroke > 1)
        stroke = 1;
      float under = original / 255.f * (1 - stroke), alpha = stroke + under;
      if (alpha <= 0)
        continue;
      uint32_t result = (uint32_t)(alpha * 255 + .5f) << 24;
      for (int shift = 0; shift <= 16; shift += 8) {
        float c = (((edge_color >> shift) & 255) * stroke +
                   ((color >> shift) & 255) * under) /
                  alpha;
        result |= (uint32_t)(c + .5f) << shift;
      }
      pixels[y * iw + x] = result;
    }
  free(fill);
  d->header.magic = LV_IMAGE_HEADER_MAGIC;
  d->header.cf = LV_COLOR_FORMAT_ARGB8888;
  d->header.w = iw;
  d->header.h = ih;
  d->header.stride = iw * 4;
  d->data_size = iw * ih * 4;
  d->data = (uint8_t *)pixels;
  retain(d, w, anchor, loop, start, color);
  return attach(parent, d);
}
