/* VDP: registers, VRAM/CRAM/VSRAM, ports, DMA, status and HV counter, and a scanline renderer
 * for planes A and B, the window and sprites with priorities. Not modelled: shadow/highlight,
 * interlace, the sprite-per-line limits, FIFO timing, DMA timing (DMA completes at once). */
#include <string.h>
#include "md.h"

extern unsigned int m68k_read_memory_16(unsigned int a);

uint8_t vdp_reg[32];
uint8_t vram[0x10000];
uint16_t cram[64], vsram[40];
int vdp_line, vdp_line_cycle;
uint32_t frame_rgb[240][320];
int frame_width = 320;

static int pending, code;
static unsigned addr;
static int fill_pending, vblank, vint_pending;

void vdp_reset(void)
{
    memset(vdp_reg, 0, sizeof vdp_reg); memset(vram, 0, sizeof vram);
    memset(cram, 0, sizeof cram); memset(vsram, 0, sizeof vsram);
    pending = code = 0; addr = 0; fill_pending = vblank = vint_pending = 0;
}

void vdp_state_io(FILE *f, int saving)
{
    state_io(f, vdp_reg, sizeof vdp_reg, saving); state_io(f, vram, sizeof vram, saving);
    state_io(f, cram, sizeof cram, saving); state_io(f, vsram, sizeof vsram, saving);
    int v[6] = {pending, code, (int)addr, fill_pending, vblank, vint_pending};
    state_io(f, v, sizeof v, saving);
    if (!saving) { pending = v[0]; code = v[1]; addr = (unsigned)v[2]; fill_pending = v[3]; vblank = v[4]; vint_pending = v[5]; }
}

/* the port latches, for comparing VDP state (the VBlank and interrupt flags move with time) */
void vdp_latch(int v[4]) { v[0] = pending; v[1] = code; v[2] = (int)addr; v[3] = fill_pending; }

int vdp_vint_enabled(void) { return vdp_reg[1] & 0x20; }
int vdp_hint_enabled(void) { return vdp_reg[0] & 0x10; }
int vdp_hint_counter_reload(void) { return vdp_reg[10]; }
void vdp_set_vblank(int on) { vblank = on; }
void vdp_set_vint_pending(int on) { vint_pending = on; }

static void write_word(uint16_t w)
{
    switch (code & 0x0F) {
    case 0x01:                                       /* VRAM: big-endian, odd address swaps bytes */
        if (addr & 1) w = (uint16_t)(w << 8 | w >> 8);
        vram[addr & 0xFFFE] = w >> 8; vram[(addr & 0xFFFE) | 1] = w & 0xFF; break;
    case 0x03: cram[(addr >> 1) & 0x3F] = w & 0x0EEE; break;
    case 0x05: if (((addr >> 1) & 0x3F) < 40) vsram[(addr >> 1) & 0x3F] = w & 0x07FF; break;
    }
    addr = (addr + vdp_reg[15]) & 0xFFFF;
}

static unsigned dma_length(void)
{
    unsigned n = vdp_reg[19] | vdp_reg[20] << 8;
    return n ? n : 0x10000;
}

static void dma_68k(void)
{
    unsigned src = (vdp_reg[21] << 1) | (vdp_reg[22] << 9) | ((vdp_reg[23] & 0x7F) << 17);
    for (unsigned n = dma_length(); n; n--) {
        write_word((uint16_t)m68k_read_memory_16(src));
        src = (src & 0xFE0000) | ((src + 2) & 0x1FFFF);
    }
    vdp_reg[19] = vdp_reg[20] = 0;
}

static void dma_copy(void)
{
    unsigned src = vdp_reg[21] | vdp_reg[22] << 8;
    for (unsigned n = dma_length(); n; n--) {
        vram[addr] = vram[src & 0xFFFF];
        src++; addr = (addr + vdp_reg[15]) & 0xFFFF;
    }
    vdp_reg[19] = vdp_reg[20] = 0;
}

void vdp_write_ctrl(uint16_t w)
{
    if (pending) {
        pending = 0;
        addr = (addr & 0x3FFF) | (w & 3) << 14;
        code = (code & 0x03) | ((w >> 2) & 0x3C);
        if ((code & 0x20) && (vdp_reg[1] & 0x10)) {
            switch (vdp_reg[23] >> 6) {
            case 0: case 1: dma_68k(); break;
            case 2: fill_pending = 1; break;
            case 3: dma_copy(); break;
            }
        }
        return;
    }
    if ((w & 0xC000) == 0x8000) {                    /* register write */
        unsigned r = (w >> 8) & 0x1F;
        if (r < 24) vdp_reg[r] = w & 0xFF;
        code = 0;
        return;
    }
    code = (code & 0x3C) | w >> 14;
    addr = (addr & 0xC000) | (w & 0x3FFF);
    pending = 1;
}

