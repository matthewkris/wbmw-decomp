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

/* ---- batch T64: the vector maths ($1220-$135C) ---- */
static uint32_t swap32(uint32_t v) { return v << 16 | v >> 16; }
static void exg(uint32_t *a, uint32_t *b) { uint32_t t = *a; *a = *b; *b = t; }
/* divu.w s,d: the quotient in the low word and the remainder in the high; on overflow d is kept */
static uint32_t divu_w(uint32_t d, uint16_t s)
{
    if (!s) return d;                               /* a trap on the 68000: not reached */
    uint32_t q = d / s;
    if (q > 0xFFFF) return d;
    return (d % s) << 16 | q;
}
static uint32_t divs_w(uint32_t d, uint16_t s)
{
    if (!s) return d;
    int32_t n = (int32_t)d, m = (int16_t)s, q = n / m;
    if (q < -0x8000 || q > 0x7FFF) return d;
    return (uint32_t)(uint16_t)(n % m) << 16 | (uint16_t)q;
}
/* $127A: (d4, d5) the vector (d0, d1) scaled to length d2 along its longer side (d3 $FF when x is
 * the longer); d2 0 for a zero vector */
static void scale_vector(void)
{
    c->d[4] = set_w(c->d[4], c->d[0]);
    if (c->d[4] & 0x8000) c->d[4] = set_w(c->d[4], -c->d[4]);
    c->d[5] = set_w(c->d[5], c->d[1]);
    if (c->d[5] & 0x8000) c->d[5] = set_w(c->d[5], -c->d[5]);
    int ge = (uint16_t)c->d[4] >= (uint16_t)c->d[5];
    c->d[3] = set_b(c->d[3], ge ? 0xFF : 0);
    if (!ge) exg(&c->d[4], &c->d[5]);
    if (!(c->d[4] & 0xFFFF)) { c->d[2] = set_w(c->d[2], 0); return; }
    c->d[5] = swap32(c->d[5]) & 0xFFFF0000u;
    c->d[5] = divu_w(c->d[5], (uint16_t)c->d[4]);
    c->d[5] = (uint32_t)(uint16_t)c->d[5] * (uint16_t)c->d[2];
    c->d[5] = swap32(c->d[5]);
    exg(&c->d[2], &c->d[4]);
    if (!(c->d[3] & 0xFF)) exg(&c->d[4], &c->d[5]);
    if (c->d[0] & 0x8000) c->d[4] = set_w(c->d[4], -c->d[4]);
    if (c->d[1] & 0x8000) c->d[5] = set_w(c->d[5], -c->d[5]);
}
/* $1310: for a jump d2 high: d5 about its time of flight (a square root, 8 bits), d2 the speed
 * across for distance d0, d3 |d0|; d5 0 when that is over 2 */
static void flight_time(void)
{
    c->d[2] = set_w(c->d[2], c->d[2] + 7);
    c->d[6] = set_w(c->d[6], c->d[2]);
    c->d[2] = set_w(c->d[2], c->d[2] << 5);
    c->d[2] = set_w(c->d[2], c->d[2] + 1);
    c->d[5] = 0; c->d[3] = 0; c->d[4] = 7;
    for (int k = 0; k < 8; k++) {
        c->d[5] = set_w(c->d[5], c->d[5] * 2);
        c->d[2] <<= 2;
        c->d[2] = swap32(c->d[2]);
        c->d[3] = set_w(c->d[3], c->d[3] * 2 + 1);
        if ((uint16_t)c->d[2] >= (uint16_t)c->d[3]) {
            c->d[2] = set_w(c->d[2], c->d[2] - c->d[3]);
            c->d[3] = set_w(c->d[3], c->d[3] + 1);
            c->d[5] = set_w(c->d[5], c->d[5] + 1);
        } else c->d[3] = set_w(c->d[3], c->d[3] - 1);
        c->d[2] = swap32(c->d[2]);
    }
    c->d[4] = 0xFFFF;
    c->d[5] = set_w(c->d[5], c->d[5] - 1);
    c->d[5] = set_w(c->d[5], (uint16_t)c->d[5] >> 1);
    c->d[5] = set_w(c->d[5], c->d[5] + 7);
    c->d[2] = 0;
    c->d[2] = set_w(c->d[2], c->d[0]);
    if (c->d[2] & 0x8000) c->d[2] = set_w(c->d[2], -c->d[2]);
    c->d[3] = set_w(c->d[3], c->d[2]);
    c->d[2] <<= 8;
    c->d[2] = divu_w(c->d[2], (uint16_t)c->d[5]);
    if ((uint16_t)c->d[2] > 0x200) c->d[5] = 0;
}
/* $12B6: a jump to (d0, d1) away: d2 the speed across, d3 up, d6 the fraction; across at most 2
 * (then a flatter arc) */
