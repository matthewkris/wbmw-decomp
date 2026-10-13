/* The rooms' data, as ordinary C (the idiomatic rewrite; docs/PORT.md, "The rooms").
 *
 * A room has two layers, each described by a record ($FF9754 for plane A, $FF97D4 for plane B):
 * among its fields, the room's data at $34, its blocks' table at $1C and its map at $22. The data
 * starts with offsets to its packed blocks and its packed map.
 *
 * Batch T11, the unpackers:
 * - $9F6A, the blocks: a block is four tiles (a word each) and a byte, packed by a nibble each
 *   telling how its tiles follow from its first (in a run, the same palette and plane, or each
 *   given); 8 bytes a block in the table, the bytes $800 after.
 * - $A068, a layer's map: a chunk for each place used, each of 192 cells of blocks, packed as
 *   runs of one block over a bitmap of the cells, then the rest as 10-bit numbers four to five
 *   bytes, or with zero runs ($A1C4).
 * Batch T12, loading a room: its list of commands (layers, graphics, palettes, music, doors and
 * things), the layers' records, the ending's scenes and the room's music.
 * Ported register for register, with every register and flag in the contract. */
#include "world.h"

#define DATA_DIR      0x0001CC00u   /* the ROM's directory of data tables */
#define FILLED        0xFFFF970Cu   /* 24 bytes: a chunk's cells written (a bit each) */
#define ZERO_RUNS     0xFFFF9862u   /* $A1C4's output */

static Cpu *c;

static void f_move(uint32_t v, uint32_t sign) { c->ccr = (uint16_t)((c->ccr & CC_X) | (v & sign ? CC_N : 0) | ((v & (sign * 2 - 1)) ? 0 : CC_Z)); }
#define F_B(v) f_move((v), 0x80u)
#define F_W(v) f_move((v), 0x8000u)
#define F_L(v) f_move((v), 0x80000000u)
static void f_bit(int on) { c->ccr = (uint16_t)((c->ccr & ~CC_Z) | (on ? 0 : CC_Z)); }   /* btst, bset, bclr */
static uint32_t sx(uint32_t w) { return (uint32_t)(int32_t)(int16_t)w; }
static uint8_t next8(int r) { return rd8(c->a[r]++); }   /* (an)+, a byte */
static void put16(int r, uint32_t v) { wr16(c->a[r], (uint16_t)v); c->a[r] += 2; F_W(v); }   /* move.w dn,(an)+ */
static void and_w(int r, uint32_t m) { c->d[r] = set_w(c->d[r], c->d[r] & m); F_W(c->d[r]); }
static void and_b(int r, uint32_t m) { c->d[r] = set_b(c->d[r], c->d[r] & m); F_B(c->d[r]); }
static void or_w(int r, uint32_t v) { c->d[r] = set_w(c->d[r], c->d[r] | v); F_W(c->d[r]); }
static void or_b(int r, uint32_t v) { c->d[r] = set_b(c->d[r], c->d[r] | v); F_B(c->d[r]); }
static void move_b(int r, uint8_t v) { c->d[r] = set_b(c->d[r], v); F_B(v); }
static uint32_t roxl_w(uint16_t *ccr, uint32_t v, unsigned n)   /* roxl.w #1 (n is 1 here) */
{
    (void)n;
    uint16_t x = *ccr & CC_X ? 1 : 0, out = (uint16_t)(v >> 15 & 1), r = (uint16_t)(v << 1 | x);
    *ccr = (uint16_t)((out ? CC_X | CC_C : 0) | (r & 0x8000 ? CC_N : 0) | (r ? 0 : CC_Z));
    return r;
}
static int dbra(int r) { c->d[r] = set_w(c->d[r], c->d[r] - 1); return (c->d[r] & 0xFFFF) != 0xFFFF; }

/* $9F6A: the blocks at a3 into the table at a1 (and their bytes at a1 + $800). The first word is
 * the count; then a nibble a block (high first), then the blocks' bytes from half-way */
