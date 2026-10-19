/* The kernel's helpers ($01000-$03000) not yet C elsewhere: the map's cell lists and rectangles,
 * the animated cells' scripts, the area triggers. Batch T63 on.
 *
 * Plain C (as from batch T42), exact in the registers and flags their callers use. */
#include "world.h"
#include "task.h"

static Cpu *c;
static void call(uint32_t addr) { call_68k(c, addr); }

static void f_move(uint32_t v, uint32_t sign) { c->ccr = (uint16_t)((c->ccr & CC_X) | (v & sign ? CC_N : 0) | ((v & (sign * 2 - 1)) ? 0 : CC_Z)); }
#define F_B(v) f_move((v), 0x80u)
#define F_W(v) f_move((v), 0x8000u)
#define F_L(v) f_move((v), 0x80000000u)
static uint32_t sx(uint32_t w) { return (uint32_t)(int32_t)(int16_t)w; }

#define PLANE_A      0xFFFF9754u    /* the cells' plane prefixes ($2670 draws through a3) */
#define PLANE_B      0xFFFF97D4u
#define SET_SHOWN    0x2670u        /* cell d0 at (d6, d7): set, and drawn if on screen */

/* ---- batch T63: cell lists and rectangles, animated cells, area triggers ---- */

/* $27FA: the cells listed at a0 (cell, x, y words) to a negative word, on plane A */
static void cell_list(void)
{
    c->a[3] = PLANE_A;
    for (;;) {
        c->d[0] = set_w(c->d[0], rd16(c->a[0])); c->a[0] += 2;
        if (c->d[0] & 0x8000) { F_W(c->d[0]); return; }
        c->d[6] = set_w(c->d[6], rd16(c->a[0])); c->a[0] += 2;
        c->d[7] = set_w(c->d[7], rd16(c->a[0])); c->a[0] += 2;
        c->a[7] -= 4; wr32(c->a[7], c->a[0]);
        call(SET_SHOWN);
        c->a[0] = rd32(c->a[7]); c->a[7] += 4;
    }
}
/* $2812: d1 rows of d0 cells d2 from (d6, d7), on plane A */
static void cell_fill(void)
{
    uint16_t cols = (uint16_t)c->d[0], rows = (uint16_t)c->d[1], cell = (uint16_t)c->d[2], x = (uint16_t)c->d[6];
    c->a[3] = PLANE_A;
    do {
        c->d[6] = set_w(c->d[6], x);
        uint16_t n = cols;
        do {
            c->d[0] = set_w(c->d[0], cell);
            call(SET_SHOWN);
            c->d[6] = set_w(c->d[6], c->d[6] + 1);
        } while (--n);
        c->d[7] = set_w(c->d[7], c->d[7] + 0x40);
    } while (--rows);
}
/* $2856: a block at a0 (its width and height bytes, then its cells) from (d6, d7), on plane A;
 * $2852 with the place first (x, y words) */
static void cell_block(void)
{
    c->a[3] = PLANE_A;
    c->d[0] = rd8(c->a[0]); c->a[0]++;
    uint16_t cols = (uint16_t)c->d[0];
    c->d[0] = set_b(c->d[0], rd8(c->a[0])); c->a[0]++;
    uint16_t rows = (uint16_t)c->d[0], x = (uint16_t)c->d[6];
    do {
        c->d[6] = set_w(c->d[6], x);
        uint16_t n = cols;
        do {
            c->d[0] = set_w(c->d[0], rd16(c->a[0])); c->a[0] += 2;
            c->a[7] -= 4; wr32(c->a[7], c->a[0]);
            call(SET_SHOWN);
            c->a[0] = rd32(c->a[7]); c->a[7] += 4;
            c->d[6] = set_w(c->d[6], c->d[6] + 1);
        } while (--n);
        c->d[7] = set_w(c->d[7], c->d[7] + 0x40);
    } while (--rows);
}
static void cell_block_at(void)
{
    c->d[6] = set_w(c->d[6], rd16(c->a[0])); c->a[0] += 2;
    c->d[7] = set_w(c->d[7], rd16(c->a[0])); c->a[0] += 2;
    cell_block();
}

