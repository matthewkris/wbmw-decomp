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
