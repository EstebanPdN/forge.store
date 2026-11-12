// A native layout of the public terminal-style profile. Static fields/portrait
// ship with the app; counts use the same refreshed GitHub metadata as the reader.
static Sheet profile_art;
static json_t *profile_data;
static bool gh_native_profile(void){return gh_user&&gh_tab==0&&!strcmp(gh_name,"EstebanPdN")&&profile_data;}
static const char *profile_value(const char *label){
    json_t *row;size_t i;
    json_array_foreach(json_object_get(profile_data,"rows"),i,row)
        if(!strcmp(jstr(row,"label"),label))return jstr(row,"value");
    return "";
}
static void gh_profile_draw(float alpha){
    float y=36-gh_scroll;
    glass(12,y,376,160,12,alpha);
    if(sheet_ok(&profile_art))draw_img_tint(sheet_img(&profile_art,0),22,y+9,1,1,C(TH.ink,alpha*.88f));
    text_style(jstr(profile_data,"login"),142,y+12,11,700,FONT_MONO,C(FORGE,alpha),AL_LEFT);
    rect(142,y+31,230,1,C(TH.line,TH.line_a*alpha));
    const char *labels[]={"OS","Hardware","IDE"};
    for(int i=0;i<3;i++){
        float by=y+40+i*37;
        text_style(labels[i],142,by,9,700,FONT_MONO,C(FORGE,alpha),AL_LEFT);
        text_fit_style(profile_value(labels[i]),142,by+13,10,600,230,C(TH.ink,alpha));
    }
    float x=92;const char *names[]={"Website","Discord","Patreon"};
    for(int i=0;i<3;i++){
        float width=text_style_w(names[i],9,700,0)+20;
        rrect(x,y+169,width,18,9,C(TH.surface2,alpha));
        text_style(names[i],x+10,y+172,9,700,0,C(TH.ink_soft,alpha),AL_LEFT);x+=width+3;
    }
    y+=201;
    const char *keys[]={"Languages.Programming","Languages.Computer","Languages.Real","Hobbies.Software","Hobbies.Hardware"};
    const char *shorts[]={"Code","Web","Languages","Software","Hardware"};
    glass(12,y,376,125,10,alpha);
    for(int i=0;i<5;i++){
        text_style(shorts[i],24,y+12+i*22,10,700,FONT_MONO,C(FORGE,alpha),AL_LEFT);
        text_fit_style(profile_value(keys[i]),116,y+12+i*22,10,500,260,C(TH.ink,alpha));
    }
    y+=137;
    long long stars=0;json_t *repo;size_t i;
    json_array_foreach(gh_repos,i,repo)stars+=json_integer_value(json_object_get(repo,"stargazers_count"));
    long long values[]={json_integer_value(json_object_get(gh_meta,"public_repos")),stars,json_integer_value(json_object_get(gh_meta,"followers"))};
    const char *labels2[]={T("gh.repos"),T("gh.totalStars"),T("gh.followers")};
    for(int k=0;k<3;k++){
        float bx=12+k*127;char value[32];snprintf(value,sizeof value,"%lld",values[k]);
        glass(bx,y,122,49,9,alpha);text_style(value,bx+10,y+7,16,800,0,C(TH.ink,alpha),AL_LEFT);
        text_style(labels2[k],bx+10,y+30,9,600,0,C(TH.ink_faint,alpha),AL_LEFT);
    }
    y+=65;
    text_style("Contact",24,y,11,700,FONT_MONO,C(FORGE,alpha),AL_LEFT);
    text_style(profile_value("Email"),24,y+20,10,500,0,C(TH.ink,alpha),AL_LEFT);
    text_style(profile_value("Discord"),24,y+36,10,500,0,C(TH.ink,alpha),AL_LEFT);
    y+=70;text_style("Development",24,y,11,700,FONT_MONO,C(FORGE,alpha),AL_LEFT);
    text_wrap_style(jstr(profile_data,"development"),24,y+21,10,500,C(TH.ink_soft,alpha),352);
    text_style("Learn about the development process",24,y+66,10,600,0,C(SKY,alpha),AL_LEFT);
}
