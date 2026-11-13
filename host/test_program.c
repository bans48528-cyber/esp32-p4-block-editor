#include "compile.h"
#include "program.h"
#include <stdio.h>
#include <string.h>
static char source[16384];
static unsigned char code[32768];
int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  Node nodes[LIMIT];
  FILE *f = fopen(argv[1], "rb");
  if (!f)
    return 3;
  if (fread(nodes, sizeof nodes, 1, f) != 1)
    return 4;
  fclose(f);
  if (!program_source(nodes, source, sizeof source))
    return 5;
  f = fopen(argv[2], "wb");
  if (!f)
    return 6;
  fwrite(source, 1, strlen(source), f);
  fclose(f);
  size_t n = board_compile(source, code, sizeof code);
  if (!n)
    return 7;
  f = fopen(argv[3], "wb");
  if (!f)
    return 8;
  fwrite(code, 1, n, f);
  fclose(f);
  return 0;
}
