/* Standalone malformed-file regressions for both camera decoders.
 * gcc -DSE_HOSTED -fsanitize=address,undefined -fno-sanitize-recover=all
 *   src/tools/cr2_safety_test.c src/engine/{se,cr2,jpeg_dec}.c -o parser_safety
 */
#include "../engine/cr2.h"
#include "../engine/jpeg_dec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static void check(int ok, const char *name)
{
    checks++;
    if (!ok) { fprintf(stderr, "FAIL: %s\n", name); exit(1); }
}

static void le16(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }
static void le32(unsigned char *p, unsigned v)
{ p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void be16(unsigned char *p, unsigned v) { p[0] = v >> 8; p[1] = v; }

static size_t marker(unsigned char *p, size_t n, unsigned m)
{
    size_t i;
    for (i = 0; i + 1 < n; i++) if (p[i] == 255 && p[i + 1] == m) return i;
    check(0, "fixture marker exists");
    return 0;
}

/* A real, decodable SOF3 stream: four 12-bit samples, all exactly 2048. */
static size_t lossless(unsigned char *p)
{
    size_t n = 0;
    unsigned char sof[] = {255,195,0,11,12,0,2,0,2,1,1,17,0};
    unsigned char sos[] = {255,218,0,8,1,1,0,1,0,0};
    p[n++] = 255; p[n++] = 216;
    memcpy(p + n, sof, sizeof sof); n += sizeof sof;
    p[n++] = 255; p[n++] = 196; p[n++] = 0; p[n++] = 20;
    p[n++] = 0; p[n++] = 1;
    memset(p + n, 0, 15); n += 15;
    p[n++] = 0; /* category zero, one-bit code zero */
    memcpy(p + n, sos, sizeof sos); n += sizeof sos;
    p[n++] = 15; /* four zero codes and four one pad bits */
    p[n++] = 255; p[n++] = 217;
    return n;
}

/* Ordinary 8x8 JPEG, DC=0/EOB for each component, all RGB channels=128. */
static size_t baseline(unsigned char *p, int comps)
{
    size_t n = 0;
    int i, t;
    p[n++] = 255; p[n++] = 216;
    p[n++] = 255; p[n++] = 219; p[n++] = 0; p[n++] = 67; p[n++] = 0;
    memset(p + n, 1, 64); n += 64;
    p[n++] = 255; p[n++] = 192;
    be16(p + n, 8 + comps * 3); n += 2;
    p[n++] = 8; p[n++] = 0; p[n++] = 8; p[n++] = 0; p[n++] = 8;
    p[n++] = (unsigned char)comps;
    for (i = 0; i < comps; i++) { p[n++] = i + 1; p[n++] = 17; p[n++] = 0; }
    p[n++] = 255; p[n++] = 196; p[n++] = 0; p[n++] = 38;
    for (t = 0; t < 2; t++) {
        p[n++] = t ? 16 : 0; p[n++] = 1;
        memset(p + n, 0, 15); n += 15; p[n++] = 0;
    }
    p[n++] = 255; p[n++] = 218;
    be16(p + n, 6 + comps * 2); n += 2; p[n++] = (unsigned char)comps;
    for (i = 0; i < comps; i++) { p[n++] = i + 1; p[n++] = 0; }
    p[n++] = 0; p[n++] = 63; p[n++] = 0;
    p[n++] = comps == 1 ? 63 : 3;
    p[n++] = 255; p[n++] = 217;
    return n;
}

static void entry(unsigned char *p, unsigned tag, unsigned val)
{ le16(p, tag); le16(p + 2, 4); le32(p + 4, 1); le32(p + 8, val); }

static size_t cr2_fixture(unsigned char *p)
{
    size_t n;
    memset(p, 0, 512);
    p[0] = p[1] = 'I'; p[2] = 42; le32(p + 4, 16);
    p[8] = 'C'; p[9] = 'R'; p[10] = 2; le32(p + 12, 64);
    le16(p + 64, 5);
    entry(p + 66, 256, 2); entry(p + 78, 257, 2);
    entry(p + 90, 259, 6); entry(p + 102, 273, 160);
    n = lossless(p + 160); entry(p + 114, 279, (unsigned)n);
    return 160 + n;
}

typedef struct { const unsigned char *p; size_t n; unsigned calls; } reader;
static int read_at(void *ctx, uint32_t off, void *dst, uint32_t n)
{
    reader *r = ctx;
    r->calls++;
    if (off > r->n || n > r->n - off) return -1;
    memcpy(dst, r->p + off, n);
    return 0;
}

typedef struct { unsigned rows, count; int check_flat; } output;
static int raw_sink(void *ctx, int row, int col, int n, const uint16_t *p)
{
    output *o = ctx;
    int i;
    if (row < 0 || row >= 2 || col < 0 || n < 0 || col + n > 2) return -1;
    for (i = 0; i < n; i++) if (p[i] != 2048) return -1;
    o->count += n; o->rows++;
    return 0;
}

static int rgb_sink(void *ctx, const uint8_t *p, int rows, int width)
{
    output *o = ctx;
    size_t i, n = (size_t)rows * width * 3;
    if (rows <= 0 || width <= 0 || width > 65535) return -1;
    if (o->check_flat) for (i = 0; i < n; i++) if (p[i] != 128) return -1;
    o->rows += rows; o->count += (unsigned)n;
    return 0;
}

static se_err decode_raw(const unsigned char *p, size_t n, const se_cr2_info *info, int stream)
{
    unsigned char arena_mem[131072];
    se_arena a;
    output o = {0,0,0};
    reader r = {p,n,0};
    se_arena_init(&a, arena_mem, sizeof arena_mem);
    return stream ? se_cr2_decode_stream(read_at, &r, info, raw_sink, &o, &a) :
                    se_cr2_decode_sink(p, n, info, raw_sink, &o, &a);
}

static se_err decode_jpeg(const unsigned char *p, size_t n, int scale, int flat, int stream)
{
    unsigned char *amem = malloc(4u << 20);
    se_arena a;
    se_jdec_info info;
    output o = {0,0,flat};
    reader r = {p,n,0};
    se_err rc;
    check(amem != NULL, "test arena allocation");
    se_arena_init(&a, amem, 4u << 20);
    rc = stream ? se_jpeg_decode_stream_scaled(p, n, (uint32_t)n, read_at, &r, scale,
                                               &a, rgb_sink, &o, &info) :
                  se_jpeg_decode_scaled(p, n, scale, &a, rgb_sink, &o, &info);
    if (rc == SE_OK && flat)
        check(o.rows == (unsigned)(8 / scale) && o.count == (unsigned)(64 * 3 / scale / scale),
              "valid JPEG delivered every requested pixel");
    free(amem);
    return rc;
}

static void raw_tests(void)
{
    unsigned char b[512], m[512];
    size_t n = cr2_fixture(b), s = 160, dht, sof, sos, i;
    se_cr2_info info, bad;
    check(se_cr2_parse(b, n, &info) == SE_OK, "valid CR2 metadata");
    check(decode_raw(b, n, &info, 0) == SE_OK, "valid lossless JPEG memory decode");
    check(decode_raw(b, n, &info, 1) == SE_OK, "valid lossless JPEG streaming decode");
    dht = s + marker(b + s, n - s, 196);
    sof = s + marker(b + s, n - s, 195);
    sos = s + marker(b + s, n - s, 218);
#define RAW_BAD(label, change) do { memcpy(m,b,n); change; \
    check(decode_raw(m,n,&info,0) != SE_OK,label); \
    check(decode_raw(m,n,&info,1) != SE_OK,label " stream"); } while(0)
    RAW_BAD("lossless selector outside table array", m[sos+6]=0x40);
    RAW_BAD("oversubscribed lossless Huffman table", m[dht+5]=3);
    RAW_BAD("invalid lossless magnitude symbol", m[dht+21]=255);
    RAW_BAD("zero lossless precision", m[sof+4]=0);
    RAW_BAD("oversized lossless precision", m[sof+4]=255);
    RAW_BAD("point transform exceeds precision", m[sos+9]=12);
    RAW_BAD("short SOF segment despite following file data", m[sof+3]=2);
    RAW_BAD("short DHT segment despite following file data", m[dht+3]=3);
    RAW_BAD("short SOS segment despite following file data", m[sos+3]=2);
    RAW_BAD("entropy data truncated before first sample", m[sos+10]=255; m[sos+11]=217);
#undef RAW_BAD
    bad = info; bad.slice_count = 1; bad.slice_width = 0; bad.slice_last = 2;
    check(decode_raw(b,n,&bad,0) != SE_OK, "zero slice width cannot divide by zero");
    bad.slice_width = 2; bad.slice_last = 0;
    check(decode_raw(b,n,&bad,0) != SE_OK, "zero final slice width rejected");
    bad = info; bad.raw_width = 1; bad.raw_height = 4;
    check(decode_raw(b,n,&bad,0) != SE_OK, "equal sample count with wrong raster rejected");
    bad = info; bad.strip_offset = UINT32_MAX; bad.strip_bytes = 20;
    check(decode_raw(b,n,&bad,0) != SE_OK, "32-bit strip offset overflow rejected");
    memcpy(m,b,n); le32(m+4,UINT32_MAX);
    check(se_cr2_parse(m,n,&bad) != SE_OK, "overflowing TIFF directory offset rejected");
    memcpy(m,b,n); le32(m+74,UINT32_MAX);
    check(se_cr2_parse(m,n,&bad) != SE_OK, "negative/oversized raw dimensions rejected");
    /* Exact-size allocations turn every short-prefix read into an ASan failure. */
    for (i = 0; i < n; i++) {
        unsigned char *shortbuf = malloc(i ? i : 1);
        memcpy(shortbuf,b,i);
        (void)se_cr2_parse(shortbuf,i,&bad);
        (void)decode_raw(shortbuf,i,&info,0);
        free(shortbuf);
    }
}

