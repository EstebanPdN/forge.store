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
static void test_forge_zip(void) {
    // Independent WinZip AES counter vector over raw-DEFLATE("Stream boundary fixture." x 30).
    const u8 cipher[] = {
        0xcc,0x9b,0x30,0xce,0x27,0xdd,0x14,0x54,0x1c,0x83,0xca,0x80,0x4a,0xd4,0xab,0xfc,
        0x06,0x3f,0x14,0xa3,0xc2,0xf8,0x03,0x86,0xc8,0x61,0xef,0xe9,0x7b,0xe5,0x48,0x99,0x80
    };
    const char word[]="Stream boundary fixture.";
    const size_t size=30*(sizeof word-1);
    ForgeZipMember member={0};
    for (size_t i=0;i<sizeof member.key;i++) member.key[i]=(u8)i;
    ForgeZipStream stream;
    FILE *file=tmpfile();assert(file);
    assert(forge_zip_begin(&stream,&member,size));
    for (size_t i=0;i<sizeof cipher;i++) {
        u8 byte=cipher[i];
        assert(forge_zip_write(&stream,file,&byte,1));
        if (i+1<sizeof cipher) assert(!stream.ended);
    }
    assert(stream.ended && stream.plain==size);
    u8 extra=0;
    assert(!forge_zip_write(&stream,file,&extra,1));
    rewind(file);
    for (size_t i=0;i<size;i++) assert(fgetc(file)==word[i%(sizeof word-1)]);
    assert(fgetc(file)==EOF);
    forge_zip_end(&stream);
    u8 data[sizeof cipher+1]={0};
    memcpy(data,cipher,sizeof cipher);
    assert(forge_zip_begin(&stream,&member,size));
    assert(!forge_zip_write(&stream,file,data,sizeof data)); // trailing input in same callback
    forge_zip_end(&stream);
    memcpy(data,cipher,sizeof cipher);
    assert(forge_zip_begin(&stream,&member,size-1));
    assert(!forge_zip_write(&stream,file,data,sizeof cipher));
    forge_zip_end(&stream);
    fclose(file);
}
static Job progress_job;
static void test_progress(void) {
    // The percentage is the bytes downloaded over the bytes to download, nothing else.
    progress_job=(Job){.cia=true,.state=JOB_RUNNING,.phase=PHASE_STEPS,.total_bytes=200};
    JobStep step={0};Dl dl={.job=&progress_job,.step=&step,.expected=100,.written=50,.base=100};
    assert(!on_progress(&dl,100,50,0,0) && job_percent(&progress_job)==75);
    dl.written=100;assert(!on_progress(&dl,100,100,0,0) && job_percent(&progress_job)==99);  // 100 % only when done
    progress_job.unknown_size=true;
    assert(!on_progress(&dl,0,50,0,0) && job_percent(&progress_job)==-1);
    progress_job.state=JOB_DONE;assert(job_percent(&progress_job)==100);
    progress_job=(Job){.state=JOB_RUNNING,.nsteps=1,.cur=0,.weight={1},.wtotal=1,.phase=PHASE_FINISH};
    progress_job.steps[0].progress=1;
    assert(job_percent(&progress_job)==99 && !strcmp(job_activity(&progress_job),"install.finalizing"));
    progress_job.state=JOB_DONE;assert(job_percent(&progress_job)==100);
}

// FBI's header walk: header, certificates and ticket are 64-byte aligned; the TMD's title ID
// and version follow its signature.
static void test_cia_head(void) {
    static u8 cia[0x3000];memset(cia,0,sizeof cia);
    u32 sizes[4]={0x2020,0xa00,0x350,0xb34};
    for(int i=0,at=0;i<4;i++,at+=(i==1?8:4)){u32 v=sizes[i];int o=i==0?0:i==1?8:i==2?12:16;cia[o]=v;cia[o+1]=v>>8;cia[o+2]=v>>16;cia[o+3]=v>>24;(void)at;}
    u32 tmd=0x2040+0xa00+0x380;cia[tmd+3]=1;  // RSA-2048: 0x140-byte signature block
    u8 id[8]={0x00,0x04,0x00,0x00,0x0f,0x53,0x65,0x00};memcpy(cia+tmd+0x140+0x4c,id,8);
    cia[tmd+0x140+0x9c]=0x04;cia[tmd+0x140+0x9d]=0x10;
    u64 title=0;u16 version=0;
    assert(cia_head(cia,sizeof cia,&title,&version) && title==0x000400000F536500ULL && version==0x0410);
    assert(!cia_head(cia,tmd+0x100,&title,&version));  // the TMD is not complete yet
    cia[tmd+3]=9;assert(!cia_head(cia,sizeof cia,&title,&version));
}