static void jump_to(void)
{
    if (c->d[1] & 0x8000) {
        c->d[2] = 0;
        c->d[2] = set_w(c->d[2], c->d[1]);
        c->d[2] = set_w(c->d[2], -c->d[2]);
        flight_time();
        if (c->d[5] & 0xFFFF) {
            c->d[3] = set_w(c->d[3], c->d[5]);
            c->d[3] = set_w(c->d[3], c->d[3] - 7);
            c->d[4] = set_w(c->d[4], c->d[3]);
            c->d[4] = set_w(c->d[4], c->d[4] - 1);
            c->d[4] = set_w(c->d[4], c->d[4] << 5);
            c->d[4] = (uint32_t)(uint16_t)c->d[4] * (uint16_t)c->d[3];
            c->d[6] = set_w(c->d[6], c->d[6] << 8);
            c->d[6] = set_w(c->d[6], c->d[6] - c->d[4]);
            c->d[3] = set_w(c->d[3], c->d[3] << 6);
            c->d[3] = set_w(c->d[3], -c->d[3]);
            c->d[6] = set_w(c->d[6], -c->d[6]);
            goto sign;
        }
    } else {
        c->d[2] = 0;
        c->d[2] = set_w(c->d[2], c->d[1]);
        flight_time();
        if (c->d[5] & 0xFFFF) { c->d[3] = set_w(c->d[3], 0xFE40); goto flat; }
    }
    c->d[5] = set_w(c->d[5], c->d[3]);                /* too far across for that height */
    c->d[5] = set_w(c->d[5], (uint16_t)c->d[5] >> 1);
    c->d[3] = set_w(c->d[3], c->d[1]);
    c->d[3] = (uint32_t)(int32_t)(int16_t)c->d[3];
    c->d[3] <<= 8;
    c->d[3] = divs_w(c->d[3], (uint16_t)c->d[5]);
    c->d[4] = set_w(c->d[4], c->d[5]);
    c->d[4] = set_w(c->d[4], c->d[4] + 1);
    c->d[4] = set_w(c->d[4], c->d[4] << 5);
    c->d[3] = set_w(c->d[3], c->d[3] - c->d[4]);
    c->d[2] = set_w(c->d[2], 0x200);
flat:
    c->d[6] = 0;
sign:
    if (c->d[0] & 0x8000) c->d[2] = set_w(c->d[2], -c->d[2]);
}
/* $1220 (not called): the velocity at angle and speed ($1032, $FC0) */
static void velocity_at_angle(void)
{
    call(0x1032);
    c->d[6] = set_w(c->d[6], c->d[3]);
    call(0xFC0);
    wr16(c->a[4] - 0x3600, (uint16_t)c->d[0]);
    wr16(c->a[4] - 0x35FE, (uint16_t)c->d[5]);
}
ENTRY(VelocityAtAngle, velocity_at_angle)  /* $1220 */
ENTRY(ScaleVector, scale_vector)           /* $127A */
ENTRY(JumpTo, jump_to)                     /* $12B6 */
ENTRY(FlightTime, flight_time)             /* $1310 */

/* ---- tile animations, the lift's cells, effects, the item rain ---- */
/* $23A6: a tile animation from a0 (its word, then its script) into a free slot of $950C (8, $10
 * apart), its task ($6EA8) started ($47A); none free stops the game (the original loops at $23C6) */
