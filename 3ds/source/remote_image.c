// Native remote images. Only pump/exit touch GPU objects; only worker does IO.
#include "remote_image.h"
#include "image_round.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#include <limits.h>
#include <png.h>
#include <jpeglib.h>
#include <gif_lib.h>
#include <webp/decode.h>
#include <zlib.h>
#include "vendor/remote_svg.h"
#ifndef REMOTE_IMAGE_HOST_TEST
#include "gfx.h"
#include "network.h"
#include "http_cache.h"
#include <sys/stat.h>
#include <time.h>
#endif

#define RI_PAYLOAD (2u * 1024u * 1024u)
#define RI_WIDTH 376
#define RI_HEIGHT 240
#define RI_SOURCE_SIDE 8192u
#define RI_SOURCE_PIXELS (4u * 1024u * 1024u)

typedef struct { const unsigned char *data; size_t size, pos; } RIMemory;
typedef struct { unsigned char *rgba; int w, h; } RIPixels;
static bool ri_source_size(unsigned w, unsigned h) {
    return w && h && w <= RI_SOURCE_SIDE && h <= RI_SOURCE_SIDE && w <= RI_SOURCE_PIXELS / h;
}
static bool ri_pixels(RIPixels *p, unsigned w, unsigned h) {
    if (!ri_source_size(w, h)) return false;
    double scale = fmin(1.0, fmin((double)RI_WIDTH / w, (double)RI_HEIGHT / h));
    p->w = (int)fmax(1., floor(w * scale + .5));
    p->h = (int)fmax(1., floor(h * scale + .5));
    p->rgba = calloc((size_t)p->w * p->h, 4);
    return p->rgba != NULL;
}
// Sampling centers never run past the last source pixel, even for a 1px image.
static unsigned ri_sample(unsigned out, unsigned source, unsigned target) {
    return (unsigned)(((uint64_t)out * 2 + 1) * source / (target * 2));
}
static void ri_copy_row(RIPixels *p, const unsigned char *row, unsigned source_w,
                        unsigned source_y, unsigned source_h, unsigned channels) {
    for (unsigned y = 0; y < (unsigned)p->h; y++) {
        if (ri_sample(y, source_h, p->h) != source_y) continue;
        unsigned char *dst = p->rgba + (size_t)y * p->w * 4;
        for (unsigned x = 0; x < (unsigned)p->w; x++) {
            const unsigned char *src = row + ri_sample(x, source_w, p->w) * channels;
            dst[x*4] = src[0]; dst[x*4+1] = src[1]; dst[x*4+2] = src[2];
            dst[x*4+3] = channels == 4 ? src[3] : 255;
        }
    }
}
static void ri_png_read(png_structp png, png_bytep out, png_size_t n) {
    RIMemory *m = png_get_io_ptr(png);
    if (n > m->size - m->pos) png_error(png, "truncated");
    memcpy(out, m->data + m->pos, n); m->pos += n;
}
static void ri_png_error(png_structp png, png_const_charp message) { (void)message; png_longjmp(png, 1); }
static void ri_png_warning(png_structp png, png_const_charp message) { (void)png; (void)message; }
static bool ri_png(const unsigned char *data, size_t size, RIPixels *out) {
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, ri_png_error, ri_png_warning);
    if (!png) return false;
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); return false; }
    // Volatile pointers remain defined across libpng's longjmp on corrupt input.
    unsigned char *volatile row = NULL;
    bool ok = false;
    RIMemory m = {data, size, 0};
    if (setjmp(png_jmpbuf(png))) goto done;
    png_set_read_fn(png, &m, ri_png_read);
    png_set_user_limits(png, RI_SOURCE_SIDE, RI_SOURCE_SIDE);
    png_set_chunk_malloc_max(png, 64 * 1024);
    png_set_chunk_cache_max(png, 8);
    png_set_keep_unknown_chunks(png, PNG_HANDLE_CHUNK_NEVER, NULL, 0);
    png_read_info(png, info);
    png_uint_32 w, h;
    int depth, color, interlace;
    png_get_IHDR(png, info, &w, &h, &depth, &color, &interlace, NULL, NULL);
    // Adam7 would require a full source canvas. Fail visibly within our RAM cap.
    if (interlace != PNG_INTERLACE_NONE || !ri_pixels(out, w, h)) goto done;
    if (depth == 16) png_set_strip_16(png);
    if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    bool trans = png_get_valid(png, info, PNG_INFO_tRNS) != 0;
    if (trans) png_set_tRNS_to_alpha(png);
    if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if (!(color & PNG_COLOR_MASK_ALPHA) && !trans) png_set_add_alpha(png, 255, PNG_FILLER_AFTER);
    png_read_update_info(png, info);
    if (png_get_rowbytes(png, info) != w * 4) goto done;
    row = malloc((size_t)w * 4);
    if (!row) goto done;
    for (unsigned y = 0; y < h; y++) {
        png_read_row(png, row, NULL);
        ri_copy_row(out, row, w, y, h, 4);
    }
    png_read_end(png, NULL);
    ok = true;
