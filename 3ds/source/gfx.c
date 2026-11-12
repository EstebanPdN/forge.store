// Textures, background loading, shapes and cached system-font text.
#include "gfx.h"
#include <zlib.h>

Sheet UI;
static json_t *ui_names;

#include "texture_stream.inc"

// ---- UI atlas ----------------------------------------------------------------------------
// Name -> index, hashed once: icons are looked up many times per frame.
#define UI_SLOTS 256
static struct {
    const char *name;
    int index;
} ui_map[UI_SLOTS];

static u32 name_hash(const char *s) {
    u32 h = 2166136261u;
    while (*s) h = (h ^ (u8)*s++) * 16777619u;
    return h;
}

static void ui_map_build(void) {
    size_t i;
    json_t *v;
    json_array_foreach(ui_names, i, v) {
        const char *n = json_string_value(v);
        u32 k = name_hash(n) % UI_SLOTS;
        while (ui_map[k].name) k = (k + 1) % UI_SLOTS;
        ui_map[k].name = n;
        ui_map[k].index = (int)i;
    }
}

int ui_index(const char *name) {
    for (u32 k = name_hash(name) % UI_SLOTS; ui_map[k].name; k = (k + 1) % UI_SLOTS)
        if (!strcmp(ui_map[k].name, name)) return ui_map[k].index;
    return -1;
}

C2D_Image ui_img(const char *name) {
    int i = ui_index(name);
    return sheet_img(&UI, i < 0 ? 0 : i);
}

// ---- text: the prototype's fonts, baked per pixel size (render-fonts.mjs) -------------------------
// FNT3 stores explicit V2 line boxes; glyph coverage comes from offline FreeType.
typedef struct __attribute__((packed)) {
    u32 cp;
    u16 x, y;
    u8 w, h;
    s8 ox, oy;
    u16 adv;  // 1/64 px
    u8 page, ink_insets; // left/right low-coverage fringe; two 4-bit insets
} Glyph;

typedef struct {
    u8 kind, family;  // kind 0 font, 1 icon
    u16 weight;
    float size, ascent, descent;
    u32 first, count;
    int line, base;  // line box height and baseline, whole pixels
    float mono_advance;
    int mono_cell;
    s32 ascii[96];
} Style;

#define MAX_STYLES 96
#define MAX_GLYPH_PAGES 16
static Style styles[MAX_STYLES];
static int nstyles, npages;
static Glyph *glyphs;
typedef struct __attribute__((packed)) {u32 left,right;s16 delta;} KernPair;
static KernPair *kerning[96];
static u32 kern_count[96];
static void kerning_free(void){for(int i=0;i<96;i++){free(kerning[i]);kerning[i]=NULL;kern_count[i]=0;}}
static void kerning_load(int count){
    kerning_free();FILE*f=fopen("romfs:/gfx/kerning.bin","rb");if(!f)return;
    char magic[4];u16 n=0;
    bool ok=fread(magic,1,4,f)==4&&!memcmp(magic,"KRN1",4)&&fread(&n,2,1,f)==1&&n==count;
    for(int i=0;ok&&i<count;i++){
        u32 k=0;ok=fread(&k,4,1,f)==1&&k<=100000;
        if(!ok)break;
        if(k){kerning[i]=malloc(k*sizeof(KernPair));ok=kerning[i]&&fread(kerning[i],sizeof(KernPair),k,f)==k;}
        kern_count[i]=k;
        for(u32 j=1;ok&&j<k;j++){KernPair*a=&kerning[i][j-1],*b=&kerning[i][j];if(a->left>b->left||(a->left==b->left&&a->right>=b->right))ok=false;}
    }
    fclose(f);if(!ok)kerning_free();
}
static Sheet glyph_pages[MAX_GLYPH_PAGES];
static struct {
    const char *name;
    int index;
} icon_map[128];
static char icon_names[64][20];

/* Decode one scalar without reading past NUL or accepting malformed UTF-8. */
static int utf8(const char *s, u32 *cp) {
    const u8 *u = (const u8 *)s;
    *cp = 0xfffd;
    if (u[0] < 0x80) { *cp = u[0]; return 1; }
    int n = u[0] >= 0xc2 && u[0] <= 0xdf ? 2 :
            u[0] >= 0xe0 && u[0] <= 0xef ? 3 :
            u[0] >= 0xf0 && u[0] <= 0xf4 ? 4 : 0;
    if (!n) return 1;
    u32 c = u[0] & (0x7f >> n);
    for (int i = 1; i < n; i++) {
        if (!u[i] || (u[i] & 0xc0) != 0x80) return 1;
        c = (c << 6) | (u[i] & 63);
    }
    if ((n == 3 && c < 0x800) || (n == 4 && c < 0x10000) ||
        (c >= 0xd800 && c <= 0xdfff) || c > 0x10ffff) return 1;
    *cp = c;
    return n;
}

