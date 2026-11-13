#include "compile.h"
#include "PikaParser.h"
#include "PikaVM.h"
static void u32(uint8_t *p, uint32_t n) {
  p[0] = n;
  p[1] = n >> 8;
  p[2] = n >> 16;
  p[3] = n >> 24;
}
size_t board_compile(char *source, uint8_t *out, size_t capacity) {
  ByteCodeFrame frame;
  byteCodeFrame_init(&frame);
  size_t result = 0;
  if (pika_lines2Bytes(&frame, source) == PIKA_RES_OK) {
    size_t ins = frame.instruct_array.size, pool = frame.const_pool.size;
    if (ins + pool + 16 <= capacity) {
      memcpy(out,
             "\x0f"
             "pyo",
             4);
      u32(out + 4, (uint32_t)(ins + pool + 8));
      u32(out + 8, (uint32_t)ins);
      memcpy(out + 12, instructArray_getStart(&frame.instruct_array), ins);
      u32(out + 12 + ins, (uint32_t)pool);
      memcpy(out + 16 + ins, constPool_getStart(&frame.const_pool), pool);
      result = ins + pool + 16;
    }
  }
  byteCodeFrame_deinit(&frame);
  return result;
}
