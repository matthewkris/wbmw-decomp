/* VDP: registers, VRAM/CRAM/VSRAM, ports, DMA, status and HV counter, and a scanline renderer
 * for planes A and B, the window and sprites with priorities, sprite masking and the per-line
 * sprite limits. Not modelled: shadow/highlight, interlace, FIFO timing, DMA timing (DMA
 * completes at once). */
#include <string.h>
#include "md.h"

extern unsigned int m68k_read_memory_16(unsigned int a);

uint8_t vdp_reg[32];
uint8_t vram[0x10000];
uint16_t cram[64], vsram[40];
int vdp_line, vdp_line_cycle;
uint32_t frame_rgb[240][FRAME_STRIDE];
int frame_width = 320;
int vdp_pad, vdp_pad_fill, vdp_band_top = 64, vdp_hud_wide, vdp_hide_msg;

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

/* a layer pixel: bits 0-5 colour (palette * 16 + index), bit 6 priority; 0 index = transparent.
 * The layers are indexed by picture x; the game's own x is that less the pad (widescreen). */
static uint8_t lay_a[FRAME_STRIDE], lay_b[FRAME_STRIDE], lay_s[FRAME_STRIDE];

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

/* a plane over picture x [lo, hi), the game's x being x - pad; plane A not where the window is
 * (picture x [win_from, win_to)). Columns of vertical scroll beyond the game's width use its
 * nearest. */
static void draw_plane(uint8_t *out, unsigned base, int line, int lo, int hi, int pad, int width, int is_a, int win_from, int win_to)
{
    int pw = plane_dim(vdp_reg[16] & 3), ph = plane_dim((vdp_reg[16] >> 4) & 3);
    unsigned hs_base = (vdp_reg[13] & 0x3F) << 10;
    int hmode = vdp_reg[11] & 3, vmode = (vdp_reg[11] >> 2) & 1;
    int hrow = hmode == 0 ? 0 : hmode == 2 ? (line & ~7) : line;
    unsigned ha = (hs_base + hrow * 4 + (is_a ? 0 : 2)) & 0xFFFF;
    int hscroll = (vram[ha] << 8 | vram[ha + 1]) & 0x3FF;
    for (int xp = lo; xp < hi; xp++) {
        if (is_a && xp >= win_from && xp < win_to) continue;
        int x = xp - pad;
        int col = vmode ? (x < 0 ? 0 : x >= width ? (width >> 4) - 1 : x >> 4) : 0;
        int vs = vsram[(col * 2 + (is_a ? 0 : 1)) % 40] & 0x3FF;
        int px = (x - hscroll) & (pw * 8 - 1);
        int py = (line + vs) & (ph * 8 - 1);
        unsigned na = (base + ((py >> 3) * pw + (px >> 3)) * 2) & 0xFFFF;
        uint16_t name = vram[na] << 8 | vram[na + 1];
        uint8_t row[8];
        tile_pixel_row(name, py & 7, row);
        out[xp] = row[px & 7];
    }
}

/* the window on this line, where it is in the game's width; *from, *to: the picture x where plane
 * A is not drawn. On the HUD's own lines (the top 64) above vdp_band_top a window that reaches an
 * edge of the game's picture reaches the same edge of a wide one too (those sides are black:
 * vdp_render_line). From vdp_band_top down the map is drawn beside the HUD, and lines the window
 * takes beyond the 64 for a while (a message under the HUD, "Found 5 GOLD.") are the play
 * field's: their sides show plane A as usual. */
#define HUD_LINES 64
/* The HUD laid out for the wide picture (vdp_hud_wide, the native build's widescreen): its top row
 * only, 32 lines, the gold's box (the bottom row's right, x 160-255) moved beside the items' (the
 * top row's right), the three centred: hearts, items, gold, 352 pixels. The bottom row's left, the
 * last enemy's health, is left out (the host shows each enemy's health as it is hit). Its lines
 * (32-63) are the play field's where the map is drawn there (vdp_band_top), black elsewhere. Only
 * while the window is the HUD: from the top, the full width, at least its 64 lines. */
#define HUD_TOP 32
#define HUD_W 352
static int hud_wide(int width, int pad)
{
    return vdp_hud_wide && pad && width == 256 && vdp_reg[17] == 0 && !(vdp_reg[18] & 0x80) && (vdp_reg[18] & 0x1F) >= 8;
}
static int hud_x0(int pad) { return pad + 128 - HUD_W / 2; }
/* The gold's box is drawn from a copy of its tiles, kept while the window is the HUD alone (8
 * rows), as the status screen (Start) grows the window down and draws over the HUD's bottom row.
 * While that row's top edge differs from the copy's (something other than the HUD drew there)
 * the bottom row's lines show the window as it is, in the game's width, so that the screen drawn
 * there is whole; a message under the HUD ("Found 5 GOLD.") leaves the row as it is. */