static bool fonts_load(void) {
    FILE *f = fopen("romfs:/gfx/fonts.bin", "rb");
    if (!f) return false;
    struct __attribute__((packed)) {
        char magic[4];
        u16 nstyles, npages;
        u32 nglyphs;
    } head = {0};
    struct __attribute__((packed)) {
        u8 kind, family;
        u16 weight;
        float size, ascent, descent, line;
        u32 first, count;
    } fs;
    bool ok = false;
    if (fread(&head, sizeof head, 1, f) != 1 || memcmp(head.magic, "FNT3", 4) ||
        !head.nstyles || head.nstyles > MAX_STYLES || !head.npages ||
        head.npages > MAX_GLYPH_PAGES || !head.nglyphs || head.nglyphs > 200000) goto done;
    for (int i = 0; i < head.nstyles; i++) {
        if (fread(&fs, sizeof fs, 1, f) != 1 || fs.kind > 1 ||
            fs.first > head.nglyphs || fs.count > head.nglyphs - fs.first ||
            !isfinite(fs.size) || fs.size <= 0 || fs.size > 64 ||
            !isfinite(fs.ascent) || fs.ascent < 0 || fs.ascent > 128 ||
            !isfinite(fs.descent) || fs.descent < 0 || fs.descent > 128 ||
            !isfinite(fs.line) || fs.line < 1 || fs.line > 128) goto done;
        Style *st = &styles[i];
        st->kind = fs.kind, st->family = fs.family, st->weight = fs.weight;
        st->size = fs.size, st->ascent = fs.ascent, st->descent = fs.descent;
        st->first = fs.first, st->count = fs.count;
        st->line = (int)floorf(fs.line + 0.5f);
        st->base = (int)floorf((fs.line - fs.ascent - fs.descent) / 2 + fs.ascent + 0.5f);
        for (int c = 0; c < 96; c++) st->ascii[c] = -1;
    }
    glyphs = malloc(head.nglyphs * sizeof(Glyph));
    if (!glyphs || fread(glyphs, sizeof(Glyph), head.nglyphs, f) != head.nglyphs) goto done;
    for (u32 g = 0; g < head.nglyphs; g++) {
        const Glyph *v = &glyphs[g];
        if (v->page >= head.npages || (v->w && (v->ink_insets & 15) + (v->ink_insets >> 4) >= v->w)) goto done;
    }
    for (int i = 0; i < head.nstyles; i++) {
        Style *st = &styles[i];
        int left = 0, right = 0;
        st->mono_advance = 0;
        for (u32 g = st->first; g < st->first + st->count; g++) {
            if (g > st->first && glyphs[g-1].cp >= glyphs[g].cp) goto done;
            if (st->kind == 0 && glyphs[g].cp >= 32 && glyphs[g].cp < 128) {
                st->ascii[glyphs[g].cp - 32] = (s32)g;
                const Glyph *v = &glyphs[g];
                if (v->cp == '0') st->mono_advance = v->adv / 64.f;
                if (v->w) {
                    int l = v->ox + (v->ink_insets & 15), r = v->ox + v->w - (v->ink_insets >> 4);
                    if (l < left) left = l;
                    if (r > right) right = r;
                }
            }
        }
        st->mono_cell = (int)fmaxf(ceilf(st->mono_advance), right - left + 1);
    }
    u16 nicons = 0;
    if (fread(&nicons, 2, 1, f) != 1 || nicons > 64) goto done;
    for (int i = 0; i < nicons; i++) {
        u8 len = 0;
        if (fread(&len, 1, 1, f) != 1 || !len || len >= sizeof icon_names[i] ||
            fread(icon_names[i], 1, len, f) != len) goto done;
        icon_names[i][len] = 0;
        u32 k = name_hash(icon_names[i]) % 128;
        while (icon_map[k].name) k = (k + 1) % 128;
        icon_map[k].name = icon_names[i], icon_map[k].index = i;
    }
    npages = head.npages;
    for (int i = 0; i < npages; i++) {
        char path[48];
        snprintf(path, sizeof path, "romfs:/gfx/glyphs-%d.ftx", i);
        glyph_pages[i].crisp = true;
        if (!sheet_load_now(&glyph_pages[i], path)) goto done;
    }
    for (u32 i = 0; i < head.nglyphs; i++) {
        const Glyph *g = &glyphs[i];
        if (g->x + g->w > glyph_pages[g->page].w || g->y + g->h > glyph_pages[g->page].h) goto done;
    }
    nstyles = head.nstyles;
    kerning_load(nstyles);
    ok = true;
done:
    fclose(f);
    if (!ok) {
        for (int i = 0; i < npages; i++) sheet_free(&glyph_pages[i]);
        free(glyphs); glyphs = NULL;
        nstyles = npages = 0;
        memset(icon_map, 0, sizeof icon_map);
        memset(styles, 0, sizeof styles);
    }
    return ok;
}