/* The animated cells: 8 slots at $9C34 (4 bytes apart): flags (bit 7 on, bit 1 plane B, bit 0
 * set by $FD), timer; at $9C36 a repeat count; their place ($9CA8, $9CAA), script ($9D1C) and
 * loop point ($9D90). A script: a byte n < $FA, a frame shown after n frames: its cells (cell
 * word, dx, dy bytes) to a negative word; $FA n a loop of n, $FB its end, $FC the other plane,
 * $FD bit 0, $FE a jump, $FF stopped here, 0 the end. */
/* $289A: all cleared */
static void cell_anims_clear(void)
{
    c->d[0] = 0;
    c->a[0] = 0xFFFF9C34u;
    for (int k = 0; k < 8; k++) { wr8(c->a[0], 0); c->a[0] += 4; }
    c->d[1] = 0xFFFF;
}
/* $28AC: each one running, when its timer runs out: its script to its next frame */
static void cell_anims_run(void)
{
#define s (c->a[4])
    for (c->a[4] = 0; c->a[4] < 0x20; c->a[4] += 4) {
        if (!(rd8(s - 0x63CC) & 0x80)) continue;
        if (!rd8(s - 0x63CB)) continue;
        wr8(s - 0x63CB, (uint8_t)(rd8(s - 0x63CB) - 1));
        if (rd8(s - 0x63CB)) continue;
        c->a[0] = rd32(s - 0x62E4);
        for (;;) {
            c->d[0] = set_b(c->d[0], rd8(c->a[0])); c->a[0]++;
            uint8_t k = (uint8_t)c->d[0];
            if (!k) { wr8(s - 0x63CC, 0); goto next; }
            if (k >= 0xFD) {
                if (k == 0xFF) break;
                if (k == 0xFD) { wr8(s - 0x63CC, rd8(s - 0x63CC) | 1); continue; }
                c->d[0] = set_b(c->d[0], rd8(c->a[0])); c->a[0]++;          /* $FE: a jump */
                c->d[0] = set_w(c->d[0], c->d[0] << 8);
                c->d[0] = set_b(c->d[0], rd8(c->a[0]));
                c->d[0] = set_w(c->d[0], c->d[0] - 1);
                c->a[0] += sx(c->d[0]);
                continue;
            }
            if (k >= 0xFA) {
                if (k == 0xFA) { wr8(s - 0x63CA, rd8(c->a[0])); c->a[0]++; wr32(s - 0x6270, c->a[0]); continue; }
                if (k == 0xFC) { wr8(s - 0x63CC, rd8(s - 0x63CC) ^ 2); continue; }
                wr8(s - 0x63CA, (uint8_t)(rd8(s - 0x63CA) - 1));             /* $FB */
                if (rd8(s - 0x63CA)) c->a[0] = rd32(s - 0x6270);
                continue;
            }
            wr8(s - 0x63CB, k);                                           /* a frame */
            c->a[3] = (rd8(s - 0x63CC) & 2) ? PLANE_B : PLANE_A;
            for (;;) {
                c->d[0] = set_b(c->d[0], rd8(c->a[0])); c->a[0]++;
                c->d[0] = set_w(c->d[0], c->d[0] << 8);
                c->d[0] = set_b(c->d[0], rd8(c->a[0])); c->a[0]++;
                if (c->d[0] & 0x8000) break;
                c->d[6] = set_w(c->d[6], rd16(s - 0x6358));
                c->d[1] = set_b(c->d[1], rd8(c->a[0])); c->a[0]++;
                c->d[1] = set_w(c->d[1], (uint16_t)(int8_t)c->d[1]);
                c->d[6] = set_w(c->d[6], c->d[6] + c->d[1]);
                c->d[7] = set_w(c->d[7], rd16(s - 0x6356));
                c->d[1] = set_b(c->d[1], rd8(c->a[0])); c->a[0]++;
                c->d[1] = set_w(c->d[1], (uint16_t)(int8_t)c->d[1]);
                c->d[1] = set_w(c->d[1], c->d[1] << 6);
                c->d[7] = set_w(c->d[7], c->d[7] + c->d[1]);
                c->a[7] -= 4; wr32(c->a[7], c->a[0]);
                call(SET_SHOWN);
                c->a[0] = rd32(c->a[7]); c->a[7] += 4;
            }
            break;
        }
        wr32(s - 0x62E4, c->a[0]);
    next:;
    }
}
#undef s