static void unpack_blocks(void)
{
    c->d[4] = set_w(c->d[4], rd16(c->a[3])); c->a[3] += 2; F_W(c->d[4]);
    if (!(c->d[4] & 0xFFFF)) return;
    c->a[0] = c->a[1] + 0x800;
    c->d[0] = set_w(c->d[0], c->d[4]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, 1, c->d[0]));
    c->d[0] = set_w(c->d[0], lsr_n(&c->ccr, c->d[0], 1, 16));
    c->a[2] = c->a[3] + sx(c->d[0]);
    c->d[3] = 0;
    c->d[4] = set_w(c->d[4], sub_w(&c->ccr, 1, c->d[4]));
    do {
        c->d[3] = set_w(c->d[3], c->d[3] ^ 1); F_W(c->d[3]);
        if (c->d[3] & 0xFFFF) {
            move_b(1, rd8(c->a[3]));
            c->d[1] = set_b(c->d[1], lsr_n(&c->ccr, c->d[1], 4, 8));
        } else move_b(1, next8(3));
        and_b(1, 0xF);
        if (!(c->d[1] & 0xFF)) { c->a[0] += 1; c->a[1] += 8; continue; }   /* an empty block */
        c->d[1] = set_b(c->d[1], c->d[1] ^ 0xF); F_B(c->d[1]);
        c->d[0] = 0; F_L(0);
        f_bit(c->d[1] >> 3 & 1);
        if (!(c->d[1] & 8)) move_b(0, next8(2));                       /* its byte, or 0 */
        wr8(c->a[0]++, (uint8_t)c->d[0]); F_B(c->d[0]);
        move_b(0, next8(2));
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 8, 16));
        move_b(0, next8(2));
        put16(1, c->d[0]);                                              /* the first tile */
        and_b(1, 7);
        if (!(c->d[1] & 0xFF)) {                                        /* 0: the other three given */
            for (int k = 0; k < 6; k++) { uint8_t v = next8(2); wr8(c->a[1]++, v); F_B(v); }
            continue;
        }
        c->d[1] = set_b(c->d[1], sub_b(&c->ccr, 2, c->d[1]));
        if (!(c->d[1] & 0xFF)) {                                        /* 2: three numbers and the flips, in 2 bytes */
            and_w(0, 0xFFE0);
            move_b(1, next8(2)); and_w(1, 0x1F); or_w(1, c->d[0]); put16(1, c->d[1]);
            move_b(1, rd8(c->a[2])); and_w(1, 0x1F); or_w(1, c->d[0]); put16(1, c->d[1]);
            move_b(1, next8(2)); and_w(1, 0x60);
            c->d[1] = set_w(c->d[1], lsr_n(&c->ccr, c->d[1], 2, 16));
            or_w(0, c->d[1]);
            move_b(1, rd8(c->a[2] - 2));
            c->d[1] = set_b(c->d[1], lsr_n(&c->ccr, c->d[1], 5, 8));
            or_b(0, c->d[1]);
            put16(1, c->d[0]);
            continue;
        }
        if (c->ccr & CC_C) {                                            /* 1: the next tiles in a 16-wide sheet */
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, 1, c->d[0])); put16(1, c->d[0]);
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, 0xF, c->d[0])); put16(1, c->d[0]);
            c->d[0] = set_w(c->d[0], add_w(&c->ccr, 1, c->d[0])); put16(1, c->d[0]);
            continue;
        }
        c->d[1] = set_b(c->d[1], sub_b(&c->ccr, 1, c->d[1]));
        if (!(c->d[1] & 0xFF)) {                                        /* 3: the low bytes given */
            for (int k = 0; k < 3; k++) { move_b(0, next8(2)); put16(1, c->d[0]); }
            continue;
        }
        and_w(0, 0xE7E0);                                               /* 4-7: numbers and flips, a byte each */
        for (int k = 0; k < 3; k++) {
            move_b(1, next8(2));
            move_b(2, (uint8_t)c->d[1]);
            and_w(2, 0x1F); or_w(2, c->d[0]);
            and_w(1, 0xC0);
            c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 5, 16));
            or_w(1, c->d[2]);
            put16(1, c->d[1]);
        }
    } while (dbra(4));
}

/* $9F64: a room's blocks (a4 its data: the first word the offset to them) */
static void unpack_room_blocks(void)
{
    c->d[4] = set_w(c->d[4], rd16(c->a[4])); F_W(c->d[4]);
    c->a[3] = c->a[4] + sx(c->d[4]);
    unpack_blocks();
}

/* $9F46: the blocks every room shares, three sets (from the directory's table 1) */
static void load_shared_blocks(void)
{
    c->a[4] = rd32(DATA_DIR + 4);
    c->a[3] = rd32(c->a[4]); c->a[4] += 4;
    c->a[1] = 0xFFFFE500u;
    unpack_blocks();
    c->a[3] = rd32(c->a[4]); c->a[4] += 4;
    c->a[1] = 0xFFFFEE00u;
    unpack_blocks();
    c->a[3] = rd32(c->a[4]);
    c->a[1] = 0xFFFFF700u;
    unpack_blocks();
}

/* $A1C4: a2's zero runs expanded to ZERO_RUNS (a2 left there): a count, a mask's length, the
 * mask (a bit set for a 0) and the bytes */
static void expand_zero_runs(void)
{
    c->a[1] = ZERO_RUNS;
    c->d[1] = 0;
    c->d[1] = set_b(c->d[1], next8(2));
    c->d[2] = 0;
    move_b(2, next8(2));
    c->a[0] = c->a[2] + sx(c->d[2]);
    while (dbra(2)) {
        c->d[0] = 7;
        move_b(3, next8(2));
        do {
            c->d[4] = 0; F_L(0);
            c->d[3] = set_b(c->d[3], add_b(&c->ccr, c->d[3], c->d[3]));
            if (!(c->ccr & CC_C)) move_b(4, next8(0));
            wr8(c->a[1]++, (uint8_t)c->d[4]); F_B(c->d[4]);
            c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 1, c->d[1]));
            if (!(c->d[1] & 0xFFFF)) goto done;
        } while (dbra(0));
    }
    while (dbra(1)) { uint8_t v = next8(0); wr8(c->a[1]++, v); F_B(v); }
done:
    c->a[2] = ZERO_RUNS;
}

