/* Restricted remote SVG renderer. NanoSVG and font notices are adjacent.
 * Deliberately fail closed before invoking NanoSVG's permissive XML parser.
 * Only our validated callbacks are used, never nsvgParse() on remote XML. */
#include "remote_svg.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#define RSVG_INPUT_MAX (128u * 1024u)
#define RSVG_ARENA_MAX (2u * 1024u * 1024u)
#define RSVG_TEXT_MAX 256
#define RSVG_DEPTH_MAX 16
#define RSVG_ATTR_MAX 32
#define RSVG_NUMBER_MAX 4096

typedef union RsvgAlloc { size_t size; max_align_t alignment; } RsvgAlloc;
typedef struct RsvgArena { unsigned char *data; size_t used; jmp_buf fail; } RsvgArena;
static RsvgArena *rsvg_arena;
static atomic_flag rsvg_busy = ATOMIC_FLAG_INIT;
static void *rsvg_alloc(size_t size) {
    size_t align = sizeof(RsvgAlloc), total;
    if (size > RSVG_ARENA_MAX - align) longjmp(rsvg_arena->fail, 1);
    total = ((size + align - 1) / align + 1) * align;
    if (total > RSVG_ARENA_MAX - rsvg_arena->used) longjmp(rsvg_arena->fail, 1);
    RsvgAlloc *p = (RsvgAlloc *)(rsvg_arena->data + rsvg_arena->used);
    p->size = size; rsvg_arena->used += total;
    return p + 1;
}
static void *rsvg_realloc(void *ptr, size_t size) {
    void *p = rsvg_alloc(size);
    if (ptr) { size_t old = ((RsvgAlloc *)ptr)[-1].size; memcpy(p, ptr, old < size ? old : size); }
    return p;
}
static void rsvg_free(void *p) { (void)p; }
/* Bump arena bounds all NanoSVG geometry/raster allocations, including failure
 * paths that upstream otherwise silently ignores. One cleanup frees the arena. */
#define malloc rsvg_alloc
#define realloc rsvg_realloc
#define free rsvg_free
#define NANOSVG_ALL_COLOR_KEYWORDS
#define NANOSVG_IMPLEMENTATION
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
/* Upstream nsvgParseFromFile compares size_t with -1L. We never call it. */
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "nanosvg.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#undef malloc
#undef realloc
#undef free
#undef NANOSVG_IMPLEMENTATION
#undef NANOSVGRAST_IMPLEMENTATION
#undef NANOSVG_ALL_COLOR_KEYWORDS
#include "remote_svg_font.inc"
#include "remote_svg_sans_font.inc"