static uint16_t hud_gold[4][12], hud_edge[32];
static int hud_kept;
static uint16_t win_cell(int row, int col)
{
    unsigned na = (((vdp_reg[3] & 0x3E) << 10) + (row * 32 + col) * 2) & 0xFFFF;
    return (uint16_t)(vram[na] << 8 | vram[na + 1]);
}
static void hud_keep(void)                      /* each frame the window is 8 rows */
{
    static int odd;                             /* frames its edge has differed (a new HUD, not the status screen) */
    int same = hud_kept;
    for (int c = 0; c < 32 && same; c++) if (win_cell(4, c) != hud_edge[c]) same = 0;
    if (!same && hud_kept && ++odd < 30) return;   /* the status screen draws over it a frame before it grows */
    odd = 0;
    for (int r = 0; r < 4; r++) for (int c = 0; c < 12; c++) hud_gold[r][c] = win_cell(4 + r, 20 + c);
    for (int c = 0; c < 32; c++) hud_edge[c] = win_cell(4, c);
    hud_kept = 1;
}
static int hud_covered(void)                    /* the bottom row drawn over (the status screen) */
{
    int rows = vdp_reg[18] & 0x1F;
    if (rows <= 8) return 0;
    if (!hud_kept) return rows >= 12;           /* no copy yet (a state loaded with it open): by its size */
    for (int c = 0; c < 32; c++) if (win_cell(4, c) != hud_edge[c]) return 1;
    return 0;
}
/* The status screen hung from the HUD's bottom row, which was its top. It is drawn 8 lines lower,
 * with its own bottom row (the screen's last, 15, drawn before it slides open) turned over above
 * it: from line 32, the line under the HUD's row. */
#define MENU_LAST 15
static int menu_line(uint8_t *out, int line, int width, int pad, int *from, int *to)
{
    int rows = vdp_reg[18] & 0x1F;
    if (line >= rows * 8 + 8) return 0;         /* below it: the play field */
    int sl = line - 8, flip = line < HUD_TOP + 8;
    for (int x = 0; x < width; x++) {
        uint8_t row[8];
        uint16_t name = flip ? (uint16_t)(win_cell(MENU_LAST, x >> 3) ^ 0x1000) : win_cell(sl >> 3, x >> 3);
        tile_pixel_row(name, flip ? line & 7 : sl & 7, row);
        out[x + pad] = row[x & 7];
    }
    *from = pad; *to = pad + width;
    return 1;
}
static void draw_window(uint8_t *out, int line, int width, int lo, int hi, int pad, int *from, int *to)
{
    if (hud_wide(width, pad) && vdp_hide_msg && line >= HUD_LINES) { *from = *to = 0; return; }   /* the message hidden */
    if (hud_wide(width, pad) && line >= HUD_TOP && hud_covered()) {   /* the status screen, a row lower */
        if (!menu_line(out, line, width, pad, from, to)) *from = *to = 0;
        return;
    }
    if (hud_wide(width, pad) && line < HUD_LINES) {
        *from = *to = 0;
        if (line == 0 && (vdp_reg[18] & 0x1F) == 8) hud_keep();
        if (line >= HUD_TOP) {                      /* (the status screen: menu_line, above) */
            /* a message under the HUD ("Found a Potion.") hung from the bottom row, its edge its
             * top: it gets its own bottom row turned over, on the line above it */
            int rows = vdp_reg[18] & 0x1F;
            if (line < HUD_LINES - 8 || rows <= 8 || vdp_hide_msg) return;
            for (int x = 0; x < width; x++) {
                uint8_t row[8];
                tile_pixel_row((uint16_t)(win_cell(rows - 1, x >> 3) ^ 0x1000), line & 7, row);
                out[x + pad] = row[x & 7];
            }
            *from = pad; *to = pad + width;
            return;
        }
        int x0 = hud_x0(pad);
        for (int X = 0; X < HUD_W; X++) {
            /* past the items: the gold's box, in the items' box's frame (its own had no top: the
             * items' box's bottom was its top, and its sides are not the items' box's corners),
             * the inside (columns 22-29) its own rows after the first (an empty one under that
             * shared edge), so its words are level with the items' */
            int sx = X < 256 ? X : X - 96, r = line >> 3, col = sx >> 3;
            int inside = X >= 256 && (r == 1 || r == 2) && col >= 22 && col <= 29;
            uint16_t name = !inside ? win_cell(r, col)
                          : hud_kept ? hud_gold[r][col - 20]
                          : hud_covered() ? win_cell(r, 21) : win_cell(4 + r, col);   /* no copy, covered: left empty */
            uint8_t row[8];
            tile_pixel_row(name, line & 7, row);
            out[x0 + X] = row[sx & 7];
        }
        *from = x0; *to = x0 + HUD_W;
        return;
    }
    int h40 = vdp_reg[12] & 1;
    unsigned base = (vdp_reg[3] & (h40 ? 0x3C : 0x3E)) << 10;
    int hp = (vdp_reg[17] & 0x1F) * 16, right = vdp_reg[17] & 0x80;
    int vp = (vdp_reg[18] & 0x1F) * 8, down = vdp_reg[18] & 0x80;
    int in_v = down ? line >= vp : line < vp;
    int f, t;
    if (in_v) { f = 0; t = width; }
    else if (right) { f = hp; t = width; }
    else { f = 0; t = hp; }
    if (f >= t) { *from = *to = 0; return; }
    int ww = h40 ? 64 : 32;
    for (int x = f; x < t; x++) {
        unsigned na = (base + ((line >> 3) * ww + (x >> 3)) * 2) & 0xFFFF;
        uint16_t name = vram[na] << 8 | vram[na + 1];
        uint8_t row[8];
        tile_pixel_row(name, line & 7, row);
        out[x + pad] = row[x & 7];
    }
    int sides = line < HUD_LINES && line < vdp_band_top;
    *from = f == 0 && sides ? lo : f + pad;
    *to = t == width && sides ? hi : t + pad;
}