/* $A068: the map of layer a3 (its record). Its map ($22, 64 bytes) gets a chunk number for each
 * place used (-1 the rest); each chunk's 192 cells go to $FF5000 + $180 a chunk (the layers'
 * chunks numbered from 0 or $10). A chunk: runs of one block (a count of runs, each a
 * block and the bitmap of the cells it fills, or zero runs first), then the cells left. a3 ends
 * as the word it had, sign-extended (pushed and popped as a word) */
static void unpack_layer_map(void)
{
    uint16_t a3_word = (uint16_t)c->a[3];
    c->a[7] -= 2; wr16(c->a[7], a3_word);           /* move.w a3,-(a7); link a6,#-10 */
    c->a[7] -= 4; wr32(c->a[7], c->a[6]); c->a[6] = c->a[7]; c->a[7] -= 10;
    uint8_t chunk_no, runs;
    uint16_t map;
    uint32_t chunk_at;
    move_b(0, rd8(c->a[3]));
    and_w(0, 1);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    chunk_no = (uint8_t)c->d[0]; F_B(chunk_no);
    c->a[0] = sx(rd16(c->a[3] + 0x22));
    map = (uint16_t)c->a[0];
    c->d[1] = 15;
    c->d[0] = 0xFFFFFFFFu; F_L(c->d[0]);
    do { wr32(c->a[0], c->d[0]); c->a[0] += 4; F_L(c->d[0]); } while (dbra(1));
    c->a[4] = rd32(c->a[3] + 0x34);
    c->a[1] = rd32(c->a[3] + 0x1C);
    uint32_t a3 = c->a[3];
    unpack_room_blocks();
    c->a[3] = a3;
    c->d[0] = set_w(c->d[0], rd16(c->a[4] + 2)); F_W(c->d[0]);
    c->a[3] = c->a[4] + sx(c->d[0]);
    c->a[2] = c->a[4] + 4;
    uint32_t saved_a2;
next_place:                                         /* $A0A8: a place on the map and its chunk */
    move_b(0, next8(2));
    if (c->d[0] & 0x80) {
        c->a[7] = c->a[6]; c->a[6] = rd32(c->a[7]); c->a[7] += 4;   /* unlk a6 */
        c->a[3] = sx(rd16(c->a[7])); c->a[7] += 2;
        return;
    }
    c->d[0] = set_w(c->d[0], (uint32_t)(int32_t)(int8_t)c->d[0]); F_W(c->d[0]);
    c->a[0] = sx(c->d[0]);
    c->a[0] += sx(map);
    c->d[0] = 0;
    move_b(0, chunk_no);
    wr8(c->a[0], chunk_no); F_B(chunk_no);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->d[0] = set_b(c->d[0], add_b(&c->ccr, chunk_no, c->d[0]));
    chunk_no = (uint8_t)add_b(&c->ccr, 1, chunk_no);
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 7, 16));
    c->d[0] = (uint32_t)add_l(&c->ccr, 0xFF5000, c->d[0]);
    chunk_at = c->d[0];
    c->a[0] = FILLED;
    c->d[0] = 0;
    c->d[1] = 5;
    do { wr32(c->a[0], 0); c->a[0] += 4; F_L(0); } while (dbra(1));
    c->d[0] = 0;
    move_b(0, next8(2));
    saved_a2 = c->a[2];
    c->d[0] = set_w(c->d[0], rd16(c->a[3] + sx(c->d[0]))); F_W(c->d[0]);
    c->a[2] = c->a[4] + sx(c->d[0]);
next_runs:                                          /* $A0F4 */
    c->d[5] = 0xFFFFFFC0u;                          /* -64: 192 cells to fill, counted to 0 */
    runs = next8(2); F_B(runs);
    if (!runs) goto rest;
    if (runs & 0x80) { expand_zero_runs(); goto next_runs; }
    goto run;
run_again:                                          /* $A106 */
    F_B(c->d[5]);
    if (!(c->d[5] & 0xFF)) goto put;
run:                                                /* $A10C: a run: a block over the cells of a bitmap */
    c->a[1] = chunk_at;
    move_b(2, next8(2));
    c->d[2] = set_w(c->d[2], lsl_n(&c->ccr, c->d[2], 8, 16));
    move_b(2, next8(2));
    c->d[4] = set_w(c->d[4], 0x17);
    c->a[0] = FILLED;
    c->d[0] = 1;
    c->d[1] = 0; F_L(0);
    for (;;) {
        f_bit(rd8(c->a[0]) >> (c->d[1] & 7) & 1);
        if (!(c->ccr & CC_Z)) goto skip;            /* written already: not in the bitmap */
        c->d[0] = set_b(c->d[0], sub_b(&c->ccr, 1, c->d[0]));
        if (!(c->d[0] & 0xFF)) { c->d[0] = 8; move_b(3, next8(2)); }
        c->d[3] = set_b(c->d[3], lsr_n(&c->ccr, c->d[3], 1, 8));
        if (c->ccr & CC_C) {
            wr16(c->a[1], (uint16_t)c->d[2]); F_W(c->d[2]);
            f_bit(rd8(c->a[0]) >> (c->d[1] & 7) & 1);
            wr8(c->a[0], rd8(c->a[0]) | 1 << (c->d[1] & 7));
            c->d[5] = set_b(c->d[5], sub_b(&c->ccr, 1, c->d[5]));
        }
    skip:
        c->a[1] += 2;
        c->d[1] = set_b(c->d[1], add_b(&c->ccr, 1, c->d[1]));
        f_bit(c->d[1] >> 3 & 1); c->d[1] &= ~8u;
        if (c->ccr & CC_Z) continue;
        c->a[0] += 1;
        if (!dbra(4)) break;
    }
    runs = (uint8_t)sub_b(&c->ccr, 1, runs);
    if (runs) goto run_again;
    F_B(c->d[5]);
    if (!(c->d[5] & 0xFF)) goto chunk_done;
