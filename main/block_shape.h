#pragma once
#include "lvgl.h"
/* The web block-layout.js outline, rasterized natively with 2x coverage. */
lv_obj_t *block_shape(lv_obj_t *parent, int width, int anchor, bool loop,
                      bool start, uint32_t color);
