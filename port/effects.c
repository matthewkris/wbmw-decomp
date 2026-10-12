/* Items and effects: the spawner, as ordinary C (the idiomatic rewrite; docs/PORT.md).
 *
 * Items and effects (gold, hearts, sparkles, debris, the sword's trail...) live in the sixteen
 * entity slots $80-$BC. A kind is a word: its high byte picks a group and its low byte the kind
 * in it, both byte offsets into the table at $11094, which gives each kind an 8-byte record:
 *
 *    +0 word  animation set        +4 byte  speed limit x (<< 4)
 *    +2 byte  priority (0 first)   +5 byte  speed limit y (<< 4)
 *    +3 byte  -> -$2200            +6, +7   acceleration x, y (signed)
 *
 * When every slot is busy, the new kind replaces the first one of a less important kind (a
 * higher priority number), else the first drawn one of the same priority, else it is dropped.
 * Kinds $1000 and up are the big items, spawned elsewhere ($B42A, not rewritten yet). */
#include "world.h"

#define ITEMS         0x11094u      /* the kinds' table */

#define ALIVE(e)      ((e) - 0x4000)
#define TYPE(e)       ((e) - 0x2BFE)   /* word: the kind */
#define MOVE_FLAGS(e) ((e) - 0x2F00)   /* bit 7 copied from the parent (SpawnItemAt) */

/* the table's offsets are signed words, and so are the indexes into it */
static uint32_t at(uint32_t base, int16_t index) { return base + (uint32_t)(int32_t)rdw(base + (uint32_t)(int32_t)index); }

static uint32_t group_of(uint16_t kind) { return at(ITEMS, (int16_t)(kind >> 8)); }
static uint32_t record(uint16_t kind) { return ITEMS + (uint32_t)(int32_t)rdw(group_of(kind) + (kind & 0xFF)); }

/* the slot for a new item of `kind`, or -1: a free one, else one it may replace */
static int32_t slot_for(Cpu *c, uint16_t kind)
{
    for (Ent e = 0x80; e < 0xC0; e += 4)
        if (!(rd8(ALIVE(e)) & 0x80)) return (int32_t)e;
    /* all busy. The original keeps the record offset of the last slot looked at in a register
     * and replaces only its low byte with the next slot's group, so a group's table is found
     * from that leftover high byte too: kept. */
    uint16_t own = (uint16_t)rdw(group_of(kind) + (kind & 0xFF));
    uint8_t prio = rd8(ITEMS + (uint32_t)(int32_t)(int16_t)own + 2);
    c->d[4] = (c->d[4] & 0xFFFF0000u) | (own & 0xFF00) | prio;   /* the registers as it leaves them */
    c->d[7] = 0xFFFFFFFFu;
    c->a[2] = ITEMS;
    int32_t same = -1;
    uint16_t r = 0;
    for (Ent e = 0x80; e < 0xC0; e += 4) {
        uint16_t lead = r & 0xFF00;
        c->a[1] = at(ITEMS, (int16_t)(lead | rd8(TYPE(e))));
        r = (uint16_t)rdw(c->a[1] + (uint32_t)(int32_t)(int16_t)(lead | rd8(TYPE(e) + 1)));
        c->d[5] = r;
        uint8_t p = rd8(ITEMS + (uint32_t)(int32_t)(int16_t)r + 2);
        if (prio < p) return (int32_t)e;
        if (prio == p && (rd8(ALIVE(e)) & 0x40) && same < 0) {
            same = (int32_t)e;
            c->d[7] = 0xFFFF0000u | e;
        }
    }
    return same;
}

/* $1090C / $10910: a new item of kind d0 at d1, d2 (SpawnItemAt: a4 its parent). Out: a0 the
 * item, d3 0 and Z set; or d3 -1 and N set if there was no room. */
static void place(Cpu *c);
static void big_item(Cpu *c);

static void spawn(Cpu *c, int at)
{
    c->d[6] = at ? 0xFFFFFF00u | (rd8(MOVE_FLAGS(c->a[4])) & 0x80) : 0;
    if ((uint16_t)c->d[0] >= 0x1000) big_item(c);
    else place(c);
}

/* the 68000's movem.w back into a register: the word, sign-extended */
static uint32_t sext(uint32_t v) { return (uint32_t)(int32_t)(int16_t)v; }

/* $B42A: a big item (kinds $1000 and up) goes in the eight slots $E0-$FC, one at a time (it
 * sets the timer $FF9962 to 9; while that runs there is no other). A heart container ($40)
 * once the maximum is 14 hearts, and the revival item ($2A) when the player has it already,
 * become kind $1A (a small pickup) instead. Out: d0 0 and Z set if placed, N set if not. */