rest:                                               /* $A152: the cells left, 10-bit numbers */
    c->d[3] = 0;
    c->d[4] = 0;
    c->a[0] = FILLED;
    c->a[1] = chunk_at;
cell:                                               /* $A15E */
    f_bit(rd8(c->a[0]) >> (c->d[3] & 7) & 1);
    if (!(c->ccr & CC_Z)) goto next_cell;
    switch (c->d[4] & 0xFFFF) {                     /* jmp tbl(pc,d4.w): where in the five bytes */
    case 0:
        c->d[0] = 0;
        move_b(0, next8(2));
        move_b(1, next8(2));
        c->d[1] = set_b(c->d[1], lsl_n(&c->ccr, c->d[1], 1, 8));
        c->d[0] = set_w(c->d[0], roxl_w(&c->ccr, c->d[0], 1));
        c->d[1] = set_b(c->d[1], lsl_n(&c->ccr, c->d[1], 1, 8));
        c->d[0] = set_w(c->d[0], roxl_w(&c->ccr, c->d[0], 1));
        break;
    case 2:
        c->d[0] = 0;
        move_b(0, (uint8_t)c->d[1]);
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
        move_b(1, rd8(c->a[2]));
        c->d[1] = set_b(c->d[1], lsr_n(&c->ccr, c->d[1], 4, 8));
        or_b(0, c->d[1]);
        break;
    case 4:
        c->d[0] = 0;
        move_b(0, next8(2));
        c->d[0] = set_b(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 8));
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
        move_b(1, rd8(c->a[2]));
        c->d[1] = set_b(c->d[1], lsr_n(&c->ccr, c->d[1], 2, 8));
        or_b(0, c->d[1]);
        break;
    default:                                        /* 6 */
        move_b(0, next8(2));
        c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 8, 16));
        and_w(0, 0x300);
        move_b(0, next8(2));
        break;
    }
put:                                                /* $A1A4 */
    wr16(c->a[1], (uint16_t)c->d[0]); F_W(c->d[0]);
    c->d[4] = set_w(c->d[4], add_w(&c->ccr, 2, c->d[4]));
    and_w(4, 6);
    c->d[5] = set_b(c->d[5], sub_b(&c->ccr, 1, c->d[5]));
    if (!(c->d[5] & 0xFF)) goto chunk_done;
next_cell:                                          /* $A1B0 */
    c->a[1] += 2;
    c->d[3] = set_b(c->d[3], add_b(&c->ccr, 1, c->d[3]));
    f_bit(c->d[3] >> 3 & 1); c->d[3] &= ~8u;
    if (c->ccr & CC_Z) goto cell;
    c->a[0] += 1;
    goto cell;
chunk_done:                                         /* $A1BE */
    c->a[2] = saved_a2;
    goto next_place;
}

#define ENTRY(name, fn) void name(Cpu *cpu) { c = cpu; fn(); c->ret = 0; }
ENTRY(UnpackBlocks, unpack_blocks)             /* $9F6A */
ENTRY(UnpackRoomBlocks, unpack_room_blocks)    /* $9F64 */
ENTRY(LoadSharedBlocks, load_shared_blocks)    /* $9F46 */
ENTRY(ExpandZeroRuns, expand_zero_runs)        /* $A1C4 */
ENTRY(UnpackLayerMap, unpack_layer_map)        /* $A068 */

/* ---- batch T12: loading a room ----
 * A room is a list of commands (by area $9668 and room $9669, from the directory's table 8):
 * its layers, graphics, palettes, music, doors and things, ended by $FF. LoadRoom ($9330)
 * resets the room's state, runs them, and finishes the graphics and music. */

#define AREA          0xFFFF9668u   /* byte */
#define ROOM          0xFFFF9669u   /* byte */
#define LAYER_A       0xFFFF9754u   /* the layers' records */
#define LAYER_B       0xFFFF97D4u
#define ROOM_COMMANDS 0x9442u       /* the commands' offsets */
#define TEST_FLAG     0x274Cu       /* TestGameFlag: d0 the flag (ne: set) */
#define SET_GFX_SLOTS 0x234Cu
#define LOAD_PALETTES 0x135Cu
#define STREAM_GFX    0x22F0u
#define OWNS          0x23E4u

