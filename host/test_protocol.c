#include "spark_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 uint8_t frame[262],data[255];for(int i=0;i<255;i++)data[i]=(uint8_t)i;
 SparkRx rx={0};
 for(unsigned n=0;n<=255;n++) {
  size_t size=spark_frame(frame,0xaa,data,n);assert(size==n+7);
  for(size_t i=0;i<size;i++)assert(spark_rx_byte(&rx,frame[i])==(i+1==size?1:0));
 }
 const char *json="{\"value\":{\"nested\":\"}\\\"{\"}}";
 for(size_t i=0;i<strlen(json);i++)assert(spark_rx_byte(&rx,json[i])==(i+1==strlen(json)?2:0));
 assert(rx.json_depth==0);
 size_t size=spark_frame(frame,0xbc,data,128);assert(size==135);
 for(int pass=0;pass<2;pass++)for(size_t i=0;i<size;i++)assert(spark_rx_byte(&rx,frame[i])==(i+1==size?1:0));
 frame[size-2]^=1;for(size_t i=0;i<size;i++)assert(spark_rx_byte(&rx,frame[i])==(i+1==size?-1:0));
 assert(!spark_frame(frame,0xaa,data,256));
 puts("PASS: all frame sizes, split/coalesced ACKs, JSON isolation, checksum rejection and bounds");
}
