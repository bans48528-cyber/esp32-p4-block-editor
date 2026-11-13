#pragma once
#include "projects.h"
#define THUMB_WIDTH 182
#define THUMB_HEIGHT 108
#define THUMB_BYTES (THUMB_WIDTH * THUMB_HEIGHT * 3)
bool thumbnail_load(int slot, const Node *nodes, uint8_t *pixels);
bool thumbnail_save(int slot, const Node *nodes, const uint8_t *pixels);