void TileAnimStart(Cpu *cpu)
{
    c = cpu;
    c->a[7] -= 4; wr32(c->a[7], c->a[0]);
    c->a[1] = 0x6EA8;
    call(0x47A);
    c->a[0] = rd32(c->a[7]); c->a[7] += 4;
    c->a[1] = 0xFFFF950Cu;
    c->d[0] = 7;
    while (rd16(c->a[1]) & 0x8000) {
        c->a[1] += 0x10;
        c->d[0] = set_w(c->d[0], c->d[0] - 1);
        if ((c->d[0] & 0xFFFF) == 0xFFFF) { port_continue_at(c, 0x23C6, 0); return; }
    }
    wr8(0xFFFF950Au, 0);
    wr32(c->a[1], 0x80000000u);
    wr16(c->a[1] + 4, rd16(c->a[0])); c->a[0] += 2;
    wr32(c->a[1] + 8, c->a[0]);
    wr32(c->a[1] + 0xC, c->a[0]);
    c->ret = 0;
}
/* $2C08: the lift's four cells ($2C46, by $9A8B) at its place ($9A8C, $9A8E) */
static void lift_cells(void)
{
    c->d[6] = set_w(c->d[6], rd16(0xFFFF9A8Cu));
    c->d[6] = set_w(c->d[6], (uint16_t)c->d[6] >> 4);
    c->d[6] = set_w(c->d[6], c->d[6] - 1);
    c->d[7] = set_w(c->d[7], rd16(0xFFFF9A8Eu));
    c->d[7] = set_w(c->d[7], c->d[7] - 0x20);
    c->d[7] = set_w(c->d[7], c->d[7] << 2);
    c->d[0] = rd8(0xFFFF9A8Bu);
    c->d[0] = set_w(c->d[0], c->d[0] << 3);
    c->a[4] = 0x2C46 + sx(c->d[0]);
    c->d[0] = set_w(c->d[0], rd16(c->a[4])); c->a[4] += 2;
    call(0x266C);
    c->d[0] = set_w(c->d[0], rd16(c->a[4])); c->a[4] += 2;
    c->d[6] = set_w(c->d[6], c->d[6] + 1);
    call(SET_SHOWN);
    c->d[0] = set_w(c->d[0], rd16(c->a[4])); c->a[4] += 2;
    c->d[7] = set_w(c->d[7], c->d[7] + 0x40);
    call(SET_SHOWN);
    c->d[6] = set_w(c->d[6], c->d[6] - 1);
    c->d[0] = set_w(c->d[0], rd16(c->a[4]));
    call(SET_SHOWN);
}
/* $2CC0: effect a4 at (d6, d7), cleared, facing, animation 0, drawn */
static void effect_place(void)
{
    wr16(c->a[4] - 0x3800, (uint16_t)c->d[6]);
    wr16(c->a[4] - 0x3700, (uint16_t)c->d[7]);
    call(0xA36);
    wr8(c->a[4] - 0x3FFE, 0x80);
    c->d[0] = 0;
    call(0x7E8);
    wr8(c->a[4] - 0x4000, 0xC0);
}
/* $2C4E: the lift opened: its game flag ($9A8A) set, a sound, a puff and four sparks flying out */
static void lift_open(void)
{
    c->d[0] = set_b(c->d[0], rd8(0xFFFF9A8Au));
    call(0x2752);
    c->d[0] = 0x57; call(0x366);
    wr8(0xFFFF9A8Au, 0);
    wr16(0xFFFF9A94u, 2);
    c->d[6] = set_w(c->d[6], rd16(0xFFFF9A8Cu));
    c->d[7] = set_w(c->d[7], rd16(0xFFFF9A8Eu) - 0x10);
    call(0x27E6);
    if (c->ccr & CC_N) return;
    wr16(c->a[4] - 0x3D00, 0x120A);
    effect_place();
    wr16(c->a[4] - 0x2BFE, 6);
    wr8(c->a[4] - 0x3CFD, 0x12);
    c->a[0] = 0x2CE0;
    c->d[5] = 3;
    do {
        call(0x27E6);
        if (c->ccr & CC_N) return;
        wr16(c->a[4] - 0x3D00, 0x160A);
        effect_place();
        wr16(c->a[4] - 0x3600, rd16(c->a[0])); c->a[0] += 2;
        wr16(c->a[4] - 0x35FE, rd16(c->a[0])); c->a[0] += 2;
        wr16(c->a[4] - 0x2BFE, 0xA);
        wr16(c->a[4] - 0x2600, 0x1E);
        wr8(c->a[4] - 0x3CFD, 0x14);
        c->d[5] = set_w(c->d[5], c->d[5] - 1);
    } while ((c->d[5] & 0xFFFF) != 0xFFFF);
}
/* $2CF0: an effect (kind $10, look $1600) at (d6, d7) for d5 frames */
static void effect_make(void)
{
    call(0x27E6);
    call(0xA36);
    wr16(c->a[4] - 0x3800, (uint16_t)c->d[6]);
    wr16(c->a[4] - 0x3700, (uint16_t)c->d[7]);
    wr16(c->a[4] - 0x3D00, 0x1600);
    wr8(c->a[4] - 0x3CFD, 0x1E);
    c->d[5] = set_w(c->d[5], c->d[5] & 0xFF);
    wr16(c->a[4] - 0x2800, (uint16_t)c->d[5]);
    wr16(c->a[4] - 0x2BFE, 0x10);
}
/* The item rain ($9BB6 on, bit 0 dropped still; $9BB7 its interval, $9BB8 the countdown, $9BB9
 * the next item (bit 7: an effect instead), $9BBA the list; about ($9BBE, $9BC0) within ($9BC2,
 * $9BC4)) */