static void jpeg_tests(void)
{
    unsigned char b[512], m[512];
    size_t n = baseline(b,3), sof, dht, dqt, sos, i;
    se_jdec_info ji;
    int c, scale;
    for (c = 1; c <= 3; c += 2) {
        n = baseline(b,c);
        for (scale=1;scale<=2;scale++) {
            check(decode_jpeg(b,n,scale,1,0)==SE_OK,"valid baseline JPEG pixels");
            check(decode_jpeg(b,n,scale,1,1)==SE_OK,"valid streamed JPEG pixels");
        }
    }
    sof=marker(b,n,192); dht=marker(b,n,196);
    dqt=marker(b,n,219); sos=marker(b,n,218);
#define JPG_BAD(label, change) do { memcpy(m,b,n); change; \
    check(se_jpeg_dec_info(m,n,&ji)!=SE_OK,label " header"); \
    check(decode_jpeg(m,n,1,0,0)!=SE_OK,label); } while(0)
    JPG_BAD("short baseline SOF", m[sof+3]=2);
    JPG_BAD("short baseline DHT", m[dht+3]=3);
    JPG_BAD("short baseline DQT", m[dqt+3]=3);
    JPG_BAD("short baseline SOS", m[sos+3]=2);
    JPG_BAD("out-of-range quantization selector", m[sof+12]=4);
    JPG_BAD("out-of-range Huffman selectors", m[sos+6]=0x44);
    JPG_BAD("oversubscribed baseline Huffman tree", m[dht+5]=3);
    JPG_BAD("invalid Huffman table class", m[dht+4]=0x20);
    JPG_BAD("invalid DC category", m[dht+21]=255);
    JPG_BAD("invalid AC category", m[dht+39]=11);
    JPG_BAD("invalid quantization precision", m[dqt+4]=0x20);
    JPG_BAD("unsupported spectral selection", m[sos+12]=1);
    JPG_BAD("subsampled Y would under-read its plane", m[sof+14]=0x21);