void vdp_write_data(uint16_t w)
{
    pending = 0;
    if (fill_pending) {                               /* VRAM fill: high byte, one step per byte */
        fill_pending = 0;
        write_word(w);
        for (unsigned n = dma_length(); n; n--) {
            vram[addr ^ 1] = w >> 8;
            addr = (addr + vdp_reg[15]) & 0xFFFF;
        }
        vdp_reg[19] = vdp_reg[20] = 0;
        return;
    }
    write_word(w);
}

uint16_t vdp_read_data(void)
{
    uint16_t w = 0;
    pending = 0;
    switch (code & 0x0F) {
    case 0x00: w = vram[addr & 0xFFFE] << 8 | vram[(addr & 0xFFFE) | 1]; break;
    case 0x08: w = cram[(addr >> 1) & 0x3F]; break;
    case 0x04: w = vsram[((addr >> 1) & 0x3F) % 40]; break;
    }
    addr = (addr + vdp_reg[15]) & 0xFFFF;
    return w;
}

uint16_t vdp_read_status(void)
{
    int hblank = vdp_line_cycle > CYCLES_PER_LINE - 84;
    uint16_t s = 0x3400 | 0x0200                       /* FIFO empty */
               | (vint_pending ? 0x80 : 0) | (vblank ? 0x08 : 0) | (hblank ? 0x04 : 0);
    pending = 0;
    return s;
}

uint16_t vdp_read_hv(void)
{
    int v = vdp_line > 0xEA ? vdp_line - 6 : vdp_line;          /* NTSC: $00-$EA, $E5-$FF */
    int h = vdp_line_cycle * ((vdp_reg[12] & 1) ? 0xD2 : 0xAA) / CYCLES_PER_LINE;
    if ((vdp_reg[12] & 1) && h > 0xB6) h += 0xE4 - 0xB7;
    else if (!(vdp_reg[12] & 1) && h > 0x93) h += 0xE9 - 0x94;
    return (uint16_t)((v & 0xFF) << 8 | (h & 0xFF));
}

/* ---- rendering ---- */
static uint32_t rgb(uint16_t c)
{
    unsigned r = (c >> 1) & 7, g = (c >> 5) & 7, b = (c >> 9) & 7;
    return (r * 255 / 7) << 16 | (g * 255 / 7) << 8 | (b * 255 / 7);
}

/* a layer pixel: bits 0-5 colour (palette * 16 + index), bit 6 priority; 0 index = transparent */
static uint8_t lay_a[320], lay_b[320], lay_s[320];

static int plane_dim(int bits) { return bits == 0 ? 32 : bits == 1 ? 64 : bits == 3 ? 128 : 32; }

static void tile_pixel_row(uint16_t name, int ty, uint8_t out[8])
{
    unsigned tile = name & 0x7FF, pal = (name >> 13) & 3;
    int vf = name & 0x1000, hf = name & 0x800;
    int row = vf ? 7 - ty : ty;
    const uint8_t *p = &vram[(tile * 32 + row * 4) & 0xFFFF];
    for (int x = 0; x < 8; x++) {
        int sx = hf ? 7 - x : x;
        unsigned c = (p[sx >> 1] >> ((sx & 1) ? 0 : 4)) & 15;
        out[x] = c ? (uint8_t)(pal << 4 | c | ((name & 0x8000) ? 0x40 : 0)) : 0;
    }
}

static void draw_plane(uint8_t *out, unsigned base, int line, int width, int is_a, int win_from, int win_to)
{
    int pw = plane_dim(vdp_reg[16] & 3), ph = plane_dim((vdp_reg[16] >> 4) & 3);
    unsigned hs_base = (vdp_reg[13] & 0x3F) << 10;
    int hmode = vdp_reg[11] & 3, vmode = (vdp_reg[11] >> 2) & 1;
    int hrow = hmode == 0 ? 0 : hmode == 2 ? (line & ~7) : line;
    unsigned ha = (hs_base + hrow * 4 + (is_a ? 0 : 2)) & 0xFFFF;
    int hscroll = (vram[ha] << 8 | vram[ha + 1]) & 0x3FF;
    for (int x = 0; x < width; x++) {
        if (is_a && x >= win_from && x < win_to) continue;
        int col = vmode ? (x >> 4) : 0;
        int vs = vsram[(col * 2 + (is_a ? 0 : 1)) % 40] & 0x3FF;
        int px = (x - hscroll) & (pw * 8 - 1);
        int py = (line + vs) & (ph * 8 - 1);
        unsigned na = (base + ((py >> 3) * pw + (px >> 3)) * 2) & 0xFFFF;
        uint16_t name = vram[na] << 8 | vram[na + 1];
        uint8_t row[8];
        tile_pixel_row(name, py & 7, row);
        out[x] = row[px & 7];
    }
}

