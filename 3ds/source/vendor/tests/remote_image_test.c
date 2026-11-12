// Host decoder integration test. Compile this single file (see run-remote-tests.sh).
#define REMOTE_IMAGE_HOST_TEST
#include "../../remote_image.c"
#include <assert.h>

typedef struct { unsigned char *data; size_t size; } TestBytes;
static void test_png_write(png_structp p, png_bytep b, png_size_t n) {
    TestBytes *out = png_get_io_ptr(p);
    unsigned char *next = realloc(out->data, out->size + n);
    assert(next); out->data = next;
    memcpy(out->data + out->size, b, n); out->size += n;
}
static void test_png_flush(png_structp p) { (void)p; }
static TestBytes make_png(int w, int h, bool interlace) {
    TestBytes bytes = {0};
    png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(p);
    assert(p && info && !setjmp(png_jmpbuf(p)));
    png_set_write_fn(p, &bytes, test_png_write, test_png_flush);
    png_set_IHDR(p, info, w, h, 8, PNG_COLOR_TYPE_RGBA, interlace ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(p, info);
    int passes = interlace ? png_set_interlace_handling(p) : 1;
    unsigned char *row = malloc(w*4); assert(row);
    for (int x = 0; x < w; x++) { row[x*4] = 243; row[x*4+1] = 42; row[x*4+2] = 17; row[x*4+3] = 191; }
    for (int pass = 0; pass < passes; pass++) for (int y = 0; y < h; y++) png_write_row(p, row);
    png_write_end(p, info); png_destroy_write_struct(&p, &info); free(row);
    return bytes;
}
static TestBytes make_jpeg(int w, int h, bool progressive) {
    struct jpeg_compress_struct c = {0}; struct jpeg_error_mgr e;
    c.err = jpeg_std_error(&e); jpeg_create_compress(&c);
    unsigned char *memory = NULL; unsigned long size = 0;
    jpeg_mem_dest(&c, &memory, &size);
    c.image_width = w; c.image_height = h; c.input_components = 3; c.in_color_space = JCS_RGB;
    jpeg_set_defaults(&c); if (progressive) jpeg_simple_progression(&c);
    jpeg_start_compress(&c, TRUE);
    unsigned char *row = malloc(w*3); assert(row);
    for (int x = 0; x < w; x++) { row[x*3] = 243; row[x*3+1] = 42; row[x*3+2] = 17; }
    while (c.next_scanline < c.image_height) { JSAMPROW p = row; jpeg_write_scanlines(&c, &p, 1); }
    jpeg_finish_compress(&c); jpeg_destroy_compress(&c); free(row);
    return (TestBytes){memory, size};
}
static void expect_raster(TestBytes b, int w, int h, int alpha) {
    RIPixels p; assert(ri_decode(b.data, b.size, &p));
    assert(p.w == w && p.h == h);
    for (int i = 0; i < p.w*p.h; i++) {
        assert(abs(p.rgba[i*4]-243) < 8 && abs(p.rgba[i*4+1]-42) < 8 && abs(p.rgba[i*4+2]-17) < 8);
        assert(p.rgba[i*4+3] == alpha);
    }
    free(p.rgba);
    // Reject truncation at every prefix, not just the header.
    for (size_t n = 0; n < b.size; n += b.size/50+1) {
        bool ok = ri_decode(b.data, n, &p);
        assert(!ok && !p.rgba);
    }
    free(b.data);
}
int main(int argc, char **argv) {
    expect_raster(make_png(752, 320, false), 376, 160, 191);
    expect_raster(make_png(1, 1, false), 1, 1, 191);
    expect_raster(make_png(1, 8000, false), 1, 240, 191);
    expect_raster(make_jpeg(800, 400, false), 376, 188, 255);
    expect_raster(make_jpeg(160, 80, true), 160, 80, 255);
    RIPixels p;
    TestBytes large_progressive = make_jpeg(800, 400, true);
    assert(!ri_decode(large_progressive.data, large_progressive.size, &p) && !p.rgba); free(large_progressive.data);
    TestBytes interlace = make_png(32, 32, true);
    assert(!ri_decode(interlace.data, interlace.size, &p) && !p.rgba); free(interlace.data);
    TestBytes oversized = make_png(8193, 1, false);
    assert(!ri_decode(oversized.data, oversized.size, &p) && !p.rgba); free(oversized.data);
    assert(!ri_source_size(0, 100)); assert(!ri_source_size(8192, 8192)); assert(!ri_source_size(UINT_MAX, UINT_MAX));
    assert(!ri_decode((const unsigned char *)"x", RI_PAYLOAD+1, &p));
    const unsigned char gif[] = { 'G','I','F','8','9','a', 1,0,1,0, 0x80,0,0, 243,42,17, 0,0,0, ',',0,0,0,0,1,0,1,0,0,2,2,0x44,1,0,';' };
    assert(ri_decode(gif, sizeof gif, &p) && p.w == 1 && p.h == 1 && p.rgba[0] == 243 && p.rgba[3] == 255); free(p.rgba);
    for (size_t n = 0; n < sizeof gif-3; n++) assert(!ri_decode(gif,n,&p) && !p.rgba);
    unsigned char random[512]; uint32_t rng = 123;
    for (int trial = 0; trial < 1000; trial++) {
        for (size_t i = 0; i < sizeof random; i++) { rng = rng*1664525u+1013904223u; random[i] = rng>>24; }
        bool ok = ri_decode(random, sizeof random, &p); if (ok) free(p.rgba); else assert(!p.rgba);
    }
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb"); assert(f); fseek(f,0,SEEK_END); long n = ftell(f); rewind(f);
        assert(n > 0 && n < RI_PAYLOAD); unsigned char *b = malloc(n); assert(b && fread(b,1,n,f) == (size_t)n); fclose(f);
        assert(ri_decode(b,n,&p) && p.w <= RI_WIDTH && p.h <= RI_HEIGHT);
        printf("SVG %s: %dx%d\n", argv[i],p.w,p.h); free(p.rgba); free(b);
    }
    puts("PASS: PNG/JPEG/GIF pixels, sizing, truncation, malformed inputs and memory bounds");
    return 0;
}