static void rain_next(void)                     /* $2DDE */
{
    c->a[0] = rd32(0xFFFF9BBAu);
    c->d[0] = set_b(c->d[0], rd8(c->a[0])); c->a[0]++;
    wr8(0xFFFF9BB9u, (uint8_t)c->d[0]);
    c->d[0] = set_b(c->d[0], c->d[0] + 1);
    if (!(c->d[0] & 0xFF)) { wr8(0xFFFF9BB6u, 0); return; }
    wr32(0xFFFF9BBAu, c->a[0]);
    wr8(0xFFFF9BB8u, rd8(0xFFFF9BB7u));
}
/* $2D1C: each interval the next item, at a random place, falling (or dropped still) */
static void item_rain(void)
{
    if (!(rd8(0xFFFF9BB6u) & 0x80)) return;
    wr8(0xFFFF9BB8u, (uint8_t)(rd8(0xFFFF9BB8u) - 1));
    if (rd8(0xFFFF9BB8u)) return;
    c->d[5] = set_b(c->d[5], rd8(0xFFFF9BB9u));
    if (c->d[5] & 0x80) {
        c->d[5] = set_w(c->d[5], c->d[5] & 0x7F);
        c->d[6] = set_w(c->d[6], rd16(0xFFFF9BBEu));
        c->d[7] = set_w(c->d[7], rd16(0xFFFF9BC0u));
        call(0x2CF0);
        wr16(c->a[4] - 0x20FE, (uint16_t)c->d[7]);
        wr8(c->a[4] - 0x31FE, 0xFF);
        wr8(c->a[4] - 0x3200, 0xFF);
        wr16(c->a[4] - 0x35FE, 0xFA00);
        c->d[0] = 0; call(0x7E8);
        wr8(c->a[4] - 0x4000, 0xC0);
        rain_next();
        return;
    }
    call(0x5D8);
    c->d[2] = 0;
    c->d[2] = set_w(c->d[2], c->d[0]);
    c->d[3] = set_w(c->d[3], rd16(0xFFFF9BC4u));
    c->d[2] = swap32(divu_w(c->d[2], (uint16_t)c->d[3]));
    if (c->d[0] & 0x8000) c->d[2] = set_w(c->d[2], -c->d[2]);
    call(0x5D8);
    c->d[1] = 0;
    c->d[1] = set_w(c->d[1], c->d[0]);
    c->d[3] = set_w(c->d[3], rd16(0xFFFF9BC2u));
    c->d[1] = swap32(divu_w(c->d[1], (uint16_t)c->d[3]));
    if (c->d[0] & 0x8000) c->d[1] = set_w(c->d[1], -c->d[1]);
    c->a[7] -= 4; wr16(c->a[7], (uint16_t)c->d[1]); wr16(c->a[7] + 2, (uint16_t)c->d[2]);
    c->d[1] = set_w(c->d[1], c->d[1] + rd16(0xFFFF9BBEu));
    c->d[2] = set_w(c->d[2], c->d[2] + rd16(0xFFFF9BC0u));
    c->d[0] = rd8(0xFFFF9BB9u);
    call(0x1090C);
    c->d[0] = set_b(c->d[0], (c->ccr & CC_N) ? 0 : 0xFF);
    c->d[1] = sx(rd16(c->a[7])); c->d[2] = sx(rd16(c->a[7] + 2)); c->a[7] += 4;
    if (!(c->d[0] & 0xFF)) { wr8(0xFFFF9BB6u, 0); return; }
    wr8(c->a[0] - 0x25FD, rd8(c->a[0] - 0x25FD) | 1);
    c->d[1] = set_w(c->d[1], c->d[1] << 3);
    wr16(c->a[0] - 0x2100, (uint16_t)c->d[1]);
    if (rd8(0xFFFF9BB6u) & 1) c->d[0] = 0;
    else {
        call(0x5D8);
        c->d[0] = set_w(c->d[0], c->d[0] & 0x1FF);
        c->d[0] = set_w(c->d[0], c->d[0] + 0x400);
        c->d[0] = set_w(c->d[0], -c->d[0]);
    }
    wr16(c->a[0] - 0x20FE, (uint16_t)c->d[0]);
    rain_next();
}
/* $2DFE: the rain started from a0 (flags, interval, first delay, then its list; in area $12 the
 * list at $2E2A), about (d0, d1) within (d2, d3) */