static void big_item(Cpu *c)
{
    uint8_t k = (uint8_t)c->d[0];
    int check = k == 0x2A;
    if (k == 0x40 && rd16(0xFFFF9F00u) >= 0xE00) { c->d[0] = (c->d[0] & 0xFFFF0000u) | 0x102A; check = 1; }
    if (check) {
        uint32_t d0 = c->d[0], d1 = c->d[1], d2 = c->d[2], d6 = c->d[6];
        call_68k(c, 0x23E4);                        /* has the item? (Z clear if so) */
        c->d[0] = sext(d0); c->d[1] = sext(d1); c->d[2] = sext(d2); c->d[6] = sext(d6);
        if (!(c->ccr & CC_Z)) {
            c->d[0] = 0x1A;
            place(c);
            c->ret = 0;
            return;
        }
    }
    c->ret = 0;
    if (rdw(0xFFFF9962u)) {
        c->d[0] = 0xFFFFFFFFu;
        c->ccr = (uint16_t)((c->ccr & CC_X) | CC_N);
        return;
    }
    c->d[3] = (c->d[3] & 0xFFFF0000u) | (uint16_t)c->d[0];
    uint32_t caller = c->a[4];
    c->a[3] = caller;
    /* FindFreeEffect ($27E6): the first of $E0-$FC not in use */
    uint16_t n = 7;
    Ent e = 0xE0;
    while ((rd8(ALIVE(e)) & 0x80) && n != 0xFFFF) { n--; if (n != 0xFFFF) e += 4; }
    c->d[0] = (c->d[0] & 0xFFFF0000u) | n;
    c->a[4] = e;
    if (rd8(ALIVE(e)) & 0x80) {                     /* none free */
        c->ccr = (uint16_t)((c->ccr & CC_X) | CC_N);
        c->a[4] = caller;
        return;
    }
    wr8(ALIVE(e), 0x80);
    wrw(e - 0x3800, (uint16_t)c->d[1]);
    wrw(e - 0x3700, (uint16_t)c->d[2]);
    c->d[3] = (c->d[3] & 0xFFFF0000u) | ((uint16_t)c->d[3] & 0xFF);
    wrw(e - 0x2300, (uint16_t)c->d[3]);             /* which big item */
    wrw(0xFFFF9968u, (uint16_t)e);
    wrw(TYPE(e), 4);
    wrw(0xFFFF9962u, 9);
    c->d[0] = 0;
    c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    c->a[4] = caller;
}

/* $10916 on: a kind below $1000 in the slots $80-$BC; d6's low byte is its MOVE_FLAGS */
static void place(Cpu *c)
{
    uint16_t kind = (uint16_t)c->d[0];
    uint8_t flags = (uint8_t)c->d[6];
    int32_t s = slot_for(c, kind);
    if (s < 0) {
        c->a[0] = 0xC0; c->d[3] = 0xFFFFFFFFu;
        c->ccr = (uint16_t)((c->ccr & CC_X) | CC_N);
        return;
    }
    Ent e = (Ent)s;
    uint32_t rec = record(kind);
    wr8(MOVE_FLAGS(e), flags);
    wr8(ALIVE(e), 0x80);
    wrw(TYPE(e), kind);
    wrw(e - 0x3800, (uint16_t)c->d[1]);             /* x, y */
    wrw(e - 0x3700, (uint16_t)c->d[2]);
    wrw(e - 0x3D00, rdw(rec));                      /* animation set */
    wr8(e - 0x2200, rd8(rec + 3));
    wrw(e - 0x3300, rd8(rec + 4) << 4);             /* speed limits */
    wrw(e - 0x32FE, rd8(rec + 5) << 4);
    wrw(e - 0x3400, (int8_t)rd8(rec + 6));          /* acceleration */
    wrw(e - 0x33FE, (int8_t)rd8(rec + 7));
    wr8(e - 0x3CFD, 10);                            /* animation rate, timer step */
    wrw(e - 0x39FE, 0x100);
    wrw(e - 0x3FFE, 0);                             /* and the rest cleared */
    wr32(e - 0x3E00, 0);
    wrw(e - 0x37FE, 0);
    wrw(e - 0x36FE, 0);
    wr32(e - 0x3600, 0);
    wr8(e - 0x2BFF, 0);
    wr8(e - 0x3100, 0);
    wr8(e - 0x2EFF, 0);
    wr8(e - 0x25FD, 0);
    /* the registers as the original leaves them */
    c->a[0] = e;
    c->a[1] = rec + 7;
    c->a[2] = group_of(kind);
    c->d[0] = (c->d[0] & 0xFFFF0000u) | (kind & 0xFF);
    c->d[3] = 0;
    c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
}

void SpawnItem(Cpu *c)   { spawn(c, 0); }
void SpawnItemAt(Cpu *c) { spawn(c, 1); }