static void call(uint32_t addr) { call_68k(c, addr); }
static uint32_t sub_l(uint16_t *ccr, uint32_t s, uint32_t d)   /* sub.l / subi.l */
{
    uint32_t r = d - s;
    int v = ((s ^ d) & (r ^ d) & 0x80000000u) != 0;
    *ccr = (uint16_t)((r & 0x80000000u ? CC_N : 0) | (r ? 0 : CC_Z) | (v ? CC_V : 0) | (s > d ? CC_C | CC_X : 0));
    return r;
}
static void cmp_byte(uint32_t s, uint32_t d) { uint16_t x = c->ccr & CC_X; sub_b(&c->ccr, s, d); c->ccr = (uint16_t)((c->ccr & ~CC_X) | x); }
static uint16_t next16(int r) { uint16_t v = rd16(c->a[r]); c->a[r] += 2; return v; }   /* (an)+, a word */
static uint32_t next32(int r) { uint32_t v = rd32(c->a[r]); c->a[r] += 4; return v; }
static void put8(uint32_t a, uint32_t v) { wr8(a, (uint8_t)v); F_B(v); }                /* move.b to memory */
static void put16m(uint32_t a, uint32_t v) { wr16(a, (uint16_t)v); F_W(v); }
static void put32m(uint32_t a, uint32_t v) { wr32(a, v); F_L(v); }
static void push32(uint32_t v) { c->a[7] -= 4; wr32(c->a[7], v); }
static uint32_t pop32(void) { uint32_t v = rd32(c->a[7]); c->a[7] += 4; return v; }
/* a call with a1-a2 kept on the stack (movem.l a1-a2,-(a7) ... (a7)+,a1-a2) */
static void call_keep_a1a2(uint32_t addr)
{
    c->a[7] -= 8; wr32(c->a[7], c->a[1]); wr32(c->a[7] + 4, c->a[2]);
    call(addr);
    c->a[1] = rd32(c->a[7]); c->a[2] = rd32(c->a[7] + 4); c->a[7] += 8;
}

/* $9622: layer a3's record: its buffers ($FFE500, $900 apart) and plane's fields by layer
 * ($965E for A, $966E for B) */
static void init_layer(void)
{
    c->a[0] = 0xFFFFE500u;
    put32m(c->a[3] + 0x10, c->a[0]);
    c->a[0] += 0x900;
    put32m(c->a[3] + 0x14, c->a[0]);
    c->a[0] += 0x900;
    put32m(c->a[3] + 0x18, c->a[0]);
    c->a[0] = 0x965Eu;
    f_bit(rd8(c->a[3]) & 1);
    if (rd8(c->a[3]) & 1) c->a[0] = 0x966Eu;
    put16m(c->a[3] + 0x20, next16(0));
    put16m(c->a[3] + 0x22, next16(0));
    put32m(c->a[3] + 0x1C, next32(0));
    put32m(c->a[3] + 0x28, next32(0));
    put32m(c->a[3] + 0x2C, next32(0));
}

/* $967E: both layers' records, the base graphics, the planes' scroll kept */
static void init_layers(void)
{
    wr8(0xFFFF9858u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    c->a[0] = 0x96BCu; call(SET_GFX_SLOTS);
    c->a[3] = LAYER_A; put8(c->a[3], 0); init_layer();
    c->a[3] = LAYER_B; put8(c->a[3], 1); init_layer();
    c->d[0] = rd32(0xFFFFC800u); F_L(c->d[0]);
    c->d[0] = sub_l(&c->ccr, 0x800000, c->d[0]);
    put32m(0xFFFF972Cu, c->d[0]);
    c->d[0] = rd32(0xFFFFC900u); F_L(c->d[0]);
    c->d[0] = sub_l(&c->ccr, 0x600000, c->d[0]);
    put32m(0xFFFF9730u, c->d[0]);
}

/* $946C: a layer command's record (d0 bit 1: B): its kind (d0), width and height in screens
 * ($38/$39, the limits $44/$46) and a word ($3A) */
static void setup_layer(void)
{
    c->a[3] = LAYER_A;
    and_w(0, 2);
    if (c->d[0] & 0xFFFF) c->a[3] = LAYER_B;
    move_b(0, next8(2));
    c->d[1] = 0;
    c->d[1] = set_b(c->d[1], next8(2));
    put8(c->a[3] + 0x38, c->d[1]);
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 4, 16));
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 1, c->d[1]));
    put16m(c->a[3] + 0x44, c->d[1]);
    c->d[1] = 0;
    c->d[1] = set_b(c->d[1], next8(2));
    put8(c->a[3] + 0x39, c->d[1]);
    c->d[1] = set_b(c->d[1], add_b(&c->ccr, c->d[1], c->d[1]));
    c->d[1] = set_b(c->d[1], add_b(&c->ccr, rd8(c->a[3] + 0x39), c->d[1]));
    c->d[1] = set_w(c->d[1], lsl_n(&c->ccr, c->d[1], 8, 16));
    c->d[1] = set_w(c->d[1], sub_w(&c->ccr, 0x40, c->d[1]));
    put16m(c->a[3] + 0x46, c->d[1]);
    put16m(c->a[3] + 0x3A, next16(2));
}

