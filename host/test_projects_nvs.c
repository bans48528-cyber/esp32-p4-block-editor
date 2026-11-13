#include "projects.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {uint32_t magic,version;struct {uint32_t used;char name[64];Node nodes[32];} items[PROJECTS];} legacy;
static Library saved,pending;
static int available=1,exists,fail;
int nvs_flash_init(void){return ESP_OK;}
int nvs_flash_init_partition(const char *p){assert(!strcmp(p,"projects"));return available?ESP_OK:ESP_ERR_NOT_FOUND;}
int nvs_open(const char *ns,int mode,nvs_handle_t *h){assert(!strcmp(ns,"card_projects") && mode==NVS_READONLY);*h=1;return 0;}
int nvs_open_from_partition(const char *p,const char *ns,int mode,nvs_handle_t *h){assert(!strcmp(p,"projects") && !strcmp(ns,"card_projects"));*h=2;return mode==NVS_READONLY&&!exists?ESP_ERR_NVS_NOT_FOUND:0;}
int nvs_get_blob(nvs_handle_t h,const char *key,void *data,size_t *size){
 const void *src;size_t n;
 if(h==1){if(strcmp(key,"library_v2"))return ESP_ERR_NVS_NOT_FOUND;src=&legacy;n=sizeof legacy;}
 else{assert(!strcmp(key,"library_v3"));if(!exists)return ESP_ERR_NVS_NOT_FOUND;src=&saved;n=sizeof saved;}
 if(data){assert(*size>=n);memcpy(data,src,n);}*size=n;return 0;
}
int nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size){assert(h==2 && !strcmp(key,"library_v3") && size==sizeof pending);pending=*(const Library*)data;return 0;}
int nvs_commit(nvs_handle_t h){assert(h==2);if(fail)return 9;saved=pending;exists=1;return 0;}
void nvs_close(nvs_handle_t h){(void)h;}
int main(void){
 Library l,loaded;projects_empty(&l);legacy.magic=l.magic;legacy.version=2;
 legacy.items[0].used=1;strcpy(legacy.items[0].name,"legacy");legacy.items[0].nodes[0]=node_default(WAIT);
 assert(projects_load(&l)==1 && l.version==3 && l.items[0].nodes[0].type==WAIT);
 for(int p=0;p<PROJECTS;p++) {l.items[p].used=1;strcpy(l.items[p].name,"100 blocks");for(int i=0;i<LIMIT;i++){l.items[p].nodes[i]=node_default(COMBO_DIRECTION);l.items[p].nodes[i].order=i;l.items[p].nodes[i].option=i%4;}}
 assert(projects_save(&l));assert(projects_load(&loaded)==1 && !memcmp(&l,&loaded,sizeof l));
 assert(legacy.version==2 && legacy.items[0].nodes[0].type==WAIT);
 fail=1;l.items[0].nodes[0].option=2;assert(!projects_save(&l));assert(projects_load(&loaded)==1 && loaded.items[0].nodes[0].option==0);
 saved.version=999;assert(projects_load(&loaded)==-1);
 available=0;assert(projects_load(&loaded)==1 && loaded.items[0].nodes[0].type==WAIT);assert(!projects_save(&loaded));
 puts("PASS: separate NVS partition, V2 fallback, six 100-block projects, failed commit, corrupt V3 rejection, missing partition");
}