/* $2974: a free slot started with script a0 at (d6, d7): eq (d0 0), or none (mi) */
static void cell_anim_start(void)
{
    c->a[1] = 0;
    c->d[0] = 7;
    for (;;) {
        uint8_t v = rd8(c->a[1] - 0x63CC);
        F_B(v);
        if (!(v & 0x80)) break;
        c->a[1] += 4;
        c->d[0] = set_w(c->d[0], c->d[0] - 1);
        if ((c->d[0] & 0xFFFF) == 0xFFFF) return;
    }
    wr8(c->a[1] - 0x63CC, 0x80);
    wr8(c->a[1] - 0x63CB, 1);
    wr16(c->a[1] - 0x6358, (uint16_t)c->d[6]);
    wr16(c->a[1] - 0x6356, (uint16_t)c->d[7]);
    wr32(c->a[1] - 0x62E4, c->a[0]);
    c->d[0] = 0; F_L(0);
}

/* The area triggers: 4 at $9E04 (4 bytes apart): flags (bit 7 on, bit 2 the player inside,
 * bit 1 just entered, bit 0 the flag's kind), the game flag ($9E05), the item to give ($9E06);
 * their rectangles ($9E38, $9E3A, $9E6C, $9E6E) and the next items' list ($9EA0) */
/* $29A4: all cleared */
static void triggers_clear(void)
{
    c->a[0] = 0xFFFF9E04u;
    c->d[0] = 3;
    for (int k = 0; k < 4; k++) { wr8(c->a[0], 0); c->a[0] += 4; }
    c->d[0] = 0xFFFF;
}
/* $29B4: each one: the player's entering it gives its item ($1090C, at its middle top) and sets
 * its game flag ($2752 or bset), then the next item of its list ($FF: done) */
