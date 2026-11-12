#define FORGE_INSTALL_HOST_TEST
#include "../source/install.c"
#include <assert.h>
static bool path_exists(const char *p) {struct stat st;return !stat(p,&st);}
static void svcSleepThread(u64 ns) {if(host_auto_ack) install_poll(NULL);}
static void put(const char *p,const char*s) {FILE*f=fopen(p,"wb");assert(f);assert(fwrite(s,1,strlen(s),f)==strlen(s));assert(!fclose(f));}
static void equal(const char*p,const char*s) {char b[256]={0};FILE*f=fopen(p,"rb");assert(f);fread(b,1,sizeof b-1,f);fclose(f);assert(!strcmp(b,s));}
static Job make_job(void) {Job j={0};return j;}
static void clear_job(Job*j) {json_decref(j->files);json_decref(j->dirs);json_decref(j->hashes);json_decref(j->record);}
static void stage(Job*j,const char*path,const char*text) {TxFile*e=tx_file(j,path);assert(e);put(e->stage,text);}
static void archive_fixture(const char*file,bool bad) {
 struct archive*a=archive_write_new();archive_write_set_format_pax_restricted(a);assert(archive_write_open_filename(a,file)==ARCHIVE_OK);
 const char*names[]={"demo.3dsx",bad?"../escape":"data/file.bin"};
 for(int i=0;i<2;i++){struct archive_entry*e=archive_entry_new();archive_entry_set_pathname(e,names[i]);archive_entry_set_filetype(e,AE_IFREG);archive_entry_set_perm(e,0644);archive_entry_set_size(e,3);assert(archive_write_header(a,e)==ARCHIVE_OK);assert(archive_write_data(a,"new",3)==3);archive_entry_free(e);}
 assert(archive_write_close(a)==ARCHIVE_OK);archive_write_free(a);
}
int main(void) {
 assert(!mkdir("sdmc:",0777));assert(!mkdir("sdmc:/3ds",0777));assert(!mkdir(DATA_DIR,0777));
 const char *live="sdmc:/3ds/demo.3dsx";put(live,"old");
 char path[PATH_CAP];assert(sd_path(path,sizeof path,"%3DSX%/demo.3dsx"));assert(!strcmp(path,live));
 assert(!sd_path(path,sizeof path,"/../escape"));assert(!sd_path(path,sizeof path,"/bad:device"));assert(!sd_path(path,sizeof path,"/bad./file"));assert(!sd_path(path,sizeof path,"%UNKNOWN%/file"));
 char oversized[700];memset(oversized,'x',sizeof oversized-1);oversized[0]='/';oversized[sizeof oversized-1]=0;assert(!sd_path(path,sizeof path,oversized));
 Job j=make_job();assert(tx_begin(&j));stage(&j,live,"new");equal(live,"old");assert(tx_finish(&j,false));equal(live,"old");clear_job(&j);
 j=make_job();assert(tx_begin(&j));stage(&j,live,"new");stage(&j,"sdmc:/3ds/newdir/data.bin","data");assert(tx_publish(&j));equal(live,"new");assert(tx_finish(&j,false));equal(live,"old");assert(!path_exists("sdmc:/3ds/newdir"));clear_job(&j);
 j=make_job();assert(tx_begin(&j));stage(&j,live,"new");stage(&j,"sdmc:/3ds/missing.bin","new");remove(tx_find("sdmc:/3ds/missing.bin")->stage);assert(!tx_publish(&j));assert(tx_finish(&j,false));equal(live,"old");clear_job(&j);
 archive_fixture("good.tar",false);archive_fixture("bad.tar",true);
 JobStep step={0};j=make_job();assert(tx_begin(&j));assert(!extract(&j,&step,"bad.tar",NULL,"sdmc:/3ds/"));assert(tx_finish(&j,false));equal(live,"old");assert(!path_exists("sdmc:/escape"));clear_job(&j);
 j=make_job();assert(tx_begin(&j));j.cancel=true;assert(!extract(&j,&step,"good.tar",NULL,"sdmc:/3ds/"));assert(tx_finish(&j,false));equal(live,"old");clear_job(&j);
 j=make_job();assert(tx_begin(&j));assert(extract(&j,&step,"good.tar",NULL,"sdmc:/3ds/"));equal(live,"old");assert(tx_publish(&j));assert(tx_finish(&j,true));equal(live,"new");clear_job(&j);
 // A valid entry followed by a corrupted archive header must fail the whole extraction.
 FILE*f=fopen("truncated.tar","wb");FILE*in=fopen("good.tar","rb");char bytes[1200];size_t n=fread(bytes,1,sizeof bytes,in);assert(n==sizeof bytes);fclose(in);assert(fwrite(bytes,1,n,f)==n);fclose(f);
 j=make_job();assert(tx_begin(&j));assert(!extract(&j,&step,"truncated.tar",NULL,"sdmc:/3ds/"));assert(tx_finish(&j,false));equal(live,"new");clear_job(&j);
 // Bind digest to original asset filename, not merely any matching hash line.
 App app={.id="demo",.repo="example/demo"};app.j=json_loads("{\"install\":{\"3dsx\":[{\"type\":\"downloadRelease\",\"output\":\"/3ds/demo.3dsx\",\"asset\":\"binary\"}]}}",0,NULL);
 json_t*rel=json_loads("{\"assets\":{\"binary\":{\"name\":\"original.3dsx\"}}}",0,NULL);
 j=make_job();j.app=&app;j.release=rel;assert(tx_begin(&j));stage(&j,live,"abc");step.raw=json_loads("{\"type\":\"verifySha256\",\"file\":\"/3ds/demo.3dsx\"}",0,NULL);
 strcpy(sums,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad other.3dsx\n");assert(!run_step(&j,&step));equal(live,"new");
 strcpy(sums,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *original.3dsx\r\n");assert(run_step(&j,&step));j.cancel=true;assert(!run_step(&j,&step));assert(tx_finish(&j,false));equal(live,"new");clear_job(&j);json_decref(step.raw);json_decref(rel);json_decref(app.j);
 // Real write callback rejects size overruns, cancellation and space exhaustion.
 f=tmpfile();Dl d={.f=f,.job=&j,.expected=3};j.cancel=false;assert(on_write("abc",1,3,&d)==3);assert(on_write("x",1,1,&d)==0);j.cancel=true;assert(on_write("",1,0,&d)==0);j.cancel=false;d.expected=0;host_free_space=0;assert(on_write("x",1,1,&d)==0);host_free_space=UINT64_MAX;fclose(f);
 // Active job wins over old higher slot; newest serial wins among finished jobs.
 njobs=3;jobs[0]=(Job){.app=&app,.state=JOB_RUNNING,.serial=4};jobs[2]=(Job){.app=&app,.state=JOB_DONE,.serial=2};assert(install_job(&app)==&jobs[0]);jobs[0].state=JOB_DONE;assert(install_job(&app)==&jobs[0]);
 // Main-thread record failure produces negative acknowledgment for worker rollback.
 jobs[0].commit_pending=true;jobs[0].commit_ack=false;host_commit_ok=false;install_poll(NULL);assert(jobs[0].commit_ack&&!jobs[0].commit_ok);host_commit_ok=true;
 // Inject a mid-extraction short write and a final promotion failure.
 j=make_job();assert(tx_begin(&j));host_write_budget=4;assert(!extract(&j,&step,"good.tar",NULL,"sdmc:/3ds/"));host_write_budget=-1;assert(tx_finish(&j,false));equal(live,"new");clear_job(&j);
 j=make_job();assert(tx_begin(&j));stage(&j,live,"replacement");host_rename_calls=0;host_rename_fail_at=2;assert(!tx_publish(&j));host_rename_fail_at=0;assert(tx_finish(&j,false));equal(live,"new");clear_job(&j);
 // Real run() + install_poll() on a failed durable metadata commit restores published files.
 assert(copy_file("good.tar","sdmc:/source.tar"));
 app.j=json_loads("{\"install\":{\"3dsx\":[]}}",0,NULL);
 memset(jobs,0,sizeof jobs);njobs=1;Job *full=&jobs[0];full->app=&app;full->nsteps=1;
 full->steps[0].raw=json_loads("{\"type\":\"extractFile\",\"file\":\"/source.tar\",\"output\":\"/3ds/\"}",0,NULL);
 put(live,"old-again");host_auto_ack=true;host_commit_ok=false;run(full);assert(full->state==JOB_FAILED);equal(live,"old-again");json_decref(full->steps[0].raw);clear_job(full);
 // Uninstall preserves modified/untracked user data and rolls back on record failure.
 char goodhash[65];volatile bool no=false;assert(sha256_file(live,goodhash,&step,&no));
 json_t *record=json_pack("{s:[s,s],s:{},s:[]}","files",live,"sdmc:/3ds/user.dat","hashes","dirs");
 json_object_set_new(json_object_get(record,"hashes"),live,json_string(goodhash));put("sdmc:/3ds/user.dat","user-save");
 memset(full,0,sizeof *full);full->app=&app;full->uninstall=true;full->record=json_incref(record);run_uninstall(full);assert(full->state==JOB_FAILED);equal(live,"old-again");equal("sdmc:/3ds/user.dat","user-save");clear_job(full);
 host_commit_ok=true;memset(full,0,sizeof *full);full->app=&app;full->uninstall=true;full->record=json_incref(record);run_uninstall(full);assert(full->state==JOB_DONE);assert(!path_exists(live));equal("sdmc:/3ds/user.dat","user-save");assert(full->warnings==1);clear_job(full);json_decref(record);json_decref(app.j);
 // Mid-entry cancellation leaves the previous installed bytes intact.
 put(live,"keep");j=make_job();assert(tx_begin(&j));host_cancel_on_write=&j;assert(!extract(&j,&step,"good.tar",NULL,"sdmc:/3ds/"));assert(tx_finish(&j,false));equal(live,"keep");clear_job(&j);
 // Mocked AM: reject an external existing title before AM overwrite when its original CIA is unavailable.
 j=make_job();assert(tx_begin(&j));stage(&j,"sdmc:/candidate.cia","package");strcpy(tx.cia_path,"sdmc:/candidate.cia");host_title=0x0004000012345600ULL;host_am_starts=0;assert(!cia_prepare(&j));assert(!host_am_starts);assert(tx_finish(&j,false));clear_job(&j);
 // Mocked AM: new-title rollback removes only this transaction's title/cache.
 host_title=0;j=make_job();assert(tx_begin(&j));stage(&j,"sdmc:/candidate.cia","package");tx_find("sdmc:/candidate.cia")->deleted=true;strcpy(tx.cia_path,"sdmc:/candidate.cia");assert(cia_prepare(&j));assert(tx_publish(&j));assert(hash_published(&j));tx.cia_attempted=true;assert(install_cia(&j,&step,tx.cia_cache));assert(host_title);assert(cia_restore(&j));assert(!host_title);assert(tx_finish(&j,false));clear_job(&j);
 // Mocked AM: an update's old package is reinstalled and original cache bytes restored.
 assert(mkdirs(DATA_DIR "/cia-cache",false));put(DATA_DIR "/cia-cache/0004000012345600.cia","old-package");host_title=0x0004000012345600ULL;
 j=make_job();assert(tx_begin(&j));stage(&j,"sdmc:/candidate.cia","new-package");tx_find("sdmc:/candidate.cia")->deleted=true;strcpy(tx.cia_path,"sdmc:/candidate.cia");assert(cia_prepare(&j));assert(tx_publish(&j));tx.cia_attempted=true;assert(install_cia(&j,&step,tx.cia_cache));int starts=host_am_starts;assert(cia_restore(&j));assert(host_am_starts==starts+1);assert(tx_finish(&j,false));equal(DATA_DIR "/cia-cache/0004000012345600.cia","old-package");clear_job(&j);
 // Pinned hashes bind downloaded bytes before extraction or publication.
 j=make_job();assert(tx_begin(&j));stage(&j,live,"abc");
 assert(verify_download(&j,&step,live,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
 assert(!verify_download(&j,&step,live,"0000000000000000000000000000000000000000000000000000000000000000"));
 assert(tx_finish(&j,false));clear_job(&j);
 // Personal data participates in rollback but never in the uninstall inventory.
 const char *personal="sdmc:/3ds/personal.bin";
 j=make_job();assert(tx_begin(&j));stage(&j,personal,"personal");tx_find(personal)->personal=true;
 assert(tx_publish(&j));assert(json_array_size(j.files)==0);assert(tx_finish(&j,true));equal(personal,"personal");clear_job(&j);
 // Existing personal data is preserved without accessing its download URL.
 j=make_job();assert(tx_begin(&j));step.raw=json_loads("{\"type\":\"downloadUrl\",\"personal\":true,\"output\":\"/3ds/personal.bin\"}",0,NULL);
 assert(run_step(&j,&step));assert(tx.count==0);assert(tx_finish(&j,true));equal(personal,"personal");clear_job(&j);json_decref(step.raw);
 // A file appearing after staging is preserved; transaction rollback removes no user bytes.
 j=make_job();assert(tx_begin(&j));stage(&j,personal,"replacement");tx_find(personal)->personal=true;
 assert(!tx_publish(&j));assert(tx_finish(&j,false));equal(personal,"personal");clear_job(&j);
 remove(personal);j=make_job();assert(tx_begin(&j));stage(&j,personal,"new");tx_find(personal)->personal=true;assert(tx_publish(&j));assert(tx_finish(&j,false));assert(!path_exists(personal));clear_job(&j);
 puts("install integrity: 27 scenario groups passed (isolated host fixture; AM mocked)");return 0;
}