// The CIA goes to AM in 256 KB blocks as it arrives; AM opens once, after the header.
static void test_cia_stream(void) {
    host_title=0;host_am_starts=0;host_am_overwrites=0;
    Job job={.state=JOB_RUNNING,.release=json_pack("{s:s}","ciaTitle","0004000012345600")};
    static u8 block[CIA_BLOCK];struct CiaSink sink={.j=&job,.buf=block};
    static u8 body[CIA_BLOCK*2+1234];memset(body,'x',sizeof body);memcpy(body,"CIA:0004000012345600:3",22);
    for(size_t at=0;at<sizeof body;at+=16384){size_t n=sizeof body-at<16384?sizeof body-at:16384;assert(cia_sink_write(&sink,body+at,n));}
    assert(sink.open && host_am_starts==1 && sink.off==CIA_BLOCK*2);
    assert(cia_sink_flush(&sink) && sink.off==sizeof body);
    assert(AM_FinishCiaInstall(sink.am)==0 && host_title==0x0004000012345600ULL && host_version==3);
    // Another app's title with the same ID is never replaced.
    memset(&tx,0,sizeof tx);Job other={.state=JOB_RUNNING,.release=json_object()};struct CiaSink s2={.j=&other,.buf=block};
    assert(!cia_sink_write(&s2,body,sizeof body) && strstr(other.error,"Another installed title"));
    // The release names its title: a different CIA is refused before AM opens.
    memset(&tx,0,sizeof tx);host_title=0;Job wrong={.state=JOB_RUNNING,.release=json_pack("{s:s}","ciaTitle","0004000099999900")};
    struct CiaSink s3={.j=&wrong,.buf=block};host_am_starts=0;
    assert(!cia_sink_write(&s3,body,sizeof body) && host_am_starts==0 && strstr(wrong.error,"does not match"));
    json_decref(job.release);json_decref(other.release);json_decref(wrong.release);memset(&tx,0,sizeof tx);host_title=0;
}

// The same CIA, recorded with its SHA-256 and still installed, is not downloaded again.
static void test_recorded_cia(void) {
    const char *want = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const char *keys[] = {"title:0004000012345600", DATA_DIR "/cia-cache/0004000012345600.cia"};  // 1.4.2 / earlier
    for (int i = 0; i < 4; i++) {
        Job job = make_job();
        json_decref(job.release);job.release = json_pack("{s:s}", "ciaTitle", "0004000012345600");
        job.record = json_pack("{s:{s:s}}", "hashes", keys[i & 1], i == 3 ? "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" : want);
        host_title = i == 2 ? 0 : 0x0004000012345600ULL;
        u64 title = 0;
        bool same = cia_already_installed(&job, want, &title);
        assert(same == (i < 2) && (!same || title == 0x0004000012345600ULL));
        clear_job(&job);
    }
    host_title = 0;
}
// Each file is read once: digests of files already on the SD are kept while their size and
// modification time stay the same; any change makes the next check read it again.
static void test_hash_cache(void) {
    const char *path = "sdmc:/3ds/cached/data.bin";
    assert(mkdirs(path, true));
    put(path, "first contents");
    JobStep step = {0};
    volatile bool cancel = false;
    char a[65], b[65], c[65];
    assert(sha256_file(path, a, &step, &cancel));
    assert(hash_cache_lookup(path, b) && !strcmp(a, b));
    put(path, "other contents");  // same size, new bytes
    assert(!hash_cache_lookup(path, b));
    assert(sha256_file(path, c, &step, &cancel) && strcmp(a, c));
    hash_cache_save();
    json_decref(hash_cache); hash_cache = NULL;  // as after a restart
    assert(hash_cache_lookup(path, b) && !strcmp(b, c));
    // Staging names are reused by every installation: never cached.
    assert(!hash_cacheable(TX_DIR "/0000.new") && !hash_cacheable(DATA_DIR "/tmp/x.cia"));
    remove(path);
    assert(!hash_cache_lookup(path, b));
}