/* $9500: a pointer in the commands: a byte, then a long (if it is not 0) or an offset from the
 * room's list (a1) */
static void read_room_pointer(void)
{
    uint8_t v = next8(2); F_B(v);
    if (v) { c->a[0] = next32(2); return; }
    c->d[0] = set_w(c->d[0], next16(2)); F_W(c->d[0]);
    c->a[0] = c->a[1] + sx(c->d[0]);
}

/* $94B8: the layer's data (a4), kind and its record's long from $94CC */
static void layer_data(void)
{
    put32m(c->a[3] + 0x34, c->a[4]);
    put8(c->a[3] + 1, c->d[0]);
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    put32m(c->a[3] + 0x30, rd32(0x94CCu + sx(c->d[0])));
}

/* the commands, by their handlers' addresses */
static void cmd_layer(void)        /* $94AA, 0-1: a layer, its data at an offset */
{
    setup_layer();
    c->d[1] = set_w(c->d[1], next16(2)); F_W(c->d[1]);
    c->a[4] = c->a[1] + sx(c->d[1]);
    layer_data();
}
static void cmd_layer_at(void)     /* $94B4, 16-17: a layer, its data at an address */
{
    setup_layer();
    c->a[4] = next32(2);
    layer_data();
}
static void cmd_gfx(void)          /* $9510, 2: the graphics */
{
    read_room_pointer();
    put32m(0xFFFF9672u, c->a[0]);
    c->a[3] = c->a[1];
    call(SET_GFX_SLOTS);
    c->a[1] = c->a[3];
}
static void cmd_palettes(void)     /* $9520, 3 */
{
    put8(0xFFFF9670u, next8(2));
    put16m(0xFFFF966Cu, next16(2));
}
static void cmd_palette2(void) { put8(0xFFFF9671u, next8(2)); }          /* $952A, 10 */
static void cmd_4(void) { read_room_pointer(); call_keep_a1a2(0x23A6); }  /* $9530, 4 */
static void cmd_5(void) { read_room_pointer(); put32m(0xFFFF996Cu, c->a[0]); }   /* $9540 */
static void cmd_6(void) { put8(0xFFFF966Bu, next8(2)); }                  /* $9548: under water */
static void cmd_7(void)            /* $954E, 7 */
{
    c->d[5] = 0;
    c->d[5] = set_b(c->d[5], next8(2));
    c->d[4] = set_w(c->d[4], next16(2));
    c->d[6] = 0;
    c->d[6] = set_b(c->d[6], next8(2));
    c->d[6] = set_w(c->d[6], lsl_n(&c->ccr, c->d[6], 4, 16));
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 0xF00, c->d[6]));
    c->d[7] = 0;
    c->d[7] = set_b(c->d[7], next8(2));
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 4, 16));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 0xF00, c->d[7]));
    call_keep_a1a2(0x11BF0);
}
static void cmd_8(void)            /* $9578, 8: as $9500, kept at $A11E */
{
    uint8_t v = next8(2); F_B(v);
    if (v) c->a[0] = next32(2);
    else { c->d[0] = set_w(c->d[0], next16(2)); F_W(c->d[0]); c->a[0] = c->a[1] + sx(c->d[0]); }
    put32m(0xFFFFA11Eu, c->a[0]);
}
static void cmd_9(void) { put8(0xFFFFA0E0u, next8(2)); }                  /* $958C */
static void cmd_11(void)           /* $9592, 11 */
{
    read_room_pointer();
    c->d[6] = 0;
    c->d[6] = set_b(c->d[6], next8(2));
    c->d[6] = set_w(c->d[6], add_w(&c->ccr, 0x100, c->d[6]));
    c->d[7] = 0;
    c->d[7] = set_b(c->d[7], next8(2));
    c->d[7] = set_w(c->d[7], lsl_n(&c->ccr, c->d[7], 6, 16));
    c->d[7] = set_w(c->d[7], add_w(&c->ccr, 0x4000, c->d[7]));
    call(0x2974);
}
static void cmd_12(void)           /* $95AC, 12 */
{
    c->d[0] = 0;
    move_b(0, next8(2));
    call_keep_a1a2(0x5658);
}
static void cmd_13(void)           /* $95C0, 13 */
{
    c->d[0] = 0;
    move_b(0, next8(2));
    put16m(0xFFFF9682u, c->d[0]);
}
static void cmd_14(void) { put8(0xFFFF9685u, next8(2)); }                 /* $95CA */
static void cmd_15(void)           /* $95D0, 15 */
{
    put8(0xFFFF9688u, next8(2));
    put16m(0xFFFF9686u, next16(2));
}
static void cmd_18(void)           /* $95DA, 18 */
{
    c->d[0] = 0;
    move_b(0, next8(2));
    put16m(0xFFFF968Au, c->d[0]);
    cmp_w(&c->ccr, rd16(0xFFFF9F32u), c->d[0]);
    if ((c->d[0] & 0xFFFF) != rd16(0xFFFF9F32u)) call(0xF880);
}
static void cmd_19(void)           /* $95EE, 19 */
{
    c->a[2] += 1;
    push32(c->a[1]); F_L(c->a[1]);
    c->a[0] = c->a[2];
    call(0xB49C);
    c->a[2] += 8;
    c->a[1] = pop32();
}
static void cmd_20(void)           /* $95FE, 20: a thing in a free slot ($11CE8) */
{
    call(0x11CE8);
    if (c->ccr & CC_N) return;
    put8(c->a[1] - 0x6072, 0x80);
    put8(c->a[1] - 0x6071, next8(2));
    put32m(c->a[1] - 0x601E, next32(2));
    put32m(c->a[1] - 0x5F76, next32(2));
    wr32(c->a[1] - 0x5FCA, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    wr8(c->a[1] - 0x6070, 0xFF);
}

static void room_command(uint32_t at)
{
    switch (at) {
    case 0x94AA: cmd_layer(); break;
    case 0x94B4: cmd_layer_at(); break;
    case 0x9510: cmd_gfx(); break;
    case 0x9520: cmd_palettes(); break;
    case 0x952A: cmd_palette2(); break;
    case 0x9530: cmd_4(); break;
    case 0x9540: cmd_5(); break;
    case 0x9548: cmd_6(); break;
    case 0x954E: cmd_7(); break;
    case 0x9578: cmd_8(); break;
    case 0x958C: cmd_9(); break;
    case 0x9592: cmd_11(); break;
    case 0x95AC: cmd_12(); break;
    case 0x95C0: cmd_13(); break;
    case 0x95CA: cmd_14(); break;
    case 0x95D0: cmd_15(); break;
    case 0x95DA: cmd_18(); break;
    case 0x95EE: cmd_19(); break;
    case 0x95FE: cmd_20(); break;
    default: call(at); break;                       /* past the table: as the original jumps */
    }
}

/* $9330: the room: its state reset, its commands run; then the things' graphics, and the music
 * and palette by the game's flags */
static void load_room(void)
{
    init_layers();
    call(0x11BBC);
    call(0x289A);
    call(0x29A4);
    call(0xB4FA);
    call(0xB852);
    c->d[0] = 0; F_L(0);
    wr8(0xFFFF966Bu, 0); wr32(0xFFFF996Cu, 0); wr8(0xFFFF9966u, 0); wr16(0xFFFF9962u, 0);
    wr32(0xFFFFA11Eu, 0); wr8(0xFFFFA0E0u, 0); wr16(0xFFFF966Cu, 0); wr8(0xFFFF9685u, 0);
    wr16(0xFFFF9980u, 0); wr32(0xFFFF9734u, 0); wr32(0xFFFF9738u, 0);
    put32m(0xFFFF966Eu, 0x10000);
    wr8(0xFFFF9854u, 0);
    put16m(0xFFFF9EDAu, 0x1FF);
    wr16(0xFFFF974Au, 0); wr8(0xFFFF9BB6u, 0); wr8(0xFFFF9667u, 0); wr16(0xFFFF9994u, 0); F_L(0);
    move_b(1, rd8(AREA));
    cmp_byte(rd8(0xFFFF9684u), c->d[1]);
    if ((c->d[1] & 0xFF) != rd8(0xFFFF9684u)) { put8(0xFFFF9684u, c->d[1]); put16m(0xFFFF9992u, c->d[0]); }
    c->d[0] = 0xFFFFFFFFu;
    wr16(0xFFFF9682u, 0xFFFF); wr16(0xFFFF968Au, 0xFFFF); wr16(0xFFFF9686u, 0xFFFF);
    wr16(0xFFFF9748u, 0xFFFF); wr16(0xFFFF9C18u, 0xFFFF);
    c->d[0] = 0;
    move_b(0, rd8(AREA));
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 2, 16));
    c->d[0] = add_l(&c->ccr, DATA_DIR + 0x20, c->d[0]);
    c->a[1] = rd32(c->d[0]);
    c->d[0] = 0;
    move_b(0, rd8(ROOM));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->d[0] = set_w(c->d[0], rd16(c->a[1] + sx(c->d[0]))); F_W(c->d[0]);
    c->a[2] = c->a[1] + sx(c->d[0]);
    for (;;) {
        c->d[0] = 0;
        move_b(0, next8(2));
        cmp_byte(0xFF, c->d[0]);
        if ((c->d[0] & 0xFF) == 0xFF) break;
        c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
        c->a[0] = ROOM_COMMANDS + sx(rd16(ROOM_COMMANDS + sx(c->d[0])));
        c->a[7] -= 4; wr32(c->a[7], 0x93FA);    /* jsr (a0) */
        room_command(c->a[0]);
        c->a[7] += 4;
    }
    call(0xF5FC);
    call(0x137C8);
    call(STREAM_GFX);
    F_B(rd8(0xFFFF9685u));
    if (rd8(0xFFFF9685u)) {
        c->d[0] = 49; F_L(49);
        call(OWNS);
        if (c->ccr & CC_Z) put8(0xFFFF9670u, 0x76);
    }
    F_W(rd16(0xFFFF9686u));
    if (rd16(0xFFFF9686u) & 0x8000) return;
    move_b(0, rd8(0xFFFF9688u));
    call(TEST_FLAG);
    if (!(c->ccr & CC_Z)) { wr8(0xFFFF9686u, 0xFF); return; }
    put8(0xFFFF9689u, rd8(0xFFFF9671u));
    put8(0xFFFF9671u, 0x76);
}