static const Glyph *glyph_exact(const Style *st, u32 cp) {
    if (!glyphs || !st->count) return NULL;
    if (cp >= 32 && cp < 128) {
        int i = st->ascii[cp - 32];
        return i >= 0 ? &glyphs[i] : NULL;
    }
    int lo = st->first, hi = st->first + st->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (glyphs[mid].cp == cp) return &glyphs[mid];
        if (glyphs[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

/* The same fallback must determine width, wrapping, alignment and drawn ink. */
static const Style *font_style(float size, int weight, int family);
static const Glyph *glyph(const Style *st, u32 cp) {
    const Glyph *g = glyph_exact(st, cp);
    // DM Sans has a small brand subset. Section names still need Japanese/Cyrillic
    // coverage from the existing UI atlas, aligned to the same baseline.
    if (!g && st->family == FONT_BRAND)
        g = glyph_exact(font_style(st->size,st->weight,FONT_UI),cp);
    if (!g) g = glyph_exact(st, 0xfffd);
    if (!g) g = glyph_exact(st, '?');
    return g;
}

/** Nearest baked font: size first, then numeric weight distance. */
static const Style *font_style(float size, int weight, int family) {
    static const Style *last;
    static float last_size;
    static int last_weight = -1, last_family = -1;
    if (last && size == last_size && weight == last_weight && family == last_family) return last;
    const Style *best = &styles[0];
    float bd = 1e9f;
    for (int i = 0; i < nstyles; i++) {
        const Style *st = &styles[i];
        if (st->kind != 0 || st->family != family) continue;
        float d = fabsf(st->size - size) * 1000 + abs((int)st->weight - weight) / 100.0f;
        if (d < bd) bd = d, best = st;
    }
    last = best, last_size = size, last_weight = weight, last_family = family;
    return best;
}

static void draw_glyph(const Glyph *g, float x, float y, float scale, const C2D_ImageTint *t) {
    if (!g || g->page >= npages || !sheet_ok(&glyph_pages[g->page])) return;
    Sheet *p = &glyph_pages[g->page];
    Tex3DS_SubTexture sub = {g->w, g->h, g->x / (float)p->w, 1 - g->y / (float)p->h, (g->x + g->w) / (float)p->w, 1 - (g->y + g->h) / (float)p->h};
    C2D_DrawImageAt((C2D_Image){&p->tex, &sub}, x + DRAW_X, y + DRAW_Y, 0.5f, t, scale, scale);
}

static inline float adv(const Glyph *g) { return g->adv / 64.0f; }
static float kern(const Style*st,u32 a,u32 b){
    if(!a||!b)return 0;
    int si=st-styles;if(si<0||si>=96)return 0;
    u32 l=0,r=kern_count[si];while(l<r){u32 m=l+(r-l)/2;KernPair*p=&kerning[si][m];
        if(p->left<a||(p->left==a&&p->right<b))l=m+1;else r=m;}
    return l<kern_count[si]&&kerning[si][l].left==a&&kerning[si][l].right==b?kerning[si][l].delta/64.f:0;
}

typedef struct {
    float pen;
    int right, extent, word_gap;
    u32 previous;
    bool ink;
} TextCursor;

/** Shared placement for draw, measurement and wrapping. The 32/255 coverage bounds
 * ignore only the faint antialias fringe; the original glyph pixels are untouched. */
static int text_step(const Style *st, TextCursor *c, u32 cp, const Glyph *g, float spacing, bool pixel_gap) {
    float pos = c->pen + kern(st,c->previous,cp);
    int origin = (int)floorf(pos + .5f);
    if (pixel_gap && g->w && g->adv && c->ink) {
        int need = c->right + (c->word_gap ? c->word_gap : 1) - g->ox - (g->ink_insets & 15);
        if (origin < need) pos += need - origin, origin = need;
    }
    if (g->w) {
        int right = origin + g->ox + g->w - (g->ink_insets >> 4);
        if (!c->ink || right > c->right) c->right = right;
        int extent = origin + g->ox + g->w;
        if (extent > c->extent) c->extent = extent;
        c->ink = true;
        c->word_gap = 0;
    }
    float advance = adv(g);
    if (pixel_gap && (cp == ' ' || cp == 0xa0)) {
        c->word_gap = (int)ceilf(st->size * .3f);
        advance = fmaxf(advance,c->word_gap);
    }
    if (pixel_gap && st->family == FONT_MONO && advance && st->mono_advance > 0)
        advance = ceilf((advance - .01f) / st->mono_advance) * st->mono_cell;
    c->pen = pos + advance + spacing;
    c->previous = cp;
    return origin;
}

static float text_extent(const TextCursor *c) { return fmaxf(c->pen, c->extent); }

static float run_sp_mode(const Style *st, const char *s, const char *end, int x, int y, const C2D_ImageTint *t, float spacing, bool pixel_gap) {
    TextCursor c = {0};
    while (s < end && *s) {
        u32 cp;
        s += utf8(s, &cp);
        if (cp == '\n') break;
        if (cp == '\r' || cp == 0x200d || (cp >= 0xfe00 && cp <= 0xfe0f)) continue;
        const Glyph *g = glyph(st, cp);
        if (!g) continue;
        int origin = text_step(st,&c,cp,g,spacing,pixel_gap);
        if (t && g->w) draw_glyph(g,x+origin+g->ox,y+st->base+g->oy,1,t);
    }
    return pixel_gap ? fmaxf(c.pen, c.extent + spacing) : c.pen;
}

/** Width of [s, end); drawing and measuring use exactly the same pixel clearance. */
static float run_sp(const Style *st, const char *s, const char *end, int x, int y, const C2D_ImageTint *t, float spacing) {
    return run_sp_mode(st,s,end,x,y,t,spacing,true);
}

static float run(const Style *st, const char *s, const char *end, int x, int y, const C2D_ImageTint *t) { return run_sp(st, s, end, x, y, t, 0); }

static inline int px(float v) { return (int)floorf(v + 0.5f); }

float text_ex(const char *s, float x, float y, float size, int weight, int family, float spacing, u32 color, int align) {
    const Style *st = font_style(size, weight, family);
    if (!s || !*s || !(color >> 24)) return st->line;
    const char *end = s + strlen(s);
    float w = align != AL_LEFT ? run_sp(st, s, end, 0, 0, NULL, spacing) - spacing : 0;
    if (align == AL_CENTER) x -= w / 2;
    if (align == AL_RIGHT) x -= w;
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, color, 1.0f);
    run_sp(st, s, end, px(x), px(y), &t, spacing);
    return st->line;
}

float text_ex_w(const char *s, float size, int weight, int family, float spacing) {
    return s && *s ? run_sp(font_style(size, weight, family), s, s + strlen(s), 0, 0, NULL, spacing) - spacing : 0;
}

float text_style(const char *s, float x, float y, float size, int weight, int family, u32 color, int align) {
    return text_ex(s, x, y, size, weight, family, 0, color, align);
}

// The separately approved status numerals retain their existing font metrics and positions.
float text_status_w(const char *s, float size) {
    return s ? run_sp_mode(font_style(size,600,FONT_UI),s,s+strlen(s),0,0,NULL,0,false) : 0;
}
void text_status(const char *s, float x, float y, float size, u32 color) {
    if (!s || !(color >> 24)) return;
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint,color,1);
    run_sp_mode(font_style(size,600,FONT_UI),s,s+strlen(s),px(x),px(y),&tint,0,false);
}

float text(const char *s, float x, float y, float size, u32 color, int align) { return text_style(s, x, y, size, FONT_WEIGHT_BODY, FONT_UI, color, align); }

/** Uppercase (ASCII, Latin-1 accents, Cyrillic) into `out`. */
static void upper_utf8(char *out, size_t n, const char *s) {
    size_t o = 0;
    for (const u8 *p = (const u8 *)s; *p && o + 2 < n; p++) {
        u8 c = *p;
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c == 0xC3 && p[1] >= 0xA0 && p[1] <= 0xBE && p[1] != 0xB7) {
            out[o++] = c, out[o++] = p[1] - 0x20, p++;
            continue;
        } else if (c == 0xD0 && p[1] >= 0xB0 && p[1] <= 0xBF) {
            out[o++] = c, out[o++] = p[1] - 0x20, p++;
            continue;
        } else if (c == 0xD1 && p[1] >= 0x80 && p[1] <= 0x8F) {
            out[o++] = 0xD0, out[o++] = p[1] + 0x20, p++;
            continue;
        }
        out[o++] = c;
    }
    out[o] = 0;
}