static void item_rain_start(void)
{
    c->d[4] = set_b(c->d[4], rd8(c->a[0]) | 0x80); c->a[0]++;
    wr8(0xFFFF9BB6u, (uint8_t)c->d[4]);
    wr8(0xFFFF9BB7u, rd8(c->a[0])); c->a[0]++;
    wr8(0xFFFF9BB8u, rd8(c->a[0])); c->a[0]++;
    if (rd8(0xFFFF9668u) == 0x12) c->a[0] = 0x2E2A;
    wr32(0xFFFF9BBAu, c->a[0]);
    c->a[0] = 0xFFFF9BBEu;
    wr16(0xFFFF9BBEu, (uint16_t)c->d[0]); wr16(0xFFFF9BC0u, (uint16_t)c->d[1]);
    wr16(0xFFFF9BC2u, (uint16_t)c->d[2]); wr16(0xFFFF9BC4u, (uint16_t)c->d[3]);
    rain_next();
}
ENTRY(LiftCells, lift_cells)               /* $2C08 */
ENTRY(LiftOpen, lift_open)                 /* $2C4E */
ENTRY(EffectPlace, effect_place)           /* $2CC0 */
ENTRY(EffectMake, effect_make)             /* $2CF0 */
ENTRY(ItemRain, item_rain)                 /* $2D1C */
ENTRY(ItemRainStart, item_rain_start)      /* $2DFE */

/* ---- batch T68: the VDP registers' helpers, the on-screen tests, velocities cleared ($05FE-$0D2C) ----
 * The VDP's registers are kept in RAM ($8A5A-$8A6E, a word each with its register number) and
 * written to the control port from there. */
