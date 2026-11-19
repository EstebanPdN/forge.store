// Host build tool: interleaved stereo PCM16 (32 kHz, little-endian) -> Ogg Vorbis.
// The stream serial is fixed, so the same input and quality always give the same bytes
// and an unchanged sound keeps its hash between builds.
// Usage: vorbis-encode <quality -0.1..1> <input.pcm> <output.ogg>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vorbis/vorbisenc.h>

static int write_page(FILE *out, const ogg_page *page) {
    return fwrite(page->header, 1, page->header_len, out) == (size_t)page->header_len &&
           fwrite(page->body, 1, page->body_len, out) == (size_t)page->body_len;
}

int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: vorbis-encode quality in.pcm out.ogg\n"); return 2; }
    float quality = strtof(argv[1], NULL);
    FILE *in = fopen(argv[2], "rb");
    if (!in) { perror(argv[2]); return 1; }
    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    rewind(in);
    if (size <= 0 || size % 4) { fprintf(stderr, "%s: not stereo PCM16\n", argv[2]); return 1; }
    short *pcm = malloc(size);
    if (!pcm || fread(pcm, 1, size, in) != (size_t)size) { fprintf(stderr, "read failed\n"); return 1; }
    fclose(in);
    long frames = size / 4;

    vorbis_info vi;
    vorbis_info_init(&vi);
    if (vorbis_encode_init_vbr(&vi, 2, 32000, quality)) { fprintf(stderr, "unsupported quality\n"); return 1; }
    vorbis_comment vc;
    vorbis_comment_init(&vc);
    vorbis_comment_add_tag(&vc, "ENCODER", "forge.store");
    vorbis_dsp_state vd;
    vorbis_block vb;
    vorbis_analysis_init(&vd, &vi);
    vorbis_block_init(&vd, &vb);
    ogg_stream_state os;
    ogg_stream_init(&os, 0x46524745);  // "FRGE"
    ogg_packet header, comments, codebooks;
    vorbis_analysis_headerout(&vd, &vc, &header, &comments, &codebooks);
    ogg_stream_packetin(&os, &header);
    ogg_stream_packetin(&os, &comments);
    ogg_stream_packetin(&os, &codebooks);

    FILE *out = fopen(argv[3], "wb");
    if (!out) { perror(argv[3]); return 1; }
    ogg_page page;
    int ok = 1;
    while (ok && ogg_stream_flush(&os, &page)) ok = write_page(out, &page);

    long at = 0;
    for (;;) {
        long n = frames - at > 1024 ? 1024 : frames - at;
        if (n > 0) {
            float **buffer = vorbis_analysis_buffer(&vd, (int)n);
            for (long i = 0; i < n; i++) {
                buffer[0][i] = pcm[(at + i) * 2] / 32768.f;
                buffer[1][i] = pcm[(at + i) * 2 + 1] / 32768.f;
            }
            vorbis_analysis_wrote(&vd, (int)n);
            at += n;
        } else {
            vorbis_analysis_wrote(&vd, 0);  // end of stream: the last granule holds the exact length
        }
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, NULL);
            vorbis_bitrate_addblock(&vb);
            ogg_packet packet;
            while (vorbis_bitrate_flushpacket(&vd, &packet)) {
                ogg_stream_packetin(&os, &packet);
                while (ok && ogg_stream_pageout(&os, &page)) ok = write_page(out, &page);
            }
        }
        if (n <= 0) break;
    }
    while (ok && ogg_stream_flush(&os, &page)) ok = write_page(out, &page);
    ok = ok && fclose(out) == 0;
    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    free(pcm);
    if (!ok) { fprintf(stderr, "write failed\n"); remove(argv[3]); return 1; }
    return 0;
}