/** Eyebrow labels: uppercase with letter-spacing (CSS text-transform + letter-spacing). */
float text_caps_w(const char *s, float size, float spacing) {
    if (!s || !*s) return 0;
    char up[160];
    upper_utf8(up, sizeof up, s);
    return run_sp(font_style(size, 900, FONT_UI), up, up + strlen(up), 0, 0, NULL, spacing) - spacing;
}
float text_caps(const char *s, float x, float y, float size, float spacing, u32 color, int align) {
    const Style *st = font_style(size, 900, FONT_UI);
    if (!s || !*s || !(color >> 24)) return st->line;
    char up[160];
    upper_utf8(up, sizeof up, s);
    const char *end = up + strlen(up);
    float w = run_sp(st, up, end, 0, 0, NULL, spacing) - spacing;
    if (align == AL_CENTER) x -= w / 2;
    if (align == AL_RIGHT) x -= w;
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, color, 1.0f);
    run_sp(st, up, end, px(x), px(y), &t, spacing);
    return st->line;
}

void text_caps_fit(const char *s,float x,float y,float size,float spacing,u32 color,float width) {
    char up[160];upper_utf8(up,sizeof up,s?s:"");
    size_t n=strlen(up);
    if(text_ex_w(up,size,900,FONT_UI,spacing)>width) {
        for(;;) {
            if(n+4<=sizeof up) {
                memcpy(up+n,"…",4);
                if(text_ex_w(up,size,900,FONT_UI,spacing)<=width)break;
            }
            if(!n){up[0]=0;break;}
            do{n--;}while(n&&((unsigned char)up[n]&0xc0)==0x80);
            up[n]=0;
        }
    }
    text_ex(up,x,y,size,900,FONT_UI,spacing,color,AL_LEFT);
}
float text_style_w(const char *s, float size, int weight, int family) { return s ? run(font_style(size, weight, family), s, s + strlen(s), 0, 0, NULL) : 0; }
void text_dyn(const char *s, float x, float y, float size, u32 color, int align) { text_style(s, x, y, size, FONT_WEIGHT_BODY, FONT_UI, color, align); }
float text_w(const char *s, float size) { return s ? run(font_style(size, FONT_WEIGHT_BODY, FONT_UI), s, s + strlen(s), 0, 0, NULL) : 0; }
float line_h(float size) { return font_style(size, FONT_WEIGHT_BODY, FONT_UI)->line; }
float text_base(float size, int weight, int family) { return font_style(size, weight, family)->base; }

float text_ink_top(const char *s, float cy, float size, int weight, int family) {
    const Style *st = font_style(size, weight, family);
    int top = 128, bottom = -128;
    while (s && *s) {
        u32 cp;
        s += utf8(s, &cp);
        if (cp == '\n') break;
        const Glyph *g = glyph(st, cp);
        if (!g || !g->w || !g->h) continue;
        if (g->oy < top) top = g->oy;
        if (g->oy + g->h > bottom) bottom = g->oy + g->h;
    }
    if (bottom < top) return roundf(cy - st->line / 2.0f);
    return roundf(cy - st->base - (top + bottom) / 2.0f);
}

// Wrapped paragraphs: line breaks are computed once and cached.
#define WCACHE 192
typedef struct {
    u32 hash;
    char *str;
    const Style *st;
    float width;
    int lines;
    size_t *begin, *end;
} WEntry;
static WEntry wcache[WCACHE];
static int nwcache;

void text_reset(void) {
    for (int i = 0; i < WCACHE; i++) {
        free(wcache[i].str);
        free(wcache[i].begin);
        free(wcache[i].end);
        memset(&wcache[i], 0, sizeof wcache[i]);
    }
    nwcache = 0;
}