#define VDP_CTRL 0xC00004u
static void vdp_reg_out(uint32_t mirror)          /* the mirror and the port from d0 */
{
    wr16(mirror, (uint16_t)c->d[0]);
    wr16(VDP_CTRL, (uint16_t)c->d[0]);
    F_W(c->d[0]);
}
static void reg_or(uint32_t mirror, uint32_t bits) { c->d[0] = bits; c->d[0] = set_w(c->d[0], c->d[0] | rd16(mirror)); vdp_reg_out(mirror); }
static void reg_and(uint32_t mirror, uint32_t bits) { c->d[0] = bits; c->d[0] = set_w(c->d[0], c->d[0] & rd16(mirror)); vdp_reg_out(mirror); }
/* register 0 ($8A5A) */
static void vdp0_set10(void) { reg_or(0xFFFF8A5Au, 0x10); }                    /* $5FE */
static void vdp0_clr10(void) { reg_and(0xFFFF8A5Au, 0xFFFFFFEFu); }            /* $606 */
static void vdp0_store(void) { vdp_reg_out(0xFFFF8A5Au); }                      /* $60C */
/* register 1 ($8A5C): $618 the display on (and $8006 bit 1); $62A off, then the wait at $3B8 */
static void display_on(void)
{
    c->d[0] = 0x40;
    c->d[0] = set_w(c->d[0], c->d[0] | rd16(0xFFFF8A5Cu));
    wr16(0xFFFF8A5Cu, (uint16_t)c->d[0]);
    wr8(0xFFFF8006u, rd8(0xFFFF8006u) | 2);
}
void DisplayOff(Cpu *cpu)                        /* $62A: then the frame's wait in place ($3B8) */
{
    c = cpu;
    c->d[0] = 0xFFFFFFBFu;
    c->d[0] = set_w(c->d[0], c->d[0] & rd16(0xFFFF8A5Cu));
    wr16(0xFFFF8A5Cu, (uint16_t)c->d[0]);
    wr8(0xFFFF8006u, rd8(0xFFFF8006u) | 2);
    port_continue_at(c, 0x3B8, 0);
}
static void vdp1_set40(void) { reg_or(0xFFFF8A5Cu, 0x40); }                    /* $63E */
static void vdp1_clr40(void) { reg_and(0xFFFF8A5Cu, 0xFFFFFFBFu); }            /* $646 */
static void vdp1_set20(void) { reg_or(0xFFFF8A5Cu, 0x20); }                    /* $64E */
static void vdp1_clr20(void) { reg_and(0xFFFF8A5Cu, 0xFFFFFFDFu); }            /* $656 */
static void vdp1_set10(void) { reg_or(0xFFFF8A5Cu, 0x10); }                    /* $65E */
static void vdp1_or(void)  { c->d[0] = set_w(c->d[0], c->d[0] | rd16(0xFFFF8A5Cu)); vdp_reg_out(0xFFFF8A5Cu); }   /* $660 */
static void vdp1_clr10(void) { reg_and(0xFFFF8A5Cu, 0xFFFFFFEFu); }            /* $666 */
static void vdp1_and(void) { c->d[0] = set_w(c->d[0], c->d[0] & rd16(0xFFFF8A5Cu)); vdp_reg_out(0xFFFF8A5Cu); }  /* $668 */
static void vdp1_store(void) { vdp_reg_out(0xFFFF8A5Cu); }                      /* $66C */
/* $678 and $708: a register's value from d0 ($8A6C, $8A6E: the number then the value) */
static void vdp_byte_out(uint32_t mirror)
{
    wr8(mirror + 1, (uint8_t)c->d[0]);
    uint16_t v = rd16(mirror);
    wr16(VDP_CTRL, v);
    F_W(v);
}
static void vdp_6c(void) { vdp_byte_out(0xFFFF8A6Cu); }                          /* $678 */
static void vdp_6e(void) { vdp_byte_out(0xFFFF8A6Eu); }                          /* $708 */
/* register 11 ($8A5E) */
static void vdp11_set8(void) { reg_or(0xFFFF8A5Eu, 8); }                        /* $686 */
static void vdp11_clr8(void) { reg_and(0xFFFF8A5Eu, 0xFFFFFFF7u); }             /* $68A */
static void vdp11_clr4(void) { c->d[0] = set_w(c->d[0], 0xFFFB); c->d[0] = set_w(c->d[0], c->d[0] & rd16(0xFFFF8A5Eu)); vdp_reg_out(0xFFFF8A5Eu); }   /* $68E */
static void vdp11_set4(void) { reg_or(0xFFFF8A5Eu, 4); }                        /* $694 */
static void vdp11_clr3(void) { reg_and(0xFFFF8A5Eu, 0xFFFFFFFCu); }             /* $698 */
static void vdp11_and(void) { c->d[0] = set_w(c->d[0], c->d[0] & rd16(0xFFFF8A5Eu)); vdp_reg_out(0xFFFF8A5Eu); }  /* $69A */
static void vdp11_set2(void)                                                    /* $6A0 */
{
    c->d[0] = 0xFFFFFFFCu;
    c->d[0] = set_w(c->d[0], c->d[0] & rd16(0xFFFF8A5Eu));
    c->d[0] = set_w(c->d[0], c->d[0] | 2);
    vdp_reg_out(0xFFFF8A5Eu);
}
static void vdp11_set3(void) { reg_or(0xFFFF8A5Eu, 3); }                        /* $6AC */
static void vdp11_or(void) { c->d[0] = set_w(c->d[0], c->d[0] | rd16(0xFFFF8A5Eu)); vdp_reg_out(0xFFFF8A5Eu); }   /* $6AE */
/* register 12 ($8A60) */
static void vdp12_low(uint32_t or_bits)                                         /* $6BE, $6C8 */
{
    c->d[0] = set_w(c->d[0], rd16(0xFFFF8A60u));
    c->d[0] = set_b(c->d[0], c->d[0] & 0x7E);
    c->d[0] = set_b(c->d[0], c->d[0] | or_bits);
    vdp_reg_out(0xFFFF8A60u);
}
static void vdp12_h32(void) { vdp12_low(0); }
static void vdp12_h40(void) { vdp12_low(0x81); }
static void vdp12_clr6(void) { reg_and(0xFFFF8A60u, 0xFFFFFFF9u); }             /* $6D6 */
static void vdp12_mode(uint32_t bits)                                           /* $6DA, $6E4 */
{
    c->d[0] = 0xFFFFFFF9u;
    wr16(0xFFFF8A60u, (uint16_t)(rd16(0xFFFF8A60u) & c->d[0]));
    c->d[0] = bits;
    c->d[0] = set_w(c->d[0], c->d[0] | rd16(0xFFFF8A60u));
    vdp_reg_out(0xFFFF8A60u);
}
static void vdp12_mode2(void) { vdp12_mode(2); }
static void vdp12_mode4(void) { vdp12_mode(4); }
static void vdp12_set8(void) { reg_or(0xFFFF8A60u, 8); }                        /* $6EE */
static void vdp12_or(void) { c->d[0] = set_w(c->d[0], c->d[0] | rd16(0xFFFF8A60u)); vdp_reg_out(0xFFFF8A60u); }   /* $6F0 */
static void vdp12_clr8(void) { reg_and(0xFFFF8A60u, 0xFFFFFFF7u); }             /* $6F6 */
static void vdp12_and(void) { c->d[0] = set_w(c->d[0], c->d[0] & rd16(0xFFFF8A60u)); vdp_reg_out(0xFFFF8A60u); }  /* $6F8 */
static void vdp12_store(void) { vdp_reg_out(0xFFFF8A60u); }                     /* $6FC */
/* the planes' addresses: register 2 (plane A, $8A62), 3 (the window, $8A66; even in H40),
 * 4 (plane B, $8A64), 5 (the sprites, $8A68; even in H40), 13 (the scroll table, $8A6A) */