int main(void) {
 test_forge_zip();test_progress();
 Job rate_job={0};JobStep rate_step={0};
 Dl rate={.job=&rate_job,.step=&rate_step,.expected=100,.written=90,.last_bytes=70};
 rate.last_tick=svcGetSystemTick()-SYSCLOCK_ARM11;
 assert(on_progress(&rate,20,0,0,0)==0); // cURL started a new range or followed a redirect
 assert(rate_job.done_bytes==90 && rate_step.progress>.89f && rate_step.progress<.91f);
 assert(rate_job.speed>0 && rate_job.speed<21);
 assert(!mkdir("sdmc:",0777));assert(!mkdir("sdmc:/3ds",0777));assert(!mkdir(DATA_DIR,0777));
 test_cia_head();
 test_cia_stream();
 test_recorded_cia();
 test_hash_cache();
 const char *live="sdmc:/3ds/demo.3dsx";put(live,"old");
 char path[PATH_CAP];assert(sd_path(path,sizeof path,"%3DSX%/demo.3dsx"));assert(!strcmp(path,live));
 assert(!sd_path(path,sizeof path,"/../escape"));assert(!sd_path(path,sizeof path,"/bad:device"));assert(!sd_path(path,sizeof path,"/bad./file"));assert(!sd_path(path,sizeof path,"%UNKNOWN%/file"));
 char oversized[700];memset(oversized,'x',sizeof oversized-1);oversized[0]='/';oversized[sizeof oversized-1]=0;assert(!sd_path(path,sizeof path,oversized));
 Job j=make_job();assert(tx_begin(&j));stage(&j,live,"new");equal(live,"old");assert(tx_finish(&j,false));equal(live,"old");clear_job(&j);
 j=make_job();assert(tx_begin(&j));stage(&j,live,"new");stage(&j,"sdmc:/3ds/newdir/data.bin","data");assert(tx_publish(&j));equal(live,"new");assert(tx_finish(&j,false));equal(live,"old");assert(!path_exists("sdmc:/3ds/newdir"));clear_job(&j);
 j=make_job();assert(tx_begin(&j));stage(&j,live,"new");stage(&j,"sdmc:/3ds/missing.bin","new");remove(tx_find("sdmc:/3ds/missing.bin")->stage);assert(!tx_publish(&j));assert(tx_finish(&j,false));equal(live,"old");clear_job(&j);
 archive_fixture("good.tar",false);archive_fixture("bad.tar",true);
 JobStep step={0};j=make_job();assert(tx_begin(&j));assert(!extract(&j,&step,"bad.tar",NULL,"sdmc:/3ds/",false));assert(tx_finish(&j,false));equal(live,"old");assert(!path_exists("sdmc:/escape"));clear_job(&j);
 j=make_job();assert(tx_begin(&j));j.cancel=true;assert(!extract(&j,&step,"good.tar",NULL,"sdmc:/3ds/",false));assert(tx_finish(&j,false));equal(live,"old");clear_job(&j);
 j=make_job();assert(tx_begin(&j));assert(extract(&j,&step,"good.tar",NULL,"sdmc:/3ds/",false));equal(live,"old");assert(tx_publish(&j));assert(tx_finish(&j,true));equal(live,"new");clear_job(&j);
 // Authenticated personal archives fill missing files and preserve existing copies.
 j=make_job();assert(tx_begin(&j));
 assert(extract(&j,&step,"good.tar",NULL,"sdmc:/3ds/",true));assert(!tx_find(live));
 assert(extract(&j,&step,"good.tar",NULL,"sdmc:/vb/",true));assert(tx_find("sdmc:/vb/demo.3dsx")->personal);
 assert(tx_publish(&j));assert(tx_finish(&j,true));equal(live,"new");clear_job(&j);
 // A previous edition has already unpacked the bundle. One missing or changed member forces repair.
 j=make_job();assert(tx_begin(&j));char digest[65],source[PATH_CAP];
 assert(sha256_file(live,digest,&step,&j.cancel));
 step.raw=json_pack("{s:s,s:b,s:s,s:s,s:{s:s,s:s}}","type","downloadUrl","personal",1,
                    "output","%TMP%/bundle.zip","unpackTo","/vb/","members","demo.3dsx",digest,"data/file.bin",digest);
 assert(local_download(&j,&step,source));put("sdmc:/vb/demo.3dsx","mod");
 assert(!local_download(&j,&step,source));assert(extract(&j,&step,"good.tar",NULL,"sdmc:/vb/",true));
 assert(tx_publish(&j));assert(tx_finish(&j,true));equal("sdmc:/vb/demo.3dsx","new");equal("sdmc:/vb/demo.3dsx.forge-backup-1","mod");
 assert(local_download(&j,&step,source));assert(!remove("sdmc:/vb/data/file.bin"));assert(!local_download(&j,&step,source));
 json_decref(step.raw);step.raw=NULL;clear_job(&j);
 // A valid entry followed by a corrupted archive header must fail the whole extraction.
 FILE*f=fopen("truncated.tar","wb");FILE*in=fopen("good.tar","rb");char bytes[1200];size_t n=fread(bytes,1,sizeof bytes,in);assert(n==sizeof bytes);fclose(in);assert(fwrite(bytes,1,n,f)==n);fclose(f);
 j=make_job();assert(tx_begin(&j));assert(!extract(&j,&step,"truncated.tar",NULL,"sdmc:/3ds/",false));assert(tx_finish(&j,false));equal(live,"new");clear_job(&j);
 // Bind digest to original asset filename, not merely any matching hash line.
 App app={.id="demo",.repo="example/demo"};app.j=json_loads("{\"install\":{\"3dsx\":[{\"type\":\"downloadRelease\",\"output\":\"/3ds/demo.3dsx\",\"asset\":\"binary\"}]}}",0,NULL);
 // Even a release marked Old-compatible cannot override a New-only catalog entry.
 json_object_set_new(app.j,"releases",json_pack("{s:{s:s,s:b}}","stable","tag","v1","o3ds",1));
 worker=(Thread)1;
 IS_NEW3DS=false;
 assert(!install_start(&app,false,false) && njobs==0);
 IS_NEW3DS=true;
 Job *compatible=install_start(&app,false,false);
 assert(compatible && compatible->state==JOB_QUEUED);
 json_decref(compatible->release);json_decref(compatible->script);
 memset(compatible,0,sizeof *compatible);njobs=0;worker=NULL;
 json_t*rel=json_loads("{\"assets\":{\"binary\":{\"name\":\"original.3dsx\"}}}",0,NULL);
 j=make_job();j.app=&app;j.release=rel;assert(tx_begin(&j));stage(&j,live,"abc");step.raw=json_loads("{\"type\":\"verifySha256\",\"file\":\"/3ds/demo.3dsx\"}",0,NULL);
 strcpy(sums,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad other.3dsx\n");assert(!run_step(&j,&step));equal(live,"new");
 json_t *binary=json_object_get(json_object_get(rel,"assets"),"binary");json_object_set_new(binary,"sumsName",json_string("other.3dsx"));
 assert(!run_step(&j,&step));json_object_set_new(binary,"sha256",json_string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
 assert(run_step(&j,&step));
 strcpy(sums,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *original.3dsx\r\n");assert(run_step(&j,&step));j.cancel=true;assert(!run_step(&j,&step));assert(tx_finish(&j,false));equal(live,"new");clear_job(&j);json_decref(step.raw);json_decref(rel);json_decref(app.j);
 // Real write callback rejects size overruns, cancellation and space exhaustion.
 f=tmpfile();Dl d={.f=f,.job=&j,.expected=3};j.cancel=false;assert(on_write("abc",1,3,&d)==3);assert(on_write("x",1,1,&d)==0);j.cancel=true;assert(on_write("",1,0,&d)==0);j.cancel=false;d.expected=0;host_free_space=0;assert(on_write("x",1,1,&d)==0);host_free_space=UINT64_MAX;fclose(f);
 // Active job wins over old higher slot; newest serial wins among finished jobs.
 njobs=3;jobs[0]=(Job){.app=&app,.state=JOB_RUNNING,.serial=4};jobs[2]=(Job){.app=&app,.state=JOB_DONE,.serial=2};assert(install_job(&app)==&jobs[0]);jobs[0].state=JOB_DONE;assert(install_job(&app)==&jobs[0]);
 // Main-thread record failure produces negative acknowledgment for worker rollback.
 jobs[0].commit_pending=true;jobs[0].commit_ack=false;host_commit_ok=false;install_poll(NULL);assert(jobs[0].commit_ack&&!jobs[0].commit_ok);host_commit_ok=true;
 // Inject a mid-extraction short write and a final promotion failure.
 j=make_job();assert(tx_begin(&j));host_write_budget=4;assert(!extract(&j,&step,"good.tar",NULL,"sdmc:/3ds/",false));host_write_budget=-1;assert(tx_finish(&j,false));equal(live,"new");clear_job(&j);
 j=make_job();assert(tx_begin(&j));stage(&j,live,"replacement");host_rename_calls=0;host_rename_fail_at=2;assert(!tx_publish(&j));host_rename_fail_at=0;assert(tx_finish(&j,false));equal(live,"new");clear_job(&j);
 // Real run() + install_poll() on a failed durable metadata commit restores published files.
 {TxFile target={0};strcpy(target.stage,"sdmc:/source.tar");assert(copy_file("good.tar",&target)&&strlen(target.digest)==64);}
 app.j=json_loads("{\"install\":{\"3dsx\":[]}}",0,NULL);
 memset(jobs,0,sizeof jobs);njobs=1;Job *full=&jobs[0];full->app=&app;full->nsteps=1;
 full->steps[0].raw=json_loads("{\"type\":\"extractFile\",\"file\":\"/source.tar\",\"output\":\"/3ds/\"}",0,NULL);
 put(live,"old-again");host_auto_ack=true;host_commit_ok=false;run(full);assert(full->state==JOB_FAILED);equal(live,"old-again");json_decref(full->steps[0].raw);clear_job(full);
 // Uninstall, FBI-style. A 3DSX app: "uninstall only" removes the files it installed and
 // keeps the port's data; the record is forgotten only when the main thread saves that.
 json_t *record=json_pack("{s:[s],s:{},s:[]}","files",live,"hashes","dirs");put("sdmc:/3ds/user.dat","user-save");
 memset(full,0,sizeof *full);full->app=&app;full->uninstall=true;full->record=json_incref(record);run_uninstall(full);assert(full->state==JOB_FAILED);clear_job(full);
 put(live,"old-again");host_commit_ok=true;memset(full,0,sizeof *full);full->app=&app;full->uninstall=true;full->record=json_incref(record);run_uninstall(full);assert(full->state==JOB_DONE);assert(!path_exists(live));equal("sdmc:/3ds/user.dat","user-save");clear_job(full);json_decref(record);
 // A CIA: "uninstall only" deletes the title and its ticket; every file stays.
 host_title=0x0004000012345600ULL;host_tickets_deleted=0;put(live,"data");
 record=json_pack("{s:[s,s],s:{},s:[]}","files","title:0004000012345600",live,"hashes","dirs");
 memset(full,0,sizeof *full);full->app=&app;full->uninstall=true;full->record=json_incref(record);run_uninstall(full);
 assert(full->state==JOB_DONE && !host_title && host_tickets_deleted==1);equal(live,"data");clear_job(full);
 // "Uninstall and delete files": the port's folder and personal data go too, never /3ds itself,
 // another top folder or the store's own folder.
 assert(mkdirs("sdmc:/3ds/PortX/saves/slot1.sav",true));put("sdmc:/3ds/PortX/saves/slot1.sav","s");put("sdmc:/3ds/PortX/game.rom","r");
 assert(mkdirs("sdmc:/vb/a.vb",true));put("sdmc:/vb/a.vb","rom");put("sdmc:/vb/keep.vb","other");
 host_title=0x0004000012345600ULL;
 memset(full,0,sizeof *full);full->app=&app;full->uninstall=true;full->record=json_incref(record);
 full->purge=json_pack("[s,s,s,s,s]","sdmc:/3ds/PortX","sdmc:/vb/a.vb","sdmc:/3ds",DATA_DIR,"sdmc:/vb");run_uninstall(full);
 assert(full->state==JOB_DONE && !host_title);assert(!path_exists("sdmc:/3ds/PortX"));assert(!path_exists("sdmc:/vb/a.vb"));assert(!path_exists(live));
 equal("sdmc:/vb/keep.vb","other");assert(path_exists(DATA_DIR));json_decref(full->purge);clear_job(full);json_decref(record);json_decref(app.j);
 // Mid-entry cancellation leaves the previous installed bytes intact.
 put(live,"keep");j=make_job();assert(tx_begin(&j));host_cancel_on_write=&j;assert(!extract(&j,&step,"good.tar",NULL,"sdmc:/3ds/",false));assert(tx_finish(&j,false));equal(live,"keep");clear_job(&j);
 // Mocked AM: a new title whose record cannot be saved is removed again.
 host_title=0;j=make_job();assert(tx_begin(&j));put("sdmc:/candidate.cia","package");tx.title_id=0x0004000012345600ULL;tx.had_title=false;assert(install_cia(&j,&step,"sdmc:/candidate.cia",false));tx.cia_attempted=true;assert(host_title);assert(cia_restore(&j));assert(!host_title);assert(tx_finish(&j,false));clear_job(&j);remove("sdmc:/candidate.cia");
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
 puts("install integrity: 28 scenario groups passed (isolated host fixture; AM mocked)");return 0;
}