void text_frame(void) {}

static WEntry *wrap_get(const char *s, const Style *st, float width) {
    u32 h = name_hash(s) ^ ((u32)(width * 4) * 2654435761u) ^ (u32)(st - styles) * 40503u;
    if (!h) h = 1;
    u32 k = h % WCACHE;
    for (int n = 0; n < WCACHE; n++, k = (k + 1) % WCACHE) {
        WEntry *e = &wcache[k];
        if (!e->hash) break;
        if (e->hash == h && e->st == st && e->width == width && !strcmp(e->str, s)) return e;
    }
    if (nwcache >= WCACHE * 3 / 4) text_reset(), k = h % WCACHE;
    while (wcache[k].hash) k = (k + 1) % WCACHE;
    WEntry *e = &wcache[k];
    size_t len = strlen(s);
    int cap = 4;
    e->begin = malloc(cap * sizeof(size_t));
    e->end = malloc(cap * sizeof(size_t));
    e->lines = 0;
#define PUSH(b, en)                                                     \
    do {                                                                \
        if (e->lines == cap) {                                          \
            cap *= 2;                                                   \
            e->begin = realloc(e->begin, cap * sizeof(size_t));                      \
            e->end = realloc(e->end, cap * sizeof(size_t));                          \
        }                                                               \
        size_t begin_ = (size_t)(b), end_ = (size_t)(en);                 \
        while (end_ > begin_ && s[end_-1] == ' ') end_--;                 \
        e->begin[e->lines] = begin_, e->end[e->lines++] = end_;            \
    } while (0)
    size_t start = 0;
    int space = -1;
    TextCursor cursor = {0};
    for (size_t i = 0; i < len;) {
        u32 cp;
        int n = utf8(s + i, &cp);
        if (cp == '\n') {
            PUSH(start, i);
            start = i + n, space = -1;
            cursor = (TextCursor){0};
            i += n;
            continue;
        }
        if (cp == '\r' || cp == 0x200d || (cp >= 0xfe00 && cp <= 0xfe0f)) { i += n; continue; }
        const Glyph *g = glyph(st, cp);
        TextCursor next = cursor;
        if (g) text_step(st,&next,cp,g,0,true);
        if (cp == ' ') {
            space = (int)i, cursor = next, i += n;
            continue;
        }
        if (text_extent(&next) > width && i > start) {
            if (space > (int)start && cp < 0x2E80) {
                PUSH(start, space);
                start = space + 1;
            } else {  // CJK or one very long word: break before this glyph
                PUSH(start, i);
                start = i;
            }
            // Replay the new line from its actual origin: no previous-line kerning/rounding.
            i = start; space = -1; cursor = (TextCursor){0};
            continue;
        }
        cursor = next, i += n;
    }
    PUSH(start, len);
#undef PUSH
    e->hash = h, e->st = st, e->width = width, e->str = strdup(s);
    nwcache++;
    return e;
}

// V2 paragraphs: Inter 500 with explicit line boxes (11/15, 10/14, 9/12).
static int para_line(const Style *st, float size) { (void)size; return st->line; }

float text_wrap_family(const char *s,float x,float y,float size,int weight,int family,u32 color,float width){
    if (!s || !*s) return 0;
    const Style *st=font_style(size,weight,family);
    WEntry *e = wrap_get(s, st, width);
    if (color >> 24) {
        C2D_ImageTint t;
        C2D_PlainImageTint(&t, color, 1.0f);
        int lh = para_line(st, size), dy = (lh - st->line) / 2;
        for (int i = 0; i < e->lines; i++) run(st, s + e->begin[i], s + e->end[i], px(x), px(y) + dy + i * lh, &t);
    }
    return e->lines * para_line(st, size);
}

float text_wrap_h_family(const char *s,float size,int weight,int family,float width){
    if (!s || !*s) return 0;
    const Style *st=font_style(size,weight,family);
    return wrap_get(s, st, width)->lines * para_line(st, size);
}

float text_wrap_style(const char*s,float x,float y,float size,int weight,u32 color,float width){return text_wrap_family(s,x,y,size,weight,FONT_UI,color,width);}
float text_wrap_h_style(const char*s,float size,int weight,float width){return text_wrap_h_family(s,size,weight,FONT_UI,width);}

float text_wrap(const char*s,float x,float y,float size,u32 color,float width){return text_wrap_style(s,x,y,size,FONT_WEIGHT_BODY,color,width);}
float text_wrap_h(const char*s,float size,float width){return text_wrap_h_style(s,size,FONT_WEIGHT_BODY,width);}

// ---- shapes: anti-aliased circles, rings and rounded corners ---------------------------------------
// Rasterized on the CPU (exact coverage) into a small A8 texture the first time each size is
// needed, then drawn 1:1. citro2d's own ellipses have hard, jagged edges.
#define SHAPE_W 512
#define SHAPE_H 512
#define MAX_SHAPES 256
typedef struct {
    u16 r2, t2, cx2, cy2;  // radius, ring thickness (0 = filled) and centre, in half pixels
    u16 x, y, w, h;
} Shape;
static C3D_Tex shape_tex;
static Shape shapes[MAX_SHAPES];
static int nshapes, sh_x, sh_y, sh_h;

