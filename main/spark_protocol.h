#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
  uint8_t bytes[262];
  char json[4097];
  unsigned used, json_depth, json_size;
  bool quoted, escape;
} SparkRx;
size_t spark_frame(uint8_t *out, uint8_t cmd, const uint8_t *data, size_t size);
/* 1 valid binary response; 2 complete JSON in rx->json; -1 corruption/overflow.
 * JSON completion is never a binary ACK.
 */
int spark_rx_byte(SparkRx *rx, uint8_t byte);