typedef struct RsvgText { char *value; int anchor; bool sans, clipped; } RsvgText;
typedef struct RsvgFrame { char *name; float font_size; int anchor; bool sans, clipped; } RsvgFrame;
typedef struct RsvgContext {
    RsvgArena arena;
    char *xml;
    NSVGparser *parser;
    RsvgText texts[RSVG_TEXT_MAX];
    RsvgFrame stack[RSVG_DEPTH_MAX];
    unsigned elements, numbers, chars, text_count;
    int depth, root_closed;
    FT_Library ft;
    FT_Face face, sans_face;
    char clip_id[64];float clip_w,clip_h,clip_r;
    bool in_clip,have_clip;
    unsigned char *canvas, *layer, *output;
    int width, height;
} RsvgContext;
static int rsvg_ws(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static char *rsvg_skip(char *p) { while (rsvg_ws((unsigned char)*p)) ++p; return p; }
static int rsvg_name(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ':'; }
static int rsvg_is(const char *s, const char *list) {
    size_t n = strlen(s);
    while (*list) { const char *end = strchr(list, '|'); size_t m = end ? (size_t)(end-list) : strlen(list);
        if (m == n && !memcmp(s, list, n)) return 1;
        if (!end) break;
        list = end + 1;
    } return 0;
}
static int rsvg_num(char **p, float *out) {
    char *end; double d;
    if (!(**p == '+' || **p == '-' || **p == '.' || (**p >= '0' && **p <= '9'))) return 0;
    d = strtod(*p, &end);
    if (end == *p || !isfinite(d) || fabs(d) > 8192 || end-*p > 40) return 0;
    *p = end; *out = (float)d; return 1;
}
static int rsvg_scalar(char *p, float *out) {
    p = rsvg_skip(p);
    if (!rsvg_num(&p, out)) return 0;
    if (p[0] == 'p' && p[1] == 'x') p += 2;
    return !*rsvg_skip(p);
}
static int rsvg_numbers(RsvgContext *c, char *p, float *v, int max) {
    int n = 0;
    while (*(p = rsvg_skip(p))) {
        if (n == max || ++c->numbers > RSVG_NUMBER_MAX || !rsvg_num(&p, &v[n++])) return -1;
        p = rsvg_skip(p); if (*p == ',') { p = rsvg_skip(p + 1); if (!*p) return -1; }
    } return n;
}
static int rsvg_color(char *p) {
    if (!strcmp(p, "none")) return 1;
    if (*p == '#') { size_t n = strlen(++p); if (n != 3 && n != 6) return 0;
        while (*p) if (!isxdigit((unsigned char)*p++)) return 0;
        return 1;
    }
    if (!strncmp(p, "rgb(", 4)) {
        p += 4; int percentage = -1;
        for (int i = 0; i < 3; ++i) { float x; p = rsvg_skip(p); if (!rsvg_num(&p,&x) || x < 0) return 0;
            int pct = *p == '%'; if (pct) ++p;
            if (x > (pct ? 100 : 255) || (percentage != -1 && pct != percentage)) return 0;
            percentage = pct; p = rsvg_skip(p); if (*p++ != (i == 2 ? ')' : ',')) return 0;
        } return !*p;
    }
    for (size_t i=0; i < sizeof(nsvg__colors)/sizeof(nsvg__colors[0]); ++i)
        if (!strcmp(p, nsvg__colors[i].name)) return 1;
    return 0;
}
static int rsvg_transform(RsvgContext *c, char *p) {
    unsigned count = 0;
    while (*(p = rsvg_skip(p))) {
        int scale = !strncmp(p, "scale", 5), len = scale ? 5 : 9;
        if ((!scale && strncmp(p,"translate",9)) || ++count > 8) return 0;
        p = rsvg_skip(p + len); if (*p++ != '(') return 0;
        char *end = strchr(p,')'); if (!end) return 0;
        char save = *end; *end = 0; float v[2]; int n = rsvg_numbers(c,p,v,2); *end = save;
        if (n < 1 || (scale && (v[0] < 0.01f || v[0] > 100 || (n == 2 && v[0] != v[1])))) return 0;
        p = end + 1;
    } return count > 0;
}
static int rsvg_path(RsvgContext *c, char *p) {
    int command = 0, first = 1, count = 0;
    while (*(p = rsvg_skip(p))) {
        if (isalpha((unsigned char)*p)) { command = *p++; count = 0;
            if (!strchr("MmLlHhVvCcSsQqTtZz", command) || (first && command != 'M' && command != 'm')) return 0;
            first = 0; if (command == 'Z' || command == 'z') { command = 0; continue; }
        }
        if (!command) return 0;
        int arity = (command == 'H' || command == 'h' || command == 'V' || command == 'v') ? 1 :
            (command == 'C' || command == 'c') ? 6 : strchr("SsQq",command) ? 4 : 2;
        for (int i=0; i<arity; ++i) { float x; p = rsvg_skip(p);
            if (++c->numbers > RSVG_NUMBER_MAX || !rsvg_num(&p,&x)) return 0;
            p = rsvg_skip(p); if (*p == ',') { ++p; if (!*rsvg_skip(p) || isalpha((unsigned char)*rsvg_skip(p))) return 0; }
        } ++count;
    } return !first && (count || !command);
}
/* Only the five XML entities and printable ASCII numeric references are valid
 * in rendered text. No entity declarations, expansion, or external resources. */
static int rsvg_text_decode(RsvgContext *c, char *s) {
    char *out = s; size_t count = 0;
    while (*s) {
        unsigned x = (unsigned char)*s++;
        if (x == '&') {
            char *end = strchr(s,';'); if (!end || end-s > 12) return 0;
            if (!strncmp(s,"amp;",4)) x='&'; else if (!strncmp(s,"lt;",3)) x='<';
            else if (!strncmp(s,"gt;",3)) x='>'; else if (!strncmp(s,"quot;",5)) x='"';
            else if (!strncmp(s,"apos;",5)) x='\'';
            else if (*s == '#') {
                char *num = s+1, *tail; int base=10;
                if (*num == 'x') { ++num; base=16; }
                if (num == end || !isxdigit((unsigned char)*num)) return 0;
                unsigned long v = strtoul(num,&tail,base);
                if (tail != end || v < 32 || v > 126) return 0;
                x=(unsigned)v;
            } else return 0;
            s=end+1;
        }
        if (x < 32 || x > 126 || ++count > 1024 || ++c->chars > 16384) return 0;
        *out++=(char)x;
    } *out=0; return 1;
}
static int rsvg_css(char *p) {
    p=rsvg_skip(p); if (strncmp(p,"text",4)) return 0;
    p=rsvg_skip(p+4); if (*p++ != '{') return 0;
    while (*(p=rsvg_skip(p)) != '}') {
        char *colon=strchr(p,':'); if (!colon) return 0;
        char *end=colon; while(end>p && rsvg_ws(end[-1])) --end; *end=0;
        char *val=rsvg_skip(colon+1), *semi=strchr(val,';'); if(!semi) return 0;
        end=semi; while(end>val && rsvg_ws(end[-1])) --end; *end=0;
        if (!strcmp(p,"font-family")) { if (!strstr(val,"monospace")) return 0;
            for(char *s=val;*s;++s) if (!(isalnum((unsigned char)*s) || strchr(" \t,\"'-",*s))) return 0;
        } else if (!strcmp(p,"white-space")) { if(strcmp(val,"pre")) return 0; }
        else if (!strcmp(p,"font-variant-ligatures")) { if(strcmp(val,"none")) return 0; }
        else return 0;
        p=semi+1;
    } return !*rsvg_skip(p+1);
}
static int rsvg_attribute(RsvgContext *c, const char *tag, char *key, char *v, RsvgFrame *f) {
    float x;
    if (!strcmp(key,"id")) {if(strlen(v)>=64||!strncmp(v,"__rsvg",6)||!strncmp(v,"__rclip",7))return 0;if(!strcmp(tag,"clipPath"))strcpy(c->clip_id,v);return 1;}
    if (rsvg_is(key,"role|aria-label|aria-labelledby")) return 1;
    if (!strcmp(key,"xmlns")) return !strcmp(tag,"svg") && !strcmp(v,"http://www.w3.org/2000/svg");
    if (!strcmp(key,"xml:space")) return !strcmp(v,"preserve");
    if (!strcmp(key,"font-family")) {
        if(strlen(v)>128)return 0;
        for(char *p=v;*p;p++)if(!isalnum((unsigned char)*p)&&!strchr(" ,_'\"-",*p))return 0;
        if(strstr(v,"monospace")){f->sans=false;return 1;}
        if(strstr(v,"sans-serif")||strstr(v,"Arial")||strstr(v,"Verdana")||strstr(v,"Inter")){f->sans=true;return 1;}
        return 0;
    }
    if (!strcmp(key,"clip-path")) {char expected[80];snprintf(expected,sizeof expected,"url(#%s)",c->clip_id);if(!c->have_clip||strcmp(tag,"g")||strcmp(v,expected))return 0;f->clipped=true;return 1;}
    if (!strcmp(key,"font-size")) { if(!rsvg_scalar(v,&x) || x<1 || x>256) return 0; f->font_size=x; return 1; }
    if (!strcmp(key,"text-anchor")) { if(!rsvg_is(v,"start|middle|end")) return 0; f->anchor=!strcmp(v,"end")?2:!strcmp(v,"middle"); return 1; }
    if (rsvg_is(key,"fill|stroke")) return rsvg_color(v) && (strcmp(tag,"text") || strcmp(key,"stroke") || !strcmp(v,"none"));
    if (!strcmp(key,"transform")) return rsvg_transform(c,v);
    /* Group opacity requires an offscreen group composite, so reject it. */
    if (rsvg_is(key,"opacity|fill-opacity|stroke-opacity")) {
        if (!rsvg_scalar(v,&x) || x<0 || x>1) return 0;
        return strcmp(key,"opacity") || !rsvg_is(tag,"g|svg") || x==1;
    }
    if (!strcmp(key,"display")) return rsvg_is(v,"none|inline");
    if (!strcmp(key,"fill-rule")) return rsvg_is(v,"nonzero|evenodd");
    if (!strcmp(key,"stroke-linecap")) return rsvg_is(v,"butt|round|square");
    if (!strcmp(key,"stroke-linejoin")) return rsvg_is(v,"miter|round|bevel");
    if (!strcmp(key,"stroke-miterlimit")) return rsvg_scalar(v,&x) && x>=1 && x<=10;
    if (!strcmp(key,"stroke-width")) return rsvg_scalar(v,&x) && x>=0 && x<=128;
    if (!strcmp(key,"stroke-dashoffset")) return rsvg_scalar(v,&x);
    if (!strcmp(key,"stroke-dasharray")) { float d[8]; int n; if(!strcmp(v,"none")) return 1;
        n=rsvg_numbers(c,v,d,8); if(n<1) return 0; for(int i=0;i<n;++i) if(d[i]<1) return 0; return 1; }
    if (!strcmp(key,"viewBox")) { float b[4]; return !strcmp(tag,"svg") && rsvg_numbers(c,v,b,4)==4 && b[2]>=1 && b[3]>=1; }
    if (!strcmp(key,"preserveAspectRatio")) return !strcmp(v,"xMidYMid meet");
    if (!strcmp(key,"d")) return !strcmp(tag,"path") && rsvg_path(c,v);
    if (!strcmp(key,"points")) { float values[512]; int n=rsvg_numbers(c,v,values,512); return rsvg_is(tag,"polyline|polygon") && n>=4 && !(n%2); }
    if (rsvg_is(key,"x|y|width|height|rx|ry|cx|cy|r|x1|y1|x2|y2")) {
        const char *allowed = !strcmp(tag,"svg") ? "width|height" :
            !strcmp(tag,"rect") ? "x|y|width|height|rx|ry" :
            !strcmp(tag,"text") ? "x|y" :
            !strcmp(tag,"circle") ? "cx|cy|r" :
            !strcmp(tag,"ellipse") ? "cx|cy|rx|ry" :
            !strcmp(tag,"line") ? "x1|y1|x2|y2" : "";
        if (!rsvg_is(key,allowed)) return 0;
        if (!rsvg_scalar(v,&x)) return 0;
        if (rsvg_is(key,"width|height|rx|ry|r") && x<0) return 0;
        if (!strcmp(tag,"svg") && rsvg_is(key,"width|height") && (x<1 || x>4096)) return 0;
        return 1;
    }
    return 0;
}
static int rsvg_parse(RsvgContext *c) {
    char *p=c->xml; int root_seen=0;
    while (*(p=rsvg_skip(p))) {
        if (!strncmp(p,"<!--",4)) { char *end=strstr(p+4,"-->"); if(!end) return 0; p=end+3; continue; }
        if (*p++!='<') return 0;
        int close=*p=='/'; if(close) ++p;
        char *name=p; while(rsvg_name((unsigned char)*p)) ++p;
        if(p==name || p-name>32 || !*p) return 0;
        char delim=*p; *p++=0;
        if(close) {
            if(delim!='>') { if(!rsvg_ws(delim)) return 0; p=rsvg_skip(p); if(*p++!='>') return 0; }
            if(!c->depth || strcmp(c->stack[c->depth-1].name,name)) return 0;
            if(!strcmp(name,"clipPath"))c->in_clip=false;
            else nsvg__endElement(c->parser,name);
            --c->depth;
            if(!c->depth) c->root_closed=1;
            continue;
        }
        if(++c->elements>512 || c->depth>=RSVG_DEPTH_MAX || c->root_closed) return 0;
        if (!rsvg_is(name,"svg|g|rect|circle|ellipse|line|polyline|polygon|path|text|title|desc|style|clipPath")) return 0;
        if(c->in_clip&&strcmp(name,"rect"))return 0;
        if(!strcmp(name,"svg")) { if(root_seen || c->depth) return 0; root_seen=1; }
        else if(!c->depth) return 0;
        RsvgFrame f=c->depth?c->stack[c->depth-1]:(RsvgFrame){.font_size=12}; f.name=name;
        const char *attrs[RSVG_ATTR_MAX*2+9]; int n=0, self=0, has_width=0, has_height=0;
        if(delim=='/') { if(*p++!='>') return 0; self=1; }
        else if(delim!='>') {
            if(!rsvg_ws(delim)) return 0;
            for(;;) {
                p=rsvg_skip(p); if(*p=='>') { ++p; break; }
                if(*p=='/' && p[1]=='>') { p+=2; self=1; break; }
                if(n==RSVG_ATTR_MAX*2) return 0;
                char *key=p; while(rsvg_name((unsigned char)*p)) ++p;
                if(p==key || p-key>40 || !*p) return 0;
                char d=*p; *p++=0; if(d!='=') { if(!rsvg_ws(d)) return 0; p=rsvg_skip(p); if(*p++!='=') return 0; }
                p=rsvg_skip(p); int quote=*p++; if(quote!='\'' && quote!='"') return 0;
                char *val=p; while(*p && *p!=quote) { if((unsigned char)*p<32 || (unsigned char)*p>126 || *p=='<' || *p=='&') return 0; ++p; }
                if(!*p || p-val>32768) return 0;
                *p++=0;
                for(int i=0;i<n;i+=2) if(!strcmp(attrs[i],key)) return 0;
                if(!rsvg_attribute(c,name,key,val,&f)) return 0;
                attrs[n++]=key; attrs[n++]=val;
                has_width |= !strcmp(key,"width"); has_height |= !strcmp(key,"height");
            }
        }
        attrs[n]=NULL;
        if(!strcmp(name,"clipPath")){
            if(self||c->have_clip||!c->clip_id[0]||n!=2||strcmp(attrs[0],"id"))return 0;
            c->in_clip=true;c->stack[c->depth++]=f;continue;
        }
        if(c->in_clip){
            if(!self||c->have_clip)return 0;
            float width=0,height=0,radius=0;
            for(int i=0;i<n;i+=2){float value;if(!rsvg_scalar((char *)attrs[i+1],&value))return 0;
                if(!strcmp(attrs[i],"width"))width=value;else if(!strcmp(attrs[i],"height"))height=value;
                else if(!strcmp(attrs[i],"rx"))radius=value;else if(!strcmp(attrs[i],"x")||!strcmp(attrs[i],"y")){if(value!=0)return 0;}else return 0;
            }
            if(width!=c->parser->image->width||height!=c->parser->image->height||radius<0||radius>fminf(width,height)/2)return 0;
            c->clip_w=width;c->clip_h=height;c->clip_r=radius;c->have_clip=true;continue;
        }
        if(!strcmp(name,"svg") && (!has_width || !has_height)) return 0;
        if(rsvg_is(name,"title|desc|style|text")) {
            if(self) return 0;
            char *body=p, *end=strchr(p,'<'); if(!end) return 0; *end=0;
            char *tail=end+1; if(*tail++!='/') return 0;
            size_t len=strlen(name); if(strncmp(tail,name,len)) return 0;
            tail=rsvg_skip(tail+len); if(*tail++!='>') return 0;
            if(!strcmp(name,"style")) { if(!rsvg_css(body)) return 0; }
            else if(!strcmp(name,"text")) {
                if(c->text_count==RSVG_TEXT_MAX || !rsvg_text_decode(c,body)) return 0;
                unsigned index=c->text_count++;
                c->texts[index]=(RsvgText){body,f.anchor,f.sans,f.clipped};
                char id[32], fs[32]; snprintf(id,sizeof(id),"__rsvg%u",index); snprintf(fs,sizeof(fs),"%.6f",f.font_size);
                /* Marker geometry carries exactly the same paint, viewBox and
                 * translate/uniform-scale as a shape. Height encodes font size. */
                const char *a[RSVG_ATTR_MAX*2+9]; int k=0;
                for(int i=0;i<n;i+=2) if(!rsvg_is(attrs[i],"id|width|height|rx|ry")) { a[k++]=attrs[i]; a[k++]=attrs[i+1]; }
                a[k++]="id"; a[k++]=id; a[k++]="width"; a[k++]=fs; a[k++]="height"; a[k++]=fs; a[k]=NULL;
                nsvg__startElement(c->parser,"rect",a); nsvg__endElement(c->parser,"rect");
            }
            p=tail; continue;
        }
        if(!self && !rsvg_is(name,"svg|g")) return 0;
        if(f.clipped&&rsvg_is(name,"rect|circle|ellipse|line|polyline|polygon|path")){
            const char *clipped[RSVG_ATTR_MAX*2+9];int k=0;for(int i=0;i<n;i+=2)if(strcmp(attrs[i],"id")){clipped[k++]=attrs[i];clipped[k++]=attrs[i+1];}clipped[k++]="id";clipped[k++]="__rclip";clipped[k]=NULL;nsvg__startElement(c->parser,name,clipped);
        }else nsvg__startElement(c->parser,name,attrs);
        if(self) { nsvg__endElement(c->parser,name); if(!strcmp(name,"svg")) c->root_closed=1; }
        else c->stack[c->depth++]=f;
    }
    if(!root_seen || !c->root_closed || c->depth) return 0;
    nsvg__scaleToViewbox(c->parser,"px");
    return 1;
}
/* Source-over, straight-alpha pixels, with a premultiplied intermediate. */
static bool rsvg_inside_clip(const RsvgContext *c,int x,int y,float scale){
    float px=(x+.5f)/scale,py=(y+.5f)/scale,r=c->clip_r;
    if(px<0||py<0||px>=c->clip_w||py>=c->clip_h)return false;
    float dx=px<r?r-px:px>c->clip_w-r?px-(c->clip_w-r):0;
    float dy=py<r?r-py:py>c->clip_h-r?py-(c->clip_h-r):0;
    return dx*dx+dy*dy<=r*r;
}
static void rsvg_blend(unsigned char *d, unsigned r, unsigned g, unsigned b, unsigned a) {
    unsigned da=d[3], oa=a*255+da*(255-a);
    if(!oa) return;
    d[0]=(unsigned char)((r*a*255+d[0]*da*(255-a)+oa/2)/oa);
    d[1]=(unsigned char)((g*a*255+d[1]*da*(255-a)+oa/2)/oa);
    d[2]=(unsigned char)((b*a*255+d[2]*da*(255-a)+oa/2)/oa);
    d[3]=(unsigned char)((oa+127)/255);
}
static int rsvg_draw_text(RsvgContext *c, NSVGshape *s, float scale, int w, int h) {
    char *end; unsigned long index=strtoul(s->id+6,&end,10);
    if(*end || index>=c->text_count || s->stroke.type!=NSVG_PAINT_NONE) return 0;
    if(s->fill.type==NSVG_PAINT_NONE || !(s->flags&NSVG_FLAGS_VISIBLE)) return 1;
    float pixels=(s->bounds[3]-s->bounds[1])*scale;
    if(!isfinite(pixels) || pixels<=0 || pixels>256) return 0;
    RsvgText *text=&c->texts[index];FT_Face face=text->sans?c->sans_face:c->face;
    if(!face||FT_Set_Char_Size(face,0,(FT_F26Dot6)lroundf(pixels*64),72,72)) return 0;
    FT_Pos advance=0;
    for(unsigned char *p=(unsigned char *)text->value;*p;++p) {
        if(FT_Load_Char(face,*p,FT_LOAD_NO_HINTING|FT_LOAD_NO_BITMAP)) return 0;
        advance+=face->glyph->advance.x;
    }
    FT_Pos pen=(FT_Pos)lroundf(s->bounds[0]*scale*64)-advance*text->anchor/2;
    FT_Pos baseline=(FT_Pos)lroundf(s->bounds[1]*scale*64);
    unsigned color=s->fill.color, alpha=(unsigned)lroundf((color>>24)*s->opacity);
    for(unsigned char *p=(unsigned char *)text->value;*p;++p) {
        FT_Vector delta={pen%64,-(baseline%64)};
        FT_Set_Transform(face,NULL,&delta);
        if(FT_Load_Char(face,*p,FT_LOAD_NO_HINTING|FT_LOAD_NO_BITMAP) || FT_Render_Glyph(face->glyph,FT_RENDER_MODE_NORMAL)) return 0;
        FT_GlyphSlot g=face->glyph; FT_Bitmap *bm=&g->bitmap;
        if(bm->pixel_mode!=FT_PIXEL_MODE_GRAY || bm->pitch<0) return 0;
        int left=(int)(pen/64)+g->bitmap_left, top=(int)(baseline/64)-g->bitmap_top;
        for(unsigned y=0;y<bm->rows;++y) for(unsigned x=0;x<bm->width;++x) {
            int dx=left+(int)x, dy=top+(int)y;
            if(dx>=0 && dy>=0 && dx<w && dy<h&&(!text->clipped||rsvg_inside_clip(c,dx,dy,scale))) rsvg_blend(c->canvas+((size_t)dy*w+dx)*4,color&255,(color>>8)&255,(color>>16)&255,(bm->buffer[y*bm->pitch+x]*alpha+127)/255);
        }
        pen+=g->advance.x;
    }
    FT_Set_Transform(face,NULL,NULL); return 1;
}
static int rsvg_render(RsvgContext *c) {
    NSVGimage *img=c->parser->image;
    if(!isfinite(img->width) || !isfinite(img->height) || img->width<1 || img->height<1 || img->width>4096 || img->height>4096) return 0;
    float scale=fminf(1.0f,fminf(376.0f/img->width,240.0f/img->height));
    c->width=(int)floorf(img->width*scale+0.0001f); c->height=(int)floorf(img->height*scale+0.0001f);
    if(c->width<1 || c->height<1) return 0;
    int w=c->width*2, h=c->height*2; scale*=2;
    size_t bytes=(size_t)w*h*4;
    c->canvas=calloc(1,bytes); c->layer=malloc(bytes); c->output=malloc(bytes/4);
    if(!c->canvas || !c->layer || !c->output) return 0;
    if(c->text_count && (FT_Init_FreeType(&c->ft) || FT_New_Memory_Face(c->ft,remote_svg_font,sizeof(remote_svg_font),0,&c->face))) return 0;
    for(unsigned i=0;i<c->text_count;i++)if(c->texts[i].sans){if(FT_New_Memory_Face(c->ft,remote_svg_sans_font,sizeof(remote_svg_sans_font),0,&c->sans_face))return 0;break;}
    NSVGrasterizer *rast=nsvgCreateRasterizer(); if(!rast) return 0;
    unsigned points=0;
    for(NSVGshape *s=img->shapes;s;s=s->next) {
        if(s->fill.type>NSVG_PAINT_COLOR || s->stroke.type>NSVG_PAINT_COLOR || !isfinite(s->strokeWidth) || s->strokeWidth*scale>256) return 0;
        for(int i=0;i<4;++i) if(!isfinite(s->bounds[i]) || fabsf(s->bounds[i]*scale)>4096) return 0;
        for(NSVGpath *path=s->paths;path;path=path->next) {
            if(path->npts<1 || (points+=(unsigned)path->npts)>8192) return 0;
            for(int i=0;i<path->npts*2;++i) if(!isfinite(path->pts[i]) || fabsf(path->pts[i]*scale)>4096) return 0;
        }
        for(int i=0;i<s->strokeDashCount;++i) if(s->strokeDashArray[i]*scale<0.5f) return 0;
        if(!strncmp(s->id,"__rsvg",6)) { if(!rsvg_draw_text(c,s,scale,w,h)) return 0; }
        else {
            NSVGshape copy=*s; copy.next=NULL; NSVGimage layer=*img; layer.shapes=&copy;
            nsvgRasterize(rast,&layer,0,0,scale,c->layer,w,h,w*4);
            bool clip=!strcmp(s->id,"__rclip");
            for(size_t i=0;i<bytes;i+=4) if(c->layer[i+3]&&(!clip||rsvg_inside_clip(c,(i/4)%w,(i/4)/w,scale))) rsvg_blend(c->canvas+i,c->layer[i],c->layer[i+1],c->layer[i+2],c->layer[i+3]);
        }
    }
    for(int y=0;y<c->height;++y) for(int x=0;x<c->width;++x) {
        unsigned a=0, rgb[3]={0}; unsigned char *dst=c->output+((size_t)y*c->width+x)*4;
        for(int yy=0;yy<2;++yy) for(int xx=0;xx<2;++xx) {
            unsigned char *src=c->canvas+((size_t)(y*2+yy)*w+x*2+xx)*4;
            a+=src[3]; for(int k=0;k<3;++k) rgb[k]+=src[k]*src[3];
        }
        dst[3]=(unsigned char)((a+2)/4); for(int k=0;k<3;++k) dst[k]=a?(unsigned char)((rgb[k]+a/2)/a):0;
    }
    return 1;
}
bool remote_svg_decode(const unsigned char *data, size_t size, unsigned char **rgba, int *width, int *height) {
    if(rgba) *rgba=NULL;
    if(width) *width=0;
    if(height) *height=0;
    if(!data || !rgba || !width || !height || !size || size>RSVG_INPUT_MAX || memchr(data,0,size)) return false;
    if(atomic_flag_test_and_set_explicit(&rsvg_busy,memory_order_acquire)) return false;
    RsvgContext *c=calloc(1,sizeof(*c)); bool ok=false;
    if(!c) goto unlock;
    c->xml=malloc(size+1); c->arena.data=malloc(RSVG_ARENA_MAX);
    if(!c->xml || !c->arena.data) goto done;
    memcpy(c->xml,data,size); c->xml[size]=0; rsvg_arena=&c->arena;
    if(setjmp(c->arena.fail)) goto done;
    c->parser=nsvg__createParser();
    if(!c->parser || !rsvg_parse(c) || !rsvg_render(c)) goto done;
    *rgba=c->output; c->output=NULL; *width=c->width; *height=c->height; ok=true;
done:
    if(c->sans_face) FT_Done_Face(c->sans_face);
    if(c->face) FT_Done_Face(c->face);
    if(c->ft) FT_Done_FreeType(c->ft);
    free(c->output); free(c->layer); free(c->canvas); free(c->xml); free(c->arena.data); free(c); rsvg_arena=NULL;
unlock:
    atomic_flag_clear_explicit(&rsvg_busy,memory_order_release); return ok;
}