#undef JPG_BAD
    memcpy(m,b,n); m[sos+14]=255; m[sos+15]=217;
    check(decode_jpeg(m,n,1,0,0)!=SE_OK,"truncated baseline entropy rejected");
    {
        unsigned char amem[131072];
        se_arena a;
        reader r={b,n,0};
        output o={0,0,0};
        se_arena_init(&a,amem,sizeof amem);
        check(se_jpeg_decode_stream_scaled(b,n,1,read_at,&r,1,&a,rgb_sink,&o,NULL)!=SE_OK,
              "stream length smaller than scan cannot underflow");
        check(!r.calls,"invalid stream range rejected before I/O");
        se_arena_init(&a,amem,sizeof amem); r.n=0;
        check(se_jpeg_decode_stream_scaled(b,n,(uint32_t)n,read_at,&r,1,&a,rgb_sink,&o,NULL)==SE_E_IO,
              "stream read failure is an error, never synthetic pixels");
    }
    for(i=0;i<n;i++) {
        unsigned char *shortbuf=malloc(i?i:1);
        memcpy(shortbuf,b,i);
        (void)se_jpeg_dec_info(shortbuf,i,&ji);
        (void)decode_jpeg(shortbuf,i,1,0,0);
        free(shortbuf);
    }
    /* Target every header byte, including count, length and table selectors. */
    for(i=2;i<sos+14;i++) {
        memcpy(m,b,n); m[i]=255;
        (void)se_jpeg_dec_info(m,n,&ji);
        (void)decode_jpeg(m,n,1,0,0);
    }
}