static void draw_window(uint8_t *out, int line, int width, int *from, int *to)
{
    int h40 = vdp_reg[12] & 1;
    unsigned base = (vdp_reg[3] & (h40 ? 0x3C : 0x3E)) << 10;
    int hp = (vdp_reg[17] & 0x1F) * 16, right = vdp_reg[17] & 0x80;
    int vp = (vdp_reg[18] & 0x1F) * 8, down = vdp_reg[18] & 0x80;
    int in_v = down ? line >= vp : line < vp;
    if (in_v) { *from = 0; *to = width; }
    else if (right) { *from = hp; *to = width; }
    else { *from = 0; *to = hp; }
    if (*from >= *to) { *from = *to = 0; return; }
    int ww = h40 ? 64 : 32;
    for (int x = *from; x < *to; x++) {
        unsigned na = (base + ((line >> 3) * ww + (x >> 3)) * 2) & 0xFFFF;
        uint16_t name = vram[na] << 8 | vram[na + 1];
        uint8_t row[8];
        tile_pixel_row(name, line & 7, row);
        out[x] = row[x & 7];
    }
}

static void draw_sprites(uint8_t *out, int line, int width)
{
    int h40 = vdp_reg[12] & 1;
    unsigned sat = (vdp_reg[5] & (h40 ? 0x7E : 0x7F)) << 9;
    int max = h40 ? 80 : 64, link = 0;
    memset(out, 0, 320);
    for (int n = 0; n < max; n++) {
        const uint8_t *s = &vram[(sat + link * 8) & 0xFFFF];
        int y = ((s[0] << 8 | s[1]) & 0x3FF) - 128;
        int hs = ((s[2] >> 2) & 3) + 1, vs = (s[2] & 3) + 1;
        uint16_t attr = s[4] << 8 | s[5];
        int x = ((s[6] << 8 | s[7]) & 0x1FF) - 128;
        if (line >= y && line < y + vs * 8) {
            int ty = line - y;
            int vf = attr & 0x1000, hf = attr & 0x800;
            int row = vf ? vs * 8 - 1 - ty : ty;
            for (int c = 0; c < hs; c++) {
                int col = hf ? hs - 1 - c : c;
                uint16_t name = (attr & 0xF800) | ((attr + col * vs + (row >> 3)) & 0x7FF);
                uint8_t px[8];
                tile_pixel_row((uint16_t)(name & ~0x1000), row & 7, px);   /* row already flipped */
                for (int i = 0; i < 8; i++) {
                    int sx = x + c * 8 + i;
                    if (sx >= 0 && sx < width && px[i] && !out[sx]) out[sx] = px[i];
                }
            }
        }
        link = s[3] & 0x7F;
        if (!link) break;
    }
}

void vdp_render_line(int line)
{
    int width = (vdp_reg[12] & 1) ? 320 : 256;
    frame_width = width;
    uint32_t bg = rgb(cram[vdp_reg[7] & 0x3F]);
    if (!(vdp_reg[1] & 0x40)) {                       /* display off */
        for (int x = 0; x < 320; x++) frame_rgb[line][x] = 0;
        return;
    }
    int wf = 0, wt = 0;
    memset(lay_a, 0, sizeof lay_a); memset(lay_b, 0, sizeof lay_b);
    draw_window(lay_a, line, width, &wf, &wt);
    draw_plane(lay_a, (vdp_reg[2] & 0x38) << 10, line, width, 1, wf, wt);
    draw_plane(lay_b, (vdp_reg[4] & 0x07) << 13, line, width, 0, 0, 0);
    draw_sprites(lay_s, line, width);
    for (int x = 0; x < width; x++) {
        uint8_t a = lay_a[x], b = lay_b[x], s = lay_s[x], c = 0;
        /* priority order, front to back: S hi, A hi, B hi, S lo, A lo, B lo */
        if (s & 0x40) c = s; else if (a & 0x40) c = a; else if (b & 0x40) c = b;
        else if (s) c = s; else if (a) c = a; else if (b) c = b;
        frame_rgb[line][x] = c ? rgb(cram[c & 0x3F]) : bg;
    }
    for (int x = width; x < 320; x++) frame_rgb[line][x] = 0;
}