static const Shape *shape_get(float r, float t, float cx, float cy) {
    u16 r2 = (u16)(r * 2 + 0.5f), t2 = (u16)(t * 2 + 0.5f), cx2 = (u16)(cx * 2 + 0.5f), cy2 = (u16)(cy * 2 + 0.5f);
    for (int i = nshapes - 1; i >= 0; i--)
        if (shapes[i].r2 == r2 && shapes[i].t2 == t2 && shapes[i].cx2 == cx2 && shapes[i].cy2 == cy2) return &shapes[i];
    int w = (int)ceilf(cx + r + 1), h = (int)ceilf(cy + r + 1);
    if (sh_x + w + 1 > SHAPE_W) sh_x = 0, sh_y += sh_h, sh_h = 0;
    if (sh_y + h + 1 > SHAPE_H || nshapes == MAX_SHAPES) {  // full: start over (rare)
        memset(shape_tex.data, 0, SHAPE_W * SHAPE_H);
        nshapes = sh_x = sh_y = sh_h = 0;
    }
    Shape *s = &shapes[nshapes++];
    *s = (Shape){r2, t2, cx2, cy2, (u16)sh_x, (u16)sh_y, (u16)w, (u16)h};
    u8 *data = shape_tex.data;
    float inner = r - t;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            float a = clampf(r + 0.5f - d, 0, 1);
            if (t > 0) a *= clampf(d - inner + 0.5f, 0, 1);
            int X = sh_x + x, Y = sh_y + y;
            u32 m = (X & 1) | ((Y & 1) << 1) | ((X & 2) << 1) | ((Y & 2) << 2) | ((X & 4) << 2) | ((Y & 4) << 3);
            data[((((Y >> 3) * (SHAPE_W >> 3) + (X >> 3)) << 6) + m)] = (u8)(a * 255 + 0.5f);
        }
    sh_x += w + 1;
    if (h + 1 > sh_h) sh_h = h + 1;
    C3D_TexFlush(&shape_tex);
    return s;
}

/** Draws part (sx, sy, w, h) of a cached shape at (x, y), scaled by k. */
static void shape_draw(const Shape *s, float x, float y, int sx, int sy, int w, int h, float k, u32 c) {
    Tex3DS_SubTexture sub = {(u16)w, (u16)h, (s->x + sx) / (float)SHAPE_W, 1 - (s->y + sy) / (float)SHAPE_H,
                             (s->x + sx + w) / (float)SHAPE_W, 1 - (s->y + sy + h) / (float)SHAPE_H};
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, c, 1.0f);
    C2D_DrawImageAt((C2D_Image){&shape_tex, &sub}, x + DRAW_X, y + DRAW_Y, 0.5f, &t, k, k);
}

void rect(float x, float y, float w, float h, u32 c) {
    if (c >> 24) C2D_DrawRectSolid(x + DRAW_X, y + DRAW_Y, 0.5f, w, h, c);
}

static void disc(float cx, float cy, float r, float t, u32 c) {
    if (r <= 0 || !(c >> 24)) return;
    if (r > 40) {  // large (halos, pops): one big cached disc, scaled
        const Shape *s = shape_get(48, t > 0 ? t * 48 / r : 0, 49, 49);
        float k = r / 48;
        shape_draw(s, cx - 49 * k, cy - 49 * k, 0, 0, s->w, s->h, k, c);
        return;
    }
    float qr = roundf(r * 2) / 2, qx = roundf(cx * 2) / 2, qy = roundf(cy * 2) / 2;
    int ix = (int)floorf(qx - qr - 1), iy = (int)floorf(qy - qr - 1);
    const Shape *s = shape_get(qr, t > 0 ? fmaxf(0.5f, roundf(t * 2) / 2) : 0, qx - ix, qy - iy);
    shape_draw(s, ix, iy, 0, 0, s->w, s->h, 1, c);
}

void circle(float cx, float cy, float r, u32 c) { disc(cx, cy, r, 0, c); }

void ellipse(float x, float y, float w, float h, u32 c) {
    if (w <= 0 || h <= 0 || !(c >> 24)) return;
    const Shape *s = shape_get(16, 0, 17, 17);
    Tex3DS_SubTexture sub = {32, 32, (s->x + 1) / (float)SHAPE_W, 1 - (s->y + 1) / (float)SHAPE_H, (s->x + 33) / (float)SHAPE_W, 1 - (s->y + 33) / (float)SHAPE_H};
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, c, 1.0f);
    C2D_DrawImageAt((C2D_Image){&shape_tex, &sub}, x + DRAW_X, y + DRAW_Y, 0.5f, &t, w / 32, h / 32);
}

// Partial arcs (progress, spinners): triangles with a 1 px feathered edge on both sides.
static void arc(float cx, float cy, float r, float t, float from, float to, u32 c) {
    float span_a = fabsf(to - from);
    int n = (int)ceilf(span_a * (r + 1) / 2.5f);
    if (n < 4) n = 4;
    u32 c0 = c & 0x00FFFFFFu;
    float ro = r + 0.5f, rm = r - 0.5f, ri = r - t + 0.5f, rz = fmaxf(0, r - t - 0.5f);
    if (ri > rm) ri = rm = r - t / 2;  // hairline: core collapses to the middle
    float a0 = from, cs0 = cosf(a0), sn0 = sinf(a0);
    for (int i = 1; i <= n; i++) {
        float a1 = from + (to - from) * i / n, cs1 = cosf(a1), sn1 = sinf(a1);
#define P(rr, cs, sn) cx + DRAW_X + (cs) * (rr), cy + DRAW_Y + (sn) * (rr)
        C2D_DrawTriangle(P(ro, cs0, sn0), c0, P(rm, cs0, sn0), c, P(ro, cs1, sn1), c0, 0.5f);
        C2D_DrawTriangle(P(rm, cs0, sn0), c, P(rm, cs1, sn1), c, P(ro, cs1, sn1), c0, 0.5f);
        if (rm > ri) {
            C2D_DrawTriangle(P(rm, cs0, sn0), c, P(ri, cs0, sn0), c, P(rm, cs1, sn1), c, 0.5f);
            C2D_DrawTriangle(P(ri, cs0, sn0), c, P(ri, cs1, sn1), c, P(rm, cs1, sn1), c, 0.5f);
        }
        C2D_DrawTriangle(P(ri, cs0, sn0), c, P(rz, cs0, sn0), c0, P(ri, cs1, sn1), c, 0.5f);
        C2D_DrawTriangle(P(rz, cs0, sn0), c0, P(rz, cs1, sn1), c0, P(ri, cs1, sn1), c, 0.5f);
#undef P
        cs0 = cs1, sn0 = sn1;
    }
}