done:
    free(row);
    png_destroy_read_struct(&png, &info, NULL);
    if (!ok) { free(out->rgba); memset(out, 0, sizeof *out); }
    return ok;
}
typedef struct { struct jpeg_error_mgr pub; jmp_buf jump; } RIJError;
static void ri_jpeg_error(j_common_ptr c) { longjmp(((RIJError *)c->err)->jump, 1); }
static void ri_jpeg_message(j_common_ptr c) { (void)c; }
static bool ri_jpeg(const unsigned char *data, size_t size, RIPixels *out) {
    // Keep the mutable libjpeg struct off the setjmp stack (C longjmp rules).
    struct jpeg_decompress_struct *c = calloc(1, sizeof *c);
    if (!c) return false;
    RIJError e;
    unsigned char *volatile row = NULL;
    bool ok = false;
    c->err = jpeg_std_error(&e.pub);
    e.pub.error_exit = ri_jpeg_error; e.pub.output_message = ri_jpeg_message;
    if (setjmp(e.jump)) goto done;
    jpeg_create_decompress(c);
    c->mem->max_memory_to_use = 2 * 1024 * 1024;
    jpeg_mem_src(c, data, size);
    if (jpeg_read_header(c, TRUE) != JPEG_HEADER_OK || !ri_source_size(c->image_width, c->image_height)) goto done;
    // Progressive coefficient arrays are independent of output downscaling.
    if (c->progressive_mode && (uint64_t)c->image_width * c->image_height > 512u * 512u) goto done;
    if (c->jpeg_color_space == JCS_CMYK || c->jpeg_color_space == JCS_YCCK) goto done;
    if (!ri_pixels(out, c->image_width, c->image_height)) goto done;
    c->out_color_space = JCS_RGB;
    c->dct_method = JDCT_IFAST;
    c->do_fancy_upsampling = FALSE;
    c->scale_num = 1; c->scale_denom = 1;
    while (c->scale_denom < 8 && c->image_width / (c->scale_denom * 2) >= (unsigned)out->w &&
           c->image_height / (c->scale_denom * 2) >= (unsigned)out->h) c->scale_denom *= 2;
    jpeg_start_decompress(c);
    if (c->output_components != 3 || !ri_source_size(c->output_width, c->output_height)) goto done;
    row = malloc((size_t)c->output_width * 3);
    if (!row) goto done;
    while (c->output_scanline < c->output_height) {
        unsigned y = c->output_scanline;
        JSAMPROW r = row;
        if (jpeg_read_scanlines(c, &r, 1) != 1) goto done;
        ri_copy_row(out, row, c->output_width, y, c->output_height, 3);
    }
    jpeg_finish_decompress(c);
    // libjpeg recovers truncated input with warnings; don't cache damaged images.
    ok = c->err->num_warnings == 0;
done:
    free(row);
    jpeg_destroy_decompress(c); free(c);
    if (!ok) { free(out->rgba); memset(out, 0, sizeof *out); }
    return ok;
}
static int ri_gif_read(GifFileType *g, GifByteType *out, int n) {
    RIMemory *m = g->UserData;
    if (n < 0 || (size_t)n > m->size - m->pos) return 0;
    memcpy(out, m->data + m->pos, n); m->pos += n; return n;
}
static bool ri_gif(const unsigned char *data, size_t size, RIPixels *out) {
    RIMemory m = {data, size, 0}; int err, transparent = -1;
    GifFileType *g = DGifOpen(&m, ri_gif_read, &err);
    if (!g) return false;
    bool ok = false; unsigned char *row = NULL;
    if (!ri_pixels(out, g->SWidth, g->SHeight)) goto done;
    GifRecordType record;
    while (DGifGetRecordType(g, &record) == GIF_OK) {
        if (record == TERMINATE_RECORD_TYPE) break;
        if (record == EXTENSION_RECORD_TYPE) {
            GifByteType *ext; int code;
            if (DGifGetExtension(g, &code, &ext) == GIF_ERROR) goto done;
            if (code == GRAPHICS_EXT_FUNC_CODE && ext && ext[0] == 4 && (ext[1] & 1)) transparent = ext[4];
            while (ext) if (DGifGetExtensionNext(g, &ext) == GIF_ERROR) goto done;
        } else if (record == IMAGE_DESC_RECORD_TYPE) {
            if (DGifGetImageDesc(g) == GIF_ERROR) goto done;
            GifImageDesc *im = &g->Image;
            if (im->Width <= 0 || im->Height <= 0 || im->Left < 0 || im->Top < 0 ||
                im->Left + im->Width > g->SWidth || im->Top + im->Height > g->SHeight) goto done;
            ColorMapObject *map = im->ColorMap ? im->ColorMap : g->SColorMap;
            if (!map || !map->Colors || map->ColorCount <= 0) goto done;
            // First frame starts with the logical screen background (unless transparent).
            if (transparent < 0 && g->SColorMap && g->SBackGroundColor >= 0 && g->SBackGroundColor < g->SColorMap->ColorCount) {
                GifColorType bg = g->SColorMap->Colors[g->SBackGroundColor];
                for (int i = 0; i < out->w * out->h; i++) {
                    unsigned char *d = out->rgba + i * 4;
                    d[0] = bg.Red; d[1] = bg.Green; d[2] = bg.Blue; d[3] = 255;
                }
            }
            row = malloc(im->Width);
            if (!row) goto done;
            const int start[] = {0, 4, 2, 1}, step[] = {8, 8, 4, 2};
            for (int pass = 0; pass < (im->Interlace ? 4 : 1); pass++) {
                for (int sy = im->Interlace ? start[pass] : 0; sy < im->Height; sy += im->Interlace ? step[pass] : 1) {
                    if (DGifGetLine(g, row, im->Width) == GIF_ERROR) goto done;
                    for (int ix = 0; ix < im->Width; ix++) if (row[ix] >= map->ColorCount) goto done;
                    for (int y = 0; y < out->h; y++) {
                        if ((int)ri_sample(y, g->SHeight, out->h) != im->Top + sy) continue;
                        for (int x = 0; x < out->w; x++) {
                            int sx = (int)ri_sample(x, g->SWidth, out->w) - im->Left;
                            if (sx < 0 || sx >= im->Width || row[sx] == transparent) continue;
                            GifColorType col = map->Colors[row[sx]];
                            unsigned char *d = out->rgba + (y * out->w + x) * 4;
                            d[0] = col.Red; d[1] = col.Green; d[2] = col.Blue; d[3] = 255;
                        }
                    }
                }
            }
            ok = true; break; // Never slurp animation frames into memory.
        } else goto done;
    }
done:
    free(row); DGifCloseFile(g, &err);
    if (!ok) { free(out->rgba); memset(out, 0, sizeof *out); }
    return ok;
}
static bool ri_webp(const unsigned char *data,size_t size,RIPixels *out) {
    WebPDecoderConfig config;
    if(!WebPInitDecoderConfig(&config)||WebPGetFeatures(data,size,&config.input)!=VP8_STATUS_OK||
       config.input.has_animation||!ri_pixels(out,config.input.width,config.input.height))return false;
    config.output.colorspace=MODE_RGBA;
    config.output.is_external_memory=1;
    config.output.u.RGBA.rgba=out->rgba;
    config.output.u.RGBA.stride=out->w*4;
    config.output.u.RGBA.size=(size_t)out->w*out->h*4;
    config.options.use_scaling=1;
    config.options.scaled_width=out->w;
    config.options.scaled_height=out->h;
    bool ok=WebPDecode(data,size,&config)==VP8_STATUS_OK;
    WebPFreeDecBuffer(&config.output);
    if(!ok){free(out->rgba);memset(out,0,sizeof *out);}
    return ok;
}
static bool ri_decode(const unsigned char *data, size_t size, RIPixels *out) {
    memset(out, 0, sizeof *out);
    if (!data || !size || size > RI_PAYLOAD) return false;
    if(size>=12&&!memcmp(data,"RIFF",4)&&!memcmp(data+8,"WEBP",4))return ri_webp(data,size,out);
    if (size >= 8 && !png_sig_cmp(data, 0, 8)) return ri_png(data, size, out);
    if (size >= 3 && data[0] == 255 && data[1] == 216 && data[2] == 255) return ri_jpeg(data, size, out);
    if (size >= 6 && (!memcmp(data, "GIF87a", 6) || !memcmp(data, "GIF89a", 6))) return ri_gif(data, size, out);
    return remote_svg_decode(data, size, &out->rgba, &out->w, &out->h);
}