int main(int argc,char **argv)
{
    int i;
    raw_tests(); jpeg_tests();
    for(i=1;i<argc;i++) {
        FILE *f=fopen(argv[i],"rb");
        long len;
        unsigned char *p;
        check(f!=NULL,"external fixture opens");
        check(fseek(f,0,SEEK_END)==0,"fixture seek"); len=ftell(f);
        check(len>0 && len<(128<<20),"bounded external fixture");
        rewind(f); p=malloc((size_t)len); check(p!=NULL,"fixture allocation");
        check(fread(p,1,(size_t)len,f)==(size_t)len,"fixture read"); fclose(f);
        if(len>10 && p[0]=='I' && p[8]=='C' && p[9]=='R') {
            uint32_t off,bytes;
            check(se_cr2_find_preview(p,(size_t)len,&off,&bytes,NULL,NULL)==SE_OK,
                  "real Canon preview located");
            check(off<=(size_t)len && bytes<=(size_t)len-off,"real preview range");
            check(decode_jpeg(p+off,bytes,1,0,0)==SE_OK,"real Canon JPEG preview full-size");
            check(decode_jpeg(p+off,bytes,2,0,1)==SE_OK,"real Canon JPEG preview half-size stream");
        } else {
            se_err stream_rc;
            check(decode_jpeg(p,(size_t)len,1,0,0)==SE_OK,"external baseline JPEG full-size");
            stream_rc=decode_jpeg(p,(size_t)len,2,0,1);
            /* Restart-bearing files are intentionally unsupported by the
             * forward-only streaming API. Canon's tested preview has no DRI. */
            check(stream_rc==SE_OK || stream_rc==SE_E_UNSUPPORTED,
                  "external baseline JPEG half-size stream or explicit unsupported variant");
        }
        free(p);
    }
    printf("parser safety: %u checks passed (synthetic raw/JPEG, streaming, truncations, mutations)\n",checks);
    return 0;
}
