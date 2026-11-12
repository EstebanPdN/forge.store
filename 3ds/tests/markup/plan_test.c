#include "../../source/install_plan.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static InstallPlan p;
static int find(const char*s){for(int i=0;i<p.count;i++)if(!strcmp(p.rows[i].path,s))return i;return -1;}
int main(void){
 json_t *steps=json_loads("[{\"type\":\"mkdir\",\"directory\":\"%3DSX%/game/mods\"},{\"type\":\"downloadRelease\",\"asset\":\"game\",\"output\":\"%3DSX%/old.3dsx\"},{\"type\":\"move\",\"old\":\"%3DSX%/old.3dsx\",\"new\":\"/3ds/game/game.3dsx\"},{\"type\":\"downloadUrl\",\"output\":\"/temp.zip\"},{\"type\":\"deleteFile\",\"file\":\"/temp.zip\"},{\"type\":\"downloadUrl\",\"output\":\"%TMP%/x.cia\"},{\"type\":\"installCia\"},{\"type\":\"dumpDsp\"}]",0,NULL);
 json_t *assets=json_loads("{\"game\":{\"size\":123,\"o3ds\":{\"size\":456}}}",0,NULL);
 json_t *requires=json_loads("[{\"path\":\"/3ds/game/data/\",\"example\":\"rom.bin\",\"size\":1000}]",0,NULL);
 install_plan_build(&p,steps,assets,requires,true);
 assert(p.cia&&!p.truncated&&p.count==7);assert(find("/temp.zip")==-1&&find("/3ds/old.3dsx")==-1);
 assert(p.rows[find("/3ds/game/game.3dsx")].size==456);
 assert(p.rows[find("/3ds/game/data/rom.bin")].requirement==0);
 assert(find("/3ds/game/data")<find("/3ds/game/game.3dsx"));
 assert(p.rows[0].dir&&!strcmp(p.rows[0].path,"/3ds"));
 json_decref(steps);json_decref(assets);json_decref(requires);
 steps=json_array();for(int i=0;i<400;i++){char path[80];snprintf(path,sizeof path,"/folder/%d/file",i);json_array_append_new(steps,json_pack("{s:s,s:s}","type","mkdir","directory",path));}
 install_plan_build(&p,steps,NULL,NULL,false);assert(p.truncated&&p.count==PLAN_MAX);json_decref(steps);
 puts("PASS: hierarchical plan, directories first, move/delete/temp, sizes, requirements, bounds");
}