#ifndef REMOTE_IMAGE_HOST_TEST
#define RI_ENTRIES 64
#define RI_URL 2048
#define RI_GPU_SLOTS 24
#define RI_GPU_BYTES (1024u * 1024u)
#define RI_DISK_SLOTS 16
#define RI_RETRY_MS 3000u

typedef enum { RI_EMPTY, RI_QUEUED, RI_WORKING, RI_READY, RI_FAILED } RIState;
typedef struct {
    char url[RI_URL];
    RIState state;
    int w, h, gpu, round_side, round_radius;
    unsigned tries;
    uint64_t touched, retry_at;
} RIEntry;
typedef struct {
    C3D_Tex texture;
    Tex3DS_SubTexture sub;
    size_t bytes;
    int entry;
    bool live;
} RIGpu;
typedef struct {
    char url[RI_URL];
    RIPixels pixels;
    int entry, round_side, round_radius;
    bool ok;
} RIJob;
static RIEntry ri_entries[RI_ENTRIES];
static RIGpu ri_gpu[RI_GPU_SLOTS];
static RIJob ri_job;
static Thread ri_thread;
static LightEvent ri_wake;
static int ri_job_state; // atomic: 0 idle, 1 worker owns job, 2 main owns result
static bool ri_stop, ri_shutdown, ri_changed;
static uint64_t ri_clock;
static size_t ri_gpu_bytes;
static bool ri_stopping(void) { return __atomic_load_n(&ri_stop, __ATOMIC_ACQUIRE); }
static bool ri_valid_url(const char *url) {
    if (!url || strncmp(url, "https://", 8)) return false;
    size_t n = strnlen(url, RI_URL);
    if (n <= 8 || n >= RI_URL || url[8] == '/' || url[8] == '?' || url[8] == '#') return false;
    for (size_t i = 8; i < n; i++) if ((unsigned char)url[i] <= 32 || (unsigned char)url[i] == 127) return false;
    return true;
}
static int ri_find_variant(const char *url,int side,int radius) {
    if (!url) return -1;
    for (int i = 0; i < RI_ENTRIES; i++)
        if (ri_entries[i].state != RI_EMPTY && ri_entries[i].round_side==side && ri_entries[i].round_radius==radius && !strcmp(url, ri_entries[i].url)) return i;
    return -1;
}
static int ri_find(const char *url){return ri_find_variant(url,0,0);}
static bool ri_cancelled(void *ctx){return ri_stopping();}
static bool ri_download(const char *url,RIPixels *out){
    size_t bytes=0;char error[128];bool refresh=!http_cache_fresh(url,"*/*",86400);
    char *data=http_cache_get(url,"*/*",RI_PAYLOAD,refresh,ri_cancelled,NULL,&bytes,error,sizeof error);
    bool ok=data&&!ri_stopping()&&ri_decode((unsigned char*)data,bytes,out);if(data&&!ok&&!ri_stopping())http_cache_invalidate(url,"*/*");free(data);return ok;
}
static void ri_worker(void *unused) {
    (void)unused;
    for (;;) {
        LightEvent_Wait(&ri_wake);
        if (ri_stopping()) break;
        if (__atomic_load_n(&ri_job_state, __ATOMIC_ACQUIRE) != 1) continue;
        bg_wait();
        ri_job.ok = ri_download(ri_job.url, &ri_job.pixels);
        // Disk cache retains the unmodified source, shared by all shape variants.
        if(ri_job.ok&&ri_job.round_side){
            unsigned char *rounded=image_round_rgba(ri_job.pixels.rgba,ri_job.pixels.w,ri_job.pixels.h,ri_job.round_side,ri_job.round_radius);
            free(ri_job.pixels.rgba);ri_job.pixels.rgba=rounded;
            ri_job.pixels.w=ri_job.pixels.h=ri_job.round_side;ri_job.ok=rounded!=NULL;
        }
        __atomic_store_n(&ri_job_state, 2, __ATOMIC_RELEASE);
    }
}
static bool ri_start(void) {
    if (ri_shutdown) return false;
    if (ri_thread) return true;
    LightEvent_Init(&ri_wake, RESET_ONESHOT);
    s32 priority = 0x30; svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    ri_thread = threadCreate(ri_worker, NULL, 64 * 1024, priority + 2, -2, false);
    return ri_thread != NULL;
}
static bool ri_request_variant(const char *url,int side,int radius) {
    if (ri_shutdown || !ri_valid_url(url)) return false;
    int i = ri_find_variant(url,side,radius);
    if (i < 0) {
        uint64_t oldest = UINT64_MAX;
        for (int j = 0; j < RI_ENTRIES; j++) {
            RIEntry *e = &ri_entries[j];
            if (e->state == RI_EMPTY) { i = j; break; }
            if (e->state != RI_WORKING && e->gpu < 0 && e->touched < oldest) { oldest = e->touched; i = j; }
        }
        if (i < 0) return false;
        RIEntry *e = &ri_entries[i];
        memset(e, 0, sizeof *e); strcpy(e->url, url); e->gpu = -1;
        e->round_side=side;e->round_radius=radius;
        e->state = RI_QUEUED;
    }
    RIEntry *e = &ri_entries[i]; e->touched = ++ri_clock;
    if (e->state == RI_READY && e->gpu < 0) e->state = RI_QUEUED;
    if (e->state == RI_FAILED && e->tries < 4 && osGetTime() >= e->retry_at) e->state = RI_QUEUED;
    if (!ri_start()) { e->state = RI_FAILED; return false; }
    return true;
}
bool remote_image_request(const char *url){return ri_request_variant(url,0,0);}
bool remote_image_request_rounded(const char *url,int side,int radius){
    return side>0&&side<=128&&radius>=0&&radius<=side/2&&ri_request_variant(url,side,radius);
}
bool remote_image_dimensions(const char *url, int *w, int *h) {
    int i = ri_find(url);
    if (i < 0 || !ri_entries[i].w || !ri_entries[i].h) return false;
    if (w) *w = ri_entries[i].w;
    if (h) *h = ri_entries[i].h;
    return true;
}
bool remote_image_changed(void) { bool changed = ri_changed; ri_changed = false; return changed; }
static void ri_evict(int slot) {
    RIGpu *g = &ri_gpu[slot];
    if (!g->live) return;
    ri_entries[g->entry].gpu = -1;
    C3D_TexDelete(&g->texture);
    ri_gpu_bytes -= g->bytes;
    memset(g, 0, sizeof *g);
}
static int ri_oldest_gpu(void) {
    int oldest = -1;
    for (int i = 0; i < RI_GPU_SLOTS; i++)
        if (ri_gpu[i].live && (oldest < 0 || ri_entries[ri_gpu[i].entry].touched < ri_entries[ri_gpu[oldest].entry].touched)) oldest = i;
    return oldest;
}
void remote_image_trim(void){
    // Keep a small warm set of avatars/badges when leaving the reader. Large
    // screenshots are released for the video player; returning to GitHub need
    // not decode every small SVG again. The retained budget is at most 128 KiB.
    for(int i=0;i<RI_GPU_SLOTS;i++)
        if(ri_gpu[i].live&&ri_gpu[i].bytes>32768)ri_evict(i);
    while(ri_gpu_bytes>128*1024){int old=ri_oldest_gpu();if(old<0)break;ri_evict(old);}
    for(int i=0;i<RI_ENTRIES;i++)if(ri_entries[i].state==RI_QUEUED)ri_entries[i].state=RI_READY;
}
static bool ri_upload(int entry, const RIPixels *p) {
    int tw = 8, th = 8;
    while (tw < p->w) tw *= 2;
    while (th < p->h) th *= 2;
    size_t bytes = (size_t)tw * th * 4;
    while (ri_gpu_bytes + bytes > RI_GPU_BYTES) {
        int old = ri_oldest_gpu(); if (old < 0) return false; ri_evict(old);
    }
    int slot = -1;
    for (int i = 0; i < RI_GPU_SLOTS; i++) if (!ri_gpu[i].live) { slot = i; break; }
    if (slot < 0) { slot = ri_oldest_gpu(); ri_evict(slot); }
    RIGpu *g = &ri_gpu[slot];
    if (!C3D_TexInit(&g->texture, tw, th, GPU_RGBA8)) return false;
    g->live = true; g->bytes = bytes; g->entry = entry; ri_gpu_bytes += bytes;
    C3D_TexSetFilter(&g->texture, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&g->texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    // ABGR byte order and Morton layout match pack.py/gfx.c. Extend edge pixels
    // through POT padding so bilinear filtering cannot introduce a black fringe.
    unsigned char *dest = g->texture.data;
    for (int y = 0; y < th; y++) for (int x = 0; x < tw; x++) {
        unsigned m = (x&1) | ((y&1)<<1) | ((x&2)<<1) | ((y&2)<<2) | ((x&4)<<2) | ((y&4)<<3);
        size_t at = ((((y>>3) * (tw>>3) + (x>>3)) << 6) + m) * 4;
        const unsigned char *src = p->rgba + ((y < p->h ? y : p->h-1)*p->w + (x < p->w ? x : p->w-1))*4;
        dest[at] = src[3]; dest[at+1] = src[2]; dest[at+2] = src[1]; dest[at+3] = src[0];
    }
    C3D_TexFlush(&g->texture);
    g->sub = (Tex3DS_SubTexture){p->w, p->h, 0, 1, (float)p->w/tw, 1-(float)p->h/th};
    ri_entries[entry].gpu = slot;
    return true;
}
void remote_image_pump(void) {
    if (ri_shutdown || !ri_thread) return;
    int state = __atomic_load_n(&ri_job_state, __ATOMIC_ACQUIRE);
    if (state == 2) {
        RIEntry *e = &ri_entries[ri_job.entry];
        if (ri_job.ok && ri_job.pixels.rgba && ri_job.pixels.w > 0 && ri_job.pixels.w <= RI_WIDTH &&
            ri_job.pixels.h > 0 && ri_job.pixels.h <= RI_HEIGHT) {
            if (e->w != ri_job.pixels.w || e->h != ri_job.pixels.h) ri_changed = true;
            e->w = ri_job.pixels.w; e->h = ri_job.pixels.h;
            e->state = ri_upload(ri_job.entry, &ri_job.pixels) ? RI_READY : RI_FAILED;
            if (e->state == RI_READY) e->tries = 0;
        } else e->state = RI_FAILED;
        if (e->state == RI_FAILED) e->retry_at = osGetTime() + RI_RETRY_MS;
        free(ri_job.pixels.rgba); memset(&ri_job, 0, sizeof ri_job);
        __atomic_store_n(&ri_job_state, 0, __ATOMIC_RELEASE);
        state = 0;
    }
    if (state == 0) {
        // Recently requested visible images win over old document queues.
        int next = -1;
        for (int i = 0; i < RI_ENTRIES; i++)
            if (ri_entries[i].state == RI_QUEUED && (next < 0 || ri_entries[i].touched > ri_entries[next].touched)) next = i;
        if (next >= 0) {
            RIEntry *e = &ri_entries[next]; e->state = RI_WORKING; e->tries++;
            ri_job.entry = next; strcpy(ri_job.url, e->url);
            ri_job.round_side=e->round_side;ri_job.round_radius=e->round_radius;
            __atomic_store_n(&ri_job_state, 1, __ATOMIC_RELEASE);
            LightEvent_Signal(&ri_wake);
        }
    }
}
// 5x7 uppercase bitmap status text avoids gfx text-cache allocations in draw.
static const unsigned char ri_letters[26][5] = {
 {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
 {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
 {0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
 {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
 {62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},
 {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
 {3,4,120,4,3},{97,81,73,69,67}
};
static void ri_placeholder(float x, float y, float w, float h, float alpha, bool failed) {
    x += DRAW_X; y += DRAW_Y;
    C2D_DrawRectSolid(x, y, .5f, w, h, C2D_Color32(80, 86, 96, (unsigned char)(36*alpha)));
    const char *label = failed ? "IMAGE UNAVAILABLE" : "LOADING IMAGE";
    int length = strlen(label);
    if (w < length * 6 + 4 || h < 11) { label = "IMAGE"; length = 5; }
    if (w < 32 || h < 9) return;
    float bx = x + (w - length*6 + 1)*.5f, by = y + (h-7)*.5f;
    u32 col = C2D_Color32(133, 140, 152, (unsigned char)(255*alpha));
    for (int i = 0; i < length; i++) {
        int ch = label[i]-'A'; if (ch < 0 || ch >= 26) continue;
        for (int xx = 0; xx < 5; xx++) for (int yy = 0; yy < 7; yy++)
            if (ri_letters[ch][xx] & (1 << yy)) C2D_DrawRectSolid(bx+i*6+xx, by+yy, .5f, 1, 1, col);
    }
}
static bool ri_draw_variant(const char *url,float x,float y,float w,float h,float alpha,int side,int radius) {
    if (!isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h) || !isfinite(alpha) || w <= 0 || h <= 0 || alpha <= 0) return false;
    alpha = fminf(alpha, 1);
    int i = ri_find_variant(url,side,radius);
    if (i >= 0 && ri_entries[i].gpu >= 0) {
        RIGpu *g = &ri_gpu[ri_entries[i].gpu];
        C2D_Image image = {&g->texture, &g->sub};
        draw_img(image, x, y, w/g->sub.width, h/g->sub.height, alpha);
        return true;
    }
    if(side){
        rrect(x,y,w,h,radius,C2D_Color32(80,86,96,(unsigned char)(36*alpha)));
        draw_icon("github",x+w*.5f,y+h*.5f,fminf(20,side*.5f),C2D_Color32(133,140,152,(unsigned char)(255*alpha)));
    }else ri_placeholder(x, y, w, h, alpha, i < 0 || ri_entries[i].state == RI_FAILED);
    return false;
}
bool remote_image_draw(const char *url,float x,float y,float w,float h,float alpha){return ri_draw_variant(url,x,y,w,h,alpha,0,0);}
bool remote_image_draw_rounded(const char *url,float x,float y,int side,int radius,float alpha){
    return side>0&&side<=128&&radius>=0&&radius<=side/2&&ri_draw_variant(url,x,y,side,side,alpha,side,radius);
}
bool remote_image_ready(const char *url){int i=ri_find(url);return i>=0&&ri_entries[i].gpu>=0;}
void remote_image_stop(void) {
    ri_shutdown = true;
    __atomic_store_n(&ri_stop, true, __ATOMIC_RELEASE);
    if (ri_thread) LightEvent_Signal(&ri_wake);
}
void remote_image_exit(void) {
    remote_image_stop();
    // Final shutdown only, never panel navigation. A download that has not returned by
    // the deadline keeps its buffers; nothing below may be freed under it.
    if (!thread_reap(&ri_thread)) return;
    free(ri_job.pixels.rgba); memset(&ri_job, 0, sizeof ri_job);
    for (int i = 0; i < RI_GPU_SLOTS; i++) ri_evict(i);
    memset(ri_entries, 0, sizeof ri_entries);
    ri_changed = false;
}
#endif

// Included intentionally: the existing build only enumerates source/*.c.
#include "vendor/remote_svg.c"
