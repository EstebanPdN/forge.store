#include "../source/sd_usage.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static void file(const char *path,int bytes){FILE *f=fopen(path,"wb");assert(f);for(int i=0;i<bytes;i++)assert(fputc(0,f)==0);assert(!fclose(f));}
int main(void){
    char temp[]="/tmp/forge-sd-usage-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));assert(!mkdir("store",0700));assert(!mkdir("game",0700));assert(!mkdir("user",0700));file("store/cache",31);file("game/app.3dsx",73);file("user/data",101);file("outside",999);assert(!symlink("../outside","store/link"));
    json_t *installed=json_pack("{s:{s:[s,s]},s:{s:[s,s]}}","a","files","game/app.3dsx","store/cache","b","files","game/app.3dsx","title:0x0004000000000000");json_t *paths=json_pack("[s,s]","user","user/data");SdUsage u=sd_usage_measure("store",installed,paths);assert(u.complete&&u.store==104&&u.user==101);
    bool complete=false;assert(sd_usage_path("store",256,&complete)==31&&complete);assert(sd_usage_path("missing",256,&complete)==0&&complete);sd_usage_path("store",1,&complete);assert(!complete);assert(!mkdir("store/deep",0700));char p[256]="store/deep";for(int i=0;i<10;i++){strcat(p,"/a");assert(!mkdir(p,0700));}sd_usage_path("store",256,&complete);assert(!complete);
    json_decref(paths);json_decref(installed);puts("PASS: actual file sizes, store/user separation, deduplication, symlink isolation, missing files, depth and IO budgets");
}