static void triggers_run(void)
{
    c->a[4] = sx(rd16(0xFFFF9EEEu));
    for (c->a[3] = 0; c->a[3] < 0x10; c->a[3] += 4) {
        uint32_t t = c->a[3];
        if (!(rd8(t - 0x61FC) & 0x80)) continue;
        int in = 0;
        uint16_t px = rd16(c->a[4] - 0x3800), py = rd16(c->a[4] - 0x3700);
        c->d[0] = set_w(c->d[0], px - rd16(t - 0x61C8));
        if (px >= rd16(t - 0x61C8) && (uint16_t)c->d[0] < rd16(t - 0x6194)) {
            c->d[0] = set_w(c->d[0], py - rd16(t - 0x61C6));
            if (py >= rd16(t - 0x61C6) && (uint16_t)c->d[0] < rd16(t - 0x6192)) in = 1;
        }
        if (in) {
            uint8_t f = rd8(t - 0x61FC);
            wr8(t - 0x61FC, f | 4);
            if (!(f & 4)) wr8(t - 0x61FC, rd8(t - 0x61FC) | 2);
        } else wr8(t - 0x61FC, rd8(t - 0x61FC) & 0xFB);
        if (!(rd8(t - 0x61FC) & 2)) continue;
        c->d[0] = rd8(t - 0x61FA);
        c->d[1] = set_w(c->d[1], (rd16(t - 0x6194) >> 1) + rd16(t - 0x61C8));
        c->d[2] = set_w(c->d[2], rd16(t - 0x6192) + rd16(t - 0x61C6));
        c->a[7] -= 2; wr16(c->a[7], (uint16_t)c->a[3]);
        call(0x1090C);
        c->a[3] = sx(rd16(c->a[7])); c->a[7] += 2;
        if (c->ccr & CC_N) continue;
        t = c->a[3];
        c->d[0] = set_b(c->d[0], rd8(t - 0x61FB));
        if (rd8(t - 0x61FC) & 1) call(0x2752);
        else {
            c->d[1] = c->d[0] & 0xFF;                       /* $2758 and bset */
            c->d[0] = set_w(c->d[0], c->d[0] & 7);
            c->a[0] = 0xFFFF9992u;
            c->d[1] = set_w(c->d[1], ((uint16_t)c->d[1] >> 3) ^ 1);
            uint32_t a = c->a[0] + sx(c->d[1]);
            wr8(a, rd8(a) | (uint8_t)(1u << (c->d[0] & 7)));
        }
        wr8(t - 0x61FC, rd8(t - 0x61FC) & 0xFD);
        c->a[0] = rd32(t - 0x6160);
        c->d[0] = set_b(c->d[0], rd8(c->a[0])); c->a[0]++;
        if ((c->d[0] & 0xFF) == 0xFF) { wr8(t - 0x61FC, 0); continue; }
        wr8(t - 0x61FA, (uint8_t)c->d[0]);
        wr32(t - 0x6160, c->a[0]);
    }
}
/* $2A6C: a free trigger: flag d3 (low byte; bit 8 its kind), rectangle (d4, d5, d6, d7), items
 * at a0: eq (d0 0), or none (mi) */
static void trigger_add(void)
{
    c->a[1] = 0;
    c->d[0] = 3;
    for (;;) {
        uint8_t v = rd8(c->a[1] - 0x61FC);
        F_B(v);
        if (!(v & 0x80)) break;
        c->a[1] += 4;
        c->d[0] = set_w(c->d[0], c->d[0] - 1);
        if ((c->d[0] & 0xFFFF) == 0xFFFF) return;
    }
    wr8(c->a[1] - 0x61FB, (uint8_t)c->d[3]);
    c->d[3] = set_w(c->d[3], (uint16_t)c->d[3] >> 8);
    c->d[3] = set_b(c->d[3], (c->d[3] & 1) | 0x80);
    wr8(c->a[1] - 0x61FC, (uint8_t)c->d[3]);
    wr16(c->a[1] - 0x61C8, (uint16_t)c->d[4]);
    wr16(c->a[1] - 0x61C6, (uint16_t)c->d[5]);
    wr16(c->a[1] - 0x6194, (uint16_t)c->d[6]);
    wr16(c->a[1] - 0x6192, (uint16_t)c->d[7]);
    wr8(c->a[1] - 0x61FA, rd8(c->a[0])); c->a[0]++;
    wr32(c->a[1] - 0x6160, c->a[0]);
    c->d[0] = 0; F_L(0);
}

#define ENTRY(name, fn) void name(Cpu *cpu) { c = cpu; fn(); c->ret = 0; }
ENTRY(CellList, cell_list)                 /* $27FA */
ENTRY(CellFill, cell_fill)                 /* $2812 */
ENTRY(CellBlockAt, cell_block_at)          /* $2852 */
ENTRY(CellBlock, cell_block)               /* $2856 */
ENTRY(CellAnimsClear, cell_anims_clear)    /* $289A */
ENTRY(CellAnimsRun, cell_anims_run)        /* $28AC */
ENTRY(CellAnimStart, cell_anim_start)      /* $2974 */
ENTRY(TriggersClear, triggers_clear)       /* $29A4 */
ENTRY(TriggersRun, triggers_run)           /* $29B4 */
ENTRY(TriggerAdd, trigger_add)             /* $2A6C */
