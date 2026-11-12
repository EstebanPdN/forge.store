// Bounded Markdown/HTML reader. No scripts, CSS, external entities or HTML execute.
#include "gh_markup.h"
#include "gh_tables.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static bool contains(const char *s, const char *needle) {
    size_t n = strlen(needle); for (; *s; s++) if (!strncasecmp(s, needle, n)) return true; return false;
}
static void append_styled(char *out, size_t *n, const char *s, size_t len, bool *cut, unsigned char *styles, unsigned char style) {
    size_t available = GH_DOC_BYTES - *n;
    if (len > available) { len = available; *cut = true; }
    if (len) {memcpy(out + *n, s, len);memset(styles+*n,style,len);}
    *n += len; out[*n] = 0;
}
static void attr(const char *tag, const char *name, char *out, size_t size) {
    out[0] = 0; size_t key = strlen(name);
    for (const char *p = tag; *p; p++) {
        if (p != tag && !isspace((unsigned char)p[-1])) continue;
        if (strncasecmp(p, name, key)) continue;
        const char *q = p + key; while (isspace((unsigned char)*q)) q++;
        if (*q++ != '=') continue;
        while (isspace((unsigned char)*q)) q++;
        char quote = *q == '\'' || *q == '"' ? *q++ : 0; const char *end = q;
        while (*end && (quote ? *end != quote : !isspace((unsigned char)*end) && *end != '>')) end++;
        size_t n = end - q; if (n >= size) n = size - 1;
        memcpy(out, q, n); out[n] = 0; return;
    }
}
static bool resolve(char *out, size_t cap, const char *src, const char *repo, const char *branch, bool image) {
    if (!src || !*src) return false;
    if (*src == '#') {
        if (image || strlen(src) >= cap) return false;
        snprintf(out, cap, "%s", src);
        return true;
    }
    int n;
    if (!strncmp(src, "https://", 8)) n = snprintf(out, cap, "%s", src);
    else if (strchr(src, ':') || !strncmp(src, "//", 2)) return false;
    else {
        while (*src == '/') src++;
        if (!strncmp(src, "./", 2)) src += 2;
        n = snprintf(out, cap, image ? "https://raw.githubusercontent.com/%s/%s/%s" : "https://github.com/%s/blob/%s/%s", repo, branch, src);
    }
    return n > 0 && (size_t)n < cap;
}
static void decode_entities(char *s) {
    char *d = s;
    while (*s) {
        static const char *keys[] = {"&amp;", "&lt;", "&gt;", "&quot;", "&#39;", "&nbsp;"};
        static const char vals[] = {'&','<','>','"','\'',' '}; bool done = false;
        for (int i = 0; i < 6; i++) if (!strncmp(s, keys[i], strlen(keys[i]))) { *d++ = vals[i]; s += strlen(keys[i]); done = true; break; }
        if (!done) *d++ = *s++;
    }
    *d = 0;
}
static bool node_styled(GhDocument *d, GhKind kind, const char *s, const char *url, int level, unsigned group, unsigned char style) {
    size_t n = s ? strlen(s) : 0, u = url ? strlen(url) : 0;
    if (d->count >= GH_DOC_NODES || n + u > GH_DOC_BYTES - d->bytes) { d->truncated = true; return false; }
    GhNode *v = &d->nodes[d->count];
    *v = (GhNode){kind, strdup(s ? s : ""), url ? strdup(url) : NULL, level, group, style};
    if (!v->text || (url && !v->url)) { free(v->text); free(v->url); d->truncated = true; return false; }
    decode_entities(v->text); if (v->url) decode_entities(v->url);
    d->bytes += n + u; d->count++; return true;
}
static void text_runs(GhDocument *d, GhKind kind, const char *s, size_t len,
                      const unsigned char *styles, int level, unsigned group) {
    char *run = malloc(len + 1);
    if (!run) { d->truncated = true; return; }
    size_t used = 0; unsigned char active = 0, markdown = 0;
    for (size_t i = 0; i < len; i++) {
        if(kind!=GH_CODE&&s[i]=='`'){markdown^=64;continue;}
        if (kind!=GH_CODE && !(markdown&64) && i + 1 < len && ((s[i] == '*' && s[i+1] == '*') || (s[i] == '_' && s[i+1] == '_'))) {
            markdown ^= 2; i++; continue;
        }
        unsigned char style = styles[i] | markdown;
        if (used && style != active) {
            run[used] = 0; node_styled(d,kind,run,NULL,level,group,active); used = 0;
        }
        active = style; run[used++] = s[i];
    }
    if (used) { run[used] = 0; node_styled(d,kind,run,NULL,level,group,active); }
    free(run);
}
void gh_document_free(GhDocument *d) {
    if (!d) return;
    for (int i = 0; i < d->count; i++) { free(d->nodes[i].text); free(d->nodes[i].url); }
    free(d->nodes); memset(d, 0, sizeof *d);
}
static unsigned list_prefix(const char *s,const char *end,int *number) {
    *number=0;
    if(end-s>=4&&!strncmp(s,"• ",4))return 4;
    if(end-s>=2&&strchr("-*+",*s)&&isspace((unsigned char)s[1]))return 2;
    const char *p=s;unsigned value=0;
    while(p<end&&isdigit((unsigned char)*p)&&p-s<6){value=value*10+*p-'0';p++;}
    if(p>s&&p+1<end&&(*p=='.'||*p==')')&&isspace((unsigned char)p[1])){*number=value;return p-s+2;}
    return 0;
}
// Markdown soft newlines are spaces within one paragraph. Preserve structural
// lines, explicit HTML <br>, fenced code, and Markdown's two-space hard break.
static bool prose_line(const char *begin,const char *end) {
    while(begin<end&&(*begin==' '||*begin=='\t'))begin++;
    if(begin==end||*begin==31)return false;
    if(strchr("#>|`~",*begin)||(!strncmp(begin,"![",2))||(!strncmp(begin,"• ",4)))return false;
    if(strchr("-*+",*begin)&&(begin+1==end||isspace((unsigned char)begin[1])||begin[1]==*begin))return false;
    const char *p=begin;while(p<end&&isdigit((unsigned char)*p))p++;
    return !(p>begin&&p+1<end&&(*p=='.'||*p==')')&&isspace((unsigned char)p[1]));
}
static void soft_breaks(char *plain,const unsigned char *styles) {
    bool code=false;
    for(char *line=plain,*end;(end=strchr(line,'\n'));line=end+1) {
        char *start=line;while(start<end&&(*start==' '||*start=='\t'))start++;
        bool fence=end-start>=3&&(!strncmp(start,"```",3)||!strncmp(start,"~~~",3));
        if(fence){code=!code;continue;}
        char *following=end+1,*last=strchr(following,'\n');if(!last)last=following+strlen(following);
        bool hard=end-line>=2&&end[-1]==' '&&end[-2]==' ';
        int number;bool list=list_prefix(start,end,&number)>0;
        if(!code&&!hard&&!(styles[end-plain]&4)&&!(styles[line-plain]&4)&&!(styles[following-plain]&4)&&(prose_line(line,end)||list)&&prose_line(following,last))*end=' ';
    }
}
#define append(o,n,s,l,c) append_styled(o,n,s,l,c,styles,current_style)
#define node(d,k,s,u,l) node_styled(d,k,s,u,l,line_group,line_style)
bool gh_document_parse(GhDocument *d, const char *source, const char *repo, const char *branch, bool dark) {
    gh_document_free(d);
    d->nodes = calloc(GH_DOC_NODES, sizeof *d->nodes);
    char *plain = calloc(GH_DOC_BYTES + 1, 1);
    unsigned char *styles=calloc(GH_DOC_BYTES+1,1),current_style=0;
    if (!d->nodes || !plain || !styles) { free(plain); free(styles); gh_document_free(d); return false; }
    size_t o = 0; char chosen[2048] = "", href[2048] = ""; bool skip = false, fenced = false, inline_code = false, paragraph = false;
    const char *begin = source ? source : "";
    for (const char *p = begin; *p;) {
        if (o == GH_DOC_BYTES) { d->truncated = true; break; }
        if (!skip && (p == begin || p[-1] == '\n') && (!strncmp(p, "```", 3) || !strncmp(p, "~~~", 3))) {
            fenced = !fenced;
            const char *end = strchr(p, '\n'); if (!end) end = p + strlen(p);
            append(plain, &o, p, end - p, &d->truncated); p = end; continue;
        }
        if (!skip && *p == '`' && !fenced) inline_code = !inline_code;
        if (!skip && (fenced || inline_code)) { append(plain, &o, p++, 1, &d->truncated); continue; }
        if (!strncmp(p, "<!--", 4)) { const char *e = strstr(p + 4, "-->"); p = e ? e + 3 : p + strlen(p); continue; }
        if (*p != '<') {
            if (!skip) {
                bool space=paragraph&&isspace((unsigned char)*p);
                if(!space||!o||plain[o-1]!=' ')append(plain,&o,space?" ":p,1,&d->truncated);
            }p++;continue;
        }
        const char *end = strchr(p, '>');
        if (!end || end - p > 4095) { append(plain, &o, p++, 1, &d->truncated); continue; }
        char tag[4096]; size_t n = end - p - 1; memcpy(tag, p + 1, n); tag[n] = 0; p = end + 1;
        char name[32]; size_t k = 0; while (tag[k] && !isspace((unsigned char)tag[k]) && k < sizeof name - 1) { name[k] = tolower((unsigned char)tag[k]); k++; } name[k] = 0;
        if (!strcmp(name, "script") || !strcmp(name, "style")) { skip = true; continue; }
        if (!strcmp(name, "/script") || !strcmp(name, "/style")) { skip = false; continue; }
        if (skip) continue;
        if(!strcmp(name,"table"))append(plain,&o,"\n\x1fT\n",4,&d->truncated);
        else if(!strcmp(name,"/table"))append(plain,&o,"\n\x1f" "E\n",4,&d->truncated);
        else if(!strcmp(name,"tr"))append(plain,&o,"\n\x1fR\n",4,&d->truncated);
        else if(!strcmp(name,"td")||!strcmp(name,"th"))append(plain,&o,!strcmp(name,"th")?"\n\x1fH\n":"\n\x1f" "C\n",4,&d->truncated);
        else if(!strcmp(name,"br"))append_styled(plain,&o,"\n",1,&d->truncated,styles,current_style|4);
        else if(!strcmp(name,"p")){char align[32];attr(tag,"align",align,sizeof align);current_style=4|(!strcasecmp(align,"center")?1:0);paragraph=true;append(plain,&o,"\n\n",2,&d->truncated);}
        else if(!strcmp(name,"/p")){paragraph=false;current_style=0;append(plain,&o,"\n\n",2,&d->truncated);}
        else if (!strcmp(name, "picture") || !strcmp(name, "/picture")) chosen[0] = 0;
        else if (!strcmp(name, "source")) {
            char media[128], src[2048]; attr(tag, "media", media, sizeof media); attr(tag, "srcset", src, sizeof src);
            if (strstr(media, dark ? "dark" : "light")) snprintf(chosen, sizeof chosen, "%s", src);
        } else if (!strcmp(name, "img")) {
            char src[2048], alt[256]; attr(tag, "src", src, sizeof src); attr(tag, "alt", alt, sizeof alt);
            if (chosen[0]) snprintf(src, sizeof src, "%s", chosen);
            if (contains(src, "qr-code") || contains(src, "-qr.") || contains(alt, "qr code")) continue;
            char line[2400]; snprintf(line, sizeof line, "![%s](%s)", alt, src); append(plain, &o, line, strlen(line), &d->truncated);
        } else if(!strcmp(name,"sub"))current_style|=8;
        else if(!strcmp(name,"/sub"))current_style&=~8;
        else if(!strcmp(name,"strong")||!strcmp(name,"b"))current_style|=2;
        else if(!strcmp(name,"/strong")||!strcmp(name,"/b"))current_style&=~2;
        else if (!strcmp(name, "a")) { attr(tag, "href", href, sizeof href); if (*href) append(plain, &o, "[", 1, &d->truncated); }
        else if (!strcmp(name, "/a") && *href) { char tail[2100]; snprintf(tail, sizeof tail, "](%s)", href); append(plain, &o, tail, strlen(tail), &d->truncated); href[0] = 0; }
        else if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2]) {
            char align[32];attr(tag,"align",align,sizeof align);current_style=!strcasecmp(align,"center")?1:0;
            append(plain, &o, "\n", 1, &d->truncated); for (int h = 0; h < name[1] - '0'; h++) append(plain, &o, "#", 1, &d->truncated); append(plain, &o, " ", 1, &d->truncated);
        } else if (!strcmp(name, "pre") || !strcmp(name, "/pre")) append(plain, &o, "\n```\n", 5, &d->truncated);
        else if (!strcmp(name, "li")) append(plain, &o, "\n• ", 5, &d->truncated);
        else if (!strcmp(name, "blockquote")) append(plain, &o, "\n> ", 3, &d->truncated);
        else if (!strcmp(name, "td") || !strcmp(name, "th")) append(plain, &o, " | ", 3, &d->truncated);
        else if (!strcmp(name, "hr")) append(plain, &o, "\n---\n", 5, &d->truncated);
        else if (!strcmp(name, "br") || !strcmp(name, "/p") || !strcmp(name, "/div") || !strcmp(name, "/tr") || !strcmp(name, "/li") || (name[0] == '/' && name[1] == 'h')) append(plain, &o, "\n", 1, &d->truncated);
    }
    gh_tables_project(&plain,&styles,&o,&d->truncated,GH_DOC_BYTES);
    soft_breaks(plain,styles);
    bool code=false,header=false;char *cursor=plain;unsigned line_group=0,table=0,table_id=0,row=0,col=0,columns=0,table_start=0;unsigned char line_style=0;
    while (cursor && *cursor) {
        char *end = strchr(cursor, '\n'); if (end) *end = 0;
        char *s = cursor; if (!code) while (*s && isspace((unsigned char)*s)) s++;
        if(!code){char *tail=s+strlen(s);while(tail>s&&isspace((unsigned char)tail[-1]))*--tail=0;}
        line_group++;line_style=styles[s-plain];
        if((unsigned char)*s==31&&s[1]){
            if(s[1]=='T'){table=++table_id;row=col=columns=0;table_start=d->count;}
            else if(s[1]=='R'){row++;col=0;}
            else if(s[1]=='C'||s[1]=='H'){col++;if(col>columns)columns=col;header=s[1]=='H';}
            else if(s[1]=='E'){for(unsigned i=table_start;i<(unsigned)d->count;i++)d->nodes[i].table_cols=columns;table=0;}
            cursor=end?end+1:NULL;continue;
        }
        int line_first=d->count,list_number=0;unsigned prefix=code?0:list_prefix(s,s+strlen(s),&list_number);
        unsigned depth=(s-cursor)/2;if(depth>4)depth=4;
        if(prefix)s+=prefix;
        if (!strncmp(s, "```", 3) || !strncmp(s, "~~~", 3)) code = !code;
        else if (*s) {
            int level = 0; while (s[level] == '#' && level < 6) level++;
            GhKind kind = code ? GH_CODE : level && s[level] == ' ' ? GH_HEADING : *s == '>' ? GH_QUOTE : strchr(s, '|') ? GH_TABLE : GH_TEXT;
            if (kind == GH_HEADING) s += level + 1; else if (kind == GH_QUOTE) s++;
            if (!strcmp(s, "---") || !strcmp(s, "***")) node(d, GH_RULE, "", NULL, 0);
            else {
                if (code) node(d, GH_CODE, s, NULL, 0);
                else {
                    // Preserve every run on a line, including multiple badges/links.
                    // Linked images retain a separate navigable link after the image.
                    while (*s) {
                        char *mark = strchr(s, '[');
                        char *labelend = mark ? strstr(mark + 1, "](") : NULL;
                        char *urlend = labelend ? strchr(labelend + 2, ')') : NULL;
                        if (!mark || !labelend || !urlend) {
                            if (*s && strcmp(s,"[")) text_runs(d,kind,s,strlen(s),styles+(s-plain),level,line_group);
                            break;
                        }
                        // The outer '[' of [![image](src)](link) is not its label.
                        char *outer=mark,*nested = strchr(mark+1,'[');
                        if (nested && nested < labelend) mark=nested;
                        bool image = mark>s && mark[-1]=='!';
                        char *prefix=mark!=outer?outer:image?mark-1:mark;
                        *prefix=0;
                        if (*s && strcmp(s,"[")) text_runs(d,kind,s,strlen(s),styles+(s-plain),level,line_group);
                        *labelend=0; *urlend=0;
                        char url[2048];
                        line_style=styles[mark+1-plain];
                        if (resolve(url,sizeof url,labelend+2,repo,branch,image)) node(d,image?GH_IMAGE:GH_LINK,mark+1,url,0);
                        else node(d,kind,mark+1,NULL,level);
                        s=urlend+1;
                        if (image && !strncmp(s,"](",2)) {
                            char *close=strchr(s+2,')');
                            if (close) { *close=0; if(resolve(url,sizeof url,s+2,repo,branch,false))node(d,GH_LINK,"",url,0); s=close+1; }
                        }
                    }
                }
            }
        }
        if(prefix)for(int i=line_first;i<d->count;i++){d->nodes[i].style|=32;d->nodes[i].level=list_number;d->nodes[i].list_depth=depth;}
        if(table)for(int i=line_first;i<d->count;i++){GhNode*n=&d->nodes[i];n->table=table;n->table_row=row?row-1:0;n->table_col=col?col-1:0;n->table_header=header;}
        cursor = end ? end + 1 : NULL;
        if (d->count == GH_DOC_NODES) { d->truncated = true; break; }
    }
    if(table)for(unsigned i=table_start;i<(unsigned)d->count;i++)d->nodes[i].table_cols=columns;
    free(plain);free(styles); return true;
}

#undef append
#undef node