/* Sprites on one line, in link order, with the hardware's limits: at most 16 sprites and 256
 * sprite pixels per line in H32 (20 and 320 in H40), and masking: a sprite at x = 0 (raw) hides
 * every later sprite on its lines, once a sprite not at x = 0 has been found on the line. The
 * game masks the bottom of the HUD this way, so hanging spider webs pass behind it. A wide
 * picture (lo..hi past the game's width) has limits in proportion. */
static void draw_sprites(uint8_t *out, int line, int lo, int hi, int pad, int width)
{
    int h40 = vdp_reg[12] & 1;
    unsigned sat = (vdp_reg[5] & (h40 ? 0x7E : 0x7F)) << 9;
    int max = h40 ? 80 : 64, link = 0;
    int per_line = (h40 ? 20 : 16) * (hi - lo) / width, pixels = hi - lo, seen = 0, nonzero = 0, masked = 0;
    /* Widescreen: masking hides sprites behind the HUD, which in a wide picture covers only the
     * game's width. On the HUD's lines every sprite after the mask (the first sprite at x = 0
     * after one that is not, wherever it is on the screen) is drawn at the sides only, without
     * the limits, on the mask's own lines and above them: the game builds them there too in
     * widescreen (port/sprites.c keeps its top limit; it would leave out what is behind the
     * HUD), and Wonder Boy jumping up beside the HUD lost his head and body. Only where the map
     * is drawn beside the HUD (from vdp_band_top): in the black above it they stay hidden, as
     * behind the HUD (the spears of area 7 rising back into the ceiling showed there). */
    int side_ok = line < HUD_LINES && (lo < pad || hi > pad + width), mask_at = -1;
    int sides_shown = line >= vdp_band_top;         /* the map beside the HUD on this line, not the black */
    int hw = hud_wide(width, pad), hx0 = hud_x0(pad);   /* the wide HUD: it covers only its row's boxes */
    if (side_ok) {
        int l = 0, nz = 0;
        for (int n = 0; n < max; n++) {
            const uint8_t *s = &vram[(sat + l * 8) & 0xFFFF];
            int rx = (s[6] << 8 | s[7]) & 0x1FF;
            if (rx == 0 && nz) { mask_at = n; break; }
            if (rx) nz = 1;
            l = s[3] & 0x7F;
            if (!l) break;
        }
    }
    memset(out, 0, FRAME_STRIDE);
    for (int n = 0; n < max; n++) {
        if (mask_at >= 0 && n >= mask_at) masked = 1;
        const uint8_t *s = &vram[(sat + link * 8) & 0xFFFF];
        int y = ((s[0] << 8 | s[1]) & 0x3FF) - 128;
        int hs = ((s[2] >> 2) & 3) + 1, vs = (s[2] & 3) + 1;
        uint16_t attr = s[4] << 8 | s[5];
        int rawx = (s[6] << 8 | s[7]) & 0x1FF, x = rawx - 128 + pad;
        if (line >= y && line < y + vs * 8) {
            if (seen++ == per_line && !masked) break;
            if (rawx == 0 && nonzero && !side_ok) break;   /* masking */
            if (rawx) nonzero = 1;
            int draw = masked || hs * 8 < pixels ? hs * 8 : pixels;    /* the pixel budget can run out inside a sprite */
            if (!masked) pixels -= draw;
            int ty = line - y;
            int vf = attr & 0x1000, hf = attr & 0x800;
            int row = vf ? vs * 8 - 1 - ty : ty;
            for (int c = 0; c < hs; c++) {
                int col = hf ? hs - 1 - c : c;
                uint16_t name = (attr & 0xF800) | ((attr + col * vs + (row >> 3)) & 0x7FF);
                uint8_t px[8];
                tile_pixel_row((uint16_t)(name & ~0x1000), row & 7, px);   /* row already flipped */
                for (int i = 0; i < 8 && c * 8 + i < draw; i++) {
                    int sx = x + c * 8 + i;
                    int covered = hw ? line < HUD_TOP && sx >= hx0 && sx < hx0 + HUD_W : sx >= pad && sx < pad + width;
                    if (masked && (!sides_shown || covered)) continue;
                    if (sx >= lo && sx < hi && px[i] && !out[sx]) out[sx] = px[i];
                }
            }
            if (!pixels && !masked) break;
        }
        link = s[3] & 0x7F;
        if (!link) break;
    }
}

