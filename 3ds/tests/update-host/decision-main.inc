// Included after the unmodified updater implementation by run-store-network.py.
static void *test_worker(void *unused) { run(NULL); return NULL; }
int main(int argc,char **argv) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    hb=!strncmp(argv[1],"hb",2);homebrew=hb;fault=atoi(argv[2]);
    strcpy(executable,"sdmc:/app.3dsx");
    FILE *f=fopen(executable,"wb");assert(f);fwrite("3DSXold",1,7,f);fclose(f);
    if(!strcmp(argv[1],"hb-missing"))strcpy(executable,"sdmc:/missing.3dsx");
    assert(!store_update_decide(true)); // A press before an offer is not consent.
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
        if(strcmp(argv[1],"bad-version"))assert(!strcmp(store_update_version(),"2026.11.12.3"));
        else {char fallback[40];snprintf(fallback,sizeof fallback,"%lld",(long long)STORE_BUILD+1);assert(!strcmp(store_update_version(),fallback));}
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
    if(want!=UPDATE_DONE)assert(finishes==0||fault==2);
    if(want==UPDATE_DONE&&!hb)assert(starts==1&&finishes==1&&writes>0);
    if(fault==1||fault==2)assert(cancels==1);
    if(!strcmp(argv[1],"wrong-title")||fault==3||want==UPDATE_DECLINED)assert(starts==0);
    if(hb){
        f=fopen("sdmc:/app.3dsx","rb");assert(f);char b[8]={0};fread(b,1,7,f);fclose(f);
        assert(!memcmp(b,want==UPDATE_DONE?"3DSXxxx":"3DSXold",7));
    }
    if(hb&&want==UPDATE_DONE){
        f=fopen("sdmc:/app.forge-previous.3dsx","rb");assert(f);char backup[8]={0};
        assert(fread(backup,1,7,f)==7&&!memcmp(backup,"3DSXold",7));fclose(f);
    }
    assert(access(DATA_DIR "/store-update.part",F_OK));
    curl_global_cleanup();return 0;
}
