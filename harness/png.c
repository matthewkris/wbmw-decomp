/* Minimal PNG writer (8-bit RGB, deflate "stored" blocks), so the harness needs no libraries; and
 * a reader for its own files (the save slots' pictures). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "md.h"

static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_table[i] = v;
        }
    c = ~c;
    while (n--) c = crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void chunk(FILE *f, const char *type, const uint8_t *data, size_t n)
{
    uint8_t hdr[8]; put32(hdr, (uint32_t)n); for (int i = 0; i < 4; i++) hdr[4 + i] = type[i];
    fwrite(hdr, 1, 8, f); if (n) fwrite(data, 1, n, f);
    uint32_t c = crc(0, hdr + 4, 4); c = crc(c, data, n);
    uint8_t t[4]; put32(t, c); fwrite(t, 1, 4, f);
}

int png_write_rgb(const char *path, const uint32_t *px, int w, int h, int stride)
{
    size_t raw_n = (size_t)h * (1 + 3 * w);
    uint8_t *raw = malloc(raw_n), *q = raw;
    for (int y = 0; y < h; y++) {
        *q++ = 0;
        for (int x = 0; x < w; x++) { uint32_t v = px[y * stride + x]; *q++ = v >> 16; *q++ = v >> 8; *q++ = v; }
    }
    size_t blocks = (raw_n + 65534) / 65535;
    size_t z_n = 2 + raw_n + blocks * 5 + 4;
    uint8_t *z = malloc(z_n), *o = z;
    *o++ = 0x78; *o++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < raw_n; off += 65535) {
        size_t n = raw_n - off < 65535 ? raw_n - off : 65535;
        *o++ = off + n == raw_n; *o++ = n & 0xFF; *o++ = n >> 8; *o++ = ~n & 0xFF; *o++ = (~n >> 8) & 0xFF;
        for (size_t i = 0; i < n; i++) *o++ = raw[off + i];
    }
    put32(o, b << 16 | a); o += 4;
    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return -1; }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13]; put32(ihdr, w); put32(ihdr + 4, h); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13); chunk(f, "IDAT", z, (size_t)(o - z)); chunk(f, "IEND", NULL, 0);
    fclose(f); free(raw); free(z);
    return 0;
}

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

/* a PNG as png_write_rgb writes them (8-bit RGB, stored blocks, no filters) into px (max pixels,
 * rows of *w); anything else, or larger, fails (-1) */
int png_read_rgb(const char *path, uint32_t *px, int max, int *w, int *h)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 33 && n < (1 << 24) ? malloc((size_t)n) : NULL;
    int ok = d && fread(d, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    if (!ok || memcmp(d, "\x89PNG\r\n\x1a\n", 8) || memcmp(d + 12, "IHDR", 4)) { free(d); return -1; }
    *w = (int)get32(d + 16); *h = (int)get32(d + 20);
    if (d[24] != 8 || d[25] != 2 || *w <= 0 || *h <= 0 || (long)*w * *h > max) { free(d); return -1; }
    size_t raw_n = (size_t)*h * (1 + 3 * *w), got = 0;
    uint8_t *raw = malloc(raw_n);
    for (long p = 8; p + 12 <= n; ) {                   /* the IDAT chunks' stored blocks, after zlib's 2 bytes */
        uint32_t len = get32(d + p);
        if (p + 12 + (long)len > n) break;
        if (!memcmp(d + p + 4, "IDAT", 4)) {
            const uint8_t *z = d + p + 8 + 2, *end = d + p + 8 + len;
            while (z + 5 <= end && got < raw_n) {
                if (z[0] & 6) { got = 0; break; }       /* compressed: not ours */
                size_t k = z[1] | z[2] << 8;
                if (z + 5 + k > end || got + k > raw_n) { got = 0; break; }
                memcpy(raw + got, z + 5, k); got += k; z += 5 + k;
            }
        }
        p += 12 + len;
    }
    free(d);
    if (got != raw_n) { free(raw); return -1; }
    for (int y = 0; y < *h; y++) {
        const uint8_t *r = raw + (size_t)y * (1 + 3 * *w);
        if (r[0]) { free(raw); return -1; }
        for (int x = 0; x < *w; x++) px[y * *w + x] = (uint32_t)r[1 + 3 * x] << 16 | r[2 + 3 * x] << 8 | r[3 + 3 * x];
    }
    free(raw);
    return 0;
}