void vdp_render_line(int line)
{
    int width = (vdp_reg[12] & 1) ? 320 : 256;      /* the game's own */
    int pad = vdp_pad ? (256 + 2 * vdp_pad - width) / 2 : 0;
    int fill = vdp_pad_fill && width == 256;
    int lo = fill ? 0 : pad, hi = fill ? width + 2 * pad : pad + width;   /* the picture x drawn */
    int bar = fill && line < HUD_LINES && line < vdp_band_top;   /* the band's sides black (no map above it), the sprites still drawn */
    int total = width + 2 * pad;
    frame_width = total;
    uint32_t bg = rgb(cram[vdp_reg[7] & 0x3F]);
    if (!(vdp_reg[1] & 0x40)) {                       /* display off */
        for (int x = 0; x < FRAME_STRIDE; x++) frame_rgb[line][x] = 0;
        return;
    }
    int wf = 0, wt = 0, hw = hud_wide(width, pad) && line < HUD_LINES, hx0 = hud_x0(pad);
    memset(lay_a, 0, sizeof lay_a); memset(lay_b, 0, sizeof lay_b);
    draw_window(lay_a, line, width, lo, hi, pad, &wf, &wt);
    draw_plane(lay_a, (vdp_reg[2] & 0x38) << 10, line, lo, hi, pad, width, 1, wf, wt);
    draw_plane(lay_b, (vdp_reg[4] & 0x07) << 13, line, lo, hi, pad, width, 0, 0, 0);
    draw_sprites(lay_s, line, lo, hi, pad, width);
    for (int x = 0; x < total; x++) {
        int boxes = line < HUD_TOP ? x >= hx0 && x < hx0 + HUD_W : wt > wf && x >= wf && x < wt;
        if (hw && !boxes) {                         /* the wide HUD's lines, outside its boxes (or the screen over them) */
            if (!(fill && line >= vdp_band_top)) {  /* no map there: black, the sprites over it */
                frame_rgb[line][x] = lay_s[x] ? rgb(cram[lay_s[x] & 0x3F]) : 0;
                continue;
            }
        } else if (!hw && (x < lo || x >= hi)) { frame_rgb[line][x] = 0; continue; }   /* the sides of a picture not wide */
        if (!hw && bar && (x < pad || x >= pad + width)) {   /* the bar: no scenery, but the sprites over it */
            frame_rgb[line][x] = lay_s[x] ? rgb(cram[lay_s[x] & 0x3F]) : 0;
            continue;
        }
        uint8_t a = lay_a[x], b = lay_b[x], s = lay_s[x], c = 0;
        /* priority order, front to back: S hi, A hi, B hi, S lo, A lo, B lo */
        if (s & 0x40) c = s; else if (a & 0x40) c = a; else if (b & 0x40) c = b;
        else if (s) c = s; else if (a) c = a; else if (b) c = b;
        frame_rgb[line][x] = c ? rgb(cram[c & 0x3F]) : bg;
    }
    for (int x = total; x < FRAME_STRIDE; x++) frame_rgb[line][x] = 0;
}