/* $96D6: one of the ending's scenes (d0, from the directory's table 7): its two layers' data,
 * a palette, the graphics, and both maps */
static void load_scene(void)
{
    c->d[0] = set_w(c->d[0], lsl_n(&c->ccr, c->d[0], 4, 16));
    c->a[0] = rd32(DATA_DIR + 0x1C);
    c->a[0] += sx(c->d[0]);
    push32(c->a[0]); F_L(c->a[0]);
    init_layers();
    wr32(0xFFFF9758u, 0); c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z);
    c->a[1] = pop32();
    put32m(0xFFFF9788u, next32(1));
    put32m(0xFFFF9808u, next32(1));
    c->a[0] = 0xFFFF966Au;
    c->a[0] -= 2; put16m(c->a[0], next16(1));
    c->a[0] -= 2; put16m(c->a[0], 1);
    push32(c->a[1]); F_L(c->a[1]);
    call(LOAD_PALETTES);
    c->a[0] = pop32();
    put16m(0xFFFF8B56u, next16(0));
    c->a[0] = rd32(c->a[0]);
    call(SET_GFX_SLOTS);
    c->a[3] = LAYER_A;
    c->a[7] -= 4; wr32(c->a[7], 0x9718); unpack_layer_map(); c->a[7] += 4;
    c->a[3] = LAYER_B;
    unpack_layer_map();
}

