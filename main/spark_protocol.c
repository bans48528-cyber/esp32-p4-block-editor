#include "spark_protocol.h"
#include <string.h>
size_t spark_frame(uint8_t *out, uint8_t cmd, const uint8_t *data, size_t n) {
  if (n > 255)
    return 0;
  out[0] = 0x5a;
  out[1] = 0x97;
  out[2] = 0x98;
  out[3] = (uint8_t)n;
  out[4] = cmd;
  memcpy(out + 5, data, n);
  uint8_t sum = 0;
  for (size_t i = 0; i < n + 5; i++)
    sum += out[i];
  out[n + 5] = sum;
  out[n + 6] = 0xa5;
  return n + 7;
}
int spark_rx_byte(SparkRx *r, uint8_t b) {
  if (r->json_depth) {
    if (++r->json_size > 65536) {
      memset(r, 0, sizeof *r);
      return -1;
    }
    if(r->json_size < sizeof r->json) r->json[r->json_size-1]=(char)b;
    if (r->escape)
      r->escape = false;
    else if (r->quoted && b == '\\')
      r->escape = true;
    else if (b == '"')
      r->quoted = !r->quoted;
    else if (!r->quoted) {
      if (b == '{')
        r->json_depth++;
      if (b == '}')
        r->json_depth--;
    }
    if(!r->json_depth) {
      if(r->json_size >= sizeof r->json)return -1;
      r->json[r->json_size]=0;return 2;
    }
    return 0;
  }
  if (!r->used) {
    if (b == '{') {
      r->json_depth = 1;
      r->json_size = 1;
      r->json[0] = '{';
      r->quoted = r->escape = false;
      return 0;
    }
    if (b != 0x5a)
      return 0;
  }
  r->bytes[r->used++] = b;
  if ((r->used == 2 || r->used == 3) && b != 0x97 && b != 0x98) {
    r->used = 0;
    return -1;
  }
  if (r->used >= 4 && r->used == (unsigned)r->bytes[3] + 7) {
    uint8_t sum = 0;
    for (unsigned i = 0; i < r->used - 2; i++)
      sum += r->bytes[i];
    bool ok = b == 0xa5 && sum == r->bytes[r->used - 2];
    r->used = 0;
    return ok ? 1 : -1;
  }
  return 0;
}
