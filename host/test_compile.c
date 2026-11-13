#include "compile.h"
#include <stdio.h>
static char source[16384];
static unsigned char output[32768];
int main(int argc,char **argv) {
 if(argc!=3)return 2;
 FILE *f=fopen(argv[1],"rb");if(!f)return 3;
 size_t n=fread(source,1,sizeof(source)-1,f);fclose(f);source[n]=0;
 n=board_compile(source,output,sizeof output);if(!n)return 4;
 f=fopen(argv[2],"wb");if(!f)return 5;
 fwrite(output,1,n,f);fclose(f);return 0;
}