/* $92D4: the room's music ($9660), from the directory's table 3 by area and room: $00-$7F and
 * $F0-$FF as they are; $EF by $DAC8 (85 or 15); others pick by a game flag from the pairs at $932A */
static void room_music(void)
{
    c->a[0] = rd32(DATA_DIR + 0xC);
    c->d[0] = 0;
    move_b(0, rd8(AREA));
    c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
    c->a[0] += sx(rd16(c->a[0] + sx(c->d[0])));
    move_b(0, rd8(ROOM));
    move_b(0, rd8(c->a[0] + sx(c->d[0])));
    if (c->d[0] & 0x80) {
        cmp_byte(0xF0, c->d[0]);
        if ((c->d[0] & 0xFF) < 0xF0) {
            cmp_byte(0xEF, c->d[0]);
            if ((c->d[0] & 0xFF) >= 0xEF) {
                call(0xDAC8);
                c->d[0] = c->ccr & CC_Z ? 15 : 85; F_L(c->d[0]);
            } else {
                and_w(0, 0x7F);
                c->d[1] = set_w(c->d[1], c->d[0]); F_W(c->d[0]);
                c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[0], c->d[0]));
                c->d[0] = set_w(c->d[0], add_w(&c->ccr, c->d[1], c->d[0]));
                c->a[1] = 0x932Au + sx(c->d[0]);
                move_b(0, next8(1));
                call(TEST_FLAG);
                if (!(c->ccr & CC_Z)) c->a[1] += 1;
                move_b(0, rd8(c->a[1]));
            }
        }
    }
    put8(0xFFFF9660u, c->d[0]);
}

ENTRY(LoadRoom, load_room)                /* $9330 */
ENTRY(InitLayers, init_layers)            /* $967E */
ENTRY(InitLayer, init_layer)              /* $9622 */
ENTRY(SetupLayer, setup_layer)            /* $946C */
ENTRY(ReadRoomPointer, read_room_pointer) /* $9500 */
ENTRY(LoadScene, load_scene)              /* $96D6 */
ENTRY(RoomMusic, room_music)              /* $92D4 */
ENTRY(RoomLayer, cmd_layer)               /* $94AA */
ENTRY(RoomLayerAt, cmd_layer_at)          /* $94B4 */
ENTRY(RoomGfx, cmd_gfx)                   /* $9510 */
ENTRY(RoomPalettes, cmd_palettes)         /* $9520 */
ENTRY(RoomPalette2, cmd_palette2)         /* $952A */
ENTRY(RoomCmd4, cmd_4)                    /* $9530 */
ENTRY(RoomCmd5, cmd_5)                    /* $9540 */
ENTRY(RoomCmd6, cmd_6)                    /* $9548 */
ENTRY(RoomCmd7, cmd_7)                    /* $954E */
ENTRY(RoomCmd8, cmd_8)                    /* $9578 */
ENTRY(RoomCmd9, cmd_9)                    /* $958C */
ENTRY(RoomCmd11, cmd_11)                  /* $9592 */
ENTRY(RoomCmd12, cmd_12)                  /* $95AC */
ENTRY(RoomCmd13, cmd_13)                  /* $95C0 */
ENTRY(RoomCmd14, cmd_14)                  /* $95CA */
ENTRY(RoomCmd15, cmd_15)                  /* $95D0 */
ENTRY(RoomCmd18, cmd_18)                  /* $95DA */
ENTRY(RoomCmd19, cmd_19)                  /* $95EE */
ENTRY(RoomCmd20, cmd_20)                  /* $95FE */
