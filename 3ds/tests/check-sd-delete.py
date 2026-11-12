#!/usr/bin/env python3
"""Exercise the production SD deletion walker only in a temporary fake sdmc:."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'3ds/source/panels.inc').read_text();code=s[s.index('static bool sd_protected('):s.index('static void sd_read(')]
with tempfile.TemporaryDirectory(prefix='forge-sd-delete-') as temp:
 p=Path(temp);src=p/'test.c';exe=p/'test'
 src.write_text('''#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <assert.h>
#define DATA_DIR "sdmc:/3ds/forge-store"
'''+code+'''
int main(void){
 assert(sd_protected("sdmc:/"));assert(sd_protected("sdmc:/3ds"));assert(sd_protected(DATA_DIR));assert(sd_protected("sdmc:/luma/config.ini"));
 assert(!sd_protected("sdmc:/luma-other"));assert(!sd_protected("sdmc:/3ds/game"));
 mkdir("sdmc:",0700);mkdir("sdmc:/game",0700);mkdir("sdmc:/game/nested",0700);
 FILE*f=fopen("sdmc:/game/nested/file","wb");assert(f);fputs("test",f);fclose(f);
 f=fopen("outside","wb");assert(f);fputs("keep",f);fclose(f);assert(!symlink("../../outside","sdmc:/game/link"));
 assert(sd_unlink_tree("sdmc:/game",0));assert(access("sdmc:/game",F_OK));assert(!access("outside",F_OK));
 errno=0;assert(!sd_unlink_tree("sdmc:/luma",0)&&errno==EPERM);
 errno=0;assert(!sd_unlink_tree("sdmc:/x",33)&&errno==ELOOP);
 assert(sd_unlink_tree("sdmc:/missing",0));
 puts("PASS: production deletion, nested files, symlink isolation, protected roots, depth and missing paths");
}
''')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],cwd=p,check=True)