void ring(float cx, float cy, float r, float thick, float from, float to, u32 c) {
    if (!(c >> 24) || r <= 0) return;
    if (fabsf(to - from) >= 2 * M_PI - 0.001f) disc(cx, cy, r, thick, c);
    else arc(cx, cy, r, thick, from, to, c);
}

typedef struct {
    int x, y, w, h, r;
} Box;

static Box snap_box(float x, float y, float w, float h, float r) {
    Box b = {px(x), px(y), 0, 0, 0};
    b.w = px(x + w) - b.x, b.h = px(y + h) - b.y;
    int lim = (b.w < b.h ? b.w : b.h) / 2;
    b.r = px(r);
    if (b.r > lim) b.r = lim;
    if (b.r < 0) b.r = 0;
    return b;
}

static void corners(const Shape *s, Box b, u32 c) {
    int R = b.r;
    shape_draw(s, b.x, b.y, 1, 1, R, R, 1, c);
    shape_draw(s, b.x + b.w - R, b.y, R + 1, 1, R, R, 1, c);
    shape_draw(s, b.x, b.y + b.h - R, 1, R + 1, R, R, 1, c);
    shape_draw(s, b.x + b.w - R, b.y + b.h - R, R + 1, R + 1, R, R, 1, c);
}

void rrect(float x, float y, float w, float h, float r, u32 c) {
    Box b = snap_box(x, y, w, h, r);
    if (b.w <= 0 || b.h <= 0 || !(c >> 24)) return;
    if (b.r < 1) {
        rect(b.x, b.y, b.w, b.h, c);
        return;
    }
    int R = b.r;
    corners(shape_get(R, 0, R + 1, R + 1), b, c);
    rect(b.x + R, b.y, b.w - 2 * R, b.h, c);
    rect(b.x, b.y + R, R, b.h - 2 * R, c);
    rect(b.x + b.w - R, b.y + R, R, b.h - 2 * R, c);
}

/** Rounded box with a left-to-right gradient (the orange primary buttons). */
void rrect_hgrad(float x, float y, float w, float h, float r, u32 c1, u32 c2) {
    Box b = snap_box(x, y, w, h, r);
    if (b.w <= 0 || b.h <= 0) return;
    int R = b.r;
    if (R >= 1) {
        const Shape *s = shape_get(R, 0, R + 1, R + 1);
        shape_draw(s, b.x, b.y, 1, 1, R, R, 1, c1);
        shape_draw(s, b.x, b.y + b.h - R, 1, R + 1, R, R, 1, c1);
        shape_draw(s, b.x + b.w - R, b.y, R + 1, 1, R, R, 1, c2);
        shape_draw(s, b.x + b.w - R, b.y + b.h - R, R + 1, R + 1, R, R, 1, c2);
        rect(b.x, b.y + R, R, b.h - 2 * R, c1);
        rect(b.x + b.w - R, b.y + R, R, b.h - 2 * R, c2);
    }
    C2D_DrawRectangle(b.x + R + DRAW_X, b.y + DRAW_Y, 0.5f, b.w - 2 * R, b.h, c1, c2, c1, c2);
}

void rrect_outline(float x, float y, float w, float h, float r, float t, u32 c) {
    Box b = snap_box(x, y, w, h, r);
    if (b.w <= 0 || b.h <= 0 || !(c >> 24)) return;
    float T = fmaxf(0.5f, roundf(t * 2) / 2);
    int full = (int)T;
    float part = T - full;
    u32 cp = (c & 0x00FFFFFFu) | ((u32)((c >> 24) * part) << 24);
    int R = b.r < full + 1 ? 0 : b.r;
    if (R) corners(shape_get(R, T, R + 1, R + 1), b, c);
    int L = b.w - 2 * R, V = b.h - 2 * R;
    // Straight edges: whole rows at full strength plus a partial row for half-pixel strokes.
    rect(b.x + R, b.y, L, full, c);
    rect(b.x + R, b.y + b.h - full, L, full, c);
    rect(b.x, b.y + R + (R ? 0 : full), full, V - (R ? 0 : 2 * full), c);
    rect(b.x + b.w - full, b.y + R + (R ? 0 : full), full, V - (R ? 0 : 2 * full), c);
    if (part > 0) {
        rect(b.x + R + (R ? 0 : full), b.y + full, L - (R ? 0 : 2 * full), 1, cp);
        rect(b.x + R + (R ? 0 : full), b.y + b.h - full - 1, L - (R ? 0 : 2 * full), 1, cp);
        rect(b.x + full, b.y + R + (R ? 0 : full + 1), 1, V - (R ? 0 : 2 * full + 2), cp);
        rect(b.x + b.w - full - 1, b.y + R + (R ? 0 : full + 1), 1, V - (R ? 0 : 2 * full + 2), cp);
    }
}