static uint16_t rol16(uint16_t v, int n) { return (uint16_t)(v << n | v >> (16 - n)); }
static void plane_reg(uint32_t mirror, int rol, uint16_t mask, uint16_t h40_mask, uint16_t reg)
{
    c->d[0] = set_w(c->d[0], rd16(mirror));
    c->d[0] = set_w(c->d[0], rol16((uint16_t)c->d[0], rol));
    c->d[0] = set_w(c->d[0], c->d[0] & mask);
    if (h40_mask && (rd8(0xFFFF8A60u) & 1)) c->d[0] = set_w(c->d[0], c->d[0] & h40_mask);
    c->d[0] = set_w(c->d[0], c->d[0] | reg);
    wr16(VDP_CTRL, (uint16_t)c->d[0]);
    F_W(c->d[0]);
}
static void plane_a_reg(void)  { plane_reg(0xFFFF8A62u, 6, 0x38, 0, 0x8200); }      /* $716 */
static void window_reg(void)   { plane_reg(0xFFFF8A66u, 6, 0x3E, 0x3C, 0x8300); }   /* $72C */
static void plane_b_reg(void)  { plane_reg(0xFFFF8A64u, 3, 0x07, 0, 0x8400); }      /* $74E */
static void sprites_reg(void)  { plane_reg(0xFFFF8A68u, 7, 0x7F, 0x7E, 0x8500); }   /* $764 */
static void hscroll_reg(void)  { plane_reg(0xFFFF8A6Au, 6, 0x3F, 0, 0x8D00); }      /* $786 */
/* $79C: register 16, the planes' size from their widths in cells ($8A72 high, $8A70 wide), and
 * their masks ($8A86, $8A88, $8A8A) */
static void plane_size_reg(void)
{
    c->d[0] = 0;
    c->a[0] = 0x7E0;
    c->d[1] = set_w(c->d[1], rd16(0xFFFF8A72u));
    c->d[2] = set_w(c->d[2], c->d[1]);
    c->d[2] = set_w(c->d[2], c->d[2] << 3);
    c->d[2] = set_w(c->d[2], c->d[2] - 1);
    wr16(0xFFFF8A86u, (uint16_t)c->d[2]);
    c->d[1] = set_w(c->d[1], (uint16_t)c->d[1] >> 6);
    c->d[0] = set_b(c->d[0], rd8(c->a[0] + sx(c->d[1])));
    c->d[0] = set_w(c->d[0], c->d[0] << 4);
    c->d[1] = set_w(c->d[1], rd16(0xFFFF8A70u));
    c->d[2] = set_w(c->d[2], c->d[1]);
    c->d[2] = set_w(c->d[2], c->d[2] << 3);
    c->d[2] = set_w(c->d[2], c->d[2] - 1);
    wr16(0xFFFF8A88u, (uint16_t)c->d[2]);
    c->d[1] = set_w(c->d[1], (uint16_t)c->d[1] >> 6);
    c->d[0] = set_b(c->d[0], c->d[0] | rd8(c->a[0] + sx(c->d[1])));
    c->d[0] = set_w(c->d[0], c->d[0] | 0x9000);
    wr16(VDP_CTRL, (uint16_t)c->d[0]);
    uint8_t m = rd8(c->a[0] + 4 + sx(c->d[1]));
    wr8(0xFFFF8A8Au, m);
    F_B(m);
}
/* $A1A: the entity in use and drawn, its animation cleared ($A20) */
static void entity_on(void) { wr16(c->a[4] - 0x4000, 0xC000); call(0xA20); }
/* $BC6 (its x) and $BD0 (d0, half width d2): off the screen across, $20 of margin: d0 -1, else 0;
 * $BF4 and $BFE the same for y; $BEE both */
