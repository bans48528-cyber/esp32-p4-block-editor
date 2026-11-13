#pragma once
#include <stddef.h>
#include <stdint.h>
/* Serialized use only. Caller owns output. Returns 0 on compile failure. */
size_t board_compile(char *source, uint8_t *output, size_t capacity);