void vgrad(float x, float y, float w, float h, u32 top, u32 bottom) {
    C2D_DrawRectangle(x + DRAW_X, y + DRAW_Y, 0.5f, w, h, top, top, bottom, bottom);
}

float DRAW_X, DRAW_Y;

void scissor(int screen_w, float x, float y, float w, float h) {
    C2D_Flush();
    x += DRAW_X;
    y += DRAW_Y;
    int l = (int)fmaxf(0, SCR_H - (y + h)), t = (int)fmaxf(0, screen_w - (x + w));
    int r = (int)fminf(SCR_H, SCR_H - y), b = (int)fminf(screen_w, screen_w - x);
    C3D_SetScissor(GPU_SCISSOR_NORMAL, l, t, r, b);
}

void scissor_off(void) {
    C2D_Flush();
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
}

// ---- images ----------------------------------------------------------------------------------------
void draw_img(C2D_Image im, float x, float y, float sx, float sy, float alpha) {
    if (alpha <= 0.004f) return;
    if (alpha >= 0.999f) {
        C2D_DrawImageAt(im, x + DRAW_X, y + DRAW_Y, 0.5f, NULL, sx, sy);
        return;
    }
    C2D_ImageTint t;
    C2D_AlphaImageTint(&t, alpha);
    C2D_DrawImageAt(im, x + DRAW_X, y + DRAW_Y, 0.5f, &t, sx, sy);
}

void draw_img_tint(C2D_Image im, float x, float y, float sx, float sy, u32 color) {
    if (!(color >> 24)) return;
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, color, 1.0f);
    C2D_DrawImageAt(im, x + DRAW_X, y + DRAW_Y, 0.5f, &t, sx, sy);
}

void draw_img_rot(C2D_Image im, float cx, float cy, float sx, float sy, float angle, float alpha) {
    C2D_DrawParams p = {
        .pos = {cx + DRAW_X, cy + DRAW_Y, im.subtex->width * sx, im.subtex->height * sy},
        .center = {im.subtex->width * sx / 2, im.subtex->height * sy / 2},
        .depth = 0.5f,
        .angle = angle,
    };
    C2D_ImageTint t;
    C2D_AlphaImageTint(&t, alpha);
    C2D_DrawImage(im, &p, alpha < 0.999f ? &t : NULL);
}

int icon_index(const char *name) {
    for (u32 k = name_hash(name) % 128; icon_map[k].name; k = (k + 1) % 128)
        if (!strcmp(icon_map[k].name, name)) return icon_map[k].index;
    return -1;
}

/** UI icon centred on (cx, cy): the baked size when it exists (1:1), else the nearest one scaled. */
void draw_icon(const char *name, float cx, float cy, float size, u32 color) {
    int i = icon_index(name);
    if (i < 0 || !(color >> 24)) return;
    const Style *best = NULL;
    for (int k = 0; k < nstyles; k++)
        if (styles[k].kind == 1 && (!best || fabsf(styles[k].size - size) < fabsf(best->size - size))) best = &styles[k];
    if (!best || (u32)i >= best->count) return;
    const Glyph *g = &glyphs[best->first + i];
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, color, 1.0f);
    if (fabsf(best->size - size) < 0.01f) {
        int x = px(cx - size / 2), y = px(cy - size / 2);
        draw_glyph(g, x + g->ox, y + g->oy, 1, &t);
    } else {
        float k = size / best->size;
        draw_glyph(g, cx - (best->size / 2 - g->ox) * k, cy - (best->size / 2 - g->oy) * k, k, &t);
    }
}

void draw_badge(const char *button, float x, float y, float alpha) {
    char name[24];
    snprintf(name, sizeof name, "badge-%s", button);
    draw_img(ui_img(name), px(x), px(y), 1, 1, alpha);
}

// ---- lifecycle ----------------------------------------------------------------------------------------
void gfx_init(void) {
    LightLock_Init(&q_lock);
    LightEvent_Init(&q_event, RESET_ONESHOT);
    LightEvent_Init(&upload_event, RESET_ONESHOT);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    // Same core as the main thread, lower priority: it only runs while a frame waits for vsync.
    loader = threadCreate(loader_main, NULL, 32 * 1024, prio + 2, -2, false);
    UI.crisp = true;
    sheet_load_now(&UI, "romfs:/gfx/ui.ftx");
    json_error_t err;
    ui_names = json_load_file("romfs:/gfx/ui.json", 0, &err);
    ui_map_build();
    fonts_load();
    C3D_TexInit(&shape_tex, SHAPE_W, SHAPE_H, GPU_A8);
    memset(shape_tex.data, 0, SHAPE_W * SHAPE_H);
    C3D_TexSetFilter(&shape_tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexFlush(&shape_tex);
}

void gfx_exit(void) {
    loader_quit = true;
    LightEvent_Signal(&q_event);
    LightEvent_Signal(&upload_event);
    if (!thread_reap(&loader)) return;  // still decoding a sheet from a busy card
    free(banner_bank);banner_bank=NULL;
    text_reset();
    for (int i = 0; i < npages; i++) sheet_free(&glyph_pages[i]);
    free(glyphs); glyphs = NULL;
    nstyles = npages = 0;
    C3D_TexDelete(&shape_tex);
}
