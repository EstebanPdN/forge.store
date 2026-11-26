// Included after the unmodified updater implementation by run-store-network.py.
static void put(const char *path,const char *text){FILE *f=fopen(path,"wb");assert(f);fputs(text,f);fclose(f);}
static bool has(const char *path,const char *text){
    char b[64]={0};FILE *f=fopen(path,"rb");if(!f)return false;fread(b,1,sizeof b-1,f);fclose(f);return !strncmp(b,text,strlen(text));
}
static void *test_worker(void *unused) { run(NULL); return NULL; }
int main(int argc,char **argv) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    hb=!strncmp(argv[1],"hb",2);homebrew=hb;fault=atoi(argv[2]);
    strcpy(executable,"sdmc:/app.3dsx");
    // A release download in the store's folder, beside a launchable 1.1 backup.
    bool renamed=!strcmp(argv[1],"hb-rename"),kept=!strcmp(argv[1],"hb-keep");
    if(renamed||kept){
        strcpy(executable,DATA_DIR "/forge-store-v1.1.3dsx");
        put(DATA_DIR "/forge-store-v1.1.forge-previous.3dsx","3DSXbak");
        put(DATA_DIR "/settings.json","{}");
        if(kept)put(STORE_HOME_3DSX,"3DSXoth");
        else unlink(STORE_HOME_3DSX);
    }
    FILE *f=fopen(executable,"wb");assert(f);fwrite("3DSXold",1,7,f);fclose(f);
    if(!strcmp(argv[1],"hb-missing"))strcpy(executable,"sdmc:/missing.3dsx");
    assert(!store_update_decide(true)); // A press before an offer is not consent.
    started_at=osGetTime();
    if(!strcmp(argv[1],"deadline")){
        started_at=osGetTime()-5001;store_update_poll();assert(store_update_state()==UPDATE_OFFLINE);
    }
    if(!strcmp(argv[1],"cancelled"))cancelled=true;
    pthread_t job;assert(!pthread_create(&job,NULL,test_worker,NULL));
    u64 end=osGetTime()+5000;
    while(store_update_state()==UPDATE_CHECKING&&osGetTime()<end)svcSleepThread(1000000);
    assert(store_update_state()!=UPDATE_CHECKING);
    if(store_update_state()==UPDATE_AVAILABLE){
        assert(*store_update_version());
        assert(!strcmp(store_update_version(),"1.10"));
        svcSleepThread(100000000);
        assert(store_update_state()==UPDATE_AVAILABLE);
        assert(access(DATA_DIR "/store-update.part",F_OK)&&!starts&&!writes);
        started_at=osGetTime()-60000;store_update_poll();
        assert(store_update_state()==UPDATE_AVAILABLE); // No deadline for the user's choice.
        if(!strcmp(argv[1],"cancel-wait"))store_update_stop();
        else {
            bool accept=strcmp(argv[1],"decline")!=0;
            assert(store_update_decide(accept));
            assert(!store_update_decide(!accept)); // Decision cannot be replaced.
        }
    }
    pthread_join(job,NULL);
    int want=atoi(argv[3]);assert(store_update_state()==want);
    if(want!=UPDATE_DONE)assert(finishes==0||fault==2||fault==4);  // 4: finished, title unchanged
    if(want==UPDATE_DONE&&!hb)assert(starts==1&&finishes==1&&writes>0);
    if(fault==1||fault==2)assert(cancels==1);
    if(!strcmp(argv[1],"wrong-title")||fault==3||want==UPDATE_DECLINED)assert(starts==0);
    const char *app=renamed?STORE_HOME_3DSX:kept?DATA_DIR "/forge-store-v1.1.3dsx":"sdmc:/app.3dsx";
    if(hb&&strcmp(argv[1],"hb-missing")){
        assert(!strcmp(executable,app));  // the path a restart launches
        assert(has(app,want==UPDATE_DONE?"3DSXxxx":"3DSXold"));
    }
    if(hb&&want==UPDATE_DONE){
        char backup[1100];snprintf(backup,sizeof backup,"%s.previous",app);
        assert(has(backup,"3DSXold"));
        tidy_homebrew();  // the next start
        assert(access(backup,F_OK)&&has(app,"3DSXxxx"));
    }
    if(renamed||kept){
        assert(access(DATA_DIR "/forge-store-v1.1.forge-previous.3dsx",F_OK)&&has(DATA_DIR "/settings.json","{}"));
        assert(!renamed||access(DATA_DIR "/forge-store-v1.1.3dsx",F_OK));
        assert(!kept||has(STORE_HOME_3DSX,"3DSXoth"));  // never replaces another copy
    }
    assert(access(DATA_DIR "/store-update.part",F_OK));
    curl_global_cleanup();return 0;
}