static void off_screen(uint32_t camera, uint16_t size)
{
    c->d[0] = set_w(c->d[0], c->d[0] - rd16(camera));
    c->d[1] = set_w(c->d[1], c->d[0]);
    c->d[1] = set_w(c->d[1], c->d[1] + c->d[2]);
    c->d[1] = set_w(c->d[1], c->d[1] + 0x20);
    if (!(c->d[1] & 0x8000)) {
        c->d[0] = set_w(c->d[0], c->d[0] - c->d[2]);
        c->d[0] = set_w(c->d[0], c->d[0] - size);
        if (c->d[0] & 0x8000) { c->d[0] = 0; F_L(0); return; }
    }
    c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]);
}
static void off_x_at(void) { off_screen(0xFFFF9758u, 0x140); }                 /* $BD0 */
static void off_y_at(void) { off_screen(0xFFFF975Au, 0xE0); }                  /* $BFE */
static void off_x(void)                                                         /* $BC6 */
{
    c->d[0] = set_w(c->d[0], rd16(c->a[4] - 0x3800));
    c->d[2] = rd8(c->a[4] - 0x34FE);
    off_x_at();
}
static void off_y(void)                                                         /* $BF4 */
{
    c->d[0] = set_w(c->d[0], rd16(c->a[4] - 0x3700));
    c->d[2] = rd8(c->a[4] - 0x34FD);
    off_y_at();
}
static void off_screen_either(void) { off_x(); if (c->ccr & CC_Z) off_y(); }     /* $BEE */
/* $C1C: velocities and their fractions cleared; $C22 the fractions; $C2E the velocities */
static void stop_all(void) { c->d[0] = 0; wr32(c->a[4] - 0x3600, 0); wr16(c->a[4] - 0x37FE, 0); wr16(c->a[4] - 0x36FE, 0); F_L(0); }
static void stop_fractions(void) { c->d[0] = 0; wr16(c->a[4] - 0x37FE, 0); wr16(c->a[4] - 0x36FE, 0); F_L(0); }
static void stop_velocity(void) { c->d[0] = 0; wr32(c->a[4] - 0x3600, 0); F_L(0); }
/* $D24 (not called): $D2C with a2 and a4 swapped */
static void d2c_swapped(void)
{
    uint32_t t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t;
    call(0xD2C);
    t = c->a[2]; c->a[2] = c->a[4]; c->a[4] = t;
}
ENTRY(Vdp0Set10, vdp0_set10)               /* $5FE */
ENTRY(Vdp0Clr10, vdp0_clr10)               /* $606 */
ENTRY(Vdp0Store, vdp0_store)               /* $60C */
ENTRY(DisplayOn, display_on)               /* $618 */
ENTRY(Vdp1Set40, vdp1_set40)               /* $63E */
ENTRY(Vdp1Clr40, vdp1_clr40)               /* $646 */
ENTRY(Vdp1Set20, vdp1_set20)               /* $64E */
ENTRY(Vdp1Clr20, vdp1_clr20)               /* $656 */
ENTRY(Vdp1Set10, vdp1_set10)               /* $65E */
ENTRY(Vdp1Or, vdp1_or)                     /* $660 */
ENTRY(Vdp1Clr10, vdp1_clr10)               /* $666 */
ENTRY(Vdp1And, vdp1_and)                   /* $668 */
ENTRY(Vdp1Store, vdp1_store)               /* $66C */
ENTRY(Vdp6C, vdp_6c)                       /* $678 */
ENTRY(Vdp11Set8, vdp11_set8)               /* $686 */
ENTRY(Vdp11Clr8, vdp11_clr8)               /* $68A */
ENTRY(Vdp11Clr4, vdp11_clr4)               /* $68E */
ENTRY(Vdp11Set4, vdp11_set4)               /* $694 */
ENTRY(Vdp11Clr3, vdp11_clr3)               /* $698 */
ENTRY(Vdp11And, vdp11_and)                 /* $69A */
ENTRY(Vdp11Set2, vdp11_set2)               /* $6A0 */
ENTRY(Vdp11Set3, vdp11_set3)               /* $6AC */
ENTRY(Vdp11Or, vdp11_or)                   /* $6AE */
ENTRY(Vdp12H32, vdp12_h32)                 /* $6BE */
ENTRY(Vdp12H40, vdp12_h40)                 /* $6C8 */
ENTRY(Vdp12Clr6, vdp12_clr6)               /* $6D6 */
ENTRY(Vdp12Mode2, vdp12_mode2)             /* $6DA */
ENTRY(Vdp12Mode4, vdp12_mode4)             /* $6E4 */
ENTRY(Vdp12Set8, vdp12_set8)               /* $6EE */
ENTRY(Vdp12Or, vdp12_or)                   /* $6F0 */
ENTRY(Vdp12Clr8, vdp12_clr8)               /* $6F6 */
ENTRY(Vdp12And, vdp12_and)                 /* $6F8 */
ENTRY(Vdp12Store, vdp12_store)             /* $6FC */
ENTRY(Vdp6E, vdp_6e)                       /* $708 */
ENTRY(PlaneAReg, plane_a_reg)              /* $716 */
ENTRY(WindowReg, window_reg)               /* $72C */
ENTRY(PlaneBReg, plane_b_reg)              /* $74E */
ENTRY(SpritesReg, sprites_reg)             /* $764 */
ENTRY(HScrollReg, hscroll_reg)             /* $786 */
ENTRY(PlaneSizeReg, plane_size_reg)        /* $79C */
ENTRY(EntityOn, entity_on)                 /* $A1A */
ENTRY(OffScreenX, off_x)                   /* $BC6 */
ENTRY(OffScreenXAt, off_x_at)              /* $BD0 */
ENTRY(OffScreen, off_screen_either)        /* $BEE */
ENTRY(OffScreenY, off_y)                   /* $BF4 */
ENTRY(OffScreenYAt, off_y_at)              /* $BFE */
ENTRY(StopAll, stop_all)                   /* $C1C */
ENTRY(StopFractions, stop_fractions)       /* $C22 */
ENTRY(StopVelocity, stop_velocity)         /* $C2E */
ENTRY(D2cSwapped, d2c_swapped)             /* $D24 */
